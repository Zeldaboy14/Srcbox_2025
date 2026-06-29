#include "cbase.h"
#include "debugoverlay_shared.h"
#include "filesystem.h"
#include "igameevents.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"


#define HEADBLOBS_CONFIG_PATH		"scripts/p3_headblobs.txt"

static ConVar p3_headblobs_debug( "p3_headblobs_debug", "0", FCVAR_REPLICATED );
static ConVar p3_headblobs( "p3_headblobs", "1", FCVAR_REPLICATED );



//-----------------------------------------------------------------------------
// CHeadblobSystem
//-----------------------------------------------------------------------------

class CHeadblobSystem : public CAutoGameSystemPerFrame
#ifdef CLIENT_DLL
	, public IGameEventListener2
#endif
{
	typedef CAutoGameSystemPerFrame BaseClass;

public:
	CHeadblobSystem();

	virtual void					LevelInitPreEntity();
	virtual void					LevelShutdownPostEntity();

	SF void							LoadConfig();
	CUtlDict< CUtlSymbol >			m_mEffectsTable;		// читается из конфига

#ifdef CLIENT_DLL
	virtual void					Update( float frametime );
	virtual void					FireGameEvent( IGameEvent* event );
	
	void							SetHeadblob( int entindex, const char* effectName );

	CUtlMap< int, CUtlSymbol, int >	m_vEntityHeadblobs;		// какой хедблоб сейчас активен
#endif
};

static CHeadblobSystem g_HeadblobSystem;

CHeadblobSystem::CHeadblobSystem() :
	BaseClass( "HeadblobSystem" )
{
#ifdef CLIENT_DLL
	m_vEntityHeadblobs.SetLessFunc( DefLessFunc( int ) );
#endif
}

void CHeadblobSystem::LevelInitPreEntity()
{
	BaseClass::LevelInitPreEntity();

	LoadConfig();

#ifdef CLIENT_DLL
	gameeventmanager->AddListener( this, "headblob", false );
#else
	FOR_EACH_DICT( m_mEffectsTable, i )
	{
		const char* p = m_mEffectsTable[i].String();
		if ( p && p[0] ) PrecacheParticleSystem( p );
	}
#endif
}

void CHeadblobSystem::LevelShutdownPostEntity()
{
	BaseClass::LevelShutdownPostEntity();

#ifdef CLIENT_DLL
	gameeventmanager->RemoveListener( this );
	m_vEntityHeadblobs.Purge();
#endif

	m_mEffectsTable.Purge();
}

SF void CHeadblobSystem::LoadConfig()
{
	m_mEffectsTable.Purge();

	KeyValues* pConfigKV = new KeyValues( "HeadblobConfig" );
	pConfigKV->LoadFromFile( filesystem, HEADBLOBS_CONFIG_PATH );

	KeyValues* pHeadblobKV = pConfigKV->GetFirstSubKey();
	for ( ; pHeadblobKV; pHeadblobKV = pHeadblobKV->GetNextKey() )
	{
		const char* pName = pHeadblobKV->GetName();
		if ( m_mEffectsTable.IsValidIndex( m_mEffectsTable.Find( pName ) ) )
		{
			DevWarning( "Headblobs: Duplicate headblob name: \"%s\"!\n", pName );
			continue;
		}

		m_mEffectsTable.Insert( pName, CUtlSymbol( pHeadblobKV->GetString() ) );
	}

	pConfigKV->deleteThis();
}

#ifdef CLIENT_DLL

void CHeadblobSystem::Update( float frametime )
{
}

void CHeadblobSystem::FireGameEvent( IGameEvent* event )
{
	if ( !Q_stricmp( event->GetName(), "headblob" ) )
	{
		const char* pHeadblobName = event->GetString( "name", NULL );
		const int nEntIndex = event->GetInt( "entindex", -1 );
		
		if ( !pHeadblobName || !pHeadblobName[0] ||
		     !Q_stricmp( pHeadblobName, "false" ) || !Q_stricmp( pHeadblobName, "reset" ) )
		{
			SetHeadblob( nEntIndex, NULL );
			return;
		}

		const int nHeadblobDesc = m_mEffectsTable.Find( pHeadblobName );
		if ( m_mEffectsTable.IsValidIndex( nHeadblobDesc ) )
		{
			const char* pEffectName = m_mEffectsTable[nHeadblobDesc].String();
			if ( !pEffectName || !pEffectName[0] )
			{
				return;
			}
			else
			{
				SetHeadblob( nEntIndex, pEffectName );
			}
		}
		else
		{
			DevWarning( "Headblobs: Headblob description not found: %s!\n", pHeadblobName );
		}
	}
}

void CHeadblobSystem::SetHeadblob( int entindex, const char* effectName )
{
	EHANDLE hEntity = ((entindex != -1) ? g_pEntityList->GetNetworkableHandle( entindex ) : NULL);
	int idx = m_vEntityHeadblobs.Find( entindex );

	if ( !hEntity )
	{
		// а энтити-то такой уже нет...
		if ( m_vEntityHeadblobs.IsValidIndex( idx ) )
		{
			m_vEntityHeadblobs.Remove( idx );
		}
		return;
	}

	CParticleProperty* pParticleProp = hEntity->ParticleProp();

	if ( effectName && m_vEntityHeadblobs.IsValidIndex( idx ) &&
	    !Q_stricmp( m_vEntityHeadblobs[idx].String(), effectName ) )
	{
		// уже играется такой?
		if ( CNewParticleEffect* pOldEffect = pParticleProp->FindActiveEffect( m_vEntityHeadblobs[idx].String() ) )
		{
			//pOldEffect->StartEmission();
			return;
		}
	}

	CNewParticleEffect* pOldEffect = (idx == -1 ? NULL : pParticleProp->FindActiveEffect( m_vEntityHeadblobs[idx].String() ) );
	CNewParticleEffect* pNewEffect = (!effectName ? NULL :
		pParticleProp->Create( effectName, PATTACH_POINT_FOLLOW, hEntity->LookupAttachment( "eyes" ), Vector(0,0,10) ));
	
	if ( m_vEntityHeadblobs.IsValidIndex( idx ) )
	{
		if ( pOldEffect && !pNewEffect )
		{
			// убираем старый эффект
			pParticleProp->StopEmission( pOldEffect );
			//pParticleProp->StopParticlesNamed( m_vEntityHeadblobs[nActiveEntityIndex].String() );
			//pParticleProp->StopEmissionAndDestroyImmediately( pOldEffect );
			m_vEntityHeadblobs.Remove( idx );
		}
		else
		{
			// заменяем старый эффект на новый
			//pParticleProp->ReplaceParticleEffect( pOldEffect, pNewEffect );
			if ( pOldEffect ) pParticleProp->StopEmission( pOldEffect );
			//pParticleProp->StopParticlesNamed( m_vEntityHeadblobs[nActiveEntityIndex].String() );
			//if ( pOldEffect ) pParticleProp->StopEmissionAndDestroyImmediately( pOldEffect );
			//pNewEffect->StartEmission();
			m_vEntityHeadblobs[idx] = CUtlSymbol( effectName );
		}
	}
	else if ( pNewEffect )
	{
		// эффект создали, теперь добавляем в список
		//pNewEffect->StartEmission();
		m_vEntityHeadblobs.Insert( entindex, CUtlSymbol( effectName ) );
	}
}

#else

void P3_SetHeadlob( CBaseEntity* pEntity, const char* pHeadblobName )
{
	if ( !pEntity )
	{
		return;
	}

	if ( IGameEvent *event = gameeventmanager->CreateEvent( "headblob" ) )
	{
		event->SetInt( "entindex", pEntity->entindex() );
		event->SetString( "name", pHeadblobName );
		gameeventmanager->FireEventClientSide( event );
	}
}
#endif