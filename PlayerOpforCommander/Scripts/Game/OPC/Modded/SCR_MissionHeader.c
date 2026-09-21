//------------------------------------------------------------------------------------------------
//! Per-scenario overrides for the Opfor Player Commander fog of war.
//! Leave empty / 0 / -1 to use the values from the game mode prefab (see modded SCR_BaseGameMode).
modded class SCR_MissionHeader : MissionHeader
{
	[Attribute(defvalue: "", desc: "OPC: Fallback key of the faction to HIDE from the commander (empty = game mode default; normally the toolbar picks a playable faction).", category: "OPC Fog of War")]
	string m_sOPC_HiddenFactionKey;

	[Attribute(defvalue: "0", desc: "OPC: Seconds a spotted enemy stays revealed after the last AI contact (0 = game mode default).", category: "OPC Fog of War", params: "0 600 1")]
	float m_fOPC_RevealTimeout;

	[Attribute(defvalue: "0", desc: "OPC: Seconds between perception polls / updates sent to the commander (0 = game mode default).", category: "OPC Fog of War", params: "0 10 0.25")]
	float m_fOPC_UpdateInterval;

	[Attribute(defvalue: "-1", desc: "OPC: Whether player-controlled characters hostile to the hidden faction also reveal what they perceive. -1 = game mode default, 0 = off, 1 = on.", category: "OPC Fog of War", params: "-1 1 1")]
	int m_iOPC_PlayersCanSpot;

	[Attribute(defvalue: "-1", desc: "OPC: Seconds before the same contact can raise another report in the commander's notification log. -1 = game mode default, 0 = no contact reports.", category: "OPC Fog of War", params: "-1 900 5")]
	float m_fOPC_ContactReportCooldown;
}
