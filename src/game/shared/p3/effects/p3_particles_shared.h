#ifndef P3_PARTICLES_SHARED
#define P3_PARTICLES_SHARED
#ifdef _WIN32
#pragma once
#endif

namespace p3
{
	class ParticleDrop
	{
	public:
		ParticleDrop( const Vector& p, const Vector& v, float m, float d );

		void Simulate( float dt );

		Vector	initPos, initVel;
		Vector	pos, prevPos;
		float	mass, drag;

		float	time;
	};

	bool ParticlesCheckCollision( const Vector& a, const Vector& b, trace_t& tr, CBaseEntity* exclude = 0 );

}		// namespace p3

//-----------------------------------------------------------------------------
// P3_ParticleSystem
//-----------------------------------------------------------------------------

class P3_ParticleSystem
{
public:
	struct Particle
	{
		Vector	initPosition;
		Vector	initVelocity;

		Vector	position;
		Vector	prevPosition;

		float	time;
		float	lifetime;
	};

	abstract_class CollisionListener
	{
	public:
		virtual void OnParticleCollision( trace_t& tr ) = 0;
	};

	P3_ParticleSystem();
	~P3_ParticleSystem();

	void			Init( int maxCount );
	void			SetCollisionListener( CollisionListener* listener );

	void			InitEmitter( const Vector& position, const Vector& direction );
	void			InitEmitterParams( float mass, float speed, float drag, float lifetime, float rate );
	void			EnableEmitter( bool enable );

	void			SetSimulateInterval( float interval );
	void			Simulate( float dt );

	void			AddEntityToIgnore( CBaseEntity* ent );

	int				Count() const;
	const Particle&	GetParticle( int index ) const;

	void			DrawDebugOverlay();

private:
	int				AddParticle();
	void			RemoveParticle( int index );

	void			UpdateLifetime();
	void			UpdateMovement( float dt );
	void			UpdateParticleMovement( Particle& particle, float dt );
	void			UpdateEmitter( float dt );
	void			UpdateCollisions();

	Particle*		m_particles;
	int				m_maxCount;
	int				m_active;

	bool			m_emitterEnabled;

	float			m_emitterRate;
	float			m_emitterRateAccum;

	float			m_emitterDt;
	float			m_emitterDtAccum;

	Vector			m_emitterPosition;
	Vector			m_emitterDirection;
	float			m_emitterMass;
	float			m_emitterSpeed;
	float			m_emitterDrag;
	float			m_emitterLifetime;

	CollisionListener* m_collisionListener;
	CTraceFilterSimpleList m_traceFilter;
};

//-----------------------------------------------------------------------------
// Функции
//-----------------------------------------------------------------------------

void P3_SelectCurrentBloodFlags( CBaseEntity* ent, int damageType );

void P3_StartClientEffect( CBaseEntity* ent, const char* effect, const char* attachment = NULL, CBaseEntity* target = NULL );
void P3_StopClientEffect( CBaseEntity* ent, const char* effect, bool force = false );

#ifndef CLIENT_DLL
class CParticleSystem;
CParticleSystem* P3_CreateServerEffect( CBaseAnimating* ent, const char* effect, int attachment, float lifetime );
CParticleSystem* P3_CreateServerEffect( CBaseAnimating* ent, const char* effect, const Vector& pos, const QAngle& ang, float lifetime );
#endif

#endif	// P3_PARTICLES_SHARED