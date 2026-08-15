//------------------------------------------------------------------------------------------------
//! Server-side sighting bookkeeping for one hidden (player) faction.
class OPC_FogOfWarSightings
{
	string m_sFactionKey; //!< key of the HIDDEN faction; spotters are all AI hostile to it
	//! entity -> world time (seconds) it was last seen/detected by any spotter AI
	ref map<IEntity, float> m_mLastSeen = new map<IEntity, float>();
	//! Cached result of the last poll
	ref array<RplId> m_aRevealedIds = {};

	//------------------------------------------------------------------------------------------------
	void OPC_FogOfWarSightings(string factionKey)
	{
		m_sFactionKey = factionKey;
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

	//! playerId -> faction key this commander is subscribed to
	protected ref map<int, string> m_mSubscribers = new map<int, string>();
	//! faction key -> sightings
	protected ref map<string, ref OPC_FogOfWarSightings> m_mSightings = new map<string, ref OPC_FogOfWarSightings>();

	protected bool m_bPolling;

	protected static const ref array<ETargetCategory> TARGET_CATEGORIES = {ETargetCategory.ENEMY, ETargetCategory.DETECTED, ETargetCategory.UNKNOWN};

	//------------------------------------------------------------------------------------------------
	void Configure(string defaultFactionKey, float revealTimeout, float updateInterval, bool playersCanSpot)
	{
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
	//! Subscribe / unsubscribe a commander. factionKey = the faction to HIDE (empty = server default).
	void SetSubscribed(int playerId, bool subscribe, string factionKey = string.Empty)
	{
		if (!subscribe)
		{
			m_mSubscribers.Remove(playerId);
			CleanupSightings();
			if (m_mSubscribers.IsEmpty())
				StopPolling();
			return;
		}

		// Validate requested key (must be a known faction), otherwise use server default
		FactionManager factionManager = GetGame().GetFactionManager();
		if (factionKey.IsEmpty() || !factionManager || !factionManager.GetFactionByKey(factionKey))
			factionKey = m_sDefaultFactionKey;

		m_mSubscribers.Set(playerId, factionKey);

		if (!m_mSightings.Contains(factionKey))
			m_mSightings.Insert(factionKey, new OPC_FogOfWarSightings(factionKey));

		// Confirm to the client with the effective config
		SCR_PlayerController pc = GetPlayerController(playerId);
		if (pc)
			pc.OPC_SendConfig(factionKey, m_fRevealTimeout);

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
	//! Main update: gather perception of all AI hostile to the hidden faction, apply timeout, send to subscribers
	protected void Poll()
	{
		if (m_mSubscribers.IsEmpty())
		{
			StopPolling();
			return;
		}

		float now = GetGame().GetWorld().GetWorldTime() * 0.001;

		foreach (string factionKey, OPC_FogOfWarSightings sightings : m_mSightings)
		{
			UpdateSightings(sightings, now);
		}

		foreach (int playerId, string subscribedKey : m_mSubscribers)
		{
			OPC_FogOfWarSightings sightings;
			if (!m_mSightings.Find(subscribedKey, sightings))
				continue;

			SCR_PlayerController pc = GetPlayerController(playerId);
			if (pc)
				pc.OPC_SendRevealed(sightings.m_aRevealedIds);
		}
	}

	//------------------------------------------------------------------------------------------------
	protected void UpdateSightings(notnull OPC_FogOfWarSightings sightings, float now)
	{
		AIWorld aiWorld = GetGame().GetAIWorld();
		if (!aiWorld)
			return;

		PlayerManager playerManager = GetGame().GetPlayerManager();

		FactionManager factionManager = GetGame().GetFactionManager();
		if (!factionManager)
			return;

		Faction hiddenFaction = factionManager.GetFactionByKey(sightings.m_sFactionKey);
		if (!hiddenFaction)
			return;

		array<AIAgent> agents = {};
		aiWorld.GetAIAgents(agents);

		array<BaseTarget> targets = {};
		array<IEntity> extraOccupants = {};

		foreach (AIAgent agent : agents)
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

			// Spotters are AI hostile to the hidden faction (the OPFOR the commander is running)
			if (!spotterFaction || spotterFaction == hiddenFaction || !spotterFaction.IsFactionEnemy(hiddenFaction))
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
				targets.Clear();
				perception.GetTargetsList(targets, category);

				foreach (BaseTarget target : targets)
				{
					if (!target)
						continue;

					IEntity targetEntity = target.GetTargetEntity();
					if (!targetEntity)
						continue;

					// Only the hidden faction (and its allies) matters
					Faction targetFaction = target.GetPerceivedFaction();
					if (targetFaction && targetFaction != hiddenFaction && !hiddenFaction.IsFactionFriendly(targetFaction))
						continue;

					float since = Math.Min(target.GetTimeSinceSeen(), target.GetTimeSinceDetected());
					if (since < 0 || since > m_fRevealTimeout)
						continue;

					float seenAt = now - since;
					RegisterSighting(sightings, targetEntity, seenAt);

					// Character inside a vehicle -> the vehicle is spotted as well
					PerceivableComponent perceivable = target.GetPerceivableComponent();
					if (perceivable && perceivable.IsInCompartment())
					{
						IEntity vehicle = CompartmentAccessComponent.GetVehicleIn(targetEntity);
						if (vehicle)
							RegisterSighting(sightings, vehicle, seenAt);
					}

					// Vehicle spotted -> its occupants are spotted as well
					if (Vehicle.Cast(targetEntity))
					{
						SCR_BaseCompartmentManagerComponent compartmentManager = SCR_BaseCompartmentManagerComponent.Cast(targetEntity.FindComponent(SCR_BaseCompartmentManagerComponent));
						if (compartmentManager)
						{
							extraOccupants.Clear();
							compartmentManager.GetOccupants(extraOccupants);
							foreach (IEntity occupant : extraOccupants)
							{
								RegisterSighting(sightings, occupant, seenAt);
							}
						}
					}
				}
			}
		}

		// Apply timeout, drop deleted entities, build the RplId list
		sightings.m_aRevealedIds.Clear();
		array<IEntity> expired = {};
		foreach (IEntity entity, float lastSeen : sightings.m_mLastSeen)
		{
			if (!entity || now - lastSeen > m_fRevealTimeout)
			{
				expired.Insert(entity);
				continue;
			}

			RplId id = Replication.FindItemId(entity);
			if (id.IsValid())
				sightings.m_aRevealedIds.Insert(id);
		}

		foreach (IEntity entity : expired)
		{
			sightings.m_mLastSeen.Remove(entity);
		}
	}

	//------------------------------------------------------------------------------------------------
	protected void RegisterSighting(notnull OPC_FogOfWarSightings sightings, IEntity entity, float seenAt)
	{
		if (!entity)
			return;

		float existing;
		if (sightings.m_mLastSeen.Find(entity, existing) && existing >= seenAt)
			return;

		sightings.m_mLastSeen.Set(entity, seenAt);
	}
}
