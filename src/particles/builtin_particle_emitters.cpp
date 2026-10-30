//===== Copyright (c) Valve Corporation, All rights reserved. ======//
//
// Purpose: particle emission operators
//
// Reconstructed from the Alien Swarm SDK particles.lib (builtin_particle_emitters.obj).
//
//===========================================================================//

#include "tier0/platform.h"
#include "particles/particles.h"
#include "filesystem.h"
#include "tier2/tier2.h"
#include "tier2/fileutils.h"
#include "tier0/fasttimer.h"
#include "mathlib/ssemath.h"
#include "particles_internal.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"


// Set by tools that need SkipToTime to be fast (skips the re-simulation of continuous emitters)
bool g_bDontMakeSkipToTimeTakeForever = false;


//-----------------------------------------------------------------------------
// Emits particles immediately
//-----------------------------------------------------------------------------
struct InstantaneousEmitterContext_t
{
	int m_nRemainingParticles;
	int m_nTotalEmitCount;
	int m_nInitialTotalEmitCount;
	float m_flTimeOffset;
	bool m_bScaledEmitCount;
};

class C_OP_InstantaneousEmitter : public CParticleOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_OP_InstantaneousEmitter );

	uint32 GetWrittenAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_CREATION_TIME_MASK;
	}

	uint32 GetReadAttributes( void ) const
	{
		return 0;
	}

	virtual uint64 GetReadControlPointMask() const
	{
		uint64 nMask = 0;
		if ( m_nScaleControlPoint >= 0 )
			nMask |= ( 1ULL << m_nScaleControlPoint );
		if ( m_nSnapshotControlPoint >= 0 )
			nMask |= ( 1ULL << m_nSnapshotControlPoint );
		return nMask;
	}

	virtual uint64 GetNonPositionalControlPointMask() const
	{
		if ( m_nScaleControlPoint >= 0 )
			return ( 1ULL << m_nScaleControlPoint );
		return 0;
	}

	virtual uint32 Emit( CParticleCollection *pParticles, float flCurStrength,
						 void *pContext ) const;

	virtual void InitParams( CParticleSystemDefinition *pDef )
	{
		if ( m_nMinParticlesToEmit >= 0 )
		{
			if ( m_nMinParticlesToEmit > m_nParticlesToEmit )
			{
				V_swap( m_nMinParticlesToEmit, m_nParticlesToEmit );
			}
		}

		if ( m_nPerFrameNum < 0 )
		{
			m_nPerFrameNum = MIN( pDef->m_nMaxParticles, 5000 );
		}
		m_nScaleControlPointField = clamp( m_nScaleControlPointField, 0, 2 );

		// the snapshot provides the emission count, so the scale control point is ignored
		if ( m_nSnapshotControlPoint != -1 )
			m_nScaleControlPoint = -1;
		m_bUsedSnapshotSize = false;
	}

	virtual void StopEmission( CParticleCollection *pParticles, void *pContext, bool bInfiniteOnly ) const
	{
		if ( !bInfiniteOnly )
		{
			InstantaneousEmitterContext_t *pCtx = reinterpret_cast<InstantaneousEmitterContext_t *>( pContext );
			pCtx->m_nRemainingParticles = 0;
		}
	}

	virtual void StartEmission( CParticleCollection *pParticles, void *pContext, bool bInfiniteOnly ) const
	{
		if ( !bInfiniteOnly )
		{
			InstantaneousEmitterContext_t *pCtx = reinterpret_cast<InstantaneousEmitterContext_t *>( pContext );
			pCtx->m_nRemainingParticles = pCtx->m_nTotalEmitCount;
			SkipToTime( pParticles->m_flCurTime, pParticles, pContext );
		}
	}

	// Called when the SFM wants to skip forward in time
	virtual void SkipToTime( float flTime, CParticleCollection *pParticles, void *pContext ) const
	{
		InstantaneousEmitterContext_t *pCtx = reinterpret_cast<InstantaneousEmitterContext_t *>( pContext );

		// If we're skipping well past the emission time, don't emit at all
		float flStartTime = m_flStartTime + pCtx->m_flTimeOffset;
		if ( flTime > flStartTime + 2.0f )
		{
			pCtx->m_nRemainingParticles = 0;
		}
	}

	virtual void InitializeContextData( CParticleCollection *pParticles, void *pContext ) const
	{
		InstantaneousEmitterContext_t *pCtx = reinterpret_cast<InstantaneousEmitterContext_t *>( pContext );
		if ( m_nMinParticlesToEmit >= 0 )
		{
			pCtx->m_nTotalEmitCount = pParticles->RandomInt( m_nMinParticlesToEmit, m_nParticlesToEmit );
		}
		else
		{
			pCtx->m_nTotalEmitCount = m_nParticlesToEmit;
		}
		pCtx->m_nInitialTotalEmitCount = pCtx->m_nTotalEmitCount;
		pCtx->m_nRemainingParticles = pCtx->m_nTotalEmitCount;
		pCtx->m_flTimeOffset = 0.0f;
		pCtx->m_bScaledEmitCount = false;
	}

	virtual void Restart( CParticleCollection *pParticles, void *pContext )
	{
		InstantaneousEmitterContext_t *pCtx = reinterpret_cast<InstantaneousEmitterContext_t *>( pContext );
		pCtx->m_nRemainingParticles = pCtx->m_nInitialTotalEmitCount;
		pCtx->m_flTimeOffset = pParticles->m_flCurTime;
		pCtx->m_bScaledEmitCount = false;
	}

	virtual size_t GetRequiredContextBytes( ) const
	{
		return sizeof( InstantaneousEmitterContext_t );
	}

	virtual bool MayCreateMoreParticles( CParticleCollection const *pParticles, void *pContext ) const
	{
		InstantaneousEmitterContext_t *pCtx = reinterpret_cast<InstantaneousEmitterContext_t *>( pContext );
		return ( pCtx->m_nRemainingParticles > 0 );
	}

	int m_nParticlesToEmit;
	int m_nMinParticlesToEmit;
	float m_flStartTime;
	int m_nPerFrameNum;
	int m_nScaleControlPoint;
	int m_nScaleControlPointField;
	int m_nSnapshotControlPoint;
	mutable bool m_bUsedSnapshotSize;
};

DEFINE_PARTICLE_OPERATOR( C_OP_InstantaneousEmitter, "emit_instantaneously", OPERATOR_GENERIC );

BEGIN_PARTICLE_OPERATOR_UNPACK( C_OP_InstantaneousEmitter )
	DMXELEMENT_UNPACK_FIELD( "emission_start_time", "0", float, m_flStartTime )
	DMXELEMENT_UNPACK_FIELD( "num_to_emit_minimum", "-1", int, m_nMinParticlesToEmit )
	DMXELEMENT_UNPACK_FIELD( "num_to_emit", "100", int, m_nParticlesToEmit )
	DMXELEMENT_UNPACK_FIELD( "maximum emission per frame", "-1", int, m_nPerFrameNum )
	DMXELEMENT_UNPACK_FIELD( "emission count scale control point", "-1", int, m_nScaleControlPoint )
	DMXELEMENT_UNPACK_FIELD( "emission count scale control point field", "0", int, m_nScaleControlPointField )
	DMXELEMENT_UNPACK_FIELD( "control point with snapshot data", "-1", int, m_nSnapshotControlPoint )
END_PARTICLE_OPERATOR_UNPACK( C_OP_InstantaneousEmitter )

uint32 C_OP_InstantaneousEmitter::Emit( CParticleCollection *pParticles, float flCurStrength,
										void *pContext ) const
{
	InstantaneousEmitterContext_t *pCtx = reinterpret_cast<InstantaneousEmitterContext_t *>( pContext );

	if ( !m_bUsedSnapshotSize )
	{
		CParticleSnapshot *pSnapshot = ( m_nSnapshotControlPoint != -1 ) ? pParticles->GetControlPointSnapshot( m_nSnapshotControlPoint ) : NULL;
		if ( pSnapshot )
		{
			// emit one particle per snapshot entry
			pCtx->m_nTotalEmitCount = pSnapshot->NumCols();
			pCtx->m_nRemainingParticles = pCtx->m_nTotalEmitCount;
		}
		m_bUsedSnapshotSize = true;
	}

	// Wait until we're told to start emitting
	if ( pCtx->m_nRemainingParticles <= 0 )
		return 0;

	float flStartTime = m_flStartTime + pCtx->m_flTimeOffset;
	if ( pParticles->m_flCurTime < flStartTime )
		return 0;

	if ( pCtx->m_nTotalEmitCount == 0 )
		return 0;

	if ( ( m_nScaleControlPoint >= 0 ) && !pCtx->m_bScaledEmitCount )
	{
		Vector vecScale;
		if ( ( pParticles->m_flCurTime >= flStartTime ) && ( flStartTime >= ( pParticles->m_flCurTime - pParticles->m_flPreviousDt ) ) )
		{
			pParticles->GetControlPointAtTime( m_nScaleControlPoint, flStartTime, &vecScale );
		}
		else
		{
			pParticles->GetControlPointAtPrevTime( m_nScaleControlPoint, &vecScale );
		}

		pCtx->m_nTotalEmitCount = pCtx->m_nInitialTotalEmitCount * vecScale[m_nScaleControlPointField];
		pCtx->m_nRemainingParticles = pCtx->m_nRemainingParticles * vecScale[m_nScaleControlPointField];
		pCtx->m_bScaledEmitCount = true;
	}

	pCtx->m_nRemainingParticles = MAX( 0, pCtx->m_nRemainingParticles );

	// We're only allowed to emit so many particles, though..
	// If we run out of room, only emit the last N particles
	int nAllowedParticlesToEmit = pParticles->m_nMaxAllowedParticles - pParticles->m_nActiveParticles;

	int nPerFrameNum = MIN( m_nPerFrameNum, pCtx->m_nRemainingParticles );

	int nActualParticlesToEmit = MIN( MIN( nAllowedParticlesToEmit, nPerFrameNum ), pCtx->m_nTotalEmitCount * g_nParticle_Multiplier );
	pCtx->m_nRemainingParticles -= nPerFrameNum;

	if ( nActualParticlesToEmit == 0 )
		return 0;

	int nStartParticle = pParticles->m_nActiveParticles;
	pParticles->SetNActiveParticles( nActualParticlesToEmit + pParticles->m_nActiveParticles );

	// !! speed!! do sse init here
	for( int i = nStartParticle; i < nStartParticle + nActualParticlesToEmit; i++ )
	{
		float *pTimeStamp = pParticles->GetFloatAttributePtrForWrite( PARTICLE_ATTRIBUTE_CREATION_TIME, i );
		*pTimeStamp = flStartTime;
	}

	return PARTICLE_ATTRIBUTE_CREATION_TIME_MASK;
}


//-----------------------------------------------------------------------------
// Emits particles over time
//-----------------------------------------------------------------------------
struct ContinuousEmitterContext_t
{
	float m_flTotalActualParticlesSoFar;
	int m_nTotalEmittedSoFar;
	float m_flNextStartTime;
	float m_flTimeOffset;
	bool m_bStoppedEmission;
};

class C_OP_ContinuousEmitter : public CParticleOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_OP_ContinuousEmitter );

	uint32 GetWrittenAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_CREATION_TIME_MASK;
	}

	uint32 GetReadAttributes( void ) const
	{
		return 0;
	}

	virtual uint64 GetReadControlPointMask() const
	{
		if ( m_nScaleControlPoint >= 0 )
			return ( 1ULL << m_nScaleControlPoint );
		return 0;
	}

	virtual uint64 GetNonPositionalControlPointMask() const
	{
		if ( m_nScaleControlPoint >= 0 )
			return ( 1ULL << m_nScaleControlPoint );
		return 0;
	}

	virtual void InitParams( CParticleSystemDefinition *pDef )
	{
		if ( m_flEmitRate < 0.0f )
		{
			m_flEmitRate = 0.0f;
		}
		if ( m_flEmissionDuration < 0.0f )
		{
			m_flEmissionDuration = 0.0f;
		}
		m_flEmitRate *= g_nParticle_Multiplier;
	}

	virtual uint32 Emit( CParticleCollection *pParticles, float flCurStrength,
						 void *pContext ) const ;

	bool IsInfinitelyEmitting() const
	{
		return ( m_flEmissionDuration == 0.0f );
	}

	virtual void StopEmission( CParticleCollection *pParticles, void *pContext, bool bInfiniteOnly ) const
	{
		ContinuousEmitterContext_t *pCtx = reinterpret_cast<ContinuousEmitterContext_t *>( pContext );
		if ( !bInfiniteOnly || IsInfinitelyEmitting() )
		{
			pCtx->m_bStoppedEmission = true;
		}
	}

	virtual void StartEmission( CParticleCollection *pParticles, void *pContext, bool bInfiniteOnly ) const
	{
		ContinuousEmitterContext_t *pCtx = reinterpret_cast<ContinuousEmitterContext_t *>( pContext );
		if ( !bInfiniteOnly || IsInfinitelyEmitting() )
		{
			pCtx->m_bStoppedEmission = false;
			SkipToTime( pParticles->m_flCurTime, pParticles, pContext );
		}
	}

	virtual void InitializeContextData( CParticleCollection *pParticles, void *pContext ) const
	{
		ContinuousEmitterContext_t *pCtx = reinterpret_cast<ContinuousEmitterContext_t *>( pContext );
		pCtx->m_flNextStartTime = m_flStartTime;
		pCtx->m_flTotalActualParticlesSoFar = 0.0f;
		pCtx->m_nTotalEmittedSoFar = 0;
		pCtx->m_flTimeOffset = 0.0f;
		pCtx->m_bStoppedEmission = false;
	}

	virtual void Restart( CParticleCollection *pParticles, void *pContext )
	{
		if ( !IsInfinitelyEmitting() )
		{
			ContinuousEmitterContext_t *pCtx = reinterpret_cast<ContinuousEmitterContext_t *>( pContext );
			pCtx->m_flNextStartTime = m_flStartTime + pParticles->m_flCurTime;
			pCtx->m_flTotalActualParticlesSoFar = 0.0f;
			pCtx->m_nTotalEmittedSoFar = 0;
			pCtx->m_flTimeOffset = pParticles->m_flCurTime;
		}
	}

	// Called when the SFM wants to skip forward in time
	virtual void SkipToTime( float flTime, CParticleCollection *pParticles, void *pContext ) const;

	virtual size_t GetRequiredContextBytes( ) const
	{
		return sizeof( ContinuousEmitterContext_t );
	}

	virtual bool MayCreateMoreParticles( CParticleCollection const *pParticles, void *pContext ) const
	{
		ContinuousEmitterContext_t *pCtx = reinterpret_cast<ContinuousEmitterContext_t *>( pContext );
		if ( pCtx->m_bStoppedEmission )
			return false;

		if ( m_bInitFromKilledParentParticles )
			return false;

		if ( m_flEmitRate <= 0.0f )
			return false;

		if ( !IsInfinitelyEmitting() && ( ( pParticles->m_flCurTime - pParticles->m_flDt ) > ( m_flStartTime + pCtx->m_flTimeOffset + m_flEmissionDuration ) ) )
			return false;

		return true;
	}

	virtual bool ShouldRun( bool bApplyingParentKillList ) const
	{
		if ( m_bInitFromKilledParentParticles )
			return bApplyingParentKillList;
		return !bApplyingParentKillList;
	}

	float m_flEmissionDuration;
	float m_flStartTime;
	float m_flEmitRate;
	float m_flTimePerEmission;
	float m_flEmissionScale;
	int m_nScaleControlPoint;
	int m_nScaleControlPointField;
	bool m_bScalePerParticle;
	bool m_bInitFromKilledParentParticles;
};

DEFINE_PARTICLE_OPERATOR( C_OP_ContinuousEmitter, "emit_continuously", OPERATOR_GENERIC );

BEGIN_PARTICLE_OPERATOR_UNPACK( C_OP_ContinuousEmitter )
	DMXELEMENT_UNPACK_FIELD( "emission_start_time", "0", float, m_flStartTime )
	DMXELEMENT_UNPACK_FIELD( "emission_rate", "100", float, m_flEmitRate )
	DMXELEMENT_UNPACK_FIELD( "emission_duration", "0", float, m_flEmissionDuration )
	DMXELEMENT_UNPACK_FIELD( "scale emission to used control points", "0.0", float, m_flEmissionScale )
	DMXELEMENT_UNPACK_FIELD( "use parent particles for emission scaling", "0", bool, m_bScalePerParticle )
	DMXELEMENT_UNPACK_FIELD( "emission count scale control point", "-1", int, m_nScaleControlPoint )
	DMXELEMENT_UNPACK_FIELD( "emission count scale control point field", "0", int, m_nScaleControlPointField )
	DMXELEMENT_UNPACK_FIELD( "emit particles for killed parent particles", "0", bool, m_bInitFromKilledParentParticles )
END_PARTICLE_OPERATOR_UNPACK( C_OP_ContinuousEmitter )

void C_OP_ContinuousEmitter::SkipToTime( float flTime, CParticleCollection *pParticles, void *pContext ) const
{
	ContinuousEmitterContext_t *pCtx = reinterpret_cast<ContinuousEmitterContext_t *>( pContext );
	float flStartTime = m_flStartTime + pCtx->m_flTimeOffset;
	if ( flTime <= flStartTime )
		return;

	// emitters driven by the parent's kill list have nothing to catch up on
	if ( m_bInitFromKilledParentParticles )
		return;

	int nHighestControlPoint = pParticles->GetHighestControlPoint();
	float flControlPointScale = m_flEmissionScale * nHighestControlPoint;

	// Have we reached the start time yet?
	float flEmissionRate = m_flEmitRate;
	float flEmitStrength;
	if ( pParticles->CheckIfOperatorShouldRun( this, &flEmitStrength ) )
	{
		flEmissionRate *= flEmitStrength;
	}

	if ( m_nScaleControlPoint >= 0 )
	{
		Vector vecScale;
		pParticles->GetControlPointAtTime( m_nScaleControlPoint, pParticles->m_flCurTime, &vecScale );
		float flScale = vecScale[m_nScaleControlPointField];
		flEmissionRate *= MAX( 0.0f, flScale );
	}

	if ( flControlPointScale != 0.0f )
	{
		flEmissionRate *= flControlPointScale;
	}

	float flCurrDrawTime = pParticles->m_flCurTime;
	float flPrevDrawTime = pParticles->m_flCurTime - flTime;
	if ( !IsInfinitelyEmitting() )
	{
		if ( flPrevDrawTime < flStartTime )
		{
			flPrevDrawTime = flStartTime;
		}
	}

	// Don't emit more than a few seconds worth of particles
	float flDeltaTime = flCurrDrawTime - flPrevDrawTime;
	flDeltaTime = MIN( flDeltaTime, 4.0f );
	flPrevDrawTime = flCurrDrawTime - flDeltaTime;

	pCtx->m_flTotalActualParticlesSoFar = flDeltaTime * flEmissionRate;
	pCtx->m_nTotalEmittedSoFar = 0;
	int nActualParticlesToEmit = floor( pCtx->m_flTotalActualParticlesSoFar );

	int nStartParticle = pParticles->m_nActiveParticles;
	if ( pParticles->m_nMaxAllowedParticles < nStartParticle + nActualParticlesToEmit )
	{
		nActualParticlesToEmit = pParticles->m_nMaxAllowedParticles - nStartParticle;
	}
	pParticles->SetNActiveParticles( nActualParticlesToEmit + pParticles->m_nActiveParticles );

	float flTimeStampStep = flDeltaTime / nActualParticlesToEmit;
	float flTimeStamp = flPrevDrawTime + flTimeStampStep;
	for ( int i = nStartParticle; i < nStartParticle + nActualParticlesToEmit; i++ )
	{
		float *pTimeStamp = pParticles->GetFloatAttributePtrForWrite( PARTICLE_ATTRIBUTE_CREATION_TIME, i );
		flTimeStamp = MIN( flTimeStamp, flCurrDrawTime );
		*pTimeStamp = flTimeStamp;
		flTimeStamp += flTimeStampStep;
	}

	if ( !g_bDontMakeSkipToTimeTakeForever )
	{
		// re-simulate the skipped interval, but only as far back as the system can sleep
		flPrevDrawTime = MAX( flPrevDrawTime, flCurrDrawTime - pParticles->m_pDef->m_flNoDrawTimeToGoToSleep );
		pParticles->m_flCurTime = flPrevDrawTime;
		pParticles->m_fl4CurTime = ReplicateX4( flPrevDrawTime );
		pParticles->m_flTargetDrawTime = flPrevDrawTime;
		for ( float i = flPrevDrawTime; i < flCurrDrawTime; i += 0.1 )
		{
			pParticles->Simulate( 0.1f );
		}
	}
}

uint32 C_OP_ContinuousEmitter::Emit( CParticleCollection *pParticles, float flCurStrength,
									 void *pContext ) const
{
	ContinuousEmitterContext_t *pCtx = reinterpret_cast<ContinuousEmitterContext_t *>( pContext );

	// Have we reached the start time yet?
	float flStartTime = m_flStartTime + pCtx->m_flTimeOffset;
	if ( flStartTime > pParticles->m_flCurTime )
		return 0;

	float flPrevDrawTime;
	float flCurrDrawTime;
	if ( m_bInitFromKilledParentParticles )
	{
		// emit one particle per parent particle killed this frame
		int nNumParticlesToKill;
		GetParentKillList( pParticles, nNumParticlesToKill );

		flCurrDrawTime = pParticles->m_flCurTime;
		if ( !IsInfinitelyEmitting() )
		{
			flCurrDrawTime = clamp( flCurrDrawTime, flStartTime, flStartTime + m_flEmissionDuration );
		}
		flPrevDrawTime = flCurrDrawTime;

		pCtx->m_flTotalActualParticlesSoFar += nNumParticlesToKill;
	}
	else
	{
		int nHighestControlPoint = pParticles->GetHighestControlPoint();
		float flControlPointScale = m_flEmissionScale * nHighestControlPoint;

		float flEmissionRate = m_flEmitRate * flCurStrength;
		if ( m_nScaleControlPoint >= 0 )
		{
			Vector vecScale;
			pParticles->GetControlPointAtTime( m_nScaleControlPoint, pParticles->m_flCurTime, &vecScale );
			flEmissionRate *= MAX( 0.0f, vecScale[m_nScaleControlPointField] );
		}

		if ( ( flControlPointScale != 0.0f ) || m_bScalePerParticle )
		{
			if ( m_bScalePerParticle )
			{
				if ( pParticles->m_pParent )
				{
					flControlPointScale = pParticles->m_pParent->m_nActiveParticles * m_flEmissionScale;
				}
				else
				{
					flControlPointScale = m_flEmissionScale;
				}
			}
			flEmissionRate *= flControlPointScale;
		}

		if ( flEmissionRate == 0.0f )
			return 0;

		if ( !MayCreateMoreParticles( pParticles, pContext ) )
			return 0;

		flPrevDrawTime = pParticles->m_flCurTime - pParticles->m_flDt;
		flCurrDrawTime = pParticles->m_flCurTime;
		if ( !IsInfinitelyEmitting() )
		{
			flPrevDrawTime = MAX( flPrevDrawTime, flStartTime );
			flCurrDrawTime = MIN( flCurrDrawTime, flStartTime + m_flEmissionDuration );
		}

		pCtx->m_flTotalActualParticlesSoFar += flEmissionRate * ( flCurrDrawTime - flPrevDrawTime );
	}

	// Emit whole particles only, carrying the fraction over to the next frame
	int nActualParticlesToEmit = floor( pCtx->m_flTotalActualParticlesSoFar ) - pCtx->m_nTotalEmittedSoFar;

	pCtx->m_nTotalEmittedSoFar += nActualParticlesToEmit;

	if ( nActualParticlesToEmit == 0 )
		return 0;

	// We're only allowed to emit so many particles, though..
	// If we run out of room, only emit the last N particles
	int nStartParticle = pParticles->m_nActiveParticles;
	if ( pParticles->m_nMaxAllowedParticles - nStartParticle < nActualParticlesToEmit )
	{
		nActualParticlesToEmit = pParticles->m_nMaxAllowedParticles - nStartParticle;
	}

	if ( nActualParticlesToEmit == 0 )
		return 0;

	pParticles->SetNActiveParticles( nActualParticlesToEmit + pParticles->m_nActiveParticles );

	// spread the creation times evenly across the emission interval
	float flTimeStampStep = ( flCurrDrawTime - flPrevDrawTime ) / ( nActualParticlesToEmit );
	float flTimeStamp = flPrevDrawTime + flTimeStampStep;
	for ( int i = nStartParticle; i < nStartParticle + nActualParticlesToEmit; i++ )
	{
		float *pTimeStamp = pParticles->GetFloatAttributePtrForWrite( PARTICLE_ATTRIBUTE_CREATION_TIME, i );
		flTimeStamp = MIN( flTimeStamp, flCurrDrawTime );
		*pTimeStamp = flTimeStamp;
		flTimeStamp += flTimeStampStep;
	}

	return PARTICLE_ATTRIBUTE_CREATION_TIME_MASK;
}


//-----------------------------------------------------------------------------
// Noise Emitter
//-----------------------------------------------------------------------------
struct NoiseEmitterContext_t
{
	float m_flTotalActualParticlesSoFar;
	int m_nTotalEmittedSoFar;
	float m_flNextStartTime;
	float m_flTimeOffset;
	bool m_bStoppedEmission;
};

class C_OP_NoiseEmitter : public CParticleOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_OP_NoiseEmitter );

	uint32 GetWrittenAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_CREATION_TIME_MASK;
	}

	uint32 GetReadAttributes( void ) const
	{
		return 0;
	}

	virtual void InitParams( CParticleSystemDefinition *pDef )
	{
		if ( m_flEmitRate < 0.0f )
		{
			m_flEmitRate = 0.0f;
		}
		if ( m_flEmissionDuration < 0.0f )
		{
			m_flEmissionDuration = 0.0f;
		}
		m_flEmitRate *= g_nParticle_Multiplier;
	}

	virtual uint32 Emit( CParticleCollection *pParticles, float flCurStrength,
						 void *pContext ) const ;

	bool IsInfinitelyEmitting() const
	{
		return ( m_flEmissionDuration == 0.0f );
	}

	virtual void StopEmission( CParticleCollection *pParticles, void *pContext, bool bInfiniteOnly ) const
	{
		NoiseEmitterContext_t *pCtx = reinterpret_cast<NoiseEmitterContext_t *>( pContext );
		if ( !bInfiniteOnly || IsInfinitelyEmitting() )
		{
			pCtx->m_bStoppedEmission = true;
		}
	}

	virtual void StartEmission( CParticleCollection *pParticles, void *pContext, bool bInfiniteOnly ) const
	{
		NoiseEmitterContext_t *pCtx = reinterpret_cast<NoiseEmitterContext_t *>( pContext );
		if ( !bInfiniteOnly || IsInfinitelyEmitting() )
		{
			pCtx->m_bStoppedEmission = false;
			SkipToTime( pParticles->m_flCurTime, pParticles, pContext );
		}
	}

	virtual void InitializeContextData( CParticleCollection *pParticles, void *pContext ) const
	{
		NoiseEmitterContext_t *pCtx = reinterpret_cast<NoiseEmitterContext_t *>( pContext );
		pCtx->m_flNextStartTime = m_flStartTime;
		pCtx->m_flTotalActualParticlesSoFar = 1.0f;
		pCtx->m_nTotalEmittedSoFar = 0;
		pCtx->m_flTimeOffset = 0.0f;
		pCtx->m_bStoppedEmission = false;
	}

	virtual void Restart( CParticleCollection *pParticles, void *pContext )
	{
		if ( !IsInfinitelyEmitting() )
		{
			NoiseEmitterContext_t *pCtx = reinterpret_cast<NoiseEmitterContext_t *>( pContext );
			pCtx->m_flNextStartTime = m_flStartTime + pParticles->m_flCurTime;
			pCtx->m_flTotalActualParticlesSoFar = 1.0f;
			pCtx->m_nTotalEmittedSoFar = 0;
			pCtx->m_flTimeOffset = pParticles->m_flCurTime;
		}
	}

	// Called when the SFM wants to skip forward in time
	virtual void SkipToTime( float flTime, CParticleCollection *pParticles, void *pContext ) const
	{
		NoiseEmitterContext_t *pCtx = reinterpret_cast<NoiseEmitterContext_t *>( pContext );
		float flStartTime = m_flStartTime + pCtx->m_flTimeOffset;
		if ( flTime <= flStartTime )
			return;

		int nHighestControlPoint = pParticles->GetHighestControlPoint();
		float flControlPointScale = m_flEmissionScale * nHighestControlPoint;

		float flEmissionRate = m_flEmitRate;
		float flEmitStrength;
		if ( pParticles->CheckIfOperatorShouldRun( this, &flEmitStrength ) )
		{
			flEmissionRate *= flEmitStrength;
		}

		if ( flControlPointScale != 0.0f )
		{
			flEmissionRate *= flControlPointScale;
		}

		pCtx->m_flTotalActualParticlesSoFar = ( pParticles->m_flCurTime - flStartTime ) * flEmissionRate + 1.0f;

		// NOTE: Using floor here 'cause it's what the code was doing before
		pCtx->m_nTotalEmittedSoFar = 0;
	}

	virtual size_t GetRequiredContextBytes( ) const
	{
		return sizeof( NoiseEmitterContext_t );
	}

	virtual bool MayCreateMoreParticles( CParticleCollection const *pParticles, void *pContext ) const
	{
		NoiseEmitterContext_t *pCtx = reinterpret_cast<NoiseEmitterContext_t *>( pContext );
		if ( pCtx->m_bStoppedEmission )
			return false;

		if ( m_flEmitRate <= 0.0f )
			return false;

		if ( !IsInfinitelyEmitting() && ( ( pParticles->m_flCurTime - pParticles->m_flDt ) > ( m_flStartTime + pCtx->m_flTimeOffset + m_flEmissionDuration ) ) )
			return false;

		return true;
	}

	float m_flEmissionDuration;
	float m_flStartTime;
	float m_flEmitRate;
	float m_flTimePerEmission;
	float m_flEmissionScale;
	bool m_bAbsVal;
	bool m_bAbsValInv;
	float m_flOffset;
	float m_flOutputMin;
	float m_flOutputMax;
	float m_flNoiseScale;
	float m_flNoiseScaleLoc;
	Vector m_vecOffsetLoc;
	float m_flWorldTimeScale;
};

DEFINE_PARTICLE_OPERATOR( C_OP_NoiseEmitter, "emit noise", OPERATOR_GENERIC );

BEGIN_PARTICLE_OPERATOR_UNPACK( C_OP_NoiseEmitter )
	DMXELEMENT_UNPACK_FIELD( "emission_start_time", "0", float, m_flStartTime )
	DMXELEMENT_UNPACK_FIELD( "emission_duration", "0", float, m_flEmissionDuration )
	DMXELEMENT_UNPACK_FIELD( "scale emission to used control points", "0.0", float, m_flEmissionScale )
	DMXELEMENT_UNPACK_FIELD( "time noise coordinate scale", "0.1", float, m_flNoiseScale )
	DMXELEMENT_UNPACK_FIELD( "time coordinate offset", "0", float, m_flOffset )
	DMXELEMENT_UNPACK_FIELD( "absolute value", "0", bool, m_bAbsVal )
	DMXELEMENT_UNPACK_FIELD( "invert absolute value", "0", bool, m_bAbsValInv )
	DMXELEMENT_UNPACK_FIELD( "emission minimum", "0", float, m_flOutputMin )
	DMXELEMENT_UNPACK_FIELD( "emission maximum", "100", float, m_flOutputMax )
	DMXELEMENT_UNPACK_FIELD( "world time noise coordinate scale", "0", float, m_flWorldTimeScale )
END_PARTICLE_OPERATOR_UNPACK( C_OP_NoiseEmitter )

uint32 C_OP_NoiseEmitter::Emit( CParticleCollection *pParticles, float flCurStrength,
								void *pContext ) const
{
	NoiseEmitterContext_t *pCtx = reinterpret_cast<NoiseEmitterContext_t *>( pContext );

	int nHighestControlPoint = pParticles->GetHighestControlPoint();
	float flControlPointScale = m_flEmissionScale * nHighestControlPoint;

	// Noise is in the -1..1 range by default; absolute value folds it into 0..1
	int nAbsVal = 0xffffffff;
	float flAbsScale = 0.5f;
	if ( m_bAbsVal )
	{
		nAbsVal = 0x7fffffff;
		flAbsScale = 1.0f;
	}

	float fMin = m_flOutputMin;
	float fMax = m_flOutputMax;

	float flCoord = pParticles->m_flCurTime + m_flOffset;
	Vector Coord( flCoord * m_flNoiseScale, flCoord * m_flNoiseScale, flCoord * m_flNoiseScale );

	// world time keeps different systems out of sync with each other
	Vector CoordWorldTime;
	CoordWorldTime.x = Plat_MSTime() * m_flWorldTimeScale;
	CoordWorldTime.y = Plat_MSTime() * m_flWorldTimeScale;
	CoordWorldTime.z = Plat_MSTime() * m_flWorldTimeScale;
	Coord += CoordWorldTime;

	FourVectors fvNoise;
	fvNoise.DuplicateVector( Coord );
	fltx4 flNoise128 = NoiseSIMD( fvNoise );
	float flNoise = SubFloat( flNoise128, 0 );

	*( int * ) &flNoise &= nAbsVal;

	float ValueScale = flAbsScale * ( fMax - fMin );
	float ValueBase = fMin + ( ( 1.0f - flAbsScale ) * ( fMax - fMin ) );

	if ( m_bAbsValInv )
	{
		flNoise = 1.0f - flNoise;
	}

	float flInitialNoise = ValueBase + ( ValueScale * flNoise );
	flInitialNoise = clamp( flInitialNoise, 0.0f, (float)INT_MAX );

	float flEmissionRate = flInitialNoise * flCurStrength;
	if ( flControlPointScale != 0.0f )
	{
		flEmissionRate *= flControlPointScale;
	}

	if ( flEmissionRate == 0.0f )
		return 0;

	if ( !MayCreateMoreParticles( pParticles, pContext ) )
		return 0;

	// Have we reached the start time yet?
	float flStartTime = m_flStartTime + pCtx->m_flTimeOffset;
	if ( flStartTime > pParticles->m_flCurTime )
		return 0;

	float flPrevDrawTime = pParticles->m_flCurTime - pParticles->m_flDt;
	float flCurrDrawTime = pParticles->m_flCurTime;
	if ( !IsInfinitelyEmitting() )
	{
		if ( flPrevDrawTime < flStartTime )
		{
			flPrevDrawTime = flStartTime;
		}
		if ( flCurrDrawTime > flStartTime + m_flEmissionDuration )
		{
			flCurrDrawTime = flStartTime + m_flEmissionDuration;
		}
	}

	float flDeltaTime = flCurrDrawTime - flPrevDrawTime;

	// Emit whole particles only, carrying the fraction over to the next frame
	pCtx->m_flTotalActualParticlesSoFar += flDeltaTime * flEmissionRate;
	int nActualParticlesToEmit = floor( pCtx->m_flTotalActualParticlesSoFar ) - pCtx->m_nTotalEmittedSoFar;

	pCtx->m_nTotalEmittedSoFar += nActualParticlesToEmit;

	if ( nActualParticlesToEmit == 0 )
		return 0;

	// We're only allowed to emit so many particles, though..
	// If we run out of room, only emit the last N particles
	int nStartParticle = pParticles->m_nActiveParticles;
	if ( pParticles->m_nMaxAllowedParticles - nStartParticle < nActualParticlesToEmit )
	{
		nActualParticlesToEmit = pParticles->m_nMaxAllowedParticles - nStartParticle;
	}

	if ( nActualParticlesToEmit == 0 )
		return 0;

	pParticles->SetNActiveParticles( nActualParticlesToEmit + pParticles->m_nActiveParticles );

	float flTimeStampStep = ( flDeltaTime ) / ( nActualParticlesToEmit );
	float flTimeStamp = flPrevDrawTime + flTimeStampStep;
	for ( int i = nStartParticle; i < nStartParticle + nActualParticlesToEmit; i++ )
	{
		float *pTimeStamp = pParticles->GetFloatAttributePtrForWrite( PARTICLE_ATTRIBUTE_CREATION_TIME, i );
		flTimeStamp = MIN( flTimeStamp, flCurrDrawTime );
		*pTimeStamp = flTimeStamp;
		flTimeStamp += flTimeStampStep;
	}

	return PARTICLE_ATTRIBUTE_CREATION_TIME_MASK;
}


//-----------------------------------------------------------------------------
// Maintains a particle count
//-----------------------------------------------------------------------------
struct MaintainEmitterContext_t
{
	int m_nParticlesToMaintain;
	float m_flTimeOffset;
	bool m_bEmitting;
};

class C_OP_MaintainEmitter : public CParticleOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_OP_MaintainEmitter );

	uint32 GetWrittenAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_CREATION_TIME_MASK;
	}

	uint32 GetReadAttributes( void ) const
	{
		return 0;
	}

	virtual uint64 GetReadControlPointMask() const
	{
		uint64 nMask = 0;
		if ( m_nScaleControlPoint >= 0 )
			nMask |= ( 1ULL << m_nScaleControlPoint );
		if ( m_nSnapshotControlPoint >= 0 )
			nMask |= ( 1ULL << m_nSnapshotControlPoint );
		return nMask;
	}

	virtual uint64 GetNonPositionalControlPointMask() const
	{
		if ( m_nScaleControlPoint >= 0 )
			return ( 1ULL << m_nScaleControlPoint );
		return 0;
	}

	virtual uint32 Emit( CParticleCollection *pParticles, float flCurStrength,
						 void *pContext ) const;

	virtual void InitParams( CParticleSystemDefinition *pDef )
	{
		m_nScaleControlPoint = clamp( m_nScaleControlPoint, -1, MAX_PARTICLE_CONTROL_POINTS );
		m_nScaleControlPointField = clamp( m_nScaleControlPointField, 0, 2 );

		// the snapshot provides the particle count, so the scale control point is ignored
		if ( m_nSnapshotControlPoint != -1 )
			m_nScaleControlPoint = -1;
		m_bUsedSnapshotSize = false;
	}

	virtual void StopEmission( CParticleCollection *pParticles, void *pContext, bool bInfiniteOnly ) const
	{
		MaintainEmitterContext_t *pCtx = reinterpret_cast<MaintainEmitterContext_t *>( pContext );
		pCtx->m_bEmitting = false;
	}

	virtual void StartEmission( CParticleCollection *pParticles, void *pContext, bool bInfiniteOnly ) const
	{
		MaintainEmitterContext_t *pCtx = reinterpret_cast<MaintainEmitterContext_t *>( pContext );
		pCtx->m_bEmitting = true;
		float flStartTime = m_flStartTime + pCtx->m_flTimeOffset;
		if ( m_nScaleControlPoint >= 0 )
		{
			Vector vecScale;
			if ( ( pParticles->m_flCurTime >= flStartTime ) && ( flStartTime >= ( pParticles->m_flCurTime - pParticles->m_flPreviousDt ) ) )
			{
				pParticles->GetControlPointAtTime( m_nScaleControlPoint, flStartTime, &vecScale );
			}
			else
			{
				pParticles->GetControlPointAtPrevTime( m_nScaleControlPoint, &vecScale );
			}
			pCtx->m_nParticlesToMaintain = m_nParticlesToMaintain * vecScale[m_nScaleControlPointField];
		}
		SkipToTime( pParticles->m_flCurTime, pParticles, pContext );
	}

	// Called when the SFM wants to skip forward in time
	virtual void SkipToTime( float flTime, CParticleCollection *pParticles, void *pContext ) const
	{
		MaintainEmitterContext_t *pCtx = reinterpret_cast<MaintainEmitterContext_t *>( pContext );

		// If we're skipping well past the emission time, don't emit at all
		float flStartTime = m_flStartTime + pCtx->m_flTimeOffset;
		if ( flTime > flStartTime + 2.0f )
		{
			pCtx->m_nParticlesToMaintain = 0;
		}
	}

	virtual void InitializeContextData( CParticleCollection *pParticles, void *pContext ) const
	{
		MaintainEmitterContext_t *pCtx = reinterpret_cast<MaintainEmitterContext_t *>( pContext );
		pCtx->m_flTimeOffset = 0.0f;
		pCtx->m_bEmitting = true;
		if ( m_nScaleControlPoint < 0 )
			pCtx->m_nParticlesToMaintain = m_nParticlesToMaintain;
	}

	virtual void Restart( CParticleCollection *pParticles, void *pContext )
	{
		MaintainEmitterContext_t *pCtx = reinterpret_cast<MaintainEmitterContext_t *>( pContext );
		pCtx->m_flTimeOffset = pParticles->m_flCurTime;
		pCtx->m_bEmitting = true;
	}

	virtual size_t GetRequiredContextBytes( ) const
	{
		return sizeof( MaintainEmitterContext_t );
	}

	virtual bool MayCreateMoreParticles( CParticleCollection const *pParticles, void *pContext ) const
	{
		MaintainEmitterContext_t *pCtx = reinterpret_cast<MaintainEmitterContext_t *>( pContext );
		if ( pCtx->m_bEmitting )
		{
			if ( m_nScaleControlPoint >= 0 )
				return true;
			if ( pCtx->m_nParticlesToMaintain > pParticles->m_nActiveParticles )
				return true;
			float flStartTime = m_flStartTime + pCtx->m_flTimeOffset;
			if ( flStartTime > pParticles->m_flCurTime )
				return true;
		}
		return false;
	}

	int m_nParticlesToMaintain;
	float m_flStartTime;
	int m_nScaleControlPoint;
	int m_nScaleControlPointField;
	int m_nSnapshotControlPoint;
	mutable bool m_bUsedSnapshotSize;
};

DEFINE_PARTICLE_OPERATOR( C_OP_MaintainEmitter, "emit to maintain count", OPERATOR_GENERIC );

BEGIN_PARTICLE_OPERATOR_UNPACK( C_OP_MaintainEmitter )
	DMXELEMENT_UNPACK_FIELD( "emission start time", "0", float, m_flStartTime )
	DMXELEMENT_UNPACK_FIELD( "count to maintain", "100", int, m_nParticlesToMaintain )
	DMXELEMENT_UNPACK_FIELD( "maintain count scale control point", "-1", int, m_nScaleControlPoint )
	DMXELEMENT_UNPACK_FIELD( "maintain count scale control point field", "0", int, m_nScaleControlPointField )
	DMXELEMENT_UNPACK_FIELD( "control point with snapshot data", "-1", int, m_nSnapshotControlPoint )
END_PARTICLE_OPERATOR_UNPACK( C_OP_MaintainEmitter )

uint32 C_OP_MaintainEmitter::Emit( CParticleCollection *pParticles, float flCurStrength,
								   void *pContext ) const
{
	MaintainEmitterContext_t *pCtx = reinterpret_cast<MaintainEmitterContext_t *>( pContext );

	if ( !pCtx->m_bEmitting )
		return 0;

	if ( !m_bUsedSnapshotSize )
	{
		CParticleSnapshot *pSnapshot = ( m_nSnapshotControlPoint != -1 ) ? pParticles->GetControlPointSnapshot( m_nSnapshotControlPoint ) : NULL;
		if ( pSnapshot )
		{
			// maintain one particle per snapshot entry
			pCtx->m_nParticlesToMaintain = pSnapshot->NumCols();
		}
		m_bUsedSnapshotSize = true;
	}

	// Wait until we're told to start emitting
	float flStartTime = m_flStartTime + pCtx->m_flTimeOffset;
	if ( flStartTime > pParticles->m_flCurTime )
		return 0;

	if ( m_nScaleControlPoint >= 0 )
	{
		Vector vecScale;
		if ( ( pParticles->m_flCurTime >= flStartTime ) && ( flStartTime >= ( pParticles->m_flCurTime - pParticles->m_flPreviousDt ) ) )
		{
			pParticles->GetControlPointAtTime( m_nScaleControlPoint, flStartTime, &vecScale );
		}
		else
		{
			pParticles->GetControlPointAtPrevTime( m_nScaleControlPoint, &vecScale );
		}
		pCtx->m_nParticlesToMaintain = m_nParticlesToMaintain * vecScale[m_nScaleControlPointField];
	}

	// Don't emit if we're already at (or above) the count we're maintaining
	int nActiveParticles = pParticles->m_nActiveParticles;
	if ( ( nActiveParticles >= pCtx->m_nParticlesToMaintain ) || ( pCtx->m_nParticlesToMaintain <= 0 ) )
		return 0;

	// We're only allowed to emit so many particles, though..
	// If we run out of room, only emit the last N particles
	int nAllowedParticlesToEmit = pParticles->m_nMaxAllowedParticles - nActiveParticles;

	int nActualParticlesToEmit = pCtx->m_nParticlesToMaintain - nActiveParticles;
	nActualParticlesToEmit = MIN( nAllowedParticlesToEmit, nActualParticlesToEmit );

	if ( nActualParticlesToEmit < 0 )
		return 0;

	pParticles->SetNActiveParticles( nActualParticlesToEmit + nActiveParticles );
	float flTimeStamp = MAX( pParticles->m_flPrevSimTime, flStartTime );
	float flTimeStampStep = ( pParticles->m_flCurTime - flTimeStamp ) / nActualParticlesToEmit;

	// spread the creation times across the last simulation step
	for ( int i = nActiveParticles; i < nActiveParticles + nActualParticlesToEmit; i++ )
	{
		float *pTimeStamp = pParticles->GetFloatAttributePtrForWrite( PARTICLE_ATTRIBUTE_CREATION_TIME, i );
		flTimeStamp = MIN( flTimeStamp, pParticles->m_flCurTime );
		*pTimeStamp = flTimeStamp;
		flTimeStamp += flTimeStampStep;
	}

	return PARTICLE_ATTRIBUTE_CREATION_TIME_MASK;
}


//-----------------------------------------------------------------------------
// Add emitter operators
//-----------------------------------------------------------------------------
void AddBuiltInParticleEmitters( void )
{
	REGISTER_PARTICLE_OPERATOR( FUNCTION_EMITTER, C_OP_ContinuousEmitter );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_EMITTER, C_OP_InstantaneousEmitter );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_EMITTER, C_OP_NoiseEmitter );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_EMITTER, C_OP_MaintainEmitter );
}
