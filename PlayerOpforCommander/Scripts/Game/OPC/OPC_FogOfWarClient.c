//------------------------------------------------------------------------------------------------
//! Client-side "fog of war" for the Opfor Player Commander.
//!
//! When enabled, every editable entity (character, vehicle, group, player delegate) that belongs
//! to the chosen "hidden" faction (a playable faction, i.e. the human players' side) is hidden from
//! the local Game Master unless the server currently reports it as spotted by AI hostile to that
//! faction (i.e. the OPFOR AI the commander is running).
//!
//! Hiding is done on the local client only:
//!  - Editor VISIBLE state is denied  -> no icons, no hover/selection, no tooltips, no entity list
//!  - Owner entity loses EntityFlags.VISIBLE -> not rendered in the 3D editor camera
//!  - Map descriptor item hidden -> not shown on the 2D map
//!
//! Spotted entities are streamed from the server via SCR_PlayerController (see modded class).
class OPC_FogOfWarClient
{
	// NOTE: no static instance on purpose - the instance is owned by the local SCR_PlayerController
	// so it dies with the game (a static holding editor components would leak across game restarts
	// and trigger the Workbench "Resources are leaking" assertion).

	//! Enabled by the toolbar toggle. Independent of whether the editor is currently open.
	protected bool m_bEnabled;
	//! Fog of war is only applied while the editor is open (so a GM who closes the editor and
	//! plays as a soldier is not blinded).
	protected bool m_bEditorOpen;
	protected bool m_bApplied;

	protected string m_sHiddenFactionKey;    //!< effective key of the hidden (player) faction, confirmed by server
	protected string m_sRequestedFactionKey; //!< key we asked for (empty = server default)
	protected Faction m_HiddenFaction;

	//! Entities the server currently reports as spotted (owner entities)
	protected ref set<IEntity> m_RevealedEntities = new set<IEntity>();

	//! Editable entities currently hidden by us
	protected ref set<SCR_EditableEntityComponent> m_HiddenEntities = new set<SCR_EditableEntityComponent>();

	//! IEntities whose VISIBLE flag we cleared (so we only restore what we touched)
	protected ref set<IEntity> m_HiddenRenderEntities = new set<IEntity>();

	//! IEntities whose map item we hid
	protected ref set<IEntity> m_HiddenMapEntities = new set<IEntity>();

	protected ref ScriptInvoker m_OnEnabledChanged = new ScriptInvoker(); //!< (bool enabled)
	protected ref ScriptInvoker m_OnConfigChanged = new ScriptInvoker(); //!< ()

	protected bool m_bEditorEventsHooked;
	protected bool m_bCoreEventsHooked;
	protected bool m_bMapEventsHooked;

	//------------------------------------------------------------------------------------------------
	static OPC_FogOfWarClient GetInstance(bool create = true)
	{
		SCR_PlayerController pc = SCR_PlayerController.Cast(GetGame().GetPlayerController());
		if (!pc)
			return null;

		return pc.OPC_GetFogOfWarClient(create);
	}

	//------------------------------------------------------------------------------------------------
	ScriptInvoker GetOnEnabledChanged()
	{
		return m_OnEnabledChanged;
	}

	//------------------------------------------------------------------------------------------------
	ScriptInvoker GetOnConfigChanged()
	{
		return m_OnConfigChanged;
	}

	//------------------------------------------------------------------------------------------------
	bool IsEnabled()
	{
		return m_bEnabled;
	}

	//------------------------------------------------------------------------------------------------
	//! True when hiding is actually in effect (enabled + editor open)
	bool IsActive()
	{
		return m_bEnabled && m_bEditorOpen;
	}

	//------------------------------------------------------------------------------------------------
	string GetHiddenFactionKey()
	{
		return m_sHiddenFactionKey;
	}

	//------------------------------------------------------------------------------------------------
	//! Human readable name of the effective hidden faction (falls back to the key)
	string GetHiddenFactionDisplayName()
	{
		if (m_HiddenFaction)
		{
			UIInfo info = m_HiddenFaction.GetUIInfo();
			if (info && !info.GetName().IsEmpty())
				return info.GetName();

			return m_HiddenFaction.GetFactionKey();
		}

		if (!m_sHiddenFactionKey.IsEmpty())
			return m_sHiddenFactionKey;

		return m_sRequestedFactionKey;
	}

	//------------------------------------------------------------------------------------------------
	//! Faction key the commander asked for (empty = server default)
	string GetRequestedFactionKey()
	{
		return m_sRequestedFactionKey;
	}

	//------------------------------------------------------------------------------------------------
	//! Keys of all playable factions in this mission, in faction manager order
	static int GetPlayableFactionKeys(out notnull array<string> outKeys)
	{
		outKeys.Clear();
		FactionManager factionManager = GetGame().GetFactionManager();
		if (!factionManager)
			return 0;

		array<Faction> factions = {};
		factionManager.GetFactionsList(factions);
		foreach (Faction faction : factions)
		{
			SCR_Faction scrFaction = SCR_Faction.Cast(faction);
			if (scrFaction && scrFaction.IsPlayable())
				outKeys.Insert(scrFaction.GetFactionKey());
		}

		return outKeys.Count();
	}

	//------------------------------------------------------------------------------------------------
	//! Enable fog of war for the given faction key, or disable when key is empty
	void SetEnabled(bool enable, string factionKey = string.Empty)
	{
		if (m_bEnabled == enable && (!enable || factionKey == m_sRequestedFactionKey))
			return;

		m_bEnabled = enable;
		if (enable)
			m_sRequestedFactionKey = factionKey;

		HookEditorEvents();
		SCR_EditorManagerEntity editorManager = SCR_EditorManagerEntity.GetInstance();
		m_bEditorOpen = editorManager && editorManager.IsOpened();

		// Ask server to start/stop streaming spotted entities
		SCR_PlayerController pc = SCR_PlayerController.Cast(GetGame().GetPlayerController());
		if (pc)
			pc.OPC_RequestSubscribe(enable, m_sRequestedFactionKey);

		if (!enable)
		{
			m_RevealedEntities.Clear();
			m_sRequestedFactionKey = string.Empty;
		}

		UpdateApplied();
		m_OnEnabledChanged.Invoke(enable);
	}

	//------------------------------------------------------------------------------------------------
	//! Toggle on (first playable faction) / off
	void ToggleEnabled()
	{
		if (m_bEnabled)
		{
			SetEnabled(false);
			return;
		}

		SetEnabled(true, GetPreferredFactionKey());
	}

	//------------------------------------------------------------------------------------------------
	//! Cycle which playable (player) faction is hidden: OFF -> faction 1 -> faction 2 -> ... -> OFF
	void CycleFaction()
	{
		array<string> keys = {};
		if (GetPlayableFactionKeys(keys) == 0)
		{
			ToggleEnabled();
			return;
		}

		if (!m_bEnabled)
		{
			SetEnabled(true, GetPreferredFactionKey());
			return;
		}

		int index = keys.Find(m_sHiddenFactionKey);
		if (index == -1)
			index = keys.Find(m_sRequestedFactionKey);

		index++;
		if (index >= keys.Count())
			SetEnabled(false);
		else
			SetEnabled(true, keys[index]);
	}

	//------------------------------------------------------------------------------------------------
	//! First playable faction (the human players' side), otherwise server default
	protected string GetPreferredFactionKey()
	{
		array<string> keys = {};
		GetPlayableFactionKeys(keys);

		if (!keys.IsEmpty())
			return keys[0];

		return string.Empty;
	}

	//------------------------------------------------------------------------------------------------
	//! Called from SCR_PlayerController when the server confirms subscription and sends its config
	void OnConfigReceived(string hiddenFactionKey, float revealTimeout)
	{
		m_sHiddenFactionKey = hiddenFactionKey;
		m_HiddenFaction = null;

		FactionManager factionManager = GetGame().GetFactionManager();
		if (factionManager && !hiddenFactionKey.IsEmpty())
			m_HiddenFaction = factionManager.GetFactionByKey(hiddenFactionKey);

		if (!m_HiddenFaction)
			Print(string.Format("[OPC] Fog of war: hidden faction '%1' not found in faction manager!", hiddenFactionKey), LogLevel.WARNING);

		m_OnConfigChanged.Invoke();
		Refresh();
	}

	//------------------------------------------------------------------------------------------------
	//! Called from SCR_PlayerController every time the server sends the current spotted list
	void OnRevealedReceived(notnull array<RplId> revealedIds)
	{
		m_RevealedEntities.Clear();
		foreach (RplId id : revealedIds)
		{
			IEntity entity = IEntity.Cast(Replication.FindItem(id));
			if (!entity)
			{
				// Might be an RplComponent id instead of an entity id
				RplComponent rpl = RplComponent.Cast(Replication.FindItem(id));
				if (rpl)
					entity = rpl.GetEntity();
			}

			if (entity)
				m_RevealedEntities.Insert(entity);
		}

		Refresh();
	}

	//------------------------------------------------------------------------------------------------
	//! Is this editable entity something we would ever hide (belongs to / is allied with the hidden faction)?
	bool IsEnemyEntity(SCR_EditableEntityComponent entity)
	{
		if (!entity)
			return false;

		EEditableEntityType type = entity.GetEntityType();
		if (type != EEditableEntityType.CHARACTER && type != EEditableEntityType.VEHICLE && type != EEditableEntityType.GROUP && entity.GetPlayerID() <= 0)
			return false;

		Faction faction = entity.GetFaction();
		if (!faction)
			return false;

		if (!m_HiddenFaction)
		{
			// Config not received yet - fall back to key comparison if we have one
			if (m_sHiddenFactionKey.IsEmpty())
				return false;

			return faction.GetFactionKey() == m_sHiddenFactionKey;
		}

		if (faction == m_HiddenFaction)
			return true;

		// Allies of the hidden faction are hidden as well
		return m_HiddenFaction.IsFactionFriendly(faction) && !faction.IsFactionEnemy(m_HiddenFaction);
	}

	//------------------------------------------------------------------------------------------------
	//! Is this editable entity currently spotted by AI hostile to the hidden faction?
	bool IsRevealed(SCR_EditableEntityComponent entity)
	{
		if (!entity)
			return false;

		IEntity owner = entity.GetOwner();
		if (owner && m_RevealedEntities.Find(owner) != -1)
			return true;

		// Player delegate: check the controlled entity
		if (entity.GetPlayerID() > 0)
		{
			IEntity controlled = GetGame().GetPlayerManager().GetPlayerControlledEntity(entity.GetPlayerID());
			if (controlled && m_RevealedEntities.Find(controlled) != -1)
				return true;
		}

		// Group is revealed when any of its members is
		if (entity.GetEntityType() == EEditableEntityType.GROUP)
		{
			set<SCR_EditableEntityComponent> children = new set<SCR_EditableEntityComponent>();
			entity.GetChildren(children, true);
			foreach (SCR_EditableEntityComponent child : children)
			{
				IEntity childOwner = child.GetOwner();
				if (childOwner && m_RevealedEntities.Find(childOwner) != -1)
					return true;
			}
		}

		return false;
	}

	//------------------------------------------------------------------------------------------------
	//! Should this entity be hidden from the local GM right now?
	bool IsHidden(SCR_EditableEntityComponent entity)
	{
		if (!IsActive())
			return false;

		return IsEnemyEntity(entity) && !IsRevealed(entity);
	}

	//------------------------------------------------------------------------------------------------
	//! Re-evaluate every editable entity and (un)hide accordingly
	void Refresh()
	{
		SCR_EditableEntityCore core = SCR_EditableEntityCore.Cast(SCR_EditableEntityCore.GetInstance(SCR_EditableEntityCore));
		if (!core)
			return;

		set<SCR_EditableEntityComponent> entities = new set<SCR_EditableEntityComponent>();
		core.GetAllEntities(entities);

		array<SCR_EditableEntityComponent> changed = {};
		foreach (SCR_EditableEntityComponent entity : entities)
		{
			if (ApplyToEntity(entity))
				changed.Insert(entity);
		}

		// Entities that were hidden but are no longer registered
		for (int i = m_HiddenEntities.Count() - 1; i >= 0; i--)
		{
			SCR_EditableEntityComponent hidden = m_HiddenEntities[i];
			if (!hidden || entities.Find(hidden) == -1)
			{
				if (hidden)
					Unhide(hidden);
				m_HiddenEntities.Remove(i);
			}
		}

		if (!changed.IsEmpty())
			RevalidateEditorState(changed);
	}

	//------------------------------------------------------------------------------------------------
	//! Apply hidden/visible state to one entity. Returns true when the state changed.
	protected bool ApplyToEntity(SCR_EditableEntityComponent entity)
	{
		if (!entity)
			return false;

		bool shouldHide = IsHidden(entity);
		int index = m_HiddenEntities.Find(entity);
		bool isHidden = index != -1;

		if (shouldHide)
		{
			// Always re-run so newly attached children (e.g. new crew) get hidden too
			Hide(entity);
			if (!isHidden)
				m_HiddenEntities.Insert(entity);
			return !isHidden;
		}
		else if (isHidden)
		{
			Unhide(entity);
			m_HiddenEntities.Remove(index);
			return true;
		}

		return false;
	}

	//------------------------------------------------------------------------------------------------
	protected void Hide(SCR_EditableEntityComponent entity)
	{
		IEntity owner = entity.GetOwner();
		if (!owner)
			return;

		if (entity.GetEntityType() != EEditableEntityType.GROUP)
			HideRender(owner);

		HideMapItem(owner);
	}

	//------------------------------------------------------------------------------------------------
	protected void Unhide(SCR_EditableEntityComponent entity)
	{
		IEntity owner = entity.GetOwner();
		if (!owner)
			return;

		UnhideRender(owner);
		UnhideMapItem(owner);
	}

	//------------------------------------------------------------------------------------------------
	//! Clear VISIBLE flag on the entity and all its descendants, remembering what we touched
	protected void HideRender(IEntity root)
	{
		if (!root)
			return;

		if (root.GetFlags() & EntityFlags.VISIBLE)
		{
			root.ClearFlags(EntityFlags.VISIBLE, false);
			m_HiddenRenderEntities.Insert(root);
		}

		IEntity child = root.GetChildren();
		while (child)
		{
			HideRender(child);
			child = child.GetSibling();
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Restore VISIBLE flag on the entity and descendants that we hid
	protected void UnhideRender(IEntity root)
	{
		if (!root)
			return;

		int index = m_HiddenRenderEntities.Find(root);
		if (index != -1)
		{
			root.SetFlags(EntityFlags.VISIBLE, false);
			m_HiddenRenderEntities.Remove(index);
		}

		IEntity child = root.GetChildren();
		while (child)
		{
			UnhideRender(child);
			child = child.GetSibling();
		}
	}

	//------------------------------------------------------------------------------------------------
	protected void HideMapItem(IEntity owner)
	{
		MapDescriptorComponent descriptor = MapDescriptorComponent.Cast(owner.FindComponent(MapDescriptorComponent));
		if (!descriptor)
			return;

		MapItem item = descriptor.Item();
		if (!item)
			return;

		if (item.IsVisible())
		{
			item.SetVisible(false);
			if (m_HiddenMapEntities.Find(owner) == -1)
				m_HiddenMapEntities.Insert(owner);
		}
	}

	//------------------------------------------------------------------------------------------------
	protected void UnhideMapItem(IEntity owner)
	{
		int index = m_HiddenMapEntities.Find(owner);
		if (index == -1)
			return;

		m_HiddenMapEntities.Remove(index);

		MapDescriptorComponent descriptor = MapDescriptorComponent.Cast(owner.FindComponent(MapDescriptorComponent));
		if (!descriptor)
			return;

		MapItem item = descriptor.Item();
		if (item)
			item.SetVisible(true);
	}

	//------------------------------------------------------------------------------------------------
	//! Push changes into the editor filter chain (VISIBLE state and everything below it)
	protected void RevalidateEditorState(array<SCR_EditableEntityComponent> entities)
	{
		SCR_BaseEditableEntityFilter visibleFilter = SCR_BaseEditableEntityFilter.GetInstance(EEditableEntityState.VISIBLE);
		if (!visibleFilter)
			return;

		if (entities.Count() > 32)
		{
			visibleFilter.SetFromPredecessor();
			return;
		}

		foreach (SCR_EditableEntityComponent entity : entities)
		{
			visibleFilter.Validate(entity);
		}
	}

	//------------------------------------------------------------------------------------------------
	protected void UpdateApplied()
	{
		bool shouldApply = IsActive();
		if (shouldApply == m_bApplied)
		{
			if (shouldApply)
				Refresh();
			return;
		}

		m_bApplied = shouldApply;

		if (shouldApply)
		{
			HookCoreEvents(true);
			HookMapEvents(true);
			Refresh();
		}
		else
		{
			HookCoreEvents(false);
			HookMapEvents(false);
			UnhideAll();
		}
	}

	//------------------------------------------------------------------------------------------------
	protected void UnhideAll()
	{
		array<SCR_EditableEntityComponent> changed = {};
		foreach (SCR_EditableEntityComponent entity : m_HiddenEntities)
		{
			if (!entity)
				continue;

			Unhide(entity);
			changed.Insert(entity);
		}
		m_HiddenEntities.Clear();

		// Safety net: restore anything left over (e.g. children that changed hierarchy)
		foreach (IEntity entity : m_HiddenRenderEntities)
		{
			if (entity)
				entity.SetFlags(EntityFlags.VISIBLE, false);
		}
		m_HiddenRenderEntities.Clear();

		foreach (IEntity mapOwner : m_HiddenMapEntities)
		{
			if (!mapOwner)
				continue;

			MapDescriptorComponent descriptor = MapDescriptorComponent.Cast(mapOwner.FindComponent(MapDescriptorComponent));
			if (descriptor && descriptor.Item())
				descriptor.Item().SetVisible(true);
		}
		m_HiddenMapEntities.Clear();

		SCR_BaseEditableEntityFilter visibleFilter = SCR_BaseEditableEntityFilter.GetInstance(EEditableEntityState.VISIBLE);
		if (visibleFilter)
			visibleFilter.SetFromPredecessor();
	}

	//------------------------------------------------------------------------------------------------
	// Editor open / close
	//------------------------------------------------------------------------------------------------
	protected void HookEditorEvents()
	{
		if (m_bEditorEventsHooked)
			return;

		SCR_EditorManagerEntity editorManager = SCR_EditorManagerEntity.GetInstance();
		if (!editorManager)
			return;

		editorManager.GetOnOpened().Insert(OnEditorOpened);
		editorManager.GetOnClosed().Insert(OnEditorClosed);
		m_bEditorEventsHooked = true;
	}

	//------------------------------------------------------------------------------------------------
	protected void OnEditorOpened()
	{
		m_bEditorOpen = true;
		if (!m_bEnabled)
			return;

		// Re-subscribe (server may have dropped us) and re-apply once the editor components exist
		SCR_PlayerController pc = SCR_PlayerController.Cast(GetGame().GetPlayerController());
		if (pc)
			pc.OPC_RequestSubscribe(true, m_sRequestedFactionKey);

		GetGame().GetCallqueue().CallLater(UpdateApplied, 100, false);
	}

	//------------------------------------------------------------------------------------------------
	protected void OnEditorClosed()
	{
		m_bEditorOpen = false;
		UpdateApplied();
	}

	//------------------------------------------------------------------------------------------------
	// Editable entity core events (entities streaming in / out)
	//------------------------------------------------------------------------------------------------
	protected void HookCoreEvents(bool hook)
	{
		SCR_EditableEntityCore core = SCR_EditableEntityCore.Cast(SCR_EditableEntityCore.GetInstance(SCR_EditableEntityCore));
		if (!core)
			return;

		if (hook && !m_bCoreEventsHooked)
		{
			core.Event_OnEntityRegistered.Insert(OnEntityRegistered);
			core.Event_OnEntityUnregistered.Insert(OnEntityUnregistered);
			core.Event_OnParentEntityChanged.Insert(OnParentEntityChanged);
			m_bCoreEventsHooked = true;
		}
		else if (!hook && m_bCoreEventsHooked)
		{
			core.Event_OnEntityRegistered.Remove(OnEntityRegistered);
			core.Event_OnEntityUnregistered.Remove(OnEntityUnregistered);
			core.Event_OnParentEntityChanged.Remove(OnParentEntityChanged);
			m_bCoreEventsHooked = false;
		}
	}

	//------------------------------------------------------------------------------------------------
	protected void OnEntityRegistered(SCR_EditableEntityComponent entity)
	{
		// The entity may not have its faction yet on the very frame it registers - defer a bit
		GetGame().GetCallqueue().CallLater(ApplyDeferred, 250, false, entity);
	}

	//------------------------------------------------------------------------------------------------
	protected void ApplyDeferred(SCR_EditableEntityComponent entity)
	{
		if (!entity || !m_bApplied)
			return;

		if (ApplyToEntity(entity))
		{
			array<SCR_EditableEntityComponent> changed = {};
			changed.Insert(entity);
			RevalidateEditorState(changed);
		}
	}

	//------------------------------------------------------------------------------------------------
	protected void OnEntityUnregistered(SCR_EditableEntityComponent entity)
	{
		int index = m_HiddenEntities.Find(entity);
		if (index != -1)
			m_HiddenEntities.Remove(index);
	}

	//------------------------------------------------------------------------------------------------
	protected void OnParentEntityChanged(SCR_EditableEntityComponent entity, SCR_EditableEntityComponent parentEntity, SCR_EditableEntityComponent parentEntityPrev)
	{
		// Group membership changed -> group reveal state may change
		GetGame().GetCallqueue().CallLater(Refresh, 100, false);
	}

	//------------------------------------------------------------------------------------------------
	// Map events (map items are created lazily, re-apply when map opens)
	//------------------------------------------------------------------------------------------------
	protected void HookMapEvents(bool hook)
	{
		if (hook && !m_bMapEventsHooked)
		{
			SCR_MapEntity.GetOnMapOpen().Insert(OnMapOpen);
			m_bMapEventsHooked = true;
		}
		else if (!hook && m_bMapEventsHooked)
		{
			SCR_MapEntity.GetOnMapOpen().Remove(OnMapOpen);
			m_bMapEventsHooked = false;
		}
	}

	//------------------------------------------------------------------------------------------------
	protected void OnMapOpen(MapConfiguration config)
	{
		// Give the map a frame or two to create its items
		GetGame().GetCallqueue().CallLater(Refresh, 200, false);
	}
}
