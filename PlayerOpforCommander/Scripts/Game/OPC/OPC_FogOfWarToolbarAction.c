//------------------------------------------------------------------------------------------------
//! Game Master toolbar toggle: "Fog of War" for the Opfor Player Commander.
//! Purely local (IsServer = false); flips OPC_FogOfWarClient on/off and mirrors its state.
//!
//! Registered from script by the modded SCR_ToolbarActionsEditorComponentClass (no config
//! or prefab override needed, so it does not clash with GME's EditorModeEdit.et override).
[BaseContainerProps(), SCR_BaseContainerCustomTitleUIInfo("m_Info")]
class OPC_FogOfWarToolbarAction : SCR_BaseToggleToolbarAction
{
	// Distinct vanilla textures for the two states, so the button reads at a glance with no imported
	// assets. The mod also ships a matching pair of custom icons in UI/Textures/OPC - once the addon
	// has been opened in Workbench it assigns them a {GUID}, and these two lines become:
	//   static const ResourceName ICON_OFF = "{GUID}UI/Textures/OPC/OPC_FogOfWar_Off.edds";
	//   static const ResourceName ICON_ON  = "{GUID}UI/Textures/OPC/OPC_FogOfWar_On.edds";
	// See README, "Custom icon".
	static const ResourceName ICON_OFF = "{A489F552FB7489C3}UI/Textures/Editor/EditableEntities/Characters/EditableEntity_Character_Custom.edds";
	static const ResourceName ICON_ON = "{857DD01860810AE9}UI/Textures/Editor/Attributes/Categories/Attribute_Category_Weather.edds";

	//------------------------------------------------------------------------------------------------
	//! Factory used by the modded toolbar component class
	static OPC_FogOfWarToolbarAction Create(EEditorActionGroup group, int order)
	{
		OPC_FogOfWarToolbarAction action = new OPC_FogOfWarToolbarAction();
		action.Init(group, order);
		return action;
	}

	//------------------------------------------------------------------------------------------------
	void Init(EEditorActionGroup group, int order)
	{
		m_Info = SCR_UIInfo.CreateInfo("Fog of War: OFF", "Hide the players' (playable) faction unless spotted by AI hostile to it - i.e. see the battlefield the way your OPFOR AI sees it. Click to enable; further clicks cycle through the playable factions.", ICON_OFF);
		m_InfoToggled = SCR_UIInfo.CreateInfo("Fog of War: ON", "The playable faction is only shown where hostile AI have it spotted.", ICON_ON);
		m_ActionType = EEditorActionType.TOGGLE;
		m_ActionGroup = group;
		m_iOrder = order;
		m_bEnabled = true;
	}

	//------------------------------------------------------------------------------------------------
	protected void OnFogOfWarChanged(bool enabled)
	{
		RefreshState();
	}

	//------------------------------------------------------------------------------------------------
	protected void OnFogOfWarConfigChanged()
	{
		RefreshState();
	}

	//------------------------------------------------------------------------------------------------
	//! Update label + highlight from the client state.
	//! Value = index of the faction (+2) rather than a plain 0/1: SCR_BaseToggleToolbarAction.Toggle()
	//! early-returns when neither argument changed, and SCR_ActionToolbarItemEditorUIComponent only
	//! re-reads GetInfoToggled() from that event, so without a changing value the tooltip would keep
	//! naming the previous faction while cycling.
	protected void RefreshState()
	{
		OPC_FogOfWarClient fow = OPC_FogOfWarClient.GetInstance();
		if (!fow)
		{
			Toggle(0, false);
			return;
		}

		bool enabled = fow.IsEnabled();

		int value = 0;
		if (enabled)
		{
			array<string> keys = {};
			OPC_FogOfWarClient.GetPlayableFactionKeys(keys);
			value = keys.Find(fow.GetHiddenFactionKey()) + 2;

			string factionName = fow.GetHiddenFactionDisplayName();
			if (factionName.IsEmpty())
				factionName = "default faction";

			m_InfoToggled = SCR_UIInfo.CreateInfo(
				string.Format("Fog of War: hiding %1", factionName),
				string.Format("%1 units, vehicles and players are only shown while AI hostile to them have them spotted. Click to switch to the next playable faction, or off after the last one.", factionName),
				ICON_ON);
		}

		Toggle(value, enabled);
	}

	//------------------------------------------------------------------------------------------------
	override bool IsServer()
	{
		return false;
	}

	//------------------------------------------------------------------------------------------------
	override bool CanBeShown(SCR_EditableEntityComponent hoveredEntity, notnull set<SCR_EditableEntityComponent> selectedEntities, vector cursorWorldPosition, int flags)
	{
		return true;
	}

	//------------------------------------------------------------------------------------------------
	override bool CanBePerformed(SCR_EditableEntityComponent hoveredEntity, notnull set<SCR_EditableEntityComponent> selectedEntities, vector cursorWorldPosition, int flags)
	{
		return true;
	}

	//------------------------------------------------------------------------------------------------
	override void Perform(SCR_EditableEntityComponent hoveredEntity, notnull set<SCR_EditableEntityComponent> selectedEntities, vector cursorWorldPosition, int flags, int param = -1)
	{
		// OFF -> own/first playable faction -> next playable faction -> ... -> OFF
		OPC_FogOfWarClient fow = OPC_FogOfWarClient.GetInstance();
		if (fow)
			fow.CycleFaction();
	}

	//------------------------------------------------------------------------------------------------
	override void Track()
	{
		OPC_FogOfWarClient fow = OPC_FogOfWarClient.GetInstance();
		if (!fow)
			return;

		fow.GetOnEnabledChanged().Insert(OnFogOfWarChanged);
		fow.GetOnConfigChanged().Insert(OnFogOfWarConfigChanged);
		RefreshState();
	}

	//------------------------------------------------------------------------------------------------
	override void Untrack()
	{
		OPC_FogOfWarClient fow = OPC_FogOfWarClient.GetInstance(false);
		if (!fow)
			return;

		fow.GetOnEnabledChanged().Remove(OnFogOfWarChanged);
		fow.GetOnConfigChanged().Remove(OnFogOfWarConfigChanged);
	}
}
