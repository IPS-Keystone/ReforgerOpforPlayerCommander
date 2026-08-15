//------------------------------------------------------------------------------------------------
//! Hosts the server-side fog of war logic. Settings can be set on the game mode prefab, or per
//! scenario via the mission header (SCR_MissionHeader, see modded class) which takes precedence.
modded class SCR_BaseGameMode : BaseGameMode
{
	[Attribute(defvalue: "US", desc: "Fallback key of the faction to HIDE from the commander (the players' side) when the commander requests none. Normally the toolbar picks a playable faction.", category: "OPC Fog of War")]
	protected string m_sOPC_SpotterFactionKey;

	[Attribute(defvalue: "30", desc: "Seconds a spotted enemy stays revealed after the last AI contact.", category: "OPC Fog of War", params: "1 600 1")]
	protected float m_fOPC_RevealTimeout;

	[Attribute(defvalue: "1", desc: "Seconds between perception polls / updates sent to the commander.", category: "OPC Fog of War", params: "0.25 10 0.25")]
	protected float m_fOPC_UpdateInterval;

	[Attribute(defvalue: "0", desc: "When enabled, player-controlled characters hostile to the hidden faction also reveal what they perceive.", category: "OPC Fog of War")]
	protected bool m_bOPC_PlayersCanSpot;

	protected ref OPC_FogOfWarServer m_OPC_FogOfWar;

	//------------------------------------------------------------------------------------------------
	//! Server only
	OPC_FogOfWarServer OPC_GetFogOfWar()
	{
		if (!Replication.IsServer())
			return null;

		if (!m_OPC_FogOfWar)
		{
			m_OPC_FogOfWar = new OPC_FogOfWarServer();

			string factionKey = m_sOPC_SpotterFactionKey;
			float timeout = m_fOPC_RevealTimeout;
			float interval = m_fOPC_UpdateInterval;
			bool playersCanSpot = m_bOPC_PlayersCanSpot;

			// Mission header overrides
			SCR_MissionHeader header = SCR_MissionHeader.Cast(GetGame().GetMissionHeader());
			if (header)
			{
				if (!header.m_sOPC_SpotterFactionKey.IsEmpty())
					factionKey = header.m_sOPC_SpotterFactionKey;

				if (header.m_fOPC_RevealTimeout > 0)
					timeout = header.m_fOPC_RevealTimeout;
			}

			m_OPC_FogOfWar.Configure(factionKey, timeout, interval, playersCanSpot);
		}

		return m_OPC_FogOfWar;
	}

	//------------------------------------------------------------------------------------------------
	override protected void OnPlayerDisconnected(int playerId, KickCauseCode cause, int timeout)
	{
		super.OnPlayerDisconnected(playerId, cause, timeout);

		if (m_OPC_FogOfWar)
			m_OPC_FogOfWar.OnPlayerDisconnected(playerId);
	}
}
