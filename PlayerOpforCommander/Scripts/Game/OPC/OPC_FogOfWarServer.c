//------------------------------------------------------------------------------------------------
//! Server-side sighting bookkeeping for one hidden (player) faction.
class OPC_FogOfWarSightings
{
	string m_sFactionKey;       //!< key of the HIDDEN faction; spotters are all AI hostile to it
	Faction m_HiddenFaction;    //!< resolved once, re-resolved lazily if the faction manager wasn't ready

	//! RplId of a spotted entity -> world time (seconds) it was last seen/detected by any spotter AI.
	//! Keyed by RplId and not by IEntity on purpose: an IEntity key nulls out when the entity is
	//! destroyed, and Remove(null) can no longer find the slot it was hashed under, so every casualty
	//! would leak one map entry for the rest of the mission.
	ref map<RplId, float> m_mLastSeen = new map<RplId, float>();

	//! Cached result of the last poll, sent to subscribed commanders
	ref array<RplId> m_aRevealedIds = {};

	//! Bumped every time the revealed set actually changes. A subscriber is only sent an update when
	//! its acknowledged revision is behind this one, so a static front costs no traffic at all.
	int m_iRevision = 1;

	//! Set when an entity enters or leaves m_mLastSeen (a moved timestamp is not a change)
	bool m_bMembershipChanged;

	//------------------------------------------------------------------------------------------------
	void OPC_FogOfWarSightings(string factionKey, Faction hiddenFaction)
	{
		m_sFactionKey = factionKey;
		m_HiddenFaction = hiddenFaction;
	}
}

//------------------------------------------------------------------------------------------------
//! Server-side fog of war: for each hidden (player) faction, polls the perception of every AI that
//! is hostile to it and sends the list of currently spotted entities to subscribed commanders
//! (via SCR_PlayerController).
//! Owned by the modded SCR_BaseGameMode.
class OPC_FogOfWarServer
{
	protected float m_fRevealTimeout = 30;
	protected float m_fUpdateInterval = 1;
	protected bool m_bPlayersCanSpot = false;
	protected string m_sDefaultFactionKey = "US";
	protected float m_fContactReportCooldown = 120;

	//! playerId -> faction key this commander is subscribed to
	protected ref map<int, string> m_mSubscribers = new map<int, string>();
	//! playerId -> sightings revision this commander has already been sent (0 = force a full update)
	protected ref map<int, int> m_mSentRevision = new map<int, int>();
	//! faction key -> sightings
	protected ref map<string, ref OPC_FogOfWarSightings> m_mSightings = new map<string, ref OPC_FogOfWarSightings>();

	protected bool m_bPolling;

	//! Vanilla perception code only ever queries DETECTED and ENEMY (see SCR_AIGroupPerception).
	//! ETargetCategory.UNKNOWN is the enum's zero value rather than a queryable bucket, so asking for it
	//! cost a third of the perception queries for nothing.
	protected static const ref array<ETargetCategory> TARGET_CATEGORIES = {ETargetCategory.DETECTED, ETargetCategory.ENEMY};

	//--- Scratch, reused between polls so the 1 Hz loop does not allocate
	protected ref array<AIAgent> m_aAgents = {};
	protected ref array<BaseTarget> m_aTargets = {};
	protected ref array<IEntity> m_aOccupants = {};
	protected ref array<IEntity> m_aSpotted = {};
	protected ref array<OPC_FogOfWarSightings> m_aActiveSightings = {};
	protected ref array<OPC_FogOfWarSightings> m_aRelevantSightings = {};

	//------------------------------------------------------------------------------------------------
	//! Entity -> RplId.
	//! NOTE: Replication.FindItemId(entity) does NOT work here. An entity is only registered as a
	//! replication item when the entity class itself has a non-empty replication layout; plain
	//! characters and vehicles do not, their RplComponent is the head item of the node. Every vanilla
	//! entity -> RplId conversion goes through a component for the same reason - see
	//! SCR_PossessingManagerComponent, SCR_SpawnPoint and SCR_BaseActionsEditorComponent.
	static RplId GetEntityRplId(notnull IEntity entity)
	{
		RplComponent rpl = RplComponent.Cast(entity.FindComponent(RplComponent));
		if (rpl)
			return rpl.Id();

		return RplId.Invalid();
	}

	//------------------------------------------------------------------------------------------------
	void Configure(string defaultFactionKey, float revealTimeout, float updateInterval, bool playersCanSpot, float contactReportCooldown)
	{
		if (contactReportCooldown >= 0)
			m_fContactReportCooldown = contactReportCooldown;

		if (!defaultFactionKey.IsEmpty())
			m_sDefaultFactionKey = defaultFactionKey;

		if (revealTimeout > 0)
			m_fRevealTimeout = revealTimeout;

		if (updateInterval > 0)
			m_fUpdateInterval = updateInterval;

		m_bPlayersCanSpot = playersCanSpot;

		// Restart timer with new interval if already running
		if (m_bPolling)
		{
			StopPolling();
			StartPolling();
		}
	}

	//------------------------------------------------------------------------------------------------
	string GetDefaultFactionKey()
	{
		return m_sDefaultFactionKey;
	}

	//------------------------------------------------------------------------------------------------
	float GetRevealTimeout()
	{
		return m_fRevealTimeout;
	}

	//------------------------------------------------------------------------------------------------
	//! Seconds before the same contact can be reported again. 0 disables contact reports.
	float GetContactReportCooldown()
	{
		return m_fContactReportCooldown;
	}

	//------------------------------------------------------------------------------------------------
	//! Subscribe / unsubscribe a commander. factionKey = the faction to HIDE (empty = server default).
	void SetSubscribed(int playerId, bool subscribe, string factionKey = string.Empty)
	{
		if (!subscribe)
		{
			m_mSubscribers.Remove(playerId);
			m_mSentRevision.Remove(playerId);
			CleanupSightings();
			if (m_mSubscribers.IsEmpty())
				StopPolling();

			return;
		}

		// Validate requested key (must be a known faction), otherwise use server default
		FactionManager factionManager = GetGame().GetFactionManager();
		Faction hiddenFaction;
		if (factionManager && !factionKey.IsEmpty())
			hiddenFaction = factionManager.GetFactionByKey(factionKey);

		if (!hiddenFaction)
		{
			factionKey = m_sDefaultFactionKey;
			if (factionManager)
				hiddenFaction = factionManager.GetFactionByKey(factionKey);
		}

		m_mSubscribers.Set(playerId, factionKey);

		// A commander cycling factions replaces their entry above; without this the faction they just
		// left keeps its sightings set, and Poll() iterates m_mSightings rather than m_mSubscribers, so
		// it would keep costing a full AI sweep every tick for the rest of the mission.
		CleanupSightings();

		if (!m_mSightings.Contains(factionKey))
			m_mSightings.Insert(factionKey, new OPC_FogOfWarSightings(factionKey, hiddenFaction));

		// Revision 0 is never produced, so this forces one full update even if nothing has changed
		m_mSentRevision.Set(playerId, 0);

		// Confirm to the client with the effective config
		SCR_PlayerController pc = GetPlayerController(playerId);
		if (pc)
			pc.OPC_SendConfig(factionKey, m_fRevealTimeout, m_fContactReportCooldown);

		StartPolling();

		// Send an immediate update so the commander doesn't wait a full interval
		Poll();
	}

	//------------------------------------------------------------------------------------------------
	void OnPlayerDisconnected(int playerId)
	{
		SetSubscribed(playerId, false);
	}

	//------------------------------------------------------------------------------------------------
	protected SCR_PlayerController GetPlayerController(int playerId)
	{
		PlayerManager playerManager = GetGame().GetPlayerManager();
		if (!playerManager)
			return null;

		return SCR_PlayerController.Cast(playerManager.GetPlayerController(playerId));
	}

	//------------------------------------------------------------------------------------------------
	protected void StartPolling()
	{
		if (m_bPolling)
			return;

		m_bPolling = true;
		int delayMs = m_fUpdateInterval * 1000;
		GetGame().GetCallqueue().CallLater(Poll, delayMs, true);
	}

	//------------------------------------------------------------------------------------------------
	protected void StopPolling()
	{
		if (!m_bPolling)
			return;

		m_bPolling = false;
		GetGame().GetCallqueue().Remove(Poll);
	}

	//------------------------------------------------------------------------------------------------
	//! Drop sightings for factions nobody is subscribed to anymore
	protected void CleanupSightings()
	{
		array<string> unused = {};
		foreach (string factionKey, OPC_FogOfWarSightings sightings : m_mSightings)
		{
			bool used = false;
			foreach (int playerId, string subscribedKey : m_mSubscribers)
			{
				if (subscribedKey == factionKey)
				{
					used = true;
					break;
				}
			}
			if (!used)
				unused.Insert(factionKey);
		}

		foreach (string key : unused)
		{
			m_mSightings.Remove(key);
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Main update: gather perception of all AI hostile to the hidden factions, apply the timeout, and
	//! send the result to any commander whose view is out of date.
	protected void Poll()
	{
		if (m_mSubscribers.IsEmpty())
		{
			StopPolling();
			return;
		}

		float now = GetGame().GetWorld().GetWorldTime() * 0.001;

		UpdateAllSightings(now);

		foreach (int playerId, string subscribedKey : m_mSubscribers)
		{
			OPC_FogOfWarSightings sightings;
			if (!m_mSightings.Find(subscribedKey, sightings))
				continue;

			int sentRevision;
			m_mSentRevision.Find(playerId, sentRevision);
			if (sentRevision == sightings.m_iRevision)
				continue;

			SCR_PlayerController pc = GetPlayerController(playerId);
			if (!pc)
				continue;

			pc.OPC_SendRevealed(sightings.m_aRevealedIds);
			m_mSentRevision.Set(playerId, sightings.m_iRevision);
		}
	}

	//------------------------------------------------------------------------------------------------
	//! One pass over every AI agent in the world, bucketing each contact into every sightings set the
	//! spotter is a valid spotter for. The previous version re-walked the whole agent list (and re-ran
	//! three perception queries per agent) once per hidden faction, so two commanders on different
	//! factions doubled the cost of the tick.
	protected void UpdateAllSightings(float now)
	{
		AIWorld aiWorld = GetGame().GetAIWorld();
		FactionManager factionManager = GetGame().GetFactionManager();
		if (!aiWorld || !factionManager)
			return;

		m_aActiveSightings.Clear();
		foreach (string factionKey, OPC_FogOfWarSightings sightings : m_mSightings)
		{
			if (!sightings.m_HiddenFaction)
				sightings.m_HiddenFaction = factionManager.GetFactionByKey(sightings.m_sFactionKey);

			if (sightings.m_HiddenFaction)
				m_aActiveSightings.Insert(sightings);
		}

		if (m_aActiveSightings.IsEmpty())
			return;

		PlayerManager playerManager = GetGame().GetPlayerManager();

		m_aAgents.Clear();
		aiWorld.GetAIAgents(m_aAgents);

		foreach (AIAgent agent : m_aAgents)
		{
			if (!agent || SCR_AIGroup.Cast(agent))
				continue;

			IEntity spotter = agent.GetControlledEntity();
			if (!spotter)
				continue;

			// Faction of the spotter
			Faction spotterFaction;
			SCR_ChimeraCharacter character = SCR_ChimeraCharacter.Cast(spotter);
			if (character)
			{
				spotterFaction = character.GetFaction();
			}
			else
			{
				FactionAffiliationComponent affiliation = FactionAffiliationComponent.Cast(spotter.FindComponent(FactionAffiliationComponent));
				if (affiliation)
					spotterFaction = affiliation.GetAffiliatedFaction();
			}

			if (!spotterFaction)
				continue;

			// Spotters are AI hostile to the hidden faction (the OPFOR the commander is running)
			m_aRelevantSightings.Clear();
			foreach (OPC_FogOfWarSightings candidate : m_aActiveSightings)
			{
				if (spotterFaction == candidate.m_HiddenFaction || !spotterFaction.IsFactionEnemy(candidate.m_HiddenFaction))
					continue;

				m_aRelevantSightings.Insert(candidate);
			}

			if (m_aRelevantSightings.IsEmpty())
				continue;

			// Player-controlled characters still have an AI agent; skip them unless allowed
			if (!m_bPlayersCanSpot && playerManager && playerManager.GetPlayerIdFromControlledEntity(spotter) > 0)
				continue;

			// Dead spotters don't see anything
			if (character)
			{
				CharacterControllerComponent charController = character.GetCharacterController();
				if (charController && charController.IsDead())
					continue;
			}

			PerceptionComponent perception = PerceptionComponent.Cast(spotter.FindComponent(PerceptionComponent));
			if (!perception)
				continue;

			foreach (ETargetCategory category : TARGET_CATEGORIES)
			{
				m_aTargets.Clear();
				perception.GetTargetsList(m_aTargets, category);

				foreach (BaseTarget target : m_aTargets)
				{
					if (!target)
						continue;

					IEntity targetEntity = target.GetTargetEntity();
					if (!targetEntity)
						continue;

					float since = Math.Min(target.GetTimeSinceSeen(), target.GetTimeSinceDetected());
					if (since < 0 || since > m_fRevealTimeout)
						continue;

					float seenAt = now - since;
					Faction targetFaction = target.GetPerceivedFaction();

					// Resolve the contact and everything its detection drags with it once per target,
					// rather than once per subscribed faction
					CollectSpotted(target, targetEntity);

					foreach (OPC_FogOfWarSightings sightings : m_aRelevantSightings)
					{
						// Only the hidden faction (and its allies) matters
						if (targetFaction && targetFaction != sightings.m_HiddenFaction && !sightings.m_HiddenFaction.IsFactionFriendly(targetFaction))
							continue;

						foreach (IEntity spotted : m_aSpotted)
						{
							RegisterSighting(sightings, spotted, seenAt);
						}
					}
				}
			}
		}

		foreach (OPC_FogOfWarSightings sightings : m_aActiveSightings)
		{
			RebuildRevealed(sightings, now);
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Fill m_aSpotted with the contact plus anything its detection implies: the vehicle a spotted
	//! crewman is riding in, and the occupants of a spotted vehicle.
	protected void CollectSpotted(notnull BaseTarget target, notnull IEntity targetEntity)
	{
		m_aSpotted.Clear();
		m_aSpotted.Insert(targetEntity);

		// Character inside a vehicle -> the vehicle is spotted as well
		PerceivableComponent perceivable = target.GetPerceivableComponent();
		if (perceivable && perceivable.IsInCompartment())
		{
			IEntity vehicle = GetVehicleOf(targetEntity);
			if (vehicle)
				m_aSpotted.Insert(vehicle);
		}

		// Vehicle spotted -> its occupants are spotted as well
		if (Vehicle.Cast(targetEntity))
		{
			SCR_BaseCompartmentManagerComponent compartmentManager = SCR_BaseCompartmentManagerComponent.Cast(targetEntity.FindComponent(SCR_BaseCompartmentManagerComponent));
			if (compartmentManager)
			{
				m_aOccupants.Clear();
				compartmentManager.GetOccupants(m_aOccupants);
				foreach (IEntity occupant : m_aOccupants)
				{
					if (occupant)
						m_aSpotted.Insert(occupant);
				}
			}
		}
	}

	//------------------------------------------------------------------------------------------------
	//! CompartmentAccessComponent.GetVehicleIn() returns the compartment owner, which for a turret is a
	//! child entity rather than the vehicle itself. SCR_CompartmentAccessComponent.GetVehicle() walks up
	//! to the real vehicle, so prefer it and keep the engine call as the fallback.
	protected IEntity GetVehicleOf(notnull IEntity character)
	{
		SCR_CompartmentAccessComponent access = SCR_CompartmentAccessComponent.Cast(character.FindComponent(SCR_CompartmentAccessComponent));
		if (access)
		{
			IEntity vehicle = access.GetVehicle();
			if (vehicle)
				return vehicle;
		}

		return CompartmentAccessComponent.GetVehicleIn(character);
	}

	//------------------------------------------------------------------------------------------------
	protected void RegisterSighting(notnull OPC_FogOfWarSightings sightings, IEntity entity, float seenAt)
	{
		if (!entity)
			return;

		RplId id = GetEntityRplId(entity);
		if (!id.IsValid())
			return;

		float existing;
		if (sightings.m_mLastSeen.Find(id, existing))
		{
			// Already revealed - only the timestamp moves, the commander's view does not change
			if (existing < seenAt)
				sightings.m_mLastSeen.Set(id, seenAt);

			return;
		}

		sightings.m_mLastSeen.Set(id, seenAt);
		sightings.m_bMembershipChanged = true;
	}

	//------------------------------------------------------------------------------------------------
	//! Apply the reveal timeout and rebuild the outgoing RplId list, bumping the revision only when the
	//! set actually changed so Poll() can skip the RPC entirely on a quiet tick.
	protected void RebuildRevealed(notnull OPC_FogOfWarSightings sightings, float now)
	{
		// Remove by index: the key is an RplId, so unlike an IEntity key it stays valid (and findable)
		// after the entity it refers to has been destroyed
		for (int i = sightings.m_mLastSeen.Count() - 1; i >= 0; i--)
		{
			if (now - sightings.m_mLastSeen.GetElement(i) > m_fRevealTimeout)
			{
				sightings.m_mLastSeen.RemoveElement(i);
				sightings.m_bMembershipChanged = true;
			}
		}

		if (!sightings.m_bMembershipChanged)
			return;

		sightings.m_bMembershipChanged = false;

		sightings.m_aRevealedIds.Clear();
		for (int i = 0, count = sightings.m_mLastSeen.Count(); i < count; i++)
		{
			sightings.m_aRevealedIds.Insert(sightings.m_mLastSeen.GetKey(i));
		}

		sightings.m_iRevision++;
	}
}
