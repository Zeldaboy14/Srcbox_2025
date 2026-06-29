#include "cbase.h"
#include "core/coreHelpers.h"
#include "p3_particles_shared.h"
#include "debugoverlay_shared.h"
#include "effect_dispatch_data.h"
#include "igameevents.h"
#include "particle_parse.h"
#include "p3_shareddefs.h"

#ifndef CLIENT_DLL
#include "particle_system.h"
#endif

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

namespace p3
{

//-----------------------------------------------------------------------------
// ParticleDrop
//-----------------------------------------------------------------------------

ParticleDrop::ParticleDrop( const Vector& p, const Vector& v, float m, float d )
{
	pos = initPos = prevPos = p;
	initVel = v;
	mass = m;
	drag = d;

	time = 0;
}

void ParticleDrop::Simulate( float dt )
{
	time += dt;
	
	Vector gravity( 0, 0, METERS_TO_INCHES(-9.81f) * time );
	Vector vdrag( -initVel * drag );
	Vector velocity = ( initVel + gravity + vdrag ) * mass * time;

	prevPos = pos;
	pos = initPos + velocity;
}

//-----------------------------------------------------------------------------

bool ParticlesCheckCollision( const Vector& a, const Vector& b, trace_t& tr, CBaseEntity* exclude )
{
	if ( a.DistToSqr( b ) < 0.1f ) return false;
	//UTIL_TraceLine( a, b, MASK_SOLID_BRUSHONLY|CONTENTS_SOLID, 0, COLLISION_GROUP_NONE, &tr );
	UTIL_TraceLine( a, b, MASK_SHOT, exclude, COLLISION_GROUP_NONE, &tr );
	return ( tr.DidHit() && !( tr.surface.flags & SURF_SKY ) );
}

}	// namespace p3

//-----------------------------------------------------------------------------
// P3_ParticleSystem
//-----------------------------------------------------------------------------
P3_ParticleSystem::P3_ParticleSystem() :
	m_traceFilter( COLLISION_GROUP_INTERACTIVE_DEBRIS )
{
	m_particles			= NULL;
	m_maxCount			= 0;
	m_active			= 0;

	m_emitterEnabled	= false;
	m_emitterPosition	= vec3_origin;
	m_emitterDirection	= Vector(0,0,1);
	m_emitterMass		= 0.1f;
	m_emitterSpeed		= 1.0f;
	m_emitterDrag		= 0.1f;
	m_emitterLifetime	= 3.0f;

	m_emitterRate		= 0.2f;
	m_emitterRateAccum	= 0.0f;

	m_emitterDt			= 0.05f;
	m_emitterDtAccum	= 0.0f;
}

P3_ParticleSystem::~P3_ParticleSystem()
{
	delete[] m_particles;
}

void P3_ParticleSystem::Init( int maxCount )
{
	if ( m_particles == NULL || m_maxCount != maxCount )
	{
		delete[] m_particles;
		m_particles = new Particle[maxCount];
		m_maxCount = maxCount;
	}

	m_active = 0;
}

void P3_ParticleSystem::SetCollisionListener( CollisionListener* listener )
{
	m_collisionListener	= listener;
}

void P3_ParticleSystem::InitEmitter( const Vector& position, const Vector& direction )
{
	m_emitterPosition	= position;
	m_emitterDirection	= direction;
}

void P3_ParticleSystem::InitEmitterParams( float mass, float speed, float drag, float lifetime, float rate )
{
	m_emitterMass		= mass;
	m_emitterSpeed		= speed;
	m_emitterDrag		= drag;
	m_emitterLifetime	= lifetime;
	m_emitterRate		= rate;
}

void P3_ParticleSystem::EnableEmitter( bool enable )
{
	m_emitterEnabled = enable;

	if ( !enable )
	{
		m_emitterRateAccum = 0;
	}
}

void P3_ParticleSystem::SetSimulateInterval( float interval )
{
	m_emitterDt = interval;
}

void P3_ParticleSystem::Simulate( float dt )
{
	m_emitterDtAccum += dt;
	if ( m_emitterDtAccum > m_emitterDt )
	{
		UpdateLifetime();
		UpdateMovement( m_emitterDtAccum );
		UpdateEmitter( m_emitterDtAccum );
		UpdateCollisions();

		m_emitterDtAccum = 0;
	}
}

void P3_ParticleSystem::AddEntityToIgnore( CBaseEntity* ent )
{
	m_traceFilter.AddEntityToIgnore( ent );
}

int P3_ParticleSystem::Count() const
{
	return m_active;
}

const P3_ParticleSystem::Particle& P3_ParticleSystem::GetParticle( int index ) const
{
	Assert( index >= 0 && index < m_active );
	return m_particles[index];
}

void P3_ParticleSystem::DrawDebugOverlay()
{
	for ( int i = 0; i < m_active; i++ )
	{
		NDebugOverlay::Line( m_particles[i].prevPosition, m_particles[i].position, 255,255,255, true, 0 );
	}

	NDebugOverlay::Sphere( m_emitterPosition, 5, 255,255,255, false, 0.2f );
	NDebugOverlay::Line( m_emitterPosition, m_emitterPosition + 20*m_emitterDirection, 255,255,255, false, 0.2f );
}

int P3_ParticleSystem::AddParticle()
{
	if ( m_active != m_maxCount )
	{
		return m_active++;
	}
	else
	{
		return 0;
	}
}

void P3_ParticleSystem::RemoveParticle( int index )
{
	Assert( m_active );
	Assert( index >= 0 && index < m_active );

	m_active--;

	if ( index != m_active )
	{
		int bytes = sizeof( Particle ) * ( m_active - index );
		memcpy( &m_particles[index], &m_particles[index+1], bytes );
	}
}

void P3_ParticleSystem::UpdateLifetime()
{
	for ( int i = 0; i < m_active; /* пусто */ )
	{
		const Particle& particle = m_particles[i];
		if ( particle.time > particle.lifetime )
		{
			RemoveParticle( i );
		}
		else
		{
			i++;
		}
	}
}

void P3_ParticleSystem::UpdateMovement( float dt )
{
	for ( int i = 0; i < m_active; i++ )
	{
		UpdateParticleMovement( m_particles[i], dt );
	}
}

void P3_ParticleSystem::UpdateParticleMovement( Particle& particle, float dt )
{
	particle.time += dt;

	Vector gravity( 0, 0, METERS_TO_INCHES(-9.81f) * particle.time );
	Vector drag( -particle.initVelocity * m_emitterDrag );
	Vector velocity = ( particle.initVelocity + gravity + drag ) * m_emitterMass * particle.time;

	particle.prevPosition = particle.position;
	particle.position = particle.initPosition + velocity;		
}

void P3_ParticleSystem::UpdateEmitter( float dt )
{
	if ( !m_emitterEnabled )
	{
		m_emitterRateAccum = 0;
		return;
	}

	m_emitterRateAccum += dt;

	int numEmitt = int(m_emitterRateAccum/m_emitterRate);
	if ( numEmitt > 0 )
	{
		m_emitterRateAccum -= numEmitt*m_emitterRate;

		for ( int i = 0; i < numEmitt; i++ )
		{
			Particle& particle		= m_particles[AddParticle()];
			particle.initPosition	= m_emitterPosition;
			particle.initVelocity	= m_emitterDirection * m_emitterSpeed;
			particle.position		= particle.initPosition;
			particle.prevPosition	= particle.initPosition;
			particle.time			= 0;
			particle.lifetime		= m_emitterLifetime;

			if ( i != 0 )
			{
				float simDt = i * m_emitterRate/(numEmitt-1);
				UpdateParticleMovement( particle, simDt );
			}
		}
	}
}

void P3_ParticleSystem::UpdateCollisions()
{
	for ( int i = 0; i < m_active; /* пусто */ )
	{
		const Particle& particle = m_particles[i];
		trace_t tr;

		UTIL_TraceLine( particle.prevPosition, particle.position, MASK_SHOT, &m_traceFilter, &tr );
		if ( tr.DidHit() && !( tr.surface.flags & SURF_SKY ) )
		{
			if ( m_collisionListener )
			{
				m_collisionListener->OnParticleCollision( tr );
			}

			RemoveParticle( i );
		}
		else
		{
			i++;
		}
	}
}

//-----------------------------------------------------------------------------
// Функции
//-----------------------------------------------------------------------------

void P3_SelectCurrentBloodFlags( CBaseEntity* ent, int damageType )
{
	extern int g_nCurrentBloodFlags;

	if ( ent->BloodColor() == BLOOD_COLOR_RED )
	{
		switch ( damageType & ~DMG_REMOVENORAGDOLL )
		{
		case DMG_SLASH:
		case DMG_SHOVEL:
		case DMG_SHOVEL2:
		case DMG_NAILBAT:
			g_nCurrentBloodFlags |= EFFECT_FLAG_SLASH_HIT;
			break;

		case DMG_CLUB:
		case DMG_KNUCKLE:
		case DMG_BATON:
		case DMG_HAMMER:
			g_nCurrentBloodFlags |= EFFECT_FLAG_LIGHT_HIT;
			break;

		default:
			if ( damageType & DMG_BULLET )
			{
				g_nCurrentBloodFlags |= EFFECT_FLAG_FIREARM_HIT;
			}
			else if ( damageType & DMG_SLASH )
			{
				g_nCurrentBloodFlags |= EFFECT_FLAG_SLASH_HIT;
			}
			else if ( damageType & DMG_CLUB )
			{
				// melee
				g_nCurrentBloodFlags |= EFFECT_FLAG_LIGHT_HIT;
			}
			break;
		}

		if ( ent->IsPlayer() )
		{
			g_nCurrentBloodFlags |= EFFECT_FLAG_PLAYER_HIT;
		}
	}
	else
	{
		g_nCurrentBloodFlags = 0;
	}
}

void P3_StartClientEffect( CBaseEntity* ent, const char* effect, const char* attachment /*= NULL*/, CBaseEntity* target /*= NULL*/ )
{
	Assert( ent );
	Assert( effect );

	IGameEvent* event = gameeventmanager->CreateEvent( "start_effect" );
	if ( event )
	{
		event->SetString( "name", effect );
		event->SetInt( "sourceindex", ent->entindex() );
		event->SetString( "attachment", attachment );
		event->SetInt( "attach_type", attachment ? PATTACH_POINT_FOLLOW : PATTACH_ABSORIGIN_FOLLOW );
		event->SetInt( "targetindex", target ? target->entindex() : -1 );

		gameeventmanager->FireEvent( event );
	}
}

void P3_StopClientEffect( CBaseEntity* ent, const char* effect, bool force /*= false*/ )
{
	Assert( ent );
	Assert( effect );

	IGameEvent* event = gameeventmanager->CreateEvent( "stop_effect" );
	if ( event )
	{
		event->SetInt( "sourceindex", ent->entindex() );
		event->SetString( "name", effect );
		event->SetBool( "force", force );
		gameeventmanager->FireEvent( event );
	}
}

#ifndef CLIENT_DLL
CParticleSystem* P3_CreateServerEffect( CBaseAnimating* ent, const char* effect, int attachment, float lifetime )
{
	Vector pos;
	QAngle ang;

	ent->GetAttachment( attachment, pos, ang );
	CParticleSystem* pParticle = P3_CreateServerEffect( ent, effect, pos, ang, lifetime );
	if ( pParticle )
	{
		pParticle->SetParent( ent, attachment );
	}

	return pParticle;
}

CParticleSystem* P3_CreateServerEffect( CBaseAnimating* ent, const char* effect, const Vector& pos, const QAngle& ang, float lifetime )
{
	CParticleSystem* pParticle = (CParticleSystem*)CreateEntityByName( "info_particle_system" );
	if ( pParticle != NULL )
	{
		QAngle angles = ang;
		angles.x += 90;

		// Setup our basic parameters
		pParticle->KeyValue( "start_active", "1" );
		pParticle->KeyValue( "effect_name", effect );
		pParticle->SetAbsOrigin( pos );
		pParticle->SetAbsAngles( angles );
		DispatchSpawn( pParticle );
		if ( gpGlobals->curtime > 0.5f )
			pParticle->Activate();

		if ( lifetime > 0 )
		{
			pParticle->SetThink( &CBaseEntity::SUB_Remove );
			pParticle->SetNextThink( gpGlobals->curtime + lifetime );
		}
	}

	return pParticle;
}
#endif