//=============================================================================
//
// Purpose: C++ implementation of the Garry's Mod env_skypaint entity
//          Translated to Source SDK 2013
//
//=============================================================================

#include "cbase.h"

//-----------------------------------------------------------------------------
// Shared definitions and Class Header
//-----------------------------------------------------------------------------
#ifdef CLIENT_DLL
#include "c_baseentity.h"
#else
#include "entitylist.h"
#endif

#ifdef CLIENT_DLL
#define CEnvSkyPaint C_EnvSkyPaint
#endif

class CEnvSkyPaint : public CBaseEntity
{
	DECLARE_DATADESC();
public:
	DECLARE_CLASS( CEnvSkyPaint, CBaseEntity );
	DECLARE_NETWORKCLASS();
	DECLARE_PREDICTABLE();

	CEnvSkyPaint();
	virtual ~CEnvSkyPaint();

	virtual void Spawn( void );
	virtual void Precache( void );
	virtual void Think( void );

#ifndef CLIENT_DLL
	virtual int  UpdateTransmitState(void);
	virtual int  ShouldTransmit( const CCheckTransmitInfo *pInfo ) { return FL_EDICT_ALWAYS; }
	bool KeyValue(const char* szKeyName, const char* szValue);
#endif

public:
	// Networked variables
	CNetworkVector( m_vTopColor );
	CNetworkVector( m_vBottomColor );
	CNetworkVar( float, m_flFadeBias );

	CNetworkVar( float, m_flSunSize );
	CNetworkVector( m_vSunNormal );
	CNetworkVector( m_vSunColor );

	CNetworkVar( float, m_flDuskScale );
	CNetworkVar( float, m_flDuskIntensity );
	CNetworkVector( m_vDuskColor );

	CNetworkVar( bool, m_bDrawStars );
	CNetworkString( m_szStarTexture, MAX_PATH );

	CNetworkVar( int, m_nStarLayers );
	// Using a QAngle to store StarScale, StarFade, StarSpeed as components (p, y, r) per Lua logic
	CNetworkQAngle( m_vStarParams ); 

	CNetworkVar( float, m_flHDRScale );

private:
#if !defined( CLIENT_DLL )
	EHANDLE m_hEnvSun;
	bool	m_bCheckedForSun;
#endif
};

//-----------------------------------------------------------------------------
// Global pointer for the client-side rendering system to access
//-----------------------------------------------------------------------------
//#if defined( CLIENT_DLL )
CEnvSkyPaint *g_pSkyPaint = NULL;
//#endif

//-----------------------------------------------------------------------------
// Server-Side Implementation
//-----------------------------------------------------------------------------

LINK_ENTITY_TO_CLASS( env_skypaint, CEnvSkyPaint );

BEGIN_DATADESC( CEnvSkyPaint )
	DEFINE_KEYFIELD( m_vTopColor, FIELD_VECTOR, "topcolor" ),
	DEFINE_KEYFIELD( m_vBottomColor, FIELD_VECTOR, "bottomcolor" ),
	DEFINE_KEYFIELD( m_flFadeBias, FIELD_FLOAT, "fadebias" ),
	DEFINE_KEYFIELD( m_flSunSize, FIELD_FLOAT, "sunsize" ),
	DEFINE_KEYFIELD( m_vSunNormal, FIELD_VECTOR, "sunnormal" ),
	DEFINE_KEYFIELD( m_vSunColor, FIELD_VECTOR, "suncolor" ),
	DEFINE_KEYFIELD( m_flDuskScale, FIELD_FLOAT, "duskscale" ),
	DEFINE_KEYFIELD( m_flDuskIntensity, FIELD_FLOAT, "duskintensity" ),
	DEFINE_KEYFIELD( m_vDuskColor, FIELD_VECTOR, "duskcolor" ),
	DEFINE_KEYFIELD( m_bDrawStars, FIELD_BOOLEAN, "drawstars" ),
	DEFINE_AUTO_ARRAY_KEYFIELD( m_szStarTexture, FIELD_CHARACTER, "startexture" ),
	DEFINE_KEYFIELD( m_nStarLayers, FIELD_INTEGER, "starlayers" ),
	DEFINE_KEYFIELD( m_flHDRScale, FIELD_FLOAT, "hdrscale" ),

	// Map inputs for the "NetworkVarElement" logic
	DEFINE_FIELD( m_vStarParams, FIELD_VECTOR ),
	
#ifdef SERVER_DLL
	DEFINE_THINKFUNC( Think ),
#endif
END_DATADESC()

IMPLEMENT_NETWORKCLASS_ALIASED(EnvSkyPaint, DT_EnvSkyPaint)

BEGIN_NETWORK_TABLE(CEnvSkyPaint, DT_EnvSkyPaint)
#ifdef CLIENT_DLL
	RecvPropVector(RECVINFO(m_vTopColor)),
	RecvPropVector(RECVINFO(m_vBottomColor)),
	RecvPropFloat(RECVINFO(m_flFadeBias)),
	RecvPropFloat(RECVINFO(m_flSunSize)),
	RecvPropVector(RECVINFO(m_vSunNormal)),
	RecvPropVector(RECVINFO(m_vSunColor)),
	RecvPropFloat(RECVINFO(m_flDuskScale)),
	RecvPropFloat(RECVINFO(m_flDuskIntensity)),
	RecvPropVector(RECVINFO(m_vDuskColor)),
	RecvPropBool(RECVINFO(m_bDrawStars)),
	RecvPropString(RECVINFO(m_szStarTexture)),
	RecvPropInt(RECVINFO(m_nStarLayers)),
	RecvPropQAngles(RECVINFO(m_vStarParams)),
	RecvPropFloat(RECVINFO(m_flHDRScale)),
#else
	SendPropVector( SENDINFO( m_vTopColor ) ),
	SendPropVector( SENDINFO( m_vBottomColor ) ),
	SendPropFloat( SENDINFO( m_flFadeBias ) ),
	SendPropFloat( SENDINFO( m_flSunSize ) ),
	SendPropVector( SENDINFO( m_vSunNormal ) ),
	SendPropVector( SENDINFO( m_vSunColor ) ),
	SendPropFloat( SENDINFO( m_flDuskScale ) ),
	SendPropFloat( SENDINFO( m_flDuskIntensity ) ),
	SendPropVector( SENDINFO( m_vDuskColor ) ),
	SendPropBool( SENDINFO( m_bDrawStars ) ),
	SendPropString( SENDINFO( m_szStarTexture ) ),
	SendPropInt( SENDINFO( m_nStarLayers ) ),
	SendPropQAngles( SENDINFO( m_vStarParams ) ),
	SendPropFloat( SENDINFO( m_flHDRScale ) ),
#endif
END_NETWORK_TABLE()

#ifdef CLIENT_DLL
BEGIN_PREDICTION_DATA(CEnvSkyPaint)
END_PREDICTION_DATA()
#endif

void CEnvSkyPaint::Precache(void)
{
	BaseClass::Precache();
}

CEnvSkyPaint::~CEnvSkyPaint()
{
	if (g_pSkyPaint == this)
	{
		g_pSkyPaint = NULL;
	}
}

CEnvSkyPaint::CEnvSkyPaint()
{
#ifdef CLIENT_DLL
	// Initialize client side pointer if not set
	if (!g_pSkyPaint)
	{
		g_pSkyPaint = this;
	}

#else

	m_hEnvSun = NULL;
	m_bCheckedForSun = false;

	// Entity defaults (SERVER)
	m_vTopColor.GetForModify().Init( 0.2f, 0.5f, 1.0f );
	m_vBottomColor.GetForModify().Init( 0.8f, 1.0f, 1.0f );
	m_flFadeBias = 1.0f;

	m_vSunNormal.GetForModify().Init( 0.4f, 0.0f, 0.01f );
	m_vSunColor.GetForModify().Init( 0.2f, 0.1f, 0.0f );
	m_flSunSize = 2.0f;

	m_vDuskColor.GetForModify().Init( 1.0f, 0.2f, 0.0f );
	m_flDuskScale = 1.0f;
	m_flDuskIntensity = 1.0f;

	m_bDrawStars = true;
	m_nStarLayers = 1;
	
	// StarScale (p), StarFade (y), StarSpeed (r)
	m_vStarParams.GetForModify().Init( 0.5f, 1.5f, 0.01f ); 
	
	Q_strncpy( m_szStarTexture.GetForModify(), "skybox/starfield", MAX_PATH );

	m_flHDRScale = 0.66f;
#endif
}

void CEnvSkyPaint::Spawn( void )
{
#ifdef CLIENT_DLL
	BaseClass::Spawn();
	SetNextThink(CLIENT_THINK_ALWAYS);
#else
	Precache();
	SetSolid( SOLID_NONE );
	SetMoveType( MOVETYPE_NONE );
	
	SetThink( &CEnvSkyPaint::Think );
	SetNextThink( gpGlobals->curtime + 0.1f );
#endif
}

#ifndef CLIENT_DLL
int CEnvSkyPaint::UpdateTransmitState()
{
	return SetTransmitState(FL_EDICT_ALWAYS);
}
#endif

#ifndef CLIENT_DLL
bool CEnvSkyPaint::KeyValue( const char *szKeyName, const char* szValue)
{
	// Logic from ENT:KeyValue / SetNetworkKeyValue
	if (FStrEq(szKeyName, "topcolor"))
	{
		UTIL_StringToVector(m_vTopColor.GetForModify().Base(), szValue);
	}
	else if (FStrEq(szKeyName, "bottomcolor"))
	{
		UTIL_StringToVector(m_vBottomColor.GetForModify().Base(), szValue);
	}
	else if (FStrEq(szKeyName, "fadebias"))
	{
		m_flFadeBias = atof(szValue);
	}
	else if (FStrEq(szKeyName, "sunsize"))
	{
		m_flSunSize = atof(szValue);
	}
	else if (FStrEq(szKeyName, "sunnormal"))
	{
		UTIL_StringToVector(m_vSunNormal.GetForModify().Base(), szValue);
	}
	else if (FStrEq(szKeyName, "suncolor"))
	{
		UTIL_StringToVector(m_vSunColor.GetForModify().Base(), szValue);
	}
	else if (FStrEq(szKeyName, "duskscale"))
	{
		m_flDuskScale = atof(szValue);
	}
	else if (FStrEq(szKeyName, "duskintensity"))
	{
		m_flDuskIntensity = atof(szValue);
	}
	else if (FStrEq(szKeyName, "duskcolor"))
	{
		UTIL_StringToVector(m_vDuskColor.GetForModify().Base(), szValue);
	}
	else if (FStrEq(szKeyName, "drawstars"))
	{
		m_bDrawStars = (atoi(szValue) != 0);
	}
	else if (FStrEq(szKeyName, "startexture"))
	{
		Q_strncpy(m_szStarTexture.GetForModify(), szValue, MAX_PATH);
	}
	else if (FStrEq(szKeyName, "starlayers"))
	{
		m_nStarLayers = atoi(szValue);
	}
	else if (FStrEq(szKeyName, "starscale"))
	{
		m_vStarParams.GetForModify()[0] = atof(szValue); // Angle 'p'
	}
	else if (FStrEq(szKeyName, "starfade"))
	{
		m_vStarParams.GetForModify()[1] = atof(szValue); // Angle 'y'
	}
	else if (FStrEq(szKeyName, "starspeed"))
	{
		m_vStarParams.GetForModify()[2] = atof(szValue); // Angle 'r'
	}
	else if (FStrEq(szKeyName, "hdrscale"))
	{
		m_flHDRScale = atof(szValue);
	}
	else
		return BaseClass::KeyValue(szKeyName, szValue);

	return true;

	// Note: sunposmethod TODO in Lua is not implemented here as it wasn't in source
}
#endif

void CEnvSkyPaint::Think( void )
{
#ifdef CLIENT_DLL
	// Become the active sky again if we're not already
	if (g_pSkyPaint != this && g_pSkyPaint == NULL)
	{
		g_pSkyPaint = this;
	}
#else
	// Find an env_sun if we don't have one
	if ( !m_bCheckedForSun )
	{
		m_hEnvSun = gEntList.FindEntityByClassname( NULL, "env_sun" );
		m_bCheckedForSun = true;
	}

	// If we have a sun - force our sun normal to its value
	if ( m_hEnvSun != NULL )
	{
		// In Source SDK, env_sun uses a vector named m_vDirection
		// This requires including the env_sun header or using GetInternalVariable if not exposed
		Vector vDir;
		//if ( m_hEnvSun->GetVariable( "m_vDirection", &vDir, sizeof(Vector) ) )
		/*if (Q_stricmp(NULL, "m_vDirection"))
		{
			m_vSunNormal = vDir;
		}*/
	}

	SetNextThink( gpGlobals->curtime + 0.1f );
#endif
}
