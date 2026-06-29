#include "cbase.h"
#include "p3_ballistic_controller.h"
#include "basegrenade_shared.h"
#include "debugoverlay_shared.h"
#include "vprof.h"
#include "takedamageinfo.h"
#include "utils/p3_collisionblocks.h"

#ifndef CLIENT_DLL
#include "hl2mp/hl2mp_player.h"
#endif

static const Vector up(0.0,0.0,1.0);
static const float maxTime = 2.5f;
static const float maxThrowAngle = 60.0f;

ConVar p3_debug_bc( "p3_debug_bc", "0", FCVAR_CHEAT | FCVAR_REPLICATED, "Draw the trajectory, computed by the ballistic controller" );
//----------------------------------------------------------------------------------------
//
//					CP3_BallisticController
//
//----------------------------------------------------------------------------------------
CP3_BallisticController::CP3_BallisticController()
{
	Construct();
}
//----------------------------------------------------------------------------------------
CP3_BallisticController::CP3_BallisticController(const CP3_BallisticController &copy)
{
	Construct();
}
//----------------------------------------------------------------------------------------
void CP3_BallisticController::Construct()
{
#ifndef CLIENT_DLL
	static bool doOnce = true;

	if(doOnce)//dont draw the trajectory on the server
	{
		if( UTIL_IsMultiplayer() && !IsClient() )
			p3_debug_bc.SetValue(0);
	}
#endif

	ConVarRef sv_gravity("sv_gravity");
	SetGravity(sv_gravity.GetFloat());	
	m_ControlledEnt=0;
	m_TraceHull=false;
	m_ItemEnt=0;
	m_OwnerEnt=0;
	m_MaxSteps = 16;
	m_Trajectory.EnsureCapacity(m_MaxSteps);
	m_EstimatedTime = 0.0f;
	m_FlyTime = 0.0f;
	Vector	mins (-5,-5,-5);
	Vector	maxs ( 5, 5, 5);
	SetTraceHull(mins,maxs);
}
//----------------------------------------------------------------------------------------
void CP3_BallisticController::ComputePoint(float curTime, BcControlPoint& point)
{
	//integrate particle position and velocity
	point.m_Velocity = m_InitialVelocity + m_Gravity * curTime;
	point.m_Position = m_InitialPosition + m_InitialVelocity * curTime + 0.5 * m_Gravity * curTime * curTime;
	point.m_Time = curTime;
}
//----------------------------------------------------------------------------------------
void CP3_BallisticController::Init(const Vector& dir, float velocityMin, float velocityMax, const Vector& initPos)
{
	m_InitialPosition = initPos;

	ComputeThrowAngle(dir);
	//interpolate velocity based on the throw angle
	float velocity = velocityMin + (velocityMax - velocityMin) * fabs(m_ThrowAngle) / maxThrowAngle;
	m_InitialVelocity = dir * velocity;	
	ComputeFlyTime(velocity);
}
//----------------------------------------------------------------------------------------
void CP3_BallisticController::Init(const Vector& dir, float phaseAngle, float velocityMin, float velocityMax, const Vector& initPos)
{
	m_InitialPosition = initPos;

	Vector newDir;
	float angle = ComputeThrowAngle(dir,phaseAngle,newDir);
	float velocity = velocityMin + (velocityMax - velocityMin) * fabs(angle) / maxThrowAngle;
	m_InitialVelocity = newDir * velocity;	
	ComputeFlyTime(velocity);
}
//----------------------------------------------------------------------------------------
void CP3_BallisticController::Init(const Vector& initVelocity, const Vector& initPos)
{	
	m_InitialPosition = initPos;
	m_InitialVelocity = initVelocity;
	
	Vector velNorm = m_InitialVelocity;
	float velocity = velNorm.NormalizeInPlace();
	ComputeThrowAngle(velNorm);
	ComputeFlyTime(velocity);
}
//----------------------------------------------------------------------------------------
void CP3_BallisticController::ComputeThrowAngle(const Vector& dir)
{
	float dirDotUp = dir.Dot(up);
	float sign = dirDotUp >= 0.0f ? 1.0f : -1.0f; //take into account whether dir points up or down
	Vector velProj = dir - ( dir.Dot(up) ) * up;//dir projected on the ground plane
	velProj.NormalizeInPlace();
	m_ThrowAngle = sign * acosf( dir.Dot(velProj) );
}
//----------------------------------------------------------------------------------------
float CP3_BallisticController::ComputeThrowAngle(const Vector& dir, float phaseAngle, Vector& newDir)
{
	float dirDotUp = dir.Dot(up);
	float sign = dirDotUp >= 0.0f ? 1.0f : -1.0f;
	Vector velProj = dir - dirDotUp * up;	
	velProj.NormalizeInPlace();
	float origAngle = sign * acosf( dir.Dot(velProj) );//original throw angle

	phaseAngle = DEG2RAD(phaseAngle);

	if( fabs(origAngle) < phaseAngle )//if the throw angle is too small -> smoothstep interpolate the phase
	{
		float t = fabs(origAngle) / phaseAngle;
		phaseAngle = phaseAngle * t * t * (3 - 2 * t);
	}

	m_ThrowAngle = origAngle + sign * phaseAngle;
	newDir = velProj * cos(m_ThrowAngle) + up * sin(m_ThrowAngle);//compute phased throw direction
	return origAngle;
}
//----------------------------------------------------------------------------------------
void CP3_BallisticController::ComputeFlyTime(float velocity)
{
	//estimate fly time required to fall to the ground
	m_EstimatedTime = fabs( 4 * velocity * sin(m_ThrowAngle) / m_GravLength );
}
//----------------------------------------------------------------------------------------
void CP3_BallisticController::SetGravity(float gravity)
{
	m_Gravity = Vector(0.0f, 0.0f, -gravity);
	m_GravLength = gravity;
}
//----------------------------------------------------------------------------------------
void CP3_BallisticController::SetControlledEntity(CBaseEntity* ent)
{
	m_ControlledEnt = ent;
}
//----------------------------------------------------------------------------------------
CBaseEntity* CP3_BallisticController::GetControlledEntity()const
{
	return m_ControlledEnt;
}
//----------------------------------------------------------------------------------------
const Vector& CP3_BallisticController::GetInitialVelocity()const
{
	return m_InitialVelocity;
}
//----------------------------------------------------------------------------------------
const Vector& CP3_BallisticController::GetInitialPosition()const
{
	return m_InitialPosition;
}
//----------------------------------------------------------------------------------------
void CP3_BallisticController::SetTraceHull(const Vector& mins, const Vector& maxs)
{
	m_TraceHull = true;
	m_HullMins = mins;
	m_HullMaxs = maxs;
}
//----------------------------------------------------------------------------------------
float CP3_BallisticController::GetMaxDimension()
{
	if(m_TraceHull)
	{
		Vector diff = m_HullMaxs - m_HullMins;

		return max(max(diff.x, diff.y), diff.z);
	}
	else
		return 0.0f;
}
//----------------------------------------------------------------------------------------
void CP3_BallisticController::SetIgnoreEntities(CBaseEntity* owner, CBaseEntity* item)
{
	m_OwnerEnt = owner;
	m_ItemEnt = item;
}
//----------------------------------------------------------------------------------------
void CP3_BallisticController::DrawTrajectory()
{
	if(m_TrajectorySteps)
	{
		if( p3_debug_bc.GetBool() )
		{
			//draw intermediate points
			for(int i=0; i<m_TrajectorySteps; ++i)
			{
				NDebugOverlay::Sphere(m_Trajectory[i].m_Position,1.0,0,0,255,false,0.1f);
			}

			//draw last point			
			NDebugOverlay::Sphere(m_Trajectory[m_TrajectorySteps].m_Position,4.0,255,0,0,false,0.1f);

#ifdef CLIENT_DLL
		for(int i=0; i<m_TrajectorySteps; ++i)
			NDebugOverlay::Line( m_Trajectory[i].m_Position, m_Trajectory[i+1].m_Position, 255,255,255, false, 0 );
#endif
		}
	}
}
//----------------------------------------------------------------------------------------
void CP3_BallisticController::SetMaxSteps( int n )
{
	m_MaxSteps = n;
	m_Trajectory.EnsureCapacity( m_MaxSteps );
}
//----------------------------------------------------------------------------------------
void CP3_BallisticController::InitForPlayer( CBasePlayer* pPlayer )
{
	static const float velMin = 800;
	static const float velMax = 1200;
	static const float phase = 15.0f;	

	/*Vector	vForward, vRight, vForward2D;//forward vector is always zero on client
	pPlayer->EyeVectors( &vForward, &vRight, NULL );
	vForward2D.z = 0;
	vForward2D.NormalizeInPlace();

	if(IsClient())
		Msg("client fw %.3f %.3f rg %.3f %.3f %.3f\n", vForward2D.x, vForward2D.y, vRight.x, vRight.y, vRight.z);
	else
		Msg("server fw %.3f %.3f rg %.3f %.3f %.3f\n", vForward2D.x, vForward2D.y, vRight.x, vRight.y, vRight.z);*/

	Vector	vForward, vRight;
	pPlayer->EyeVectors( &vForward, &vRight, NULL );

	if ( pPlayer->GetActiveWeapon() && pPlayer->GetActiveWeapon()->IsEffectActive( EF_MIRROR_WEAPON ) )
		VectorNegate( vRight );

	Vector vecSrc = pPlayer->GetAbsOrigin() + Vector(0,0,64) + vRight*16 /*+ vForward2D*20*/;

	Init(vForward, phase, velMin, velMax, vecSrc);
}
//----------------------------------------------------------------------------------------
void CP3_BallisticController::Trace(const Vector& from, const Vector& to, trace_t* tr)
{
	class CSkipEntityAndOwned : public CTraceFilterSkipTwoEntities
	{
	public:
		CSkipEntityAndOwned( const IHandleEntity *passentity, const IHandleEntity *passentity2 ):
		  CTraceFilterSkipTwoEntities( passentity, passentity2, COLLISION_GROUP_NONE ) {}

		virtual bool ShouldHitEntity( IHandleEntity *pHandleEntity, int contentsMask )
		{
			if ( CTraceFilterSkipTwoEntities::ShouldHitEntity( pHandleEntity, contentsMask ) )
			{
				CBaseEntity* pEntity = EntityFromEntityHandle( pHandleEntity );
				CBaseEntity* pEntityOwner = pEntity->GetOwnerEntity();

				if ( !pEntityOwner || (pEntityOwner != GetPassEntity() && pEntityOwner != GetPassEntity2() ) )
				{
					return true;
				}
			}

			return false;
		}
	};

	CSkipEntityAndOwned filter( m_OwnerEnt, m_ItemEnt );
	//CTraceFilterSkipTwoEntities filter( m_OwnerEnt, m_ItemEnt, COLLISION_GROUP_NONE );
	if(m_TraceHull)
		UTIL_TraceHull(from, to, m_HullMins, m_HullMaxs, MASK_SHOT, &filter, tr);
	else
		UTIL_TraceLine(from, to, MASK_SHOT, &filter, tr);

	extern CP3_CollisionBlocks g_CollisionBlocks;
	g_CollisionBlocks.FuckingFuck( from, to, MASK_SHOT, &filter, tr );

}
//----------------------------------------------------------------------------------------
void CP3_BallisticController::PredictTrajectory()
{	
	float deltaTime = /*m_EstimatedTime*/maxTime / (m_MaxSteps - 1);
	float curTime = 0.0f;
	m_TrajectorySteps = 0;
	m_Trajectory.RemoveAll();

	memset( &m_HitTrace, 0, sizeof m_HitTrace );

	while(true)
	{
		Assert( m_Trajectory.Base() );

		//add new point if required
		if(m_Trajectory.Count() < m_TrajectorySteps + 1)
			m_Trajectory.AddToTail( BcControlPoint() );

		ComputePoint(curTime, m_Trajectory[m_TrajectorySteps]);		

		//trace the world
		if(m_TrajectorySteps > 0)
		{
			trace_t tr;

			Trace(m_Trajectory[m_TrajectorySteps-1].m_Position, m_Trajectory[m_TrajectorySteps].m_Position, &tr);

			if(tr.DidHit())
			{
				m_Trajectory[m_TrajectorySteps].m_Position = tr.endpos;
				memcpy( &m_HitTrace, &tr, sizeof m_HitTrace );

				if( p3_debug_bc.GetBool() )
				{
					NDebugOverlay::Sphere(m_Trajectory[m_TrajectorySteps].m_Position,4.0,255,0,0,false,0.1f);	
				}

				break;
			}
		}

		if( p3_debug_bc.GetBool() )
		{
			NDebugOverlay::Sphere(m_Trajectory[m_TrajectorySteps].m_Position,1.0,0,0,255,false,0.1f);
		}

		//stop predicting if we made too many steps
		if(m_TrajectorySteps < m_MaxSteps - 1)
		{
			m_TrajectorySteps++;
		}
		else
			break;

		curTime += deltaTime;		
	}	
}
//----------------------------------------------------------------------------------------
void CP3_BallisticController::Fire()
{
	m_CurrentWaypoint = 0;
	m_FlyTime = 0.0f;

	//snap the entity into the start position
	if(m_ControlledEnt && m_TrajectorySteps)
	{
		IPhysicsObject* pObj = m_ControlledEnt->VPhysicsGetObject();

		if(pObj)
		{
			pObj->SetVelocityInstantaneous(&m_Trajectory[m_CurrentWaypoint].m_Velocity,0);
			pObj->SetPosition(m_Trajectory[m_CurrentWaypoint].m_Position,QAngle(),true);
		}
	}	
}
//----------------------------------------------------------------------------------------
void CP3_BallisticController::Fly()
{	
	if(m_ControlledEnt)
	{
#ifndef CLIENT_DLL
		float deltaTime = gpGlobals->frametime;
		if(m_OwnerEnt && m_OwnerEnt->IsPlayer())
		{
			CP3_Player* player = P3_GetPlayer();

			if(player==m_OwnerEnt && player->IsSlowmoEnabled())
				deltaTime *= player->GetSlowmoMultiplier();
		}
		
		m_FlyTime += deltaTime;
#else
		m_FlyTime += gpGlobals->frametime;
#endif

		IBcControllable* cent = dynamic_cast<IBcControllable*>( m_ControlledEnt.Get() );

		if(!cent)
			return;

		if( !cent->CanBeControlled() )//grenade can disable the controller
			return;

		if( m_FlyTime > m_Trajectory[m_TrajectorySteps].m_Time )
		{
			cent->OnBcTimeout();
			m_ControlledEnt=0;
			return;
		}		

		DrawTrajectory();
		BcControlPoint point;
		ComputePoint(m_FlyTime,point);
		IPhysicsObject* pObj = m_ControlledEnt->VPhysicsGetObject();
		if(pObj)
		{
			Vector pos; QAngle ang;
			pObj->GetPosition(&pos, &ang);
			trace_t tr;
			Trace(pos, point.m_Position, &tr);

			if(tr.DidHit())//maybe should move grenade bouncing calculations here
			{
				//approximate contact plane normal based on the velocity because vphysics does not calculate them when tracing hulls
				Vector dir(point.m_Velocity);
				dir.NormalizeInPlace();
				tr.plane.normal = - dir;
				point.m_Position = tr.endpos - dir * GetMaxDimension() * 0.5f;
				//NDebugOverlay::Sphere(tr.endpos, 16, 255, 0, 0, true, 3.0);
				//NDebugOverlay::Line(tr.endpos, tr.endpos + dir * 10.0, 0, 255, 0, true, 3.0);
			}			
			
			if( m_ControlledEnt->IsNPC() )
			{
				m_ControlledEnt->SetAbsOrigin(point.m_Position);
				//m_ControlledEnt->SetAbsAngles(ang);
				m_ControlledEnt->SetAbsVelocity(point.m_Velocity);
			}
			else
			{
				pObj->SetPosition(point.m_Position,ang,true);
				pObj->SetVelocityInstantaneous(&point.m_Velocity,0);
			}			

			if(tr.DidHit())
			{	
				// попали в стекло и т.п.

				trace_t& trace = tr;

				// if its breakable glass and we kill it, don't bounce.
				// give some damage to the glass, and if it breaks, pass 
				// through it.
				bool breakthrough = false;

				if( trace.m_pEnt && FClassnameIs( trace.m_pEnt, "func_breakable" ) )
				{
					breakthrough = true;
				}

				if( trace.m_pEnt && FClassnameIs( trace.m_pEnt, "func_breakable_surf" ) )
				{
					breakthrough = true;
				}

				if (breakthrough)
				{
					CTakeDamageInfo info( m_ControlledEnt, m_ControlledEnt, 10, DMG_CLUB );
					trace.m_pEnt->DispatchTraceAttack( info, m_ControlledEnt->GetAbsVelocity(), &trace );
				}
				else
				{
					cent->OnBcHit(tr);
					m_ControlledEnt=0;
				}
			}
		}

	}
}
//----------------------------------------------------------------------------------------
float CP3_BallisticController::GetThrowAngle()
{
	return m_ThrowAngle;
}
//----------------------------------------------------------------------------------------
bool CP3_BallisticController::IsClient()
{
	return gpGlobals->IsClient();
}
//----------------------------------------------------------------------------------------
//
//					CP3_BallisticControllerCache
//
//----------------------------------------------------------------------------------------
static CP3_BallisticControllerCache* g_pBallisticControllerCache = &CP3_BallisticControllerCache::Instance();
//----------------------------------------------------------------------------------------
CP3_BallisticControllerCache::CP3_BallisticControllerCache()
	:
CAutoGameSystemPerFrame("ballistic_controller")
{

}
//----------------------------------------------------------------------------------------
CP3_BallisticController* CP3_BallisticControllerCache::GetFreeController(CP3_BallisticController* prev)
{
	if(prev && !prev->GetControlledEntity())
		return prev;

	for(int i = 0; i < m_Controllers.Count(); ++i)
	{
		CP3_BallisticController* cnt = &m_Controllers[i];
		if( !cnt->GetControlledEntity() )
			return cnt;
	}

	m_Controllers.AddToTail( CP3_BallisticController() );
	return &m_Controllers.Tail();
}
//----------------------------------------------------------------------------------------
CP3_BallisticControllerCache&	CP3_BallisticControllerCache::Instance()
{
	static CP3_BallisticControllerCache cache;
	return cache;
}
//----------------------------------------------------------------------------------------
void CP3_BallisticControllerCache::FrameUpdatePreEntityThink()
{
	FOR_EACH_VEC(m_Controllers,i)
	{
		if(m_Controllers[i].GetControlledEntity())
			m_Controllers[i].Fly();
	}
}