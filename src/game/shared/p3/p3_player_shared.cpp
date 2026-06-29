//========= Copyright © 1996-2005, Valve Corporation, All rights reserved. ============//
//
// Purpose: Implements shared baseplayer class functionality
//
// $NoKeywords: $
//=============================================================================//

#include "cbase.h"
#include "movevars_shared.h"
#include "util_shared.h"
#include "datacache/imdlcache.h"
#include "p3_shareddefs.h"
#include "in_buttons.h"
#include "p3/weapons/p3_weapon_shared.h"
#include "debugoverlay_shared.h"
 
#if defined( CLIENT_DLL )

	#include "iclientvehicle.h"
	#include "prediction.h"
	#include "c_basedoor.h"
	#include "c_world.h"
	#include "view.h"

	#define CRecipientFilter C_RecipientFilter

	#include "ivieweffects.h"
	#include "p3/P3_c_player.h"
	#include "p3/Simulators/P3_c_Simulator.h"
	#include "p3/weapons/p3_c_base_weapon.h"
	
	#define CP3_Player C_P3_Player
	#define CP3_Simulator C_P3_Simulator
	#define CBaseCombatWeapon C_BaseCombatWeapon
	#define CP3_BaseWeapon C_P3_BaseWeapon

#else
	#include "ai_basenpc.h"
	#include "doors.h"
	#include "env_zoom.h"
	#include "iservervehicle.h"
	#include "physics_prop_ragdoll.h"
	#include "trains.h"
	#include "world.h"

	extern int TrainSpeed(int iSpeed, int iMax);
	#include "p3/P3_player.h"
	#include "p3/Simulators/P3_Simulator.h"
	#include "p3/weapons/p3_base_weapon.h"

#endif

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

ConVar p3_player_aim_maxdistance( "p3_player_aim_maxdistance", "3000", FCVAR_REPLICATED );

ConVar cv_topcolor( "topcolor", "0", FCVAR_REPLICATED );
ConVar cv_bottomcolor( "bottomcolor", "0", FCVAR_REPLICATED );
ConVar cl_himodels( "cl_himodels", "0", FCVAR_REPLICATED );
ConVar cl_crosshairusealpha( "cl_crosshairusealpha", "0", FCVAR_REPLICATED );
ConVar cl_crosshaircolor( "cl_crosshaircolor", "0", FCVAR_REPLICATED );
ConVar cl_crosshairscale( "cl_crosshairscale", "0", FCVAR_REPLICATED );
ConVar cl_crosshair_red( "cl_crosshair_red", "0", FCVAR_REPLICATED );
ConVar cl_crosshair_green( "cl_crosshair_green", "0", FCVAR_REPLICATED );
ConVar cl_crosshair_blue( "cl_crosshair_blue", "0", FCVAR_REPLICATED );
ConVar cl_crosshair_scale( "cl_crosshair_scale", "0", FCVAR_REPLICATED );
ConVar cl_crosshair_file( "cl_crosshair_file", "0", FCVAR_REPLICATED );


CP3_BaseWeapon *
CP3_Player::GetP3ActiveWeapon() const
{
	return static_cast< CP3_BaseWeapon * >( GetActiveWeapon() );
}

//---------------------------------------------------------
//---------------------------------------------------------
Vector CP3_Player::EyeDirection3D( void )
{
	Vector vecForward;

	// Return the vehicle angles if we request them
	if ( GetSimulator() != NULL )
	{
		CacheVehicleView();
		EyeVectors( &vecForward );
		return vecForward;
	}
	
	AngleVectors( EyeAngles(), &vecForward );
	return vecForward;
}

Vector
CP3_Player::GetCameraAttachementPosition() const
{
	if ( m_nBodyState == BS_NORMAL || m_nBodyState == BS_CORPSE )
	{
		return GetAbsOrigin();
	}

	if ( !m_hRagdoll )
	{
		return GetAbsOrigin();
	}

	return m_hRagdoll->GetAbsOrigin();
}

void CP3_Player::UpdateAimTraces()
{
	m_vecAimMatrixCenter = GetCameraOrigin();

	UTIL_TraceLine(GetCameraOrigin()
		, GetCameraOrigin() + GetCameraDir() * p3_player_aim_maxdistance.GetFloat()
		, MASK_SHOT, this, COLLISION_GROUP_NONE, &m_traceCameraAim );

	m_bUseCameraAimTrace = true;

	CP3_BaseWeapon* w = GetP3ActiveWeapon();
	//if ( w && !w->IsWeaponBlocked() )
	if ( w )
	{
		Vector vecWeaponAim;
		QAngle angWeaponAim;

		m_vecAimMatrixCenter = EyePosition();

		int i = w->LookupAttachment( "attach_shot_direction" );
		w->InvalidateBoneCache();
		if ( i < 0 || !w->GetAttachment( i, vecWeaponAim, angWeaponAim ) )
		{
			vecWeaponAim = GetAimMatrixCenter();
			angWeaponAim = EyeAngles();
		}

#ifdef NEW_BLOCKED_TRACE
		// трейсим луч из оружия в цель
		UTIL_TraceLine( vecWeaponAim, m_traceCameraAim.endpos
			, MASK_SHOT, &traceFilter, &m_traceWeaponAim );
#else
		// трейсим луч из оружия в цель		
		Vector endPos = m_traceCameraAim.endpos - vecWeaponAim;
		endPos.NormalizeInPlace();
		endPos = vecWeaponAim + p3_player_aim_maxdistance.GetFloat() * endPos;
		UTIL_TraceLine( vecWeaponAim, endPos
			, MASK_SHOT, this, COLLISION_GROUP_NONE, &m_traceWeaponAim );		
#endif

		if (!w->IsWeaponBlocked() )
		{
			CBaseEntity* pEntity = m_traceWeaponAim.m_pEnt;
			if ( m_traceWeaponAim.DidHit() && pEntity && !pEntity->IsWorld() )
			{
				// если на пути попался нпц или проп, то стреляем из оружия
				if ( pEntity->IsNPC() || pEntity->GetMoveType() == MOVETYPE_VPHYSICS )
				{
					m_bUseCameraAimTrace = false;
				}
			}
		}
	}	
}

//-----------------------------------------------------------------------------
// Purpose: Update the simulator view, or simply return the cached position and angles
//-----------------------------------------------------------------------------
void CP3_Player::CacheSimulatorView( void )
{
	// If we've calculated the view this frame, then there's no need to recalculate it
	//if ( m_nSimulatorViewSavedFrame == gpGlobals->framecount )
	//	return;

#ifdef CLIENT_DLL
	C_P3_Simulator *pSimulator = GetSimulator();
#else
	CP3_Simulator *pSimulator = GetSimulator();
#endif

	if ( pSimulator != NULL )
	{		
		int nRole = pSimulator->GetPassengerRole( this );

		// Get our view for this frame
		pSimulator->GetVehicleViewPosition( nRole, &m_vecSimulatorViewOrigin, &m_vecSimulatorViewAngles, &m_flSimulatorViewFOV );
		m_nSimulatorViewSavedFrame = gpGlobals->framecount;
	}
}

//-----------------------------------------------------------------------------
// Purpose: The main view setup function for simulators
//-----------------------------------------------------------------------------
void CP3_Player::CalcSimulatorView( 
#if defined( CLIENT_DLL )
	C_P3_Simulator *pVehicle, 
#else
	CP3_Simulator *pVehicle,
#endif
	Vector& eyeOrigin, QAngle& eyeAngles,
	float& zNear, float& zFar, float& fov )
{
	Assert( pVehicle );

	// Start with our base origin and angles
	//CacheSimulatorView();
	eyeOrigin = m_vecSimulatorViewOrigin;
	eyeAngles = m_vecSimulatorViewAngles;

#if defined( CLIENT_DLL )

	fov = GetFOV();

	// Allows the vehicle to change the clip planes
	pVehicle->GetVehicleClipPlanes( zNear, zFar );
#endif

	// Snack off the origin before bob + water offset are applied
	Vector vecBaseEyePosition = eyeOrigin;

	CalcViewRoll( eyeAngles );

	// Apply punch angle
	VectorAdd( eyeAngles, m_Local.m_vecPunchAngle, eyeAngles );

#if defined( CLIENT_DLL )
	if ( !prediction->InPrediction() )
	{
		// Shake it up baby!
		vieweffects->CalcShake();
		vieweffects->ApplyShake( eyeOrigin, eyeAngles, 1.0 );
	}
#endif

}

bool CP3_Player::IsSimulatorOverridesView()
{	
	#if defined( CLIENT_DLL )
		C_P3_Simulator *pSim;
	#else
		CP3_Simulator *pSim;
	#endif
	pSim = GetSimulator();
	if (pSim && pSim->IsOverridesView())
		return true;
	return false;	
}

Vector CP3_Player::EyePosition()
{
	if ( IsSimulatorOverridesView() )
	{
		/*Vector pos;
		EntityToWorldSpace( Vector(-30,0,60), &pos );		
		return pos; */
		// Return the cached result
		//CacheSimulatorView();
		return m_vecSimulatorViewOrigin;
	}

#ifndef CLIENT_DLL
	if ( IsCovering() )
	{
		Vector eyePos = GetAbsOrigin();
		Vector coverRightOffset = GetCoverRight() * 20;

		int state = GetPlayerState();
		if ( P3_CHECK_FLAGS( state, P3_PLAYERSTATE_DUCK ) )
		{
			eyePos.z += 40;
		}
		else
		{
			eyePos.z += 60;
		}

		if ( P3_CHECK_FLAGS( state, P3_PLAYERSTATE_CORNERLEFT | P3_PLAYERSTATE_LOOKLEFT ) ||
		     P3_CHECK_FLAGS( state, P3_PLAYERSTATE_CORNERLEFT | P3_PLAYERSTATE_AIMING ) )
		{
			eyePos -= coverRightOffset;
		}
		else if ( P3_CHECK_FLAGS( state, P3_PLAYERSTATE_CORNERRIGHT | P3_PLAYERSTATE_LOOKRIGHT ) ||
		          P3_CHECK_FLAGS( state, P3_PLAYERSTATE_CORNERRIGHT | P3_PLAYERSTATE_AIMING ) )
		{
			eyePos += coverRightOffset;
		}
		
		return eyePos;
	}
	else
#endif
	{
		return BaseClass::EyePosition();
	}
}

//-----------------------------------------------------------------------------
// Returns eye vectors
//-----------------------------------------------------------------------------
void CP3_Player::EyeVectors( Vector *pForward, Vector *pRight, Vector *pUp )
{
	if ( IsSimulatorOverridesView() )
	{
		// Cache or retrieve our calculated position in the vehicle
		//CacheSimulatorView();
		AngleVectors( m_vecSimulatorViewAngles, pForward, pRight, pUp );
	}
	else
	{
		return BaseClass::EyeVectors( pForward, pRight, pUp );
		//AngleVectors( EyeAngles(), pForward, pRight, pUp );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Returns the eye position and angle vectors.
//-----------------------------------------------------------------------------
void CP3_Player::EyePositionAndVectors( Vector *pPosition, Vector *pForward,
										 Vector *pRight, Vector *pUp )
{
	// Handle the view in the vehicle
	if ( IsSimulatorOverridesView() )
	{
		//CacheSimulatorView();
		AngleVectors( m_vecSimulatorViewAngles, pForward, pRight, pUp );
		
		if ( pPosition != NULL )
		{
			*pPosition = m_vecSimulatorViewOrigin;
		}
	}
	else
	{
		return BaseClass::EyePositionAndVectors( pPosition, pForward, pRight, pUp );
		/*VectorCopy( BaseClass::EyePosition(), *pPosition );
		AngleVectors( EyeAngles(), pForward, pRight, pUp );*/
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : eyeOrigin - 
//			eyeAngles - 
//			zNear - 
//			zFar - 
//			fov - 
//-----------------------------------------------------------------------------
void CP3_Player::CalcView( Vector &eyeOrigin, QAngle &eyeAngles, float &zNear, float &zFar, float &fov )
{
#if defined( CLIENT_DLL )
	C_P3_Simulator *pVehicle; 
#else
	CP3_Simulator *pVehicle;
#endif
	pVehicle = GetSimulator();

	if ( !pVehicle || !IsSimulatorOverridesView() )
	{
		return BaseClass::CalcView( eyeOrigin, eyeAngles, zNear, zFar, fov );
		/*if ( IsObserver() )
		{
			CalcObserverView( eyeOrigin, eyeAngles, fov );
		}
		else
		{
			CalcPlayerView( eyeOrigin, eyeAngles, fov );
		}*/
	}
	else
	{
		CalcSimulatorView( pVehicle, eyeOrigin, eyeAngles, zNear, zFar, fov );
	}
}

#ifndef CLIENT_DLL
static ConVar p3_player_aimdebug( "p3_player_aimdebug", "0", FCVAR_CHEAT );
#endif

void CP3_Player::ItemPostFrame( void )
{
#ifndef CLIENT_DLL
	if(!(m_nButtons & IN_LASERPEN))//make the player press the button again to switch back
	{
		m_LaserpenModeWaitForRelease = false;
	}

	if(m_LaserpenModeWaitForRelease)
	{
		m_nButtons &= ~IN_LASERPEN;
	}


	if(m_nButtons & IN_LASERPEN)//switch between laserpen and active weapon
	{
		CBaseCombatWeapon* laserpen = Weapon_GetThisType("p3_weapon_laserpen");

		if(m_LaserpenModeButtonDownTime == 0.0)
			m_LaserpenModeButtonDownTime = gpGlobals->curtime;
		else
		{
			if( laserpen && gpGlobals->curtime - m_LaserpenModeButtonDownTime > 1.2f )
			{
				CBaseCombatWeapon* active = GetActiveWeapon();
				if(active != laserpen)
					Weapon_Switch(laserpen);
				else if(Weapon_GetLast())
					Weapon_Switch(Weapon_GetLast());

				m_LaserpenModeButtonDownTime = 0.0f;
				m_LaserpenModeWaitForRelease = true;
				m_nButtons &= ~IN_LASERPEN;
			}
		}
	}
	else
	{
		m_LaserpenModeButtonDownTime = 0.0;
	}
#endif

	BaseClass::ItemPostFrame();

	#if defined( CLIENT_DLL )
	C_P3_Simulator *pSimulator = GetSimulator();
	#else
	CP3_Simulator *pSimulator = GetSimulator();
	#endif
	
	if ( pSimulator )
	{
		CacheSimulatorView();
		//bool bUsingStandardWeapons = UsingStandardWeaponsInVehicle();

#if defined( CLIENT_DLL )
		if ( pSimulator->IsPredicted() )
#endif
		{
			pSimulator->ItemPostFrame( this );
		}

		/*if (!bUsingStandardWeapons || !pSimulator)
			return;*/
	}

#ifndef CLIENT_DLL
	if ( IsAiming() && p3_player_aimdebug.GetBool() )
	{
		Vector start = GetWeaponAimTrace().startpos;
		Vector end = GetWeaponAimTrace().endpos;
				
		NDebugOverlay::Line( start, end, 255,0,0, true, 0 );
		NDebugOverlay::Sphere( start, vec3_angle, 0.5f, 0,0,255,150, true, 0 );
		NDebugOverlay::Sphere( end, vec3_angle, 0.5f, 255,0,0,150, true, 0 );

		if ( end != GetCameraAimTrace().endpos )
		{
			NDebugOverlay::Line( end, GetCameraAimTrace().endpos, 127,127,0, true, 0 );
			NDebugOverlay::Sphere( GetCameraAimTrace().endpos, vec3_angle, 0.5f, 127,127,0,150, true, 0 );
		}

		NDebugOverlay::Sphere( GetAimEndPoint(), vec3_angle, 0.75f, 255,255,255,50, false, 0 );

		NDebugOverlay::Line( GetAimMatrixCenter(), end, 127,127,0, true, 0 );
		NDebugOverlay::Sphere( GetAimMatrixCenter(), vec3_angle, 0.85f, 0,255,0,50, true, 0 );

		NDebugOverlay::Text( end, m_bUseCameraAimTrace ? "[Camera]" : "[Weapon]", true, 0 );
	}
#endif
}

bool CP3_Player::CanUseWeapon( CBaseCombatWeapon *weapon )
{
	//C_P3_Player *pP3Player = assert_cast<C_P3_Player*>(pPlayer);
	//if (pP3Player)

	CBaseCombatWeapon *pMetaWeapon = GetMetaWeapon();
	
	if ( pMetaWeapon && pMetaWeapon!=weapon && weapon->GetOwnerEntity() == this )
		return false;
	
	
	CP3_Simulator *sim = GetSimulator();
	if (sim)
	{
		CP3_BaseWeapon *p3_weapon = assert_cast<CP3_BaseWeapon*>(weapon);
		if (weapon && !(sim->GetWeaponCompatibilityMask() &  p3_weapon->GetCompatibilityType()) )
		{
			return false;
		}
	}
	else if (HasHostage())
	{
		if(!Q_strcmp(weapon->GetClassname(),"p3_weapon_spray"))
			return true;

		int hostageMask = WEAPON_DEAGLE;
		CP3_BaseWeapon *p3_weapon = assert_cast<CP3_BaseWeapon*>(weapon);
		if (weapon && !(hostageMask &  p3_weapon->GetCompatibilityType()) )
		{
			return false;
		}
	}
	else if (IsSnatched())
	{
		int snatchedMask = WEAPON_EMPTYHANDS;
		CP3_BaseWeapon *p3_weapon = assert_cast<CP3_BaseWeapon*>(weapon);
		if (weapon && !(snatchedMask &  p3_weapon->GetCompatibilityType()) )
		{
			return false;
		}
	}
	return true;
}

CP3_Player* P3_GetPlayer()
{
#ifdef CLIENT_DLL
	return static_cast< CP3_Player* >( C_BasePlayer::GetLocalPlayer() );
#else
#ifdef P3MP_DLL
	if ( !UTIL_IsMultiplayer() )
#endif
		return static_cast< CP3_Player* >( UTIL_GetLocalPlayer() );
#ifdef P3MP_DLL
	else if ( CP3_Player *pPlayer = static_cast< CP3_Player* >( UTIL_GetCommandClient() ) )
		return pPlayer;
	else if ( !engine->IsDedicatedServer() )
		return static_cast< CP3_Player* >( UTIL_GetListenServerHost() );
#endif // P3MP_DLL
	return NULL;
#endif
}

CP3_Player* ToP3Player( CBaseEntity* ent )
{
	return ( ent && ent->IsPlayer() ? assert_cast< CP3_Player* >( ent ) : NULL );
}

