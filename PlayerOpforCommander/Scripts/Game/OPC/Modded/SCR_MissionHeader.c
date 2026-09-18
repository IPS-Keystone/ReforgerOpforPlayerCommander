//------------------------------------------------------------------------------------------------
//! Per-scenario overrides for the Opfor Player Commander fog of war.
//! Leave empty / 0 to use the values from the game mode prefab (see modded SCR_BaseGameMode).
modded class SCR_MissionHeader : MissionHeader
{
	[Attribute(defvalue: "", desc: "OPC: Fallback key of the faction to HIDE from the commander (empty = game mode default; normally the toolbar picks a playable faction).", category: "OPC Fog of War")]
	string m_sOPC_HiddenFactionKey;

	[Attribute(defvalue: "0", desc: "OPC: Seconds a spotted enemy stays revealed after the last AI contact (0 = game mode default).", category: "OPC Fog of War")]
	float m_fOPC_RevealTimeout;
}
