//========= Copyright © 1996-2005, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

#include "cbase.h"
#include "beam_shared.h"
#ifndef CLIENT_DLL
#include "player.h"
#endif
#include "gamerules.h"
#ifdef CLIENT_DLL
#include "ClientEffectPrecacheSystem.h"
#include "clientmode_shared.h"
#include "c_baseplayer.h"
#endif
#include "weapon_hl2mpbasehlmpcombatweapon.h"
#ifndef CLIENT_DLL
#include "baseviewmodel.h"
#endif
#include "vphysics/constraints.h"
#include "physics.h"
#include "in_buttons.h"
#include "IEffects.h"
#include "soundenvelope.h"
#include "engine/IEngineSound.h"
#ifndef CLIENT_DLL
#include "ndebugoverlay.h"
#endif
#include "physics_saverestore.h"
#ifndef CLIENT_DLL
#include "player_pickup.h"
#endif
#include "soundemittersystem/isoundemittersystembase.h"
#ifdef CLIENT_DLL
#include "model_types.h"
#include "view_shared.h"
#include "view.h"
#include "sprite.h"
#include "beamdraw.h"
#include "iviewrender.h"
#include "ragdoll.h"
#else
#include "physics_prop_ragdoll.h"
#endif

//#define use_old_physgun_attach 1 // debugging: use the old physgun attach code instead of the new one

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

ConVar physcannon_rotate("physcannon_rotate", "1");

// GMod 10's hold controller settings (same names and defaults as its server.dll), read every
// physics tick. Replicated so the client's predicted hold matches the server's.
ConVar physgun_maxAngular( "physgun_maxAngular", "5000", FCVAR_REPLICATED );
ConVar physgun_maxAngularDamping( "physgun_maxAngularDamping", "10000", FCVAR_REPLICATED );
ConVar physgun_maxSpeed( "physgun_maxSpeed", "5000", FCVAR_REPLICATED );
ConVar physgun_maxSpeedDamping( "physgun_maxSpeedDamping", "10000", FCVAR_REPLICATED );
ConVar physgun_DampingFactor( "physgun_DampingFactor", "0.8", FCVAR_REPLICATED );
ConVar physgun_teleportDistance( "physgun_teleportDistance", "0", FCVAR_REPLICATED );
ConVar physgun_timeToArrive( "physgun_timeToArrive", "0.05", FCVAR_REPLICATED );
ConVar physgun_timeToArriveRagdoll( "physgun_timeToArriveRagdoll", "0.1", FCVAR_REPLICATED );

// GMod 10 makes held props (not ragdolls or the jeep) this heavy, so nothing they hit slows them
#define PHYSGUN_HELD_MASS	1e11f

// GMod 10's snapping and key rotation (same names and defaults as its server.dll)
ConVar gm_snaptogrid( "gm_snaptogrid", "0", FCVAR_REPLICATED | FCVAR_ARCHIVE, "Held objects move in steps of gm_gridsize" );
ConVar gm_gridsize( "gm_gridsize", "32", FCVAR_REPLICATED | FCVAR_ARCHIVE, "Grid size for gm_snaptogrid" );
ConVar gm_snapdegrees( "gm_snapdegrees", "45", FCVAR_REPLICATED | FCVAR_ARCHIVE, "Holding Sprint while rotating (Use) snaps the held object's angles to this many degrees" );
ConVar phys_spinspeed( "phys_spinspeed", "200", FCVAR_REPLICATED | FCVAR_ARCHIVE, "Degrees per second Left/Right turn a held object while rotating (Use)" );

// GMod 10's SnapToGrid: round each axis to the nearest multiple of the grid size
static Vector PhysgunSnapToGrid( const Vector &v, float flGrid )
{
	if ( flGrid <= 0 )
		return v;
	return Vector( RoundFloatToInt( v.x / flGrid ) * flGrid,
				   RoundFloatToInt( v.y / flGrid ) * flGrid,
				   RoundFloatToInt( v.z / flGrid ) * flGrid );
}

// True for vehicles (their physics is a body plus wheels, so more than one object, but they
// aren't ragdolls)
static bool PhysgunIsVehicle( CBaseEntity *pEntity )
{
#ifdef CLIENT_DLL
	return pEntity->GetClientVehicle() != NULL || FClassnameIs( pEntity, "prop_vehicle_jeep" );
#else
	return pEntity->GetServerVehicle() != NULL || FClassnameIs( pEntity, "prop_vehicle_jeep" );
#endif
}

static int g_physgunBeam1;
static int g_physgunBeam;
static int g_physgunGlow;
#define PHYSGUN_BEAM_SPRITE				"sprites/physbeam.vmt"
#define PHYSGUN_BEAM_SPRITE_ACTIVE		"sprites/physbeama.vmt"
#define PHYSGUN_BEAM_GLOW				"sprites/physg_glow1.vmt"
#define PHYSGUN_CORE_GLOW				"sprites/physg_glow2.vmt"	// GMod 10's core light (C_PhysBeam binds physg_glow2 for it)

#define	PHYSGUN_SKIN	1

#define gmod_physgun_lock true // Lock the model as per Gmod
#define physgun_audio false // Audio from the beta

class CWeaponGravityGun;

#ifdef CLIENT_DLL
#define PHYSGUN_GLOW_SPRITE "sprites/glow04_noz"
#define PHYSGUN_ENDCAP_SPRITE "sprites/orangeflare1"
#define PHYSGUN_CENTER_GLOW "sprites/physg_glow1"
#define PHYSGUN_BLAST_SPRITE "sprites/orangecore2"

CLIENTEFFECT_REGISTER_BEGIN( PrecacheEffectGravityGun )
CLIENTEFFECT_MATERIAL( PHYSGUN_BEAM_SPRITE )
CLIENTEFFECT_MATERIAL( PHYSGUN_BEAM_SPRITE_ACTIVE )
CLIENTEFFECT_MATERIAL( PHYSGUN_BEAM_GLOW )
CLIENTEFFECT_MATERIAL( PHYSGUN_CORE_GLOW )
CLIENTEFFECT_MATERIAL(PHYSGUN_GLOW_SPRITE)
CLIENTEFFECT_MATERIAL(PHYSGUN_ENDCAP_SPRITE)
CLIENTEFFECT_MATERIAL(PHYSGUN_CENTER_GLOW)
CLIENTEFFECT_MATERIAL(PHYSGUN_BLAST_SPRITE)
CLIENTEFFECT_REGISTER_END()


#endif

//IPhysicsObject *GetPhysObjFromPhysicsBone( CBaseEntity *pEntity, short physicsbone )
IPhysicsObject *GetPhysObjFromPhysicsBone( CBaseEntity *pEntity, short physicsbone )
{
	if ( !pEntity )
		return NULL;

	if( pEntity->IsNPC() )
	{
		return pEntity->VPhysicsGetObject();
	}

	// Only models can be ragdolls. This was a static_cast, which on a brush entity (the glass
	// in gm_greenhouse is a func_lod) read m_pRagdoll from memory past the end of the object
	// and crashed; brush entities just use their one physics object.
	CBaseAnimating *pModel = pEntity->GetBaseAnimating();
	if (pModel != NULL)
	{

		trace_t tr;
		//enginetrace->ClipRayToEntity(m_rayShot, MASK_SHOT, pModel, &tr);

		IPhysicsObject	*pPhysicsObject = NULL;
		
		//Find the real object we hit.
		if (physicsbone >= 0)
		{
#ifdef CLIENT_DLL
			if ( pModel->m_pRagdoll )
			{
				CRagdoll *pCRagdoll = dynamic_cast < CRagdoll * > ( pModel->m_pRagdoll );
#else
				// Affect the object
				CRagdollProp *pCRagdoll = dynamic_cast<CRagdollProp*>( pEntity );
#endif
				if ( pCRagdoll )
				{
					ragdoll_t *pRagdollT = pCRagdoll->GetRagdoll();

					if ( physicsbone < pRagdollT->listCount )
					{
						pPhysicsObject = pRagdollT->list[physicsbone].pObject;
					}
					return pPhysicsObject;
				}
#ifdef CLIENT_DLL
			}
#endif
		}
	}

	return pEntity->VPhysicsGetObject();
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
// derive from this so we can add save/load data to it
struct physgun_shadowcontrol_params_t : public hlshadowcontrol_params_t
{
	DECLARE_SIMPLE_DATADESC();
};

BEGIN_SIMPLE_DATADESC(physgun_shadowcontrol_params_t)

DEFINE_FIELD(targetPosition, FIELD_POSITION_VECTOR),
DEFINE_FIELD(targetRotation, FIELD_VECTOR),
DEFINE_FIELD(maxAngular, FIELD_FLOAT),
DEFINE_FIELD(maxDampAngular, FIELD_FLOAT),
DEFINE_FIELD(maxSpeed, FIELD_FLOAT),
DEFINE_FIELD(maxDampSpeed, FIELD_FLOAT),
DEFINE_FIELD(dampFactor, FIELD_FLOAT),
DEFINE_FIELD(teleportDistance, FIELD_FLOAT),

END_DATADESC()

class CGravControllerPoint : public IMotionEvent
{
	DECLARE_SIMPLE_DATADESC();

public:
	CGravControllerPoint( void );
	~CGravControllerPoint( void );
	void AttachEntity( CBasePlayer *pPlayer, CBaseEntity *pEntity, IPhysicsObject *pPhys, short physicsbone, const Vector &position );
	void DetachEntity( void );

	bool UpdateObject( CBasePlayer *pPlayer, CBaseEntity *pEntity, float* x, float* y );

	void SetTargetPosition( const Vector &target, const QAngle &targetOrientation )
	{
		m_targetPosition = target;
		m_shadow.targetPosition = target;
		m_shadow.targetRotation = targetOrientation;

		// GMod 10: with gm_snaptogrid, the held object's origin moves in gm_gridsize steps
		// (only the physics target; m_targetPosition stays where the beam aims)
		if ( gm_snaptogrid.GetBool() )
			m_shadow.targetPosition = PhysgunSnapToGrid( target, gm_gridsize.GetFloat() );
		CBaseEntity *pEntity;

		// (GMod 10 keeps the time to arrive set on grab; it isn't reset on each update)

		CBaseEntity *pAttached = m_attachedEntity;
		if ( pAttached )
		{
			
			IPhysicsObject *pObj = GetPhysObjFromPhysicsBone( pAttached, m_attachedPhysicsBone );
			//IPhysicsObject *pObj = pEntity->VPhysicsGetObject();
			
			if ( pObj != NULL )
			{
				pObj->Wake();
			}
			else
			{
				DetachEntity();
			}
		}
	}
	QAngle TransformAnglesToPlayerSpace( const QAngle &anglesIn, CBasePlayer *pPlayer );
	QAngle TransformAnglesFromPlayerSpace( const QAngle &anglesIn, CBasePlayer *pPlayer );

	IMotionEvent::simresult_e Simulate( IPhysicsMotionController *pController, IPhysicsObject *pObject, float deltaTime, Vector &linear, AngularImpulse &angular );
	Vector			m_localPosition;
	Vector			m_targetPosition;
	Vector			m_worldPosition;
	// old physgun stuff
	Vector			m_localAlignNormal;
	Vector			m_localAlignPosition;
	Vector			m_targetAlignNormal;
	Vector			m_targetAlignPosition;
	bool			m_align;
	// end old physgun stuff
	float			m_saveDamping;
	float			m_maxVel;
	float			m_saveMass;
	float			m_maxAcceleration;
	Vector			m_maxAngularAcceleration;
	EHANDLE			m_attachedEntity;
	short			m_attachedPhysicsBone;
	QAngle			m_targetRotation;
	bool			m_bJeep;			// GMod 10 eases the jeep's rotation in as it lines up (see Simulate)
	float			m_timeToArrive;
	float			m_timeToArrive_Shadow;

	float			m_flRotateX;
	float			m_flRotateY;

	float			x;
	float			y;

	physgun_shadowcontrol_params_t	m_shadow;


//#ifdef ARGG
	// adnan
	// set up the modified pickup angles... allow the player to rotate the object in their grip
	QAngle		m_vecRotatedCarryAngles;
	bool			m_bHasRotatedCarryAngles;
	// end adnan
//#endif

	IPhysicsMotionController *m_controller;

private:
	//hlshadowcontrol_params_t	m_shadow;
	bool   m_bFreezeView = false;
	QAngle m_angLockedView;
};


BEGIN_SIMPLE_DATADESC( CGravControllerPoint )

	DEFINE_FIELD( m_localPosition,			FIELD_VECTOR ),
	DEFINE_FIELD( m_targetPosition,			FIELD_POSITION_VECTOR ),
	DEFINE_FIELD( m_worldPosition,			FIELD_POSITION_VECTOR ),
	DEFINE_FIELD( m_localAlignNormal,		FIELD_VECTOR),
	DEFINE_FIELD( m_localAlignPosition,		FIELD_VECTOR),
	DEFINE_FIELD( m_targetAlignNormal,		FIELD_VECTOR),
	DEFINE_FIELD( m_targetAlignPosition,	FIELD_POSITION_VECTOR),
	DEFINE_FIELD( m_align,					FIELD_BOOLEAN),
	DEFINE_FIELD( m_saveDamping,			FIELD_FLOAT ),
	DEFINE_FIELD( m_saveMass,				FIELD_FLOAT ),
	DEFINE_FIELD( m_maxAcceleration,		FIELD_FLOAT ),
	DEFINE_FIELD( m_maxAngularAcceleration,	FIELD_VECTOR ),
	DEFINE_FIELD( m_attachedEntity,			FIELD_EHANDLE ),
	//DEFINE_FIELD( m_attachedPhysicsBone,	FIELD_SHORT ),
	DEFINE_FIELD( m_targetRotation,			FIELD_VECTOR ),
	DEFINE_FIELD( m_timeToArrive,			FIELD_FLOAT ),
//#ifdef ARGG
	// adnan
	// set up the fields for our added vars
	DEFINE_FIELD( m_vecRotatedCarryAngles, FIELD_VECTOR ),
	DEFINE_FIELD( m_bHasRotatedCarryAngles, FIELD_BOOLEAN ),
	// end adnan
//#endif

	// Physptrs can't be saved in embedded classes... this is to silence classcheck
	// DEFINE_PHYSPTR( m_controller ),

END_DATADESC()


CGravControllerPoint::CGravControllerPoint( void )
{
	m_shadow.dampFactor = 0.8;
	m_shadow.teleportDistance = 0;
	// make this controller really stiff!
	m_shadow.maxSpeed = 5000;
	m_shadow.maxAngular = m_shadow.maxSpeed;
	m_shadow.maxDampSpeed = m_shadow.maxSpeed*2;
	m_shadow.maxDampAngular = m_shadow.maxAngular*2;
	m_attachedEntity = NULL;
	m_attachedPhysicsBone = 0;
	m_controller = NULL;
	m_targetPosition = vec3_origin;
	m_worldPosition = vec3_origin;
	m_localPosition = vec3_origin;
	m_targetRotation = vec3_angle;
	m_timeToArrive = 0;
	m_bJeep = false;
	m_saveDamping = 0;
	m_saveMass = -1.0f;

//#ifdef ARGG
	// adnan
	// initialize our added vars
	m_vecRotatedCarryAngles = vec3_angle;
	m_bHasRotatedCarryAngles = false;
	// end adnan
//#endif
}

CGravControllerPoint::~CGravControllerPoint( void )
{
	DetachEntity();
}


QAngle CGravControllerPoint::TransformAnglesToPlayerSpace( const QAngle &anglesIn, CBasePlayer *pPlayer )
{
	matrix3x4_t test;
	QAngle angleTest = pPlayer->EyeAngles();
	angleTest.x = 0;
	AngleMatrix( angleTest, test );
	return TransformAnglesToLocalSpace( anglesIn, test );
}

QAngle CGravControllerPoint::TransformAnglesFromPlayerSpace( const QAngle &anglesIn, CBasePlayer *pPlayer )
{
	matrix3x4_t test;
	QAngle angleTest = pPlayer->EyeAngles();
	angleTest.x = 0;
	AngleMatrix( angleTest, test );
	return TransformAnglesToWorldSpace( anglesIn, test );
}
void CGravControllerPoint::AttachEntity( CBasePlayer *pPlayer, CBaseEntity *pEntity, IPhysicsObject *pPhys, short physicsbone, const Vector &vGrabPosition )
{
	m_attachedEntity = pEntity;
	m_attachedPhysicsBone = physicsbone;	// the ragdoll bone that was hit (was never stored, so bone 0 got used)
	pPhys->WorldToLocal( &m_localPosition, vGrabPosition );
	m_worldPosition = vGrabPosition;
	// GMod 10: leave damping alone (props keep spinning and swinging freely), make props
	// effectively immovable by collisions, and give ragdolls a slower time to arrive. Ragdolls
	// and the jeep keep their mass; m_saveMass < 0 means nothing to restore.
	IPhysicsObject *pList[VPHYSICS_MAX_OBJECT_LIST_COUNT];
	bool bRagdoll = pEntity->VPhysicsGetObjectList( pList, ARRAYSIZE( pList ) ) > 1 && !PhysgunIsVehicle( pEntity );
	m_bJeep = FClassnameIs( pEntity, "prop_vehicle_jeep" );

	// GMod 10 makes sure the object can move and turns on touch callbacks for it while held
	pPhys->EnableMotion( true );
	pPhys->SetCallbackFlags( pPhys->GetCallbackFlags() | CALLBACK_GLOBAL_TOUCH );
	m_timeToArrive = bRagdoll ? physgun_timeToArriveRagdoll.GetFloat() : physgun_timeToArrive.GetFloat();

	m_saveMass = -1.0f;
	if ( !bRagdoll && !FClassnameIs( pEntity, "prop_vehicle_jeep" ) )
	{
		m_saveMass = pPhys->GetMass();			// before changing it, so the drop restores the real mass
		if ( m_saveMass < PHYSGUN_HELD_MASS )
			pPhys->SetMass( PHYSGUN_HELD_MASS );
		else
			m_saveMass = -1.0f;
	}
	m_controller = physenv->CreateMotionController( this );
	m_controller->AttachObject( pPhys, true );
	m_controller->SetPriority(IPhysicsMotionController::HIGH_PRIORITY);

	// start from where the object actually is, facing the way it already faces
	// (angles were uninitialized here, which made the object snap on grab)
	Vector position;
	QAngle angles;
	pPhys->GetPosition( &position, &angles );
	SetTargetPosition( vGrabPosition, angles );
	m_targetRotation = TransformAnglesToPlayerSpace( angles, pPlayer );
//#ifdef ARGG
	// adnan
	// we need to grab the preferred/non preferred carry angles here for the rotatedcarryangles
	m_vecRotatedCarryAngles = m_targetRotation;
	// end adnan
//#endif
}

void CGravControllerPoint::DetachEntity( void )
{
	CBaseEntity *pEntity = m_attachedEntity;
	if ( pEntity )
	{
		IPhysicsObject *pPhys = GetPhysObjFromPhysicsBone( pEntity, m_attachedPhysicsBone );
		if ( pPhys )
		{
			// on the odd chance that it's gone to sleep while under anti-gravity
			pPhys->Wake();
			if ( m_saveMass >= 0 )
				pPhys->SetMass( m_saveMass );
		}
	}
	m_attachedEntity = NULL;
	m_attachedPhysicsBone = 0;
	if ( physenv )
	{
		physenv->DestroyMotionController( m_controller );
	}
	m_controller = NULL;

	// UNDONE: Does this help the networking?
	m_targetPosition = vec3_origin;
	m_worldPosition = vec3_origin;
}

void AxisAngleQAngle( const Vector &axis, float angle, QAngle &outAngles )
{
	// map back to HL rotation axes
	outAngles.z = axis.x * angle;
	outAngles.x = axis.y * angle;
	outAngles.y = axis.z * angle;
}

IMotionEvent::simresult_e CGravControllerPoint::Simulate( IPhysicsMotionController *pController, IPhysicsObject *pObject, float deltaTime, Vector &linear, AngularImpulse &angular )
{
	// GMod 10: the limits come from the convars every tick, and the time to arrive stays what it
	// was set to on grab (the return value isn't kept)
	hlshadowcontrol_params_t shadowParams = m_shadow;
	shadowParams.maxAngular = physgun_maxAngular.GetFloat();
	shadowParams.maxDampAngular = physgun_maxAngularDamping.GetFloat();
	shadowParams.maxSpeed = physgun_maxSpeed.GetFloat();
	shadowParams.maxDampSpeed = physgun_maxSpeedDamping.GetFloat();
	shadowParams.dampFactor = physgun_DampingFactor.GetFloat();
	shadowParams.teleportDistance = physgun_teleportDistance.GetFloat();

	// GMod 10 does this every tick: the held object stays unfrozen and awake
	pObject->EnableMotion( true );
	pObject->Wake();

	// GMod 10, the jeep only: scale the turning speed by how far it still has to turn
	// (1 - the dot product of its forward and the target's), so it eases into the target
	// orientation instead of overshooting and spinning
	if ( m_bJeep )
	{
		QAngle angCurrent;
		pObject->GetPosition( NULL, &angCurrent );
		Vector vecTargetForward, vecCurrentForward;
		AngleVectors( shadowParams.targetRotation, &vecTargetForward );
		AngleVectors( angCurrent, &vecCurrentForward );
		shadowParams.maxAngular *= ( 1.0f - DotProduct( vecTargetForward, vecCurrentForward ) );
	}

	pObject->ComputeShadowControl( shadowParams, m_timeToArrive, deltaTime );
	
	linear.Init();
	angular.Init();

	return SIM_LOCAL_ACCELERATION;
}


#ifdef CLIENT_DLL
#define CWeaponGravityGun C_WeaponGravityGun
#endif

class CWeaponGravityGun : public CBaseHL2MPCombatWeapon
{
	DECLARE_DATADESC();

public:
	DECLARE_CLASS( CWeaponGravityGun, CBaseHL2MPCombatWeapon );

	DECLARE_NETWORKCLASS();
	DECLARE_PREDICTABLE();

	CWeaponGravityGun();

#ifdef CLIENT_DLL
	void GetRenderBounds( Vector& mins, Vector& maxs )
	{
		BaseClass::GetRenderBounds( mins, maxs );

		// add to the bounds, don't clear them.
		// ClearBounds( mins, maxs );
		AddPointToBounds( vec3_origin, mins, maxs );
		AddPointToBounds( m_targetPosition, mins, maxs );
		AddPointToBounds( m_worldPosition, mins, maxs );
		CBaseEntity *pEntity = GetBeamEntity();
		if ( pEntity )
		{
			mins -= pEntity->GetRenderOrigin();
			maxs -= pEntity->GetRenderOrigin();
		}
	}

	void GetRenderBoundsWorldspace( Vector& mins, Vector& maxs )
	{
		BaseClass::GetRenderBoundsWorldspace( mins, maxs );

		// add to the bounds, don't clear them.
		// ClearBounds( mins, maxs );
		AddPointToBounds( vec3_origin, mins, maxs );
		AddPointToBounds( m_targetPosition, mins, maxs );
		AddPointToBounds( m_worldPosition, mins, maxs );
		mins -= GetRenderOrigin();
		maxs -= GetRenderOrigin();
	}

	int KeyInput( int down, ButtonCode_t keynum, const char *pszCurrentBinding )
	{
		if ( gHUD.m_iKeyBits & IN_ATTACK )
		{
			switch ( keynum )
			{
			case MOUSE_WHEEL_UP:
				m_bInWeapon1 = true;
				// gHUD.m_iKeyBits |= IN_WEAPON1;
				if ( gpGlobals->maxClients > 1 )
					gHUD.m_bSkipClear = true;
				return 0;

			case MOUSE_WHEEL_DOWN:
				m_bInWeapon2 = true;
				// gHUD.m_iKeyBits |= IN_WEAPON2;
				if ( gpGlobals->maxClients > 1 )
					gHUD.m_bSkipClear = true;
				return 0;
			}
		}

		// Allow engine to process
		return BaseClass::KeyInput( down, keynum, pszCurrentBinding );
	}

	void HandleInput()
	{
		if ( m_bInWeapon1 )
		{
			gHUD.m_iKeyBits |= IN_WEAPON1;
			m_bInWeapon1 = false;
		}

		if ( m_bInWeapon2 )
		{
			gHUD.m_iKeyBits |= IN_WEAPON2;
			m_bInWeapon2 = false;
		}
	}

	void DrawEffects();
	void DrawCoreGlow( C_BasePlayer *pOwner );
	int	 DrawModel( int flags );
	void ViewModelDrawn( C_BaseViewModel* pBaseViewModel );
	bool IsTransparent( void );

	// We need to render opaque and translucent pieces
	RenderGroup_t	GetRenderGroup( void ) {	return RENDER_GROUP_TWOPASS;	}
#endif

	void Spawn( void );
	void OnRestore( void );
	void Precache( void );

//#ifdef ARGG
	// adnan
	// for overriding the mouse -> view angles (but still calc view angles)
	bool OverrideViewAngles( void );
	// end adnan
//#endif

	virtual void	UpdateOnRemove(void);
	void PrimaryAttack( void );
	void SecondaryAttack( void );
	void WeaponIdle(void);
	void ItemPreFrame( void );
	void ItemPostFrame( void );
	void FreezePlayer( float* x, float* y, CUserCmd* pCmd, IMoveHelper* moveHelper);
	virtual bool Deploy(void);
	virtual bool Holster( CBaseCombatWeapon *pSwitchingTo )
	{
		EffectDestroy();
#if physgun_audio == true
		SoundDestroy();
#endif
		return BaseClass::Holster( pSwitchingTo );
	}

	bool Reload( void );
	void Drop(const Vector &vecVelocity)
	{
		EffectDestroy();
#if physgun_audio == true
		SoundDestroy();
#endif

#ifndef CLIENT_DLL
		UTIL_Remove( this );
#endif
	}

	bool HasAnyAmmo( void );

#ifdef use_old_physgun_attach
	void AttachObject(CBaseEntity* pEdict, const Vector& start, const Vector& end, float distance);
#else
	void AttachObject( CBaseEntity *pEdict, IPhysicsObject *pPhysics, short physicsbone, const Vector& start, const Vector &end, float distance );
#endif
	void UpdateObject( void );
	void DetachObject( void );

	void TraceLine( trace_t *ptr );

	void EffectCreate( void );
	void EffectUpdate( void );
	void EffectDestroy( void );

#if physgun_audio == true
	void SoundCreate( void );
	void SoundDestroy( void );
	void SoundStop( void );
	void SoundStart( void );
	void SoundUpdate( void );
#endif

	int ObjectCaps( void ) 
	{ 
		int caps = BaseClass::ObjectCaps();
		if ( m_active )
		{
			caps |= FCAP_DIRECTIONAL_USE;
		}
		return caps;
	}

	CBaseEntity *GetBeamEntity();

	// Sprite scale factor 
	float	SpriteScaleFactor();

private:
	CNetworkVar( int, m_active );
	bool		m_useDown;
	CNetworkHandle( CBaseEntity, m_hObject );
	CNetworkVar( int, m_physicsBone );
	float		m_distance;
	float		m_rotation;
	float		m_movementLength;
	float		m_lastYaw;
	int			m_soundState;
	Vector		m_originalObjectPosition;
	CNetworkVector	( m_targetPosition );
	CNetworkVector	( m_worldPosition );
	CNetworkVar( int, m_nHeldModelBone );	// ragdolls: the model bone of the held physics bone (m_worldPosition is local to it), else -1
	CNetworkVar( int, m_nGrabCount );	// +1 on every grab, so the client can tell when the server's grab data is for this grab

#ifdef CLIENT_DLL
	// a grab the client predicted before the server's data for it arrived:
	// draw the beam end from the hold distance meanwhile, not from the
	// previous grab's networked point
	bool		m_bPredictedGrab;
	int			m_nPredictedGrabCount;
	float		m_flPredictedDistance;
#endif

//#ifdef ARGG
	// adnan
	// this is how we tell if we're rotating what we're holding
	CNetworkVar( bool, m_bIsCurrentlyHolding);
	CNetworkVar( bool, m_bIsCurrentlyRotating );
	// end adnan
//#endif

	CSoundPatch					*m_sndMotor;		// Whirring sound for the gun
	CSoundPatch					*m_sndLockedOn;
	CSoundPatch					*m_sndLightObject;
	CSoundPatch					*m_sndHeavyObject;

	CGravControllerPoint		m_gravCallback;

	bool		m_bInWeapon1;
	bool		m_bInWeapon2;
	bool		m_bBlockPrimary; // right click shit.
	float		m_flFreezeCooldownEnd;	// after freezing something, no grabbing until this time (its own timer: m_flNextPrimaryAttack is also the deploy delay)

	float		m_flRotateX;
	float		m_flRotateY;
	QAngle		m_angLockedView;
	bool		m_bWasRotating;
	//CHandle<CSprite>	m_hCenterSprite;


	DECLARE_ACTTABLE();
};

IMPLEMENT_NETWORKCLASS_ALIASED( WeaponGravityGun, DT_WeaponGravityGun )

BEGIN_NETWORK_TABLE( CWeaponGravityGun, DT_WeaponGravityGun )
#ifdef CLIENT_DLL
	RecvPropEHandle( RECVINFO( m_hObject ) ),
	RecvPropInt( RECVINFO( m_physicsBone ) ),
	RecvPropVector( RECVINFO( m_targetPosition ) ),
	RecvPropVector( RECVINFO( m_worldPosition ) ),
	RecvPropInt( RECVINFO( m_nHeldModelBone ) ),
	RecvPropInt( RECVINFO( m_nGrabCount ) ),
	RecvPropInt( RECVINFO(m_active) ),
//#ifdef ARGG
	// adnan
	// also receive if we're rotating what we're holding (by pressing use)
	RecvPropBool( RECVINFO( m_bIsCurrentlyHolding ) ),
	RecvPropBool( RECVINFO( m_bIsCurrentlyRotating ) ),
	// end adnan
//#endif
#else
	SendPropEHandle( SENDINFO( m_hObject ) ),
	SendPropInt( SENDINFO( m_physicsBone ) ),
	SendPropVector(SENDINFO( m_targetPosition ), -1, SPROP_COORD),
	SendPropVector(SENDINFO( m_worldPosition ), -1, SPROP_COORD),
	SendPropInt( SENDINFO( m_nHeldModelBone ), 10 ),
	SendPropInt( SENDINFO( m_nGrabCount ), 8, SPROP_UNSIGNED ),
	SendPropInt( SENDINFO(m_active), 1, SPROP_UNSIGNED ),
//#ifdef ARGG
	// adnan
	// need to seind if we're rotating what we're holding
	SendPropBool( SENDINFO( m_bIsCurrentlyHolding ) ),
	SendPropBool( SENDINFO( m_bIsCurrentlyRotating ) ),
	// end adnan
//#endif
#endif
END_NETWORK_TABLE()

#ifdef CLIENT_DLL
BEGIN_PREDICTION_DATA( CWeaponGravityGun )
DEFINE_FIELD(m_lastYaw, FIELD_FLOAT),
DEFINE_FIELD(m_bBlockPrimary, FIELD_BOOLEAN),
END_PREDICTION_DATA()
#endif

LINK_ENTITY_TO_CLASS( weapon_physgun, CWeaponGravityGun );
PRECACHE_WEAPON_REGISTER(weapon_physgun);

acttable_t	CWeaponGravityGun::m_acttable[] =
{
	{ ACT_HL2MP_IDLE,					ACT_HL2MP_IDLE_PHYSGUN,					false },
	{ ACT_HL2MP_RUN,					ACT_HL2MP_RUN_PHYSGUN,					false },
	{ ACT_HL2MP_IDLE_CROUCH,			ACT_HL2MP_IDLE_CROUCH_PHYSGUN,			false },
	{ ACT_HL2MP_WALK_CROUCH,			ACT_HL2MP_WALK_CROUCH_PHYSGUN,			false },
	{ ACT_HL2MP_GESTURE_RANGE_ATTACK,	ACT_HL2MP_GESTURE_RANGE_ATTACK_PHYSGUN,	false },
	{ ACT_HL2MP_GESTURE_RELOAD,			ACT_HL2MP_GESTURE_RELOAD_PHYSGUN,		false },
	{ ACT_HL2MP_JUMP,					ACT_HL2MP_JUMP_PHYSGUN,					false },
};

IMPLEMENT_ACTTABLE(CWeaponGravityGun);


//---------------------------------------------------------
// Save/Restore
//---------------------------------------------------------
BEGIN_DATADESC( CWeaponGravityGun )

	DEFINE_FIELD( m_active,				FIELD_INTEGER ),
	DEFINE_FIELD( m_useDown,				FIELD_BOOLEAN ),
	DEFINE_FIELD( m_hObject,				FIELD_EHANDLE ),
	DEFINE_FIELD( m_physicsBone,				FIELD_INTEGER ),
	DEFINE_FIELD( m_distance,			FIELD_FLOAT ),
	DEFINE_FIELD( m_movementLength,		FIELD_FLOAT ),
	DEFINE_FIELD( m_soundState,			FIELD_INTEGER ),
	DEFINE_FIELD( m_originalObjectPosition,	FIELD_POSITION_VECTOR ),
//#ifdef ARGG
	// adnan
	DEFINE_FIELD( m_bIsCurrentlyRotating, FIELD_BOOLEAN ),
	// end adnan
//#endif
	DEFINE_SOUNDPATCH( m_sndMotor ),
	DEFINE_SOUNDPATCH( m_sndLockedOn ),
	DEFINE_SOUNDPATCH( m_sndLightObject ),
	DEFINE_SOUNDPATCH( m_sndHeavyObject ),
	DEFINE_EMBEDDED( m_gravCallback ),
	// Physptrs can't be saved in embedded classes..
	DEFINE_PHYSPTR( m_gravCallback.m_controller ),

END_DATADESC()


enum physgun_soundstate { SS_SCANNING, SS_LOCKEDON };
enum physgun_soundIndex { SI_LOCKEDON = 0, SI_SCANNING = 1, SI_LIGHTOBJECT = 2, SI_HEAVYOBJECT = 3, SI_ON, SI_OFF };


//=========================================================
//=========================================================

CWeaponGravityGun::CWeaponGravityGun()
{
	m_active = false;
	m_bFiresUnderwater = true;
	m_bInWeapon1 = false;
	m_bInWeapon2 = false;
	m_bBlockPrimary = false;
	m_flFreezeCooldownEnd = 0;
	m_useDown = false;
	m_distance = 0;
	m_rotation = 0;
	m_movementLength = 0;
	m_lastYaw = 0;
	m_soundState = 0;
	m_physicsBone = 0;
	m_nHeldModelBone = -1;
	m_flRotateX = 0;
	m_flRotateY = 0;
	m_bWasRotating = false;
	m_originalObjectPosition = vec3_origin;
	m_nGrabCount = 0;
#ifdef CLIENT_DLL
	m_bPredictedGrab = false;
	m_nPredictedGrabCount = 0;
	m_flPredictedDistance = 0;
#endif
}


//-----------------------------------------------------------------------------
// On Remove
//-----------------------------------------------------------------------------
void CWeaponGravityGun::UpdateOnRemove(void)
{
	EffectDestroy();
#if physgun_audio == true
	SoundDestroy();
#endif
	BaseClass::UpdateOnRemove();
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
// adnan
// want to add an angles modifier key
bool CGravControllerPoint::UpdateObject( CBasePlayer *pPlayer, CBaseEntity *pEntity, float* x, float* y)
{
	//IPhysicsObject *pPhysics = GetPhysObjFromPhysicsBone( pEntity, m_attachedPhysicsBone );
	//if ( !pEntity || !pPhysics )
	//{
	//	return false;
	//}
//#ifdef ARGG
	// adnan
	// if we've been rotating it, set it to its proper new angles (change m_attachedAnglesPlayerSpace while modifier)
	//Pickup_GetRotatedCarryAngles( pEntity, pPlayer, pPlayer->EntityToWorldTransform(), angles );
	// added the ... && (mousedx | mousedy) so we dont have to calculate if no mouse movement
	// UPDATE: m_vecRotatedCarryAngles has become a temp variable... can be cleaned up by using actual temp vars
	// GMod 10: while rotating (Use), Left/Right turn the object at phys_spinspeed degrees a
	// second about the vertical, the same way moving the mouse sideways does
	float flKeyYaw = 0;
	if ( m_bHasRotatedCarryAngles )
	{
		if ( pPlayer->m_nButtons & IN_MOVELEFT )
			flKeyYaw += phys_spinspeed.GetFloat() * gpGlobals->frametime;
		if ( pPlayer->m_nButtons & IN_MOVERIGHT )
			flKeyYaw -= phys_spinspeed.GetFloat() * gpGlobals->frametime;
	}

#ifdef CLIENT_DLL
	if( m_bHasRotatedCarryAngles && (pPlayer->m_pCurrentCommand->mousedx || pPlayer->m_pCurrentCommand->mousedy || flKeyYaw != 0) )
#else
	if( m_bHasRotatedCarryAngles && (pPlayer->GetCurrentCommand()->mousedx || pPlayer->GetCurrentCommand()->mousedy || flKeyYaw != 0) )
#endif
	{
		// method II: relative orientation
		VMatrix vDeltaRotation, vCurrentRotation, vNewRotation;
		
		//MatrixFromAngles( m_targetRotation, vCurrentRotation );
		MatrixFromAngles( m_targetRotation, vCurrentRotation );

// This controls the rotational speed. It was initially 0.05 for some stupid reason.
#ifdef CLIENT_DLL
		m_vecRotatedCarryAngles[YAW] = pPlayer->m_pCurrentCommand->mousedx * -0.4;
		m_vecRotatedCarryAngles[PITCH] = pPlayer->m_pCurrentCommand->mousedy * 0.4;
#else
		m_vecRotatedCarryAngles[YAW] = pPlayer->GetCurrentCommand()->mousedx * -0.4;
		m_vecRotatedCarryAngles[PITCH] = pPlayer->GetCurrentCommand()->mousedy * 0.4;
#endif
		m_vecRotatedCarryAngles[YAW] += flKeyYaw;
		m_vecRotatedCarryAngles[ROLL] = 0;
		MatrixFromAngles( m_vecRotatedCarryAngles, vDeltaRotation );

		MatrixMultiply(vDeltaRotation, vCurrentRotation, vNewRotation);
		MatrixToAngles( vNewRotation, m_targetRotation );

	}
	// end adnan
//#endif

	// Keep the position target EffectUpdate set last (m_targetPosition used to be
	// stale here, the world origin after a previous drop, and the object got
	// pulled to it on frames EffectUpdate didn't run). m_targetRotation is kept
	// relative to the player's yaw; the shadow controller wants world angles.
	QAngle angTarget = TransformAnglesFromPlayerSpace( m_targetRotation, pPlayer );

	// GMod 10: Sprint + Use snaps the object's angles to gm_snapdegrees while held, and
	// letting go of Use with Sprint still down keeps the snapped angles
	float flSnap = gm_snapdegrees.GetFloat();
	if ( flSnap > 0 && ( pPlayer->m_nButtons & IN_SPEED ) )
	{
		bool bSnapping = ( pPlayer->m_nButtons & IN_USE ) != 0;
		bool bCommit = ( pPlayer->m_afButtonReleased & IN_USE ) != 0;
		if ( bSnapping || bCommit )
		{
			for ( int i = 0; i < 3; i++ )
				angTarget[i] = RoundFloatToInt( angTarget[i] / flSnap ) * flSnap;

			if ( bCommit )
				m_targetRotation = TransformAnglesToPlayerSpace( angTarget, pPlayer );
		}
	}

	SetTargetPosition( m_targetPosition, angTarget );

	return true;
}

//#ifdef ARGG
// adnan
// this is where we say that we dont want ot apply the current calculated view angles
//-----------------------------------------------------------------------------
// Purpose: Allow weapons to override mouse input to viewangles (for orbiting)
//-----------------------------------------------------------------------------
bool CWeaponGravityGun::OverrideViewAngles( void )
{
	CBasePlayer *pPlayer = ToBasePlayer( GetOwner() );
	
	if(!pPlayer)
		return false;

	if (m_bIsCurrentlyRotating && pPlayer->m_nButtons & IN_ATTACK) {
		return true;
	}

	return false;
}
// end adnan
//#endif

//=========================================================
//=========================================================
void CWeaponGravityGun::Spawn( )
{
	BaseClass::Spawn();
	SetModel( GetWorldModel() );

	// The physgun uses a different skin
	m_nSkin = PHYSGUN_SKIN;

	FallInit();
}

//-----------------------------------------------------------------------------
// Purpose: Usable the moment it's out. The base deploy holds off primary fire
//          until the draw animation ends; the physgun doesn't need that, and a
//          freeze lock from before it was put away shouldn't carry over.
//-----------------------------------------------------------------------------
bool CWeaponGravityGun::Deploy(void)
{
	bool bResult = BaseClass::Deploy();

	m_flNextPrimaryAttack = gpGlobals->curtime;
	m_flNextSecondaryAttack = gpGlobals->curtime;
	m_flFreezeCooldownEnd = 0;
	m_bBlockPrimary = false;

	return bResult;
}

void CWeaponGravityGun::OnRestore( void )
{
	BaseClass::OnRestore();

	if ( m_gravCallback.m_controller )
	{
		m_gravCallback.m_controller->SetEventHandler( &m_gravCallback );
	}
}


//=========================================================
//=========================================================
void CWeaponGravityGun::Precache( void )
{
	BaseClass::Precache();

	g_physgunBeam1 = PrecacheModel(PHYSGUN_BEAM_SPRITE);
	g_physgunBeam = PrecacheModel(PHYSGUN_BEAM_SPRITE_ACTIVE);
	g_physgunGlow = PrecacheModel(PHYSGUN_BEAM_GLOW);

//#define PHYSGUN_STRIDER_BEAM_SPRITE		"sprites/strider_bluebeam.vmt"
//#define PHYSGUN_BEAM_SPRITE		"sprites/physbeam.vmt"
//#define PHYSGUN_BEAM_GLOW		"sprites/physglow1.vmt"

//#ifdef physgun_audio == true;
	PrecacheScriptSound( "Weapon_Physgun.Scanning" );
	PrecacheScriptSound( "Weapon_Physgun.LockedOn" );
	PrecacheScriptSound( "Weapon_Physgun.Scanning" );
	PrecacheScriptSound( "Weapon_Physgun.LightObject" );
	PrecacheScriptSound( "Weapon_Physgun.HeavyObject" );
//#endif
}

void CWeaponGravityGun::EffectCreate( void )
{
	EffectUpdate();
	m_active = true;
}


// Andrew; added so we can trace both in EffectUpdate and DrawModel with the same results
void CWeaponGravityGun::TraceLine( trace_t *ptr )
{
	CBasePlayer *pOwner = ToBasePlayer( GetOwner() );
	if ( !pOwner )
		return;

	Vector start, forward, right;
	pOwner->EyeVectors( &forward, &right, NULL );

	start = pOwner->Weapon_ShootPosition();
	Vector end = start + forward * 4096;

	// UTIL_TraceLine( start, end, MASK_SHOT, pOwner, COLLISION_GROUP_NONE, ptr );
	UTIL_TraceLine( start, end, MASK_SHOT|CONTENTS_GRATE, pOwner, COLLISION_GROUP_NONE, ptr );
}

//-----------------------------------------------------------------------------
// Sprite scale factor 
//-----------------------------------------------------------------------------
inline float CWeaponGravityGun::SpriteScaleFactor()
{
	return 1.0f;
}

void CWeaponGravityGun::EffectUpdate( void )
{
	Vector start, forward, right;
	trace_t tr;

	CBasePlayer *pOwner = ToBasePlayer( GetOwner() );
	if ( !pOwner )
		return;

	pOwner->EyeVectors( &forward, &right, NULL );

	int i;
	float flScaleFactor = SpriteScaleFactor();
	CBaseEntity* pBeamEnt = pOwner->GetViewModel();

	start = pOwner->Weapon_ShootPosition();

	TraceLine( &tr );
	Vector end = tr.endpos;
	float distance = tr.fraction * 4096;

	if ( m_hObject == NULL && tr.DidHitNonWorldEntity() )
	{
		CBaseEntity *pEntity = tr.m_pEnt;
		// todo
#ifdef use_old_physgun_attach
		AttachObject( pEntity, start, tr.endpos, distance );
#else
		AttachObject( pEntity, GetPhysObjFromPhysicsBone( pEntity, tr.physicsbone ), tr.physicsbone, start, tr.endpos, distance );
#endif
		m_lastYaw = pOwner->EyeAngles().y;
	}

	// Add the incremental player yaw to the target transform
	QAngle angles = m_gravCallback.TransformAnglesFromPlayerSpace( m_gravCallback.m_targetRotation, pOwner );

	CBaseEntity* pObject = m_hObject;

	if (pObject)
	{
		if (m_useDown)
		{
			if (pOwner->m_afButtonReleased & IN_USE)
			{
				m_useDown = false;
			}
		}
		else
		{
			if (pOwner->m_afButtonPressed & IN_USE)
			{
				m_useDown = true;
			}
		}

		if (m_useDown)
		{
#ifndef CLIENT_DLL
			pOwner->SetPhysicsFlag(PFLAG_DIROVERRIDE, true);
#endif
			if (pOwner->m_nButtons & IN_FORWARD)
			{
				m_distance = Approach( 2048, m_distance, gpGlobals->frametime * 100 );	// GMod 10: up to 2048
			}
			if ( pOwner->m_nButtons & IN_BACK )
			{
				m_distance = Approach( 40, m_distance, gpGlobals->frametime * 100 );
			}
			if (pOwner->m_nButtons & IN_MOVELEFT)
			{
				// (was: indexed angles with an uninitialized float and rotated the gun itself)
			}
			if (pOwner->m_nButtons & IN_MOVERIGHT)
			{
			}
		} else {
#ifndef CLIENT_DLL
			pOwner->SetPhysicsFlag(PFLAG_DIROVERRIDE, false);
#endif
		}

		if ( pOwner->m_nButtons & IN_WEAPON1 )
		{
			m_distance = Approach( 1024, m_distance, m_distance * 0.1 );
#ifdef CLIENT_DLL
			if ( gpGlobals->maxClients > 1 )
			{
				gHUD.m_bSkipClear = false;
			}
#endif
		}
		if ( pOwner->m_nButtons & IN_WEAPON2 )
		{
			m_distance = Approach( 40, m_distance, m_distance * 0.1 );
#ifdef CLIENT_DLL
			if ( gpGlobals->maxClients > 1 )
			{
				gHUD.m_bSkipClear = false;
			}
#endif
		}

		IPhysicsObject* pPhys = GetPhysObjFromPhysicsBone(pObject, m_physicsBone);
		if ( pPhys )
		{
			//if ( pPhys->IsAsleep() )
			//{
				// on the odd chance that it's gone to sleep while under anti-gravity
				//pPhys->Wake();
			//}

			Vector newPosition = start + forward * m_distance;
			Vector offset;
			pPhys->LocalToWorld( &offset, m_worldPosition );
#ifndef CLIENT_DLL
			// where the beam holds the object, in world space, for the client to
			// draw (it can't work it out for ragdoll bones itself)
			m_targetPosition = offset;
#endif
			Vector vecOrigin;
			pPhys->GetPosition( &vecOrigin, NULL );
			m_gravCallback.SetTargetPosition( newPosition + (vecOrigin - offset), angles );
			Vector dir = (newPosition - pObject->GetLocalOrigin());
			m_movementLength = dir.Length();
		}
	}
	else
	{
		m_targetPosition = end;
		m_gravCallback.SetTargetPosition( end, m_gravCallback.m_targetRotation );
	}
}

#if physgun_audio == true
void CWeaponGravityGun::SoundCreate( void )
{
	m_soundState = SS_SCANNING;
	SoundStart();
}
#endif

#if physgun_audio == true
void CWeaponGravityGun::SoundDestroy( void )
{
	SoundStop();
}
#endif

#if physgun_audio == true
void CWeaponGravityGun::SoundStop( void )
{
	CBasePlayer *pOwner = ToBasePlayer( GetOwner() );
	if ( !pOwner )
		return;

	switch( m_soundState )
	{
	case SS_SCANNING:
		(CSoundEnvelopeController::GetController()).SoundDestroy( m_sndMotor );
		m_sndMotor = NULL;
		break;
	case SS_LOCKEDON:
		(CSoundEnvelopeController::GetController()).SoundDestroy( m_sndMotor );
		m_sndMotor = NULL;
		(CSoundEnvelopeController::GetController()).SoundDestroy( m_sndLockedOn );
		m_sndLockedOn = NULL;
		(CSoundEnvelopeController::GetController()).SoundDestroy( m_sndLightObject );
		m_sndLightObject = NULL;
		(CSoundEnvelopeController::GetController()).SoundDestroy( m_sndHeavyObject );
		m_sndHeavyObject = NULL;
		break;
	}
}
#endif


//-----------------------------------------------------------------------------
// Purpose: returns the linear fraction of value between low & high (0.0 - 1.0) * scale
//			e.g. UTIL_LineFraction( 1.5, 1, 2, 1 ); will return 0.5 since 1.5 is
//			halfway between 1 and 2
// Input  : value - a value between low & high (clamped)
//			low - the value that maps to zero
//			high - the value that maps to "scale"
//			scale - the output scale
// Output : parametric fraction between low & high
//-----------------------------------------------------------------------------
static float UTIL_LineFraction( float value, float low, float high, float scale )
{
	if ( value < low )
		value = low;
	if ( value > high )
		value = high;

	float delta = high - low;
	if ( delta == 0 )
		return 0;
	
	return scale * (value-low) / delta;
}

#if physgun_audio == true
void CWeaponGravityGun::SoundStart( void )
{
	CPASAttenuationFilter filter( this );

	switch( m_soundState )
	{
	case SS_SCANNING:
		{
			m_sndMotor = (CSoundEnvelopeController::GetController()).SoundCreate( filter, entindex(), CHAN_STATIC, "Weapon_Physgun.Scanning", ATTN_NORM );
			(CSoundEnvelopeController::GetController()).Play( m_sndMotor, 1.0f, 100 );
		}
		break;
	case SS_LOCKEDON:
		{
			m_sndLockedOn = (CSoundEnvelopeController::GetController()).SoundCreate( filter, entindex(), CHAN_STATIC, "Weapon_Physgun.LockedOn", ATTN_NORM );
			(CSoundEnvelopeController::GetController()).Play( m_sndLockedOn, 1.0f, 100 );
			m_sndMotor = (CSoundEnvelopeController::GetController()).SoundCreate( filter, entindex(), CHAN_STATIC, "Weapon_Physgun.Scanning", ATTN_NORM );
			(CSoundEnvelopeController::GetController()).Play( m_sndMotor, 1.0f, 100 );
			m_sndLightObject = (CSoundEnvelopeController::GetController()).SoundCreate( filter, entindex(), CHAN_STATIC, "Weapon_Physgun.LightObject", ATTN_NORM );
			(CSoundEnvelopeController::GetController()).Play( m_sndLightObject, 1.0f, 100 );
			m_sndHeavyObject = (CSoundEnvelopeController::GetController()).SoundCreate( filter, entindex(), CHAN_STATIC, "Weapon_Physgun.HeavyObject", ATTN_NORM );
			(CSoundEnvelopeController::GetController()).Play( m_sndHeavyObject, 1.0f, 100 );
		}
		break;
	}											//   volume, att, flags, pitch
}
#endif

#if physgun_audio == true
void CWeaponGravityGun::SoundUpdate( void )
{
	int newState;
	
	if ( m_hObject )
		newState = SS_LOCKEDON;
	else
		newState = SS_SCANNING;

	if ( newState != m_soundState )
	{
		SoundStop();
		m_soundState = newState;
		SoundStart();
	}

	switch( m_soundState )
	{
	case SS_SCANNING:
		break;
	case SS_LOCKEDON:
		{
			CPASAttenuationFilter filter( this );

			float height = m_hObject->GetAbsOrigin().z - m_originalObjectPosition.z;

			// go from pitch 90 to 150 over a height of 500
			int pitch = 90 + (int)UTIL_LineFraction( height, 0, 500, 60 );

			assert(m_sndLockedOn!=NULL);
			if ( m_sndLockedOn != NULL )
			{
				(CSoundEnvelopeController::GetController()).SoundChangePitch( m_sndLockedOn, pitch, 0.0f );
			}

			// attenutate the movement sounds over 200 units of movement
			float distance = UTIL_LineFraction( m_movementLength, 0, 200, 1.0 );

			// blend the "mass" sounds between 50 and 500 kg
			IPhysicsObject *pPhys = GetPhysObjFromPhysicsBone( m_hObject, m_physicsBone );
			if ( pPhys == NULL )
			{
				// we no longer exist!
				break;
			}
			
			//float fade = UTIL_LineFraction( pPhys->GetMass(), 50, 500, 1.0 );
			float fade = 100;
			if (pPhys)
				fade = UTIL_LineFraction(pPhys->GetMass(), 50, 500, 1.0);

			(CSoundEnvelopeController::GetController()).SoundChangeVolume( m_sndLightObject, fade * distance, 0.0f );

			(CSoundEnvelopeController::GetController()).SoundChangeVolume( m_sndHeavyObject, (1.0 - fade) * distance, 0.0f );
		}
		break;
	}
}
#endif

CBaseEntity *CWeaponGravityGun::GetBeamEntity()
{
	CBasePlayer *pOwner = ToBasePlayer( GetOwner() );
	if ( !pOwner )
		return NULL;

	// Make sure I've got a view model
	CBaseViewModel *vm = pOwner->GetViewModel();
	if ( vm )
		return vm;

	return pOwner;
}

void CWeaponGravityGun::EffectDestroy( void )
{
#ifdef CLIENT_DLL
	gHUD.m_bSkipClear = false;
#endif
	m_active = false;
#if physgun_audio == true
	SoundStop();
#endif

	DetachObject();
}

void CWeaponGravityGun::UpdateObject( void )
{
	CBasePlayer *pPlayer = ToBasePlayer( GetOwner() );
	Assert( pPlayer );

	CBaseEntity *pObject = m_hObject;
	if ( !pObject )
		return;

	if (!m_gravCallback.UpdateObject(pPlayer, pObject, &m_flRotateX, &m_flRotateY))
	{
		DetachObject();
		return;
	}
}

void CWeaponGravityGun::DetachObject( void )
{
	if ( m_hObject )
	{
#ifndef CLIENT_DLL
		CBasePlayer *pOwner = ToBasePlayer( GetOwner() );
		Pickup_OnPhysGunDrop( m_hObject, pOwner, DROPPED_BY_CANNON );
#endif

		IPhysicsObject *pList[VPHYSICS_MAX_OBJECT_LIST_COUNT];
		int count = m_hObject->VPhysicsGetObjectList( pList, ARRAYSIZE(pList) );
		for ( int i = 0; i < count; i++ )
		{
			PhysClearGameFlags( pList[i], FVPHYSICS_PLAYER_HELD );
		}
		m_gravCallback.DetachEntity();
		m_hObject = NULL;
		m_physicsBone = 0;
	m_nHeldModelBone = -1;
	}
}

// srcbox: revert me!
#ifdef use_old_physgun_attach
void CWeaponGravityGun::AttachObject( CBaseEntity *pObject, const Vector& start, const Vector &end, float distance )
#else
void CWeaponGravityGun::AttachObject(CBaseEntity * pObject, IPhysicsObject * pPhysics, short physicsbone, const Vector & start, const Vector & end, float distance)
#endif
{
	CBasePlayer *pOwner = ToBasePlayer( GetOwner() );
	//if( !pOwner )
	//	return;
	m_hObject = pObject;

#ifdef CLIENT_DLL
	// until the server's data for this grab arrives (m_nGrabCount moves on),
	// the beam end is drawn at the hold distance along the aim
	if ( !m_bPredictedGrab )
	{
		m_bPredictedGrab = true;
		m_nPredictedGrabCount = ( m_nGrabCount + 1 ) & 0xFF;
		m_flPredictedDistance = distance;
	}
#endif

#ifdef use_old_physgun_attach
	IPhysicsObject* pPhysics = pObject ? (pObject->VPhysicsGetObject()) : NULL;
#endif
	if (pPhysics && pObject->GetMoveType() == MOVETYPE_VPHYSICS )
	{
		if (gmod_physgun_lock == true){
			pPhysics->EnableMotion(true);

			// a frozen vehicle has all its objects frozen (see SecondaryAttack); unfreeze the
			// wheels too, or they fight the body being carried
			if ( PhysgunIsVehicle( pObject ) )
			{
				IPhysicsObject *pVehicleList[VPHYSICS_MAX_OBJECT_LIST_COUNT];
				int vehicleCount = pObject->VPhysicsGetObjectList( pVehicleList, ARRAYSIZE( pVehicleList ) );
				for ( int i = 0; i < vehicleCount; i++ )
					pVehicleList[i]->EnableMotion( true );
			}
		}
		m_distance = distance;
		m_physicsBone = physicsbone;	// every later lookup uses this; it was left at 0 (the root bone)
#ifndef CLIENT_DLL
		m_nGrabCount = ( m_nGrabCount + 1 ) & 0xFF;
		m_targetPosition = end;			// the held point, right away (was last grab's until the next update)

		// ragdolls: tell the client which model bone the held point is on, so it can draw the
		// beam end on the bone as it's drawn (like GMod 10's m_iPhysBone)
		m_nHeldModelBone = -1;
		CRagdollProp *pRagdollProp = dynamic_cast< CRagdollProp * >( pObject );
		if ( pRagdollProp && physicsbone >= 0 && physicsbone < pRagdollProp->GetRagdoll()->listCount )
			m_nHeldModelBone = pRagdollProp->GetRagdoll()->boneIndex[physicsbone];
#endif

		Vector worldPosition;
		pPhysics->WorldToLocal( &worldPosition, end );
		m_worldPosition = worldPosition;
		Vector vecOrigin;
		pPhysics->GetPosition( &vecOrigin, NULL );
		m_gravCallback.AttachEntity( pOwner, pObject, pPhysics, physicsbone, vecOrigin );

		m_originalObjectPosition = vecOrigin;

		pPhysics->Wake();
		IPhysicsObject *pList[VPHYSICS_MAX_OBJECT_LIST_COUNT];
		int count = pObject->VPhysicsGetObjectList( pList, ARRAYSIZE(pList) );
		for ( int i = 0; i < count; i++ )
		{
			PhysSetGameFlags( pList[i], FVPHYSICS_PLAYER_HELD );
		}

#ifndef CLIENT_DLL
		Pickup_OnPhysGunPickup( pObject, pOwner );
#endif
	}
	// Zeldaboy14
	// I disabled this code because returning a null pointer crashs the fucking game. Why were they doing this!!?!?!?
	// 3-7-26 - Recently realized that this else statment is in the original code from Valve's. However, the original physgun doesn't pick up world stuff.
	/*else
	{
		m_hObject = NULL;
		m_physicsBone = 0;
	m_nHeldModelBone = -1;
	}*/
}

//=========================================================
//=========================================================
void CWeaponGravityGun::PrimaryAttack( void )
{
	if (m_bBlockPrimary) {
		return;
	}

	// Are we capable of firing again?
	if (m_flFreezeCooldownEnd > gpGlobals->curtime)
		return;

	if ( !m_active )
	{
		SendWeaponAnim( ACT_VM_PRIMARYATTACK );
		EffectCreate();
#if physgun_audio == true
		SoundCreate();
#endif
	}
	else
	{
		EffectUpdate();
#if physgun_audio == true
		SoundUpdate();
#endif
	}
}

void CWeaponGravityGun::SecondaryAttack( void )
{
	if (gmod_physgun_lock == true) {
		if (m_hObject) {
			IPhysicsObject* phys = GetPhysObjFromPhysicsBone( m_hObject, m_physicsBone );
			if (phys) {
				phys->EnableMotion(false);

				// GMod 10 (base gamemode OnPhysgunFreeze): "With the jeep we need to pause all
				// of its physics objects to stop it spazzing out and killing the server" - the
				// wheels kept simulating against the frozen body
				if ( PhysgunIsVehicle( m_hObject ) )
				{
					IPhysicsObject *pList[VPHYSICS_MAX_OBJECT_LIST_COUNT];
					int count = m_hObject->VPhysicsGetObjectList( pList, ARRAYSIZE( pList ) );
					for ( int i = 0; i < count; i++ )
						pList[i]->EnableMotion( false );
				}

				m_bBlockPrimary = true;
			}
		}
		EffectDestroy();
#if physgun_audio == true
		SoundDestroy();
#endif	
		return;
	}
	else {
		if (m_active)
		{
			EffectDestroy();
		#if physgun_audio == true
			SoundDestroy();
		#endif	
			return;
		}
	}
}

#ifdef CLIENT_DLL

//-----------------------------------------------------------------------------
// Purpose: Render the beams and sprites for the physgun (both view and world models)
//-----------------------------------------------------------------------------
extern void FormatViewModelAttachment( Vector &vOrigin, bool bInverse );

//-----------------------------------------------------------------------------
// Purpose: The light in the physgun's core, as GMod 10 drew it (C_PhysBeam::DrawModel in its
//			client.dll), at the gun's first attachment ("core" on the world model, "muzzle"
//			on the view model):
//			- beam off: two sprites pulsing between 40 and 56 units, 5 units apart along the
//			  attachment's forward axis
//			- beam on: sprites of a random 16-64 units every frame (the fast flicker), 2 units
//			  apart; 2 of them, or 7 while holding something
//-----------------------------------------------------------------------------
void CWeaponGravityGun::DrawCoreGlow( C_BasePlayer *pOwner )
{
	// First person: the view model; third person: this weapon's world model
	C_BaseAnimating *pModel = pOwner->GetRenderedWeaponModel();
	if ( !pModel )
		return;

	// GMod 10 used attachment 1: "core" on the world model (w_physics), "muzzle" on the
	// view model (c_superphyscannon has no "core")
	int iAttachment = pModel->LookupAttachment( "core" );
	if ( iAttachment <= 0 )
		iAttachment = 1;

	Vector vecCore;
	QAngle angCore;
	if ( !pModel->GetAttachment( iAttachment, vecCore, angCore ) )
		return;

	// In first person this is drawn with the view model, in its view (view model FOV and depth
	// range), so it's hidden behind the gun's parts like the rest of the model. View model
	// attachments come moved to line up with the world view; move it back.
	if ( !pOwner->ShouldDrawThisPlayer() )
		::FormatViewModelAttachment( vecCore, true );	// the global one, not C_BaseAnimating's

	Vector vecForward;
	AngleVectors( angCore, &vecForward );

	CMatRenderContextPtr pRenderContext( materials );
	pRenderContext->Bind( materials->FindMaterial( PHYSGUN_CORE_GLOW, TEXTURE_GROUP_CLIENT_EFFECTS ) );

	int nSprites = 2;
	float flSpacing = 5.0f;
	if ( m_active )
	{
		nSprites = m_hObject.Get() ? 7 : 2;
		flSpacing = 2.0f;
	}

	Vector vecPos = vecCore;
	for ( int i = 0; i < nSprites; i++ )
	{
		color32 clr;
		clr.r = clr.g = random->RandomInt( 120, 130 );
		clr.b = random->RandomInt( 200, 255 );
		clr.a = 255;

		float flSize;
		if ( m_active )
			flSize = random->RandomFloat( 16.0f, 64.0f );
		else
			flSize = 48.0f + 8.0f * sin( ( gpGlobals->curtime + i * 3.0f ) * 5.0f );

		DrawSprite( vecPos, flSize, flSize, clr );

		vecPos += vecForward * flSpacing;
	}
}

//-----------------------------------------------------------------------------
// Purpose: GMod 10's version of DrawBeamQuadratic (beamdraw.cpp): the same 16-segment curve
//			with black ends, but the texture repeats along the beam (every 50 units, measured
//			start to end) instead of being stretched once over its length.
//-----------------------------------------------------------------------------
static void DrawBeamQuadraticGMod10( const Vector &start, const Vector &control, const Vector &end, float width, const Vector &color, float scrollOffset )
{
	const int subdivisions = 16;
	float flLength = start.DistTo( end );

	CMatRenderContextPtr pRenderContext( materials );
	CBeamSegDraw beamDraw;
	beamDraw.Start( pRenderContext, subdivisions + 1, NULL );

	BeamSeg_t seg;
	seg.m_flAlpha = 1.0f;
	seg.m_flWidth = width;

	float t = 0;
	float u = fmod( scrollOffset, 1.0f );
	float dt = 1.0f / ( float )subdivisions;
	for ( int i = 0; i <= subdivisions; i++, t += dt )
	{
		float omt = 1 - t;
		seg.m_vPos = ( omt * omt ) * start + ( 2 * t * omt ) * control + ( t * t ) * end;
		seg.m_flTexCoord = u - t * flLength * 0.02f;
		seg.m_vColor = ( i == 0 || i == subdivisions ) ? vec3_origin : color;
		beamDraw.NextSeg( &seg );
	}

	beamDraw.End();
}

void CWeaponGravityGun::DrawEffects()
{
		// Render our effects
		C_BasePlayer *pOwner = ToBasePlayer( GetOwner() );

		if ( !pOwner )
			return;

		DrawCoreGlow( pOwner );

		if (!m_active)
			return;

		Vector points[3];
		QAngle tmpAngle;

		C_BaseEntity *pObject = m_hObject;

		Vector attachPos;
		QAngle attachAng;

		int iAttachment = pOwner->LookupAttachment("camera");
		C_BaseAnimating* pViewModel = dynamic_cast<C_BaseAnimating*>(pOwner->GetRenderedWeaponModel());
		if (!pOwner->ShouldDrawThisPlayer())
		{
			if (pViewModel)
			{
				pViewModel->GetAttachment(pViewModel->LookupAttachment("muzzle"), points[0], tmpAngle);
				iAttachment = pViewModel->LookupAttachment("muzzle");
			}
		}
		else if (pOwner->ShouldDrawThisPlayer()) {
			if (pViewModel)
			{
				pViewModel->GetAttachment(pViewModel->LookupAttachment("core"), points[0], tmpAngle);
				iAttachment = pViewModel->LookupAttachment("core");
			}
		}

		// a little noise 11t & 13t should be somewhat non-periodic looking
		//points[1].z += 4*sin( gpGlobals->curtime*11 ) + 5*cos( gpGlobals->curtime*13 );
		// the server has caught up with the grab we predicted (or there's none)
		if ( m_bPredictedGrab && ( pObject == NULL || m_nGrabCount == m_nPredictedGrabCount ) )
			m_bPredictedGrab = false;

		if ( pObject == NULL )
		{
			//points[2] = m_targetPosition;
			trace_t tr;
			TraceLine( &tr );
			points[2] = tr.endpos;
		}
		else if ( m_bPredictedGrab )
		{
			// just grabbed: the networked hold point is still the previous grab's
			Vector vecAim;
			pOwner->EyeVectors( &vecAim, NULL, NULL );
			points[2] = pOwner->Weapon_ShootPosition() + vecAim * m_flPredictedDistance;
		}
		else if ( m_nHeldModelBone >= 0 && pObject->GetBaseAnimating() )
		{
			// a ragdoll: m_worldPosition is local to the held bone; use that bone as it's
			// drawn now
			matrix3x4_t boneToWorld;
			pObject->GetBaseAnimating()->GetBoneTransform( m_nHeldModelBone, boneToWorld );
			VectorTransform( m_worldPosition, boneToWorld, points[2] );
		}
		else if ( m_physicsBone > 0 )
		{
			// another multi-part object we can't follow on the client: the server's held point
			points[2] = m_targetPosition;
		}
		else
		{
			// like GMod 10: the held point on the object as it's drawn now, every frame (the
			// server's networked point lags the drawn object by the interpolation delay)
			pObject->EntityToWorldSpace(m_worldPosition, &points[2]);
		}

		// GMod 10's control point: straight at the end point when not holding anything,
		// otherwise 0.6 of the beam's length out along the aim from the eye
		Vector forward;
		pOwner->EyeVectors( &forward, NULL, NULL );
		if (pObject == NULL)
		{
			points[1] = points[2];
		}
		else
		{
			points[1] = pOwner->EyePosition() + forward * ( points[2].DistTo( points[0] ) * 0.6f );
		}

		// Now draw it.
		CViewSetup beamView = *view->GetPlayerViewSetup();

		Frustum dummyFrustum;
		render->Push3DView( beamView, 0, NULL, dummyFrustum );

		CMatRenderContextPtr pRenderContext( materials );
	#if 1
		// HACK HACK:  Munge the depth range to prevent view model from poking into walls, etc.
		// Force clipped down range
		pRenderContext->DepthRange( 0.1f, 0.2f );
	#endif

		// The beam, as GMod 10 drew it (C_PhysBeam): physbeam when empty, scrolling backwards
		// slowly; physbeama when holding, scrolling forwards fast. Three layers: width 2 at
		// that speed, a random 2-5 at 1.1x and a random 2-6 at -0.9x.
		pRenderContext->Bind( materials->FindMaterial( pObject ? PHYSGUN_BEAM_SPRITE_ACTIVE : PHYSGUN_BEAM_SPRITE, TEXTURE_GROUP_CLIENT_EFFECTS ) );

		Vector color( 1, 1, 1 );
		float flScroll = ( pObject ? 10.0f : -2.0f ) * gpGlobals->curtime;
		DrawBeamQuadraticGMod10( points[0], points[1], points[2], 2.0f, color, flScroll );
		DrawBeamQuadraticGMod10( points[0], points[1], points[2], random->RandomFloat( 2.0f, 5.0f ), color, flScroll * 1.1f );
		DrawBeamQuadraticGMod10( points[0], points[1], points[2], random->RandomFloat( 2.0f, 6.0f ), color, flScroll * -0.9f );

		// The sparkle at the end: 2 sprites, or 7 while holding something
		pRenderContext->Bind( materials->FindMaterial( PHYSGUN_BEAM_GLOW, TEXTURE_GROUP_CLIENT_EFFECTS ) );

		int nSparkles = pObject ? 7 : 2;
		for ( int i = 0; i < nSparkles; i++ )
		{
			color32 clr;
			clr.r = clr.g = random->RandomInt( 100, 150 );
			clr.b = random->RandomInt( 200, 255 );
			clr.a = 255;

			float flSize = random->RandomFloat( 1.0f, 16.0f );
			DrawSprite( points[2], flSize, flSize, clr );
		}
	#if 1
		pRenderContext->DepthRange( 0.0f, 1.0f );
	#endif

		render->PopView( dummyFrustum );
}

int CWeaponGravityGun::DrawModel(int flags)
{
	// Only render these on the transparent pass
	if (flags & STUDIO_TRANSPARENCY)
	{
		DrawEffects();
		return 1;
	}

	// Only do this on the opaque pass
	return BaseClass::DrawModel(flags);
}

//-----------------------------------------------------------------------------
// Purpose: Draw the weapon
//-----------------------------------------------------------------------------
void CWeaponGravityGun::ViewModelDrawn( C_BaseViewModel* pBaseViewModel )
{

	// Render our effects
	DrawEffects();

	// Pass this back up
	BaseClass::ViewModelDrawn(pBaseViewModel);
}

//-----------------------------------------------------------------------------
// Purpose: We are always considered transparent
//-----------------------------------------------------------------------------
bool CWeaponGravityGun::IsTransparent( void )
{
	return true;
}

#endif


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CWeaponGravityGun::ItemPreFrame()
{
	BaseClass::ItemPreFrame();

#ifndef CLIENT_DLL
	// Update the object if the weapon is switched on.
	if( m_active )
	{
		UpdateObject();
	}
#endif
}

void CWeaponGravityGun::WeaponIdle(void)
{
	SendWeaponAnim(ACT_VM_IDLE);
	if (m_active)
	{
#if physgun_audio == true
		WeaponSound(SPECIAL1);
#endif

		EffectDestroy();
#if physgun_audio == true
		SoundDestroy();
#endif
	}
	else {
		m_bBlockPrimary = false;
	}
}

void CWeaponGravityGun::ItemPostFrame( void )
{
	CBasePlayer *pOwner = ToBasePlayer( GetOwner() );
	if (!pOwner)
		return;

	bool bFiredWeapon = false;

//#ifdef ARGG
	// adnan
	// this is where we check if we're orbiting the object
	
	// if we're holding something and pressing use,
	//  then set us in the orbiting state
	//  - this will indicate to OverrideMouseInput that we should zero the input and update our delta angles
	//  UPDATE: not anymore.  now this just sets our state variables.
	CBaseEntity *pObject = m_hObject;
	if( pObject ) {

		if (pOwner->m_nButtons & IN_USE) {
			m_gravCallback.m_bHasRotatedCarryAngles = true;

			m_bIsCurrentlyRotating = true;
		} else {
			// did we just let go of use?
			m_gravCallback.m_bHasRotatedCarryAngles = false;
			m_gravCallback.m_vecRotatedCarryAngles = pObject->GetAbsAngles();
		}
	}
	// end adnan
//#endif

	if (pOwner->m_nButtons & IN_ATTACK) {
		if ((pOwner->m_nButtons & IN_USE)) {
			pOwner->m_vecUseAngles = pOwner->pl.v_angle;
		} else {
			m_bIsCurrentlyRotating = false;
			m_bBlockPrimary = false;
			m_useDown = false;
		}
		if (pOwner->m_nButtons & IN_ATTACK2) {
			SecondaryAttack();
			bFiredWeapon = true;
#if physgun_audio == true
			SoundDestroy();
#endif
		}

		PrimaryAttack();
	}
	else 
	{
		if ( m_active )
		{
			EffectDestroy();
#if physgun_audio == true
			SoundDestroy();
#endif
		}
		Reload();
		return;
	}
	if ( pOwner->m_afButtonPressed & IN_RELOAD )
	{
		Reload();
	}

	if (bFiredWeapon)
	{
		Reload();
		m_flFreezeCooldownEnd = gpGlobals->curtime + 1.2;
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
bool CWeaponGravityGun::HasAnyAmmo( void )
{
	//Always report that we have ammo
	return true;
}

//=========================================================
//=========================================================
bool CWeaponGravityGun::Reload( void )
{
	return false;
}
