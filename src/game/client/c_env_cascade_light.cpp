//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
//=============================================================================

#include "cbase.h"
#include "shareddefs.h"
#include "materialsystem/imesh.h"
#include "materialsystem/imaterial.h"
#include "materialsystem/itexture.h"
#include "view.h"
#include "iviewrender.h"
#include "view_shared.h"
#include "texture_group_names.h"
#include "tier0/icommandline.h"
#include "vguicenterprint.h"
#include "vtf/vtf.h"
#include "bitmap/imageformat.h"
#include "c_world.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

static ConVar scissor("r_flashlightscissor", "0");

//intensity
ConVar csm_intensity("csm_intensity", "1000");
ConVar csm_second_intensity("csm_second_intensity", "200");
ConVar csm_third_intensity("csm_third_intensity", "200");
ConVar csm_brightness_general("csm_brightness_general", "1");
ConVar csm_color_r("csm_color_r", "255");
ConVar csm_color_g("csm_color_g", "255");
ConVar csm_color_b("csm_color_b", "255");
ConVar csm_color_a("csm_color_a", "255");

ConVar csm_round_pos("csm_round_pos", "1");

ConVar csm_filter("csm_filter", "1");
ConVar csm_enable("csm_enable", "1");
// Srcbox: the cascades used the global mat_depthbias_shadowmap / mat_slopescaledepthbias_shadowmap,
// which this engine's config leaves larger than Mapbase's. The light is ~100000 units away, so the
// bias is multiplied by a huge depth range and every surface counted as lit (no shadows). Give the
// cascades their own, using the values Mapbase sets.
ConVar csm_depthbias( "csm_depthbias", "0.000005", 0, "Depth bias for the cascaded shadow lights" );
ConVar csm_slopescaledepthbias( "csm_slopescaledepthbias", "16", 0, "Slope-scaled depth bias for the cascaded shadow lights" );
ConVar csm_debug("csm_debug", "0", 0, "Srcbox: print each cascade light's position, parent and texture once a second");


//-----------------------------------------------------------------------------
// Srcbox: texel snapping and generated cascade masks
//
// 1) The cascades follow the player, so every small step moved each shadow map
//    texel by a fraction of a texel and the shadow edges crawled ("sliding").
//    Each cascade now moves only in whole texels across the light's view
//    (csm_snap), so shadows stay put while the cascades still follow you.
//
// 2) The masks were fixed 256x256 textures (tools/fakecsm/mask_center/_ring)
//    that didn't fit the cascade sizes: the first cascade faded out ~130 units
//    from the player but the second's hole reached ~270, leaving an unlit ring
//    around the player (the circle), and the second and third overlapped. The
//    masks are now generated from the cascades' real FOVs (csm_procedural_masks):
//    each cascade fades in exactly where the smaller one fades out, so together
//    they always add up to one.
//-----------------------------------------------------------------------------
ConVar csm_snap( "csm_snap", "1", 0, "Move the cascades in whole shadow map texels so shadows don't slide as the player moves" );
ConVar csm_procedural_masks( "csm_procedural_masks", "1", 0, "Build the cascade masks from the cascades' sizes so they blend without gaps (0 = use the mask textures from the entities)" );
ConVar csm_mask_fade_start( "csm_mask_fade_start", "0.55", 0, "Where each cascade starts fading out, as a fraction of its half width" );
ConVar csm_mask_fade_end( "csm_mask_fade_end", "0.92", 0, "Where each cascade has faded out, as a fraction of its half width" );

#define CSM_CASCADE_COUNT	3
#define CSM_MASK_SIZE		1024

static float g_flCSMFov[ CSM_CASCADE_COUNT ];			// the cascades' current FOVs (0 = not present)
static float g_flCSMMaskFov[ CSM_CASCADE_COUNT ];		// the FOVs the masks were last built for
static float g_flCSMMaskFade[ 2 ] = { -1.0f, -1.0f };
static ITexture *g_pCSMMask[ CSM_CASCADE_COUNT ];		// never released: freed with the material system

static float CSM_LinearToSRGB( float f )
{
	// the flashlight cookie is read with sRGB conversion, so store the weights encoded
	f = clamp( f, 0.0f, 1.0f );
	return ( f <= 0.0031308f ) ? f * 12.92f : 1.055f * powf( f, 1.0f / 2.4f ) - 0.055f;
}

// How much cascade j covers at distance r (in tan units, i.e. world distance / light distance)
static float CSM_Coverage( int j, float r )
{
	if ( g_flCSMMaskFov[ j ] <= 0.0f )
		return 0.0f;

	const float t = tanf( DEG2RAD( g_flCSMMaskFov[ j ] * 0.5f ) );
	const float a = g_flCSMMaskFade[ 0 ] * t;
	const float b = MAX( g_flCSMMaskFade[ 1 ] * t, a + 1e-6f );
	if ( r <= a )
		return 1.0f;
	if ( r >= b )
		return 0.0f;
	const float x = ( r - a ) / ( b - a );
	return 1.0f - x * x * ( 3.0f - 2.0f * x );	// smoothstep down
}

class CCSMMaskRegenerator : public ITextureRegenerator
{
public:
	int m_nCascade;

	virtual void RegenerateTextureBits( ITexture *pTexture, IVTFTexture *pVTFTexture, Rect_t *pRect )
	{
		const int w = pVTFTexture->Width();
		const int h = pVTFTexture->Height();
		const ImageFormat fmt = pVTFTexture->Format();
		const int bpp = ImageLoader::SizeInBytes( fmt );
		unsigned char *pBits = pVTFTexture->ImageData( 0, 0, 0 );
		if ( !pBits || bpp < 3 )
			return;

		const int i = m_nCascade;
		const float ti = ( g_flCSMMaskFov[ i ] > 0.0f ) ? tanf( DEG2RAD( g_flCSMMaskFov[ i ] * 0.5f ) ) : 0.0f;

		for ( int y = 0; y < h; y++ )
		{
			const float v = ( ( y + 0.5f ) / h ) * 2.0f - 1.0f;
			for ( int x = 0; x < w; x++ )
			{
				const float u = ( ( x + 0.5f ) / w ) * 2.0f - 1.0f;
				const float r = sqrtf( u * u + v * v ) * ti;

				// this cascade's share: its own coverage, minus whatever the smaller ones already light
				float flWeight = CSM_Coverage( i, r );
				for ( int j = 0; j < CSM_CASCADE_COUNT && flWeight > 0.0f; j++ )
				{
					if ( j != i && g_flCSMMaskFov[ j ] > 0.0f && g_flCSMMaskFov[ j ] < g_flCSMMaskFov[ i ] )
						flWeight *= 1.0f - CSM_Coverage( j, r );
				}

				const unsigned char c = (unsigned char)( CSM_LinearToSRGB( flWeight ) * 255.0f + 0.5f );
				unsigned char *p = pBits + ( y * w + x ) * bpp;
				p[ 0 ] = c;
				p[ 1 ] = c;
				p[ 2 ] = c;
				if ( bpp >= 4 )
					p[ 3 ] = 255;
			}
		}
	}

	virtual void Release()
	{
	}
};

static CCSMMaskRegenerator g_CSMMaskRegenerator[ CSM_CASCADE_COUNT ];

// The generated mask for cascade nCascade (0 = main, 1 = second, 2 = third), rebuilt whenever a
// cascade's FOV or the fade settings change
static ITexture *CSM_GetMask( int nCascade, float flFOV )
{
	g_flCSMFov[ nCascade ] = flFOV;

	if ( !g_pCSMMask[ nCascade ] )
	{
		char szName[ 64 ];
		Q_snprintf( szName, sizeof( szName ), "_rt_FakeCSMMask%d", nCascade );
		g_CSMMaskRegenerator[ nCascade ].m_nCascade = nCascade;
		g_pCSMMask[ nCascade ] = materials->CreateProceduralTexture( szName, TEXTURE_GROUP_OTHER, CSM_MASK_SIZE, CSM_MASK_SIZE, IMAGE_FORMAT_BGRA8888,
			TEXTUREFLAGS_CLAMPS | TEXTUREFLAGS_CLAMPT | TEXTUREFLAGS_NOMIP | TEXTUREFLAGS_NOLOD | TEXTUREFLAGS_PROCEDURAL | TEXTUREFLAGS_SINGLECOPY );
		if ( !g_pCSMMask[ nCascade ] )
			return NULL;
		g_pCSMMask[ nCascade ]->SetTextureRegenerator( &g_CSMMaskRegenerator[ nCascade ] );
		g_flCSMMaskFov[ nCascade ] = -1.0f;	// force a build below
	}

	bool bChanged = ( g_flCSMMaskFade[ 0 ] != csm_mask_fade_start.GetFloat() || g_flCSMMaskFade[ 1 ] != csm_mask_fade_end.GetFloat() );
	for ( int j = 0; j < CSM_CASCADE_COUNT; j++ )
	{
		if ( fabsf( g_flCSMMaskFov[ j ] - g_flCSMFov[ j ] ) > 0.0001f )
			bChanged = true;
	}

	if ( bChanged )
	{
		g_flCSMMaskFade[ 0 ] = csm_mask_fade_start.GetFloat();
		g_flCSMMaskFade[ 1 ] = csm_mask_fade_end.GetFloat();
		for ( int j = 0; j < CSM_CASCADE_COUNT; j++ )
			g_flCSMMaskFov[ j ] = g_flCSMFov[ j ];

		for ( int j = 0; j < CSM_CASCADE_COUNT; j++ )
		{
			if ( g_pCSMMask[ j ] )
				g_pCSMMask[ j ]->Download();
		}
	}

	return g_pCSMMask[ nCascade ];
}

// Move the light across its own view in whole shadow map texels
static void CSM_SnapToTexels( Vector &vPos, const Vector &vForward, const Vector &vRight, const Vector &vUp, float flFOV )
{
	if ( !csm_snap.GetBool() || flFOV <= 0.0f )
		return;

	// distance from the light to the player along the light: the size of a texel there
	double flDist = 100000.0;
	C_BasePlayer *pPlayer = C_BasePlayer::GetLocalPlayer();
	if ( pPlayer )
	{
		const Vector vDelta = pPlayer->GetAbsOrigin() - vPos;
		const double d = (double)vDelta.x * vForward.x + (double)vDelta.y * vForward.y + (double)vDelta.z * vForward.z;
		if ( d > 1.0 )
			flDist = d;
	}

	static ConVarRef r_flashlightdepthres( "r_flashlightdepthres" );
	int nRes = r_flashlightdepthres.IsValid() ? r_flashlightdepthres.GetInt() : 1024;
	if ( nRes <= 0 )
		nRes = 1024;

	const double flTexel = 2.0 * flDist * tan( DEG2RAD( flFOV * 0.5 ) ) / nRes;
	if ( flTexel <= 0.0 )
		return;

	// double precision: the light is ~100000 units out and the smallest texels are a fraction of a unit
	const double r = (double)vPos.x * vRight.x + (double)vPos.y * vRight.y + (double)vPos.z * vRight.z;
	const double u = (double)vPos.x * vUp.x + (double)vPos.y * vUp.y + (double)vPos.z * vUp.z;
	const double dr = floor( r / flTexel + 0.5 ) * flTexel - r;
	const double du = floor( u / flTexel + 0.5 ) * flTexel - u;

	vPos += vRight * (float)dr + vUp * (float)du;
}


//-----------------------------------------------------------------------------
// Srcbox: fixed (map-wide) sun, like Mapbase's orthographic projected texture.
// Instead of cascades that follow the player, the main light becomes one
// orthographic light over the whole map (or csm_fixed_size around its centre),
// so the lit area and the shadows are fixed to the map: no circle and nothing
// travelling with the player. The second and third cascades are off.
// Sharpness depends on r_flashlightdepthres (try 4096) and the map's size.
//-----------------------------------------------------------------------------
ConVar csm_fixed( "csm_fixed", "1", 0, "One orthographic sun light fixed over the whole map instead of cascades following the player" );
ConVar csm_fixed_distance( "csm_fixed_distance", "256", 0, "How far back the fixed sun light sits, in units (further = more even lighting direction; 256 = the original)" );
ConVar csm_fixed_filter( "csm_fixed_filter", "0.0005", 0, "Shadow edge softness of the fixed sun light (csm_filter is for the cascades); near 0 = sharp" );
ConVar csm_fixed_size( "csm_fixed_size", "4096", 0, "Half width of the fixed sun light in units, around where the env_cascade_light was placed (0 = fit the whole map's bounds, which includes the 3D skybox)" );

static ITexture *g_pCSMMaskFull;

class CCSMFullMaskRegenerator : public ITextureRegenerator
{
public:
	virtual void RegenerateTextureBits( ITexture *pTexture, IVTFTexture *pVTFTexture, Rect_t *pRect )
	{
		const int w = pVTFTexture->Width();
		const int h = pVTFTexture->Height();
		const int bpp = ImageLoader::SizeInBytes( pVTFTexture->Format() );
		unsigned char *pBits = pVTFTexture->ImageData( 0, 0, 0 );
		if ( !pBits || bpp < 3 )
			return;

		// white, with a one texel black border so nothing is lit outside the light (clamped)
		for ( int y = 0; y < h; y++ )
		{
			for ( int x = 0; x < w; x++ )
			{
				const bool bEdge = ( x == 0 || y == 0 || x == w - 1 || y == h - 1 );
				unsigned char *p = pBits + ( y * w + x ) * bpp;
				p[ 0 ] = p[ 1 ] = p[ 2 ] = bEdge ? 0 : 255;
				if ( bpp >= 4 )
					p[ 3 ] = 255;
			}
		}
	}
	virtual void Release() {}
};
static CCSMFullMaskRegenerator g_CSMFullMaskRegenerator;

static ITexture *CSM_GetFullMask()
{
	if ( !g_pCSMMaskFull )
	{
		g_pCSMMaskFull = materials->CreateProceduralTexture( "_rt_FakeCSMMaskFull", TEXTURE_GROUP_OTHER, 64, 64, IMAGE_FORMAT_BGRA8888,
			TEXTUREFLAGS_CLAMPS | TEXTUREFLAGS_CLAMPT | TEXTUREFLAGS_NOMIP | TEXTUREFLAGS_NOLOD | TEXTUREFLAGS_PROCEDURAL | TEXTUREFLAGS_SINGLECOPY | TEXTUREFLAGS_POINTSAMPLE );
		if ( g_pCSMMaskFull )
		{
			g_pCSMMaskFull->SetTextureRegenerator( &g_CSMFullMaskRegenerator );
			g_pCSMMaskFull->Download();
		}
	}
	return g_pCSMMaskFull;
}

// Turn the main light into the fixed map-wide orthographic sun. flCascadeDistance is how far the
// cascades sat from the player (csm_current_distance): the brightness is kept the same as theirs.
static bool CSM_SetupFixed( FlashlightState_t &state, const Vector &vForward, const Vector &vRight, const Vector &vUp, float flCascadeDistance, const Vector &vPlacedCenter )
{
	C_World *pWorld = GetClientWorldEntity();
	if ( !pWorld )
		return false;

	Vector vMins = pWorld->m_WorldMins;
	Vector vMaxs = pWorld->m_WorldMaxs;
	if ( vMins.x >= vMaxs.x || vMins.y >= vMaxs.y || vMins.z >= vMaxs.z )
		return false;

	if ( csm_fixed_size.GetFloat() > 0.0f )
	{
		// a box around where the entity was placed (the map's centre if that's unknown)
		const Vector vCenter = ( vPlacedCenter != vec3_origin ) ? vPlacedCenter : ( vMins + vMaxs ) * 0.5f;
		const float flHalf = csm_fixed_size.GetFloat();
		vMins.x = MAX( vMins.x, vCenter.x - flHalf ); vMaxs.x = MIN( vMaxs.x, vCenter.x + flHalf );
		vMins.y = MAX( vMins.y, vCenter.y - flHalf ); vMaxs.y = MIN( vMaxs.y, vCenter.y + flHalf );
		vMins.z = MAX( vMins.z, vCenter.z - flHalf ); vMaxs.z = MIN( vMaxs.z, vCenter.z + flHalf );
		if ( vMins.x >= vMaxs.x || vMins.y >= vMaxs.y || vMins.z >= vMaxs.z )
		{
			vMins = vCenter - Vector( flHalf, flHalf, flHalf );
			vMaxs = vCenter + Vector( flHalf, flHalf, flHalf );
		}
	}

	// the bounds in the light's own axes
	float flMin[ 3 ] = { FLT_MAX, FLT_MAX, FLT_MAX };
	float flMax[ 3 ] = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
	for ( int i = 0; i < 8; i++ )
	{
		const Vector vCorner( ( i & 1 ) ? vMaxs.x : vMins.x, ( i & 2 ) ? vMaxs.y : vMins.y, ( i & 4 ) ? vMaxs.z : vMins.z );
		const float f[ 3 ] = { DotProduct( vCorner, vRight ), DotProduct( vCorner, vUp ), DotProduct( vCorner, vForward ) };
		for ( int k = 0; k < 3; k++ )
		{
			flMin[ k ] = MIN( flMin[ k ], f[ k ] );
			flMax[ k ] = MAX( flMax[ k ], f[ k ] );
		}
	}

	const float flMargin = 256.0f;
	const float flHalf = MAX( flMax[ 0 ] - flMin[ 0 ], flMax[ 1 ] - flMin[ 1 ] ) * 0.5f + 16.0f;
	const float flMidR = ( flMin[ 0 ] + flMax[ 0 ] ) * 0.5f;
	const float flMidU = ( flMin[ 1 ] + flMax[ 1 ] ) * 0.5f;
	const float flDepth = flMax[ 2 ] - flMin[ 2 ];

	// how far back the light sits: further gives a more even lighting direction across the area
	// (the shaders work it out from the light's origin per pixel), default = as close as possible
	const float flBack = MAX( csm_fixed_distance.GetFloat(), flMargin );
	state.m_vecLightOrigin = vRight * flMidR + vUp * flMidU + vForward * ( flMin[ 2 ] - flBack );

	state.m_bOrtho = true;

	// The shadow depth view still gets this FOV (shadowView.fov), and the renderables in it are
	// culled against a frustum built from it: at the main cascade's ~0.15 degrees every model and
	// prop was culled, so only brushes cast shadows. Mapbase's orthographic projected textures
	// keep a normal FOV here; the ortho rect decides what is actually covered.
	state.m_fHorizontalFOVDegrees = 90.0f;
	state.m_fVerticalFOVDegrees = 90.0f;

	// softness of the shadow edges (csm_filter is tuned for the cascades)
	state.m_flShadowFilterSize = csm_fixed_filter.GetFloat();
	state.m_fOrthoLeft = -flHalf;
	state.m_fOrthoRight = flHalf;
	state.m_fOrthoTop = -flHalf;
	state.m_fOrthoBottom = flHalf;

	// the shaders fade the light out from 60% of FarZ, so keep the whole map inside that
	state.m_NearZ = 4.0f;
	state.m_FarZ = ( flBack + flDepth + flMargin ) / 0.6f;

	// same brightness as the cascades had at their distance, but even across the map
	if ( flCascadeDistance <= 0.0f )
		flCascadeDistance = 100000.0f;
	state.m_fConstantAtten = state.m_fLinearAtten / flCascadeDistance;
	state.m_fLinearAtten = 0.0f;
	state.m_fQuadraticAtten = 0.0f;

	ITexture *pFull = CSM_GetFullMask();
	if ( pFull )
		state.m_pSpotlightTexture = pFull;
	state.m_nSpotlightTextureFrame = 0;

	if ( csm_debug.GetBool() )
	{
		static float s_flNextFixedPrint = 0.0f;
		if ( gpGlobals->realtime >= s_flNextFixedPrint )
		{
			s_flNextFixedPrint = gpGlobals->realtime + 1.0f;
			Msg( "[csm] fixed: half width %.0f, depth %.0f, origin %.0f %.0f %.0f, %.1f units per shadow texel\n",
				flHalf, flDepth, state.m_vecLightOrigin.x, state.m_vecLightOrigin.y, state.m_vecLightOrigin.z,
				//flHalf * 2.0f / MAX( 1, ConVarRef( "r_flashlightdepthres" ).GetInt() ) );
				flHalf * 2.0f / MAX( 1, 4096 ) );
		}
	}
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: main point for change angle of the light
//-----------------------------------------------------------------------------


class C_LightOrigin : public C_BaseEntity
{
	DECLARE_CLASS(C_LightOrigin, C_BaseEntity);
public:
	DECLARE_CLIENTCLASS();

	void Update();

	virtual void Simulate();

private:
	Vector LightEnvVector;
	//QAngle LightEnvAngle;
};

IMPLEMENT_CLIENTCLASS_DT(C_LightOrigin, DT_LightOrigin, CLightOrigin)
RecvPropVector(RECVINFO(LightEnvVector))
END_RECV_TABLE()

void C_LightOrigin::Update()
{
	Vector pos = C_BasePlayer::GetLocalPlayer()->GetAbsOrigin();

	pos.x = ((int)(pos.x / csm_round_pos.GetInt())) * csm_round_pos.GetInt();
	pos.y = ((int)(pos.y / csm_round_pos.GetInt())) * csm_round_pos.GetInt();
	pos.z = ((int)(pos.z / csm_round_pos.GetInt())) * csm_round_pos.GetInt();

	SetAbsOrigin(pos);
}


void C_LightOrigin::Simulate()
{
	Update();
	BaseClass::Simulate();
	ConVarRef("r_farz").SetValue("210000");
}


//-----------------------------------------------------------------------------
// Purpose: main csm code	
//-----------------------------------------------------------------------------

class C_EnvCascadeLight : public C_BaseEntity
{
	DECLARE_CLASS(C_EnvCascadeLight, C_BaseEntity);
public:
	DECLARE_CLIENTCLASS();

	virtual void OnDataChanged(DataUpdateType_t updateType);
	void	ShutDownLightHandle(void);

	virtual void Simulate();

	void	UpdateLight(bool bForceUpdate);

	C_EnvCascadeLight();
	~C_EnvCascadeLight();


private:

	ClientShadowHandle_t m_LightHandle;

	EHANDLE	m_hTargetEntity;
	//color32	m_LightColor;

#ifdef MAPBASE
	float m_flBrightnessScale;
	float m_flCurrentBrightnessScale;
#endif
	Vector m_CurrentLinearFloatLightColor;
	float m_flCurrentLinearFloatLightAlpha;
	float m_flColorTransitionTime;
	bool	m_bState;
	float	m_flLightFOV;
	bool	m_bEnableShadows;
	bool	m_bLightOnlyTarget;
	bool	m_bLightWorld;
	bool	m_bCameraSpace;
	//Vector	m_LinearFloatLightColor;
	float	m_flAmbient;
	float	m_flNearZ;
	float	m_flFarZ;
	char	m_SpotlightTextureName[MAX_PATH];
	int		m_nSpotlightTextureFrame;
	int		m_nShadowQuality;
	// Srcbox: height above csm_origin, sent separately (the networked local origin wraps past 16384)
	float	m_flCSMDistance;
	Vector	m_vecFixedCenter;
};

IMPLEMENT_CLIENTCLASS_DT(C_EnvCascadeLight, DT_EnvCascadeLight, CEnvCascadeLight)
	RecvPropFloat(	 RECVINFO( m_flCSMDistance )	),
	RecvPropVector(	 RECVINFO( m_vecFixedCenter ) ),
	RecvPropEHandle( RECVINFO( m_hTargetEntity )	),
	RecvPropBool(	 RECVINFO( m_bState )			),
	RecvPropFloat(	 RECVINFO( m_flLightFOV )		),
	RecvPropBool(	 RECVINFO( m_bEnableShadows )	),
	RecvPropBool(	 RECVINFO( m_bLightOnlyTarget ) ),
	//RecvPropInt(RECVINFO(m_LightColor), 0, RecvProxy_IntToColor32),
	RecvPropBool(	 RECVINFO( m_bLightWorld )		),
	RecvPropBool(	 RECVINFO( m_bCameraSpace )		),
	//RecvPropVector(	 RECVINFO( m_LinearFloatLightColor )		),
	RecvPropFloat(	 RECVINFO( m_flAmbient )		),
	RecvPropString(  RECVINFO( m_SpotlightTextureName ) ),
	RecvPropInt(	 RECVINFO( m_nSpotlightTextureFrame ) ),
	RecvPropFloat(	 RECVINFO( m_flNearZ )	),
	RecvPropFloat(	 RECVINFO( m_flFarZ )	),
	RecvPropInt(	 RECVINFO( m_nShadowQuality )	)
END_RECV_TABLE()

C_EnvCascadeLight::C_EnvCascadeLight( void )
{
	m_flCSMDistance = 0.0f;
	
	m_LightHandle = CLIENTSHADOW_INVALID_HANDLE;
	// Srcbox: these were never set, so the colour could start from garbage or crawl towards
	// the real one at a garbage speed. Start at the current colour; changes apply instantly.
	m_flColorTransitionTime = 0.0f;
	m_CurrentLinearFloatLightColor.Init( csm_color_r.GetFloat(), csm_color_g.GetFloat(), csm_color_b.GetFloat() );
	m_flCurrentLinearFloatLightAlpha = csm_color_a.GetFloat();
#ifdef MAPBASE
	m_flBrightnessScale = 1.0f;
	m_flCurrentBrightnessScale = 1.0f;
#endif
}


C_EnvCascadeLight::~C_EnvCascadeLight( void )
{
	ShutDownLightHandle();
}

void C_EnvCascadeLight::ShutDownLightHandle( void )
{
	g_flCSMFov[ 0 ] = 0.0f;
	if (m_LightHandle != CLIENTSHADOW_INVALID_HANDLE)
	{
		g_pClientShadowMgr->DestroyFlashlight(m_LightHandle);
		m_LightHandle = CLIENTSHADOW_INVALID_HANDLE;
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : updateType - how do you increase the light's height?
//-----------------------------------------------------------------------------
void C_EnvCascadeLight::OnDataChanged( DataUpdateType_t updateType )
{
	UpdateLight( true );

	BaseClass::OnDataChanged( updateType );
}


void C_EnvCascadeLight::UpdateLight( bool bForceUpdate )
{

	if (m_bState == false)
	{
		if(m_LightHandle!=CLIENTSHADOW_INVALID_HANDLE)
		ShutDownLightHandle();
		return;
	}

	Vector vForward, vRight, vUp, vPos = GetAbsOrigin();
	FlashlightState_t state;
	state.m_flShadowFilterSize = csm_filter.GetFloat();
	

	if (m_hTargetEntity != NULL)
	{
		if (m_bCameraSpace)
		{
			const QAngle& angles = GetLocalAngles();

			C_BasePlayer* pPlayer = C_BasePlayer::GetLocalPlayer();
			if (pPlayer)
			{
				const QAngle playerAngles = pPlayer->GetAbsAngles();

				Vector vPlayerForward, vPlayerRight, vPlayerUp;
				AngleVectors(playerAngles, &vPlayerForward, &vPlayerRight, &vPlayerUp);

				matrix3x4_t	mRotMatrix;
				AngleMatrix(angles, mRotMatrix);

				VectorITransform(vPlayerForward, mRotMatrix, vForward);
				VectorITransform(vPlayerRight, mRotMatrix, vRight);
				VectorITransform(vPlayerUp, mRotMatrix, vUp);

				float dist = (m_hTargetEntity->GetAbsOrigin() - GetAbsOrigin()).Length();
				vPos = m_hTargetEntity->GetAbsOrigin() - vForward * dist;

				VectorNormalize(vForward);
				VectorNormalize(vRight);
				VectorNormalize(vUp);
			}
		}
		else
		{
			vForward = m_hTargetEntity->GetAbsOrigin() - GetAbsOrigin();
			VectorNormalize(vForward);

			Assert(0);

		}
	}
	else
	{
		AngleVectors(GetAbsAngles(), &vForward, &vRight, &vUp);
	}
	

	Vector vLinearFloatLightColor(csm_color_r.GetInt(), csm_color_g.GetInt(), csm_color_b.GetInt());
	float flLinearFloatLightAlpha = csm_color_a.GetInt();

#ifdef MAPBASE
	if (m_CurrentLinearFloatLightColor != vLinearFloatLightColor || m_flCurrentLinearFloatLightAlpha != flLinearFloatLightAlpha)
	{
		if (m_flColorTransitionTime != 0.0f)
		{
			float flColorTransitionSpeed = gpGlobals->frametime * m_flColorTransitionTime * 255.0f;

			m_CurrentLinearFloatLightColor.x = Approach(vLinearFloatLightColor.x, m_CurrentLinearFloatLightColor.x, flColorTransitionSpeed);
			m_CurrentLinearFloatLightColor.y = Approach(vLinearFloatLightColor.y, m_CurrentLinearFloatLightColor.y, flColorTransitionSpeed);
			m_CurrentLinearFloatLightColor.z = Approach(vLinearFloatLightColor.z, m_CurrentLinearFloatLightColor.z, flColorTransitionSpeed);
			m_flCurrentLinearFloatLightAlpha = Approach(flLinearFloatLightAlpha, m_flCurrentLinearFloatLightAlpha, flColorTransitionSpeed);
			//m_flCurrentBrightnessScale = Approach(m_flBrightnessScale, m_flCurrentBrightnessScale, flColorTransitionSpeed);
		}
		else
		{
			// Just do it instantly
			m_CurrentLinearFloatLightColor.x = vLinearFloatLightColor.x;
			m_CurrentLinearFloatLightColor.y = vLinearFloatLightColor.y;
			m_CurrentLinearFloatLightColor.z = vLinearFloatLightColor.z;
			m_flCurrentLinearFloatLightAlpha = flLinearFloatLightAlpha;
			//m_flCurrentBrightnessScale = m_flBrightnessScale;
		}
	}
#else
	if (m_CurrentLinearFloatLightColor != vLinearFloatLightColor || m_flCurrentLinearFloatLightAlpha != flLinearFloatLightAlpha)
	{
		float flColorTransitionSpeed = gpGlobals->frametime * m_flColorTransitionTime * 255.0f;

		m_CurrentLinearFloatLightColor.x = Approach(vLinearFloatLightColor.x, m_CurrentLinearFloatLightColor.x, flColorTransitionSpeed);
		m_CurrentLinearFloatLightColor.y = Approach(vLinearFloatLightColor.y, m_CurrentLinearFloatLightColor.y, flColorTransitionSpeed);
		m_CurrentLinearFloatLightColor.z = Approach(vLinearFloatLightColor.z, m_CurrentLinearFloatLightColor.z, flColorTransitionSpeed);
		m_flCurrentLinearFloatLightAlpha = Approach(flLinearFloatLightAlpha, m_flCurrentLinearFloatLightAlpha, flColorTransitionSpeed);
	}
#endif



	state.m_fHorizontalFOVDegrees = m_flLightFOV;
	state.m_fVerticalFOVDegrees = m_flLightFOV;

	CSM_SnapToTexels( vPos, vForward, vRight, vUp, m_flLightFOV );
	state.m_vecLightOrigin = vPos;

	// Srcbox: csm_debug 1 prints where each cascade really is, relative to the player
	if ( csm_debug.GetBool() )
	{
		static float s_flNextDebugPrint = 0.0f;
		if ( gpGlobals->realtime >= s_flNextDebugPrint )
		{
			s_flNextDebugPrint = gpGlobals->realtime + 1.0f;
			ITexture *pDebugTexture = materials->FindTexture( m_SpotlightTextureName, TEXTURE_GROUP_OTHER, false );
			C_BasePlayer *pDebugPlayer = C_BasePlayer::GetLocalPlayer();
			C_BaseEntity *pParent = GetMoveParent();
			Msg( "[csm] %s: light %.0f %.0f %.0f | player %.0f %.0f %.0f | fov %.1f near %.0f far %.0f | parent %s (%.0f %.0f %.0f) attach %d | local %.0f %.0f %.0f | fwd %.2f %.2f %.2f | tex '%s' %s\n",
				GetClassname(), vPos.x, vPos.y, vPos.z,
				pDebugPlayer ? pDebugPlayer->GetAbsOrigin().x : 0.0f, pDebugPlayer ? pDebugPlayer->GetAbsOrigin().y : 0.0f, pDebugPlayer ? pDebugPlayer->GetAbsOrigin().z : 0.0f,
				m_flLightFOV, m_flNearZ, m_flFarZ,
				pParent ? pParent->GetClassname() : "none",
				pParent ? pParent->GetAbsOrigin().x : 0.0f, pParent ? pParent->GetAbsOrigin().y : 0.0f, pParent ? pParent->GetAbsOrigin().z : 0.0f,
				GetParentAttachment(),
				GetLocalOrigin().x, GetLocalOrigin().y, GetLocalOrigin().z,
				vForward.x, vForward.y, vForward.z,
				m_SpotlightTextureName, ( pDebugTexture && !pDebugTexture->IsError() ) ? "ok" : "MISSING" );
		}
	}
	BasisToQuaternion(vForward, vRight, vUp, state.m_quatOrientation);

	state.m_fQuadraticAtten = 0.0;
	state.m_fLinearAtten = 100;
	state.m_fConstantAtten = 0.0f;
	//state.m_Color[0] = m_LinearFloatLightColor.x;
	//state.m_Color[1] = m_LinearFloatLightColor.y;
	//state.m_Color[2] = m_LinearFloatLightColor.z;

#ifdef MAPBASE
	float flAlpha = m_flCurrentLinearFloatLightAlpha * (1.0f / 255.0f);
	state.m_Color[0] = (m_CurrentLinearFloatLightColor.x * (1.0f / 255.0f) * flAlpha) * csm_intensity.GetFloat() * csm_brightness_general.GetFloat();
	state.m_Color[1] = (m_CurrentLinearFloatLightColor.y * (1.0f / 255.0f) * flAlpha) * csm_intensity.GetFloat() * csm_brightness_general.GetFloat();
	state.m_Color[2] = (m_CurrentLinearFloatLightColor.z * (1.0f / 255.0f) * flAlpha) * csm_intensity.GetFloat() * csm_brightness_general.GetFloat();
#else
	state.m_Color[0] = m_CurrentLinearFloatLightColor.x * (1.0f / 255.0f) * csm_intensity.GetFloat();
	state.m_Color[1] = m_CurrentLinearFloatLightColor.y * (1.0f / 255.0f) * csm_intensity.GetFloat();
	state.m_Color[2] = m_CurrentLinearFloatLightColor.z * (1.0f / 255.0f) * csm_intensity.GetFloat();
#endif

	state.m_Color[3] = m_flAmbient; // fixme: need to make ambient work m_flAmbient;
	state.m_NearZ = m_flNearZ;
	state.m_FarZ = m_flFarZ;
	state.m_flShadowSlopeScaleDepthBias = csm_slopescaledepthbias.GetFloat();//csm_slopescaledepthbias_shadowmap.GetFloat();
	state.m_flShadowDepthBias = csm_depthbias.GetFloat();//csm_depthbias_shadowmap.GetFloat();
	state.m_bEnableShadows = m_bEnableShadows;
	state.m_pSpotlightTexture = csm_procedural_masks.GetBool() ? CSM_GetMask( 0, m_flLightFOV ) : NULL;
	if ( !state.m_pSpotlightTexture )
		state.m_pSpotlightTexture = materials->FindTexture(m_SpotlightTextureName, TEXTURE_GROUP_OTHER, false);
	state.m_nSpotlightTextureFrame = m_nSpotlightTextureFrame;

	state.m_nShadowQuality = m_nShadowQuality; // Allow entity to affect shadow quality

	if ( csm_fixed.GetBool() )
	{
		CSM_SetupFixed( state, vForward, vRight, vUp, m_flCSMDistance, m_vecFixedCenter );
	}

	if (m_LightHandle == CLIENTSHADOW_INVALID_HANDLE)
	{
		m_LightHandle = g_pClientShadowMgr->CreateFlashlight(state);
	}
	else
	{
		if (m_hTargetEntity != NULL || bForceUpdate == true)
		{
			g_pClientShadowMgr->UpdateFlashlightState(m_LightHandle, state);
		}
	}

	if (m_bLightOnlyTarget)
	{
		g_pClientShadowMgr->SetFlashlightTarget(m_LightHandle, m_hTargetEntity);
	}
	else
	{
		g_pClientShadowMgr->SetFlashlightTarget(m_LightHandle, NULL);
	}

	g_pClientShadowMgr->SetFlashlightLightWorld(m_LightHandle, m_bLightWorld);


	g_pClientShadowMgr->UpdateProjectedTexture(m_LightHandle, true);


}

void C_EnvCascadeLight::Simulate( void )
{
	// Srcbox: put the light back at its real height above csm_origin. Interpolation restores the
	// wrapped networked origin every frame, and this runs after it, before anything renders. The
	// second and third cascades are parented to this light, so they follow.
	if ( m_flCSMDistance > 0.0f && GetMoveParent() )
	{
		SetLocalOrigin( Vector( 0, 0, m_flCSMDistance ) );
	}

	
	m_bState = csm_enable.GetBool();
	UpdateLight(true);
	BaseClass::Simulate();
}


//-----------------------------------------------------------------------------
// Purpose:	second csm	
//-----------------------------------------------------------------------------

class C_EnvCascadeLightSecond : public C_BaseEntity
{
	DECLARE_CLASS(C_EnvCascadeLightSecond, C_BaseEntity);
public:
	DECLARE_CLIENTCLASS();

	virtual void OnDataChanged(DataUpdateType_t updateType);
	void	ShutDownLightHandle(void);

	virtual void Simulate();

	void	UpdateLight(bool bForceUpdate);
	void updatePos();

	C_EnvCascadeLightSecond();
	~C_EnvCascadeLightSecond();


private:

	ClientShadowHandle_t m_LightHandle;

	//color32	m_LightColor;

#ifdef MAPBASE
	float m_flBrightnessScale;
	float m_flCurrentBrightnessScale;
#endif
	Vector m_CurrentLinearFloatLightColor;
	float m_flCurrentLinearFloatLightAlpha;
	float m_flColorTransitionTime;

	EHANDLE	m_hTargetEntity;
	CBaseEntity* pEntity = NULL;
	bool	firstUpdate = true;
	bool	m_bState;	
	float	m_flLightFOV;
	bool	m_bEnableShadows;
	bool	m_bLightOnlyTarget;
	bool	m_bLightWorld;
	bool	m_bCameraSpace;
	//Vector	m_LinearFloatLightColor;
	float	m_flAmbient;
	float	m_flNearZ;
	float	m_flFarZ;
	char	m_SpotlightTextureName[MAX_PATH];
	int		m_nSpotlightTextureFrame;
	int		m_nShadowQuality;
};

IMPLEMENT_CLIENTCLASS_DT(C_EnvCascadeLightSecond, DT_EnvCascadeLightSecond, CEnvCascadeLightSecond)
//RecvPropInt(RECVINFO(m_LightColor), 0, RecvProxy_IntToColor32),
RecvPropEHandle(RECVINFO(m_hTargetEntity)),
RecvPropBool(RECVINFO(m_bState)),
RecvPropFloat(RECVINFO(m_flLightFOV)),
RecvPropBool(RECVINFO(m_bEnableShadows)),
RecvPropBool(RECVINFO(m_bLightOnlyTarget)),
RecvPropBool(RECVINFO(m_bLightWorld)),
RecvPropBool(RECVINFO(m_bCameraSpace)),
//RecvPropVector(RECVINFO(m_LinearFloatLightColor)),
RecvPropFloat(RECVINFO(m_flAmbient)),
RecvPropString(RECVINFO(m_SpotlightTextureName)),
RecvPropInt(RECVINFO(m_nSpotlightTextureFrame)),
RecvPropFloat(RECVINFO(m_flNearZ)),
RecvPropFloat(RECVINFO(m_flFarZ)),
RecvPropInt(RECVINFO(m_nShadowQuality))
END_RECV_TABLE()

C_EnvCascadeLightSecond::C_EnvCascadeLightSecond(void)
{
	m_LightHandle = CLIENTSHADOW_INVALID_HANDLE;
	// Srcbox: these were never set, so the colour could start from garbage or crawl towards
	// the real one at a garbage speed. Start at the current colour; changes apply instantly.
	m_flColorTransitionTime = 0.0f;
	m_CurrentLinearFloatLightColor.Init( csm_color_r.GetFloat(), csm_color_g.GetFloat(), csm_color_b.GetFloat() );
	m_flCurrentLinearFloatLightAlpha = csm_color_a.GetFloat();
#ifdef MAPBASE
	m_flBrightnessScale = 1.0f;
	m_flCurrentBrightnessScale = 1.0f;
#endif
}


C_EnvCascadeLightSecond::~C_EnvCascadeLightSecond(void)
{
	ShutDownLightHandle();
}

void C_EnvCascadeLightSecond::ShutDownLightHandle(void)
{
	g_flCSMFov[ 1 ] = 0.0f;
	if (m_LightHandle != CLIENTSHADOW_INVALID_HANDLE)
	{
		g_pClientShadowMgr->DestroyFlashlight(m_LightHandle);
		m_LightHandle = CLIENTSHADOW_INVALID_HANDLE;
	}
}

void C_EnvCascadeLightSecond::OnDataChanged(DataUpdateType_t updateType)
{
	UpdateLight(true);
	BaseClass::OnDataChanged(updateType);
}

void C_EnvCascadeLightSecond::UpdateLight(bool bForceUpdate)
{

	if (m_bState == false || csm_fixed.GetBool())
	{
		
		if(m_LightHandle!=CLIENTSHADOW_INVALID_HANDLE)
		ShutDownLightHandle();
		return;
	}



	Vector vForward, vRight, vUp, vPos = GetAbsOrigin();
	FlashlightState_t state;
	state.m_flShadowFilterSize = csm_filter.GetFloat();

	if (m_hTargetEntity != NULL)
	{
		if (m_bCameraSpace)
		{
			const QAngle& angles = GetLocalAngles();

			C_BasePlayer* pPlayer = C_BasePlayer::GetLocalPlayer();
			if (pPlayer)
			{
				const QAngle playerAngles = pPlayer->GetAbsAngles();

				Vector vPlayerForward, vPlayerRight, vPlayerUp;
				AngleVectors(playerAngles, &vPlayerForward, &vPlayerRight, &vPlayerUp);

				matrix3x4_t	mRotMatrix;
				AngleMatrix(angles, mRotMatrix);

				VectorITransform(vPlayerForward, mRotMatrix, vForward);
				VectorITransform(vPlayerRight, mRotMatrix, vRight);
				VectorITransform(vPlayerUp, mRotMatrix, vUp);

				float dist = (m_hTargetEntity->GetAbsOrigin() - GetAbsOrigin()).Length();
				vPos = m_hTargetEntity->GetAbsOrigin() - vForward * dist;

				VectorNormalize(vForward);
				VectorNormalize(vRight);
				VectorNormalize(vUp);
			}
		}
		else
		{
			vForward = m_hTargetEntity->GetAbsOrigin() - GetAbsOrigin();
			VectorNormalize(vForward);
			Assert(0);
		}
	}
	else
	{
		AngleVectors(GetAbsAngles(), &vForward, &vRight, &vUp);
	}

	Vector vLinearFloatLightColor(csm_color_r.GetInt(), csm_color_g.GetInt(), csm_color_b.GetInt());
	float flLinearFloatLightAlpha = csm_color_a.GetInt();

#ifdef MAPBASE
	if (m_CurrentLinearFloatLightColor != vLinearFloatLightColor || m_flCurrentLinearFloatLightAlpha != flLinearFloatLightAlpha)
	{
		if (m_flColorTransitionTime != 0.0f)
		{
			float flColorTransitionSpeed = gpGlobals->frametime * m_flColorTransitionTime * 255.0f;

			m_CurrentLinearFloatLightColor.x = Approach(vLinearFloatLightColor.x, m_CurrentLinearFloatLightColor.x, flColorTransitionSpeed);
			m_CurrentLinearFloatLightColor.y = Approach(vLinearFloatLightColor.y, m_CurrentLinearFloatLightColor.y, flColorTransitionSpeed);
			m_CurrentLinearFloatLightColor.z = Approach(vLinearFloatLightColor.z, m_CurrentLinearFloatLightColor.z, flColorTransitionSpeed);
			m_flCurrentLinearFloatLightAlpha = Approach(flLinearFloatLightAlpha, m_flCurrentLinearFloatLightAlpha, flColorTransitionSpeed);
			//m_flCurrentBrightnessScale = Approach(m_flBrightnessScale, m_flCurrentBrightnessScale, flColorTransitionSpeed);
		}
		else
		{
			// Just do it instantly
			m_CurrentLinearFloatLightColor.x = vLinearFloatLightColor.x;
			m_CurrentLinearFloatLightColor.y = vLinearFloatLightColor.y;
			m_CurrentLinearFloatLightColor.z = vLinearFloatLightColor.z;
			m_flCurrentLinearFloatLightAlpha = flLinearFloatLightAlpha;
			//m_flCurrentBrightnessScale = m_flBrightnessScale;
		}
	}
#else
	if (m_CurrentLinearFloatLightColor != vLinearFloatLightColor || m_flCurrentLinearFloatLightAlpha != flLinearFloatLightAlpha)
	{
		float flColorTransitionSpeed = gpGlobals->frametime * m_flColorTransitionTime * 255.0f;

		m_CurrentLinearFloatLightColor.x = Approach(vLinearFloatLightColor.x, m_CurrentLinearFloatLightColor.x, flColorTransitionSpeed);
		m_CurrentLinearFloatLightColor.y = Approach(vLinearFloatLightColor.y, m_CurrentLinearFloatLightColor.y, flColorTransitionSpeed);
		m_CurrentLinearFloatLightColor.z = Approach(vLinearFloatLightColor.z, m_CurrentLinearFloatLightColor.z, flColorTransitionSpeed);
		m_flCurrentLinearFloatLightAlpha = Approach(flLinearFloatLightAlpha, m_flCurrentLinearFloatLightAlpha, flColorTransitionSpeed);
	}
#endif


	state.m_fHorizontalFOVDegrees = m_flLightFOV;
	state.m_fVerticalFOVDegrees = m_flLightFOV;

	CSM_SnapToTexels( vPos, vForward, vRight, vUp, m_flLightFOV );
	state.m_vecLightOrigin = vPos;

	// Srcbox: csm_debug 1 prints where each cascade really is, relative to the player
	if ( csm_debug.GetBool() )
	{
		static float s_flNextDebugPrint = 0.0f;
		if ( gpGlobals->realtime >= s_flNextDebugPrint )
		{
			s_flNextDebugPrint = gpGlobals->realtime + 1.0f;
			ITexture *pDebugTexture = materials->FindTexture( m_SpotlightTextureName, TEXTURE_GROUP_OTHER, false );
			C_BasePlayer *pDebugPlayer = C_BasePlayer::GetLocalPlayer();
			C_BaseEntity *pParent = GetMoveParent();
			Msg( "[csm] %s: light %.0f %.0f %.0f | player %.0f %.0f %.0f | fov %.1f near %.0f far %.0f | parent %s (%.0f %.0f %.0f) attach %d | local %.0f %.0f %.0f | fwd %.2f %.2f %.2f | tex '%s' %s\n",
				GetClassname(), vPos.x, vPos.y, vPos.z,
				pDebugPlayer ? pDebugPlayer->GetAbsOrigin().x : 0.0f, pDebugPlayer ? pDebugPlayer->GetAbsOrigin().y : 0.0f, pDebugPlayer ? pDebugPlayer->GetAbsOrigin().z : 0.0f,
				m_flLightFOV, m_flNearZ, m_flFarZ,
				pParent ? pParent->GetClassname() : "none",
				pParent ? pParent->GetAbsOrigin().x : 0.0f, pParent ? pParent->GetAbsOrigin().y : 0.0f, pParent ? pParent->GetAbsOrigin().z : 0.0f,
				GetParentAttachment(),
				GetLocalOrigin().x, GetLocalOrigin().y, GetLocalOrigin().z,
				vForward.x, vForward.y, vForward.z,
				m_SpotlightTextureName, ( pDebugTexture && !pDebugTexture->IsError() ) ? "ok" : "MISSING" );
		}
	}
	BasisToQuaternion(vForward, vRight, vUp, state.m_quatOrientation);

	state.m_fQuadraticAtten = 0.0;
	state.m_fLinearAtten = 100;
	state.m_fConstantAtten = 0.0f;

#ifdef MAPBASE
	float flAlpha = m_flCurrentLinearFloatLightAlpha * (1.0f / 255.0f);
	state.m_Color[0] = (m_CurrentLinearFloatLightColor.x * (1.0f / 255.0f) * flAlpha) * csm_second_intensity.GetFloat() * csm_brightness_general.GetFloat();
	state.m_Color[1] = (m_CurrentLinearFloatLightColor.y * (1.0f / 255.0f) * flAlpha) * csm_second_intensity.GetFloat() * csm_brightness_general.GetFloat();
	state.m_Color[2] = (m_CurrentLinearFloatLightColor.z * (1.0f / 255.0f) * flAlpha) * csm_second_intensity.GetFloat() * csm_brightness_general.GetFloat();
#else
	state.m_Color[0] = m_CurrentLinearFloatLightColor.x * (1.0f / 255.0f) * csm_second_intensity.GetFloat();
	state.m_Color[1] = m_CurrentLinearFloatLightColor.y * (1.0f / 255.0f) * csm_second_intensity.GetFloat();
	state.m_Color[2] = m_CurrentLinearFloatLightColor.z * (1.0f / 255.0f) * csm_second_intensity.GetFloat();
#endif

	state.m_Color[3] = 0.0f; // fixme: need to make ambient work m_flAmbient;
	state.m_NearZ = m_flNearZ;
	state.m_FarZ = m_flFarZ;
	state.m_flShadowSlopeScaleDepthBias = csm_slopescaledepthbias.GetFloat();
	state.m_flShadowDepthBias = csm_depthbias.GetFloat();
	state.m_bEnableShadows = m_bEnableShadows;
	state.m_pSpotlightTexture = csm_procedural_masks.GetBool() ? CSM_GetMask( 1, m_flLightFOV ) : NULL;
	if ( !state.m_pSpotlightTexture )
		state.m_pSpotlightTexture = materials->FindTexture(m_SpotlightTextureName, TEXTURE_GROUP_OTHER, false);
	state.m_nSpotlightTextureFrame = m_nSpotlightTextureFrame;

	state.m_nShadowQuality = m_nShadowQuality; // Allow entity to affect shadow quality
	

	if (m_LightHandle == CLIENTSHADOW_INVALID_HANDLE)
	{
		m_LightHandle = g_pClientShadowMgr->CreateFlashlight(state);
	}
	else
	{
		if (m_hTargetEntity != NULL || bForceUpdate == true)
		{
			g_pClientShadowMgr->UpdateFlashlightState(m_LightHandle, state);
		}
	}

	if (m_bLightOnlyTarget)
	{
		g_pClientShadowMgr->SetFlashlightTarget(m_LightHandle, m_hTargetEntity);
	}
	else
	{
		g_pClientShadowMgr->SetFlashlightTarget(m_LightHandle, NULL);
	}

	g_pClientShadowMgr->SetFlashlightLightWorld(m_LightHandle, m_bLightWorld);

	g_pClientShadowMgr->UpdateProjectedTexture(m_LightHandle, true);
	
	scissor.SetValue("0");
}


void C_EnvCascadeLightSecond::Simulate(void)
{
	m_bState = csm_enable.GetBool();
	UpdateLight(true);
	BaseClass::Simulate();
}

//-----------------------------------------------------------------------------
// Purpose:	second csm	
//-----------------------------------------------------------------------------

class C_EnvCascadeLightThird : public C_BaseEntity
{
	DECLARE_CLASS(C_EnvCascadeLightThird, C_BaseEntity);
public:
	DECLARE_CLIENTCLASS();

	virtual void OnDataChanged(DataUpdateType_t updateType);
	void	ShutDownLightHandle(void);

	virtual void Simulate();

	void	UpdateLight(bool bForceUpdate);
	void updatePos();

	C_EnvCascadeLightThird();
	~C_EnvCascadeLightThird();


private:

	ClientShadowHandle_t m_LightHandle;

	//color32	m_LightColor;

#ifdef MAPBASE
	float m_flBrightnessScale;
	float m_flCurrentBrightnessScale;
#endif
	Vector m_CurrentLinearFloatLightColor;
	float m_flCurrentLinearFloatLightAlpha;
	float m_flColorTransitionTime;

	EHANDLE	m_hTargetEntity;
	CBaseEntity* pEntity = NULL;
	bool	firstUpdate = true;
	bool	m_bState;
	float	m_flLightFOV;
	bool	m_bEnableShadows;
	bool	m_bLightOnlyTarget;
	bool	m_bLightWorld;
	bool	m_bCameraSpace;
	//Vector	m_LinearFloatLightColor;
	float	m_flAmbient;
	float	m_flNearZ;
	float	m_flFarZ;
	char	m_SpotlightTextureName[MAX_PATH];
	int		m_nSpotlightTextureFrame;
	int		m_nShadowQuality;
};

IMPLEMENT_CLIENTCLASS_DT(C_EnvCascadeLightThird, DT_EnvCascadeLightThird, CEnvCascadeLightThird)
//RecvPropInt(RECVINFO(m_LightColor), 0, RecvProxy_IntToColor32),	
RecvPropEHandle(RECVINFO(m_hTargetEntity)),
RecvPropBool(RECVINFO(m_bState)),
RecvPropFloat(RECVINFO(m_flLightFOV)),
RecvPropBool(RECVINFO(m_bEnableShadows)),
RecvPropBool(RECVINFO(m_bLightOnlyTarget)),
RecvPropBool(RECVINFO(m_bLightWorld)),
RecvPropBool(RECVINFO(m_bCameraSpace)),
//RecvPropVector(RECVINFO(m_LinearFloatLightColor)),
RecvPropFloat(RECVINFO(m_flAmbient)),
RecvPropString(RECVINFO(m_SpotlightTextureName)),
RecvPropInt(RECVINFO(m_nSpotlightTextureFrame)),
RecvPropFloat(RECVINFO(m_flNearZ)),
RecvPropFloat(RECVINFO(m_flFarZ)),
RecvPropInt(RECVINFO(m_nShadowQuality))
END_RECV_TABLE()

C_EnvCascadeLightThird::C_EnvCascadeLightThird(void)
{
	m_LightHandle = CLIENTSHADOW_INVALID_HANDLE;
	// Srcbox: these were never set, so the colour could start from garbage or crawl towards
	// the real one at a garbage speed. Start at the current colour; changes apply instantly.
	m_flColorTransitionTime = 0.0f;
	m_CurrentLinearFloatLightColor.Init( csm_color_r.GetFloat(), csm_color_g.GetFloat(), csm_color_b.GetFloat() );
	m_flCurrentLinearFloatLightAlpha = csm_color_a.GetFloat();
#ifdef MAPBASE
	m_flBrightnessScale = 1.0f;
	m_flCurrentBrightnessScale = 1.0f;
#endif
}


C_EnvCascadeLightThird::~C_EnvCascadeLightThird(void)
{
	ShutDownLightHandle();
}

void C_EnvCascadeLightThird::ShutDownLightHandle(void)
{
	g_flCSMFov[ 2 ] = 0.0f;
	if (m_LightHandle != CLIENTSHADOW_INVALID_HANDLE)
	{
		g_pClientShadowMgr->DestroyFlashlight(m_LightHandle);
		m_LightHandle = CLIENTSHADOW_INVALID_HANDLE;
	}
}

void C_EnvCascadeLightThird::OnDataChanged(DataUpdateType_t updateType)
{
	UpdateLight(true);
	BaseClass::OnDataChanged(updateType);
}

void C_EnvCascadeLightThird::UpdateLight(bool bForceUpdate)
{

	if (m_bState == false || csm_fixed.GetBool())
	{
		if (m_LightHandle != CLIENTSHADOW_INVALID_HANDLE)
			ShutDownLightHandle();
		return;
	}

	Vector vForward, vRight, vUp, vPos = GetAbsOrigin();
	FlashlightState_t state;
	state.m_flShadowFilterSize = csm_filter.GetFloat();

	if (m_hTargetEntity != NULL)
	{
		if (m_bCameraSpace)
		{
			const QAngle& angles = GetLocalAngles();

			C_BasePlayer* pPlayer = C_BasePlayer::GetLocalPlayer();
			if (pPlayer)
			{
				const QAngle playerAngles = pPlayer->GetAbsAngles();

				Vector vPlayerForward, vPlayerRight, vPlayerUp;
				AngleVectors(playerAngles, &vPlayerForward, &vPlayerRight, &vPlayerUp);

				matrix3x4_t	mRotMatrix;
				AngleMatrix(angles, mRotMatrix);

				VectorITransform(vPlayerForward, mRotMatrix, vForward);
				VectorITransform(vPlayerRight, mRotMatrix, vRight);
				VectorITransform(vPlayerUp, mRotMatrix, vUp);

				float dist = (m_hTargetEntity->GetAbsOrigin() - GetAbsOrigin()).Length();
				vPos = m_hTargetEntity->GetAbsOrigin() - vForward * dist;

				VectorNormalize(vForward);
				VectorNormalize(vRight);
				VectorNormalize(vUp);
			}
		}
		else
		{
			vForward = m_hTargetEntity->GetAbsOrigin() - GetAbsOrigin();
			VectorNormalize(vForward);
			Assert(0);
		}
	}
	else
	{
		AngleVectors(GetAbsAngles(), &vForward, &vRight, &vUp);
	}

	Vector vLinearFloatLightColor(csm_color_r.GetInt(), csm_color_g.GetInt(), csm_color_b.GetInt());
	float flLinearFloatLightAlpha = csm_color_a.GetInt();

#ifdef MAPBASE
	if (m_CurrentLinearFloatLightColor != vLinearFloatLightColor || m_flCurrentLinearFloatLightAlpha != flLinearFloatLightAlpha)
	{
		if (m_flColorTransitionTime != 0.0f)
		{
			float flColorTransitionSpeed = gpGlobals->frametime * m_flColorTransitionTime * 255.0f;

			m_CurrentLinearFloatLightColor.x = Approach(vLinearFloatLightColor.x, m_CurrentLinearFloatLightColor.x, flColorTransitionSpeed);
			m_CurrentLinearFloatLightColor.y = Approach(vLinearFloatLightColor.y, m_CurrentLinearFloatLightColor.y, flColorTransitionSpeed);
			m_CurrentLinearFloatLightColor.z = Approach(vLinearFloatLightColor.z, m_CurrentLinearFloatLightColor.z, flColorTransitionSpeed);
			m_flCurrentLinearFloatLightAlpha = Approach(flLinearFloatLightAlpha, m_flCurrentLinearFloatLightAlpha, flColorTransitionSpeed);
			//m_flCurrentBrightnessScale = Approach(m_flBrightnessScale, m_flCurrentBrightnessScale, flColorTransitionSpeed);
		}
		else
		{
			// Just do it instantly
			m_CurrentLinearFloatLightColor.x = vLinearFloatLightColor.x;
			m_CurrentLinearFloatLightColor.y = vLinearFloatLightColor.y;
			m_CurrentLinearFloatLightColor.z = vLinearFloatLightColor.z;
			m_flCurrentLinearFloatLightAlpha = flLinearFloatLightAlpha;
			//m_flCurrentBrightnessScale = m_flBrightnessScale;
		}
	}
#else
	if (m_CurrentLinearFloatLightColor != vLinearFloatLightColor || m_flCurrentLinearFloatLightAlpha != flLinearFloatLightAlpha)
	{
		float flColorTransitionSpeed = gpGlobals->frametime * m_flColorTransitionTime * 255.0f;

		m_CurrentLinearFloatLightColor.x = Approach(vLinearFloatLightColor.x, m_CurrentLinearFloatLightColor.x, flColorTransitionSpeed);
		m_CurrentLinearFloatLightColor.y = Approach(vLinearFloatLightColor.y, m_CurrentLinearFloatLightColor.y, flColorTransitionSpeed);
		m_CurrentLinearFloatLightColor.z = Approach(vLinearFloatLightColor.z, m_CurrentLinearFloatLightColor.z, flColorTransitionSpeed);
		m_flCurrentLinearFloatLightAlpha = Approach(flLinearFloatLightAlpha, m_flCurrentLinearFloatLightAlpha, flColorTransitionSpeed);
	}
#endif



	state.m_fHorizontalFOVDegrees = m_flLightFOV;
	state.m_fVerticalFOVDegrees = m_flLightFOV;

	CSM_SnapToTexels( vPos, vForward, vRight, vUp, m_flLightFOV );
	state.m_vecLightOrigin = vPos;

	// Srcbox: csm_debug 1 prints where each cascade really is, relative to the player
	if ( csm_debug.GetBool() )
	{
		static float s_flNextDebugPrint = 0.0f;
		if ( gpGlobals->realtime >= s_flNextDebugPrint )
		{
			s_flNextDebugPrint = gpGlobals->realtime + 1.0f;
			ITexture *pDebugTexture = materials->FindTexture( m_SpotlightTextureName, TEXTURE_GROUP_OTHER, false );
			C_BasePlayer *pDebugPlayer = C_BasePlayer::GetLocalPlayer();
			C_BaseEntity *pParent = GetMoveParent();
			Msg( "[csm] %s: light %.0f %.0f %.0f | player %.0f %.0f %.0f | fov %.1f near %.0f far %.0f | parent %s (%.0f %.0f %.0f) attach %d | local %.0f %.0f %.0f | fwd %.2f %.2f %.2f | tex '%s' %s\n",
				GetClassname(), vPos.x, vPos.y, vPos.z,
				pDebugPlayer ? pDebugPlayer->GetAbsOrigin().x : 0.0f, pDebugPlayer ? pDebugPlayer->GetAbsOrigin().y : 0.0f, pDebugPlayer ? pDebugPlayer->GetAbsOrigin().z : 0.0f,
				m_flLightFOV, m_flNearZ, m_flFarZ,
				pParent ? pParent->GetClassname() : "none",
				pParent ? pParent->GetAbsOrigin().x : 0.0f, pParent ? pParent->GetAbsOrigin().y : 0.0f, pParent ? pParent->GetAbsOrigin().z : 0.0f,
				GetParentAttachment(),
				GetLocalOrigin().x, GetLocalOrigin().y, GetLocalOrigin().z,
				vForward.x, vForward.y, vForward.z,
				m_SpotlightTextureName, ( pDebugTexture && !pDebugTexture->IsError() ) ? "ok" : "MISSING" );
		}
	}
	BasisToQuaternion(vForward, vRight, vUp, state.m_quatOrientation);

	state.m_fQuadraticAtten = 0.0;
	state.m_fLinearAtten = 100;
	state.m_fConstantAtten = 0.0f;
	//state.m_Color[0] = m_LinearFloatLightColor.x;
	//state.m_Color[1] = m_LinearFloatLightColor.y;
	//state.m_Color[2] = m_LinearFloatLightColor.z;

#ifdef MAPBASE
	float flAlpha = m_flCurrentLinearFloatLightAlpha * (1.0f / 255.0f);
	state.m_Color[0] = (m_CurrentLinearFloatLightColor.x * (1.0f / 255.0f) * flAlpha) * csm_third_intensity.GetFloat() * csm_brightness_general.GetFloat();
	state.m_Color[1] = (m_CurrentLinearFloatLightColor.y * (1.0f / 255.0f) * flAlpha) * csm_third_intensity.GetFloat() * csm_brightness_general.GetFloat();
	state.m_Color[2] = (m_CurrentLinearFloatLightColor.z * (1.0f / 255.0f) * flAlpha) * csm_third_intensity.GetFloat() * csm_brightness_general.GetFloat();
#else
	state.m_Color[0] = m_CurrentLinearFloatLightColor.x * (1.0f / 255.0f) * csm_third_intensity.GetFloat();
	state.m_Color[1] = m_CurrentLinearFloatLightColor.y * (1.0f / 255.0f) * csm_third_intensity.GetFloat();
	state.m_Color[2] = m_CurrentLinearFloatLightColor.z * (1.0f / 255.0f) * csm_third_intensity.GetFloat();
#endif

	state.m_Color[3] = 0.0f; // fixme: need to make ambient work m_flAmbient;
	state.m_NearZ = m_flNearZ;
	state.m_FarZ = m_flFarZ;
	state.m_flShadowSlopeScaleDepthBias = csm_slopescaledepthbias.GetFloat();//csm_slopescaledepthbias_shadowmap.GetFloat();
	state.m_flShadowDepthBias = csm_depthbias.GetFloat();//csm_depthbias_shadowmap.GetFloat();
	state.m_bEnableShadows = m_bEnableShadows;
	state.m_pSpotlightTexture = csm_procedural_masks.GetBool() ? CSM_GetMask( 2, m_flLightFOV ) : NULL;
	if ( !state.m_pSpotlightTexture )
		state.m_pSpotlightTexture = materials->FindTexture(m_SpotlightTextureName, TEXTURE_GROUP_OTHER, false);
	state.m_nSpotlightTextureFrame = m_nSpotlightTextureFrame;

	state.m_nShadowQuality = m_nShadowQuality; // Allow entity to affect shadow quality

	if (m_LightHandle == CLIENTSHADOW_INVALID_HANDLE)
	{
		m_LightHandle = g_pClientShadowMgr->CreateFlashlight(state);
	}
	else
	{
		if (m_hTargetEntity != NULL || bForceUpdate == true)
		{
			g_pClientShadowMgr->UpdateFlashlightState(m_LightHandle, state);
		}
	}

	if (m_bLightOnlyTarget)
	{
		g_pClientShadowMgr->SetFlashlightTarget(m_LightHandle, m_hTargetEntity);
	}
	else
	{
		g_pClientShadowMgr->SetFlashlightTarget(m_LightHandle, NULL);
	}

	g_pClientShadowMgr->SetFlashlightLightWorld(m_LightHandle, m_bLightWorld);

#ifdef MAPBASE
	if (state.m_bOrtho)
	{
		bool bSupressWorldLights = false;

		bSupressWorldLights = m_bEnableShadows;

		g_pClientShadowMgr->SetShadowFromWorldLightsEnabled(!bSupressWorldLights);
	}
#endif // MAPBASE

	g_pClientShadowMgr->UpdateProjectedTexture(m_LightHandle, true);

	scissor.SetValue("0");
}


void C_EnvCascadeLightThird::Simulate(void)
{
	m_bState = csm_enable.GetBool();
	UpdateLight(true);
	BaseClass::Simulate();
}
