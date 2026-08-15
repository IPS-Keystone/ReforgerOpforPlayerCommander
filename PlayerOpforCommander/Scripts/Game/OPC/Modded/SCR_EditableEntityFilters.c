//------------------------------------------------------------------------------------------------
//! Deny the editor VISIBLE state to entities hidden by fog of war.
//! Everything downstream (RENDERED icons, HOVER, SELECTED, tooltips, entity lists) follows.
//! NOTE: the BaseContainerProps attribute MUST be repeated on modded config classes, otherwise the
//! class is no longer registered for prefab/config deserialization ("Unknown class" on load).
[BaseContainerProps(), SCR_BaseContainerCustomTitleEnum(EEditableEntityState, "m_State")]
modded class SCR_VisibleEditableEntityFilter : SCR_BaseEditableEntityFilter
{
	//------------------------------------------------------------------------------------------------
	override bool CanAdd(SCR_EditableEntityComponent entity)
	{
		if (!super.CanAdd(entity))
			return false;

		OPC_FogOfWarClient fow = OPC_FogOfWarClient.GetInstance(false);
		if (fow && fow.IsHidden(entity))
			return false;

		return true;
	}
}

//------------------------------------------------------------------------------------------------
//! Belt and braces: the 3D cursor trace / icon hit-test could still submit a hidden entity as
//! hover candidate in the same frame it gets hidden - never allow hovering hidden entities.
[BaseContainerProps(), SCR_BaseContainerCustomTitleEnum(EEditableEntityState, "m_State")]
modded class SCR_HoverEditableEntityFilter : SCR_BaseEditableEntityFilter
{
	//------------------------------------------------------------------------------------------------
	override bool CanAdd(SCR_EditableEntityComponent entity)
	{
		if (!super.CanAdd(entity))
			return false;

		OPC_FogOfWarClient fow = OPC_FogOfWarClient.GetInstance(false);
		if (fow && fow.IsHidden(entity))
			return false;

		return true;
	}
}
