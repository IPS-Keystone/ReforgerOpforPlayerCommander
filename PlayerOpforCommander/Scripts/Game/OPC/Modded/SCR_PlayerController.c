//------------------------------------------------------------------------------------------------
//! Network bridge for the Opfor Player Commander fog of war:
//!  client -> server : subscribe / unsubscribe to spotted-entity updates
//!  server -> owner  : config + periodic list of spotted entities
modded class SCR_PlayerController : PlayerController
{
	//! Client-side fog of war state, owned here so it is destroyed together with the game
	protected ref OPC_FogOfWarClient m_OPC_FogOfWarClient;

	//------------------------------------------------------------------------------------------------
	OPC_FogOfWarClient OPC_GetFogOfWarClient(bool create = true)
	{
		if (!m_OPC_FogOfWarClient && create)
			m_OPC_FogOfWarClient = new OPC_FogOfWarClient();

		return m_OPC_FogOfWarClient;
	}

	//------------------------------------------------------------------------------------------------
	protected bool OPC_IsLocalOwner()
	{
		RplComponent rpl = RplComponent.Cast(FindComponent(RplComponent));
		return rpl && rpl.IsOwner();
	}

	//------------------------------------------------------------------------------------------------
	// CLIENT -> SERVER
	//------------------------------------------------------------------------------------------------
	//! Called on the owning client. Empty faction key = use server default.
	void OPC_RequestSubscribe(bool subscribe, string factionKey)
	{
		if (Replication.IsServer())
			OPC_RpcAsk_Subscribe(subscribe, factionKey); // listen server host
		else
			Rpc(OPC_RpcAsk_Subscribe, subscribe, factionKey);
	}

	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	protected void OPC_RpcAsk_Subscribe(bool subscribe, string factionKey)
	{
		SCR_BaseGameMode gameMode = SCR_BaseGameMode.Cast(GetGame().GetGameMode());
		if (!gameMode)
			return;

		OPC_FogOfWarServer fow = gameMode.OPC_GetFogOfWar();
		if (!fow)
			return;

		fow.SetSubscribed(GetPlayerId(), subscribe, factionKey);
	}

	//------------------------------------------------------------------------------------------------
	// SERVER -> OWNER
	//------------------------------------------------------------------------------------------------
	//! Server: send effective config to the owning client
	void OPC_SendConfig(string factionKey, float revealTimeout)
	{
		if (OPC_IsLocalOwner())
			OPC_RpcDo_Config(factionKey, revealTimeout);
		else
			Rpc(OPC_RpcDo_Config, factionKey, revealTimeout);
	}

	//------------------------------------------------------------------------------------------------
	//! Server: send currently spotted entities to the owning client
	void OPC_SendRevealed(notnull array<RplId> revealedIds)
	{
		if (OPC_IsLocalOwner())
			OPC_RpcDo_Revealed(revealedIds);
		else
			Rpc(OPC_RpcDo_Revealed, revealedIds);
	}

	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Owner)]
	protected void OPC_RpcDo_Config(string factionKey, float revealTimeout)
	{
		OPC_GetFogOfWarClient().OnConfigReceived(factionKey, revealTimeout);
	}

	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Owner)]
	protected void OPC_RpcDo_Revealed(array<RplId> revealedIds)
	{
		if (revealedIds)
		{
			OPC_GetFogOfWarClient().OnRevealedReceived(revealedIds);
		}
		else
		{
			array<RplId> empty = {};
			OPC_GetFogOfWarClient().OnRevealedReceived(empty);
		}
	}
}
