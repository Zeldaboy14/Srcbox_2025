//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
//===========================================================================//

#include "cbase.h"
#include "flashlighteffect.h"
#include "dlight.h"
#include "iefx.h"
#include "iviewrender.h"
#include "view.h"
#include "engine/ivdebugoverlay.h"
#include "tier0/vprof.h"
#include "tier1/KeyValues.h"
#include "toolframework_client.h"

#ifdef HL2_CLIENT_DLL
#include "c_basehlplayer.h"
#endif // HL2_CLIENT_DLL

static ConVar srcbox_flashlight_version("srcbox_flashlight_version", "0", FCVAR_REPLICATED | FCVAR_ARCHIVE, "Select which flashlight from a version of Source (or Goldsource) to use. 1 is Half-Life's (Goldsource), 2 is Half-Life 2: Episode 2, and 3 is Left 4 Dead (2). Default is 0");

#if defined( _X360 )
extern ConVar r_flashlightdepthres;
#else
extern ConVar r_flashlightdepthres;
#endif

#define m_MuzzleFlashTexture m_FlashlightTexture

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

bool bTracePlayers;

extern ConVar r_flashlightdepthtexture;

void r_newflashlightCallback_f( IConVar *pConVar, const char *pOldString, float flOldValue );

static ConVar r_swingflashlight( "r_swingflashlight", "1", FCVAR_CHEAT );
static ConVar r_flashlightlockposition( "r_flashlightlockposition", "0", FCVAR_CHEAT );
static ConVar r_flashlightfov( "r_flashlightfov", "45.0", FCVAR_CHEAT );
static ConVar r_flashlightnear( "r_flashlightnear", "4.0", FCVAR_CHEAT );
static ConVar r_flashlightfar( "r_flashlightfar", "750.0", FCVAR_CHEAT );
static ConVar r_flashlightconstant( "r_flashlightconstant", "0.0", FCVAR_CHEAT );
static ConVar r_flashlightlinear( "r_flashlightlinear", "100.0", FCVAR_CHEAT );
static ConVar r_flashlightquadratic( "r_flashlightquadratic", "0.0", FCVAR_CHEAT );
static ConVar r_flashlightvisualizetrace( "r_flashlightvisualizetrace", "0", FCVAR_CHEAT );
static ConVar r_flashlightambient( "r_flashlightambient", "0.0", FCVAR_CHEAT );
static ConVar r_flashlightshadowatten( "r_flashlightshadowatten", "0.35", FCVAR_CHEAT );
static ConVar r_flashlightladderdist( "r_flashlightladderdist", "40.0", FCVAR_CHEAT );
static ConVar mat_slopescaledepthbias_shadowmap( "mat_slopescaledepthbias_shadowmap", "16", FCVAR_CHEAT );
static ConVar mat_depthbias_shadowmap(	"mat_depthbias_shadowmap", "0.0005", FCVAR_CHEAT  );

static ConVar r_flashlightnearoffsetscale("r_flashlightnearoffsetscale", "1.0", FCVAR_CHEAT);
static ConVar r_flashlighttracedistcutoff("r_flashlighttracedistcutoff", "128");
static ConVar r_flashlightbacktraceoffset("r_flashlightbacktraceoffset", "0.4", FCVAR_CHEAT);

ConVar r_flashlightoffsetx("r_flashlightoffsetright", "5.0", FCVAR_CHEAT);
ConVar r_flashlightoffsety("r_flashlightoffsetup", "-5.0", FCVAR_CHEAT);
ConVar r_flashlightoffsetz("r_flashlightoffsetforward", "0.0", FCVAR_CHEAT);

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : nEntIndex - The m_nEntIndex of the client entity that is creating us.
//			vecPos - The position of the light emitter.
//			vecDir - The direction of the light emission.
//-----------------------------------------------------------------------------
CFlashlightEffect::CFlashlightEffect(int nEntIndex)
{
	int flashlight_version = srcbox_flashlight_version.GetInt();
	m_FlashlightHandle = CLIENTSHADOW_INVALID_HANDLE;
	m_nEntIndex = nEntIndex;

	m_bIsOn = false;
	m_pPointLight = NULL;
	switch (flashlight_version)
	{
	case 1:
		//HL1:S thru EP1 Flashlight
		m_FlashlightTexture.Init("effects/flashlight001", TEXTURE_GROUP_OTHER, true);
		break;
	case 2:
		//Half-Life 1 Gldsrc Flashlight
		m_FlashlightTexture.Init("effects/flashlight001", TEXTURE_GROUP_OTHER, true);
		break;
	case 3:
		//Left 4 Dead (2) Flashlight
		m_FlashlightTexture.Init("effects/flashlight001_l4d", TEXTURE_GROUP_OTHER, true);
		break;
	//case 4:
		// TODO: Episode 3's flashlight (joke)
	default:
		//Msg("Unknown flashlight version: %d\n. Defaulting to Half-Life 2...", flashlight_version);
		m_FlashlightTexture.Init("effects/flashlight001", TEXTURE_GROUP_OTHER, true);
		break;
	}
}


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
CFlashlightEffect::~CFlashlightEffect()
{
	LightOff();
}


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CFlashlightEffect::TurnOn()
{
	m_bIsOn = true;
	m_flDistMod = 1.0f;
}


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CFlashlightEffect::TurnOff()
{
	if (m_bIsOn)
	{
		m_bIsOn = false;
		LightOff();
	}
}

// Custom trace filter that skips the player and the view model.
// If we don't do this, we'll end up having the light right in front of us all
// the time.
class CTraceFilterSkipPlayerAndViewModel : public CTraceFilter
{
public:
	CTraceFilterSkipPlayerAndViewModel(C_BasePlayer *pPlayer, bool bTracePlayers)
	{
		m_pPlayer = pPlayer;
		m_bSkipPlayers = !bTracePlayers;

		{
			m_pLowerBody = NULL;
		}
	}

	virtual bool ShouldHitEntity( IHandleEntity *pServerEntity, int contentsMask )
	{
		// Test against the vehicle too?
		// FLASHLIGHTFIXME: how do you know that you are actually inside of the vehicle?
		C_BaseEntity *pEntity = EntityFromEntityHandle( pServerEntity );
		if ( !pEntity )
			return true;

		if ( ( dynamic_cast<C_BaseViewModel *>( pEntity ) != NULL ) ||
			 ( dynamic_cast<C_BasePlayer *>( pEntity ) != NULL ) ||
			 pEntity->GetCollisionGroup() == COLLISION_GROUP_DEBRIS ||
			 pEntity->GetCollisionGroup() == COLLISION_GROUP_INTERACTIVE_DEBRIS )
		{
			return false;
		}

		return true;
	}

private:
	C_BaseEntity *m_pPlayer;
	C_BaseEntity *m_pLowerBody;
	bool m_bSkipPlayers;
};

void C_BasePlayer::GetFlashlightOffset(const Vector& vecForward, const Vector& vecRight, const Vector& vecUp, Vector* pVecOffset) const
{
	*pVecOffset = r_flashlightoffsety.GetFloat() * vecUp + r_flashlightoffsetx.GetFloat() * vecRight + r_flashlightoffsetz.GetFloat() * vecForward;
}


//-----------------------------------------------------------------------------
// Purpose: Do the headlight
//-----------------------------------------------------------------------------
void CFlashlightEffect::UpdateLightNew(const Vector &vecPos, const Vector &vecForward, const Vector &vecRight, const Vector &vecUp )
{
	VPROF_BUDGET( "CFlashlightEffect::UpdateLightNew", VPROF_BUDGETGROUP_SHADOW_DEPTH_TEXTURING );

	FlashlightState_t state;

	const float flEpsilon = 0.1f;			// Offset flashlight position along vecUp
	float flDistCutoff = r_flashlighttracedistcutoff.GetFloat(); // Swarm does it this way
	const float flDistDrag = 0.2;
	bool bDebugVis = r_flashlightvisualizetrace.GetBool();

	C_BasePlayer *pPlayer = UTIL_PlayerByIndex(m_nEntIndex);
	if (!pPlayer)
	{

		pPlayer = C_BasePlayer::GetLocalPlayer();

		if (!pPlayer)
		{
			Assert(false);
			//return false;
		}
	}

	// We will lock some of the flashlight params if player is on a ladder, to prevent oscillations due to the trace-rays
	bool bPlayerOnLadder = (pPlayer->GetMoveType() == MOVETYPE_LADDER);

	//CTraceFilterSkipPlayerAndViewModel traceFilter;
	CTraceFilterSkipPlayerAndViewModel traceFilter(pPlayer, bTracePlayers);
	float flOffsetY = r_flashlightoffsety.GetFloat();

	if( r_swingflashlight.GetBool() )
	{
		// This projects the view direction backwards, attempting to raise the vertical
		// offset of the flashlight, but only when the player is looking down.
		Vector vecSwingLight = vecPos + vecForward * -12.0f;
		if( vecSwingLight.z > vecPos.z )
		{
			flOffsetY += (vecSwingLight.z - vecPos.z);
		}
	}

	Vector vecOffset;
	pPlayer->GetFlashlightOffset(vecForward, vecRight, vecUp, &vecOffset);
	Vector vOrigin = vecPos + flOffsetY * vecUp;

	// Not on ladder...trace a hull
	if (!bPlayerOnLadder)
	{
		Vector vecPlayerEyePos = pPlayer->GetRenderOrigin() + pPlayer->GetViewOffset();

		trace_t pmOriginTrace;
		UTIL_TraceHull(vecPlayerEyePos, vOrigin, Vector(-2, -2, -2), Vector(2, 2, 2), (MASK_SOLID & ~(CONTENTS_HITBOX)) | CONTENTS_WINDOW | CONTENTS_GRATE, &traceFilter, &pmOriginTrace);//1

		if (bDebugVis)
		{
			debugoverlay->AddBoxOverlay(pmOriginTrace.endpos, Vector(-2, -2, -2), Vector(2, 2, 2), QAngle(0, 0, 0), 0, 255, 0, 16, 0);
			if (pmOriginTrace.DidHit() || pmOriginTrace.startsolid)
			{
				debugoverlay->AddLineOverlay(pmOriginTrace.startpos, pmOriginTrace.endpos, 255, 128, 128, true, 0);
			}
			else
			{
				debugoverlay->AddLineOverlay(pmOriginTrace.startpos, pmOriginTrace.endpos, 255, 0, 0, true, 0);
			}
		}

		if (pmOriginTrace.DidHit() || pmOriginTrace.startsolid)
		{
			vOrigin = pmOriginTrace.endpos;
		}
		else
		{
			if (pPlayer->m_vecFlashlightOrigin != vecPlayerEyePos)
			{
				vOrigin = vecPos;
			}
		}
	}

	// Now do a trace along the flashlight direction to ensure there is nothing within range to pull back from
	int iMask = MASK_OPAQUE_AND_NPCS;
	iMask &= ~CONTENTS_HITBOX;
	iMask |= CONTENTS_WINDOW;

	Vector vTarget = vecPos + vecForward * r_flashlightfar.GetFloat();

	// Work with these local copies of the basis for the rest of the function
	Vector vDir   = vTarget - vOrigin;
	Vector vRight = vecRight;
	Vector vUp    = vecUp;
	VectorNormalize( vDir   );
	VectorNormalize( vRight );
	VectorNormalize( vUp    );

	//m_hLaserSight = pEnt;

	// Orthonormalize the basis, since the flashlight texture projection will require this later...
	vUp -= DotProduct( vDir, vUp ) * vDir;
	VectorNormalize( vUp );
	vRight -= DotProduct( vDir, vRight ) * vDir;
	VectorNormalize( vRight );
	vRight -= DotProduct( vUp, vRight ) * vUp;
	VectorNormalize( vRight );

	AssertFloatEquals( DotProduct( vDir, vRight ), 0.0f, 1e-3 );
	AssertFloatEquals( DotProduct( vDir, vUp    ), 0.0f, 1e-3 );
	AssertFloatEquals( DotProduct( vRight, vUp  ), 0.0f, 1e-3 );

	trace_t pmDirectionTrace;
	UTIL_TraceHull( vOrigin, vTarget, Vector( -4, -4, -4 ), Vector( 4, 4, 4 ), iMask, &traceFilter, &pmDirectionTrace );

	if ( r_flashlightvisualizetrace.GetBool() == true )
	{
		debugoverlay->AddBoxOverlay( pmDirectionTrace.endpos, Vector( -4, -4, -4 ), Vector( 4, 4, 4 ), QAngle( 0, 0, 0 ), 0, 0, 255, 16, 0 );
		debugoverlay->AddLineOverlay( vOrigin, pmDirectionTrace.endpos, 255, 0, 0, false, 0 );
	}

	float flDist = (pmDirectionTrace.endpos - vOrigin).Length();
	if ( flDist < flDistCutoff )
	{
		// We have an intersection with our cutoff range
		// Determine how far to pull back, then trace to see if we are clear
		float flPullBackDist = bPlayerOnLadder ? r_flashlightladderdist.GetFloat() : flDistCutoff - flDist;	// Fixed pull-back distance if on ladder
		m_flDistMod = Lerp( flDistDrag, m_flDistMod, flPullBackDist );
		
		if ( !bPlayerOnLadder )
		{
			trace_t pmBackTrace;
			UTIL_TraceHull( vOrigin, vOrigin - vDir*(flPullBackDist-flEpsilon), Vector( -4, -4, -4 ), Vector( 4, 4, 4 ), iMask, &traceFilter, &pmBackTrace );
			if( pmBackTrace.DidHit() )
			{
				// We have an intersection behind us as well, so limit our m_flDistMod
				float flMaxDist = (pmBackTrace.endpos - vOrigin).Length() - flEpsilon;
				if( m_flDistMod > flMaxDist )
					m_flDistMod = flMaxDist;
			}
		}
	}
	else
	{
		m_flDistMod = Lerp( flDistDrag, m_flDistMod, 0.0f );
	}
	vOrigin = vOrigin - vDir * m_flDistMod;

	state.m_vecLightOrigin = vOrigin;

	BasisToQuaternion( vDir, vRight, vUp, state.m_quatOrientation );

	state.m_fQuadraticAtten = r_flashlightquadratic.GetFloat();

	bool bFlicker = false;

//todo: make this work. may work now under mp...
/*
#ifdef HL2_EPISODIC
	C_BaseHLPlayer *pPlayer = (C_BaseHLPlayer *)C_BasePlayer::GetLocalPlayer();
	switch (flashlight_version)
	{
	case 3:
		//C_BaseHLPlayer *pPlayer = (C_BaseHLPlayer *)C_BasePlayer::GetLocalPlayer();
		if (pPlayer)
		{
			float flBatteryPower = (pPlayer->m_HL2Local.m_flFlashBattery >= 0.0f) ? (pPlayer->m_HL2Local.m_flFlashBattery) : pPlayer->m_HL2Local.m_flSuitPower;
			if (flBatteryPower <= 10.0f)
			{
				float flScale;
				if (flBatteryPower >= 0.0f)
				{
					flScale = (flBatteryPower <= 4.5f) ? SimpleSplineRemapVal(flBatteryPower, 4.5f, 0.0f, 1.0f, 0.0f) : 1.0f;
				}
				else
				{
					flScale = SimpleSplineRemapVal(flBatteryPower, 10.0f, 4.8f, 1.0f, 0.0f);
				}

				flScale = clamp(flScale, 0.0f, 1.0f);

				if (flScale < 0.35f)
				{
					float flFlicker = cosf(gpGlobals->curtime * 6.0f) * sinf(gpGlobals->curtime * 15.0f);

					if (flFlicker > 0.25f && flFlicker < 0.75f)
					{
						// On
						state.m_fLinearAtten = r_flashlightlinear.GetFloat() * flScale;
					}
					else
					{
						// Off
						state.m_fLinearAtten = 0.0f;
					}
				}
				else
				{
					float flNoise = cosf(gpGlobals->curtime * 7.0f) * sinf(gpGlobals->curtime * 25.0f);
					state.m_fLinearAtten = r_flashlightlinear.GetFloat() * flScale + 1.5f * flNoise;
				}

				state.m_fHorizontalFOVDegrees = r_flashlightfov.GetFloat() - (16.0f * (1.0f - flScale));
				state.m_fVerticalFOVDegrees = r_flashlightfov.GetFloat() - (16.0f * (1.0f - flScale));

				bFlicker = true;
			}
		}
	default:
		return;
	}
#endif // HL2_EPISODIC
*/

	if ( bFlicker == false )
	{
		state.m_fLinearAtten = r_flashlightlinear.GetFloat();
		state.m_fHorizontalFOVDegrees = r_flashlightfov.GetFloat();
		state.m_fVerticalFOVDegrees = r_flashlightfov.GetFloat();
	}

	state.m_fConstantAtten = r_flashlightconstant.GetFloat();
	state.m_Color[0] = 1.0f;
	state.m_Color[1] = 1.0f;
	state.m_Color[2] = 1.0f;
	state.m_Color[3] = r_flashlightambient.GetFloat();
	state.m_NearZ = r_flashlightnear.GetFloat() + m_flDistMod;	// Push near plane out so that we don't clip the world when the flashlight pulls back 
	state.m_FarZ = r_flashlightfar.GetFloat();
	state.m_bEnableShadows = r_flashlightdepthtexture.GetBool();
	state.m_flShadowMapResolution = r_flashlightdepthres.GetInt();

	state.m_pSpotlightTexture = m_FlashlightTexture;
	state.m_nSpotlightTextureFrame = 0;

	state.m_flShadowAtten = r_flashlightshadowatten.GetFloat();
	state.m_flShadowSlopeScaleDepthBias = mat_slopescaledepthbias_shadowmap.GetFloat();
	state.m_flShadowDepthBias = mat_depthbias_shadowmap.GetFloat();

	if( m_FlashlightHandle == CLIENTSHADOW_INVALID_HANDLE )
	{
		m_FlashlightHandle = g_pClientShadowMgr->CreateFlashlight( state );
	}
	else
	{
		if( !r_flashlightlockposition.GetBool() )
		{
			g_pClientShadowMgr->UpdateFlashlightState( m_FlashlightHandle, state );
		}
	}
	
	g_pClientShadowMgr->UpdateProjectedTexture( m_FlashlightHandle, true );
	
	// Kill the old flashlight method if we have one.
	LightOffOld();

#ifndef NO_TOOLFRAMEWORK
	if ( clienttools->IsInRecordingMode() )
	{
		KeyValues *msg = new KeyValues( "FlashlightState" );
		msg->SetFloat( "time", gpGlobals->curtime );
		msg->SetInt( "entindex", m_nEntIndex );
		msg->SetInt( "flashlightHandle", m_FlashlightHandle );
		msg->SetPtr( "flashlightState", &state );
		ToolFramework_PostToolMessage( HTOOLHANDLE_INVALID, msg );
		msg->deleteThis();
	}
#endif
}

//-----------------------------------------------------------------------------
// Purpose: Do the headlight
//-----------------------------------------------------------------------------
void CFlashlightEffect::UpdateLightOld(const Vector &vecPos, const Vector &vecDir, int nDistance)
{
	if ( !m_pPointLight || ( m_pPointLight->key != m_nEntIndex ))
	{
		// Set up the environment light
		m_pPointLight = effects->CL_AllocDlight(m_nEntIndex);
		m_pPointLight->flags = 0.0f;
		m_pPointLight->radius = 80;
	}
	
	// For bumped lighting
	VectorCopy(vecDir, m_pPointLight->m_Direction);
	
	Vector end;
	end = vecPos + nDistance * vecDir;
	C_BasePlayer *pPlayer = UTIL_PlayerByIndex(m_nEntIndex);
	
	// Trace a line outward, skipping the player model and the view model.
	trace_t pm;
	//CTraceFilterSkipPlayerAndViewModel traceFilter;
	CTraceFilterSkipPlayerAndViewModel traceFilter(pPlayer, bTracePlayers);
	UTIL_TraceLine( vecPos, end, MASK_ALL, &traceFilter, &pm );
	VectorCopy( pm.endpos, m_pPointLight->origin );
	
	float falloff = pm.fraction * nDistance;
	
	if ( falloff < 500 )
		falloff = 1.0;
	else
		falloff = 500.0 / falloff;
	
	falloff *= falloff;
	
	m_pPointLight->radius = 80;
	m_pPointLight->color.r = m_pPointLight->color.g = m_pPointLight->color.b = 255 * falloff;
	m_pPointLight->color.exponent = 0;
	
	// Make it live for a bit
	m_pPointLight->die = gpGlobals->curtime + 0.2f;
	
	// Update list of surfaces we influence
	render->TouchLight( m_pPointLight );
	
	// kill the new flashlight if we have one
	LightOffNew();
}

#define MOLOTOV_ATTACHMENT_POINT "muzzle_flashlight"

//-----------------------------------------------------------------------------
// Purpose: Do the headlight
//-----------------------------------------------------------------------------
void CFlashlightEffect::UpdateLight(const Vector &vecPos, const Vector &vecDir, const Vector &vecRight, const Vector &vecUp, int nDistance)
{
	int flashlight_version = srcbox_flashlight_version.GetInt();

	C_BaseAnimating* pEnt = new C_BaseAnimating;
	if (!pEnt)
	{
		Msg("Error, couldn't create new C_BaseAnimating\n");
		return;
	}
	pEnt->SetLocalOrigin(Vector(0, 0, 0));
	pEnt->SetLocalAngles(QAngle(0, 0, 0));
	pEnt->SetSolid(SOLID_NONE);
	pEnt->RemoveEFlags(EFL_USE_PARTITION_WHEN_NOT_SOLID);

	/*extern C_BaseAnimating* GetBaseAnimating();

	if (GetBaseAnimating()->LookupAttachment(MOLOTOV_ATTACHMENT_POINT) != INVALID_PARTICLE_ATTACHMENT)
	{
		UpdateLightNew(vecPos, vecDir, vecRight, vecUp);
	}*/


	if ( !m_bIsOn )
	{
		return;
	}

	switch (flashlight_version)
	{
	case 1:
		// Half-Life: Source
		UpdateLightOld(vecPos, vecDir, nDistance);
		break;
	case 2:
		// Half-Life 2 and Episode 1
		UpdateLightNew(vecPos, vecDir, vecRight, vecUp);
		break;
	case 3:
		// Half-Life 2: Episode 2
		UpdateLightNew(vecPos, vecDir, vecRight, vecUp);
		break;
	case 4:
		//Left 4 Dead (2)
		UpdateLightNew(vecPos, vecDir, vecRight, vecUp);
		break;
	default:
		// Default directly to Half-Life 2 if invalid
		UpdateLightNew(vecPos, vecDir, vecRight, vecUp);
		break;
	}
}


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CFlashlightEffect::LightOffNew()
{
#ifndef NO_TOOLFRAMEWORK
	if ( clienttools->IsInRecordingMode() )
	{
		KeyValues *msg = new KeyValues( "FlashlightState" );
		msg->SetFloat( "time", gpGlobals->curtime );
		msg->SetInt( "entindex", m_nEntIndex );
		msg->SetInt( "flashlightHandle", m_FlashlightHandle );
		msg->SetPtr( "flashlightState", NULL );
		ToolFramework_PostToolMessage( HTOOLHANDLE_INVALID, msg );
		msg->deleteThis();
	}
#endif

	// Clear out the light
	if( m_FlashlightHandle != CLIENTSHADOW_INVALID_HANDLE )
	{
		g_pClientShadowMgr->DestroyFlashlight( m_FlashlightHandle );
		m_FlashlightHandle = CLIENTSHADOW_INVALID_HANDLE;
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CFlashlightEffect::LightOffOld()
{	
	if ( m_pPointLight && ( m_pPointLight->key == m_nEntIndex ) )
	{
		m_pPointLight->die = gpGlobals->curtime;
		m_pPointLight = NULL;
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CFlashlightEffect::LightOff()
{	
	LightOffOld();
	LightOffNew();
}

CHeadlightEffect::CHeadlightEffect() 
{

}

CHeadlightEffect::~CHeadlightEffect()
{
	
}

void CHeadlightEffect::UpdateLight( const Vector &vecPos, const Vector &vecDir, const Vector &vecRight, const Vector &vecUp, int nDistance )
{
	if ( IsOn() == false )
		 return;

	FlashlightState_t state;
	Vector basisX, basisY, basisZ;
	basisX = vecDir;
	basisY = vecRight;
	basisZ = vecUp;
	VectorNormalize(basisX);
	VectorNormalize(basisY);
	VectorNormalize(basisZ);

	BasisToQuaternion( basisX, basisY, basisZ, state.m_quatOrientation );
		
	state.m_vecLightOrigin = vecPos;

	state.m_fHorizontalFOVDegrees = 45.0f;
	state.m_fVerticalFOVDegrees = 30.0f;
	state.m_fQuadraticAtten = r_flashlightquadratic.GetFloat();
	state.m_fLinearAtten = r_flashlightlinear.GetFloat();
	state.m_fConstantAtten = r_flashlightconstant.GetFloat();
	state.m_Color[0] = 1.0f;
	state.m_Color[1] = 1.0f;
	state.m_Color[2] = 1.0f;
	state.m_Color[3] = r_flashlightambient.GetFloat();
	state.m_NearZ = r_flashlightnear.GetFloat();
	state.m_FarZ = r_flashlightfar.GetFloat();
	state.m_bEnableShadows = true;
	state.m_pSpotlightTexture = m_FlashlightTexture;
	state.m_nSpotlightTextureFrame = 0;
	
	if( GetFlashlightHandle() == CLIENTSHADOW_INVALID_HANDLE )
	{
		SetFlashlightHandle( g_pClientShadowMgr->CreateFlashlight( state ) );
	}
	else
	{
		g_pClientShadowMgr->UpdateFlashlightState( GetFlashlightHandle(), state );
	}
	
	g_pClientShadowMgr->UpdateProjectedTexture( GetFlashlightHandle(), true );
}

