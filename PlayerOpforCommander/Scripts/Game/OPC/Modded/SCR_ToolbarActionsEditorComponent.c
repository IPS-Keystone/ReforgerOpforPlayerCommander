//------------------------------------------------------------------------------------------------
//! Injects the Fog of War toggle into the Game Master top toolbar, right after the vanilla
//! "Hide interface" (SCR_ToggleInterfaceToolbarAction) button.
//! Done lazily (first time the action list is accessed) and in script only, so no editor-mode
//! prefab has to be overridden (avoids clashing with GME) and nothing runs during world creation.
//!
//! IMPORTANT: server-performed toolbar actions are sent over the network as an INDEX into the
//! sorted action list, so client and server must build identical lists. GetAction() and
//! FindAction() are overridden as well: whichever side touches the list first inserts the
//! fog of war action at the same deterministic position, keeping indices in sync.
[ComponentEditorProps(category: "GameScripted/Editor", description: "Manager of ability actions in editor", icon: "WBData/ComponentEditorProps/componentEditor.png")]
modded class SCR_ToolbarActionsEditorComponentClass : SCR_BaseActionsEditorComponentClass
{
	//! Strong reference - m_ActionsSorted holds only weak pointers (the vanilla actions are owned by m_ActionsLists)
	protected ref OPC_FogOfWarToolbarAction m_OPC_FogOfWarAction;
	protected bool m_bOPC_Checked;

	//------------------------------------------------------------------------------------------------
	override int GetActions(out notnull array<SCR_BaseEditorAction> outActions)
	{
		OPC_InsertFogOfWarAction();
		return super.GetActions(outActions);
	}

	//------------------------------------------------------------------------------------------------
	override SCR_BaseEditorAction GetAction(int index)
	{
		OPC_InsertFogOfWarAction();
		return super.GetAction(index);
	}

	//------------------------------------------------------------------------------------------------
	override int FindAction(SCR_BaseEditorAction action)
	{
		OPC_InsertFogOfWarAction();
		return super.FindAction(action);
	}

	//------------------------------------------------------------------------------------------------
	protected void OPC_InsertFogOfWarAction()
	{
		if (m_bOPC_Checked)
			return;

		m_bOPC_Checked = true;

		if (!m_ActionsSorted)
			return;

		int anchorIndex = -1;
		SCR_BaseEditorAction anchor;
		for (int i = 0, count = m_ActionsSorted.Count(); i < count; i++)
		{
			SCR_BaseEditorAction action = m_ActionsSorted[i];
			if (!action)
				continue;

			if (OPC_FogOfWarToolbarAction.Cast(action))
				return;

			if (anchorIndex == -1 && SCR_ToggleInterfaceToolbarAction.Cast(action))
			{
				anchorIndex = i;
				anchor = action;
			}
		}

		// Only add to toolbars that actually contain the "hide interface" button (i.e. the GM top row)
		if (anchorIndex == -1)
			return;

		m_OPC_FogOfWarAction = OPC_FogOfWarToolbarAction.Create(anchor.GetActionGroup(), anchor.GetOrder() + 1);
		m_ActionsSorted.InsertAt(m_OPC_FogOfWarAction, anchorIndex + 1);
	}
}
