//------------------------------------------------------------------------------------------------
//! Adds the Fog of War toggle to the Game Master top toolbar.
//! Done lazily (first time the action list is accessed) and in script only, so no editor-mode
//! prefab has to be overridden (avoids clashing with GME) and nothing runs during world creation.
[ComponentEditorProps(category: "GameScripted/Editor", description: "Manager of ability actions in editor", icon: "WBData/ComponentEditorProps/componentEditor.png")]
modded class SCR_ToolbarActionsEditorComponentClass : SCR_BaseActionsEditorComponentClass
{
	//! Strong reference - m_ActionsSorted holds only weak pointers (the vanilla actions are owned by m_ActionsLists)
	protected ref OPC_FogOfWarToolbarAction m_OPC_FogOfWarAction;
	protected bool m_bOPC_Checked;

	//------------------------------------------------------------------------------------------------
	override int GetActions(out notnull array<SCR_BaseEditorAction> outActions)
	{
		OPC_AppendFogOfWarAction();
		return super.GetActions(outActions);
	}

	//------------------------------------------------------------------------------------------------
	override SCR_BaseEditorAction GetAction(int index)
	{
		OPC_AppendFogOfWarAction();
		return super.GetAction(index);
	}

	//------------------------------------------------------------------------------------------------
	override int FindAction(SCR_BaseEditorAction action)
	{
		OPC_AppendFogOfWarAction();
		return super.FindAction(action);
	}

	//------------------------------------------------------------------------------------------------
	//! SetShortcuts() walks m_ActionsSorted directly instead of going through GetActions(), so without
	//! this hook the action can still be missing from the list when the editor activates. Harmless
	//! while the action has no shortcut, but it would silently swallow one the day it gets a keybind.
	override void SetShortcuts(SCR_BaseActionsEditorComponent manager, bool toAdd)
	{
		OPC_AppendFogOfWarAction();
		super.SetShortcuts(manager, toAdd);
	}

	//------------------------------------------------------------------------------------------------
	//! Server-performed toolbar actions travel over the network as an INDEX into m_ActionsSorted, so
	//! client and server have to agree on that list.
	//!
	//! Inserting mid-list (next to "Hide interface") shifts every vanilla index after it. That is only
	//! safe while this is the only mod that script-inserts here: client and server reach the list
	//! through different entry points - FindAction() on the client in ActionPerformRpc(), GetAction()
	//! on the server in ActionPerformServer() - so two lazily inserting mods can interleave differently
	//! on the two sides, and the server then performs the wrong action.
	//!
	//! Appending leaves every vanilla index untouched, which makes the ordering question moot. The cost
	//! is cosmetic: the button sits at the end of the toolbar rather than beside "Hide interface".
	protected void OPC_AppendFogOfWarAction()
	{
		if (m_bOPC_Checked)
			return;

		m_bOPC_Checked = true;

		if (!m_ActionsSorted || m_ActionsSorted.IsEmpty())
			return;

		bool hasAnchor = false;
		SCR_BaseEditorAction last;
		foreach (SCR_BaseEditorAction action : m_ActionsSorted)
		{
			if (!action)
				continue;

			if (OPC_FogOfWarToolbarAction.Cast(action))
				return;

			if (SCR_ToggleInterfaceToolbarAction.Cast(action))
				hasAnchor = true;

			last = action;
		}

		// Only add to toolbars that actually contain the "hide interface" button (i.e. the GM top row)
		if (!hasAnchor || !last)
			return;

		// Inherit the trailing action's group so the toolbar does not draw a spurious group separator
		m_OPC_FogOfWarAction = OPC_FogOfWarToolbarAction.Create(last.GetActionGroup(), last.GetOrder() + 1);
		m_ActionsSorted.Insert(m_OPC_FogOfWarAction);
	}
}
