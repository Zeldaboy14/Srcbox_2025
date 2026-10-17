#include "cbase.h"
//#include "ClientHL2MPPlayer.h"
#include "functionproxy.h"
#include "proxyentity.h"
#include "materialsystem/imaterial.h"
#include "materialsystem/imaterialvar.h"

class CPlayerTeamProxy : public CResultProxy
{
	virtual bool Init( IMaterial *pMaterial, KeyValues *pKeyValues );
	virtual void OnBind( void *pBaseEntity );

private:
	int m_team;
};

bool CPlayerTeamProxy::Init( IMaterial *pMaterial, KeyValues *pKeyValues )
{
	if ( !CResultProxy::Init( pMaterial, pKeyValues ) )
		return false;

	m_team = pKeyValues->GetInt( "team" );
	return true;
}

void CPlayerTeamProxy::OnBind( void *pBaseEntity )
{
	C_BasePlayer *pViewPlayer = C_BasePlayer::GetLocalOrSpectatedPlayer();
	if ( pViewPlayer )
	{
		float result = pViewPlayer->GetTeamNumber() == m_team ? 1.0 : 0.0;
		SetFloatResult( result );
	}
}

//EXPOSE_MATERIAL_PROXY( CPlayerTeamProxy, PlayerTeam );

EXPOSE_INTERFACE(CPlayerTeamProxy, IMaterialProxy, "PlayerTeam" IMATERIAL_PROXY_INTERFACE_VERSION);