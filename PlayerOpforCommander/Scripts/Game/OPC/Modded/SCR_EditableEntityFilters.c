//------------------------------------------------------------------------------------------------
//! Deny the editor VISIBLE state to entities hidden by fog of war.
//! Everything downstream (RENDERED icons, HOVER, SELECTED, tooltips, entity lists) follows.
//! NOTE: the BaseContainerProps attribute MUST be repeated on modded config classes, otherwise the
//! class is no longer registered for prefab/config deserialization ("Unknown class" on load).
[BaseContainerProps(), SCR_BaseContainerCustomTitleEnum(EEditableEntityState, "m_State")]
modded class SCR_VisibleEditableEntityFilter : SCR_BaseEditableEntityFilter
{
	//! CanAdd() runs once per entity per validation, which during a filter rebuild means once for every
	//! editable entity in the mission. Resolving the player controller each time was pure overhead, so
	//! the client is cached - but only for the lifetime of one editor session, because the client is
	//! owned by SCR_PlayerController while this filter lives on the (config-owned) entities manager.
	protected OPC_FogOfWarClient m_OPC_FogOfWar;

	//------------------------------------------------------------------------------------------------
	override bool CanAdd(SCR_EditableEntityComponent entity)
	{
		if (!super.CanAdd(entity))
			return false;

		if (!m_OPC_FogOfWar)
			m_OPC_FogOfWar = OPC_FogOfWarClient.GetInstance(false);

		return !m_OPC_FogOfWar || !m_OPC_FogOfWar.IsHidden(entity);
	}

	//------------------------------------------------------------------------------------------------
	override void EOnEditorDeactivate()
	{
		m_OPC_FogOfWar = null;
		super.EOnEditorDeactivate();
	}
}

//------------------------------------------------------------------------------------------------
//! Belt and braces: the 3D cursor trace / icon hit-test could still submit a hidden entity as
//! hover candidate in the same frame it gets hidden - never allow hovering hidden entities.
[BaseContainerProps(), SCR_BaseContainerCustomTitleEnum(EEditableEntityState, "m_State")]
modded class SCR_HoverEditableEntityFilter : SCR_BaseEditableEntityFilter
{
	protected OPC_FogOfWarClient m_OPC_FogOfWar;

	//------------------------------------------------------------------------------------------------
	override bool CanAdd(SCR_EditableEntityComponent entity)
	{
		if (!super.CanAdd(entity))
			return false;

		if (!m_OPC_FogOfWar)
			m_OPC_FogOfWar = OPC_FogOfWarClient.GetInstance(false);

		return !m_OPC_FogOfWar || !m_OPC_FogOfWar.IsHidden(entity);
	}

	//------------------------------------------------------------------------------------------------
	override protected void EOnEditorDeactivate()
	{
		m_OPC_FogOfWar = null;
		super.EOnEditorDeactivate();
	}
}
