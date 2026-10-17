#include "cbase.h"
#include "tier1/bitbuf.h"
#include "tier0/icommandline.h"
#include "tier2/tier2.h"

#include "networkmanager.h"
#include "gameinfostore.h"
#include "lnetwork.h"
#include "networkmessage.h"

static CNetworkManager s_NetworkManager;
CNetworkManager *g_pNetworkManager = &s_NetworkManager;

#ifdef LUA_SDK
// A channel is only safe to use while the engine still has it: nothing unbinds a
// player that leaves, and the engine frees its channel. Checked before every send.
void CNetworkManager::PruneDeadChannels()
{
#ifdef GAME_DLL
    for ( int i = m_ConnectedPlayers.Count() - 1; i >= 0; i-- )
    {
        const INetChannelInfo *pChannel = m_ConnectedPlayers[i].netChannel;
        bool bLive = false;
        for ( int nClient = 1; nClient <= gpGlobals->maxClients && !bLive; nClient++ )
            bLive = ( engine->GetPlayerNetInfo( nClient ) == pChannel );
        if ( !bLive )
            m_ConnectedPlayers.Remove( i );
    }
#else
    const INetChannelInfo *pLive = engine->GetNetChannelInfo();
    for ( int i = m_ConnectedPlayers.Count() - 1; i >= 0; i-- )
    {
        if ( m_ConnectedPlayers[i].netChannel != pLive )
            m_ConnectedPlayers.Remove( i );
    }
#endif
}

// A new binding replaces any old one for the same player or channel
void CNetworkManager::RemoveBinding( int userId, INetChannel *netChannel )
{
    for ( int i = m_ConnectedPlayers.Count() - 1; i >= 0; i-- )
    {
        if ( m_ConnectedPlayers[i].userId == userId || m_ConnectedPlayers[i].netChannel == netChannel )
            m_ConnectedPlayers.Remove( i );
    }
}
#endif

#ifdef GAME_DLL
bool CNetworkManager::BindClient( int userId, INetChannel *netChannel )
{
    if ( !netChannel )
    {
        return false;
    }

#ifdef LUA_SDK
    RemoveBinding( userId, netChannel );
#endif

    ConnectedPlayer_t player;
    player.userId = userId;
    player.netChannel = netChannel;
    m_ConnectedPlayers.AddToTail( player );

    // Bind the handler that will get called on incoming messages
    CDynamicWriteNetworkMessage *netMessage = new CDynamicWriteNetworkMessage();
    netMessage->SetSender( userId );
    netChannel->RegisterMessage( netMessage );

    return true;
}

void CNetworkManager::UnbindClient( int userId )
{
    for ( int i = 0; i < m_ConnectedPlayers.Count(); i++ )
    {
        if ( m_ConnectedPlayers[i].userId == userId )
        {
            m_ConnectedPlayers.Remove( i );
            break;
        }
    }
}
#else
bool CNetworkManager::BindClient( INetChannel *netChannel )
{
    if ( !netChannel )
    {
        return false;
    }

#ifdef LUA_SDK
    RemoveBinding( -1, netChannel );
#endif

    ConnectedPlayer_t player;
    player.userId = -1;
    player.netChannel = netChannel;
    m_ConnectedPlayers.AddToTail( player );

    // Bind the handler that will get called on incoming messages
    //CDynamicWriteNetworkMessage *netMessage = new CDynamicWriteNetworkMessage();
    //netChannel->RegisterMessage( netMessage );

    return true;
}

void CNetworkManager::UnbindClient()
{
    for ( int i = 0; i < m_ConnectedPlayers.Count(); i++ )
    {
        if ( m_ConnectedPlayers[i].userId == -1 )
        {
            m_ConnectedPlayers.Remove( i );
            return;
        }
    }
}
#endif

void CNetworkManager::UnbindClients()
{
    m_ConnectedPlayers.Purge();
}

void CNetworkManager::SendClientToServerMessage( INetMessage *pMessage )
{
    if ( !pMessage )
    {
        return;
    }

#ifdef LUA_SDK
    PruneDeadChannels();
#endif

    for ( int i = 0; i < m_ConnectedPlayers.Count(); i++ )
    {
        if ( m_ConnectedPlayers[i].userId == -1 )
        {
            m_ConnectedPlayers[i].netChannel->SendNetMsg( *pMessage, true );
            return;
        }
    }
}

void CNetworkManager::BroadcastServerToClientsMessage( INetMessage *pMessage )
{
    if ( !pMessage )
    {
        return;
    }

#ifdef LUA_SDK
    PruneDeadChannels();
#endif

    for ( int i = 0; i < m_ConnectedPlayers.Count(); i++ )
    {
        m_ConnectedPlayers[i].netChannel->SendNetMsg( *pMessage, true );
    }
}

void CNetworkManager::SendServerToClientMessage( INetMessage *pMessage, int userId )
{
    if ( !pMessage )
    {
        return;
    }

#ifdef LUA_SDK
    PruneDeadChannels();
#endif

    for ( int i = 0; i < m_ConnectedPlayers.Count(); i++ )
    {
        if ( m_ConnectedPlayers[i].userId == userId )
        {
            m_ConnectedPlayers[i].netChannel->SendNetMsg( *pMessage, true );
            return;
        }
    }
}
