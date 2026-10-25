//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

#include "cbase.h"
#include "baseentity.h"
#include "sendproxy.h"
#include "sun_shared.h"
#include "map_utils.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

class CSun : public CBaseEntity
{
public:
	DECLARE_CLASS( CSun, CBaseEntity );
	DECLARE_SERVERCLASS();
	DECLARE_DATADESC();

	CSun();

	virtual void	Activate();

	// Input handlers
	void InputTurnOn( inputdata_t &inputdata );
	void InputTurnOff( inputdata_t &inputdata );
	void InputSetColor( inputdata_t &inputdata );

	virtual int UpdateTransmitState();

#ifdef LUA_SDK
	// GMod's edit_sun changes the sun at runtime through SetKeyValue. Plain keyfields are written
	// straight into memory, which never marks the network vars as changed, so the client kept the
	// map's sun. Handle those keys here and assign through the network vars.
	virtual bool KeyValue( const char *szKeyName, const char *szValue );
#endif

public:
	CNetworkVector( m_vDirection );
	
	string_t	m_strMaterial;
	string_t	m_strOverlayMaterial;

	int		m_bUseAngles;
	float	m_flPitch;
	float	m_flYaw;
	
	CNetworkVar( int, m_nSize );		// Size of the main core image
	CNetworkVar( int, m_nOverlaySize ); // Size for the glow overlay
	CNetworkVar( color32, m_clrOverlay );
	CNetworkVar( bool, m_bOn );
	CNetworkVar( int, m_nMaterial );
	CNetworkVar( int, m_nOverlayMaterial );
	CNetworkVar( float, m_flHDRColorScale );
};

IMPLEMENT_SERVERCLASS_ST_NOBASE( CSun, DT_Sun )
	SendPropInt( SENDINFO(m_clrRender), 32, SPROP_UNSIGNED, SendProxy_Color32ToInt ),
	SendPropInt( SENDINFO(m_clrOverlay), 32, SPROP_UNSIGNED, SendProxy_Color32ToInt ),
	SendPropVector( SENDINFO(m_vDirection), 0, SPROP_NORMAL ),
	SendPropInt( SENDINFO(m_bOn), 1, SPROP_UNSIGNED ),
	SendPropInt( SENDINFO(m_nSize), 10, SPROP_UNSIGNED ),
	SendPropInt( SENDINFO(m_nOverlaySize), 10, SPROP_UNSIGNED ),
	SendPropInt( SENDINFO(m_nMaterial), 32, SPROP_UNSIGNED ),
	SendPropInt( SENDINFO(m_nOverlayMaterial), 32, SPROP_UNSIGNED ),
	SendPropFloat( SENDINFO_NAME( m_flHDRColorScale, HDRColorScale ), 0,	SPROP_NOSCALE,	0.0f,	100.0f ),
END_SEND_TABLE()


LINK_ENTITY_TO_CLASS( env_sun, CSun );


BEGIN_DATADESC( CSun )

	DEFINE_FIELD( m_vDirection,		FIELD_VECTOR ),
	
	DEFINE_KEYFIELD( m_bUseAngles, FIELD_INTEGER, "use_angles" ),
	DEFINE_KEYFIELD( m_flPitch, FIELD_FLOAT, "pitch" ),
	DEFINE_KEYFIELD( m_flYaw, FIELD_FLOAT, "angle" ),
	DEFINE_KEYFIELD( m_nSize, FIELD_INTEGER, "size" ),
	DEFINE_KEYFIELD( m_clrOverlay, FIELD_COLOR32, "overlaycolor" ),
	DEFINE_KEYFIELD( m_nOverlaySize, FIELD_INTEGER, "overlaysize" ),
	DEFINE_KEYFIELD( m_strMaterial, FIELD_STRING, "material" ),
	DEFINE_KEYFIELD( m_strOverlayMaterial, FIELD_STRING, "overlaymaterial" ),
	
	// NOT SAVED
	// m_nOverlayMaterial
	// m_nMaterial

	DEFINE_FIELD( m_bOn, FIELD_BOOLEAN ),

	DEFINE_INPUTFUNC( FIELD_VOID, "TurnOn", InputTurnOn ),
	DEFINE_INPUTFUNC( FIELD_VOID, "TurnOff", InputTurnOff ),
	DEFINE_INPUTFUNC( FIELD_COLOR32, "SetColor", InputSetColor ),

	DEFINE_KEYFIELD( m_flHDRColorScale,		FIELD_FLOAT,	"HDRColorScale" ),
END_DATADESC()

CSun::CSun()
{
	m_vDirection.Init( 0, 0, 1 );
	
	m_bUseAngles = false;
	m_flPitch = 0;
	m_flYaw = 0;
	m_nSize = 16;

	m_bOn = true;
	AddEFlags( EFL_FORCE_CHECK_TRANSMIT );

	m_strMaterial = NULL_STRING;
	m_strOverlayMaterial = NULL_STRING;
	m_nOverlaySize = -1;
}

void CSun::Activate()
{
	BaseClass::Activate();

	// Find our target.
	if ( m_bUseAngles )
	{
		SetupLightNormalFromProps( GetAbsAngles(), m_flYaw, m_flPitch, m_vDirection.GetForModify() );
		m_vDirection = -m_vDirection.Get();
	}
	else
	{
		CBaseEntity *pEnt = gEntList.FindEntityByName( 0, m_target );
		if( pEnt )
		{
			Vector vDirection = GetAbsOrigin() - pEnt->GetAbsOrigin();
			VectorNormalize( vDirection );
			m_vDirection = vDirection;
		}
	}

	// Default behavior
	if ( m_nOverlaySize == -1 )
	{
		m_nOverlaySize = m_nSize;
	}

	// Cache off our image indices
	if ( m_strMaterial == NULL_STRING )
	{
		m_strMaterial = AllocPooledString( "sprites/light_glow02_add_noz.vmt" );
	}
	else 
	{
		const char *pExtension = V_GetFileExtension( STRING( m_strMaterial ) );
		if ( !pExtension )
		{
			char szFixedString[MAX_PATH];
			V_strncpy( szFixedString, STRING( m_strMaterial ), sizeof( szFixedString ) );
			V_strncat( szFixedString, ".vmt", sizeof( szFixedString ) );
			m_strMaterial = AllocPooledString( szFixedString );
		}
	}

	if ( m_strOverlayMaterial == NULL_STRING )
	{
		m_strOverlayMaterial = AllocPooledString( "sprites/light_glow02_add_noz.vmt" );
	}
	else 
	{
		const char *pExtension = V_GetFileExtension( STRING( m_strOverlayMaterial ) );
		if ( !pExtension )
		{
			char szFixedString[MAX_PATH];
			V_strncpy( szFixedString, STRING( m_strOverlayMaterial ), sizeof( szFixedString ) );
			V_strncat( szFixedString, ".vmt", sizeof( szFixedString ) );
			m_strOverlayMaterial = AllocPooledString( szFixedString );
		}
	}

	m_nMaterial = PrecacheModel( STRING( m_strMaterial ) );
	m_nOverlayMaterial = PrecacheModel( STRING( m_strOverlayMaterial ) );
}

void CSun::InputTurnOn( inputdata_t &inputdata )
{
	if( !m_bOn )
	{
		m_bOn = true;
	}
}

void CSun::InputTurnOff( inputdata_t &inputdata )
{
	if ( m_bOn )
	{
		m_bOn = false;
	}
}

void CSun::InputSetColor( inputdata_t &inputdata )
{
	m_clrRender = inputdata.value.Color32();
}

#ifdef LUA_SDK
static bool Sun_ParseColor( const char *szValue, color32 &clr )
{
	int r = 255, g = 255, b = 255, a = 255;
	int nRead = sscanf( szValue, "%d %d %d %d", &r, &g, &b, &a );
	if ( nRead < 3 )
		return false;

	clr.r = clamp( r, 0, 255 );
	clr.g = clamp( g, 0, 255 );
	clr.b = clamp( b, 0, 255 );
	clr.a = ( nRead >= 4 ) ? clamp( a, 0, 255 ) : 255;
	return true;
}

bool CSun::KeyValue( const char *szKeyName, const char *szValue )
{
	if ( FStrEq( szKeyName, "sun_dir" ) )
	{
		Vector vecDir;
		UTIL_StringToVector( vecDir.Base(), szValue );

		if ( VectorNormalize( vecDir ) > 0.0f )
		{
			m_vDirection = vecDir;
		}
		return true;
	}

	if ( FStrEq( szKeyName, "size" ) )
	{
		m_nSize = clamp( atoi( szValue ), 0, 1023 );
		return true;
	}

	if ( FStrEq( szKeyName, "overlaysize" ) )
	{
		int nSize = atoi( szValue );
		m_nOverlaySize = ( nSize < 0 ) ? -1 : MIN( nSize, 1023 );
		return true;
	}

	if ( FStrEq( szKeyName, "overlaycolor" ) )
	{
		color32 clr;
		if ( Sun_ParseColor( szValue, clr ) )
		{
			m_clrOverlay = clr;
		}
		return true;
	}

	if ( FStrEq( szKeyName, "suncolor" ) )
	{
		color32 clr;
		if ( Sun_ParseColor( szValue, clr ) )
		{
			SetRenderColor( clr.r, clr.g, clr.b );
		}
		return true;
	}

	return BaseClass::KeyValue( szKeyName, szValue );
}
#endif

int CSun::UpdateTransmitState()
{
	return SetTransmitState( FL_EDICT_ALWAYS );
}


