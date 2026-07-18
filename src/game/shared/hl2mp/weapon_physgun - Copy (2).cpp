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
#include "iviewrender.h"
#include "ragdoll.h"
#else
#include "physics_prop_ragdoll.h"
#endif
#include "IEffects.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

ConVar physcannon_rotate("physcannon_rotate", "1");

static int g_physgunBeam1;
static int g_physgunBeam;
static int g_physgunGlow;
#define PHYSGUN_BEAM_SPRITE1	"sprites/physbeam1.vmt"
#define PHYSGUN_BEAM_SPRITE		"sprites/physbeam.vmt"
#define PHYSGUN_BEAM_GLOW		"sprites/physglow.vmt"

#define	PHYSGUN_SKIN	1

#define gmod_physgun_lock true // Lock the model as per Gmod
#define physgun_audio false // Audio from the beta

class CWeaponGravityGun;

#ifdef CLIENT_DLL
#define PHYSGUN_GLOW_SPRITE "sprites/glow04_noz"
#define PHYSGUN_ENDCAP_SPRITE "sprites/orangeflare1"
#define PHYSGUN_CENTER_GLOW "sprites/orangecore1"
#define PHYSGUN_BLAST_SPRITE "sprites/orangecore2"

CLIENTEFFECT_REGISTER_BEGIN( PrecacheEffectGravityGun )
CLIENTEFFECT_MATERIAL( "sprites/physbeam1" )
CLIENTEFFECT_MATERIAL( "sprites/physbeam" )
CLIENTEFFECT_MATERIAL( "sprites/physglow" )
CLIENTEFFECT_MATERIAL(PHYSGUN_GLOW_SPRITE)
CLIENTEFFECT_MATERIAL(PHYSGUN_ENDCAP_SPRITE)
CLIENTEFFECT_MATERIAL(PHYSGUN_CENTER_GLOW)
CLIENTEFFECT_MATERIAL(PHYSGUN_BLAST_SPRITE)
CLIENTEFFECT_REGISTER_END()


#endif

IPhysicsObject *GetPhysObjFromPhysicsBone( CBaseEntity *pEntity, short physicsbone )
{
	if( pEntity->IsNPC() )
	{
		return pEntity->VPhysicsGetObject();
	}

	CBaseAnimating *pModel = static_cast< CBaseAnimating * >( pEntity );
	if (pModel != NULL)
	{
		IPhysicsObject	*pPhysicsObject = NULL;
		
		//Find the real object we hit.
		if( physicsbone >= 0 )
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
	void AttachEntityOld( CBaseEntity* pEntity, IPhysicsObject* pPhys, const Vector& position );
	void DetachEntity( void );

	bool UpdateObject( CBasePlayer *pPlayer, CBaseEntity *pEntity, float* x, float* y );

	void SetTargetPosition( const Vector &target, const QAngle &targetOrientation )
	{
		m_shadow.targetPosition = target;
		m_shadow.targetRotation = targetOrientation;

		m_timeToArrive = gpGlobals->frametime;

		CBaseEntity *pAttached = m_attachedEntity;
		if ( pAttached )
		{
			IPhysicsObject *pObj = GetPhysObjFromPhysicsBone( pAttached, m_attachedPhysicsBone );
			
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
	float			m_saveMass;
	float			m_maxAcceleration;
	Vector			m_maxAngularAcceleration;
	EHANDLE			m_attachedEntity;
	short			m_attachedPhysicsBone;
	QAngle			m_targetRotation;
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
	DEFINE_FIELD( m_attachedPhysicsBone,	FIELD_SHORT ),
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

void CGravControllerPoint::AttachEntityOld(CBaseEntity* pEntity, IPhysicsObject* pPhys, const Vector& position)
{
	pPhys->EnableMotion(true);
	m_attachedEntity = pEntity;
	pPhys->WorldToLocal(&m_localPosition, position);
	m_worldPosition = position;
	pPhys->GetDamping(NULL, &m_saveDamping);
	float damping = 2;
	pPhys->SetDamping(NULL, &damping);
	m_controller = physenv->CreateMotionController(this);
	m_controller->AttachObject(pPhys, true);
	m_controller->SetPriority(IPhysicsMotionController::HIGH_PRIORITY);
	//SetTargetPosition(position);
	QAngle angles;
	SetTargetPosition( position, angles );
	//m_maxAcceleration = phys_gunforce.GetFloat() * pPhys->GetInvMass() * 190;
	pPhys->GetPosition(NULL, &m_targetRotation);
	//float torque = phys_guntorque.GetFloat();
	//m_maxAngularAcceleration = torque * pPhys->GetInvInertia();
	m_vecRotatedCarryAngles = m_targetRotation;
}

void CGravControllerPoint::AttachEntity( CBasePlayer *pPlayer, CBaseEntity *pEntity, IPhysicsObject *pPhys, short physicsbone, const Vector &vGrabPosition )
{
	m_attachedEntity = pEntity;
	m_attachedPhysicsBone = physicsbone;
	pPhys->WorldToLocal( &m_localPosition, vGrabPosition );
	m_worldPosition = vGrabPosition;
	pPhys->GetDamping( NULL, &m_saveDamping );
	m_saveMass = pPhys->GetMass();
	float damping = 2;
	pPhys->SetDamping( NULL, &damping );
	pPhys->SetMass( 50000 );
	m_controller = physenv->CreateMotionController( this );
	m_controller->AttachObject( pPhys, true );
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
			pPhys->SetDamping( NULL, &m_saveDamping );
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
/*	hlshadowcontrol_params_t shadowParams = m_shadow;
#ifndef CLIENT_DLL
	m_timeToArrive = pObject->ComputeShadowControl( shadowParams, m_timeToArrive, deltaTime );
#else
	m_timeToArrive = pObject->ComputeShadowControl( shadowParams, (TICK_INTERVAL*2), deltaTime );
#endif
	
	linear.Init();
	angular.Init();

	return SIM_LOCAL_ACCELERATION;*/

	Vector vel;
	QAngle ang;
	pObject->GetVelocity(&vel, NULL);

	physgun_shadowcontrol_params_t shadowParams = m_shadow;
	shadowParams.maxAngular = m_shadow.maxAngular;
	//shadowParams.targetPosition = m_targetPosition; // tries to force object to world origin
	//shadowParams.targetRotation = m_targetRotation; // snaps object towards player (not good)
#ifndef CLIENT_DLL
	m_timeToArrive = pObject->ComputeShadowControl(shadowParams, m_timeToArrive_Shadow, deltaTime);
#else
	m_timeToArrive = pObject->ComputeShadowControl(shadowParams, (TICK_INTERVAL * 2), deltaTime);
#endif
	pObject->Wake();
	// Slide along the current contact points to fix bouncing problems
	Vector velocity;
	AngularImpulse angVel;

	vel.Init();
	float fracRemainingSimTime = 1.0;
	if (m_timeToArrive > 0)
	{
		fracRemainingSimTime *= deltaTime / m_timeToArrive;
		if (fracRemainingSimTime > 1)
		{
			fracRemainingSimTime = 1;
		}
	}

	m_timeToArrive -= deltaTime;
	if (m_timeToArrive < 0)
	{
		m_timeToArrive = 0;
	}

	float invDeltaTime = (1.0f / deltaTime);
	Vector world;
	pObject->LocalToWorld(&world, m_localPosition);
	m_worldPosition = world;
	pObject->GetVelocity(&vel, &angVel);
	pObject->GetVelocityAtPoint( world, &vel );
	float damping = 1.0;
	world += vel * deltaTime * damping;
	Vector delta = (m_targetPosition - world) * fracRemainingSimTime * invDeltaTime;
	Vector alignDir;
	linear = vec3_origin;
	angular = vec3_origin;

	if (m_align)
	{
		QAngle angles;
		Vector origin;
		Vector axis;
		AngularImpulse torque;

		//pObject->GetShadowPosition(&origin, &angles);
		// align local normal to target normal
		VMatrix tmp = SetupMatrixOrgAngles(origin, angles);
		Vector worldNormal = tmp.VMul3x3(m_localAlignNormal);
		axis = CrossProduct(worldNormal, m_targetAlignNormal);
		float trig = VectorNormalize(axis);
		float alignRotation = RAD2DEG(asin(trig));
		axis *= alignRotation;
		if (alignRotation < 10)
		{
			float dot = DotProduct(worldNormal, m_targetAlignNormal);
			// probably 180 degrees off
			if (dot < 0)
			{
				if (worldNormal.x < 0.5)
				{
					axis.Init(10, 0, 0);
				}
				else
				{
					axis.Init(0, 0, 10);
				}
				alignRotation = 10;
			}
		}

		// Solve for the rotation around the target normal (at the local align pos) that will 
		// move the grabbed spot to the destination.
		Vector worldRotCenter = tmp.VMul4x3(m_localAlignPosition);
		Vector rotSrc = world - worldRotCenter;
		Vector rotDest = m_targetPosition - worldRotCenter;

		// Get a basis in the plane perpendicular to m_targetAlignNormal
		Vector srcN = rotSrc;
		VectorNormalize(srcN);
		Vector tangent = CrossProduct(srcN, m_targetAlignNormal);
		float len = VectorNormalize(tangent);

		// needs at least ~5 degrees, or forget rotation (0.08 ~= sin(5))
		if (len > 0.08)
		{
			Vector binormal = CrossProduct(m_targetAlignNormal, tangent);

			// Now project the src & dest positions into that plane
			Vector planeSrc(DotProduct(rotSrc, tangent), DotProduct(rotSrc, binormal), 0);
			Vector planeDest(DotProduct(rotDest, tangent), DotProduct(rotDest, binormal), 0);

			float rotRadius = VectorNormalize(planeSrc);
			float destRadius = VectorNormalize(planeDest);
			if (rotRadius > 0.1)
			{
				if (destRadius < rotRadius)
				{
					destRadius = rotRadius;
				}
				//float ratio = rotRadius / destRadius;
				float angleSrc = atan2(planeSrc.y, planeSrc.x);
				float angleDest = atan2(planeDest.y, planeDest.x);
				float angleDiff = angleDest - angleSrc;
				angleDiff = RAD2DEG(angleDiff);
				axis += m_targetAlignNormal * angleDiff;
				world = m_targetPosition;// + rotDest * (1-ratio);
				//				NDebugOverlay::Line( worldRotCenter, worldRotCenter-m_targetAlignNormal*50, 255, 0, 0, false, 0.1 );
				//				NDebugOverlay::Line( worldRotCenter, worldRotCenter+tangent*50, 0, 255, 0, false, 0.1 );
				//				NDebugOverlay::Line( worldRotCenter, worldRotCenter+binormal*50, 0, 0, 255, false, 0.1 );
			}
		}

#ifndef CLIENT_DLL
		torque = WorldToLocalRotation(tmp, axis, 1);
#endif
		torque *= fracRemainingSimTime * invDeltaTime;
		torque -= angVel * 1.0;	 // damping
		for (int i = 0; i < 3; i++)
		{
			if (torque[i] > 0)
			{
				if (torque[i] > m_maxAngularAcceleration[i])
					torque[i] = m_maxAngularAcceleration[i];
			}
			else
			{
				if (torque[i] < -m_maxAngularAcceleration[i])
					torque[i] = -m_maxAngularAcceleration[i];
			}
		}
		torque *= invDeltaTime;
		angular += torque;
		// Calculate an acceleration that pulls the object toward the constraint
		// When you're out of alignment, don't pull very hard
		float factor = fabsf(alignRotation);
		if (factor < 5)
		{
			factor = clamp(factor, 0, 5) * (1 / 5);
			alignDir = m_targetAlignPosition - worldRotCenter;
			// Limit movement to the part along m_targetAlignNormal if worldRotCenter is on the backside of 
			// of the target plane (one inch epsilon)!
			float planeForward = DotProduct(alignDir, m_targetAlignNormal);
			if (planeForward > 1)
			{
				alignDir = m_targetAlignNormal * planeForward;
			}
			Vector accel = alignDir * invDeltaTime * fracRemainingSimTime * (1 - factor) * 0.20 * invDeltaTime;
			float mag = accel.Length();
			if (mag > m_maxAcceleration)
			{
				//accel *= (m_maxAcceleration/mag);
			}
			linear += accel;
		}
		linear -= vel * damping * invDeltaTime;
		// UNDONE: Factor in the change in worldRotCenter due to applied torque!
	}
	else
	{
		// clamp future velocity to max speed
		Vector nextVel = delta + vel;
		//float nextSpeed = nextVel.Length();

		delta *= invDeltaTime;

		float linearAccel = delta.Length();
		if (linearAccel > m_maxAcceleration)
		{
			//delta *= m_maxAcceleration / linearAccel;
		}

		Vector accel;
		AngularImpulse angAccel;

		//for (int i = 0; i < 2; i++)
		//{
		//	angAccel[i] = angles[i] - m_targetRotation[i];
		//}
		pObject->CalculateForceOffset(delta, world, &accel, new AngularImpulse(0, 0, 0));
		//linear += accel; // object jitters on the ground when the physgun grabs it, not good
		//angular += angAccel; // tries to force object to world origin
	}
	return SIM_GLOBAL_ACCELERATION;
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

#ifdef CLIENT_DLL
	enum EffectType_t
	{
		PHYSGUN_CORE = 0,

		PHYSGUN_BLAST,

		NUM_PHYSCANNON_PARAMETERS	// Must be last!
	};
#endif

	void DrawEffects();
	void DrawEffectSprite(EffectType_t effectID);
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

	void AttachObject( CBaseEntity *pEdict, IPhysicsObject *pPhysics, short physicsbone, const Vector& start, const Vector &end, float distance );
	void AttachObjectOld( CBaseEntity *pEdict, const Vector& start, const Vector &end, float distance );
	void UpdateObject( void );
	void DetachObject( void );

	void TraceLine( trace_t *ptr );

	void EffectCreate( void );
	void EffectUpdate( void );
	void EffectDestroy( void );

	QAngle m_vecRotatedCarryAngles;

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
	int			oldCollisionGroup;
	CNetworkVector	( m_targetPosition );
	CNetworkVector	( m_worldPosition );

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
	IPhysicsObject *pPhysics = GetPhysObjFromPhysicsBone( pEntity, m_attachedPhysicsBone );
	if ( !pEntity || !pPhysics )
	{
		return false;
	}
//#ifdef ARGG
	// adnan
	// if we've been rotating it, set it to its proper new angles (change m_attachedAnglesPlayerSpace while modifier)
	//Pickup_GetRotatedCarryAngles( pEntity, pPlayer, pPlayer->EntityToWorldTransform(), angles );
	// added the ... && (mousedx | mousedy) so we dont have to calculate if no mouse movement
	// UPDATE: m_vecRotatedCarryAngles has become a temp variable... can be cleaned up by using actual temp vars
#ifdef CLIENT_DLL
	if( m_bHasRotatedCarryAngles && (pPlayer->m_pCurrentCommand->mousedx || pPlayer->m_pCurrentCommand->mousedy) )
#else
	if( m_bHasRotatedCarryAngles && (pPlayer->GetCurrentCommand()->mousedx || pPlayer->GetCurrentCommand()->mousedy) )
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
		m_vecRotatedCarryAngles[ROLL] = 0;
		MatrixFromAngles( m_vecRotatedCarryAngles, vDeltaRotation );

		MatrixMultiply(vDeltaRotation, vCurrentRotation, vNewRotation);
		MatrixToAngles( vNewRotation, m_targetRotation );

	}
	// end adnan
//#endif

	SetTargetPosition( m_targetPosition, m_targetRotation );

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

	g_physgunBeam1 = PrecacheModel(PHYSGUN_BEAM_SPRITE1);
	g_physgunBeam = PrecacheModel(PHYSGUN_BEAM_SPRITE);
	g_physgunGlow = PrecacheModel(PHYSGUN_BEAM_GLOW);

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

	UTIL_TraceLine(start, end, MASK_SHOT, pOwner, COLLISION_GROUP_NONE, &tr);

	if ( m_hObject == NULL && tr.DidHitNonWorldEntity() )
	{
		CBaseEntity *pEntity = tr.m_pEnt;
		//AttachObject( pEntity, GetPhysObjFromPhysicsBone( pEntity, tr.physicsbone ), tr.physicsbone, start, tr.endpos, distance );
		// inform the object what was hit
		#ifndef CLIENT_DLL
		ClearMultiDamage();
		pEntity->DispatchTraceAttack(CTakeDamageInfo(pOwner, pOwner, 0, DMG_PHYSGUN), forward, &tr);
		ApplyMultiDamage();
		#endif
		//oldCollisionGroup = pEntity->GetCollisionGroup();
		AttachObject(pEntity, GetPhysObjFromPhysicsBone(pEntity, tr.physicsbone), tr.physicsbone, start, tr.endpos, distance);
		//AttachObjectOld(pEntity, start, tr.endpos, distance);
		m_lastYaw = pOwner->EyeAngles().y;
	}

	// Add the incremental player yaw to the target transform
	QAngle angles = m_gravCallback.TransformAnglesFromPlayerSpace( m_gravCallback.m_targetRotation, pOwner );

	CBaseEntity* pObject = m_hObject;

#ifdef CLIENT_DLL
	C_BaseEntity* heldObject = m_hObject.Get();
	float* x = new float(0.001f);
	float* y = new float(0.001f);
#endif

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

		float speed = pOwner->MaxSpeed();
		float rightSpeed = 1.0f;

		if (m_useDown)
		{
#ifndef CLIENT_DLL
			pOwner->SetPhysicsFlag(PFLAG_DIROVERRIDE, true);
#endif
			if (pOwner->m_nButtons & IN_FORWARD)
			{
				m_distance = Approach( 1024, m_distance, gpGlobals->frametime * 100 );
				//m_distance = Approach( 1024, m_distance, m_distance * 0.1);
			}
			if ( pOwner->m_nButtons & IN_BACK )
			{
				m_distance = Approach( 40, m_distance, gpGlobals->frametime * 100 );
				//m_distance = Approach( 40, m_distance, m_distance * 0.1);
			}
			if (pOwner->m_nButtons & IN_LEFT)
			{
				m_rotation = Approach( 1024, m_rotation, m_rotation * 0.1 );
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
			//m_distance = Approach( 1024, m_distance, m_distance * 0.1 );
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

		IPhysicsObject *pPhys = GetPhysObjFromPhysicsBone( pObject, m_physicsBone );
		//IPhysicsObject* pPhys = pEntity->VPhysicsGetObject();
		if (pPhys)
		{
			if ( pPhys->IsAsleep() )
			{
				// on the odd chance that it's gone to sleep while under anti-gravity
				pPhys->Wake();
			}

			Vector newPosition = start + forward * m_distance;
			Vector offset;
			pPhys->LocalToWorld( &offset, m_worldPosition );
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
			
			float fade = UTIL_LineFraction( pPhys->GetMass(), 50, 500, 1.0 );

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
	}
}

void CWeaponGravityGun::AttachObjectOld(CBaseEntity* pObject, const Vector& start, const Vector& end, float distance)
{
	m_hObject = pObject;
	IPhysicsObject* pPhysics = pObject ? (pObject->VPhysicsGetObject()) : NULL;
	if (pPhysics && pObject->GetMoveType() == MOVETYPE_VPHYSICS)
	{
		m_distance = distance;

		m_gravCallback.AttachEntityOld(pObject, pPhysics, end);
		QAngle angles;
		pPhysics->GetPosition(NULL, &angles);
		CBasePlayer* pOwner = ToBasePlayer(GetOwner());
		if (pOwner)
		{
			matrix3x4_t test;
			QAngle angleTest = pOwner->EyeAngles();
			angleTest.x = 0;
			AngleMatrix(angleTest, test);
			m_vecRotatedCarryAngles = TransformAnglesToLocalSpace(angles, test);
		}
		float mass = pPhysics->GetMass();
		Msg("Object mass: %.2f lbs (%.2f kg)\n", kg2lbs(mass), mass);
		//m_gravCallback.SetMaxVelocity( vel * 12312 );
		//		Msg( "Object mass: %.2f lbs (%.2f kg) %f %f %f\n", kg2lbs(mass), mass, pObject->GetAbsOrigin().x, pObject->GetAbsOrigin().y, pObject->GetAbsOrigin().z );
		//		Msg( "ANG: %f %f %f\n", pObject->GetAbsAngles().x, pObject->GetAbsAngles().y, pObject->GetAbsAngles().z );

		m_originalObjectPosition = pObject->GetAbsOrigin();

		pPhysics->Wake();

#ifndef CLIENT_DLL
		if (pOwner)
		{
			Pickup_OnPhysGunPickup(pObject, pOwner);
		}
#endif
	}
	else
	{
		m_hObject = NULL;
	}
}

void CWeaponGravityGun::AttachObject( CBaseEntity *pObject, IPhysicsObject *pPhysics, short physicsbone, const Vector& start, const Vector &end, float distance )
{
	CBasePlayer *pOwner = ToBasePlayer( GetOwner() );
	if( !pOwner )
		return;
	m_hObject = pObject;

	if ( pPhysics && pObject->GetMoveType() == MOVETYPE_VPHYSICS )
	{
		if (gmod_physgun_lock == true){
			pPhysics->EnableMotion(true);
		}
		m_distance = distance;

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
	}*/
}

//=========================================================
//=========================================================
void CWeaponGravityGun::PrimaryAttack( void )
{
	trace_t tr;
	TraceLine(&tr);
	CBasePlayer* pOwner = ToBasePlayer(GetOwner());
	if (!pOwner)
		return;

	if (m_bBlockPrimary) {
		return;
	}

	// Are we capable of firing again?
	if (m_flNextPrimaryAttack > gpGlobals->curtime)
		return;

	if (!m_active)
	{
		if (pOwner->m_afButtonPressed & IN_ATTACK || tr.DidHitNonWorldEntity()){
			SendWeaponAnim(ACT_VM_PRIMARYATTACK);
		} 
		else 
		{
			Reload();
		}
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
			IPhysicsObject* phys = m_hObject->VPhysicsGetObject();
			if (phys) {
				phys->EnableMotion(false);
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
// Purpose: Draws the effect sprite, given an effect parameter ID
//-----------------------------------------------------------------------------
void CWeaponGravityGun::DrawEffectSprite(EffectType_t effectID)
{
	color32 color;
	float scale;
	IMaterial* pMaterial;
	Vector	vecAttachment;

	// Don't draw invisible effects
	//if (IsEffectVisible(effectID) == false)
	//	return;

	// Get all of our parameters
	//GetEffectParameters(effectID, color, scale, &pMaterial, vecAttachment);

	// Msg( "Scale: %.2f\tAlpha: %.2f\n", scale, alpha );

	// Don't render fully translucent objects
	if (color.a <= 0.0f)
		return;

	// Draw the sprite
	CMatRenderContextPtr pRenderContext(materials);
	pRenderContext->Bind(pMaterial, this);
	DrawSprite(vecAttachment, scale, scale, color);
}

//-----------------------------------------------------------------------------
// Purpose: Render the beams and sprites for the physgun (both view and world models)
//-----------------------------------------------------------------------------
void CWeaponGravityGun::DrawEffects()
{
		if (!m_active)
			return;

		// Render our effects
		C_BasePlayer *pOwner = ToBasePlayer( GetOwner() );

		if ( !pOwner )
			return;

		Vector points[3];
		QAngle tmpAngle;

		C_BaseEntity *pObject = m_hObject;
		//if ( pObject == NULL )
		//	return;

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
		if ( pObject == NULL )
		{
			//points[2] = m_targetPosition;
			trace_t tr;
			TraceLine( &tr );
			points[2] = tr.endpos;
		}
		else
		{
			pObject->EntityToWorldSpace(m_worldPosition, &points[2]);
		}

		Vector forward, right, up;
		QAngle playerAngles = pOwner->EyeAngles();
		AngleVectors( playerAngles, &forward, &right, &up );
		if (pObject == NULL)
		{
			Vector vecDir = points[2] - points[0];
			VectorNormalize(vecDir);
			points[1] = points[0] + 0.5f * (vecDir * points[2].DistTo(points[0]));
		}
		else
		{
			Vector vecSrc = pOwner->Weapon_ShootPosition();
			points[1] = vecSrc + 0.5f * (forward * points[2].DistTo(points[0]));
		}

		//IMaterial *pMat = materials->FindMaterial( "sprites/physbeam1", TEXTURE_GROUP_CLIENT_EFFECTS );
		//Gmod 10 uses this specifically. HL2:SB had some vmt pointing elsewhere. It was stupid. Lets just use stock HL2 assets!
		IMaterial *pMat = materials->FindMaterial( "sprites/strider_bluebeam", TEXTURE_GROUP_CLIENT_EFFECTS );
		if ( pObject )
			pMat = materials->FindMaterial( "sprites/physbeam", TEXTURE_GROUP_CLIENT_EFFECTS );
		Vector color;
		color.Init(1,1,1);

		// Now draw it.
		CViewSetup beamView = *view->GetPlayerViewSetup();

		Frustum dummyFrustum;
		render->Push3DView( beamView, 0, NULL, dummyFrustum );

		float scrollOffset = gpGlobals->curtime * 2 - (int)gpGlobals->curtime * 2;
		CMatRenderContextPtr pRenderContext( materials );
		pRenderContext->Bind( pMat );
	#if 1
		// HACK HACK:  Munge the depth range to prevent view model from poking into walls, etc.
		// Force clipped down range
		pRenderContext->DepthRange( 0.1f, 0.2f );
	#endif
		DrawBeamQuadratic( points[0], points[1], points[2], pObject ? 13/3.0f : 13/3.0f, color, scrollOffset );
		DrawBeamQuadratic( points[0], points[1], points[2], pObject ? 13/3.0f : 13/3.0f, color, -scrollOffset );

		//Draw the beams endpoint!!!!

		//IMaterial *pMaterial = materials->FindMaterial( "sprites/physglow", TEXTURE_GROUP_CLIENT_EFFECTS );
		// Another change from HL2:SB to Gmod 10. Additives are better then a full blue ball!
		// Gmod 10 also seems to have a physg_glow2. Seems to be for the beam end effect (and for mimicing the orb from the physcannon)
		IMaterial *pMaterial = materials->FindMaterial( "sprites/physg_glow1", TEXTURE_GROUP_CLIENT_EFFECTS );

		color32 clr={128,128,240,255};
		/*if (pObject)
		{
			clr.r = 186;
			clr.g = 253;
			clr.b = 247;
			clr.a = 255;
		}*/

		// Guessed Gmod 10 factor
		// PObject ? holding object then not holding anything
		float scale = random->RandomFloat( 1, 4.5 ) * ( pObject ? 6 : 4 );

		// Draw the sprite
		pRenderContext->Bind( pMaterial );
		for ( int i = 0; i < 3; i++ )
		{
			DrawSprite( points[2], scale, scale, clr );
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
		m_flNextPrimaryAttack = gpGlobals->curtime + 0.6;
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
