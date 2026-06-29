//========= Copyright © 1996-2005, Valve Corporation, All rights reserved. ============//
//
// Purpose: Rockets (Weapon)
//
//=============================================================================//

#include "cbase.h"
#include "weapon_grenade_rocket.h"

#if defined( CLIENT_DLL )
// Client Only
#include "hud.h"
#include "particles_simple.h"
#else
// Server Only
#include "gameinterface.h"
#include "engine/IEngineSound.h"
#include "explode.h"
#include "iservervehicle.h"
#endif

#include "p3/effects/p3_particles_shared.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

#define ROCKET_TRAIL_FX	"M136_Rocket_Fx"
#define ROCKET_MODEL "models/weapons/m136/m136_tank.mdl"
#define TANK_ROCKET_EXPLOSION_FX "Big_Gun_Hit_Fx"

LINK_ENTITY_TO_CLASS( weapon_grenade_rocket, CWeaponGrenadeRocket );
BEGIN_PREDICTION_DATA( CWeaponGrenadeRocket )
END_PREDICTION_DATA()

BEGIN_DATADESC( CWeaponGrenadeRocket )	
#if !defined( CLIENT_DLL )	
	DEFINE_ENTITYFUNC( RocketTouch ),

	DEFINE_FIELD( m_flDamage, FIELD_FLOAT ),
	DEFINE_FIELD( m_flRadius, FIELD_FLOAT ),
	DEFINE_FIELD( m_flMaxRange, FIELD_FLOAT ),
	DEFINE_FIELD( m_flFallingSpeed, FIELD_FLOAT ),
	DEFINE_FIELD( m_flExceedRangeTime, FIELD_FLOAT ),
	DEFINE_FIELD( m_hOwner, FIELD_EHANDLE ),
	DEFINE_FIELD( m_bTankMode, FIELD_BOOLEAN ),
#endif
END_DATADESC()

IMPLEMENT_NETWORKCLASS_ALIASED( WeaponGrenadeRocket, DT_WeaponGrenadeRocket )
BEGIN_NETWORK_TABLE( CWeaponGrenadeRocket, DT_WeaponGrenadeRocket )
//#if !defined( CLIENT_DLL )
//#else
//#endif
END_NETWORK_TABLE()

ConVar p3_tank_shell_velocity				( "p3_tank_shell_velocity", "1500", FCVAR_CHEAT|FCVAR_REPLICATED);

#if !defined( CLIENT_DLL )
// Server Only
ConVar weapon_grenade_rocket_force( "weapon_grenade_rocket_force","150.0", FCVAR_NONE, "Rocket force modifier." ); 
#endif

//-----------------------------------------------------------------------------
// Purpose: Constructor
//-----------------------------------------------------------------------------
CWeaponGrenadeRocket::CWeaponGrenadeRocket()
{
	m_flDamage = 100.0f;

#if !defined( CLIENT_DLL )
	// Server Only
	UseClientSideAnimation();
#endif

	m_bTankMode = false;
}

//-----------------------------------------------------------------------------
// Purpose: Deconstructor
//-----------------------------------------------------------------------------
CWeaponGrenadeRocket::~CWeaponGrenadeRocket()
{
#if defined( CLIENT_DLL )
	StopSound( entindex(), "GrenadeRocket.FlyLoop" );
	P3_StopClientEffect(this, ROCKET_TRAIL_FX);
#endif
}

//-----------------------------------------------------------------------------
// Purpose: Create a weapon grenade rocket
//-----------------------------------------------------------------------------
#if !defined( CLIENT_DLL )
CWeaponGrenadeRocket *CWeaponGrenadeRocket::Create( const Vector &vecOrigin, const Vector &vecForward, float flMaxRange, CBaseEntity *pOwner )
{
	CWeaponGrenadeRocket *pRocket = ( CWeaponGrenadeRocket* )CreateEntityByName( "weapon_grenade_rocket" );

	UTIL_SetOrigin( pRocket, vecOrigin );
	QAngle angles;
	VectorAngles( vecForward, angles );
	pRocket->SetLocalAngles( angles );
	pRocket->Spawn();
	pRocket->SetOwnerEntity( pOwner );
	//pRocket->ChangeTeam( pOwner->GetTeamNumber() );
	pRocket->SetMaxRange( flMaxRange );

	return pRocket;
}
#endif
//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CWeaponGrenadeRocket::SetMaxRange( float flRange )
{
	m_flMaxRange = flRange;

#if !defined( CLIENT_DLL )
	if ( m_flMaxRange )
	{
		float flSpeed = GetLocalVelocity().Length();
		Assert( flSpeed );
		m_flExceedRangeTime = gpGlobals->curtime + (m_flMaxRange / flSpeed);		
	}
#endif
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CWeaponGrenadeRocket::Spawn( void )
{
#if !defined( CLIENT_DLL )
	Precache();

	m_flRadius = 100;
	SetMoveType( MOVETYPE_FLY );
	SetSolid( SOLID_BBOX );
	SetModel( ROCKET_MODEL );
	//UTIL_SetSize( this, vec3_origin, vec3_origin );
	UTIL_SetSize( this, -Vector(4,4,4), Vector(4,4,4) );

	//SetCollisionGroup( COLLISION_GROUP_WEAPON );

	// Forward!
	Vector forward;
	AngleVectors( GetLocalAngles(), &forward, NULL, NULL );
	SetAbsVelocity( forward * p3_tank_shell_velocity.GetFloat() );
	
	SetTouch( &CWeaponGrenadeRocket::RocketTouch );
#else
	// Start our flying sound loop
	CPASAttenuationFilter filter( this );
	filter.MakeReliable();
	EmitSound( filter, entindex(), "GrenadeRocket.FlyLoop" );
#endif
}

#if !defined( CLIENT_DLL )
// Server Only
//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CWeaponGrenadeRocket::Precache( void )
{
	PrecacheModel( ROCKET_MODEL );
	PrecacheParticleSystem( ROCKET_TRAIL_FX );
	PrecacheParticleSystem( TANK_ROCKET_EXPLOSION_FX );

	PrecacheScriptSound( "GrenadeRocket.FlyLoop" );
}
//-----------------------------------------------------------------------------
// Purpose: Set angles to match our velocity
//-----------------------------------------------------------------------------
void CWeaponGrenadeRocket::SetAnglesToMatchVelocity( void )
{
	QAngle angles;
	VectorAngles( GetAbsVelocity(), angles );
	SetLocalAngles( angles );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CWeaponGrenadeRocket::RocketTouch( CBaseEntity *pOther )
{
	Assert( pOther );
	if ( !pOther->IsSolid() )
		return;

	//CPropTank* 

	// Apply forces to vehicles.
	if ( pOther->GetServerVehicle() )
	{
		ApplyForcesToVehicle( pOther );
	}

	if ( m_bTankMode )
	{
		ExplosionCreateNoParticles( GetAbsOrigin(), GetAbsAngles(), this, GetDamage(), GetDamageRadius(), false, 2000 );
		P3_CreateServerEffect( NULL, TANK_ROCKET_EXPLOSION_FX, GetAbsOrigin(), GetAbsAngles(), 10 );
	}
	else
	{
		CPASFilter filter( GetAbsOrigin() );
		te->Explosion( filter, 0.0,	&GetAbsOrigin(), g_sModelIndexFireball, 2.0, 15, TE_EXPLFLAG_NONE, 100, m_flDamage );
	}

	// Use the owner's position as the reported position
	Vector vecReported = vec3_origin;
	CBaseEntity* owner = GetOwnerEntity();
	CBaseEntity* driver = 0;
	if(owner)
	{
		vecReported = owner->GetAbsOrigin();
		IServerVehicle* veh = owner->GetServerVehicle();

		if(veh)
			driver = veh->GetPassenger(VEHICLE_ROLE_DRIVER);
	}

	RadiusDamage( CTakeDamageInfo( this, driver, vec3_origin, GetAbsOrigin(), GetDamage(), GetDamageType(), 0, &vecReported ), GetAbsOrigin(), GetDamageRadius(), CLASS_NONE, NULL );

	UTIL_Remove( this );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CWeaponGrenadeRocket::ApplyForcesToVehicle( CBaseEntity *pEntity )
{
	// Check team - don't apply forces to our own team's vehicles.
	if ( pEntity->GetTeam() == GetTeam() )
		return;

	IServerVehicle *pVehicle = pEntity->GetServerVehicle();
	if ( !pVehicle )
		return;

	IPhysicsObject *pPhysObject = pEntity->VPhysicsGetObject();
	if ( pPhysObject )
	{
		//------------------------------------------------------------
		// Rocket the vehicle in the direction of the incoming rocket.
		//------------------------------------------------------------	
		Vector vecForceDir = GetAbsVelocity();
		vecForceDir.z = 0.0f;
		VectorNormalize( vecForceDir );

		float flForce = pPhysObject->GetMass();
		flForce += ( 4.0f * 100.0f );				// Wheels
		flForce *= weapon_grenade_rocket_force.GetFloat();

		vecForceDir *= flForce;

		pPhysObject->ApplyForceOffset( vecForceDir, GetAbsOrigin() );
	}
}

#else
// Client Only

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CWeaponGrenadeRocket::OnDataChanged( DataUpdateType_t updateType )
{
	BaseClass::OnDataChanged( updateType );

	P3_StartClientEffect(this, ROCKET_TRAIL_FX, "att_origin");
	// Only think when "rocketing."
	//SetNextClientThink( CLIENT_THINK_ALWAYS );
}
#endif

