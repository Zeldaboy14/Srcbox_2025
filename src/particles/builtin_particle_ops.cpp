//===== Copyright (c) Valve Corporation, All rights reserved. ======//
//
// Purpose: particle system operators
//
// Reconstructed from the Alien Swarm SDK particles.lib (builtin_particle_ops.obj).
//
//===========================================================================//

#include "tier0/platform.h"
#include "particles/particles.h"
#include "filesystem.h"
#include "tier2/tier2.h"
#include "tier2/fileutils.h"
#include "tier2/renderutils.h"
#include "tier1/UtlStringMap.h"
#include "tier1/strtools.h"
#include "bspflags.h"
#include "const.h"
#include "mathlib/ssemath.h"
#include "mathlib/noise.h"
#include "mathlib/vmatrix.h"
#include "particles_internal.h"
#include "bitmap/psheet.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"


// The shipped build samples the clock around every sub-operator call but discards the timings
#define START_OP float flOpStartTime = Plat_FloatTime(); NOTE_UNUSED( flOpStartTime );
#define END_OP if ( 1 ) { float flETime = Plat_FloatTime() - flOpStartTime; NOTE_UNUSED( flETime ); }

#define MAX_CONSTRAINTS 100


//-----------------------------------------------------------------------------
// Standard movement operator
//-----------------------------------------------------------------------------
class C_OP_BasicMovement : public CParticleOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_OP_BasicMovement );

	uint32 GetWrittenAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_XYZ_MASK | PARTICLE_ATTRIBUTE_PREV_XYZ_MASK;
	}

	uint32 GetReadAttributes( void ) const
	{
		return 0;
	}

	virtual void Operate( CParticleCollection *pParticles, float flStrength, void *pContext ) const;

	Vector m_Gravity;
	float m_fDrag;
	int m_nMaxConstraintPasses;
};

DEFINE_PARTICLE_OPERATOR( C_OP_BasicMovement, "Movement Basic", OPERATOR_GENERIC );

BEGIN_PARTICLE_OPERATOR_UNPACK( C_OP_BasicMovement )
	DMXELEMENT_UNPACK_FIELD( "gravity", "0 0 0", Vector, m_Gravity )
	DMXELEMENT_UNPACK_FIELD( "drag", "0", float, m_fDrag )
	DMXELEMENT_UNPACK_FIELD( "max constraint passes", "3", int, m_nMaxConstraintPasses )
END_PARTICLE_OPERATOR_UNPACK( C_OP_BasicMovement )

void C_OP_BasicMovement::Operate( CParticleCollection *pParticles, float flStrength, void *pContext ) const
{
	C4VAttributeWriteIterator prev_xyz( PARTICLE_ATTRIBUTE_PREV_XYZ, pParticles );
	C4VAttributeWriteIterator xyz( PARTICLE_ATTRIBUTE_XYZ, pParticles );

	// ExponentialDecay( 1 - drag, 1/30, dt ), with the operations in the order the shipped build used
	float flLogDecay = (float)log( (double)( 1.0f - MAX( 0.0f, m_fDrag ) ) );
	float flDecay = (float)exp( (double)( ( flLogDecay * pParticles->m_flDt ) * ( 1.0f / ( 1.0f / 30.0f ) ) ) );
	fltx4 adj_dt = ReplicateX4( flDecay * ( pParticles->m_flDt / pParticles->m_flPreviousDt ) );

	size_t nForceStride = 0;
	Vector acc = m_Gravity;
	fltx4 accFactorX = ReplicateX4( acc.x );
	fltx4 accFactorY = ReplicateX4( acc.y );
	fltx4 accFactorZ = ReplicateX4( acc.z );

	int nAccumulators = pParticles->m_pDef->m_ForceGenerators.Count();

	FourVectors PerParticleForceAccumulator[MAX_PARTICLES_IN_A_SYSTEM / 4];

	FourVectors *pAccOut = PerParticleForceAccumulator;
	if ( nAccumulators )
	{
		// we have forces, so we need to fill the accumulator
		nForceStride = 1;
		int nblocks = pParticles->m_nPaddedActiveParticles;
		for( int i = 0; i < nblocks; i++ )
		{
			pAccOut->x = accFactorX;
			pAccOut->y = accFactorY;
			pAccOut->z = accFactorZ;
			pAccOut++;
		}

		for( int i = 0; i < nAccumulators; i++ )
		{
			CParticleOperatorInstance *pOp = pParticles->m_pDef->m_ForceGenerators[i];
			float flOpStrength;
			if ( !pParticles->CheckIfOperatorShouldRun( pOp, &flOpStrength ) )
				continue;
			START_OP;
			pParticles->m_pDef->m_ForceGenerators[i]->AddForces(
				PerParticleForceAccumulator,
				pParticles,
				nblocks,
				flOpStrength,
				pParticles->m_pOperatorContextData + pParticles->m_pDef->m_nForceGeneratorsCtxOffsets[i] );
			END_OP;
		}
	}
	else
	{
		// we just have gravity
		pAccOut->x = accFactorX;
		pAccOut->y = accFactorY;
		pAccOut->z = accFactorZ;
	}

	fltx4 DtSquared = ReplicateX4( pParticles->m_flDt * pParticles->m_flDt );
	int ctr = pParticles->m_nPaddedActiveParticles;
	FourVectors *pAccIn = PerParticleForceAccumulator;
	do
	{
		accFactorX = MulSIMD( pAccIn->x, DtSquared );
		accFactorY = MulSIMD( pAccIn->y, DtSquared );
		accFactorZ = MulSIMD( pAccIn->z, DtSquared );

		// we will write prev xyz, and swap prev and cur at the end
		prev_xyz->x = AddSIMD( AddSIMD( MulSIMD( SubSIMD( xyz->x, prev_xyz->x ), adj_dt ), accFactorX ), xyz->x );
		prev_xyz->y = AddSIMD( AddSIMD( MulSIMD( SubSIMD( xyz->y, prev_xyz->y ), adj_dt ), accFactorY ), xyz->y );
		prev_xyz->z = AddSIMD( AddSIMD( MulSIMD( SubSIMD( xyz->z, prev_xyz->z ), adj_dt ), accFactorZ ), xyz->z );
		++prev_xyz;
		++xyz;
		pAccIn += nForceStride;
	} while ( --ctr );

	pParticles->SwapPosAndPrevPos();

	// now, enforce constraints
	int nConstraints = pParticles->m_pDef->m_Constraints.Count();
	if ( nConstraints && pParticles->m_nPaddedActiveParticles )
	{
		bool bConstraintSatisfied[ MAX_CONSTRAINTS ];
		bool bFinalConstraint[ MAX_CONSTRAINTS ];
		for( int i = 0; i < nConstraints; i++ )
		{
			bFinalConstraint[i] = pParticles->m_pDef->m_Constraints[i]->IsFinalConstaint();

			bConstraintSatisfied[i] = false;
			pParticles->m_pDef->m_Constraints[i]->SetupConstraintPerFrameData(
				pParticles, pParticles->m_pOperatorContextData +
				pParticles->m_pDef->m_nConstraintsCtxOffsets[i] );
		}

		// constraints get to see their own per psystem per op random #s
		for( int p = 0; p < m_nMaxConstraintPasses; p++ )
		{
			for( int i = 0; i < nConstraints; i++ )
			{
				if ( !bConstraintSatisfied[i] )
				{
					CParticleOperatorInstance *pOp = pParticles->m_pDef->m_Constraints[i];

					bConstraintSatisfied[i] = true;
					float flOpStrength;
					if ( ( !bFinalConstraint[i] ) && ( pParticles->CheckIfOperatorShouldRun( pOp, &flOpStrength ) ) )
					{
						START_OP;
						bool bDidSomething = pOp->EnforceConstraint(
							0, pParticles->m_nPaddedActiveParticles, pParticles,
							pParticles->m_pOperatorContextData +
							pParticles->m_pDef->m_nConstraintsCtxOffsets[i],
							pParticles->m_nActiveParticles );
						END_OP;
						if ( bDidSomething )
						{
							// other constraints now not satisfied, maybe
							for( int j = 0; j < nConstraints; j++ )
							{
								if ( i != j )
								{
									bConstraintSatisfied[ j ] = false;
								}
							}
						}
					}
				}
			}
		}

		// run the final constraints
		for( int i = 0; i < nConstraints; i++ )
		{
			CParticleOperatorInstance *pOp = pParticles->m_pDef->m_Constraints[i];
			float flOpStrength;
			if ( bFinalConstraint[i] && pParticles->CheckIfOperatorShouldRun( pOp, &flOpStrength ) )
			{
				START_OP;
				pOp->EnforceConstraint(
					0, pParticles->m_nPaddedActiveParticles, pParticles,
					pParticles->m_pOperatorContextData +
					pParticles->m_pDef->m_nConstraintsCtxOffsets[i],
					pParticles->m_nActiveParticles );
				END_OP;
			}
		}
	}
}


//-----------------------------------------------------------------------------
// Fade and kill operator
//-----------------------------------------------------------------------------
class C_OP_FadeAndKill : public CParticleOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_OP_FadeAndKill );

	uint32 GetWrittenAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_ALPHA_MASK;
	}

	uint32 GetReadAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_LIFE_DURATION_MASK | PARTICLE_ATTRIBUTE_CREATION_TIME_MASK;
	}

	uint32 GetReadInitialAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_ALPHA_MASK;
	}

	virtual uint32 GetFilter( void ) const
	{
		return FILTER_COLOR_AND_OPACITY_MASK | FILTER_LIFE_DURATION_MASK;
	}

	virtual void InitParams( CParticleSystemDefinition *pDef );

	virtual void Operate( CParticleCollection *pParticles, float flStrength, void *pContext ) const;

	float m_flStartFadeInTime;
	float m_flEndFadeInTime;
	float m_flStartFadeOutTime;
	float m_flEndFadeOutTime;
	float m_flStartAlpha;
	float m_flEndAlpha;
};

DEFINE_PARTICLE_OPERATOR( C_OP_FadeAndKill, "Alpha Fade and Decay", OPERATOR_GENERIC );

BEGIN_PARTICLE_OPERATOR_UNPACK( C_OP_FadeAndKill )
	DMXELEMENT_UNPACK_FIELD( "start_alpha", "1", float, m_flStartAlpha )
	DMXELEMENT_UNPACK_FIELD( "end_alpha", "0", float, m_flEndAlpha )
	DMXELEMENT_UNPACK_FIELD( "start_fade_in_time", "0", float, m_flStartFadeInTime )
	DMXELEMENT_UNPACK_FIELD( "end_fade_in_time", "0.5", float, m_flEndFadeInTime )
	DMXELEMENT_UNPACK_FIELD( "start_fade_out_time", "0.5", float, m_flStartFadeOutTime )
	DMXELEMENT_UNPACK_FIELD( "end_fade_out_time", "1", float, m_flEndFadeOutTime )
END_PARTICLE_OPERATOR_UNPACK( C_OP_FadeAndKill )

void C_OP_FadeAndKill::InitParams( CParticleSystemDefinition *pDef )
{
	// Cache off and validate values
	if ( m_flEndFadeInTime < m_flStartFadeInTime )
	{
		m_flEndFadeInTime = m_flStartFadeInTime;
	}
	if ( m_flEndFadeOutTime < m_flStartFadeOutTime )
	{
		m_flEndFadeOutTime = m_flStartFadeOutTime;
	}

	if ( m_flStartFadeOutTime < m_flStartFadeInTime )
	{
		V_swap( m_flStartFadeInTime, m_flStartFadeOutTime );
	}

	if ( m_flEndFadeOutTime < m_flEndFadeInTime )
	{
		V_swap( m_flEndFadeInTime, m_flEndFadeOutTime );
	}
}

void C_OP_FadeAndKill::Operate( CParticleCollection *pParticles, float flStrength, void *pContext ) const
{
	CM128AttributeIterator pCreationTime( PARTICLE_ATTRIBUTE_CREATION_TIME, pParticles );
	CM128AttributeIterator pLifeDuration( PARTICLE_ATTRIBUTE_LIFE_DURATION, pParticles );
	CM128InitialAttributeIterator pInitialAlpha( PARTICLE_ATTRIBUTE_ALPHA, pParticles );
	CM128AttributeWriteIterator pAlpha( PARTICLE_ATTRIBUTE_ALPHA, pParticles );

	fltx4 fl4StartFadeInTime = ReplicateX4( m_flStartFadeInTime );
	fltx4 fl4StartFadeOutTime = ReplicateX4( m_flStartFadeOutTime );
	fltx4 fl4EndFadeInTime = ReplicateX4( m_flEndFadeInTime );
	fltx4 fl4EndFadeOutTime = ReplicateX4( m_flEndFadeOutTime );
	fltx4 fl4EndAlpha = ReplicateX4( m_flEndAlpha );
	fltx4 fl4StartAlpha = ReplicateX4( m_flStartAlpha );

	fltx4 fl4CurTime = pParticles->m_fl4CurTime;
	int nLimit = pParticles->m_nPaddedActiveParticles << 2;

	fltx4 fl4FadeInDuration = ReplicateX4( m_flEndFadeInTime - m_flStartFadeInTime );
	fltx4 fl4OOFadeInDuration = ReciprocalEstSIMD( fl4FadeInDuration );

	fltx4 fl4FadeOutDuration = ReplicateX4( m_flEndFadeOutTime - m_flStartFadeOutTime );
	fltx4 fl4OOFadeOutDuration = ReciprocalEstSIMD( fl4FadeOutDuration );

	for ( int i = 0; i < nLimit; i+= 4 )
	{
		fltx4 fl4Age = SubSIMD( fl4CurTime, *pCreationTime );
		fltx4 fl4ParticleLifeTime = *pLifeDuration;
		fltx4 fl4KillMask = CmpGeSIMD( fl4Age, *pLifeDuration );					// takes care of lifeduration = 0 div 0
		fl4Age = MulSIMD( fl4Age, ReciprocalEstSIMD( fl4ParticleLifeTime ) );	// age 0..1
		fltx4 fl4FadingInMask = AndNotSIMD( fl4KillMask,
			AndSIMD( CmpLeSIMD( fl4StartFadeInTime, fl4Age ), CmpLtSIMD( fl4Age, fl4EndFadeInTime ) ) );
		fltx4 fl4FadingOutMask = AndNotSIMD( fl4KillMask,
			AndSIMD( CmpLeSIMD( fl4StartFadeOutTime, fl4Age ), CmpLtSIMD( fl4Age, fl4EndFadeOutTime ) ) );
		if ( IsAnyNegative( fl4FadingInMask ) )
		{
			fltx4 fl4Goal = MulSIMD( *pInitialAlpha, fl4StartAlpha );
			fltx4 fl4NewAlpha = SimpleSplineRemapValWithDeltasClamped( fl4Age, fl4StartFadeInTime, fl4FadeInDuration, fl4OOFadeInDuration,
				fl4Goal, SubSIMD( *pInitialAlpha, fl4Goal ) );
			*pAlpha = MaskedAssign( fl4FadingInMask, fl4NewAlpha, *pAlpha );
		}
		if ( IsAnyNegative( fl4FadingOutMask ) )
		{
			fltx4 fl4Goal = MulSIMD( *pInitialAlpha, fl4EndAlpha );
			fltx4 fl4NewAlpha = SimpleSplineRemapValWithDeltasClamped( fl4Age, fl4StartFadeOutTime, fl4FadeOutDuration, fl4OOFadeOutDuration,
				*pInitialAlpha, SubSIMD( fl4Goal, *pInitialAlpha ) );
			*pAlpha = MaskedAssign( fl4FadingOutMask, fl4NewAlpha, *pAlpha );
		}
		if ( IsAnyNegative( fl4KillMask ) )
		{
			int nMask = TestSignSIMD( fl4KillMask );
			if ( nMask & 1 )
				pParticles->KillParticle( i );
			if ( nMask & 2 )
				pParticles->KillParticle( i + 1 );
			if ( nMask & 4 )
				pParticles->KillParticle( i + 2 );
			if ( nMask & 8 )
				pParticles->KillParticle( i + 3 );
		}
		++pCreationTime;
		++pLifeDuration;
		++pInitialAlpha;
		++pAlpha;
	}
}


//-----------------------------------------------------------------------------
// Fade and kill operator for tracers
//-----------------------------------------------------------------------------
class C_OP_FadeAndKillForTracers : public CParticleOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_OP_FadeAndKillForTracers );

	uint32 GetWrittenAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_ALPHA_MASK | PARTICLE_ATTRIBUTE_XYZ_MASK | PARTICLE_ATTRIBUTE_TRAIL_LENGTH_MASK;
	}

	uint32 GetReadAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_LIFE_DURATION_MASK | PARTICLE_ATTRIBUTE_CREATION_TIME_MASK |
			PARTICLE_ATTRIBUTE_XYZ_MASK | PARTICLE_ATTRIBUTE_PREV_XYZ_MASK | PARTICLE_ATTRIBUTE_TRAIL_LENGTH_MASK;
	}

	uint32 GetReadInitialAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_ALPHA_MASK;
	}

	virtual uint32 GetFilter( void ) const
	{
		return FILTER_COLOR_AND_OPACITY_MASK | FILTER_LIFE_DURATION_MASK;
	}

	virtual void InitParams( CParticleSystemDefinition *pDef );

	virtual void Operate( CParticleCollection *pParticles, float flStrength, void *pContext ) const;

	float m_flStartFadeInTime;
	float m_flEndFadeInTime;
	float m_flStartFadeOutTime;
	float m_flEndFadeOutTime;
	float m_flStartAlpha;
	float m_flEndAlpha;
};

DEFINE_PARTICLE_OPERATOR( C_OP_FadeAndKillForTracers, "Alpha Fade and Decay for Tracers", OPERATOR_GENERIC );

BEGIN_PARTICLE_OPERATOR_UNPACK( C_OP_FadeAndKillForTracers )
	DMXELEMENT_UNPACK_FIELD( "start_alpha", "1", float, m_flStartAlpha )
	DMXELEMENT_UNPACK_FIELD( "end_alpha", "0", float, m_flEndAlpha )
	DMXELEMENT_UNPACK_FIELD( "start_fade_in_time", "0", float, m_flStartFadeInTime )
	DMXELEMENT_UNPACK_FIELD( "end_fade_in_time", "0.5", float, m_flEndFadeInTime )
	DMXELEMENT_UNPACK_FIELD( "start_fade_out_time", "0.5", float, m_flStartFadeOutTime )
	DMXELEMENT_UNPACK_FIELD( "end_fade_out_time", "1", float, m_flEndFadeOutTime )
END_PARTICLE_OPERATOR_UNPACK( C_OP_FadeAndKillForTracers )

void C_OP_FadeAndKillForTracers::InitParams( CParticleSystemDefinition *pDef )
{
	// Cache off and validate values
	if ( m_flEndFadeInTime < m_flStartFadeInTime )
	{
		m_flEndFadeInTime = m_flStartFadeInTime;
	}
	if ( m_flEndFadeOutTime < m_flStartFadeOutTime )
	{
		m_flEndFadeOutTime = m_flStartFadeOutTime;
	}

	if ( m_flStartFadeOutTime < m_flStartFadeInTime )
	{
		V_swap( m_flStartFadeInTime, m_flStartFadeOutTime );
	}

	if ( m_flEndFadeOutTime < m_flEndFadeInTime )
	{
		V_swap( m_flEndFadeInTime, m_flEndFadeOutTime );
	}
}

void C_OP_FadeAndKillForTracers::Operate( CParticleCollection *pParticles, float flStrength, void *pContext ) const
{
	CM128AttributeIterator pCreationTime( PARTICLE_ATTRIBUTE_CREATION_TIME, pParticles );
	CM128AttributeIterator pLifeDuration( PARTICLE_ATTRIBUTE_LIFE_DURATION, pParticles );
	CM128InitialAttributeIterator pInitialAlpha( PARTICLE_ATTRIBUTE_ALPHA, pParticles );
	CM128AttributeWriteIterator pAlpha( PARTICLE_ATTRIBUTE_ALPHA, pParticles );

	fltx4 fl4StartFadeInTime = ReplicateX4( m_flStartFadeInTime );
	fltx4 fl4StartFadeOutTime = ReplicateX4( m_flStartFadeOutTime );
	fltx4 fl4EndFadeInTime = ReplicateX4( m_flEndFadeInTime );
	fltx4 fl4EndFadeOutTime = ReplicateX4( m_flEndFadeOutTime );
	fltx4 fl4EndAlpha = ReplicateX4( m_flEndAlpha );
	fltx4 fl4StartAlpha = ReplicateX4( m_flStartAlpha );

	fltx4 fl4CurTime = pParticles->m_fl4CurTime;
	int nLimit = pParticles->m_nPaddedActiveParticles << 2;

	fltx4 fl4FadeInDuration = ReplicateX4( m_flEndFadeInTime - m_flStartFadeInTime );
	fltx4 fl4OOFadeInDuration = ReciprocalEstSIMD( fl4FadeInDuration );

	fltx4 fl4FadeOutDuration = ReplicateX4( m_flEndFadeOutTime - m_flStartFadeOutTime );
	fltx4 fl4OOFadeOutDuration = ReciprocalEstSIMD( fl4FadeOutDuration );

	for ( int i = 0; i < nLimit; i+= 4 )
	{
		fltx4 fl4Age = SubSIMD( fl4CurTime, *pCreationTime );
		fltx4 fl4ParticleLifeTime = *pLifeDuration;
		fltx4 fl4KillMask = CmpGeSIMD( fl4Age, *pLifeDuration );					// takes care of lifeduration = 0 div 0
		fl4Age = MulSIMD( fl4Age, ReciprocalEstSIMD( fl4ParticleLifeTime ) );	// age 0..1
		fltx4 fl4FadingInMask = AndNotSIMD( fl4KillMask,
			AndSIMD( CmpLeSIMD( fl4StartFadeInTime, fl4Age ), CmpLtSIMD( fl4Age, fl4EndFadeInTime ) ) );
		fltx4 fl4FadingOutMask = AndNotSIMD( fl4KillMask,
			AndSIMD( CmpLeSIMD( fl4StartFadeOutTime, fl4Age ), CmpLtSIMD( fl4Age, fl4EndFadeOutTime ) ) );
		if ( IsAnyNegative( fl4FadingInMask ) )
		{
			fltx4 fl4Goal = MulSIMD( *pInitialAlpha, fl4StartAlpha );
			fltx4 fl4NewAlpha = SimpleSplineRemapValWithDeltasClamped( fl4Age, fl4StartFadeInTime, fl4FadeInDuration, fl4OOFadeInDuration,
				fl4Goal, SubSIMD( *pInitialAlpha, fl4Goal ) );
			*pAlpha = MaskedAssign( fl4FadingInMask, fl4NewAlpha, *pAlpha );
		}
		if ( IsAnyNegative( fl4FadingOutMask ) )
		{
			fltx4 fl4Goal = MulSIMD( *pInitialAlpha, fl4EndAlpha );
			fltx4 fl4NewAlpha = SimpleSplineRemapValWithDeltasClamped( fl4Age, fl4StartFadeOutTime, fl4FadeOutDuration, fl4OOFadeOutDuration,
				*pInitialAlpha, SubSIMD( fl4Goal, *pInitialAlpha ) );
			*pAlpha = MaskedAssign( fl4FadingOutMask, fl4NewAlpha, *pAlpha );
		}
		if ( IsAnyNegative( fl4KillMask ) )
		{
			// Pull particles which died during this step back to the point where they expired
			fltx4 fl4PrevAge = SubSIMD( ReplicateX4( pParticles->m_flCurTime - pParticles->m_flDt ), *pCreationTime );
			fltx4 fl4LifeDuration = *pLifeDuration;
			int nMask = TestSignSIMD( fl4KillMask ) & TestSignSIMD( CmpLeSIMD( fl4LifeDuration, fl4PrevAge ) );
			fltx4 fl4DiedThisFrameMask = AndSIMD( CmpLtSIMD( fl4PrevAge, fl4ParticleLifeTime ), fl4KillMask );
			fltx4 fl4LerpFactor = DivSIMD( SubSIMD( fl4LifeDuration, fl4PrevAge ), ReplicateX4( pParticles->m_flDt ) );

			// NOTE: shipped code builds these iterators here, so it always adjusts the first block of particles
			C4VAttributeWriteIterator pXYZ( PARTICLE_ATTRIBUTE_XYZ, pParticles );
			C4VAttributeIterator pPrevXYZ( PARTICLE_ATTRIBUTE_PREV_XYZ, pParticles );
			CM128AttributeWriteIterator pTrailLength( PARTICLE_ATTRIBUTE_TRAIL_LENGTH, pParticles );
			FourVectors v4NewXYZ;
			v4NewXYZ.x = AddSIMD( MulSIMD( SubSIMD( pXYZ->x, pPrevXYZ->x ), fl4LerpFactor ), pPrevXYZ->x );
			v4NewXYZ.y = AddSIMD( MulSIMD( SubSIMD( pXYZ->y, pPrevXYZ->y ), fl4LerpFactor ), pPrevXYZ->y );
			v4NewXYZ.z = AddSIMD( MulSIMD( SubSIMD( pXYZ->z, pPrevXYZ->z ), fl4LerpFactor ), pPrevXYZ->z );
			fltx4 fl4OldTrailLength = *pTrailLength;
			pXYZ->x = MaskedAssign( fl4DiedThisFrameMask, v4NewXYZ.x, pXYZ->x );
			pXYZ->y = MaskedAssign( fl4DiedThisFrameMask, v4NewXYZ.y, pXYZ->y );
			pXYZ->z = MaskedAssign( fl4DiedThisFrameMask, v4NewXYZ.z, pXYZ->z );
			*pTrailLength = MaskedAssign( fl4DiedThisFrameMask, MulSIMD( fl4OldTrailLength, fl4LerpFactor ), fl4OldTrailLength );

			if ( nMask & 1 )
				pParticles->KillParticle( i );
			if ( nMask & 2 )
				pParticles->KillParticle( i + 1 );
			if ( nMask & 4 )
				pParticles->KillParticle( i + 2 );
			if ( nMask & 8 )
				pParticles->KillParticle( i + 3 );
		}
		++pCreationTime;
		++pLifeDuration;
		++pInitialAlpha;
		++pAlpha;
	}
}



//-----------------------------------------------------------------------------
// Fade In
//-----------------------------------------------------------------------------
class C_OP_FadeIn : public CParticleOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_OP_FadeIn );

	uint32 GetWrittenAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_ALPHA_MASK;
	}

	uint32 GetReadAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_LIFE_DURATION_MASK | PARTICLE_ATTRIBUTE_CREATION_TIME_MASK | PARTICLE_ATTRIBUTE_PARTICLE_ID_MASK;
	}

	uint32 GetReadInitialAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_ALPHA_MASK;
	}

	template<bool bRandomize, bool bProportional> void OperateInternal( CParticleCollection *pParticles, float flStrength, void *pContext ) const;
	template<bool bRandomize> void OperateInternal( CParticleCollection *pParticles, float flStrength, void *pContext ) const;

	virtual void Operate( CParticleCollection *pParticles, float flStrength, void *pContext ) const;

	float m_flFadeInTimeMin;
	float m_flFadeInTimeMax;
	float m_flFadeInTimeExp;
	bool m_bProportional;
};

DEFINE_PARTICLE_OPERATOR( C_OP_FadeIn, "Alpha Fade In Random", OPERATOR_GENERIC );

BEGIN_PARTICLE_OPERATOR_UNPACK( C_OP_FadeIn )
	DMXELEMENT_UNPACK_FIELD( "fade in time min", ".25", float, m_flFadeInTimeMin )
	DMXELEMENT_UNPACK_FIELD( "fade in time max", ".25", float, m_flFadeInTimeMax )
	DMXELEMENT_UNPACK_FIELD( "fade in time exponent", "1", float, m_flFadeInTimeExp )
	DMXELEMENT_UNPACK_FIELD( "proportional 0/1", "1", bool, m_bProportional )
END_PARTICLE_OPERATOR_UNPACK( C_OP_FadeIn )

template<bool bRandomize, bool bProportional>
void C_OP_FadeIn::OperateInternal( CParticleCollection *pParticles, float flStrength, void *pContext ) const
{
	CM128AttributeIterator pCreationTime( PARTICLE_ATTRIBUTE_CREATION_TIME, pParticles );
	CM128AttributeIterator pLifeDuration( PARTICLE_ATTRIBUTE_LIFE_DURATION, pParticles );
	CM128InitialAttributeIterator pInitialAlpha( PARTICLE_ATTRIBUTE_ALPHA, pParticles );
	CM128AttributeWriteIterator pAlpha( PARTICLE_ATTRIBUTE_ALPHA, pParticles );
	C4IAttributeIterator pParticleID( PARTICLE_ATTRIBUTE_PARTICLE_ID, pParticles );

	fltx4 fl4CurTime = pParticles->m_fl4CurTime;
	int nRandomOffset = pParticles->OperatorRandomSampleOffset();
	int nCtr = pParticles->m_nPaddedActiveParticles;

	fltx4 fl4FadeTimeMin = ReplicateX4( m_flFadeInTimeMin );
	fltx4 fl4FadeTimeWidth = ReplicateX4( m_flFadeInTimeMax - m_flFadeInTimeMin );
	int nSSEFixedExponent = m_flFadeInTimeExp * 4.0f;

	do
	{
		fltx4 fl4Age = SubSIMD( fl4CurTime, *pCreationTime );
		fltx4 fl4FadeInTime;
		if ( bRandomize )
		{
			fl4FadeInTime = AddSIMD( MulSIMD( Pow_FixedPoint_Exponent_SIMD( pParticles->RandomFloat( *pParticleID, nRandomOffset ), nSSEFixedExponent ), fl4FadeTimeWidth ), fl4FadeTimeMin );
		}
		else
		{
			fl4FadeInTime = fl4FadeTimeMin;
		}

		if ( bProportional )
		{
			fl4Age = MulSIMD( fl4Age, ReciprocalEstSIMD( *pLifeDuration ) );
			fl4Age = MaxSIMD( Four_Zeros, MinSIMD( Four_Ones, fl4Age ) );
		}

		fltx4 fl4FadingMask = CmpLtSIMD( fl4Age, fl4FadeInTime );
		if ( IsAnyNegative( fl4FadingMask ) )
		{
			fltx4 fl4NewAlpha = SimpleSplineRemapValWithDeltasClamped( fl4Age, Four_Zeros, fl4FadeInTime, ReciprocalEstSIMD( fl4FadeInTime ),
				Four_Zeros, *pInitialAlpha );
			*pAlpha = MaskedAssign( fl4FadingMask, fl4NewAlpha, *pAlpha );
		}
		++pCreationTime;
		++pLifeDuration;
		++pInitialAlpha;
		++pAlpha;
		++pParticleID;
	} while( --nCtr );
}

template<bool bRandomize>
void C_OP_FadeIn::OperateInternal( CParticleCollection *pParticles, float flStrength, void *pContext ) const
{
	if ( m_bProportional )
		OperateInternal<bRandomize, true>( pParticles, flStrength, pContext );
	else
		OperateInternal<bRandomize, false>( pParticles, flStrength, pContext );
}

void C_OP_FadeIn::Operate( CParticleCollection *pParticles, float flStrength, void *pContext ) const
{
	if ( m_flFadeInTimeMin != m_flFadeInTimeMax )
		OperateInternal<true>( pParticles, flStrength, pContext );
	else
		OperateInternal<false>( pParticles, flStrength, pContext );
}


//-----------------------------------------------------------------------------
// Fade Out
//-----------------------------------------------------------------------------
class C_OP_FadeOut : public CParticleOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_OP_FadeOut );

	uint32 GetWrittenAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_ALPHA_MASK;
	}

	uint32 GetReadAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_LIFE_DURATION_MASK | PARTICLE_ATTRIBUTE_CREATION_TIME_MASK | PARTICLE_ATTRIBUTE_PARTICLE_ID_MASK;
	}

	uint32 GetReadInitialAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_ALPHA_MASK;
	}

	template<bool bRandomize, bool bProportional, bool bApplyBias> void OperateInternal( CParticleCollection *pParticles, float flStrength, void *pContext ) const;

	virtual void Operate( CParticleCollection *pParticles, float flStrength, void *pContext ) const;

	virtual void InitParams( CParticleSystemDefinition *pDef );

	float m_flFadeOutTimeMin;
	float m_flFadeOutTimeMax;
	float m_flFadeOutTimeExp;
	float m_flFadeBias;
	fltx4 m_fl4BiasParam;
	bool m_bProportional;
	bool m_bEaseInAndOut;
	bool m_bRandomize;
	void ( C_OP_FadeOut::*m_pOpFunction )( CParticleCollection *pParticles, float flStrength, void *pContext ) const;
};

DEFINE_PARTICLE_OPERATOR( C_OP_FadeOut, "Alpha Fade Out Random", OPERATOR_GENERIC );

BEGIN_PARTICLE_OPERATOR_UNPACK( C_OP_FadeOut )
	DMXELEMENT_UNPACK_FIELD( "fade out time min", ".25", float, m_flFadeOutTimeMin )
	DMXELEMENT_UNPACK_FIELD( "fade out time max", ".25", float, m_flFadeOutTimeMax )
	DMXELEMENT_UNPACK_FIELD( "fade out time exponent", "1", float, m_flFadeOutTimeExp )
	DMXELEMENT_UNPACK_FIELD( "proportional 0/1", "1", bool, m_bProportional )
	DMXELEMENT_UNPACK_FIELD( "ease in and out", "1", bool, m_bEaseInAndOut )
	DMXELEMENT_UNPACK_FIELD( "fade bias", "0.5", float, m_flFadeBias )
END_PARTICLE_OPERATOR_UNPACK( C_OP_FadeOut )

template<bool bRandomize, bool bProportional, bool bApplyBias>
void C_OP_FadeOut::OperateInternal( CParticleCollection *pParticles, float flStrength, void *pContext ) const
{
	CM128AttributeIterator pCreationTime( PARTICLE_ATTRIBUTE_CREATION_TIME, pParticles );
	CM128AttributeIterator pLifeDuration( PARTICLE_ATTRIBUTE_LIFE_DURATION, pParticles );
	CM128InitialAttributeIterator pInitialAlpha( PARTICLE_ATTRIBUTE_ALPHA, pParticles );
	CM128AttributeWriteIterator pAlpha( PARTICLE_ATTRIBUTE_ALPHA, pParticles );
	C4IAttributeIterator pParticleID( PARTICLE_ATTRIBUTE_PARTICLE_ID, pParticles );

	fltx4 fl4CurTime = pParticles->m_fl4CurTime;
	int nCtr = pParticles->m_nPaddedActiveParticles;

	fltx4 FadeTimeMin = ReplicateX4( m_flFadeOutTimeMin );
	fltx4 FadeTimeWidth = ReplicateX4( m_flFadeOutTimeMax - m_flFadeOutTimeMin );
	int nSSEFixedExponent = m_flFadeOutTimeExp * 4.0f;
	int nRandomOffset = pParticles->OperatorRandomSampleOffset();

	do
	{
		fltx4 fl4FadeOutTime;
		if ( bRandomize )
		{
			fl4FadeOutTime = AddSIMD( MulSIMD( Pow_FixedPoint_Exponent_SIMD( pParticles->RandomFloat( *pParticleID, nRandomOffset ), nSSEFixedExponent ), FadeTimeWidth ), FadeTimeMin );
		}
		else
		{
			fl4FadeOutTime = FadeTimeMin;
		}

		fltx4 fl4Age = SubSIMD( fl4CurTime, *pCreationTime );
		fltx4 fl4Lifespan;
		fltx4 fl4FadeOutStart;
		if ( bProportional )
		{
			fl4Age = MulSIMD( fl4Age, ReciprocalEstSIMD( *pLifeDuration ) );
			fl4Lifespan = Four_Ones;
			fl4FadeOutStart = SubSIMD( Four_Ones, fl4FadeOutTime );
		}
		else
		{
			fl4Lifespan = *pLifeDuration;
			fl4FadeOutStart = SubSIMD( fl4Lifespan, fl4FadeOutTime );
		}

		fltx4 fl4FadeMask = CmpLtSIMD( fl4FadeOutStart, fl4Age );
		if ( IsAnyNegative( fl4FadeMask ) )
		{
			fltx4 fl4FadeOutDuration = SubSIMD( fl4Lifespan, fl4FadeOutStart );
			fltx4 fl4Alpha;
			if ( m_bEaseInAndOut )
			{
				fl4Alpha = SimpleSplineRemapValWithDeltasClamped( fl4Age, fl4FadeOutStart, fl4FadeOutDuration, ReciprocalEstSIMD( fl4FadeOutDuration ),
					*pInitialAlpha, SubSIMD( Four_Zeros, *pInitialAlpha ) );
				fl4Alpha = MaxSIMD( Four_Zeros, fl4Alpha );
			}
			else
			{
				fltx4 fl4Frac = MulSIMD( SubSIMD( fl4Age, fl4FadeOutStart ), ReciprocalEstSIMD( fl4FadeOutDuration ) );
				fl4Frac = MinSIMD( Four_Ones, MaxSIMD( Four_Zeros, fl4Frac ) );
				if ( bApplyBias )
				{
					fl4Frac = BiasSIMD( fl4Frac, m_fl4BiasParam );
				}
				fl4Alpha = MulSIMD( SubSIMD( Four_Ones, fl4Frac ), *pInitialAlpha );
			}
			*pAlpha = MaskedAssign( fl4FadeMask, fl4Alpha, *pAlpha );
		}
		++pCreationTime;
		++pLifeDuration;
		++pInitialAlpha;
		++pAlpha;
		++pParticleID;
	} while( --nCtr );
}

void C_OP_FadeOut::Operate( CParticleCollection *pParticles, float flStrength, void *pContext ) const
{
	( this->*m_pOpFunction )( pParticles, flStrength, pContext );
}

void C_OP_FadeOut::InitParams( CParticleSystemDefinition *pDef )
{
	float flBias = ( m_flFadeBias != 0.0f ) ? m_flFadeBias : 0.5f;
	m_fl4BiasParam = PreCalcBiasParameter( ReplicateX4( flBias ) );
	m_bRandomize = ( m_flFadeOutTimeMin != m_flFadeOutTimeMax );
	if ( m_bRandomize && ( m_flFadeOutTimeMin == 0.0f ) )
	{
		m_flFadeOutTimeMin = m_flFadeOutTimeMax = FLT_EPSILON;
	}

	static void ( C_OP_FadeOut::*s_pDispatchTable[8] )( CParticleCollection *pParticles, float flStrength, void *pContext ) const =
	{
		&C_OP_FadeOut::OperateInternal<false, false, false>,
		&C_OP_FadeOut::OperateInternal<false, false, true>,
		&C_OP_FadeOut::OperateInternal<false, true, false>,
		&C_OP_FadeOut::OperateInternal<false, true, true>,
		&C_OP_FadeOut::OperateInternal<true, false, false>,
		&C_OP_FadeOut::OperateInternal<true, false, true>,
		&C_OP_FadeOut::OperateInternal<true, true, false>,
		&C_OP_FadeOut::OperateInternal<true, true, true>,
	};

	// NOTE: shipped code selects the biased variants when the bias is exactly 0.5
	int nBiasIndex = ( m_flFadeBias == 0.5f ) ? 1 : 0;
	int nIndex = nBiasIndex + 2 * ( ( m_bProportional ? 1 : 0 ) + 2 * ( m_bRandomize ? 1 : 0 ) );
	m_pOpFunction = s_pDispatchTable[nIndex];
}


//-----------------------------------------------------------------------------
// Fade In Simple
//-----------------------------------------------------------------------------
class C_OP_FadeInSimple : public CParticleOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_OP_FadeInSimple );

	uint32 GetWrittenAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_ALPHA_MASK;
	}

	uint32 GetReadAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_LIFE_DURATION_MASK | PARTICLE_ATTRIBUTE_CREATION_TIME_MASK;
	}

	uint32 GetReadInitialAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_ALPHA_MASK;
	}

	virtual void Operate( CParticleCollection *pParticles, float flStrength, void *pContext ) const;

	float m_flFadeInTime;
};

DEFINE_PARTICLE_OPERATOR( C_OP_FadeInSimple, "Alpha Fade In Simple", OPERATOR_GENERIC );

BEGIN_PARTICLE_OPERATOR_UNPACK( C_OP_FadeInSimple )
	DMXELEMENT_UNPACK_FIELD( "proportional fade in time", ".25", float, m_flFadeInTime )
END_PARTICLE_OPERATOR_UNPACK( C_OP_FadeInSimple )

void C_OP_FadeInSimple::Operate( CParticleCollection *pParticles, float flStrength, void *pContext ) const
{
	CM128AttributeIterator pCreationTime( PARTICLE_ATTRIBUTE_CREATION_TIME, pParticles );
	CM128AttributeIterator pLifeDuration( PARTICLE_ATTRIBUTE_LIFE_DURATION, pParticles );
	CM128InitialAttributeIterator pInitialAlpha( PARTICLE_ATTRIBUTE_ALPHA, pParticles );
	CM128AttributeWriteIterator pAlpha( PARTICLE_ATTRIBUTE_ALPHA, pParticles );

	fltx4 fl4CurTime = pParticles->m_fl4CurTime;
	int nCtr = pParticles->m_nPaddedActiveParticles;
	fltx4 fl4FadeInTime = ReplicateX4( m_flFadeInTime );

	do
	{
		fltx4 fl4Age = SubSIMD( fl4CurTime, *pCreationTime );
		fl4Age = MulSIMD( fl4Age, ReciprocalEstSIMD( *pLifeDuration ) );
		fl4Age = MaxSIMD( Four_Zeros, MinSIMD( Four_Ones, fl4Age ) );

		fltx4 fl4FadingMask = CmpLtSIMD( fl4Age, fl4FadeInTime );
		if ( IsAnyNegative( fl4FadingMask ) )
		{
			fltx4 fl4NewAlpha = SimpleSplineRemapValWithDeltasClamped( fl4Age, Four_Zeros, fl4FadeInTime, ReciprocalEstSIMD( fl4FadeInTime ),
				Four_Zeros, *pInitialAlpha );
			*pAlpha = MaskedAssign( fl4FadingMask, fl4NewAlpha, *pAlpha );
		}
		++pCreationTime;
		++pLifeDuration;
		++pInitialAlpha;
		++pAlpha;
	} while( --nCtr );
}


//-----------------------------------------------------------------------------
// Fade Out Simple
//-----------------------------------------------------------------------------
class C_OP_FadeOutSimple : public CParticleOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_OP_FadeOutSimple );

	uint32 GetWrittenAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_ALPHA_MASK;
	}

	uint32 GetReadAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_LIFE_DURATION_MASK | PARTICLE_ATTRIBUTE_CREATION_TIME_MASK;
	}

	uint32 GetReadInitialAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_ALPHA_MASK;
	}

	virtual void Operate( CParticleCollection *pParticles, float flStrength, void *pContext ) const;

	float m_flFadeOutTime;
};

DEFINE_PARTICLE_OPERATOR( C_OP_FadeOutSimple, "Alpha Fade Out Simple", OPERATOR_GENERIC );

BEGIN_PARTICLE_OPERATOR_UNPACK( C_OP_FadeOutSimple )
	DMXELEMENT_UNPACK_FIELD( "proportional fade out time", ".25", float, m_flFadeOutTime )
END_PARTICLE_OPERATOR_UNPACK( C_OP_FadeOutSimple )

void C_OP_FadeOutSimple::Operate( CParticleCollection *pParticles, float flStrength, void *pContext ) const
{
	CM128AttributeIterator pCreationTime( PARTICLE_ATTRIBUTE_CREATION_TIME, pParticles );
	CM128AttributeIterator pLifeDuration( PARTICLE_ATTRIBUTE_LIFE_DURATION, pParticles );
	CM128InitialAttributeIterator pInitialAlpha( PARTICLE_ATTRIBUTE_ALPHA, pParticles );
	CM128AttributeWriteIterator pAlpha( PARTICLE_ATTRIBUTE_ALPHA, pParticles );

	fltx4 fl4CurTime = pParticles->m_fl4CurTime;
	int nCtr = pParticles->m_nPaddedActiveParticles;
	fltx4 fl4FadeOutStart = ReplicateX4( 1.0f - m_flFadeOutTime );
	fltx4 fl4FadeOutTime = ReplicateX4( m_flFadeOutTime );

	do
	{
		fltx4 fl4Age = SubSIMD( fl4CurTime, *pCreationTime );
		fl4Age = MulSIMD( fl4Age, ReciprocalEstSIMD( *pLifeDuration ) );

		fltx4 fl4FadingMask = CmpLtSIMD( fl4FadeOutStart, fl4Age );
		if ( IsAnyNegative( fl4FadingMask ) )
		{
			fltx4 fl4Frac = MulSIMD( SubSIMD( fl4Age, fl4FadeOutStart ), ReciprocalEstSIMD( fl4FadeOutTime ) );
			fl4Frac = MinSIMD( Four_Ones, MaxSIMD( Four_Zeros, fl4Frac ) );
			fltx4 fl4NewAlpha = MulSIMD( SubSIMD( Four_Ones, SimpleSpline( fl4Frac ) ), *pInitialAlpha );
			*pAlpha = MaskedAssign( fl4FadingMask, MinSIMD( fl4NewAlpha, *pAlpha ), *pAlpha );
		}
		++pCreationTime;
		++pLifeDuration;
		++pInitialAlpha;
		++pAlpha;
	} while( --nCtr );
}

//-----------------------------------------------------------------------------
// Oscillate Scalar operator
//-----------------------------------------------------------------------------
class C_OP_OscillateScalar : public CParticleOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_OP_OscillateScalar );

	uint32 GetWrittenAttributes( void ) const
	{
		return 1 << m_nField;
	}

	uint32 GetReadAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_PARTICLE_ID_MASK | PARTICLE_ATTRIBUTE_LIFE_DURATION_MASK | PARTICLE_ATTRIBUTE_CREATION_TIME_MASK;
	}

	virtual uint32 GetFilter( void ) const
	{
		return FILTER_PARAMETER_REMAPPING_MASK;
	}

	virtual void Operate( CParticleCollection *pParticles, float flStrength, void *pContext ) const;

	float	m_RateMin;
	float	m_RateMax;
	float	m_FrequencyMin;
	float	m_FrequencyMax;
	int		m_nField;
	bool	m_bProportional;
	bool	m_bProportionalOp;
	float	m_flStartTime_min;
	float	m_flStartTime_max;
	float	m_flEndTime_min;
	float	m_flEndTime_max;
	float	m_flOscMult;
	float	m_flOscAdd;
};

DEFINE_PARTICLE_OPERATOR( C_OP_OscillateScalar, "Oscillate Scalar", OPERATOR_GENERIC );

BEGIN_PARTICLE_OPERATOR_UNPACK( C_OP_OscillateScalar )
	DMXELEMENT_UNPACK_FIELD_USERDATA( "oscillation field", "7", int, m_nField, "intchoice particlefield_scalar" )
	DMXELEMENT_UNPACK_FIELD( "oscillation rate min", "0", float, m_RateMin )
	DMXELEMENT_UNPACK_FIELD( "oscillation rate max", "0", float, m_RateMax )
	DMXELEMENT_UNPACK_FIELD( "oscillation frequency min", "1", float, m_FrequencyMin )
	DMXELEMENT_UNPACK_FIELD( "oscillation frequency max", "1", float, m_FrequencyMax )
	DMXELEMENT_UNPACK_FIELD( "proportional 0/1", "1", bool, m_bProportional )
	DMXELEMENT_UNPACK_FIELD( "start time min", "0", float, m_flStartTime_min )
	DMXELEMENT_UNPACK_FIELD( "start time max", "0", float, m_flStartTime_max )
	DMXELEMENT_UNPACK_FIELD( "end time min", "1", float, m_flEndTime_min )
	DMXELEMENT_UNPACK_FIELD( "end time max", "1", float, m_flEndTime_max )
	DMXELEMENT_UNPACK_FIELD( "start/end proportional", "1", bool, m_bProportionalOp )
	DMXELEMENT_UNPACK_FIELD( "oscillation multiplier", "2", float, m_flOscMult )
	DMXELEMENT_UNPACK_FIELD( "oscillation start phase", ".5", float, m_flOscAdd )
END_PARTICLE_OPERATOR_UNPACK( C_OP_OscillateScalar )

void C_OP_OscillateScalar::Operate( CParticleCollection *pParticles, float flStrength, void *pContext ) const
{
	C4IAttributeIterator pParticleId( PARTICLE_ATTRIBUTE_PARTICLE_ID, pParticles );
	CM128AttributeIterator pCreationTime( PARTICLE_ATTRIBUTE_CREATION_TIME, pParticles );
	CM128AttributeIterator pLifeDuration( PARTICLE_ATTRIBUTE_LIFE_DURATION, pParticles );
	CM128AttributeWriteIterator pOscField( m_nField, pParticles );

	fltx4 fl4ScaleFactor = ReplicateX4( pParticles->m_flDt * flStrength );
	fltx4 fl4CosFactorMultiplier = ReplicateX4( m_flOscMult );
	fltx4 fl4CosFactorAdd = ReplicateX4( m_flOscAdd );
	fltx4 fl4CurTime = pParticles->m_fl4CurTime;
	fltx4 fl4CosFactor = AddSIMD( MulSIMD( fl4CosFactorMultiplier, fl4CurTime ), fl4CosFactorAdd );

	fltx4 fl4StartTimeMin = ReplicateX4( m_flStartTime_min );
	fltx4 fl4StartTimeWidth = ReplicateX4( m_flStartTime_max - m_flStartTime_min );
	fltx4 fl4EndTimeMin = ReplicateX4( m_flEndTime_min );
	fltx4 fl4EndTimeWidth = ReplicateX4( m_flEndTime_max - m_flEndTime_min );

	fltx4 fl4FrequencyMin = ReplicateX4( m_FrequencyMin );
	fltx4 fl4FrequencyWidth = ReplicateX4( m_FrequencyMax - m_FrequencyMin );
	fltx4 fl4RateMin = ReplicateX4( m_RateMin );
	fltx4 fl4RateWidth = ReplicateX4( m_RateMax - m_RateMin );

	int nRandomOffset = pParticles->OperatorRandomSampleOffset();
	int nCtr = pParticles->m_nPaddedActiveParticles;
	do
	{
		fltx4 fl4LifeDuration = *pLifeDuration;
		fltx4 fl4GoodMask = CmpGtSIMD( fl4LifeDuration, Four_Zeros );
		fltx4 fl4Age = SubSIMD( fl4CurTime, *pCreationTime );
		fltx4 fl4LifeTime;
		if ( m_bProportionalOp )
		{
			fl4LifeTime = MulSIMD( ReciprocalEstSIMD( fl4LifeDuration ), fl4Age );
		}
		else
		{
			fl4LifeTime = fl4Age;
		}

		fltx4 fl4StartTime = pParticles->RandomFloat( *pParticleId, nRandomOffset + 11 );
		fl4StartTime = AddSIMD( MulSIMD( fl4StartTime, fl4StartTimeWidth ), fl4StartTimeMin );
		fltx4 fl4EndTime = pParticles->RandomFloat( *pParticleId, nRandomOffset + 12 );

		fl4GoodMask = AndSIMD( CmpLeSIMD( fl4StartTime, fl4LifeTime ), fl4GoodMask );
		fl4EndTime = AddSIMD( MulSIMD( fl4EndTime, fl4EndTimeWidth ), fl4EndTimeMin );
		fl4GoodMask = AndSIMD( fl4GoodMask, CmpLtSIMD( fl4LifeTime, fl4EndTime ) );
		if ( IsAnyNegative( fl4GoodMask ) )
		{
			fltx4 fl4Frequency = pParticles->RandomFloat( *pParticleId, nRandomOffset );
			fl4Frequency = AddSIMD( MulSIMD( fl4Frequency, fl4FrequencyWidth ), fl4FrequencyMin );
			fltx4 fl4Rate = pParticles->RandomFloat( *pParticleId, nRandomOffset + 1 );
			fl4Rate = AddSIMD( MulSIMD( fl4Rate, fl4RateWidth ), fl4RateMin );

			fltx4 fl4Cos;
			if ( m_bProportional )
			{
				fltx4 fl4Lifetime = MulSIMD( ReciprocalEstSIMD( fl4LifeDuration ), fl4Age );
				fl4Cos = AddSIMD( MulSIMD( MulSIMD( fl4Lifetime, fl4Frequency ), fl4CosFactorMultiplier ), fl4CosFactorAdd );
			}
			else
			{
				fl4Cos = MulSIMD( fl4Frequency, fl4CosFactor );
			}

			fltx4 fl4OscMultiplier = MulSIMD( fl4Rate, fl4ScaleFactor );
			fltx4 fl4OscVal = AddSIMD( MulSIMD( SinEst01SIMD( fl4Cos ), fl4OscMultiplier ), *pOscField );
			if ( ATTRIBUTES_WHICH_ARE_0_TO_1 & ( 1 << m_nField ) )
			{
				*pOscField = MaskedAssign( fl4GoodMask, MaxSIMD( MinSIMD( fl4OscVal, Four_Ones ), Four_Zeros ), *pOscField );
			}
			else
			{
				*pOscField = MaskedAssign( fl4GoodMask, fl4OscVal, *pOscField );
			}
		}

		++pCreationTime;
		++pLifeDuration;
		++pOscField;
		++pParticleId;
	} while ( --nCtr );
}


//-----------------------------------------------------------------------------
// Oscillate Scalar Simple operator
//-----------------------------------------------------------------------------
class C_OP_OscillateScalarSimple : public CParticleOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_OP_OscillateScalarSimple );

	uint32 GetWrittenAttributes( void ) const
	{
		return 1 << m_nField;
	}

	uint32 GetReadAttributes( void ) const
	{
		return 0;
	}

	virtual uint32 GetFilter( void ) const
	{
		return FILTER_PARAMETER_REMAPPING_MASK;
	}

	virtual void InitParams( CParticleSystemDefinition *pDef );

	virtual void Operate( CParticleCollection *pParticles, float flStrength, void *pContext ) const;

	float	m_Rate;
	float	m_Frequency;
	int		m_nField;
	float	m_flOscMult;
	float	m_flOscAdd;
	fltx4	m_fl4MinCmp;
	fltx4	m_fl4MaxCmp;
};

DEFINE_PARTICLE_OPERATOR( C_OP_OscillateScalarSimple, "Oscillate Scalar Simple", OPERATOR_GENERIC );

BEGIN_PARTICLE_OPERATOR_UNPACK( C_OP_OscillateScalarSimple )
	DMXELEMENT_UNPACK_FIELD_USERDATA( "oscillation field", "7", int, m_nField, "intchoice particlefield_scalar" )
	DMXELEMENT_UNPACK_FIELD( "oscillation rate", "0", float, m_Rate )
	DMXELEMENT_UNPACK_FIELD( "oscillation frequency", "1", float, m_Frequency )
	DMXELEMENT_UNPACK_FIELD( "oscillation multiplier", "2", float, m_flOscMult )
	DMXELEMENT_UNPACK_FIELD( "oscillation start phase", ".5", float, m_flOscAdd )
END_PARTICLE_OPERATOR_UNPACK( C_OP_OscillateScalarSimple )

// Shared by the "simple"/ramp operators: per-field clamp range
#define SET_FIELD_CLAMP_RANGE( nField, fl4Min, fl4Max )			\
	if ( ATTRIBUTES_WHICH_ARE_0_TO_1 & ( 1 << ( nField ) ) )	\
	{															\
		fl4Min = Four_Zeros;									\
		fl4Max = Four_Ones;										\
	}															\
	else														\
	{															\
		if ( ATTRIBUTES_WHICH_ARE_SIZE & ( 1 << ( nField ) ) )	\
		{														\
			fl4Min = Four_Zeros;								\
		}														\
		else													\
		{														\
			fl4Min = Four_Negative_FLT_MAX;						\
		}														\
		fl4Max = Four_FLT_MAX;									\
	}

void C_OP_OscillateScalarSimple::InitParams( CParticleSystemDefinition *pDef )
{
	SET_FIELD_CLAMP_RANGE( m_nField, m_fl4MinCmp, m_fl4MaxCmp );
}

void C_OP_OscillateScalarSimple::Operate( CParticleCollection *pParticles, float flStrength, void *pContext ) const
{
	CM128AttributeWriteIterator pOscField( m_nField, pParticles );

	fltx4 fl4ScaleFactor = ReplicateX4( pParticles->m_flDt * flStrength );
	fltx4 fl4CosFactorMultiplier = ReplicateX4( m_flOscMult );
	fltx4 fl4CosFactorAdd = ReplicateX4( m_flOscAdd );
	fltx4 fl4Frequency = ReplicateX4( m_Frequency );
	fltx4 fl4Rate = ReplicateX4( m_Rate );
	fltx4 fl4CurTime = pParticles->m_fl4CurTime;

	fltx4 fl4Cos = MulSIMD( AddSIMD( MulSIMD( fl4CosFactorMultiplier, fl4CurTime ), fl4CosFactorAdd ), fl4Frequency );
	fltx4 fl4OscMultiplier = MulSIMD( fl4Rate, fl4ScaleFactor );
	fltx4 fl4OscVal = MulSIMD( SinEst01SIMD( fl4Cos ), fl4OscMultiplier );

	int nCtr = pParticles->m_nPaddedActiveParticles;
	do
	{
		*pOscField = MaxSIMD( MinSIMD( AddSIMD( *pOscField, fl4OscVal ), m_fl4MaxCmp ), m_fl4MinCmp );
		++pOscField;
	} while ( --nCtr );
}


//-----------------------------------------------------------------------------
// Oscillate Vector operator
//-----------------------------------------------------------------------------
class C_OP_OscillateVector : public CParticleOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_OP_OscillateVector );

	uint32 GetWrittenAttributes( void ) const
	{
		return 1 << m_nField;
	}

	uint32 GetReadAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_PARTICLE_ID_MASK | PARTICLE_ATTRIBUTE_LIFE_DURATION_MASK | PARTICLE_ATTRIBUTE_CREATION_TIME_MASK;
	}

	virtual uint32 GetFilter( void ) const
	{
		return FILTER_PARAMETER_REMAPPING_MASK;
	}

	virtual void Operate( CParticleCollection *pParticles, float flStrength, void *pContext ) const;

	Vector	m_RateMin;
	Vector	m_RateMax;
	Vector	m_FrequencyMin;
	Vector	m_FrequencyMax;
	int		m_nField;
	bool	m_bProportional;
	bool	m_bProportionalOp;
	bool	m_bAccelerator;		// NOTE: shipped build never reads or unpacks this
	float	m_flStartTime_min;
	float	m_flStartTime_max;
	float	m_flEndTime_min;
	float	m_flEndTime_max;
	float	m_flOscMult;
	float	m_flOscAdd;
};

DEFINE_PARTICLE_OPERATOR( C_OP_OscillateVector, "Oscillate Vector", OPERATOR_GENERIC );

BEGIN_PARTICLE_OPERATOR_UNPACK( C_OP_OscillateVector )
	DMXELEMENT_UNPACK_FIELD_USERDATA( "oscillation field", "0", int, m_nField, "intchoice particlefield_vector" )
	DMXELEMENT_UNPACK_FIELD( "oscillation rate min", "0 0 0", Vector, m_RateMin )
	DMXELEMENT_UNPACK_FIELD( "oscillation rate max", "0 0 0", Vector, m_RateMax )
	DMXELEMENT_UNPACK_FIELD( "oscillation frequency min", "1 1 1", Vector, m_FrequencyMin )
	DMXELEMENT_UNPACK_FIELD( "oscillation frequency max", "1 1 1", Vector, m_FrequencyMax )
	DMXELEMENT_UNPACK_FIELD( "proportional 0/1", "1", bool, m_bProportional )
	DMXELEMENT_UNPACK_FIELD( "start time min", "0", float, m_flStartTime_min )
	DMXELEMENT_UNPACK_FIELD( "start time max", "0", float, m_flStartTime_max )
	DMXELEMENT_UNPACK_FIELD( "end time min", "1", float, m_flEndTime_min )
	DMXELEMENT_UNPACK_FIELD( "end time max", "1", float, m_flEndTime_max )
	DMXELEMENT_UNPACK_FIELD( "start/end proportional", "1", bool, m_bProportionalOp )
	DMXELEMENT_UNPACK_FIELD( "oscillation multiplier", "2", float, m_flOscMult )
	DMXELEMENT_UNPACK_FIELD( "oscillation start phase", ".5", float, m_flOscAdd )
END_PARTICLE_OPERATOR_UNPACK( C_OP_OscillateVector )

void C_OP_OscillateVector::Operate( CParticleCollection *pParticles, float flStrength, void *pContext ) const
{
	C4IAttributeIterator pParticleId( PARTICLE_ATTRIBUTE_PARTICLE_ID, pParticles );
	CM128AttributeIterator pCreationTime( PARTICLE_ATTRIBUTE_CREATION_TIME, pParticles );
	CM128AttributeIterator pLifeDuration( PARTICLE_ATTRIBUTE_LIFE_DURATION, pParticles );
	C4VAttributeWriteIterator pOscField( m_nField, pParticles );

	fltx4 fl4ScaleFactor = ReplicateX4( pParticles->m_flDt * flStrength );
	fltx4 fl4CosFactorMultiplier = ReplicateX4( m_flOscMult );
	fltx4 fl4CosFactorAdd = ReplicateX4( m_flOscAdd );
	fltx4 fl4CurTime = pParticles->m_fl4CurTime;
	fltx4 fl4CosFactor = AddSIMD( MulSIMD( fl4CosFactorMultiplier, fl4CurTime ), fl4CosFactorAdd );

	fltx4 fl4StartTimeMin = ReplicateX4( m_flStartTime_min );
	fltx4 fl4StartTimeWidth = ReplicateX4( m_flStartTime_max - m_flStartTime_min );
	fltx4 fl4EndTimeMin = ReplicateX4( m_flEndTime_min );
	fltx4 fl4EndTimeWidth = ReplicateX4( m_flEndTime_max - m_flEndTime_min );

	FourVectors fvFrequencyMin;
	fvFrequencyMin.DuplicateVector( m_FrequencyMin );
	FourVectors fvFrequencyWidth;
	fvFrequencyWidth.DuplicateVector( m_FrequencyMax - m_FrequencyMin );
	FourVectors fvRateMin;
	fvRateMin.DuplicateVector( m_RateMin );
	FourVectors fvRateWidth;
	fvRateWidth.DuplicateVector( m_RateMax - m_RateMin );

	int nRandomOffset = pParticles->OperatorRandomSampleOffset();
	int nCtr = pParticles->m_nPaddedActiveParticles;
	do
	{
		fltx4 fl4LifeDuration = *pLifeDuration;
		fltx4 fl4GoodMask = CmpGtSIMD( fl4LifeDuration, Four_Zeros );
		fltx4 fl4Age = SubSIMD( fl4CurTime, *pCreationTime );
		fltx4 fl4LifeTime;
		if ( m_bProportionalOp )
		{
			fl4LifeTime = MulSIMD( ReciprocalEstSIMD( fl4LifeDuration ), fl4Age );
		}
		else
		{
			fl4LifeTime = fl4Age;
		}

		fltx4 fl4StartTime = pParticles->RandomFloat( *pParticleId, nRandomOffset + 11 );
		fl4StartTime = AddSIMD( MulSIMD( fl4StartTime, fl4StartTimeWidth ), fl4StartTimeMin );
		fltx4 fl4EndTime = pParticles->RandomFloat( *pParticleId, nRandomOffset + 12 );

		fl4GoodMask = AndSIMD( CmpLeSIMD( fl4StartTime, fl4LifeTime ), fl4GoodMask );
		fl4EndTime = AddSIMD( MulSIMD( fl4EndTime, fl4EndTimeWidth ), fl4EndTimeMin );
		fl4GoodMask = AndSIMD( fl4GoodMask, CmpLtSIMD( fl4LifeTime, fl4EndTime ) );
		if ( IsAnyNegative( fl4GoodMask ) )
		{
			FourVectors fvFrequency;
			fvFrequency.x = pParticles->RandomFloat( *pParticleId, nRandomOffset + 8 );
			fvFrequency.y = pParticles->RandomFloat( *pParticleId, nRandomOffset + 12 );
			fvFrequency.z = pParticles->RandomFloat( *pParticleId, nRandomOffset + 15 );
			fvFrequency.x = AddSIMD( fvFrequencyMin.x, MulSIMD( fvFrequency.x, fvFrequencyWidth.x ) );
			fvFrequency.y = AddSIMD( fvFrequencyMin.y, MulSIMD( fvFrequency.y, fvFrequencyWidth.y ) );
			fvFrequency.z = AddSIMD( fvFrequencyMin.z, MulSIMD( fvFrequency.z, fvFrequencyWidth.z ) );

			FourVectors fvRate;
			fvRate.x = pParticles->RandomFloat( *pParticleId, nRandomOffset + 3 );
			fvRate.y = pParticles->RandomFloat( *pParticleId, nRandomOffset + 7 );
			fvRate.z = pParticles->RandomFloat( *pParticleId, nRandomOffset + 9 );
			fvRate.x = AddSIMD( fvRateMin.x, MulSIMD( fvRate.x, fvRateWidth.x ) );
			fvRate.y = AddSIMD( fvRateMin.y, MulSIMD( fvRate.y, fvRateWidth.y ) );
			fvRate.z = AddSIMD( fvRateMin.z, MulSIMD( fvRate.z, fvRateWidth.z ) );

			FourVectors fvCos;
			if ( m_bProportional )
			{
				fltx4 fl4Lifetime = MulSIMD( ReciprocalEstSIMD( fl4LifeDuration ), fl4Age );
				fvCos.x = AddSIMD( MulSIMD( MulSIMD( fvFrequency.x, fl4Lifetime ), fl4CosFactorMultiplier ), fl4CosFactorAdd );
				fvCos.y = AddSIMD( MulSIMD( MulSIMD( fvFrequency.y, fl4Lifetime ), fl4CosFactorMultiplier ), fl4CosFactorAdd );
				fvCos.z = AddSIMD( MulSIMD( MulSIMD( fvFrequency.z, fl4Lifetime ), fl4CosFactorMultiplier ), fl4CosFactorAdd );
			}
			else
			{
				fvCos.x = MulSIMD( fvFrequency.x, fl4CosFactor );
				fvCos.y = MulSIMD( fvFrequency.y, fl4CosFactor );
				fvCos.z = MulSIMD( fvFrequency.z, fl4CosFactor );
			}

			FourVectors fvOscMultiplier;
			fvOscMultiplier.x = MulSIMD( fvRate.x, fl4ScaleFactor );
			fvOscMultiplier.y = MulSIMD( fvRate.y, fl4ScaleFactor );
			fvOscMultiplier.z = MulSIMD( fvRate.z, fl4ScaleFactor );

			FourVectors fvOutput = *pOscField;
			fltx4 fl4OutX = AddSIMD( MulSIMD( SinEst01SIMD( fvCos.x ), fvOscMultiplier.x ), fvOutput.x );
			fltx4 fl4OutY = AddSIMD( MulSIMD( SinEst01SIMD( fvCos.y ), fvOscMultiplier.y ), fvOutput.y );
			fltx4 fl4OutZ = AddSIMD( MulSIMD( SinEst01SIMD( fvCos.z ), fvOscMultiplier.z ), fvOutput.z );

			if ( m_nField == PARTICLE_ATTRIBUTE_TINT_RGB )
			{
				pOscField->x = MaskedAssign( fl4GoodMask, MaxSIMD( MinSIMD( fl4OutX, Four_Ones ), Four_Zeros ), fvOutput.x );
				pOscField->y = MaskedAssign( fl4GoodMask, MaxSIMD( MinSIMD( fl4OutY, Four_Ones ), Four_Zeros ), fvOutput.y );
				pOscField->z = MaskedAssign( fl4GoodMask, MaxSIMD( MinSIMD( fl4OutZ, Four_Ones ), Four_Zeros ), fvOutput.z );
			}
			else
			{
				pOscField->x = MaskedAssign( fl4GoodMask, fl4OutX, fvOutput.x );
				pOscField->y = MaskedAssign( fl4GoodMask, fl4OutY, fvOutput.y );
				pOscField->z = MaskedAssign( fl4GoodMask, fl4OutZ, fvOutput.z );
			}
		}

		++pCreationTime;
		++pLifeDuration;
		++pOscField;
		++pParticleId;
	} while ( --nCtr );
}


//-----------------------------------------------------------------------------
// Oscillate Vector Simple operator
//-----------------------------------------------------------------------------
class C_OP_OscillateVectorSimple : public CParticleOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_OP_OscillateVectorSimple );

	uint32 GetWrittenAttributes( void ) const
	{
		return 1 << m_nField;
	}

	uint32 GetReadAttributes( void ) const
	{
		return 0;
	}

	virtual uint32 GetFilter( void ) const
	{
		return FILTER_NOT_SPECIAL_MASK;
	}

	virtual void Operate( CParticleCollection *pParticles, float flStrength, void *pContext ) const;

	Vector	m_Rate;
	Vector	m_Frequency;
	int		m_nField;
	float	m_flOscMult;
	float	m_flOscAdd;
};

DEFINE_PARTICLE_OPERATOR( C_OP_OscillateVectorSimple, "Oscillate Vector Simple", OPERATOR_GENERIC );

BEGIN_PARTICLE_OPERATOR_UNPACK( C_OP_OscillateVectorSimple )
	DMXELEMENT_UNPACK_FIELD_USERDATA( "oscillation field", "0", int, m_nField, "intchoice particlefield_vector" )
	DMXELEMENT_UNPACK_FIELD( "oscillation rate", "0 0 0", Vector, m_Rate )
	DMXELEMENT_UNPACK_FIELD( "oscillation frequency", "1 1 1", Vector, m_Frequency )
	DMXELEMENT_UNPACK_FIELD( "oscillation multiplier", "2", float, m_flOscMult )
	DMXELEMENT_UNPACK_FIELD( "oscillation start phase", ".5", float, m_flOscAdd )
END_PARTICLE_OPERATOR_UNPACK( C_OP_OscillateVectorSimple )

void C_OP_OscillateVectorSimple::Operate( CParticleCollection *pParticles, float flStrength, void *pContext ) const
{
	C4VAttributeWriteIterator pOscField( m_nField, pParticles );

	fltx4 fl4ScaleFactor = ReplicateX4( pParticles->m_flDt * flStrength );
	fltx4 fl4CosFactorMultiplier = ReplicateX4( m_flOscMult );
	fltx4 fl4CosFactorAdd = ReplicateX4( m_flOscAdd );
	fltx4 fl4CurTime = pParticles->m_fl4CurTime;
	fltx4 fl4CosFactor = AddSIMD( MulSIMD( fl4CosFactorMultiplier, fl4CurTime ), fl4CosFactorAdd );

	FourVectors fvFrequency;
	fvFrequency.DuplicateVector( m_Frequency );
	FourVectors fvRate;
	fvRate.DuplicateVector( m_Rate );

	FourVectors fvCos;
	fvCos.x = MulSIMD( fvFrequency.x, fl4CosFactor );
	fvCos.y = MulSIMD( fvFrequency.y, fl4CosFactor );
	fvCos.z = MulSIMD( fvFrequency.z, fl4CosFactor );

	FourVectors fvOscMultiplier;
	fvOscMultiplier.x = MulSIMD( fvRate.x, fl4ScaleFactor );
	fvOscMultiplier.y = MulSIMD( fvRate.y, fl4ScaleFactor );
	fvOscMultiplier.z = MulSIMD( fvRate.z, fl4ScaleFactor );

	FourVectors fvOscVal;
	fvOscVal.x = MulSIMD( SinEst01SIMD( fvCos.x ), fvOscMultiplier.x );
	fvOscVal.y = MulSIMD( SinEst01SIMD( fvCos.y ), fvOscMultiplier.y );
	fvOscVal.z = MulSIMD( SinEst01SIMD( fvCos.z ), fvOscMultiplier.z );

	int nCtr = pParticles->m_nPaddedActiveParticles;
	if ( ATTRIBUTES_WHICH_ARE_COLOR_AND_OPACITY & ( 1 << m_nField ) )
	{
		do
		{
			FourVectors fvOutput = *pOscField;
			pOscField->x = MaxSIMD( MinSIMD( AddSIMD( fvOutput.x, fvOscVal.x ), Four_Ones ), Four_Zeros );
			pOscField->y = MaxSIMD( MinSIMD( AddSIMD( fvOutput.y, fvOscVal.y ), Four_Ones ), Four_Zeros );
			pOscField->z = MaxSIMD( MinSIMD( AddSIMD( fvOutput.z, fvOscVal.z ), Four_Ones ), Four_Zeros );
			++pOscField;
		} while ( --nCtr );
	}
	else
	{
		do
		{
			*pOscField += fvOscVal;
			++pOscField;
		} while ( --nCtr );
	}
}


//-----------------------------------------------------------------------------
// Remap Scalar operator
//-----------------------------------------------------------------------------
class C_OP_RemapScalar : public CParticleOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_OP_RemapScalar );

	uint32 GetWrittenAttributes( void ) const
	{
		return 1 << m_nFieldOutput;
	}

	uint32 GetReadAttributes( void ) const
	{
		return 1 << m_nFieldInput;
	}

	virtual uint32 GetFilter( void ) const
	{
		return FILTER_PARAMETER_REMAPPING_MASK;
	}

	virtual void Operate( CParticleCollection *pParticles, float flStrength, void *pContext ) const;

	int		m_nFieldInput;
	int		m_nFieldOutput;
	float	m_flInputMin;
	float	m_flInputMax;
	float	m_flOutputMin;
	float	m_flOutputMax;
};

DEFINE_PARTICLE_OPERATOR( C_OP_RemapScalar, "Remap Scalar", OPERATOR_GENERIC );

BEGIN_PARTICLE_OPERATOR_UNPACK( C_OP_RemapScalar )
	DMXELEMENT_UNPACK_FIELD_USERDATA( "input field", "7", int, m_nFieldInput, "intchoice particlefield_scalar" )
	DMXELEMENT_UNPACK_FIELD( "input minimum", "0", float, m_flInputMin )
	DMXELEMENT_UNPACK_FIELD( "input maximum", "1", float, m_flInputMax )
	DMXELEMENT_UNPACK_FIELD_USERDATA( "output field", "3", int, m_nFieldOutput, "intchoice particlefield_scalar" )
	DMXELEMENT_UNPACK_FIELD( "output minimum", "0", float, m_flOutputMin )
	DMXELEMENT_UNPACK_FIELD( "output maximum", "1", float, m_flOutputMax )
END_PARTICLE_OPERATOR_UNPACK( C_OP_RemapScalar )

void C_OP_RemapScalar::Operate( CParticleCollection *pParticles, float flStrength, void *pContext ) const
{
	// clamp the result to 0 and 1 if it's alpha
	float flMin = m_flOutputMin;
	float flMax = m_flOutputMax;
	if ( ATTRIBUTES_WHICH_ARE_0_TO_1 & ( 1 << m_nFieldOutput ) )
	{
		flMin = clamp( m_flOutputMin, 0.0f, 1.0f );
		flMax = clamp( m_flOutputMax, 0.0f, 1.0f );
	}

	for ( int i = 0; i < pParticles->m_nActiveParticles; ++i )
	{
		float *pOutput = pParticles->GetFloatAttributePtrForWrite( m_nFieldOutput, i );
		const float *pInput = pParticles->GetFloatAttributePtr( m_nFieldInput, i );
		float flOutput = RemapValClamped( *pInput, m_flInputMin, m_flInputMax, flMin, flMax );
		*pOutput = Lerp( flStrength, *pOutput, flOutput );
	}
}


//-----------------------------------------------------------------------------
// Remap Speed operator
//-----------------------------------------------------------------------------
class C_OP_RemapSpeed : public CParticleOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_OP_RemapSpeed );

	uint32 GetWrittenAttributes( void ) const
	{
		return 1 << m_nFieldOutput;
	}

	uint32 GetReadAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_XYZ_MASK | PARTICLE_ATTRIBUTE_PREV_XYZ_MASK;
	}

	uint32 GetReadInitialAttributes( void ) const
	{
		return 1 << m_nFieldOutput;
	}

	virtual uint32 GetFilter( void ) const
	{
		return FILTER_PARAMETER_REMAPPING_MASK;
	}

	virtual void InitParams( CParticleSystemDefinition *pDef );

	virtual void Operate( CParticleCollection *pParticles, float flStrength, void *pContext ) const;

	int		m_nFieldOutput;
	float	m_flInputMin;
	float	m_flInputMax;
	float	m_flOutputMin;
	float	m_flOutputMax;
	bool	m_bScaleInitialRange;
	bool	m_bScaleCurrent;
};

DEFINE_PARTICLE_OPERATOR( C_OP_RemapSpeed, "Remap Speed to Scalar", OPERATOR_GENERIC );

BEGIN_PARTICLE_OPERATOR_UNPACK( C_OP_RemapSpeed )
	DMXELEMENT_UNPACK_FIELD( "input minimum", "0", float, m_flInputMin )
	DMXELEMENT_UNPACK_FIELD( "input maximum", "1", float, m_flInputMax )
	DMXELEMENT_UNPACK_FIELD_USERDATA( "output field", "3", int, m_nFieldOutput, "intchoice particlefield_scalar" )
	DMXELEMENT_UNPACK_FIELD( "output minimum", "0", float, m_flOutputMin )
	DMXELEMENT_UNPACK_FIELD( "output maximum", "1", float, m_flOutputMax )
	DMXELEMENT_UNPACK_FIELD( "output is scalar of initial random range", "0", bool, m_bScaleInitialRange )
	DMXELEMENT_UNPACK_FIELD( "output is scalar of current value", "0", bool, m_bScaleCurrent )
END_PARTICLE_OPERATOR_UNPACK( C_OP_RemapSpeed )

void C_OP_RemapSpeed::InitParams( CParticleSystemDefinition *pDef )
{
	m_flInputMin = MAX( 0.001, m_flInputMin );
	m_flInputMax = MAX( 0.001, m_flInputMax );
}

void C_OP_RemapSpeed::Operate( CParticleCollection *pParticles, float flStrength, void *pContext ) const
{
	// clamp the result to 0 and 1 if it's alpha
	fltx4 fl4OutputMin = ReplicateX4( m_flOutputMin );
	fltx4 fl4OutputMax = ReplicateX4( m_flOutputMax );
	if ( ATTRIBUTES_WHICH_ARE_0_TO_1 & ( 1 << m_nFieldOutput ) )
	{
		fl4OutputMin = ReplicateX4( clamp( m_flOutputMin, 0.0f, 1.0f ) );
		fl4OutputMax = ReplicateX4( clamp( m_flOutputMax, 0.0f, 1.0f ) );
	}

	fltx4 fl4Dt = ReplicateX4( pParticles->m_flDt );

	C4VAttributeIterator pXYZ( PARTICLE_ATTRIBUTE_XYZ, pParticles );
	C4VAttributeIterator pPrevXYZ( PARTICLE_ATTRIBUTE_PREV_XYZ, pParticles );
	fltx4 fl4InputMin = ReplicateX4( m_flInputMin );
	fltx4 fl4InputMax = ReplicateX4( m_flInputMax );
	CM128AttributeWriteIterator pOutput( m_nFieldOutput, pParticles );
	CM128InitialAttributeIterator pInitialOutput( m_nFieldOutput, pParticles );
	fltx4 fl4Strength = ReplicateX4( flStrength );

	for ( int i = 0; i < pParticles->m_nPaddedActiveParticles; i++ )
	{
		FourVectors fvVel = *pXYZ;
		fvVel -= *pPrevXYZ;
		fltx4 fl4LengthSqr = AddSIMD( AddSIMD( MulSIMD( fvVel.y, fvVel.y ), MulSIMD( fvVel.x, fvVel.x ) ), MulSIMD( fvVel.z, fvVel.z ) );
		fltx4 fl4Speed = DivSIMD( SqrtSIMD( fl4LengthSqr ), fl4Dt );
		fltx4 fl4Output = RemapValClampedSIMD( fl4Speed, fl4InputMin, fl4InputMax, fl4OutputMin, fl4OutputMax );
		if ( m_bScaleInitialRange )
		{
			fl4Output = MulSIMD( *pInitialOutput, fl4Output );
		}
		if ( m_bScaleCurrent )
		{
			fl4Output = MulSIMD( *pOutput, fl4Output );
		}
		fltx4 fl4Current = *pOutput;
		*pOutput = AddSIMD( MulSIMD( SubSIMD( fl4Output, fl4Current ), fl4Strength ), fl4Current );

		++pXYZ;
		++pPrevXYZ;
		++pOutput;
		++pInitialOutput;
	}
}


//-----------------------------------------------------------------------------
// Remap CP Speed to CP operator
//-----------------------------------------------------------------------------

// Out-of-line RemapValClamped as the shipped build calls it: the final lerp runs on the x87 stack
// (53-bit precision) and is rounded to float by the caller, i.e. one double rounding.
static float RemapValClampedX87( float val, float A, float B, float C, float D )
{
	if ( A == B )
		return fsel( val - B, D, C );
	float cVal = ( val - A ) / ( B - A );
	cVal = clamp<float>( cVal, 0.0f, 1.0f );
	return (float)( ( (double)D - (double)C ) * (double)cVal + (double)C );
}

// 1ULL << n as the shipped build computes it (MSVC __allshl): only the low byte of the
// count is used, counts >= 64 give 0. Matters for CP numbers of -1.
static inline uint64 CPMaskBit( int nCP )
{
	unsigned int nShift = (unsigned int)nCP & 0xff;
	if ( nShift >= 64 )
		return 0;
	if ( nShift >= 32 )
		return (uint64)( 1u << ( nShift & 31 ) ) << 32;
	return (uint64)1 << nShift;
}

// The shipped binary sums the squared components in z, x, y order
static inline float LengthZXY( const Vector &v )
{
	return FastSqrt( ( v.z * v.z + v.x * v.x ) + v.y * v.y );
}

class C_OP_RemapSpeedtoCP : public CParticleOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_OP_RemapSpeedtoCP );

	uint32 GetWrittenAttributes( void ) const
	{
		return 0;
	}

	uint32 GetReadAttributes( void ) const
	{
		return 0;
	}

	virtual uint32 GetFilter( void ) const
	{
		return FILTER_CONTROL_POINTS_MASK;
	}

	virtual uint64 GetReadControlPointMask() const
	{
		return CPMaskBit( m_nOutControlPointNumber ) | CPMaskBit( m_nInControlPointNumber );
	}

	bool ShouldRunBeforeEmitters( void ) const
	{
		return true;
	}

	virtual void InitParams( CParticleSystemDefinition *pDef );

	virtual void Operate( CParticleCollection *pParticles, float flStrength, void *pContext ) const;

	int		m_nInControlPointNumber;
	int		m_nOutControlPointNumber;
	int		m_nField;
	float	m_flInputMin;
	float	m_flInputMax;
	float	m_flOutputMin;
	float	m_flOutputMax;
};

DEFINE_PARTICLE_OPERATOR( C_OP_RemapSpeedtoCP, "Remap CP Speed to CP", OPERATOR_GENERIC );

BEGIN_PARTICLE_OPERATOR_UNPACK( C_OP_RemapSpeedtoCP )
	DMXELEMENT_UNPACK_FIELD( "input control point", "0", int, m_nInControlPointNumber )
	DMXELEMENT_UNPACK_FIELD( "input minimum", "0", float, m_flInputMin )
	DMXELEMENT_UNPACK_FIELD( "input maximum", "1", float, m_flInputMax )
	DMXELEMENT_UNPACK_FIELD( "output control point", "-1", int, m_nOutControlPointNumber )
	DMXELEMENT_UNPACK_FIELD( "Output field 0-2 X/Y/Z", "0", int, m_nField )
	DMXELEMENT_UNPACK_FIELD( "output minimum", "0", float, m_flOutputMin )
	DMXELEMENT_UNPACK_FIELD( "output maximum", "1", float, m_flOutputMax )
END_PARTICLE_OPERATOR_UNPACK( C_OP_RemapSpeedtoCP )

void C_OP_RemapSpeedtoCP::InitParams( CParticleSystemDefinition *pDef )
{
	// check for recursion
	if ( m_nInControlPointNumber == m_nOutControlPointNumber )
		m_nOutControlPointNumber = -1;
}

void C_OP_RemapSpeedtoCP::Operate( CParticleCollection *pParticles, float flStrength, void *pContext ) const
{
	if ( m_nOutControlPointNumber >= 0 )
	{
		Vector vecPrevPos;
		pParticles->GetControlPointAtPrevTime( m_nInControlPointNumber, &vecPrevPos );
		Vector vecDelta = pParticles->GetControlPointAtCurrentTime( m_nInControlPointNumber ) - vecPrevPos;
		float flSpeed = LengthZXY( vecDelta ) / pParticles->m_flPreviousDt;
		float flOutput = RemapValClampedX87( flSpeed, m_flInputMin, m_flInputMax, m_flOutputMin, m_flOutputMax );
		Vector vecControlPoint = pParticles->GetControlPointAtCurrentTime( m_nOutControlPointNumber );
		vecControlPoint[m_nField] = flOutput;
		pParticles->SetControlPoint( m_nOutControlPointNumber, vecControlPoint );
	}
}


//-----------------------------------------------------------------------------
// Remap Model Volume to CP operator
//-----------------------------------------------------------------------------
class C_OP_RemapModelVolumetoCP : public CParticleOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_OP_RemapModelVolumetoCP );

	uint32 GetWrittenAttributes( void ) const
	{
		return 0;
	}

	uint32 GetReadAttributes( void ) const
	{
		return 0;
	}

	virtual uint32 GetFilter( void ) const
	{
		return FILTER_CONTROL_POINTS_MASK;
	}

	virtual uint64 GetReadControlPointMask() const
	{
		return CPMaskBit( m_nOutControlPointNumber ) | CPMaskBit( m_nInControlPointNumber );
	}

	bool ShouldRunBeforeEmitters( void ) const
	{
		return true;
	}

	virtual void InitParams( CParticleSystemDefinition *pDef );

	virtual void Operate( CParticleCollection *pParticles, float flStrength, void *pContext ) const;

	int		m_nInControlPointNumber;
	int		m_nOutControlPointNumber;
	float	m_flInputMin;
	float	m_flInputMax;
	float	m_flOutputMin;
	float	m_flOutputMax;
};

DEFINE_PARTICLE_OPERATOR( C_OP_RemapModelVolumetoCP, "Remap Model Volume to CP", OPERATOR_GENERIC );

BEGIN_PARTICLE_OPERATOR_UNPACK( C_OP_RemapModelVolumetoCP )
	DMXELEMENT_UNPACK_FIELD( "input control point", "0", int, m_nInControlPointNumber )
	DMXELEMENT_UNPACK_FIELD( "input volume minimum in cubic units", "0", float, m_flInputMin )
	DMXELEMENT_UNPACK_FIELD( "input volume maximum in cubic units", "128", float, m_flInputMax )
	DMXELEMENT_UNPACK_FIELD( "output control point", "-1", int, m_nOutControlPointNumber )
	DMXELEMENT_UNPACK_FIELD( "output minimum", "0", float, m_flOutputMin )
	DMXELEMENT_UNPACK_FIELD( "output maximum", "1", float, m_flOutputMax )
END_PARTICLE_OPERATOR_UNPACK( C_OP_RemapModelVolumetoCP )

void C_OP_RemapModelVolumetoCP::InitParams( CParticleSystemDefinition *pDef )
{
	// check for recursion
	if ( m_nInControlPointNumber == m_nOutControlPointNumber )
		m_nOutControlPointNumber = -1;
}

void C_OP_RemapModelVolumetoCP::Operate( CParticleCollection *pParticles, float flStrength, void *pContext ) const
{
	if ( m_nOutControlPointNumber >= 0 )
	{
		// NOTE: shipped bug: GetControllingObjectOBBox takes its vectors by value, so vecMin/vecMax
		// are never filled in. The shipped build (MSVC) shares one uninitialized stack slot for both,
		// so the box size is ( garbage - garbage ): 0 for finite garbage, NaN otherwise.
		// (zero-initialized here so debug runtime checks don't trip; gives the volume of 0 the shipped build
		// usually produced)
		Vector vecBox( 0.0f, 0.0f, 0.0f );
		g_pParticleSystemMgr->Query()->GetControllingObjectOBBox( pParticles, m_nInControlPointNumber, vecBox, vecBox );
		const volatile float *pBox = &vecBox.x;
		float flVolume = ( ( pBox[2] - pBox[2] ) * ( pBox[1] - pBox[1] ) ) * ( pBox[0] - pBox[0] );
		float flOutput = RemapValClampedX87( (float)pow( (double)flVolume, (double)( 1.0f / 3.0f ) ), m_flInputMin, m_flInputMax, m_flOutputMin, m_flOutputMax );
		Vector vecControlPoint( flOutput, 0.0f, 0.0f );
		pParticles->SetControlPoint( m_nOutControlPointNumber, vecControlPoint );
	}
}


//-----------------------------------------------------------------------------
// Remap Particle BBox Volume to CP operator
//-----------------------------------------------------------------------------
class C_OP_RemapBoundingVolumetoCP : public CParticleOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_OP_RemapBoundingVolumetoCP );

	uint32 GetWrittenAttributes( void ) const
	{
		return 0;
	}

	uint32 GetReadAttributes( void ) const
	{
		return 0;
	}

	virtual uint32 GetFilter( void ) const
	{
		return FILTER_CONTROL_POINTS_MASK;
	}

	virtual uint64 GetReadControlPointMask() const
	{
		return CPMaskBit( m_nOutControlPointNumber );
	}

	bool ShouldRunBeforeEmitters( void ) const
	{
		return true;
	}

	virtual void Operate( CParticleCollection *pParticles, float flStrength, void *pContext ) const;

	int		m_nOutControlPointNumber;
	float	m_flInputMin;
	float	m_flInputMax;
	float	m_flOutputMin;
	float	m_flOutputMax;
};

DEFINE_PARTICLE_OPERATOR( C_OP_RemapBoundingVolumetoCP, "Remap Particle BBox Volume to CP", OPERATOR_GENERIC );

BEGIN_PARTICLE_OPERATOR_UNPACK( C_OP_RemapBoundingVolumetoCP )
	DMXELEMENT_UNPACK_FIELD( "input volume minimum in cubic units", "0", float, m_flInputMin )
	DMXELEMENT_UNPACK_FIELD( "input volume maximum in cubic units", "128", float, m_flInputMax )
	DMXELEMENT_UNPACK_FIELD( "output control point", "-1", int, m_nOutControlPointNumber )
	DMXELEMENT_UNPACK_FIELD( "output minimum", "0", float, m_flOutputMin )
	DMXELEMENT_UNPACK_FIELD( "output maximum", "1", float, m_flOutputMax )
END_PARTICLE_OPERATOR_UNPACK( C_OP_RemapBoundingVolumetoCP )

void C_OP_RemapBoundingVolumetoCP::Operate( CParticleCollection *pParticles, float flStrength, void *pContext ) const
{
	if ( m_nOutControlPointNumber >= 0 )
	{
		Vector vecMin, vecMax;
		pParticles->GetBounds( &vecMin, &vecMax );
		float flVolume = ( ( vecMax.z - vecMin.z ) * ( vecMax.y - vecMin.y ) ) * ( vecMax.x - vecMin.x );
		float flOutput = RemapValClampedX87( (float)pow( (double)flVolume, (double)( 1.0f / 3.0f ) ), m_flInputMin, m_flInputMax, m_flOutputMin, m_flOutputMax );
		Vector vecControlPoint( flOutput, 0.0f, 0.0f );
		pParticles->SetControlPoint( m_nOutControlPointNumber, vecControlPoint );
	}
}


//-----------------------------------------------------------------------------
// Remap Average Scalar Value to CP operator
//-----------------------------------------------------------------------------
class C_OP_RemapAverageScalarValuetoCP : public CParticleOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_OP_RemapAverageScalarValuetoCP );

	uint32 GetWrittenAttributes( void ) const
	{
		return 0;
	}

	uint32 GetReadAttributes( void ) const
	{
		return 1 << m_nField;
	}

	virtual uint32 GetFilter( void ) const
	{
		return FILTER_CONTROL_POINTS_MASK;
	}

	virtual uint64 GetReadControlPointMask() const
	{
		return CPMaskBit( m_nOutControlPointNumber );
	}

	bool ShouldRunBeforeEmitters( void ) const
	{
		return true;
	}

	virtual void Operate( CParticleCollection *pParticles, float flStrength, void *pContext ) const;

	int		m_nOutControlPointNumber;
	int		m_nField;
	float	m_flInputMin;
	float	m_flInputMax;
	float	m_flOutputMin;
	float	m_flOutputMax;
};

DEFINE_PARTICLE_OPERATOR( C_OP_RemapAverageScalarValuetoCP, "Remap Average Scalar Value to CP", OPERATOR_GENERIC );

BEGIN_PARTICLE_OPERATOR_UNPACK( C_OP_RemapAverageScalarValuetoCP )
	DMXELEMENT_UNPACK_FIELD_USERDATA( "Scalar field", "3", int, m_nField, "intchoice particlefield_scalar" )
	DMXELEMENT_UNPACK_FIELD( "input volume minimum", "0", float, m_flInputMin )
	DMXELEMENT_UNPACK_FIELD( "input volume maximum", "1", float, m_flInputMax )
	DMXELEMENT_UNPACK_FIELD( "output control point", "1", int, m_nOutControlPointNumber )
	DMXELEMENT_UNPACK_FIELD( "output minimum", "0", float, m_flOutputMin )
	DMXELEMENT_UNPACK_FIELD( "output maximum", "1", float, m_flOutputMax )
END_PARTICLE_OPERATOR_UNPACK( C_OP_RemapAverageScalarValuetoCP )

void C_OP_RemapAverageScalarValuetoCP::Operate( CParticleCollection *pParticles, float flStrength, void *pContext ) const
{
	float flAverage = 0.0f;
	for ( int i = 0; i < pParticles->m_nActiveParticles; ++i )
	{
		const float *pInput = pParticles->GetFloatAttributePtr( m_nField, i );
		flAverage = *pInput + flAverage;
	}
	flAverage = flAverage / pParticles->m_nActiveParticles;

	float flOutput = RemapValClamped( flAverage, m_flInputMin, m_flInputMax, m_flOutputMin, m_flOutputMax );
	Vector vecControlPoint( flOutput, 0.0f, 0.0f );
	pParticles->SetControlPoint( m_nOutControlPointNumber, vecControlPoint );
}


//-----------------------------------------------------------------------------
// Ramp Scalar Linear Random operator
//-----------------------------------------------------------------------------
class C_OP_RampScalarLinear : public CParticleOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_OP_RampScalarLinear );

	uint32 GetWrittenAttributes( void ) const
	{
		return 1 << m_nField;
	}

	uint32 GetReadAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_PARTICLE_ID_MASK | PARTICLE_ATTRIBUTE_LIFE_DURATION_MASK | PARTICLE_ATTRIBUTE_CREATION_TIME_MASK;
	}

	virtual uint32 GetFilter( void ) const
	{
		return FILTER_PARAMETER_REMAPPING_MASK;
	}

	virtual void InitParams( CParticleSystemDefinition *pDef );

	virtual void Operate( CParticleCollection *pParticles, float flStrength, void *pContext ) const;

	float	m_RateMin;
	float	m_RateMax;
	float	m_flStartTime_min;
	float	m_flStartTime_max;
	float	m_flEndTime_min;
	float	m_flEndTime_max;
	fltx4	m_fl4MinCmp;
	fltx4	m_fl4MaxCmp;
	int		m_nField;
	bool	m_bProportionalOp;
	bool	m_bUsesStartEnd;
};

DEFINE_PARTICLE_OPERATOR( C_OP_RampScalarLinear, "Ramp Scalar Linear Random", OPERATOR_GENERIC );

BEGIN_PARTICLE_OPERATOR_UNPACK( C_OP_RampScalarLinear )
	DMXELEMENT_UNPACK_FIELD_USERDATA( "ramp field", "3", int, m_nField, "intchoice particlefield_scalar" )
	DMXELEMENT_UNPACK_FIELD( "ramp rate min", "0", float, m_RateMin )
	DMXELEMENT_UNPACK_FIELD( "ramp rate max", "0", float, m_RateMax )
	DMXELEMENT_UNPACK_FIELD( "start time min", "0", float, m_flStartTime_min )
	DMXELEMENT_UNPACK_FIELD( "start time max", "0", float, m_flStartTime_max )
	DMXELEMENT_UNPACK_FIELD( "end time min", "1", float, m_flEndTime_min )
	DMXELEMENT_UNPACK_FIELD( "end time max", "1", float, m_flEndTime_max )
	DMXELEMENT_UNPACK_FIELD( "start/end proportional", "1", bool, m_bProportionalOp )
END_PARTICLE_OPERATOR_UNPACK( C_OP_RampScalarLinear )

void C_OP_RampScalarLinear::InitParams( CParticleSystemDefinition *pDef )
{
	m_bUsesStartEnd = !( ( m_flStartTime_min == 0.0f ) && ( m_flStartTime_max == 0.0f ) &&
		( m_flEndTime_min == 1.0f ) && ( m_flEndTime_max == 1.0f ) && m_bProportionalOp );

	SET_FIELD_CLAMP_RANGE( m_nField, m_fl4MinCmp, m_fl4MaxCmp );
}

void C_OP_RampScalarLinear::Operate( CParticleCollection *pParticles, float flStrength, void *pContext ) const
{
	C4IAttributeIterator pParticleId( PARTICLE_ATTRIBUTE_PARTICLE_ID, pParticles );
	CM128AttributeIterator pCreationTime( PARTICLE_ATTRIBUTE_CREATION_TIME, pParticles );
	CM128AttributeIterator pLifeDuration( PARTICLE_ATTRIBUTE_LIFE_DURATION, pParticles );
	CM128AttributeWriteIterator pRampField( m_nField, pParticles );

	fltx4 fl4CurTime = pParticles->m_fl4CurTime;
	fltx4 fl4ScaleFactor = ReplicateX4( pParticles->m_flDt * flStrength );

	fltx4 fl4StartTimeMin = ReplicateX4( m_flStartTime_min );
	fltx4 fl4StartTimeWidth = ReplicateX4( m_flStartTime_max - m_flStartTime_min );
	fltx4 fl4EndTimeMin = ReplicateX4( m_flEndTime_min );
	fltx4 fl4EndTimeWidth = ReplicateX4( m_flEndTime_max - m_flEndTime_min );
	fltx4 fl4RateMin = ReplicateX4( m_RateMin );
	fltx4 fl4RateWidth = ReplicateX4( m_RateMax - m_RateMin );

	int nRandomOffset = pParticles->OperatorRandomSampleOffset();
	int nCtr = pParticles->m_nPaddedActiveParticles;
	do
	{
		fltx4 fl4LifeDuration = *pLifeDuration;
		fltx4 fl4GoodMask = CmpGtSIMD( fl4LifeDuration, Four_Zeros );
		if ( m_bUsesStartEnd )
		{
			fltx4 fl4LifeTime;
			if ( m_bProportionalOp )
			{
				fl4LifeTime = MulSIMD( SubSIMD( fl4CurTime, *pCreationTime ), ReciprocalEstSIMD( fl4LifeDuration ) );
			}
			else
			{
				fl4LifeTime = SubSIMD( fl4CurTime, *pCreationTime );
			}

			fltx4 fl4StartTime = pParticles->RandomFloat( *pParticleId, nRandomOffset + 11 );
			fl4StartTime = AddSIMD( MulSIMD( fl4StartTime, fl4StartTimeWidth ), fl4StartTimeMin );
			fltx4 fl4EndTime = pParticles->RandomFloat( *pParticleId, nRandomOffset + 12 );
			fl4EndTime = AddSIMD( MulSIMD( fl4EndTime, fl4EndTimeWidth ), fl4EndTimeMin );
			fl4GoodMask = AndSIMD( CmpLeSIMD( fl4StartTime, fl4LifeTime ), fl4GoodMask );
			fl4GoodMask = AndSIMD( fl4GoodMask, CmpLtSIMD( fl4LifeTime, fl4EndTime ) );
		}

		if ( IsAnyNegative( fl4GoodMask ) )
		{
			fltx4 fl4Rate = pParticles->RandomFloat( *pParticleId, nRandomOffset );
			fl4Rate = AddSIMD( MulSIMD( fl4Rate, fl4RateWidth ), fl4RateMin );
			fltx4 fl4Field = *pRampField;
			fltx4 fl4RampVal = AddSIMD( MulSIMD( fl4Rate, fl4ScaleFactor ), fl4Field );
			fl4RampVal = MaxSIMD( MinSIMD( fl4RampVal, m_fl4MaxCmp ), m_fl4MinCmp );
			*pRampField = MaskedAssign( fl4GoodMask, fl4RampVal, fl4Field );
		}

		++pCreationTime;
		++pLifeDuration;
		++pRampField;
		++pParticleId;
	} while ( --nCtr );
}


//-----------------------------------------------------------------------------
// Ramp Scalar Spline Random operator
//-----------------------------------------------------------------------------
class C_OP_RampScalarSpline : public CParticleOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_OP_RampScalarSpline );

	uint32 GetWrittenAttributes( void ) const
	{
		return 1 << m_nField;
	}

	uint32 GetReadAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_PARTICLE_ID_MASK | PARTICLE_ATTRIBUTE_LIFE_DURATION_MASK | PARTICLE_ATTRIBUTE_CREATION_TIME_MASK;
	}

	virtual uint32 GetFilter( void ) const
	{
		return FILTER_PARAMETER_REMAPPING_MASK;
	}

	virtual void InitParams( CParticleSystemDefinition *pDef );

	virtual void Operate( CParticleCollection *pParticles, float flStrength, void *pContext ) const;

	float	m_RateMin;
	float	m_RateMax;
	float	m_flStartTime_min;
	float	m_flStartTime_max;
	float	m_flEndTime_min;
	float	m_flEndTime_max;
	float	m_flBias;
	fltx4	m_fl4MinCmp;
	fltx4	m_fl4MaxCmp;
	int		m_nField;
	bool	m_bProportionalOp;
	bool	m_bEaseOut;
};

DEFINE_PARTICLE_OPERATOR( C_OP_RampScalarSpline, "Ramp Scalar Spline Random", OPERATOR_GENERIC );

BEGIN_PARTICLE_OPERATOR_UNPACK( C_OP_RampScalarSpline )
	DMXELEMENT_UNPACK_FIELD_USERDATA( "ramp field", "3", int, m_nField, "intchoice particlefield_scalar" )
	DMXELEMENT_UNPACK_FIELD( "ramp rate min", "0", float, m_RateMin )
	DMXELEMENT_UNPACK_FIELD( "ramp rate max", "0", float, m_RateMax )
	DMXELEMENT_UNPACK_FIELD( "start time min", "0", float, m_flStartTime_min )
	DMXELEMENT_UNPACK_FIELD( "start time max", "0", float, m_flStartTime_max )
	DMXELEMENT_UNPACK_FIELD( "end time min", "1", float, m_flEndTime_min )
	DMXELEMENT_UNPACK_FIELD( "end time max", "1", float, m_flEndTime_max )
	DMXELEMENT_UNPACK_FIELD( "start/end proportional", "1", bool, m_bProportionalOp )
	DMXELEMENT_UNPACK_FIELD( "ease out", "0", bool, m_bEaseOut )
	DMXELEMENT_UNPACK_FIELD( "bias", ".5", float, m_flBias )
END_PARTICLE_OPERATOR_UNPACK( C_OP_RampScalarSpline )

void C_OP_RampScalarSpline::InitParams( CParticleSystemDefinition *pDef )
{
	SET_FIELD_CLAMP_RANGE( m_nField, m_fl4MinCmp, m_fl4MaxCmp );
}

void C_OP_RampScalarSpline::Operate( CParticleCollection *pParticles, float flStrength, void *pContext ) const
{
	C4IAttributeIterator pParticleId( PARTICLE_ATTRIBUTE_PARTICLE_ID, pParticles );
	CM128AttributeIterator pCreationTime( PARTICLE_ATTRIBUTE_CREATION_TIME, pParticles );
	CM128AttributeIterator pLifeDuration( PARTICLE_ATTRIBUTE_LIFE_DURATION, pParticles );
	CM128AttributeWriteIterator pRampField( m_nField, pParticles );

	fltx4 fl4CurTime = pParticles->m_fl4CurTime;
	fltx4 fl4ScaleFactor = ReplicateX4( pParticles->m_flDt * flStrength );

	fltx4 fl4StartTimeMin = ReplicateX4( m_flStartTime_min );
	fltx4 fl4StartTimeWidth = ReplicateX4( m_flStartTime_max - m_flStartTime_min );
	fltx4 fl4EndTimeMin = ReplicateX4( m_flEndTime_min );
	fltx4 fl4EndTimeWidth = ReplicateX4( m_flEndTime_max - m_flEndTime_min );
	fltx4 fl4RateMin = ReplicateX4( m_RateMin );
	fltx4 fl4RateWidth = ReplicateX4( m_RateMax - m_RateMin );

	fltx4 fl4BiasParam = PreCalcBiasParameter( ReplicateX4( m_flBias ) );

	int nRandomOffset = pParticles->OperatorRandomSampleOffset();
	int nCtr = pParticles->m_nPaddedActiveParticles;
	do
	{
		fltx4 fl4LifeDuration = *pLifeDuration;
		fltx4 fl4StartTime = pParticles->RandomFloat( *pParticleId, nRandomOffset + 11 );
		fl4StartTime = AddSIMD( MulSIMD( fl4StartTime, fl4StartTimeWidth ), fl4StartTimeMin );
		fltx4 fl4EndTime = pParticles->RandomFloat( *pParticleId, nRandomOffset + 12 );
		fl4EndTime = AddSIMD( MulSIMD( fl4EndTime, fl4EndTimeWidth ), fl4EndTimeMin );

		fltx4 fl4GoodMask = CmpGtSIMD( fl4LifeDuration, Four_Zeros );
		fltx4 fl4LifeTime;
		if ( m_bProportionalOp )
		{
			fl4LifeTime = MulSIMD( SubSIMD( fl4CurTime, *pCreationTime ), ReciprocalEstSIMD( fl4LifeDuration ) );
		}
		else
		{
			fl4LifeTime = SubSIMD( fl4CurTime, *pCreationTime );
		}

		fl4GoodMask = AndSIMD( CmpLeSIMD( fl4StartTime, fl4LifeTime ), fl4GoodMask );
		fl4GoodMask = AndSIMD( fl4GoodMask, CmpLtSIMD( fl4LifeTime, fl4EndTime ) );

		if ( IsAnyNegative( fl4GoodMask ) )
		{
			fltx4 fl4Duration = AddSIMD( SubSIMD( fl4EndTime, fl4StartTime ), Four_Epsilons );
			fltx4 fl4Rate = pParticles->RandomFloat( *pParticleId, nRandomOffset );
			fl4Rate = AddSIMD( MulSIMD( fl4Rate, fl4RateWidth ), fl4RateMin );

			fltx4 fl4LerpVal = MulSIMD( DivSIMD( Four_Ones, fl4Duration ), SubSIMD( fl4LifeTime, fl4StartTime ) );
			fl4LerpVal = MinSIMD( Four_Ones, MaxSIMD( Four_Zeros, fl4LerpVal ) );
			if ( m_bEaseOut )
			{
				fltx4 fl4HalfMask = CmpLtSIMD( Four_PointFives, fl4LerpVal );
				fl4LerpVal = MaskedAssign( fl4HalfMask, SubSIMD( Four_Ones, fl4LerpVal ), fl4LerpVal );
				fl4LerpVal = MulSIMD( fl4LerpVal, Four_Twos );
			}

			fltx4 fl4Field = *pRampField;
			fltx4 fl4RampVal = MulSIMD( MulSIMD( BiasSIMD( fl4LerpVal, fl4BiasParam ), fl4ScaleFactor ), fl4Rate );
			fl4RampVal = AddSIMD( fl4RampVal, fl4Field );
			fl4RampVal = MaxSIMD( MinSIMD( fl4RampVal, m_fl4MaxCmp ), m_fl4MinCmp );
			*pRampField = MaskedAssign( fl4GoodMask, fl4RampVal, fl4Field );
		}

		++pCreationTime;
		++pLifeDuration;
		++pRampField;
		++pParticleId;
	} while ( --nCtr );
}


//-----------------------------------------------------------------------------
// Ramp Scalar Linear Simple operator
//-----------------------------------------------------------------------------
class C_OP_RampScalarLinearSimple : public CParticleOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_OP_RampScalarLinearSimple );

	uint32 GetWrittenAttributes( void ) const
	{
		return 1 << m_nField;
	}

	uint32 GetReadAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_LIFE_DURATION_MASK | PARTICLE_ATTRIBUTE_CREATION_TIME_MASK;
	}

	virtual uint32 GetFilter( void ) const
	{
		return FILTER_PARAMETER_REMAPPING_MASK;
	}

	virtual void InitParams( CParticleSystemDefinition *pDef );

	virtual void Operate( CParticleCollection *pParticles, float flStrength, void *pContext ) const;

	float	m_Rate;
	float	m_flStartTime;
	float	m_flEndTime;
	fltx4	m_fl4MinCmp;
	fltx4	m_fl4MaxCmp;
	int		m_nField;
};

DEFINE_PARTICLE_OPERATOR( C_OP_RampScalarLinearSimple, "Ramp Scalar Linear Simple", OPERATOR_GENERIC );

BEGIN_PARTICLE_OPERATOR_UNPACK( C_OP_RampScalarLinearSimple )
	DMXELEMENT_UNPACK_FIELD_USERDATA( "ramp field", "3", int, m_nField, "intchoice particlefield_scalar" )
	DMXELEMENT_UNPACK_FIELD( "ramp rate", "0", float, m_Rate )
	DMXELEMENT_UNPACK_FIELD( "start time", "0", float, m_flStartTime )
	DMXELEMENT_UNPACK_FIELD( "end time", "1", float, m_flEndTime )
END_PARTICLE_OPERATOR_UNPACK( C_OP_RampScalarLinearSimple )

void C_OP_RampScalarLinearSimple::InitParams( CParticleSystemDefinition *pDef )
{
	SET_FIELD_CLAMP_RANGE( m_nField, m_fl4MinCmp, m_fl4MaxCmp );
}

void C_OP_RampScalarLinearSimple::Operate( CParticleCollection *pParticles, float flStrength, void *pContext ) const
{
	CM128AttributeIterator pCreationTime( PARTICLE_ATTRIBUTE_CREATION_TIME, pParticles );
	CM128AttributeIterator pLifeDuration( PARTICLE_ATTRIBUTE_LIFE_DURATION, pParticles );
	CM128AttributeWriteIterator pRampField( m_nField, pParticles );

	fltx4 fl4CurTime = pParticles->m_fl4CurTime;
	fltx4 fl4ScaleFactor = ReplicateX4( pParticles->m_flDt * flStrength );
	fltx4 fl4StartTime = ReplicateX4( m_flStartTime );
	fltx4 fl4EndTime = ReplicateX4( m_flEndTime );
	fltx4 fl4Rate = ReplicateX4( m_Rate );

	int nCtr = pParticles->m_nPaddedActiveParticles;
	do
	{
		fltx4 fl4LifeDuration = *pLifeDuration;
		fltx4 fl4LifeTime = MulSIMD( SubSIMD( fl4CurTime, *pCreationTime ), ReciprocalEstSIMD( fl4LifeDuration ) );
		fltx4 fl4GoodMask = CmpGtSIMD( fl4LifeDuration, Four_Zeros );
		fl4GoodMask = AndSIMD( fl4GoodMask, CmpLeSIMD( fl4StartTime, fl4LifeTime ) );
		fl4GoodMask = AndSIMD( fl4GoodMask, CmpLtSIMD( fl4LifeTime, fl4EndTime ) );

		if ( IsAnyNegative( fl4GoodMask ) )
		{
			fltx4 fl4Field = *pRampField;
			fltx4 fl4RampVal = AddSIMD( MulSIMD( fl4Rate, fl4ScaleFactor ), fl4Field );
			fl4RampVal = MaxSIMD( MinSIMD( fl4RampVal, m_fl4MaxCmp ), m_fl4MinCmp );
			*pRampField = MaskedAssign( fl4GoodMask, fl4RampVal, fl4Field );
		}

		++pCreationTime;
		++pLifeDuration;
		++pRampField;
	} while ( --nCtr );
}


//-----------------------------------------------------------------------------
// Ramp Scalar Spline Simple operator
//-----------------------------------------------------------------------------
class C_OP_RampScalarSplineSimple : public CParticleOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_OP_RampScalarSplineSimple );

	uint32 GetWrittenAttributes( void ) const
	{
		return 1 << m_nField;
	}

	uint32 GetReadAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_LIFE_DURATION_MASK | PARTICLE_ATTRIBUTE_CREATION_TIME_MASK;
	}

	virtual uint32 GetFilter( void ) const
	{
		return FILTER_PARAMETER_REMAPPING_MASK;
	}

	virtual void InitParams( CParticleSystemDefinition *pDef );

	virtual void Operate( CParticleCollection *pParticles, float flStrength, void *pContext ) const;

	float	m_Rate;
	float	m_flStartTime;
	float	m_flEndTime;
	fltx4	m_fl4MinCmp;
	fltx4	m_fl4MaxCmp;
	int		m_nField;
	bool	m_bEaseOut;
};

DEFINE_PARTICLE_OPERATOR( C_OP_RampScalarSplineSimple, "Ramp Scalar Spline Simple", OPERATOR_GENERIC );

BEGIN_PARTICLE_OPERATOR_UNPACK( C_OP_RampScalarSplineSimple )
	DMXELEMENT_UNPACK_FIELD_USERDATA( "ramp field", "3", int, m_nField, "intchoice particlefield_scalar" )
	DMXELEMENT_UNPACK_FIELD( "ramp rate", "0", float, m_Rate )
	DMXELEMENT_UNPACK_FIELD( "start time", "0", float, m_flStartTime )
	DMXELEMENT_UNPACK_FIELD( "end time", "1", float, m_flEndTime )
	DMXELEMENT_UNPACK_FIELD( "ease out", "0", bool, m_bEaseOut )
END_PARTICLE_OPERATOR_UNPACK( C_OP_RampScalarSplineSimple )

void C_OP_RampScalarSplineSimple::InitParams( CParticleSystemDefinition *pDef )
{
	SET_FIELD_CLAMP_RANGE( m_nField, m_fl4MinCmp, m_fl4MaxCmp );
}

void C_OP_RampScalarSplineSimple::Operate( CParticleCollection *pParticles, float flStrength, void *pContext ) const
{
	CM128AttributeIterator pCreationTime( PARTICLE_ATTRIBUTE_CREATION_TIME, pParticles );
	CM128AttributeIterator pLifeDuration( PARTICLE_ATTRIBUTE_LIFE_DURATION, pParticles );
	CM128AttributeWriteIterator pRampField( m_nField, pParticles );

	fltx4 fl4CurTime = pParticles->m_fl4CurTime;
	fltx4 fl4ScaleFactor = ReplicateX4( pParticles->m_flDt * flStrength );
	fltx4 fl4StartTime = ReplicateX4( m_flStartTime );
	fltx4 fl4EndTime = ReplicateX4( m_flEndTime );
	fltx4 fl4Rate = ReplicateX4( m_Rate );

	int nCtr = pParticles->m_nPaddedActiveParticles;
	do
	{
		fltx4 fl4LifeDuration = *pLifeDuration;
		fltx4 fl4LifeTime = MulSIMD( SubSIMD( fl4CurTime, *pCreationTime ), ReciprocalEstSIMD( fl4LifeDuration ) );
		fltx4 fl4GoodMask = CmpGtSIMD( fl4LifeDuration, Four_Zeros );
		fl4GoodMask = AndSIMD( fl4GoodMask, CmpLeSIMD( fl4StartTime, fl4LifeTime ) );
		fl4GoodMask = AndSIMD( fl4GoodMask, CmpLtSIMD( fl4LifeTime, fl4EndTime ) );

		if ( IsAnyNegative( fl4GoodMask ) )
		{
			fltx4 fl4Duration = AddSIMD( SubSIMD( fl4EndTime, fl4StartTime ), Four_Epsilons );
			fltx4 fl4LerpVal = MulSIMD( DivSIMD( Four_Ones, fl4Duration ), SubSIMD( fl4LifeTime, fl4StartTime ) );
			fl4LerpVal = MinSIMD( Four_Ones, MaxSIMD( Four_Zeros, fl4LerpVal ) );
			if ( m_bEaseOut )
			{
				fltx4 fl4HalfMask = CmpLtSIMD( Four_PointFives, fl4LerpVal );
				fl4LerpVal = MaskedAssign( fl4HalfMask, SubSIMD( Four_Ones, fl4LerpVal ), fl4LerpVal );
				fl4LerpVal = MulSIMD( fl4LerpVal, Four_Twos );
			}

			fltx4 fl4Field = *pRampField;
			fltx4 fl4RampVal = MulSIMD( MulSIMD( SimpleSpline( fl4LerpVal ), fl4ScaleFactor ), fl4Rate );
			fl4RampVal = AddSIMD( fl4RampVal, fl4Field );
			fl4RampVal = MaxSIMD( MinSIMD( fl4RampVal, m_fl4MaxCmp ), m_fl4MinCmp );
			*pRampField = MaskedAssign( fl4GoodMask, fl4RampVal, fl4Field );
		}

		++pCreationTime;
		++pLifeDuration;
		++pRampField;
	} while ( --nCtr );
}


//-----------------------------------------------------------------------------
// Noise Scalar operator
//-----------------------------------------------------------------------------
class C_OP_Noise : public CParticleOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_OP_Noise );

	uint32 GetWrittenAttributes( void ) const
	{
		return 1 << m_nFieldOutput;
	}

	uint32 GetReadAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_XYZ_MASK;
	}

	virtual uint32 GetFilter( void ) const
	{
		return FILTER_PARAMETER_REMAPPING_MASK;
	}

	virtual void Operate( CParticleCollection *pParticles, float flStrength, void *pContext ) const;

	int		m_nFieldOutput;
	float	m_flOutputMin;
	float	m_flOutputMax;
	fltx4	m_fl4NoiseScale;
};

DEFINE_PARTICLE_OPERATOR( C_OP_Noise, "Noise Scalar", OPERATOR_GENERIC );

BEGIN_PARTICLE_OPERATOR_UNPACK( C_OP_Noise )
	DMXELEMENT_UNPACK_FIELD( "noise coordinate scale", "0.1", float, m_fl4NoiseScale )
	DMXELEMENT_UNPACK_FIELD_USERDATA( "output field", "3", int, m_nFieldOutput, "intchoice particlefield_scalar" )
	DMXELEMENT_UNPACK_FIELD( "output minimum", "0", float, m_flOutputMin )
	DMXELEMENT_UNPACK_FIELD( "output maximum", "1", float, m_flOutputMax )
END_PARTICLE_OPERATOR_UNPACK( C_OP_Noise )

void C_OP_Noise::Operate( CParticleCollection *pParticles, float flStrength, void *pContext ) const
{
	CM128AttributeWriteIterator pAttr( m_nFieldOutput, pParticles );
	C4VAttributeIterator pXYZ( PARTICLE_ATTRIBUTE_XYZ, pParticles );

	// NOTE: shipped: only the first lane of m_fl4NoiseScale is unpacked, and it is used as-is (not replicated)
	fltx4 CoordScale = m_fl4NoiseScale;

	float fMin = m_flOutputMin;
	float fMax = m_flOutputMax;

	if ( ATTRIBUTES_WHICH_ARE_ANGLES & ( 1 << m_nFieldOutput ) )
	{
		fMin *= ( M_PI / 180.0 );
		fMax *= ( M_PI / 180.0 );
	}

	// calculate coefficients. noise retuns -1..1
	float flValueScale = ( fMax - fMin ) * 0.5f;
	fltx4 ValueScale = ReplicateX4( flValueScale );
	fltx4 ValueBase = ReplicateX4( flValueScale + fMin );

	int nActive = pParticles->m_nPaddedActiveParticles;
	do
	{
		FourVectors Coord = *pXYZ;
		Coord *= CoordScale;
		*pAttr = AddSIMD( MulSIMD( NoiseSIMD( Coord ), ValueScale ), ValueBase );
		++pAttr;
		++pXYZ;
	} while ( --nActive );
}


//-----------------------------------------------------------------------------
// Noise Vector operator
//-----------------------------------------------------------------------------
class C_OP_VectorNoise : public CParticleOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_OP_VectorNoise );

	uint32 GetWrittenAttributes( void ) const
	{
		return 1 << m_nFieldOutput;
	}

	uint32 GetReadAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_XYZ_MASK;
	}

	virtual uint32 GetFilter( void ) const
	{
		return FILTER_PARAMETER_REMAPPING_MASK;
	}

	virtual void Operate( CParticleCollection *pParticles, float flStrength, void *pContext ) const;

	int		m_nFieldOutput;
	Vector	m_vecOutputMin;
	Vector	m_vecOutputMax;
	fltx4	m_fl4NoiseScale;
	bool	m_bAdditive;
};

DEFINE_PARTICLE_OPERATOR( C_OP_VectorNoise, "Noise Vector", OPERATOR_GENERIC );

BEGIN_PARTICLE_OPERATOR_UNPACK( C_OP_VectorNoise )
	DMXELEMENT_UNPACK_FIELD( "noise coordinate scale", "0.1", float, m_fl4NoiseScale )
	DMXELEMENT_UNPACK_FIELD_USERDATA( "output field", "6", int, m_nFieldOutput, "intchoice particlefield_vector" )
	DMXELEMENT_UNPACK_FIELD( "output minimum", "0 0 0", Vector, m_vecOutputMin )
	DMXELEMENT_UNPACK_FIELD( "output maximum", "1 1 1", Vector, m_vecOutputMax )
	DMXELEMENT_UNPACK_FIELD( "additive", "0", bool, m_bAdditive )
END_PARTICLE_OPERATOR_UNPACK( C_OP_VectorNoise )

void C_OP_VectorNoise::Operate( CParticleCollection *pParticles, float flStrength, void *pContext ) const
{
	C4VAttributeWriteIterator pAttr( m_nFieldOutput, pParticles );
	C4VAttributeIterator pXYZ( PARTICLE_ATTRIBUTE_XYZ, pParticles );

	// NOTE: shipped: only the first lane of m_fl4NoiseScale is unpacked, and it is used as-is (not replicated)
	fltx4 CoordScale = m_fl4NoiseScale;

	// calculate coefficients. noise retuns -1..1
	fltx4 ValueScaleX = ReplicateX4( ( m_vecOutputMax.x - m_vecOutputMin.x ) * 0.5f );
	fltx4 ValueBaseX = ReplicateX4( ( m_vecOutputMax.x - m_vecOutputMin.x ) * 0.5f + m_vecOutputMin.x );
	fltx4 ValueScaleY = ReplicateX4( ( m_vecOutputMax.y - m_vecOutputMin.y ) * 0.5f );
	fltx4 ValueBaseY = ReplicateX4( ( m_vecOutputMax.y - m_vecOutputMin.y ) * 0.5f + m_vecOutputMin.y );
	fltx4 ValueScaleZ = ReplicateX4( ( m_vecOutputMax.z - m_vecOutputMin.z ) * 0.5f );
	fltx4 ValueBaseZ = ReplicateX4( ( m_vecOutputMax.z - m_vecOutputMin.z ) * 0.5f + m_vecOutputMin.z );

	FourVectors ofs_y;
	ofs_y.DuplicateVector( Vector( 100000.5f, 300000.25f, 9000000.75f ) );
	FourVectors ofs_z;
	ofs_z.DuplicateVector( Vector( 110000.25f, 310000.75f, 9100000.5f ) );

	int nActive = pParticles->m_nActiveParticles;
	if ( m_bAdditive )
	{
		fltx4 fl4_dt = ReplicateX4( pParticles->m_flDt );
		for ( int i = 0; i < nActive; i += 4 )
		{
			FourVectors Coord = *pXYZ;
			Coord *= CoordScale;
			pAttr->x = AddSIMD( MulSIMD( AddSIMD( MulSIMD( NoiseSIMD( Coord ), ValueScaleX ), ValueBaseX ), fl4_dt ), pAttr->x );
			Coord += ofs_y;
			pAttr->y = AddSIMD( MulSIMD( AddSIMD( MulSIMD( NoiseSIMD( Coord ), ValueScaleY ), ValueBaseY ), fl4_dt ), pAttr->y );
			Coord += ofs_z;
			pAttr->z = AddSIMD( MulSIMD( AddSIMD( MulSIMD( NoiseSIMD( Coord ), ValueScaleZ ), ValueBaseZ ), fl4_dt ), pAttr->z );
			++pAttr;
			++pXYZ;
		}
	}
	else
	{
		for ( int i = 0; i < nActive; i += 4 )
		{
			FourVectors Coord = *pXYZ;
			Coord *= CoordScale;
			pAttr->x = AddSIMD( MulSIMD( NoiseSIMD( Coord ), ValueScaleX ), ValueBaseX );
			Coord += ofs_y;
			pAttr->y = AddSIMD( MulSIMD( NoiseSIMD( Coord ), ValueScaleY ), ValueBaseY );
			Coord += ofs_z;
			pAttr->z = AddSIMD( MulSIMD( NoiseSIMD( Coord ), ValueScaleZ ), ValueBaseZ );
			++pAttr;
			++pXYZ;
		}
	}
}

//-----------------------------------------------------------------------------
// Lifespan decay operator
//-----------------------------------------------------------------------------
class C_OP_Decay : public CParticleOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_OP_Decay );

	uint32 GetWrittenAttributes( void ) const
	{
		return 0;
	}

	uint32 GetReadAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_CREATION_TIME_MASK | PARTICLE_ATTRIBUTE_LIFE_DURATION_MASK;
	}

	virtual uint32 GetFilter( void ) const
	{
		return FILTER_LIFE_DURATION_MASK;
	}

	virtual void Operate( CParticleCollection *pParticles, float flStrength, void *pContext ) const;
};

DEFINE_PARTICLE_OPERATOR( C_OP_Decay, "Lifespan Decay", OPERATOR_GENERIC );

BEGIN_PARTICLE_OPERATOR_UNPACK( C_OP_Decay )
END_PARTICLE_OPERATOR_UNPACK( C_OP_Decay )

void C_OP_Decay::Operate( CParticleCollection *pParticles, float flStrength, void *pContext ) const
{
	CM128AttributeIterator pCreationTime( PARTICLE_ATTRIBUTE_CREATION_TIME, pParticles );
	CM128AttributeIterator pLifeDuration( PARTICLE_ATTRIBUTE_LIFE_DURATION, pParticles );

	fltx4 fl4CurTime = pParticles->m_fl4CurTime;
	int nLimit = pParticles->m_nPaddedActiveParticles << 2;

	for ( int i = 0; i < nLimit; i += 4 )
	{
		fltx4 fl4LifeDuration = *pLifeDuration;
		fltx4 fl4Age = SubSIMD( fl4CurTime, *pCreationTime );
		fltx4 fl4KillMask = OrSIMD( CmpGeSIMD( fl4Age, fl4LifeDuration ), CmpLeSIMD( fl4LifeDuration, Four_Zeros ) );
		int nMask = TestSignSIMD( fl4KillMask );
		if ( nMask )
		{
			if ( nMask & 1 )
				pParticles->KillParticle( i );
			if ( nMask & 2 )
				pParticles->KillParticle( i + 1 );
			if ( nMask & 4 )
				pParticles->KillParticle( i + 2 );
			if ( nMask & 8 )
				pParticles->KillParticle( i + 3 );
		}
		++pCreationTime;
		++pLifeDuration;
	}
}


//-----------------------------------------------------------------------------
// Velocity decay operator
//-----------------------------------------------------------------------------
class C_OP_VelocityDecay : public CParticleOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_OP_VelocityDecay );

	uint32 GetWrittenAttributes( void ) const
	{
		return 0;
	}

	uint32 GetReadAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_XYZ_MASK | PARTICLE_ATTRIBUTE_PREV_XYZ_MASK;
	}

	virtual uint32 GetFilter( void ) const
	{
		return FILTER_LIFE_DURATION_MASK;
	}

	virtual void InitParams( CParticleSystemDefinition *pDef )
	{
		m_flMinVelocity = MAX( 0.001, m_flMinVelocity );
	}

	virtual void Operate( CParticleCollection *pParticles, float flStrength, void *pContext ) const;

	float m_flMinVelocity;
};

DEFINE_PARTICLE_OPERATOR( C_OP_VelocityDecay, "Lifespan Minimum Velocity Decay", OPERATOR_GENERIC );

BEGIN_PARTICLE_OPERATOR_UNPACK( C_OP_VelocityDecay )
	DMXELEMENT_UNPACK_FIELD( "minimum velocity", "1", float, m_flMinVelocity )
END_PARTICLE_OPERATOR_UNPACK( C_OP_VelocityDecay )

void C_OP_VelocityDecay::Operate( CParticleCollection *pParticles, float flStrength, void *pContext ) const
{
	fltx4 fl4MinVelocity = ReplicateX4( m_flMinVelocity );
	C4VAttributeIterator pXYZ( PARTICLE_ATTRIBUTE_XYZ, pParticles );
	C4VAttributeIterator pPrevXYZ( PARTICLE_ATTRIBUTE_PREV_XYZ, pParticles );

	fltx4 fl4Dt = ReplicateX4( pParticles->m_flDt );
	fltx4 fl4OODt = ReciprocalEstSIMD( fl4Dt );
	int nLimit = pParticles->m_nPaddedActiveParticles << 2;

	for ( int i = 0; i < nLimit; i += 4 )
	{
		fltx4 fl4DX = SubSIMD( pXYZ->x, pPrevXYZ->x );
		fltx4 fl4DY = SubSIMD( pXYZ->y, pPrevXYZ->y );
		fltx4 fl4DZ = SubSIMD( pXYZ->z, pPrevXYZ->z );
		fltx4 fl4LenSq = AddSIMD( AddSIMD( MulSIMD( fl4DX, fl4DX ), MulSIMD( fl4DY, fl4DY ) ), MulSIMD( fl4DZ, fl4DZ ) );
		fltx4 fl4Velocity = MulSIMD( SqrtSIMD( fl4LenSq ), fl4OODt );
		fltx4 fl4KillMask = CmpLeSIMD( fl4Velocity, fl4MinVelocity );
		int nMask = TestSignSIMD( fl4KillMask );
		if ( nMask )
		{
			if ( nMask & 1 )
				pParticles->KillParticle( i );
			if ( nMask & 2 )
				pParticles->KillParticle( i + 1 );
			if ( nMask & 4 )
				pParticles->KillParticle( i + 2 );
			if ( nMask & 8 )
				pParticles->KillParticle( i + 3 );
		}
		++pXYZ;
		++pPrevXYZ;
	}
}


//-----------------------------------------------------------------------------
// Alpha decay operator
//-----------------------------------------------------------------------------
class C_OP_AlphaDecay : public CParticleOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_OP_AlphaDecay );

	uint32 GetWrittenAttributes( void ) const
	{
		return 0;
	}

	uint32 GetReadAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_ALPHA_MASK | PARTICLE_ATTRIBUTE_ALPHA2_MASK;
	}

	virtual uint32 GetFilter( void ) const
	{
		return FILTER_LIFE_DURATION_MASK;
	}

	virtual void InitParams( CParticleSystemDefinition *pDef )
	{
		m_flMinAlpha = MAX( 0.0f, m_flMinAlpha );
	}

	virtual void Operate( CParticleCollection *pParticles, float flStrength, void *pContext ) const;

	float m_flMinAlpha;
};

DEFINE_PARTICLE_OPERATOR( C_OP_AlphaDecay, "Lifespan Minimum Alpha Decay", OPERATOR_GENERIC );

BEGIN_PARTICLE_OPERATOR_UNPACK( C_OP_AlphaDecay )
	DMXELEMENT_UNPACK_FIELD( "minimum alpha", "0", float, m_flMinAlpha )
END_PARTICLE_OPERATOR_UNPACK( C_OP_AlphaDecay )

void C_OP_AlphaDecay::Operate( CParticleCollection *pParticles, float flStrength, void *pContext ) const
{
	fltx4 fl4MinAlpha = ReplicateX4( m_flMinAlpha );
	CM128AttributeIterator pAlpha( PARTICLE_ATTRIBUTE_ALPHA, pParticles );
	CM128AttributeIterator pAlpha2( PARTICLE_ATTRIBUTE_ALPHA2, pParticles );

	int nLimit = pParticles->m_nPaddedActiveParticles << 2;

	for ( int i = 0; i < nLimit; i += 4 )
	{
		fltx4 fl4KillMask = CmpLeSIMD( MulSIMD( *pAlpha2, *pAlpha ), fl4MinAlpha );
		int nMask = TestSignSIMD( fl4KillMask );
		if ( nMask )
		{
			if ( nMask & 1 )
				pParticles->KillParticle( i );
			if ( nMask & 2 )
				pParticles->KillParticle( i + 1 );
			if ( nMask & 4 )
				pParticles->KillParticle( i + 2 );
			if ( nMask & 8 )
				pParticles->KillParticle( i + 3 );
		}
		++pAlpha;
		++pAlpha2;
	}
}


//-----------------------------------------------------------------------------
// Lifespan maintain count decay operator
//-----------------------------------------------------------------------------
class C_OP_DecayMaintainCount : public CParticleOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_OP_DecayMaintainCount );

	uint32 GetWrittenAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_LIFE_DURATION_MASK;
	}

	uint32 GetReadAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_CREATION_TIME_MASK;
	}

	virtual uint32 GetFilter( void ) const
	{
		return FILTER_LIFE_DURATION_MASK;
	}

	virtual void InitParams( CParticleSystemDefinition *pDef )
	{
		m_nScaleControlPoint = clamp( m_nScaleControlPoint, -1, MAX_PARTICLE_CONTROL_POINTS );
		m_nScaleControlPointField = clamp( m_nScaleControlPointField, 0, 2 );
	}

	virtual size_t GetRequiredContextBytes( void ) const
	{
		return sizeof( int );
	}

	virtual void InitializeContextData( CParticleCollection *pParticles, void *pContext ) const
	{
		*( (int *)pContext ) = 0;
	}

	virtual void Operate( CParticleCollection *pParticles, float flStrength, void *pContext ) const;

	int m_nParticlesToMaintain;
	int m_nScaleControlPoint;
	int m_nScaleControlPointField;
	float m_flDecayDelay;
};

DEFINE_PARTICLE_OPERATOR( C_OP_DecayMaintainCount, "Lifespan Maintain Count Decay", OPERATOR_GENERIC );

BEGIN_PARTICLE_OPERATOR_UNPACK( C_OP_DecayMaintainCount )
	DMXELEMENT_UNPACK_FIELD( "count to maintain", "100", int, m_nParticlesToMaintain )
	DMXELEMENT_UNPACK_FIELD( "decay delay", "0", float, m_flDecayDelay )
	DMXELEMENT_UNPACK_FIELD( "maintain count scale control point", "-1", int, m_nScaleControlPoint )
	DMXELEMENT_UNPACK_FIELD( "maintain count scale control point field", "0", int, m_nScaleControlPointField )
END_PARTICLE_OPERATOR_UNPACK( C_OP_DecayMaintainCount )

void C_OP_DecayMaintainCount::Operate( CParticleCollection *pParticles, float flStrength, void *pContext ) const
{
	int nActualParticlesToMaintain = m_nParticlesToMaintain;
	if ( m_nScaleControlPoint >= 0 )
	{
		const Vector &vecScale = pParticles->GetControlPointAtCurrentTime( m_nScaleControlPoint );
		nActualParticlesToMaintain = MIN( (float)pParticles->m_pDef->m_nMaxParticles, ( (float)nActualParticlesToMaintain * vecScale[m_nScaleControlPointField] ) );
	}

	int nNumToKill = 0;
	if ( pParticles->m_nActiveParticles > nActualParticlesToMaintain )
	{
		nNumToKill = pParticles->m_nActiveParticles - *( (int *)pContext ) - nActualParticlesToMaintain;
	}

	for ( int i = 0; i < pParticles->m_nActiveParticles; ++i )
	{
		const float *tCreate = pParticles->GetFloatAttributePtr( PARTICLE_ATTRIBUTE_CREATION_TIME, i );
		float *tLifespan = pParticles->GetFloatAttributePtrForWrite( PARTICLE_ATTRIBUTE_LIFE_DURATION, i );
		if ( ( pParticles->m_flCurTime - *tCreate ) > *tLifespan )
		{
			pParticles->KillParticle( i );
			*( (int *)pContext ) = MAX( 0, *( (int *)pContext ) - 1 );
			nNumToKill--;
		}
		else if ( nNumToKill > 0 )
		{
			float flNewLife = m_flDecayDelay + pParticles->m_flCurTime;
			if ( *tLifespan > flNewLife )
			{
				*tLifespan = flNewLife - *tCreate;
				++*( (int *)pContext );
				nNumToKill--;
			}
		}
	}
}


//-----------------------------------------------------------------------------
// Random Cull Operator
//-----------------------------------------------------------------------------
class C_OP_Cull : public CParticleOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_OP_Cull );

	uint32 GetWrittenAttributes( void ) const
	{
		return 0;
	}

	virtual uint32 GetFilter( void ) const
	{
		return FILTER_LIFE_DURATION_MASK;
	}

	uint32 GetReadAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_CREATION_TIME_MASK | PARTICLE_ATTRIBUTE_LIFE_DURATION_MASK;
	}

	virtual void Operate( CParticleCollection *pParticles, float flStrength, void *pContext ) const;

	float m_flCullPerc;
	float m_flCullStart;
	float m_flCullEnd;
	float m_flCullExp;
};

DEFINE_PARTICLE_OPERATOR( C_OP_Cull, "Cull Random", OPERATOR_GENERIC );

BEGIN_PARTICLE_OPERATOR_UNPACK( C_OP_Cull )
	DMXELEMENT_UNPACK_FIELD( "Cull Start Time", "0", float, m_flCullStart )
	DMXELEMENT_UNPACK_FIELD( "Cull End Time", "1", float, m_flCullEnd )
	DMXELEMENT_UNPACK_FIELD( "Cull Time Exponent", "1", float, m_flCullExp )
	DMXELEMENT_UNPACK_FIELD( "Cull Percentage", "0.5", float, m_flCullPerc )
END_PARTICLE_OPERATOR_UNPACK( C_OP_Cull )

void C_OP_Cull::Operate( CParticleCollection *pParticles, float flStrength, void *pContext ) const
{
	for ( int i = 0; i < pParticles->m_nActiveParticles; ++i )
	{
		const float *pCreationTime = pParticles->GetFloatAttributePtr( PARTICLE_ATTRIBUTE_CREATION_TIME, i );
		const float *pLifeDuration = pParticles->GetFloatAttributePtr( PARTICLE_ATTRIBUTE_LIFE_DURATION, i );
		float flCullRank = pParticles->RandomFloat( 0.0f, 1.0f );
		float flCullTime = pParticles->RandomFloatExp( m_flCullStart, m_flCullEnd, m_flCullExp );
		if ( flCullRank > ( m_flCullPerc * flStrength ) )
			continue;

		float flParticleAge = ( pParticles->m_flCurTime - *pCreationTime ) / *pLifeDuration;
		if ( flParticleAge > 1.0f )
			flParticleAge = 1.0f;
		else if ( 0.0f > flParticleAge )
			flParticleAge = 0.0f;

		if ( ( flParticleAge >= m_flCullStart ) && ( flParticleAge <= m_flCullEnd ) && ( flParticleAge >= flCullTime ) )
		{
			pParticles->KillParticle( i );
		}
	}
}


//-----------------------------------------------------------------------------
// generic spin operator
//-----------------------------------------------------------------------------
class CGeneralSpin : public CParticleOperatorInstance
{
protected:
	virtual int GetAttributeToSpin( void ) const = 0;

	uint32 GetWrittenAttributes( void ) const
	{
		if ( m_nSpinRateDegrees != 0.0 )
			return ( 1 << GetAttributeToSpin() );
		return 0;
	}

	uint32 GetReadAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_CREATION_TIME_MASK | PARTICLE_ATTRIBUTE_LIFE_DURATION_MASK;
	}

	virtual void InitParams( CParticleSystemDefinition *pDef )
	{
		m_fSpinRateRadians = (float)m_nSpinRateDegrees * ( M_PI / 180.0f );
		m_fSpinRateMinRadians = (float)m_nSpinRateMinDegrees * ( M_PI / 180.0f );
	}

	virtual void Operate( CParticleCollection *pParticles, float flStrength, void *pContext ) const;

	int m_nSpinRateDegrees;
	int m_nSpinRateMinDegrees;
	float m_fSpinRateRadians;
	float m_fSpinRateStopTime;
	float m_fSpinRateMinRadians;
};

void CGeneralSpin::Operate( CParticleCollection *pParticles, float flStrength, void *pContext ) const
{
	float fCurSpinRate = m_fSpinRateRadians * flStrength;

	if ( fCurSpinRate == 0.0f )
		return;

	bool bIsInterpolating = pParticles->IsUsingInterpolatedRendering();

	float dt = pParticles->m_flDt;
	float drot = dt * fabs( fCurSpinRate * 2.0f * M_PI );
	if ( m_fSpinRateStopTime == 0.0f )
	{
		drot = fmod( drot, (float)( 2.0f * M_PI ) );
	}
	if ( fCurSpinRate < 0.0f )
	{
		drot = -drot;
	}
	fltx4 Rot_Add = ReplicateX4( drot );
	fltx4 Pi_2 = ReplicateX4( 2.0f * M_PI );
	fltx4 nPi_2 = ReplicateX4( -2.0f * M_PI );

	// FIXME: This is wrong
	fltx4 minSpeedRadians = ReplicateX4( dt * fabs( m_fSpinRateMinRadians * 2.0f * M_PI ) );

	fltx4 now = pParticles->m_fl4CurTime;
	fltx4 SpinRateStopTime = ReplicateX4( m_fSpinRateStopTime );

	CM128AttributeIterator pCreationTimeStamp( PARTICLE_ATTRIBUTE_CREATION_TIME, pParticles );
	CM128AttributeIterator pLifeDuration( PARTICLE_ATTRIBUTE_LIFE_DURATION, pParticles );
	CM128AttributeWriteIterator pRot( GetAttributeToSpin(), pParticles );

	int nActive = pParticles->m_nActiveParticles;
	for ( int i = 0; i < nActive; i += 4 )
	{
		// HACK: Rather than redo this, I'm simply remapping the stop time into the percentage of lifetime, rather than seconds
		fltx4 SpinFadePerc = Four_Zeros;
		fltx4 OOSpinFadeRate = Four_Zeros;
		if ( m_fSpinRateStopTime )
		{
			SpinFadePerc = MulSIMD( *pLifeDuration, SpinRateStopTime );
			OOSpinFadeRate = DivSIMD( Four_Ones, SpinFadePerc );
		}

		fltx4 Age = SubSIMD( now, *pCreationTimeStamp );
		fltx4 RScale = MaxSIMD( Four_Zeros, SubSIMD( Four_Ones, MulSIMD( Age, OOSpinFadeRate ) ) );

		// Cap the rotation at a minimum speed
		fltx4 deltaRot = MulSIMD( RScale, Rot_Add );
		fltx4 Tooslow = CmpLeSIMD( deltaRot, minSpeedRadians );
		deltaRot = OrSIMD( AndSIMD( Tooslow, minSpeedRadians ), AndNotSIMD( Tooslow, deltaRot ) );
		fltx4 NewRot = AddSIMD( deltaRot, *pRot );

		// now, cap at +/- 2*pi
		if ( !bIsInterpolating )
		{
			fltx4 Toobig = CmpGeSIMD( NewRot, Pi_2 );
			fltx4 Toosmall = CmpLeSIMD( NewRot, nPi_2 );

			NewRot = OrSIMD( AndSIMD( Toobig, SubSIMD( NewRot, Pi_2 ) ),
				AndNotSIMD( Toobig, NewRot ) );

			NewRot = OrSIMD( AndSIMD( Toosmall, AddSIMD( NewRot, Pi_2 ) ),
				AndNotSIMD( Toosmall, NewRot ) );
		}

		*pRot = NewRot;

		++pRot;
		++pCreationTimeStamp;
		++pLifeDuration;
	}
}


//-----------------------------------------------------------------------------
// generic spin update operator
//-----------------------------------------------------------------------------
class CSpinUpdateBase : public CParticleOperatorInstance
{
protected:
	virtual int GetAttributeToSpin( void ) const = 0;
	virtual int GetSpinSpeedAttribute( void ) const = 0;

	uint32 GetWrittenAttributes( void ) const
	{
		return ( 1 << GetAttributeToSpin() );
	}

	uint32 GetReadAttributes( void ) const
	{
		return ( 1 << GetSpinSpeedAttribute() ) | ( 1 << GetAttributeToSpin() ) | PARTICLE_ATTRIBUTE_CREATION_TIME_MASK;
	}

	virtual uint32 GetFilter( void ) const
	{
		return FILTER_ROTATION_MASK;
	}

	virtual void Operate( CParticleCollection *pParticles, float flStrength, void *pContext ) const;
};

void CSpinUpdateBase::Operate( CParticleCollection *pParticles, float flStrength, void *pContext ) const
{
	CM128AttributeIterator pCreationTimeStamp( PARTICLE_ATTRIBUTE_CREATION_TIME, pParticles );
	CM128AttributeIterator pRotationSpeed( GetSpinSpeedAttribute(), pParticles );
	CM128AttributeWriteIterator pRot( GetAttributeToSpin(), pParticles );

	fltx4 fl4CurTime = pParticles->m_fl4CurTime;
	fltx4 fl4Dt = ReplicateX4( pParticles->m_flDt );
	fltx4 fl4ScaleFactor = ReplicateX4( flStrength );

	int nActive = pParticles->m_nActiveParticles;
	for ( int i = 0; i < nActive; i += 4 )
	{
		fltx4 fl4SimTime = MinSIMD( fl4Dt, SubSIMD( fl4CurTime, *pCreationTimeStamp ) );
		fl4SimTime = MulSIMD( fl4SimTime, fl4ScaleFactor );
		*pRot = MaddSIMD( fl4SimTime, *pRotationSpeed, *pRot );

		++pRot;
		++pRotationSpeed;
		++pCreationTimeStamp;
	}
}


class C_OP_Spin : public CGeneralSpin
{
	DECLARE_PARTICLE_OPERATOR( C_OP_Spin );

	int GetAttributeToSpin( void ) const
	{
		return PARTICLE_ATTRIBUTE_ROTATION;
	}
};

DEFINE_PARTICLE_OPERATOR( C_OP_Spin, "Rotation Spin Roll", OPERATOR_GENERIC );

BEGIN_PARTICLE_OPERATOR_UNPACK( C_OP_Spin )
	DMXELEMENT_UNPACK_FIELD( "spin_rate_degrees", "0", int, m_nSpinRateDegrees )
	DMXELEMENT_UNPACK_FIELD( "spin_stop_time", "0", float, m_fSpinRateStopTime )
	DMXELEMENT_UNPACK_FIELD( "spin_rate_min", "0", int, m_nSpinRateMinDegrees )
END_PARTICLE_OPERATOR_UNPACK( C_OP_Spin )


class C_OP_SpinUpdate : public CSpinUpdateBase
{
	DECLARE_PARTICLE_OPERATOR( C_OP_SpinUpdate );

	virtual int GetAttributeToSpin( void ) const
	{
		return PARTICLE_ATTRIBUTE_ROTATION;
	}

	virtual int GetSpinSpeedAttribute( void ) const
	{
		return PARTICLE_ATTRIBUTE_ROTATION_SPEED;
	}
};

DEFINE_PARTICLE_OPERATOR( C_OP_SpinUpdate, "Rotation Basic", OPERATOR_GENERIC );

BEGIN_PARTICLE_OPERATOR_UNPACK( C_OP_SpinUpdate )
END_PARTICLE_OPERATOR_UNPACK( C_OP_SpinUpdate )


class C_OP_SpinYaw : public CGeneralSpin
{
	DECLARE_PARTICLE_OPERATOR( C_OP_SpinYaw );

	int GetAttributeToSpin( void ) const
	{
		return PARTICLE_ATTRIBUTE_YAW;
	}
};

DEFINE_PARTICLE_OPERATOR( C_OP_SpinYaw, "Rotation Spin Yaw", OPERATOR_GENERIC );

BEGIN_PARTICLE_OPERATOR_UNPACK( C_OP_SpinYaw )
	DMXELEMENT_UNPACK_FIELD( "yaw_rate_degrees", "0", int, m_nSpinRateDegrees )
	DMXELEMENT_UNPACK_FIELD( "yaw_stop_time", "0", float, m_fSpinRateStopTime )
	DMXELEMENT_UNPACK_FIELD( "yaw_rate_min", "0", int, m_nSpinRateMinDegrees )
END_PARTICLE_OPERATOR_UNPACK( C_OP_SpinYaw )


//-----------------------------------------------------------------------------
// Size changing operator
//-----------------------------------------------------------------------------
class C_OP_InterpolateRadius : public CParticleOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_OP_InterpolateRadius );

	uint32 GetReadInitialAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_RADIUS_MASK;
	}

	uint32 GetWrittenAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_RADIUS_MASK;
	}

	uint32 GetReadAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_CREATION_TIME_MASK | PARTICLE_ATTRIBUTE_LIFE_DURATION_MASK;
	}

	virtual void InitParams( CParticleSystemDefinition *pDef )
	{
		if ( m_flBias == 0.0f )
			m_flBias = 0.5f;
		m_fl4BiasParam = PreCalcBiasParameter( ReplicateX4( m_flBias ) );
	}

	virtual void Operate( CParticleCollection *pParticles, float flStrength, void *pContext ) const;

	float m_flStartTime;
	float m_flEndTime;
	float m_flStartScale;
	float m_flEndScale;
	bool m_bEaseInAndOut;
	float m_flBias;
	fltx4 m_fl4BiasParam;
};

DEFINE_PARTICLE_OPERATOR( C_OP_InterpolateRadius, "Radius Scale", OPERATOR_GENERIC );

BEGIN_PARTICLE_OPERATOR_UNPACK( C_OP_InterpolateRadius )
	DMXELEMENT_UNPACK_FIELD( "start_time", "0", float, m_flStartTime )
	DMXELEMENT_UNPACK_FIELD( "end_time", "1", float, m_flEndTime )
	DMXELEMENT_UNPACK_FIELD( "radius_start_scale", "1", float, m_flStartScale )
	DMXELEMENT_UNPACK_FIELD( "radius_end_scale", "1", float, m_flEndScale )
	DMXELEMENT_UNPACK_FIELD( "ease_in_and_out", "0", bool, m_bEaseInAndOut )
	DMXELEMENT_UNPACK_FIELD( "scale_bias", "0.5", float, m_flBias )
END_PARTICLE_OPERATOR_UNPACK( C_OP_InterpolateRadius )

void C_OP_InterpolateRadius::Operate( CParticleCollection *pParticles, float flStrength, void *pContext ) const
{
	if ( m_flStartTime >= m_flEndTime )
		return;

	CM128AttributeIterator pCreationTime( PARTICLE_ATTRIBUTE_CREATION_TIME, pParticles );
	CM128AttributeIterator pLifeDuration( PARTICLE_ATTRIBUTE_LIFE_DURATION, pParticles );
	CM128AttributeWriteIterator pRadius( PARTICLE_ATTRIBUTE_RADIUS, pParticles );
	CM128InitialAttributeIterator pInitialRadius( PARTICLE_ATTRIBUTE_RADIUS, pParticles );

	fltx4 fl4StartTime = ReplicateX4( m_flStartTime );
	fltx4 fl4EndTime = ReplicateX4( m_flEndTime );
	fltx4 fl4OOTimeWidth = ReciprocalSIMD( SubSIMD( fl4EndTime, fl4StartTime ) );

	fltx4 fl4ScaleWidth = ReplicateX4( m_flEndScale - m_flStartScale );
	fltx4 fl4StartScale = ReplicateX4( m_flStartScale );

	fltx4 fl4CurTime = pParticles->m_fl4CurTime;

	int nCtr = pParticles->m_nPaddedActiveParticles;
	if ( m_bEaseInAndOut )
	{
		do
		{
			fltx4 fl4LifeDuration = *pLifeDuration;
			fltx4 fl4LifeTime = MulSIMD( SubSIMD( fl4CurTime, *pCreationTime ), ReciprocalEstSIMD( fl4LifeDuration ) );
			fltx4 fl4GoodMask = CmpGtSIMD( fl4LifeDuration, Four_Zeros );
			fl4GoodMask = AndSIMD( fl4GoodMask, CmpGeSIMD( fl4LifeTime, fl4StartTime ) );
			fl4GoodMask = AndSIMD( fl4GoodMask, CmpLtSIMD( fl4LifeTime, fl4EndTime ) );
			if ( IsAnyNegative( fl4GoodMask ) )
			{
				fltx4 fl4FadeWindow = MulSIMD( SubSIMD( fl4LifeTime, fl4StartTime ), fl4OOTimeWidth );
				fl4FadeWindow = AddSIMD( MulSIMD( SimpleSpline( fl4FadeWindow ), fl4ScaleWidth ), fl4StartScale );
				*pRadius = MaskedAssign( fl4GoodMask, MulSIMD( fl4FadeWindow, *pInitialRadius ), *pRadius );
			}
			++pCreationTime;
			++pLifeDuration;
			++pRadius;
			++pInitialRadius;
		} while ( --nCtr );
	}
	else
	{
		if ( m_flBias == 0.5f )		// no bias
		{
			do
			{
				fltx4 fl4LifeDuration = *pLifeDuration;
				fltx4 fl4LifeTime = MulSIMD( SubSIMD( fl4CurTime, *pCreationTime ), ReciprocalEstSIMD( fl4LifeDuration ) );
				fltx4 fl4GoodMask = CmpGtSIMD( fl4LifeDuration, Four_Zeros );
				fl4GoodMask = AndSIMD( fl4GoodMask, CmpGeSIMD( fl4LifeTime, fl4StartTime ) );
				fl4GoodMask = AndSIMD( fl4GoodMask, CmpLtSIMD( fl4LifeTime, fl4EndTime ) );
				if ( IsAnyNegative( fl4GoodMask ) )
				{
					fltx4 fl4FadeWindow = MulSIMD( SubSIMD( fl4LifeTime, fl4StartTime ), fl4OOTimeWidth );
					fl4FadeWindow = AddSIMD( MulSIMD( fl4FadeWindow, fl4ScaleWidth ), fl4StartScale );
					*pRadius = MaskedAssign( fl4GoodMask, MulSIMD( fl4FadeWindow, *pInitialRadius ), *pRadius );
				}
				++pCreationTime;
				++pLifeDuration;
				++pRadius;
				++pInitialRadius;
			} while ( --nCtr );
		}
		else
		{
			do
			{
				fltx4 fl4LifeDuration = *pLifeDuration;
				fltx4 fl4LifeTime = MulSIMD( SubSIMD( fl4CurTime, *pCreationTime ), ReciprocalEstSIMD( fl4LifeDuration ) );
				fltx4 fl4GoodMask = CmpGtSIMD( fl4LifeDuration, Four_Zeros );
				fl4GoodMask = AndSIMD( fl4GoodMask, CmpGeSIMD( fl4LifeTime, fl4StartTime ) );
				fl4GoodMask = AndSIMD( fl4GoodMask, CmpLtSIMD( fl4LifeTime, fl4EndTime ) );
				if ( IsAnyNegative( fl4GoodMask ) )
				{
					fltx4 fl4FadeWindow = MulSIMD( SubSIMD( fl4LifeTime, fl4StartTime ), fl4OOTimeWidth );
					fl4FadeWindow = AddSIMD( MulSIMD( BiasSIMD( fl4FadeWindow, m_fl4BiasParam ), fl4ScaleWidth ), fl4StartScale );
					*pRadius = MaskedAssign( fl4GoodMask, MulSIMD( fl4FadeWindow, *pInitialRadius ), *pRadius );
				}
				++pCreationTime;
				++pLifeDuration;
				++pRadius;
				++pInitialRadius;
			} while ( --nCtr );
		}
	}
}


//-----------------------------------------------------------------------------
// Color Fade
//-----------------------------------------------------------------------------
class C_OP_ColorInterpolate : public CParticleOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_OP_ColorInterpolate );

	uint32 GetReadInitialAttributes( void ) const
	{
		return 1 << m_nFieldOutput;
	}

	uint32 GetWrittenAttributes( void ) const
	{
		return 1 << m_nFieldOutput;
	}

	uint32 GetReadAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_CREATION_TIME_MASK | PARTICLE_ATTRIBUTE_LIFE_DURATION_MASK;
	}

	virtual void InitParams( CParticleSystemDefinition *pDef )
	{
		m_flColorFade[0] = m_ColorFade[0] * ( 1.0f / 255.0f );
		m_flColorFade[1] = m_ColorFade[1] * ( 1.0f / 255.0f );
		m_flColorFade[2] = m_ColorFade[2] * ( 1.0f / 255.0f );
	}

	virtual void Operate( CParticleCollection *pParticles, float flStrength, void *pContext ) const;

	Color m_ColorFade;
	float m_flColorFade[3];
	float m_flFadeStartTime;
	float m_flFadeEndTime;
	int m_nFieldOutput;
	bool m_bEaseInOut;
};

DEFINE_PARTICLE_OPERATOR( C_OP_ColorInterpolate, "Color Fade", OPERATOR_GENERIC );

BEGIN_PARTICLE_OPERATOR_UNPACK( C_OP_ColorInterpolate )
	DMXELEMENT_UNPACK_FIELD( "color_fade", "255 255 255 255", Color, m_ColorFade )
	DMXELEMENT_UNPACK_FIELD( "fade_start_time", "0", float, m_flFadeStartTime )
	DMXELEMENT_UNPACK_FIELD( "fade_end_time", "1", float, m_flFadeEndTime )
	DMXELEMENT_UNPACK_FIELD( "ease_in_and_out", "1", bool, m_bEaseInOut )
	DMXELEMENT_UNPACK_FIELD_USERDATA( "output field", "6", int, m_nFieldOutput, "intchoice particlefield_vector" )
END_PARTICLE_OPERATOR_UNPACK( C_OP_ColorInterpolate )

void C_OP_ColorInterpolate::Operate( CParticleCollection *pParticles, float flStrength, void *pContext ) const
{
	C4VAttributeWriteIterator pColor( m_nFieldOutput, pParticles );
	CM128AttributeIterator pCreationTime( PARTICLE_ATTRIBUTE_CREATION_TIME, pParticles );
	CM128AttributeIterator pLifeDuration( PARTICLE_ATTRIBUTE_LIFE_DURATION, pParticles );
	C4VInitialAttributeIterator pInitialColor( m_nFieldOutput, pParticles );

	if ( m_flFadeEndTime == m_flFadeStartTime )
		return;

	fltx4 ooInRange = ReplicateX4( 1.0f / ( m_flFadeEndTime - m_flFadeStartTime ) );

	fltx4 curTime = pParticles->m_fl4CurTime;
	fltx4 lowRange = ReplicateX4( m_flFadeStartTime );

	fltx4 targetR = ReplicateX4( m_flColorFade[0] );
	fltx4 targetG = ReplicateX4( m_flColorFade[1] );
	fltx4 targetB = ReplicateX4( m_flColorFade[2] );

	int nCtr = pParticles->m_nPaddedActiveParticles;
	if ( m_bEaseInOut )
	{
		do
		{
			fltx4 goodMask = CmpGtSIMD( *pLifeDuration, Four_Zeros );
			if ( IsAnyNegative( goodMask ) )
			{
				fltx4 flLifeTime = DivSIMD( SubSIMD( curTime, *pCreationTime ), *pLifeDuration );
				fltx4 T = MulSIMD( SubSIMD( flLifeTime, lowRange ), ooInRange );
				T = MinSIMD( Four_Ones, MaxSIMD( Four_Zeros, T ) );
				T = SimpleSpline( T );
				pColor->x = MaskedAssign( goodMask, AddSIMD( MulSIMD( SubSIMD( targetR, pInitialColor->x ), T ), pInitialColor->x ), pColor->x );
				pColor->y = MaskedAssign( goodMask, AddSIMD( MulSIMD( SubSIMD( targetG, pInitialColor->y ), T ), pInitialColor->y ), pColor->y );
				pColor->z = MaskedAssign( goodMask, AddSIMD( MulSIMD( SubSIMD( targetB, pInitialColor->z ), T ), pInitialColor->z ), pColor->z );
			}
			++pColor;
			++pCreationTime;
			++pLifeDuration;
			++pInitialColor;
		} while ( --nCtr );
	}
	else
	{
		do
		{
			fltx4 goodMask = CmpGtSIMD( *pLifeDuration, Four_Zeros );
			if ( IsAnyNegative( goodMask ) )
			{
				fltx4 flLifeTime = DivSIMD( SubSIMD( curTime, *pCreationTime ), *pLifeDuration );
				fltx4 T = MulSIMD( SubSIMD( flLifeTime, lowRange ), ooInRange );
				T = MinSIMD( Four_Ones, MaxSIMD( Four_Zeros, T ) );
				pColor->x = MaskedAssign( goodMask, AddSIMD( MulSIMD( SubSIMD( targetR, pInitialColor->x ), T ), pInitialColor->x ), pColor->x );
				pColor->y = MaskedAssign( goodMask, AddSIMD( MulSIMD( SubSIMD( targetG, pInitialColor->y ), T ), pInitialColor->y ), pColor->y );
				pColor->z = MaskedAssign( goodMask, AddSIMD( MulSIMD( SubSIMD( targetB, pInitialColor->z ), T ), pInitialColor->z ), pColor->z );
			}
			++pColor;
			++pCreationTime;
			++pLifeDuration;
			++pInitialColor;
		} while ( --nCtr );
	}
}


//-----------------------------------------------------------------------------
// Position Lock to Control Point
// Locks all particles to the specified control point
// Useful for making particles move with their emitter and so forth
//-----------------------------------------------------------------------------
struct PositionLockContext_t
{
	Vector m_vPrevPosition;
	matrix3x4_t m_matPrevTransform;
};

class C_OP_PositionLock : public CParticleOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_OP_PositionLock );

	uint32 GetWrittenAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_XYZ_MASK | PARTICLE_ATTRIBUTE_PREV_XYZ_MASK;
	}

	uint32 GetReadAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_CREATION_TIME_MASK | PARTICLE_ATTRIBUTE_LIFE_DURATION_MASK |
			PARTICLE_ATTRIBUTE_PARTICLE_ID_MASK;
	}

	virtual uint64 GetReadControlPointMask() const
	{
		return 1ULL << m_nControlPointNumber;
	}

	virtual void InitParams( CParticleSystemDefinition *pDef )
	{
		m_nControlPointNumber = clamp( m_nControlPointNumber, 0, MAX_PARTICLE_CONTROL_POINTS - 1 );
	}

	virtual size_t GetRequiredContextBytes( void ) const
	{
		return sizeof( PositionLockContext_t );
	}

	virtual void InitializeContextData( CParticleCollection *pParticles, void *pContext ) const
	{
		PositionLockContext_t *pCtx = reinterpret_cast<PositionLockContext_t *>( pContext );
		pCtx->m_vPrevPosition = vec3_origin;
		pParticles->GetControlPointTransformAtTime( m_nControlPointNumber, pParticles->m_flCurTime, &pCtx->m_matPrevTransform );
	}

	virtual void Operate( CParticleCollection *pParticles, float flStrength, void *pContext ) const;

	int m_nControlPointNumber;
	Vector m_vPrevPosition;
	float m_flStartTime_min;
	float m_flStartTime_max;
	float m_flStartTime_exp;
	float m_flEndTime_min;
	float m_flEndTime_max;
	float m_flEndTime_exp;
	float m_flRange;
	bool m_bLockRot;
};

DEFINE_PARTICLE_OPERATOR( C_OP_PositionLock, "Movement Lock to Control Point", OPERATOR_GENERIC );

BEGIN_PARTICLE_OPERATOR_UNPACK( C_OP_PositionLock )
	DMXELEMENT_UNPACK_FIELD( "control_point_number", "0", int, m_nControlPointNumber )
	DMXELEMENT_UNPACK_FIELD( "start_fadeout_min", "1", float, m_flStartTime_min )
	DMXELEMENT_UNPACK_FIELD( "start_fadeout_max", "1", float, m_flStartTime_max )
	DMXELEMENT_UNPACK_FIELD( "start_fadeout_exponent", "1", float, m_flStartTime_exp )
	DMXELEMENT_UNPACK_FIELD( "end_fadeout_min", "1", float, m_flEndTime_min )
	DMXELEMENT_UNPACK_FIELD( "end_fadeout_max", "1", float, m_flEndTime_max )
	DMXELEMENT_UNPACK_FIELD( "end_fadeout_exponent", "1", float, m_flEndTime_exp )
	DMXELEMENT_UNPACK_FIELD( "distance fade range", "0", float, m_flRange )
	DMXELEMENT_UNPACK_FIELD( "lock rotation", "0", bool, m_bLockRot )
END_PARTICLE_OPERATOR_UNPACK( C_OP_PositionLock )

void C_OP_PositionLock::Operate( CParticleCollection *pParticles, float flStrength, void *pContext ) const
{
	PositionLockContext_t *pCtx = reinterpret_cast<PositionLockContext_t *>( pContext );

	Vector vecControlPoint = pParticles->GetControlPointAtCurrentTime( m_nControlPointNumber );

	// At initialization, set prevposition to the control point to prevent random data issues
	if ( pCtx->m_vPrevPosition == Vector( 0, 0, 0 ) )
	{
		pCtx->m_vPrevPosition = vecControlPoint;
		pParticles->GetControlPointTransformAtTime( m_nControlPointNumber, pParticles->m_flCurTime, &pCtx->m_matPrevTransform );
	}

	// NOTE: shipped - matCurrentTransform is only filled in when m_bLockRot is set, but is always
	// copied into the context at the end
	matrix3x4_t matCurrentTransform;
	matrix3x4_t matTransformLock;
	if ( m_bLockRot )
	{
		pParticles->GetControlPointTransformAtTime( m_nControlPointNumber, pParticles->m_flCurTime, &matCurrentTransform );
		matrix3x4_t matPrev;
		MatrixInvert( pCtx->m_matPrevTransform, matPrev );
		ConcatTransforms( matCurrentTransform, matPrev, matTransformLock );
	}

	int nContext = GetSIMDRandContext();

	Vector vDelta = vecControlPoint - pCtx->m_vPrevPosition;
	vDelta *= flStrength;

	FourVectors v4Delta;
	v4Delta.DuplicateVector( vDelta );

	FourVectors v4ControlPoint;
	v4ControlPoint.DuplicateVector( vecControlPoint );

	C4VAttributeWriteIterator pXYZ( PARTICLE_ATTRIBUTE_XYZ, pParticles );
	C4VAttributeWriteIterator pPrevXYZ( PARTICLE_ATTRIBUTE_PREV_XYZ, pParticles );

	fltx4 fl4_Dt = ReplicateX4( pParticles->m_flDt );

	int nCtr = pParticles->m_nPaddedActiveParticles;

	bool bUseRange;
	fltx4 fl4OORange;
	if ( m_flRange != 0.0f )
	{
		fl4OORange = ReplicateX4( 1.0f / m_flRange );
		bUseRange = true;
	}
	else
	{
		bUseRange = false;
	}

	fltx4 fl4BiasParm = PreCalcBiasParameter( ReplicateX4( 0.2f ) );

	if ( m_flStartTime_min >= 1.0f )	// always locked on
	{
		CM128AttributeIterator pCreationTime( PARTICLE_ATTRIBUTE_CREATION_TIME, pParticles );
		fltx4 fl4StrengthX4 = ReplicateX4( flStrength );
		do
		{
			fltx4 fl4ParticleAge = SubSIMD( pParticles->m_fl4CurTime, *pCreationTime );
			fltx4 fl4CreationFrameBias = MulSIMD( MinSIMD( fl4ParticleAge, fl4_Dt ), DivSIMD( Four_Ones, fl4_Dt ) );
			FourVectors v4ScaledDelta = v4Delta;
			v4ScaledDelta *= fl4CreationFrameBias;
			fltx4 fl4LockStrength = fl4StrengthX4;

			if ( bUseRange )
			{
				FourVectors fvDistance = *pXYZ;
				fvDistance += v4ScaledDelta;
				fvDistance -= v4ControlPoint;
				fltx4 fl4Distance = fvDistance.length();
				fl4Distance = MulSIMD( fl4Distance, fl4OORange );
				fl4Distance = MinSIMD( Four_Ones, fl4Distance );
				fl4Distance = BiasSIMD( fl4Distance, fl4BiasParm );
				fltx4 fl4Inverse = SubSIMD( Four_Ones, fl4Distance );
				fl4LockStrength = SubSIMD( Four_Ones, MulSIMD( fl4Distance, fl4LockStrength ) );
				v4ScaledDelta *= fl4Inverse;
			}

			if ( m_bLockRot )
			{
				fl4LockStrength = MulSIMD( fl4LockStrength, fl4CreationFrameBias );
				FourVectors fvCurPos = *pXYZ;
				FourVectors fvPrevPos = *pPrevXYZ;
				fvCurPos.TransformBy( matTransformLock );
				fvPrevPos.TransformBy( matTransformLock );
				fvCurPos -= *pXYZ;
				fvPrevPos -= *pPrevXYZ;
				fvCurPos *= fl4LockStrength;
				fvPrevPos *= fl4LockStrength;
				*pXYZ += fvCurPos;
				*pPrevXYZ += fvPrevPos;
			}
			else
			{
				*pXYZ += v4ScaledDelta;
				*pPrevXYZ += v4ScaledDelta;
			}

			++pCreationTime;
			++pXYZ;
			++pPrevXYZ;
		} while ( --nCtr );
	}
	else
	{
		CM128AttributeIterator pCreationTime( PARTICLE_ATTRIBUTE_CREATION_TIME, pParticles );
		CM128AttributeIterator pLifeDuration( PARTICLE_ATTRIBUTE_LIFE_DURATION, pParticles );
		fltx4 fl4CurTime = pParticles->m_fl4CurTime;
		fltx4 fl4StartRange = ReplicateX4( m_flStartTime_max - m_flStartTime_min );
		fltx4 fl4StartBias = ReplicateX4( m_flStartTime_min );
		fltx4 fl4EndRange = ReplicateX4( m_flEndTime_max - m_flEndTime_min );
		fltx4 fl4EndBias = ReplicateX4( m_flEndTime_min );
		int nSSEStartExponent = m_flStartTime_exp * 4.0f;
		int nSSEEndExponent = m_flEndTime_exp * 4.0f;
		do
		{
			fltx4 fl4ParticleAge = SubSIMD( fl4CurTime, *pCreationTime );
			fltx4 fl4CreationFrameBias = MulSIMD( MinSIMD( fl4ParticleAge, fl4_Dt ), DivSIMD( Four_Ones, fl4_Dt ) );
			FourVectors v4ScaledDelta = v4Delta;
			v4ScaledDelta *= fl4CreationFrameBias;

			fltx4 fl4LifeTime = MulSIMD( ReciprocalEstSIMD( *pLifeDuration ), fl4ParticleAge );
			fl4LifeTime = MaxSIMD( Four_Zeros, MinSIMD( Four_Ones, fl4LifeTime ) );

			fltx4 fl4StartTime = Pow_FixedPoint_Exponent_SIMD( RandSIMD( nContext ), nSSEStartExponent );
			fl4StartTime = AddSIMD( MulSIMD( fl4StartTime, fl4StartRange ), fl4StartBias );

			fltx4 fl4EndTime = Pow_FixedPoint_Exponent_SIMD( RandSIMD( nContext ), nSSEEndExponent );
			fl4EndTime = AddSIMD( MulSIMD( fl4EndTime, fl4EndRange ), fl4EndBias );

			fltx4 fl4Fade = DivSIMD( SubSIMD( fl4LifeTime, fl4StartTime ), SubSIMD( fl4EndTime, fl4StartTime ) );
			fltx4 fl4LockStrength = SubSIMD( Four_Ones, MaxSIMD( Four_Zeros, MinSIMD( Four_Ones, fl4Fade ) ) );

			if ( IsAnyNegative( CmpGtSIMD( fl4LockStrength, Four_Zeros ) ) )
			{
				v4ScaledDelta *= fl4LockStrength;

				if ( bUseRange )
				{
					FourVectors fvDistance = *pXYZ;
					fvDistance += v4ScaledDelta;
					fvDistance -= v4ControlPoint;
					fltx4 fl4Distance = fvDistance.length();
					fl4Distance = MulSIMD( fl4Distance, fl4OORange );
					fl4Distance = MinSIMD( Four_Ones, fl4Distance );
					fl4Distance = BiasSIMD( fl4Distance, fl4BiasParm );
					fltx4 fl4Inverse = SubSIMD( Four_Ones, fl4Distance );
					fl4LockStrength = SubSIMD( Four_Ones, MulSIMD( fl4Distance, fl4LockStrength ) );
					v4ScaledDelta *= fl4Inverse;
				}

				if ( m_bLockRot )
				{
					fl4LockStrength = MulSIMD( fl4LockStrength, fl4CreationFrameBias );
					FourVectors fvCurPos = *pXYZ;
					FourVectors fvPrevPos = *pPrevXYZ;
					fvCurPos.TransformBy( matTransformLock );
					fvPrevPos.TransformBy( matTransformLock );
					fvCurPos -= *pXYZ;
					fvPrevPos -= *pPrevXYZ;
					fvCurPos *= fl4LockStrength;
					fvPrevPos *= fl4LockStrength;
					*pXYZ += fvCurPos;
					*pPrevXYZ += fvPrevPos;
				}
				else
				{
					*pXYZ += v4ScaledDelta;
					*pPrevXYZ += v4ScaledDelta;
				}
			}

			++pCreationTime;
			++pLifeDuration;
			++pXYZ;
			++pPrevXYZ;
		} while ( --nCtr );
	}

	pCtx->m_vPrevPosition = vecControlPoint;
	// NOTE: shipped code copied matCurrentTransform unconditionally, but it is only filled in (and the
	// previous transform only ever read) when m_bLockRot is set; copying it otherwise reads stack garbage
	if ( m_bLockRot )
	{
		pCtx->m_matPrevTransform = matCurrentTransform;
	}
	ReleaseSIMDRandContext( nContext );
}


//-----------------------------------------------------------------------------
// Controlpoint Light
//-----------------------------------------------------------------------------
class C_OP_ControlpointLight : public CParticleOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_OP_ControlpointLight );

	uint32 GetReadInitialAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_TINT_RGB_MASK;
	}

	uint32 GetWrittenAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_TINT_RGB_MASK;
	}

	uint32 GetReadAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_XYZ_MASK;
	}

	virtual uint64 GetReadControlPointMask() const
	{
		return ( 1ULL << m_nControlPoint1 ) | ( 1ULL << m_nControlPoint2 ) |
			( 1ULL << m_nControlPoint3 ) | ( 1ULL << m_nControlPoint4 );
	}

	virtual void InitParams( CParticleSystemDefinition *pDef );
	virtual void Operate( CParticleCollection *pParticles, float flStrength, void *pContext ) const;
	virtual void Render( CParticleCollection *pParticles ) const;

	float m_flScale;
	LightDesc_t m_LightNode1;
	LightDesc_t m_LightNode2;
	LightDesc_t m_LightNode3;
	LightDesc_t m_LightNode4;
	int m_nControlPoint1;
	int m_nControlPoint2;
	int m_nControlPoint3;
	int m_nControlPoint4;
	Vector m_vecCPOffset1;
	Vector m_vecCPOffset2;
	Vector m_vecCPOffset3;
	Vector m_vecCPOffset4;
	float m_LightFiftyDist1;
	float m_LightZeroDist1;
	float m_LightFiftyDist2;
	float m_LightZeroDist2;
	float m_LightFiftyDist3;
	float m_LightZeroDist3;
	float m_LightFiftyDist4;
	float m_LightZeroDist4;
	Color m_LightColor1;
	Color m_LightColor2;
	Color m_LightColor3;
	Color m_LightColor4;
	bool m_bLightType1;
	bool m_bLightType2;
	bool m_bLightType3;
	bool m_bLightType4;
	bool m_bLightDynamic1;
	bool m_bLightDynamic2;
	bool m_bLightDynamic3;
	bool m_bLightDynamic4;
	bool m_bUseNormal;
	bool m_bUseHLambert;
	bool m_bLightActive1;
	bool m_bLightActive2;
	bool m_bLightActive3;
	bool m_bLightActive4;
	bool m_bClampLowerRange;
	bool m_bClampUpperRange;
};

DEFINE_PARTICLE_OPERATOR( C_OP_ControlpointLight, "Color Light from Control Point", OPERATOR_GENERIC );

BEGIN_PARTICLE_OPERATOR_UNPACK( C_OP_ControlpointLight )
	DMXELEMENT_UNPACK_FIELD( "Light 1 Control Point", "0", int, m_nControlPoint1 )
	DMXELEMENT_UNPACK_FIELD( "Light 1 Control Point Offset", "0 0 0", Vector, m_vecCPOffset1 )
	DMXELEMENT_UNPACK_FIELD( "Light 1 Type 0=Point 1=Spot", "0", bool, m_bLightType1 )
	DMXELEMENT_UNPACK_FIELD( "Light 1 Color", "0 0 0 255", Color, m_LightColor1 )
	DMXELEMENT_UNPACK_FIELD( "Light 1 Dynamic Light", "0", bool, m_bLightDynamic1 )
	DMXELEMENT_UNPACK_FIELD( "Light 1 Direction", "0 0 0", Vector, m_LightNode1.m_Direction )
	DMXELEMENT_UNPACK_FIELD( "Light 1 50% Distance", "100", float, m_LightFiftyDist1 )
	DMXELEMENT_UNPACK_FIELD( "Light 1 0% Distance", "200", float, m_LightZeroDist1 )
	DMXELEMENT_UNPACK_FIELD( "Light 1 Spot Inner Cone", "30.0", float, m_LightNode1.m_Theta )
	DMXELEMENT_UNPACK_FIELD( "Light 1 Spot Outer Cone", "45.0", float, m_LightNode1.m_Phi )
	DMXELEMENT_UNPACK_FIELD( "Light 2 Control Point", "0", int, m_nControlPoint2 )
	DMXELEMENT_UNPACK_FIELD( "Light 2 Control Point Offset", "0 0 0", Vector, m_vecCPOffset2 )
	DMXELEMENT_UNPACK_FIELD( "Light 2 Type 0=Point 1=Spot", "0", bool, m_bLightType2 )
	DMXELEMENT_UNPACK_FIELD( "Light 2 Color", "0 0 0 255", Color, m_LightColor2 )
	DMXELEMENT_UNPACK_FIELD( "Light 2 Dynamic Light", "0", bool, m_bLightDynamic2 )
	DMXELEMENT_UNPACK_FIELD( "Light 2 Direction", "0 0 0", Vector, m_LightNode2.m_Direction )
	DMXELEMENT_UNPACK_FIELD( "Light 2 50% Distance", "100", float, m_LightFiftyDist2 )
	DMXELEMENT_UNPACK_FIELD( "Light 2 0% Distance", "200", float, m_LightZeroDist2 )
	DMXELEMENT_UNPACK_FIELD( "Light 2 Spot Inner Cone", "30.0", float, m_LightNode2.m_Theta )
	DMXELEMENT_UNPACK_FIELD( "Light 2 Spot Outer Cone", "45.0", float, m_LightNode2.m_Phi )
	DMXELEMENT_UNPACK_FIELD( "Light 3 Control Point", "0", int, m_nControlPoint3 )
	DMXELEMENT_UNPACK_FIELD( "Light 3 Control Point Offset", "0 0 0", Vector, m_vecCPOffset3 )
	DMXELEMENT_UNPACK_FIELD( "Light 3 Type 0=Point 1=Spot", "0", bool, m_bLightType3 )
	DMXELEMENT_UNPACK_FIELD( "Light 3 Color", "0 0 0 255", Color, m_LightColor3 )
	DMXELEMENT_UNPACK_FIELD( "Light 3 Dynamic Light", "0", bool, m_bLightDynamic3 )
	DMXELEMENT_UNPACK_FIELD( "Light 3 Direction", "0 0 0", Vector, m_LightNode3.m_Direction )
	DMXELEMENT_UNPACK_FIELD( "Light 3 50% Distance", "100", float, m_LightFiftyDist3 )
	DMXELEMENT_UNPACK_FIELD( "Light 3 0% Distance", "200", float, m_LightZeroDist3 )
	DMXELEMENT_UNPACK_FIELD( "Light 3 Spot Inner Cone", "30.0", float, m_LightNode3.m_Theta )
	DMXELEMENT_UNPACK_FIELD( "Light 3 Spot Outer Cone", "45.0", float, m_LightNode3.m_Phi )
	DMXELEMENT_UNPACK_FIELD( "Light 4 Control Point", "0", int, m_nControlPoint4 )
	DMXELEMENT_UNPACK_FIELD( "Light 4 Control Point Offset", "0 0 0", Vector, m_vecCPOffset4 )
	DMXELEMENT_UNPACK_FIELD( "Light 4 Type 0=Point 1=Spot", "0", bool, m_bLightType4 )
	DMXELEMENT_UNPACK_FIELD( "Light 4 Color", "0 0 0 255", Color, m_LightColor4 )
	DMXELEMENT_UNPACK_FIELD( "Light 4 Dynamic Light", "0", bool, m_bLightDynamic4 )
	DMXELEMENT_UNPACK_FIELD( "Light 4 Direction", "0 0 0", Vector, m_LightNode4.m_Direction )
	DMXELEMENT_UNPACK_FIELD( "Light 4 50% Distance", "100", float, m_LightFiftyDist4 )
	DMXELEMENT_UNPACK_FIELD( "Light 4 0% Distance", "200", float, m_LightZeroDist4 )
	DMXELEMENT_UNPACK_FIELD( "Light 4 Spot Inner Cone", "30.0", float, m_LightNode4.m_Theta )
	DMXELEMENT_UNPACK_FIELD( "Light 4 Spot Outer Cone", "45.0", float, m_LightNode4.m_Phi )
	DMXELEMENT_UNPACK_FIELD( "Initial Color Bias", "0.0", float, m_flScale )
	DMXELEMENT_UNPACK_FIELD( "Clamp Minimum Light Value to Initial Color", "0", bool, m_bClampLowerRange )
	DMXELEMENT_UNPACK_FIELD( "Clamp Maximum Light Value to Initial Color", "0", bool, m_bClampUpperRange )
	DMXELEMENT_UNPACK_FIELD( "Compute Normals From Control Points", "0", bool, m_bUseNormal )
	DMXELEMENT_UNPACK_FIELD( "Half-Lambert Normals", "1", bool, m_bUseHLambert )
END_PARTICLE_OPERATOR_UNPACK( C_OP_ControlpointLight )

void C_OP_ControlpointLight::InitParams( CParticleSystemDefinition *pDef )
{
	m_LightNode1.m_Color[0] = m_LightColor1[0] * ( 1.0f / 255.0f );
	m_LightNode1.m_Color[1] = m_LightColor1[1] * ( 1.0f / 255.0f );
	m_LightNode1.m_Color[2] = m_LightColor1[2] * ( 1.0f / 255.0f );
	m_LightNode2.m_Color[0] = m_LightColor2[0] * ( 1.0f / 255.0f );
	m_LightNode2.m_Color[1] = m_LightColor2[1] * ( 1.0f / 255.0f );
	m_LightNode2.m_Color[2] = m_LightColor2[2] * ( 1.0f / 255.0f );
	m_LightNode3.m_Color[0] = m_LightColor3[0] * ( 1.0f / 255.0f );
	m_LightNode3.m_Color[1] = m_LightColor3[1] * ( 1.0f / 255.0f );
	m_LightNode3.m_Color[2] = m_LightColor3[2] * ( 1.0f / 255.0f );
	m_LightNode4.m_Color[0] = m_LightColor4[0] * ( 1.0f / 255.0f );
	m_LightNode4.m_Color[1] = m_LightColor4[1] * ( 1.0f / 255.0f );
	m_LightNode4.m_Color[2] = m_LightColor4[2] * ( 1.0f / 255.0f );
	m_LightNode1.m_Range = 0;
	m_LightNode2.m_Range = 0;
	m_LightNode3.m_Range = 0;
	m_LightNode4.m_Range = 0;
	m_LightNode1.m_Falloff = 5.0;
	m_LightNode2.m_Falloff = 5.0;
	m_LightNode3.m_Falloff = 5.0;
	m_LightNode4.m_Falloff = 5.0;
	m_LightNode1.m_Attenuation0 = 0;
	m_LightNode1.m_Attenuation1 = 0;
	m_LightNode1.m_Attenuation2 = 1;
	m_LightNode2.m_Attenuation0 = 0;
	m_LightNode2.m_Attenuation1 = 0;
	m_LightNode2.m_Attenuation2 = 1;
	m_LightNode3.m_Attenuation0 = 0;
	m_LightNode3.m_Attenuation1 = 0;
	m_LightNode3.m_Attenuation2 = 1;
	m_LightNode4.m_Attenuation0 = 0;
	m_LightNode4.m_Attenuation1 = 0;
	m_LightNode4.m_Attenuation2 = 1;

	if ( !m_bLightType1 )
	{
		m_LightNode1.m_Type = MATERIAL_LIGHT_POINT;
	}
	else
	{
		m_LightNode1.m_Type = MATERIAL_LIGHT_SPOT;
	}

	if ( !m_bLightType2 )
	{
		m_LightNode2.m_Type = MATERIAL_LIGHT_POINT;
	}
	else
	{
		m_LightNode2.m_Type = MATERIAL_LIGHT_SPOT;
	}

	if ( !m_bLightType3 )
	{
		m_LightNode3.m_Type = MATERIAL_LIGHT_POINT;
	}
	else
	{
		m_LightNode3.m_Type = MATERIAL_LIGHT_SPOT;
	}

	if ( !m_bLightType4 )
	{
		m_LightNode4.m_Type = MATERIAL_LIGHT_POINT;
	}
	else
	{
		m_LightNode4.m_Type = MATERIAL_LIGHT_SPOT;
	}

	if ( !m_bLightDynamic1 && ( m_LightColor1 != Color( 0, 0, 0, 255 ) ) )
	{
		m_bLightActive1 = true;
	}
	else
	{
		m_bLightActive1 = false;
	}

	if ( !m_bLightDynamic2 && ( m_LightColor2 != Color( 0, 0, 0, 255 ) ) )
	{
		m_bLightActive2 = true;
	}
	else
	{
		m_bLightActive2 = false;
	}

	if ( !m_bLightDynamic3 && ( m_LightColor3 != Color( 0, 0, 0, 255 ) ) )
	{
		m_bLightActive3 = true;
	}
	else
	{
		m_bLightActive3 = false;
	}

	if ( !m_bLightDynamic4 && ( m_LightColor4 != Color( 0, 0, 0, 255 ) ) )
	{
		m_bLightActive4 = true;
	}
	else
	{
		m_bLightActive4 = false;
	}

	m_LightNode1.SetupNewStyleAttenuation( m_LightFiftyDist1, m_LightZeroDist1 );
	m_LightNode2.SetupNewStyleAttenuation( m_LightFiftyDist2, m_LightZeroDist2 );
	m_LightNode3.SetupNewStyleAttenuation( m_LightFiftyDist3, m_LightZeroDist3 );
	m_LightNode4.SetupNewStyleAttenuation( m_LightFiftyDist4, m_LightZeroDist4 );
}

void C_OP_ControlpointLight::Operate( CParticleCollection *pParticles, float flStrength, void *pContext ) const
{
	// Set up location of each light - this needs to be done every time as the CP's can move
	Vector vecLocation1, vecLocation2, vecLocation3, vecLocation4;
	vecLocation1 = pParticles->GetControlPointAtCurrentTime( m_nControlPoint1 );
	vecLocation2 = pParticles->GetControlPointAtCurrentTime( m_nControlPoint2 );
	vecLocation3 = pParticles->GetControlPointAtCurrentTime( m_nControlPoint3 );
	vecLocation4 = pParticles->GetControlPointAtCurrentTime( m_nControlPoint4 );

	LightDesc_t LightNode1 = m_LightNode1;
	LightDesc_t LightNode2 = m_LightNode2;
	LightDesc_t LightNode3 = m_LightNode3;
	// NOTE: shipped bug - light 4 starts from a copy of m_LightNode3 (type, color, attenuation, cones, direction)
	LightDesc_t LightNode4 = m_LightNode3;

	// Apply any offsets
	LightNode1.m_Position = vecLocation1 + m_vecCPOffset1;
	LightNode2.m_Position = vecLocation2 + m_vecCPOffset2;
	LightNode3.m_Position = vecLocation3 + m_vecCPOffset3;
	LightNode4.m_Position = vecLocation4 + m_vecCPOffset4;

	if ( m_bLightDynamic1 )
	{
		Color lc;
		g_pParticleSystemMgr->Query()->GetLightingAtPoint( LightNode1.m_Position, lc );
		LightNode1.m_Color[0] = lc[0] * ( 1.0f / 255.0f );
		LightNode1.m_Color[1] = lc[1] * ( 1.0f / 255.0f );
		LightNode1.m_Color[2] = lc[2] * ( 1.0f / 255.0f );
	}
	if ( m_bLightDynamic2 )
	{
		Color lc;
		g_pParticleSystemMgr->Query()->GetLightingAtPoint( LightNode2.m_Position, lc );
		LightNode2.m_Color[0] = lc[0] * ( 1.0f / 255.0f );
		LightNode2.m_Color[1] = lc[1] * ( 1.0f / 255.0f );
		LightNode2.m_Color[2] = lc[2] * ( 1.0f / 255.0f );
	}
	if ( m_bLightDynamic3 )
	{
		Color lc;
		g_pParticleSystemMgr->Query()->GetLightingAtPoint( LightNode3.m_Position, lc );
		LightNode3.m_Color[0] = lc[0] * ( 1.0f / 255.0f );
		LightNode3.m_Color[1] = lc[1] * ( 1.0f / 255.0f );
		LightNode3.m_Color[2] = lc[2] * ( 1.0f / 255.0f );
	}
	if ( m_bLightDynamic4 )
	{
		Color lc;
		g_pParticleSystemMgr->Query()->GetLightingAtPoint( LightNode4.m_Position, lc );
		LightNode4.m_Color[0] = lc[0] * ( 1.0f / 255.0f );
		LightNode4.m_Color[1] = lc[1] * ( 1.0f / 255.0f );
		LightNode4.m_Color[2] = lc[2] * ( 1.0f / 255.0f );
	}

	LightNode1.RecalculateDerivedValues();
	LightNode2.RecalculateDerivedValues();
	LightNode3.RecalculateDerivedValues();
	LightNode4.RecalculateDerivedValues();

	FourVectors vScale;
	vScale.DuplicateVector( Vector( m_flScale, m_flScale, m_flScale ) );

	if ( m_bUseNormal )
	{
		FourVectors vCPPosition1, vCPPosition2, vCPPosition3, vCPPosition4;
		vCPPosition1.DuplicateVector( vecLocation1 );
		vCPPosition2.DuplicateVector( vecLocation2 );
		vCPPosition3.DuplicateVector( vecLocation3 );
		vCPPosition4.DuplicateVector( vecLocation4 );

		C4VAttributeIterator pXYZ( PARTICLE_ATTRIBUTE_XYZ, pParticles );
		C4VAttributeWriteIterator pColor( PARTICLE_ATTRIBUTE_TINT_RGB, pParticles );

		int nCtr = pParticles->m_nPaddedActiveParticles;
		do
		{
			FourVectors vLighting = vScale;
			vLighting *= *pColor;

			FourVectors vNormal = *pXYZ;
			vNormal -= vCPPosition1;
			vNormal.VectorNormalizeFast();
			LightNode1.ComputeLightAtPoints( *pXYZ, vNormal, vLighting, m_bUseHLambert );
			vNormal = *pXYZ;
			vNormal -= vCPPosition2;
			vNormal.VectorNormalizeFast();
			LightNode2.ComputeLightAtPoints( *pXYZ, vNormal, vLighting, m_bUseHLambert );
			vNormal = *pXYZ;
			vNormal -= vCPPosition3;
			vNormal.VectorNormalizeFast();
			LightNode3.ComputeLightAtPoints( *pXYZ, vNormal, vLighting, m_bUseHLambert );
			vNormal = *pXYZ;
			vNormal -= vCPPosition4;
			vNormal.VectorNormalizeFast();
			LightNode4.ComputeLightAtPoints( *pXYZ, vNormal, vLighting, m_bUseHLambert );

			if ( m_bClampLowerRange )
			{
				vLighting.x = MaxSIMD( vLighting.x, pColor->x );
				vLighting.y = MaxSIMD( vLighting.y, pColor->y );
				vLighting.z = MaxSIMD( vLighting.z, pColor->z );
			}
			else
			{
				vLighting.x = MaxSIMD( vLighting.x, Four_Zeros );
				vLighting.y = MaxSIMD( vLighting.y, Four_Zeros );
				vLighting.z = MaxSIMD( vLighting.z, Four_Zeros );
			}

			if ( m_bClampUpperRange )
			{
				vLighting.x = MinSIMD( vLighting.x, pColor->x );
				vLighting.y = MinSIMD( vLighting.y, pColor->y );
				vLighting.z = MinSIMD( vLighting.z, pColor->z );
			}
			else
			{
				vLighting.x = MinSIMD( vLighting.x, Four_Ones );
				vLighting.y = MinSIMD( vLighting.y, Four_Ones );
				vLighting.z = MinSIMD( vLighting.z, Four_Ones );
			}

			*pColor = vLighting;

			++pColor;
			++pXYZ;
		} while ( --nCtr );
	}
	else
	{
		C4VAttributeIterator pXYZ( PARTICLE_ATTRIBUTE_XYZ, pParticles );
		C4VAttributeWriteIterator pColor( PARTICLE_ATTRIBUTE_TINT_RGB, pParticles );

		int nCtr = pParticles->m_nPaddedActiveParticles;
		do
		{
			FourVectors vLighting = vScale;
			vLighting *= *pColor;

			LightNode1.ComputeNonincidenceLightAtPoints( *pXYZ, vLighting );
			LightNode2.ComputeNonincidenceLightAtPoints( *pXYZ, vLighting );
			LightNode3.ComputeNonincidenceLightAtPoints( *pXYZ, vLighting );
			LightNode4.ComputeNonincidenceLightAtPoints( *pXYZ, vLighting );

			if ( m_bClampLowerRange )
			{
				vLighting.x = MaxSIMD( vLighting.x, pColor->x );
				vLighting.y = MaxSIMD( vLighting.y, pColor->y );
				vLighting.z = MaxSIMD( vLighting.z, pColor->z );
			}
			else
			{
				vLighting.x = MaxSIMD( vLighting.x, Four_Zeros );
				vLighting.y = MaxSIMD( vLighting.y, Four_Zeros );
				vLighting.z = MaxSIMD( vLighting.z, Four_Zeros );
			}

			if ( m_bClampUpperRange )
			{
				vLighting.x = MinSIMD( vLighting.x, pColor->x );
				vLighting.y = MinSIMD( vLighting.y, pColor->y );
				vLighting.z = MinSIMD( vLighting.z, pColor->z );
			}
			else
			{
				vLighting.x = MinSIMD( vLighting.x, Four_Ones );
				vLighting.y = MinSIMD( vLighting.y, Four_Ones );
				vLighting.z = MinSIMD( vLighting.z, Four_Ones );
			}

			*pColor = vLighting;

			++pColor;
			++pXYZ;
		} while ( --nCtr );
	}
}

//-----------------------------------------------------------------------------
// Render light spheres
//-----------------------------------------------------------------------------
void C_OP_ControlpointLight::Render( CParticleCollection *pParticles ) const
{
	Vector vecOrigin1 = pParticles->GetControlPointAtCurrentTime( m_nControlPoint1 );
	vecOrigin1 += m_vecCPOffset1;
	Vector vecOrigin2 = pParticles->GetControlPointAtCurrentTime( m_nControlPoint2 );
	vecOrigin2 += m_vecCPOffset2;
	Vector vecOrigin3 = pParticles->GetControlPointAtCurrentTime( m_nControlPoint3 );
	vecOrigin3 += m_vecCPOffset3;
	Vector vecOrigin4 = pParticles->GetControlPointAtCurrentTime( m_nControlPoint4 );
	vecOrigin4 += m_vecCPOffset4;

	Color LightColor1Outer;
	LightColor1Outer[0] = m_LightColor1[0] * .5f;
	LightColor1Outer[1] = m_LightColor1[1] * .5f;
	LightColor1Outer[2] = m_LightColor1[2] * .5f;
	LightColor1Outer[3] = 255;
	Color LightColor2Outer;
	LightColor2Outer[0] = m_LightColor2[0] * .5f;
	LightColor2Outer[1] = m_LightColor2[1] * .5f;
	LightColor2Outer[2] = m_LightColor2[2] * .5f;
	LightColor2Outer[3] = 255;
	Color LightColor3Outer;
	LightColor3Outer[0] = m_LightColor3[0] * .5f;
	LightColor3Outer[1] = m_LightColor3[1] * .5f;
	LightColor3Outer[2] = m_LightColor3[2] * .5f;
	LightColor3Outer[3] = 255;
	Color LightColor4Outer;
	LightColor4Outer[0] = m_LightColor4[0] * .5f;
	LightColor4Outer[1] = m_LightColor4[1] * .5f;
	LightColor4Outer[2] = m_LightColor4[2] * .5f;
	LightColor4Outer[3] = 255;

	if ( m_bLightActive1 )
	{
		RenderWireframeSphere( vecOrigin1, m_LightFiftyDist1, 16, 8, m_LightColor1, false );
		RenderWireframeSphere( vecOrigin1, m_LightZeroDist1, 16, 8, LightColor1Outer, false );
	}
	if ( m_bLightActive2 )
	{
		RenderWireframeSphere( vecOrigin2, m_LightFiftyDist2, 16, 8, m_LightColor2, false );
		RenderWireframeSphere( vecOrigin2, m_LightZeroDist2, 16, 8, LightColor2Outer, false );
	}
	if ( m_bLightActive3 )
	{
		RenderWireframeSphere( vecOrigin3, m_LightFiftyDist3, 16, 8, m_LightColor3, false );
		RenderWireframeSphere( vecOrigin3, m_LightZeroDist3, 16, 8, LightColor3Outer, false );
	}
	if ( m_bLightActive4 )
	{
		RenderWireframeSphere( vecOrigin4, m_LightFiftyDist4, 16, 8, m_LightColor4, false );
		RenderWireframeSphere( vecOrigin4, m_LightZeroDist4, 16, 8, LightColor4Outer, false );
	}
}


//-----------------------------------------------------------------------------
// Set child control points from particle positions
//-----------------------------------------------------------------------------
class C_OP_SetChildControlPoints : public CParticleOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_OP_SetChildControlPoints );

	uint32 GetWrittenAttributes( void ) const
	{
		return 0;
	}

	uint32 GetReadAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_XYZ_MASK | PARTICLE_ATTRIBUTE_PREV_XYZ_MASK;
	}

	virtual uint32 GetFilter( void ) const
	{
		return FILTER_POSITION_AND_VELOCITY_MASK;
	}

	virtual void Operate( CParticleCollection *pParticles, float flStrength, void *pContext ) const;

	int m_nChildGroupID;
	int m_nFirstControlPoint;
	int m_nNumControlPoints;
	int m_nFirstSourcePoint;
	bool m_bSetOrientation;
};

DEFINE_PARTICLE_OPERATOR( C_OP_SetChildControlPoints, "Set child control points from particle positions", OPERATOR_GENERIC );

BEGIN_PARTICLE_OPERATOR_UNPACK( C_OP_SetChildControlPoints )
	DMXELEMENT_UNPACK_FIELD( "Group ID to affect", "0", int, m_nChildGroupID )
	DMXELEMENT_UNPACK_FIELD( "First control point to set", "0", int, m_nFirstControlPoint )
	DMXELEMENT_UNPACK_FIELD( "# of control points to set", "1", int, m_nNumControlPoints )
	DMXELEMENT_UNPACK_FIELD( "first particle to copy", "0", int, m_nFirstSourcePoint )
	DMXELEMENT_UNPACK_FIELD( "set orientation", "0", bool, m_bSetOrientation )
END_PARTICLE_OPERATOR_UNPACK( C_OP_SetChildControlPoints )

void C_OP_SetChildControlPoints::Operate( CParticleCollection *pParticles, float flStrength, void *pContext ) const
{
	int nFirst = clamp( m_nFirstControlPoint, 0, MAX_PARTICLE_CONTROL_POINTS - 1 );
	int nToSet = MIN( pParticles->m_nActiveParticles - m_nFirstSourcePoint, m_nNumControlPoints );
	nToSet = MIN( nToSet, MAX_PARTICLE_CONTROL_POINTS - nFirst );
	if ( nToSet )
	{
		for ( CParticleCollection *pChild = pParticles->m_Children.m_pHead; pChild; pChild = pChild->m_pNext )
		{
			if ( pChild->GetGroupID() == m_nChildGroupID )
			{
				for ( int p = 0; p < nToSet; p++ )
				{
					const float *pXYZ = pParticles->GetFloatAttributePtr( PARTICLE_ATTRIBUTE_XYZ, p + m_nFirstSourcePoint );
					Vector cPnt( pXYZ[0], pXYZ[4], pXYZ[8] );
					pChild->SetControlPoint( p + nFirst, cPnt );
					if ( m_bSetOrientation )
					{
						const float *pXYZ_Prev = pParticles->GetFloatAttributePtr( PARTICLE_ATTRIBUTE_PREV_XYZ, p + m_nFirstSourcePoint );
						Vector vecFwd( pXYZ[0] - pXYZ_Prev[0], pXYZ[4] - pXYZ_Prev[4], pXYZ[8] - pXYZ_Prev[8] );
						VectorNormalize( vecFwd );
						Vector vecRight, vecUp;
						VectorVectors( vecFwd, vecRight, vecUp );
						pChild->SetControlPointOrientation( p + nFirst, vecFwd, vecRight, vecUp );
					}
				}
			}
		}
	}
}


//-----------------------------------------------------------------------------
// Set Control Point Positions
//-----------------------------------------------------------------------------
class C_OP_SetControlPointPositions : public CParticleOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_OP_SetControlPointPositions );

	uint32 GetWrittenAttributes( void ) const
	{
		return 0;
	}

	uint32 GetReadAttributes( void ) const
	{
		return 0;
	}

	virtual uint32 GetFilter( void ) const
	{
		return FILTER_CONTROL_POINTS_MASK;
	}

	bool ShouldRunBeforeEmitters( void ) const
	{
		return true;
	}

	virtual uint64 GetReadControlPointMask() const
	{
		// NOTE: shipped - the mask is accumulated in an int, so control points >= 32 are lost
		// and bit 31 sign-extends into the high dword
		int nMask = ( 1ULL << m_nCP1 ) | ( 1ULL << m_nCP2 ) | ( 1ULL << m_nCP3 ) | ( 1ULL << m_nCP4 );
		if ( m_bUseWorldLocation )
			return nMask;
		return nMask | ( 1ULL << m_nHeadLocation );
	}

	virtual void Operate( CParticleCollection *pParticles, float flStrength, void *pContext ) const;

	bool m_bUseWorldLocation;
	int m_nCP1;
	int m_nCP1Parent;
	int m_nCP2;
	int m_nCP2Parent;
	int m_nCP3;
	int m_nCP3Parent;
	int m_nCP4;
	int m_nCP4Parent;
	Vector m_vecCP1Pos;
	Vector m_vecCP2Pos;
	Vector m_vecCP3Pos;
	Vector m_vecCP4Pos;
	int m_nHeadLocation;
};

DEFINE_PARTICLE_OPERATOR( C_OP_SetControlPointPositions, "Set Control Point Positions", OPERATOR_GENERIC );

BEGIN_PARTICLE_OPERATOR_UNPACK( C_OP_SetControlPointPositions )
	DMXELEMENT_UNPACK_FIELD( "First Control Point Number", "1", int, m_nCP1 )
	DMXELEMENT_UNPACK_FIELD( "First Control Point Parent", "0", int, m_nCP1Parent )
	DMXELEMENT_UNPACK_FIELD( "First Control Point Location", "128 0 0", Vector, m_vecCP1Pos )
	DMXELEMENT_UNPACK_FIELD( "Second Control Point Number", "2", int, m_nCP2 )
	DMXELEMENT_UNPACK_FIELD( "Second Control Point Parent", "0", int, m_nCP2Parent )
	DMXELEMENT_UNPACK_FIELD( "Second Control Point Location", "0 128 0", Vector, m_vecCP2Pos )
	DMXELEMENT_UNPACK_FIELD( "Third Control Point Number", "3", int, m_nCP3 )
	DMXELEMENT_UNPACK_FIELD( "Third Control Point Parent", "0", int, m_nCP3Parent )
	DMXELEMENT_UNPACK_FIELD( "Third Control Point Location", "-128 0 0", Vector, m_vecCP3Pos )
	DMXELEMENT_UNPACK_FIELD( "Fourth Control Point Number", "4", int, m_nCP4 )
	DMXELEMENT_UNPACK_FIELD( "Fourth Control Point Parent", "0", int, m_nCP4Parent )
	DMXELEMENT_UNPACK_FIELD( "Fourth Control Point Location", "0 -128 0", Vector, m_vecCP4Pos )
	DMXELEMENT_UNPACK_FIELD( "Set positions in world space", "0", bool, m_bUseWorldLocation )
	DMXELEMENT_UNPACK_FIELD( "Control Point to offset positions from", "0", int, m_nHeadLocation )
END_PARTICLE_OPERATOR_UNPACK( C_OP_SetControlPointPositions )

void C_OP_SetControlPointPositions::Operate( CParticleCollection *pParticles, float flStrength, void *pContext ) const
{
	if ( !m_bUseWorldLocation )
	{
		matrix3x4_t mat;
		pParticles->GetControlPointTransformAtTime( m_nHeadLocation, pParticles->m_flCurTime, &mat );
		Vector vecTransformLocal = vec3_origin;
		VectorTransform( m_vecCP1Pos, mat, vecTransformLocal );
		pParticles->SetControlPoint( m_nCP1, vecTransformLocal );
		pParticles->SetControlPointParent( m_nCP1, m_nCP1Parent );
		VectorTransform( m_vecCP2Pos, mat, vecTransformLocal );
		pParticles->SetControlPoint( m_nCP2, vecTransformLocal );
		pParticles->SetControlPointParent( m_nCP2, m_nCP2Parent );
		VectorTransform( m_vecCP3Pos, mat, vecTransformLocal );
		pParticles->SetControlPoint( m_nCP3, vecTransformLocal );
		pParticles->SetControlPointParent( m_nCP3, m_nCP3Parent );
		VectorTransform( m_vecCP4Pos, mat, vecTransformLocal );
		pParticles->SetControlPoint( m_nCP4, vecTransformLocal );
		pParticles->SetControlPointParent( m_nCP4, m_nCP4Parent );
	}
	else
	{
		pParticles->SetControlPoint( m_nCP1, m_vecCP1Pos );
		pParticles->SetControlPointParent( m_nCP1, m_nCP1Parent );
		pParticles->SetControlPoint( m_nCP2, m_vecCP2Pos );
		pParticles->SetControlPointParent( m_nCP2, m_nCP2Parent );
		pParticles->SetControlPoint( m_nCP3, m_vecCP3Pos );
		pParticles->SetControlPointParent( m_nCP3, m_nCP3Parent );
		pParticles->SetControlPoint( m_nCP4, m_vecCP4Pos );
		pParticles->SetControlPointParent( m_nCP4, m_nCP4Parent );
	}
}


//-----------------------------------------------------------------------------
// Dampen Movement Relative to Control Point
// The closer a particle is the the assigned control point, the less
// it can move
//-----------------------------------------------------------------------------
class C_OP_DampenToCP : public CParticleOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_OP_DampenToCP );

	uint32 GetWrittenAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_XYZ_MASK | PARTICLE_ATTRIBUTE_PREV_XYZ_MASK;
	}

	uint32 GetReadAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_CREATION_TIME_MASK | PARTICLE_ATTRIBUTE_LIFE_DURATION_MASK |
			PARTICLE_ATTRIBUTE_PARTICLE_ID_MASK;
	}

	virtual uint64 GetReadControlPointMask() const
	{
		return 1ULL << m_nControlPointNumber;
	}

	virtual void InitParams( CParticleSystemDefinition *pDef )
	{
		m_nControlPointNumber = clamp( m_nControlPointNumber, 0, MAX_PARTICLE_CONTROL_POINTS - 1 );
	}

	virtual void Operate( CParticleCollection *pParticles, float flStrength, void *pContext ) const;

	int m_nControlPointNumber;
	float m_flRange;
	float m_flScale;
};

DEFINE_PARTICLE_OPERATOR( C_OP_DampenToCP, "Movement Dampen Relative to Control Point", OPERATOR_GENERIC );

BEGIN_PARTICLE_OPERATOR_UNPACK( C_OP_DampenToCP )
	DMXELEMENT_UNPACK_FIELD( "control_point_number", "0", int, m_nControlPointNumber )
	DMXELEMENT_UNPACK_FIELD( "falloff range", "100", float, m_flRange )
	DMXELEMENT_UNPACK_FIELD( "dampen scale", "1", float, m_flScale )
END_PARTICLE_OPERATOR_UNPACK( C_OP_DampenToCP )

void C_OP_DampenToCP::Operate( CParticleCollection *pParticles, float flStrength, void *pContext ) const
{
	if ( m_flRange <= 0.0f )
		return;

	Vector vecControlPoint = pParticles->GetControlPointAtCurrentTime( m_nControlPointNumber );

	for ( int i = 0; i < pParticles->m_nActiveParticles; ++i )
	{
		float *pXYZ = pParticles->GetFloatAttributePtrForWrite( PARTICLE_ATTRIBUTE_XYZ, i );
		const float *pXYZ_Prev = pParticles->GetFloatAttributePtr( PARTICLE_ATTRIBUTE_PREV_XYZ, i );

		Vector vecParticlePosition, vecParticlePosition_prev;
		SetVectorFromAttribute( vecParticlePosition, pXYZ );
		SetVectorFromAttribute( vecParticlePosition_prev, pXYZ_Prev );

		Vector vecDelta = vecParticlePosition - vecControlPoint;
		// NOTE: shipped sums z, y, x
		float flDistance = FastSqrt( vecDelta.z * vecDelta.z + vecDelta.y * vecDelta.y + vecDelta.x * vecDelta.x );
		if ( flDistance > m_flRange )
			continue;

		float flDistanceToCP = flDistance / m_flRange;
		float flDampenAmount = pow( flDistanceToCP, m_flScale );

		Vector vecParticleDampened = ( vecParticlePosition - vecParticlePosition_prev ) * flDampenAmount;
		vecParticleDampened += vecParticlePosition_prev;
		vecParticleDampened = VectorLerp( vecParticlePosition, vecParticleDampened, flStrength );
		SetVectorAttribute( pXYZ, vecParticleDampened );
	}
}

//-----------------------------------------------------------------------------
// The shipped object calls an out-of-line RemapValClamped whose final lerp runs on
// the x87 stack (double precision) and is rounded to float once by the caller.
//-----------------------------------------------------------------------------
static float RemapValClamped_x87( float val, float A, float B, float C, float D )
{
	if ( A == B )
		return ( val - B >= 0.0f ) ? D : C;
	float cVal = ( val - A ) / ( B - A );
	cVal = clamp<float>( cVal, 0.0f, 1.0f );
	return (float)( ( (double)D - (double)C ) * (double)cVal + (double)C );
}



//-----------------------------------------------------------------------------
// Remap distance between two control points to a scalar
//-----------------------------------------------------------------------------
class C_OP_DistanceBetweenCPs : public CParticleOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_OP_DistanceBetweenCPs );

	uint32 GetWrittenAttributes( void ) const
	{
		return 1 << m_nFieldOutput;
	}

	uint32 GetReadAttributes( void ) const
	{
		return 0;
	}

	uint32 GetReadInitialAttributes( void ) const
	{
		return 1 << m_nFieldOutput;
	}

	virtual uint32 GetFilter( void ) const
	{
		return FILTER_PARAMETER_REMAPPING_MASK;
	}

	virtual uint64 GetReadControlPointMask() const
	{
		return ( 1ULL << m_nStartCP ) | ( 1ULL << m_nEndCP );
	}

	virtual void InitParams( CParticleSystemDefinition *pDef )
	{
		m_nCollisionGroupNumber = g_pParticleSystemMgr->Query()->GetCollisionGroupFromName( m_CollisionGroupName );
		m_nStartCP = clamp( m_nStartCP, 0, MAX_PARTICLE_CONTROL_POINTS - 1 );
		m_nEndCP = clamp( m_nEndCP, 0, MAX_PARTICLE_CONTROL_POINTS - 1 );
	}

	virtual void Operate( CParticleCollection *pParticles, float flStrength, void *pContext ) const;

	int m_nFieldOutput;
	int m_nStartCP;
	int m_nEndCP;
	int m_nCollisionGroupNumber;
	float m_flInputMin;
	float m_flInputMax;
	float m_flOutputMin;
	float m_flOutputMax;
	float m_flMaxTraceLength;
	float m_flLOSScale;
	char m_CollisionGroupName[128];
	bool m_bLOS;
	bool m_bScaleInitialRange;
	bool m_bScaleCurrent;
};

DEFINE_PARTICLE_OPERATOR( C_OP_DistanceBetweenCPs, "Remap Distance Between Two Control Points to Scalar", OPERATOR_GENERIC );

BEGIN_PARTICLE_OPERATOR_UNPACK( C_OP_DistanceBetweenCPs )
	DMXELEMENT_UNPACK_FIELD( "distance minimum", "0", float, m_flInputMin )
	DMXELEMENT_UNPACK_FIELD( "distance maximum", "128", float, m_flInputMax )
	DMXELEMENT_UNPACK_FIELD_USERDATA( "output field", "3", int, m_nFieldOutput, "intchoice particlefield_scalar" )
	DMXELEMENT_UNPACK_FIELD( "output minimum", "0", float, m_flOutputMin )
	DMXELEMENT_UNPACK_FIELD( "output maximum", "1", float, m_flOutputMax )
	DMXELEMENT_UNPACK_FIELD( "starting control point", "0", int, m_nStartCP )
	DMXELEMENT_UNPACK_FIELD( "ending control point", "1", int, m_nEndCP )
	DMXELEMENT_UNPACK_FIELD( "ensure line of sight", "0", bool, m_bLOS )
	DMXELEMENT_UNPACK_FIELD_STRING( "LOS collision group", "NONE", m_CollisionGroupName )
	DMXELEMENT_UNPACK_FIELD( "Maximum Trace Length", "-1", float, m_flMaxTraceLength )
	DMXELEMENT_UNPACK_FIELD( "LOS Failure Scalar", "0", float, m_flLOSScale )
	DMXELEMENT_UNPACK_FIELD( "output is scalar of initial random range", "0", bool, m_bScaleInitialRange )
	DMXELEMENT_UNPACK_FIELD( "output is scalar of current value", "0", bool, m_bScaleCurrent )
END_PARTICLE_OPERATOR_UNPACK( C_OP_DistanceBetweenCPs )

void C_OP_DistanceBetweenCPs::Operate( CParticleCollection *pParticles, float flStrength, void *pContext ) const
{
	// clamp the result to 0 and 1 if it's alpha
	float flMin = m_flOutputMin;
	float flMax = m_flOutputMax;
	if ( ATTRIBUTES_WHICH_ARE_0_TO_1 & ( 1 << m_nFieldOutput ) )
	{
		flMin = clamp( m_flOutputMin, 0.0f, 1.0f );
		flMax = clamp( m_flOutputMax, 0.0f, 1.0f );
	}

	Vector vecControlPoint1 = pParticles->GetControlPointAtCurrentTime( m_nStartCP );
	Vector vecControlPoint2 = pParticles->GetControlPointAtCurrentTime( m_nEndCP );
	Vector vecDelta = vecControlPoint1 - vecControlPoint2;
	float flDistance = sqrt( ( vecDelta.z * vecDelta.z + vecDelta.y * vecDelta.y ) + vecDelta.x * vecDelta.x );

	if ( m_bLOS )
	{
		Vector vecEndPoint = vecControlPoint2;
		if ( m_flMaxTraceLength != -1.0f && m_flMaxTraceLength < flDistance )
		{
			// NOTE: shipped code normalizes the end point position here, not the direction
			VectorNormalize( vecEndPoint );
			vecEndPoint *= m_flMaxTraceLength;
			vecEndPoint += vecControlPoint1;
		}
		CBaseTrace tr;
		g_pParticleSystemMgr->Query()->TraceLine( vecControlPoint1, vecEndPoint, MASK_OPAQUE_AND_NPCS, NULL, m_nCollisionGroupNumber, &tr );
		if ( tr.fraction != 1.0f )
		{
			flDistance = m_flLOSScale * flDistance * tr.fraction;
		}
	}

	for( int i = 0; i < pParticles->m_nActiveParticles; ++i )
	{
		float flOutput = RemapValClamped( flDistance, m_flInputMin, m_flInputMax, flMin, flMax );
		float *pOutput = pParticles->GetFloatAttributePtrForWrite( m_nFieldOutput, i );
		if ( m_bScaleInitialRange )
		{
			const float *pInitialOutput = pParticles->GetInitialFloatAttributePtr( m_nFieldOutput, i );
			flOutput = *pInitialOutput * flOutput;
		}
		if ( m_bScaleCurrent )
		{
			flOutput = *pOutput * flOutput;
		}
		*pOutput = Lerp( flStrength, *pOutput, flOutput );
	}
}


//-----------------------------------------------------------------------------
// Remap distance between two control points to a control point
//-----------------------------------------------------------------------------
class C_OP_DistanceBetweenCPsToCP : public CParticleOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_OP_DistanceBetweenCPsToCP );

	uint32 GetWrittenAttributes( void ) const
	{
		return 0;
	}

	uint32 GetReadAttributes( void ) const
	{
		return 0;
	}

	virtual uint32 GetFilter( void ) const
	{
		return FILTER_PARAMETER_REMAPPING_MASK;
	}

	virtual uint64 GetReadControlPointMask() const
	{
		return ( 1ULL << m_nStartCP ) | ( 1ULL << m_nEndCP ) | ( 1ULL << m_nOutputCP );
	}

	virtual void InitParams( CParticleSystemDefinition *pDef )
	{
		m_nCollisionGroupNumber = g_pParticleSystemMgr->Query()->GetCollisionGroupFromName( m_CollisionGroupName );
		m_nStartCP = clamp( m_nStartCP, 0, MAX_PARTICLE_CONTROL_POINTS - 1 );
		m_nEndCP = clamp( m_nEndCP, 0, MAX_PARTICLE_CONTROL_POINTS - 1 );
		m_nOutputCP = clamp( m_nOutputCP, 0, MAX_PARTICLE_CONTROL_POINTS - 1 );
		m_nOutputCPField = clamp( m_nOutputCPField, 0, 2 );
	}

	virtual bool ShouldRunBeforeEmitters( void ) const
	{
		return true;
	}

	virtual void Operate( CParticleCollection *pParticles, float flStrength, void *pContext ) const;

	int m_nStartCP;
	int m_nEndCP;
	int m_nOutputCP;
	int m_nOutputCPField;
	int m_nCollisionGroupNumber;
	float m_flInputMin;
	float m_flInputMax;
	float m_flOutputMin;
	float m_flOutputMax;
	float m_flMaxTraceLength;
	float m_flLOSScale;
	bool m_bLOS;
	char m_CollisionGroupName[128];
};

DEFINE_PARTICLE_OPERATOR( C_OP_DistanceBetweenCPsToCP, "Remap Distance Between Two Control Points to CP", OPERATOR_GENERIC );

BEGIN_PARTICLE_OPERATOR_UNPACK( C_OP_DistanceBetweenCPsToCP )
	DMXELEMENT_UNPACK_FIELD( "distance minimum", "0", float, m_flInputMin )
	DMXELEMENT_UNPACK_FIELD( "distance maximum", "128", float, m_flInputMax )
	DMXELEMENT_UNPACK_FIELD( "output control point", "2", int, m_nOutputCP )
	DMXELEMENT_UNPACK_FIELD( "output control point field", "0", int, m_nOutputCPField )
	DMXELEMENT_UNPACK_FIELD( "output minimum", "0", float, m_flOutputMin )
	DMXELEMENT_UNPACK_FIELD( "output maximum", "1", float, m_flOutputMax )
	DMXELEMENT_UNPACK_FIELD( "starting control point", "0", int, m_nStartCP )
	DMXELEMENT_UNPACK_FIELD( "ending control point", "1", int, m_nEndCP )
	DMXELEMENT_UNPACK_FIELD( "ensure line of sight", "0", bool, m_bLOS )
	DMXELEMENT_UNPACK_FIELD_STRING( "LOS collision group", "NONE", m_CollisionGroupName )
	DMXELEMENT_UNPACK_FIELD( "Maximum Trace Length", "-1", float, m_flMaxTraceLength )
	DMXELEMENT_UNPACK_FIELD( "LOS Failure Scale", "0", float, m_flLOSScale )
END_PARTICLE_OPERATOR_UNPACK( C_OP_DistanceBetweenCPsToCP )

void C_OP_DistanceBetweenCPsToCP::Operate( CParticleCollection *pParticles, float flStrength, void *pContext ) const
{
	Vector vecControlPoint1 = pParticles->GetControlPointAtCurrentTime( m_nStartCP );
	Vector vecControlPoint2 = pParticles->GetControlPointAtCurrentTime( m_nEndCP );
	Vector vecDelta = vecControlPoint1 - vecControlPoint2;
	float flDistance = sqrt( ( vecDelta.z * vecDelta.z + vecDelta.y * vecDelta.y ) + vecDelta.x * vecDelta.x );
	float flOrigDistance = flDistance;

	if ( m_bLOS )
	{
		Vector vecEndPoint = vecControlPoint2;
		if ( m_flMaxTraceLength != -1.0f && m_flMaxTraceLength < flDistance )
		{
			// NOTE: shipped code normalizes the end point position here, not the direction
			VectorNormalize( vecEndPoint );
			vecEndPoint *= m_flMaxTraceLength;
			vecEndPoint += vecControlPoint1;
		}
		CBaseTrace tr;
		g_pParticleSystemMgr->Query()->TraceLine( vecControlPoint1, vecEndPoint, MASK_OPAQUE_AND_NPCS, NULL, m_nCollisionGroupNumber, &tr );
		if ( tr.fraction != 1.0f )
		{
			flDistance = m_flLOSScale * flOrigDistance * tr.fraction;
		}
	}

	flDistance = RemapValClamped_x87( flDistance, m_flInputMin, m_flInputMax, m_flOutputMin, m_flOutputMax );
	Vector vecControlPointOutput = pParticles->GetControlPointAtCurrentTime( m_nOutputCP );
	vecControlPointOutput[m_nOutputCPField] = flDistance;
	pParticles->SetControlPoint( m_nOutputCP, vecControlPointOutput );
}


//-----------------------------------------------------------------------------
// Remap percentage between two control points to a scalar
//-----------------------------------------------------------------------------
class C_OP_PercentageBetweenCPs : public CParticleOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_OP_PercentageBetweenCPs );

	uint32 GetWrittenAttributes( void ) const
	{
		return 1 << m_nFieldOutput;
	}

	uint32 GetReadAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_XYZ_MASK;
	}

	uint32 GetReadInitialAttributes( void ) const
	{
		return 1 << m_nFieldOutput;
	}

	virtual uint32 GetFilter( void ) const
	{
		return FILTER_PARAMETER_REMAPPING_MASK;
	}

	virtual uint64 GetReadControlPointMask() const
	{
		return ( 1ULL << m_nStartCP ) | ( 1ULL << m_nEndCP );
	}

	virtual void InitParams( CParticleSystemDefinition *pDef )
	{
		m_nStartCP = clamp( m_nStartCP, 0, MAX_PARTICLE_CONTROL_POINTS - 1 );
		m_nEndCP = clamp( m_nEndCP, 0, MAX_PARTICLE_CONTROL_POINTS - 1 );
		// clamp the result to 0 and 1 if it's alpha
		if ( ATTRIBUTES_WHICH_ARE_0_TO_1 & ( 1 << m_nFieldOutput ) )
		{
			m_flOutputMin = clamp( m_flOutputMin, 0.0f, 1.0f );
			m_flOutputMax = clamp( m_flOutputMax, 0.0f, 1.0f );
		}
	}

	virtual void Operate( CParticleCollection *pParticles, float flStrength, void *pContext ) const;

	int m_nFieldOutput;
	float m_flInputMin;
	float m_flInputMax;
	float m_flOutputMin;
	float m_flOutputMax;
	int m_nStartCP;
	int m_nEndCP;
	bool m_bScaleInitialRange;
	bool m_bScaleCurrent;
	bool m_bActiveRange;
	bool m_bRadialCheck;
};

DEFINE_PARTICLE_OPERATOR( C_OP_PercentageBetweenCPs, "Remap Percentage Between Two Control Points to Scalar", OPERATOR_GENERIC );

BEGIN_PARTICLE_OPERATOR_UNPACK( C_OP_PercentageBetweenCPs )
	DMXELEMENT_UNPACK_FIELD( "percentage minimum", "0", float, m_flInputMin )
	DMXELEMENT_UNPACK_FIELD( "percentage maximum", "1", float, m_flInputMax )
	DMXELEMENT_UNPACK_FIELD_USERDATA( "output field", "3", int, m_nFieldOutput, "intchoice particlefield_scalar" )
	DMXELEMENT_UNPACK_FIELD( "output minimum", "0", float, m_flOutputMin )
	DMXELEMENT_UNPACK_FIELD( "output maximum", "1", float, m_flOutputMax )
	DMXELEMENT_UNPACK_FIELD( "starting control point", "0", int, m_nStartCP )
	DMXELEMENT_UNPACK_FIELD( "ending control point", "1", int, m_nEndCP )
	DMXELEMENT_UNPACK_FIELD( "output is scalar of initial random range", "0", bool, m_bScaleInitialRange )
	DMXELEMENT_UNPACK_FIELD( "output is scalar of current value", "0", bool, m_bScaleCurrent )
	DMXELEMENT_UNPACK_FIELD( "only active within input range", "0", bool, m_bActiveRange )
	DMXELEMENT_UNPACK_FIELD( "treat distance between points as radius", "1", bool, m_bRadialCheck )
END_PARTICLE_OPERATOR_UNPACK( C_OP_PercentageBetweenCPs )

void C_OP_PercentageBetweenCPs::Operate( CParticleCollection *pParticles, float flStrength, void *pContext ) const
{
	Vector vecControlPoint1 = pParticles->GetControlPointAtCurrentTime( m_nStartCP );

	C4VAttributeIterator xyz( PARTICLE_ATTRIBUTE_XYZ, pParticles );
	CM128AttributeWriteIterator pOutField( m_nFieldOutput, pParticles );
	CM128InitialAttributeIterator pInitialValue( m_nFieldOutput, pParticles );

	FourVectors fvControlPoint1;
	fvControlPoint1.DuplicateVector( pParticles->GetControlPointAtCurrentTime( m_nStartCP ) );
	FourVectors fvControlpoint2;
	fvControlpoint2.DuplicateVector( pParticles->GetControlPointAtCurrentTime( m_nEndCP ) );
	FourVectors fvDelta = fvControlPoint1;
	fvDelta -= fvControlpoint2;
	fltx4 fl4Distance = SqrtSIMD( AddSIMD( AddSIMD( MulSIMD( fvDelta.x, fvDelta.x ), MulSIMD( fvDelta.y, fvDelta.y ) ), MulSIMD( fvDelta.z, fvDelta.z ) ) );

	fltx4 fl4InputMin = ReplicateX4( m_flInputMin );
	fltx4 fl4InputMax = ReplicateX4( m_flInputMax );
	fltx4 fl4OutputMin = ReplicateX4( m_flOutputMin );
	fltx4 fl4OutputMax = ReplicateX4( m_flOutputMax );

	int nCtr = pParticles->m_nPaddedActiveParticles;
	do
	{
		fltx4 fl4Perc;
		if ( m_bRadialCheck )
		{
			FourVectors fvParticleDelta;
			fvParticleDelta.DuplicateVector( vecControlPoint1 );
			fvParticleDelta -= *xyz;
			fltx4 fl4ParticleDist = SqrtSIMD( AddSIMD( AddSIMD( MulSIMD( fvParticleDelta.x, fvParticleDelta.x ), MulSIMD( fvParticleDelta.y, fvParticleDelta.y ) ), MulSIMD( fvParticleDelta.z, fvParticleDelta.z ) ) );
			fl4Perc = DivSIMD( Four_Ones, DivSIMD( fl4Distance, AddSIMD( fl4ParticleDist, Four_Epsilons ) ) );
		}
		else
		{
			FourVectors fvClosestPoint;
			FourVectors::CalcClosestPointOnLineSIMD( *xyz, fvControlPoint1, fvControlpoint2, fvClosestPoint, &fl4Perc );
		}

		fltx4 fl4Output = RemapValClampedSIMD( fl4Perc, fl4InputMin, fl4InputMax, fl4OutputMin, fl4OutputMax );
		if ( m_bScaleInitialRange )
		{
			fl4Output = MulSIMD( *pInitialValue, fl4Output );
		}
		if ( m_bScaleCurrent )
		{
			fl4Output = MulSIMD( *pOutField, fl4Output );
		}
		if ( m_bActiveRange )
		{
			fltx4 fl4Mask = AndSIMD( CmpLeSIMD( fl4Perc, fl4InputMax ), CmpLeSIMD( fl4InputMin, fl4Perc ) );
			*pOutField = MaskedAssign( fl4Mask, fl4Output, *pOutField );
		}
		else
		{
			*pOutField = fl4Output;
		}

		++pOutField;
		++xyz;
		++pInitialValue;
	} while ( --nCtr );
}


//-----------------------------------------------------------------------------
// Remap percentage between two control points to a vector
//-----------------------------------------------------------------------------
class C_OP_PercentageBetweenCPsVector : public CParticleOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_OP_PercentageBetweenCPsVector );

	uint32 GetWrittenAttributes( void ) const
	{
		return 1 << m_nFieldOutput;
	}

	uint32 GetReadAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_XYZ_MASK;
	}

	uint32 GetReadInitialAttributes( void ) const
	{
		return 1 << m_nFieldOutput;
	}

	virtual uint32 GetFilter( void ) const
	{
		return FILTER_PARAMETER_REMAPPING_MASK;
	}

	virtual uint64 GetReadControlPointMask() const
	{
		return ( 1ULL << m_nStartCP ) | ( 1ULL << m_nEndCP );
	}

	virtual void InitParams( CParticleSystemDefinition *pDef )
	{
		m_nStartCP = clamp( m_nStartCP, 0, MAX_PARTICLE_CONTROL_POINTS - 1 );
		m_nEndCP = clamp( m_nEndCP, 0, MAX_PARTICLE_CONTROL_POINTS - 1 );
	}

	virtual void Operate( CParticleCollection *pParticles, float flStrength, void *pContext ) const;

	int m_nFieldOutput;
	float m_flInputMin;
	float m_flInputMax;
	Vector m_vecOutputMin;
	Vector m_vecOutputMax;
	int m_nStartCP;
	int m_nEndCP;
	bool m_bScaleInitialRange;
	bool m_bScaleCurrent;
	bool m_bActiveRange;
	bool m_bRadialCheck;
};

DEFINE_PARTICLE_OPERATOR( C_OP_PercentageBetweenCPsVector, "Remap Percentage Between Two Control Points to Vector", OPERATOR_GENERIC );

BEGIN_PARTICLE_OPERATOR_UNPACK( C_OP_PercentageBetweenCPsVector )
	DMXELEMENT_UNPACK_FIELD( "percentage minimum", "0", float, m_flInputMin )
	DMXELEMENT_UNPACK_FIELD( "percentage maximum", "1", float, m_flInputMax )
	DMXELEMENT_UNPACK_FIELD_USERDATA( "output field", "6", int, m_nFieldOutput, "intchoice particlefield_vector" )
	DMXELEMENT_UNPACK_FIELD( "output minimum", "0 0 0", Vector, m_vecOutputMin )
	DMXELEMENT_UNPACK_FIELD( "output maximum", "1 1 1", Vector, m_vecOutputMax )
	DMXELEMENT_UNPACK_FIELD( "starting control point", "0", int, m_nStartCP )
	DMXELEMENT_UNPACK_FIELD( "ending control point", "1", int, m_nEndCP )
	DMXELEMENT_UNPACK_FIELD( "output is scalar of initial random range", "0", bool, m_bScaleInitialRange )
	DMXELEMENT_UNPACK_FIELD( "output is scalar of current value", "0", bool, m_bScaleCurrent )
	DMXELEMENT_UNPACK_FIELD( "only active within input range", "0", bool, m_bActiveRange )
	DMXELEMENT_UNPACK_FIELD( "treat distance between points as radius", "1", bool, m_bRadialCheck )
END_PARTICLE_OPERATOR_UNPACK( C_OP_PercentageBetweenCPsVector )

void C_OP_PercentageBetweenCPsVector::Operate( CParticleCollection *pParticles, float flStrength, void *pContext ) const
{
	Vector vecControlPoint1 = pParticles->GetControlPointAtCurrentTime( m_nStartCP );

	C4VAttributeIterator xyz( PARTICLE_ATTRIBUTE_XYZ, pParticles );
	C4VAttributeWriteIterator pOutField( m_nFieldOutput, pParticles );
	C4VInitialAttributeIterator pInitialValue( m_nFieldOutput, pParticles );

	FourVectors fvControlPoint1;
	fvControlPoint1.DuplicateVector( pParticles->GetControlPointAtCurrentTime( m_nStartCP ) );
	FourVectors fvControlpoint2;
	fvControlpoint2.DuplicateVector( pParticles->GetControlPointAtCurrentTime( m_nEndCP ) );
	FourVectors fvDelta = fvControlPoint1;
	fvDelta -= fvControlpoint2;
	fltx4 fl4Distance = SqrtSIMD( AddSIMD( AddSIMD( MulSIMD( fvDelta.x, fvDelta.x ), MulSIMD( fvDelta.y, fvDelta.y ) ), MulSIMD( fvDelta.z, fvDelta.z ) ) );

	fltx4 fl4InputMin = ReplicateX4( m_flInputMin );
	fltx4 fl4InputMax = ReplicateX4( m_flInputMax );
	FourVectors fvOutputMin;
	fvOutputMin.DuplicateVector( m_vecOutputMin );
	FourVectors fvOutputMax;
	fvOutputMax.DuplicateVector( m_vecOutputMax );

	int nCtr = pParticles->m_nPaddedActiveParticles;
	do
	{
		fltx4 fl4Perc;
		if ( m_bRadialCheck )
		{
			FourVectors fvParticleDelta;
			fvParticleDelta.DuplicateVector( vecControlPoint1 );
			fvParticleDelta -= *xyz;
			fltx4 fl4ParticleDist = SqrtSIMD( AddSIMD( AddSIMD( MulSIMD( fvParticleDelta.x, fvParticleDelta.x ), MulSIMD( fvParticleDelta.y, fvParticleDelta.y ) ), MulSIMD( fvParticleDelta.z, fvParticleDelta.z ) ) );
			fl4Perc = DivSIMD( Four_Ones, DivSIMD( fl4Distance, AddSIMD( fl4ParticleDist, Four_Epsilons ) ) );
		}
		else
		{
			FourVectors fvClosestPoint;
			FourVectors::CalcClosestPointOnLineSIMD( *xyz, fvControlPoint1, fvControlpoint2, fvClosestPoint, &fl4Perc );
		}

		FourVectors fvOutput;
		fvOutput.x = RemapValClampedSIMD( fl4Perc, fl4InputMin, fl4InputMax, fvOutputMin.x, fvOutputMax.x );
		fvOutput.y = RemapValClampedSIMD( fl4Perc, fl4InputMin, fl4InputMax, fvOutputMin.y, fvOutputMax.y );
		fvOutput.z = RemapValClampedSIMD( fl4Perc, fl4InputMin, fl4InputMax, fvOutputMin.z, fvOutputMax.z );
		if ( m_bScaleInitialRange )
		{
			fvOutput.x = MulSIMD( pInitialValue->x, fvOutput.x );
			fvOutput.y = MulSIMD( pInitialValue->y, fvOutput.y );
			fvOutput.z = MulSIMD( pInitialValue->z, fvOutput.z );
		}
		if ( m_bScaleCurrent )
		{
			fvOutput.x = MulSIMD( pOutField->x, fvOutput.x );
			fvOutput.y = MulSIMD( pOutField->y, fvOutput.y );
			fvOutput.z = MulSIMD( pOutField->z, fvOutput.z );
		}
		if ( m_bActiveRange )
		{
			fltx4 fl4Mask = AndSIMD( CmpLeSIMD( fl4Perc, fl4InputMax ), CmpLeSIMD( fl4InputMin, fl4Perc ) );
			pOutField->x = MaskedAssign( fl4Mask, fvOutput.x, pOutField->x );
			pOutField->y = MaskedAssign( fl4Mask, fvOutput.y, pOutField->y );
			pOutField->z = MaskedAssign( fl4Mask, fvOutput.z, pOutField->z );
		}
		else
		{
			*pOutField = fvOutput;
		}

		++pOutField;
		++xyz;
		++pInitialValue;
	} while ( --nCtr );
}


//-----------------------------------------------------------------------------
// Remap distance to control point to a scalar
//-----------------------------------------------------------------------------
class C_OP_DistanceToCP : public CParticleOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_OP_DistanceToCP );

	uint32 GetWrittenAttributes( void ) const
	{
		return 1 << m_nFieldOutput;
	}

	uint32 GetReadAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_XYZ_MASK;
	}

	uint32 GetReadInitialAttributes( void ) const
	{
		return 1 << m_nFieldOutput;
	}

	virtual uint32 GetFilter( void ) const
	{
		return FILTER_PARAMETER_REMAPPING_MASK;
	}

	virtual uint64 GetReadControlPointMask() const
	{
		return 1ULL << m_nStartCP;
	}

	virtual void InitParams( CParticleSystemDefinition *pDef )
	{
		m_nCollisionGroupNumber = g_pParticleSystemMgr->Query()->GetCollisionGroupFromName( m_CollisionGroupName );
		m_nStartCP = clamp( m_nStartCP, 0, MAX_PARTICLE_CONTROL_POINTS - 1 );
	}

	virtual void Operate( CParticleCollection *pParticles, float flStrength, void *pContext ) const;

	int m_nFieldOutput;
	float m_flInputMin;
	float m_flInputMax;
	float m_flOutputMin;
	float m_flOutputMax;
	int m_nStartCP;
	bool m_bLOS;
	char m_CollisionGroupName[128];
	int m_nCollisionGroupNumber;
	float m_flMaxTraceLength;
	float m_flLOSScale;
	bool m_bScaleInitialRange;
	bool m_bScaleCurrent;
	bool m_bActiveRange;
};

DEFINE_PARTICLE_OPERATOR( C_OP_DistanceToCP, "Remap Distance to Control Point to Scalar", OPERATOR_GENERIC );

BEGIN_PARTICLE_OPERATOR_UNPACK( C_OP_DistanceToCP )
	DMXELEMENT_UNPACK_FIELD( "distance minimum", "0", float, m_flInputMin )
	DMXELEMENT_UNPACK_FIELD( "distance maximum", "128", float, m_flInputMax )
	DMXELEMENT_UNPACK_FIELD_USERDATA( "output field", "3", int, m_nFieldOutput, "intchoice particlefield_scalar" )
	DMXELEMENT_UNPACK_FIELD( "output minimum", "0", float, m_flOutputMin )
	DMXELEMENT_UNPACK_FIELD( "output maximum", "1", float, m_flOutputMax )
	DMXELEMENT_UNPACK_FIELD( "control point", "0", int, m_nStartCP )
	DMXELEMENT_UNPACK_FIELD( "ensure line of sight", "0", bool, m_bLOS )
	DMXELEMENT_UNPACK_FIELD_STRING( "LOS collision group", "NONE", m_CollisionGroupName )
	DMXELEMENT_UNPACK_FIELD( "Maximum Trace Length", "-1", float, m_flMaxTraceLength )
	DMXELEMENT_UNPACK_FIELD( "LOS Failure Scalar", "0", float, m_flLOSScale )
	DMXELEMENT_UNPACK_FIELD( "output is scalar of initial random range", "0", bool, m_bScaleInitialRange )
	DMXELEMENT_UNPACK_FIELD( "output is scalar of current value", "0", bool, m_bScaleCurrent )
	DMXELEMENT_UNPACK_FIELD( "only active within specified distance", "0", bool, m_bActiveRange )
END_PARTICLE_OPERATOR_UNPACK( C_OP_DistanceToCP )

void C_OP_DistanceToCP::Operate( CParticleCollection *pParticles, float flStrength, void *pContext ) const
{
	// clamp the result to 0 and 1 if it's alpha
	float flMin = m_flOutputMin;
	float flMax = m_flOutputMax;
	if ( ATTRIBUTES_WHICH_ARE_0_TO_1 & ( 1 << m_nFieldOutput ) )
	{
		flMin = clamp( m_flOutputMin, 0.0f, 1.0f );
		flMax = clamp( m_flOutputMax, 0.0f, 1.0f );
	}

	Vector vecControlPoint1 = pParticles->GetControlPointAtCurrentTime( m_nStartCP );

	for( int i = 0; i < pParticles->m_nActiveParticles; ++i )
	{
		const float *pXYZ = pParticles->GetFloatAttributePtr( PARTICLE_ATTRIBUTE_XYZ, i );
		Vector vecPosition2( pXYZ[0], pXYZ[4], pXYZ[8] );
		Vector vecDelta = vecControlPoint1 - vecPosition2;
		float flDistance = sqrt( ( vecDelta.z * vecDelta.z + vecDelta.y * vecDelta.y ) + vecDelta.x * vecDelta.x );

		if ( m_bActiveRange && ( flDistance < m_flInputMin || flDistance > m_flInputMax ) )
			continue;

		float flLOSDistance = flDistance;
		if ( m_bLOS )
		{
			Vector vecEndPoint = vecPosition2;
			if ( m_flMaxTraceLength != -1.0f && m_flMaxTraceLength < flDistance )
			{
				// NOTE: shipped code normalizes the particle position here, not the direction
				VectorNormalize( vecEndPoint );
				vecEndPoint *= m_flMaxTraceLength;
				vecEndPoint += vecControlPoint1;
			}
			CBaseTrace tr;
			g_pParticleSystemMgr->Query()->TraceLine( vecControlPoint1, vecEndPoint, MASK_OPAQUE_AND_NPCS, NULL, m_nCollisionGroupNumber, &tr );
			if ( tr.fraction != 1.0f )
			{
				flLOSDistance = m_flLOSScale * flDistance * tr.fraction;
			}
		}

		float flOutput = RemapValClamped( flLOSDistance, m_flInputMin, m_flInputMax, flMin, flMax );
		float *pOutput = pParticles->GetFloatAttributePtrForWrite( m_nFieldOutput, i );
		if ( m_bScaleInitialRange )
		{
			const float *pInitialOutput = pParticles->GetInitialFloatAttributePtr( m_nFieldOutput, i );
			flOutput = *pInitialOutput * flOutput;
		}
		if ( m_bScaleCurrent )
		{
			flOutput = *pOutput * flOutput;
		}
		*pOutput = Lerp( flStrength, *pOutput, flOutput );
	}
}


//-----------------------------------------------------------------------------
// Set a control point to the local player's position
//-----------------------------------------------------------------------------
class C_OP_SetControlPointToPlayer : public CParticleOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_OP_SetControlPointToPlayer );

	uint32 GetWrittenAttributes( void ) const
	{
		return 0;
	}

	uint32 GetReadAttributes( void ) const
	{
		return 0;
	}

	virtual uint32 GetFilter( void ) const
	{
		return FILTER_CONTROL_POINTS_MASK;
	}

	virtual void InitParams( CParticleSystemDefinition *pDef )
	{
		m_nCP1 = clamp( m_nCP1, 0, MAX_PARTICLE_CONTROL_POINTS - 1 );
	}

	virtual bool ShouldRunBeforeEmitters( void ) const
	{
		return true;
	}

	virtual void Operate( CParticleCollection *pParticles, float flStrength, void *pContext ) const;

	int m_nCP1;
	Vector m_vecCP1Pos;
	bool m_bOrientToEyes;
};

DEFINE_PARTICLE_OPERATOR( C_OP_SetControlPointToPlayer, "Set Control Point To Player", OPERATOR_GENERIC );

BEGIN_PARTICLE_OPERATOR_UNPACK( C_OP_SetControlPointToPlayer )
	DMXELEMENT_UNPACK_FIELD( "Control Point Number", "1", int, m_nCP1 )
	DMXELEMENT_UNPACK_FIELD( "Control Point Offset", "0 0 0", Vector, m_vecCP1Pos )
	DMXELEMENT_UNPACK_FIELD( "Use Eye Orientation", "0", bool, m_bOrientToEyes )
END_PARTICLE_OPERATOR_UNPACK( C_OP_SetControlPointToPlayer )

void C_OP_SetControlPointToPlayer::Operate( CParticleCollection *pParticles, float flStrength, void *pContext ) const
{
	Vector vecClientPos = g_pParticleSystemMgr->Query()->GetLocalPlayerPos();
	pParticles->SetControlPoint( m_nCP1, m_vecCP1Pos + vecClientPos );

	Vector vecForward;
	Vector vecRight;
	Vector vecUp;
	g_pParticleSystemMgr->Query()->GetLocalPlayerEyeVectors( &vecForward, &vecRight, &vecUp );
	if ( !m_bOrientToEyes )
	{
		if ( fabs( vecForward.z - 1.0f ) > 0.001 )
		{
			vecForward.z = 0;
			VectorNormalize( vecForward );
			vecUp = Vector( 0, 0, 1 );
			vecRight = Vector( vecForward.y, -vecForward.x, 0 );
		}
	}

	pParticles->SetControlPointOrientation( m_nCP1, vecForward, vecRight, vecUp );
}


//-----------------------------------------------------------------------------
// Movement lerp to hitbox
//-----------------------------------------------------------------------------
class C_OP_MoveToHitbox : public CParticleOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_OP_MoveToHitbox );

	uint32 GetWrittenAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_XYZ_MASK | PARTICLE_ATTRIBUTE_PREV_XYZ_MASK;
	}

	uint32 GetReadAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_XYZ_MASK | PARTICLE_ATTRIBUTE_PREV_XYZ_MASK |
			PARTICLE_ATTRIBUTE_CREATION_TIME_MASK | PARTICLE_ATTRIBUTE_HITBOX_INDEX_MASK |
			PARTICLE_ATTRIBUTE_HITBOX_RELATIVE_XYZ_MASK;
	}

	virtual uint64 GetReadControlPointMask() const
	{
		return 1ULL << m_nControlPointNumber;
	}

	virtual void InitParams( CParticleSystemDefinition *pDef )
	{
		m_nControlPointNumber = clamp( m_nControlPointNumber, 0, MAX_PARTICLE_CONTROL_POINTS - 1 );
	}

	virtual void Operate( CParticleCollection *pParticles, float flStrength, void *pContext ) const;

	int m_nControlPointNumber;
	int m_nControlPointNumberOverride;
	float m_flLifeTimeLerpStart;
	float m_flLifeTimeLerpEnd;
};

DEFINE_PARTICLE_OPERATOR( C_OP_MoveToHitbox, "Movement Lerp to Hitbox", OPERATOR_GENERIC );

BEGIN_PARTICLE_OPERATOR_UNPACK( C_OP_MoveToHitbox )
	DMXELEMENT_UNPACK_FIELD( "control point number", "0", int, m_nControlPointNumber )
	DMXELEMENT_UNPACK_FIELD( "lifetime lerp start", "0", float, m_flLifeTimeLerpStart )
	DMXELEMENT_UNPACK_FIELD( "lifetime lerp end", "1", float, m_flLifeTimeLerpEnd )
END_PARTICLE_OPERATOR_UNPACK( C_OP_MoveToHitbox )

void C_OP_MoveToHitbox::Operate( CParticleCollection *pParticles, float flStrength, void *pContext ) const
{
	pParticles->UpdateHitBoxInfo( m_nControlPointNumber );
	if ( pParticles->ControlPointHitBox( m_nControlPointNumber ).CurAndPrevValid() )
	{
		float flAgeThreshold = m_flLifeTimeLerpEnd;
		if ( flAgeThreshold <= 0.0f )
			flAgeThreshold = 1.0e20f;
		float flIScale = 0.0f;
		if ( m_flLifeTimeLerpEnd > m_flLifeTimeLerpStart )
			flIScale = 1.0f / ( m_flLifeTimeLerpEnd - m_flLifeTimeLerpStart );

		for( int i = 0; i < pParticles->m_nActiveParticles; i++ )
		{
			float *pXYZ = pParticles->GetFloatAttributePtrForWrite( PARTICLE_ATTRIBUTE_XYZ, i );
			float *pPrevXYZ = pParticles->GetFloatAttributePtrForWrite( PARTICLE_ATTRIBUTE_PREV_XYZ, i );
			const float *pUVW = pParticles->GetFloatAttributePtr( PARTICLE_ATTRIBUTE_HITBOX_RELATIVE_XYZ, i );
			const int nBoxIndex = *pParticles->GetIntAttributePtr( PARTICLE_ATTRIBUTE_HITBOX_INDEX, i );
			const float *pCreationTime = pParticles->GetFloatAttributePtr( PARTICLE_ATTRIBUTE_CREATION_TIME, i );
			float flAge = pParticles->m_flCurTime - *pCreationTime;
			if ( flAge < flAgeThreshold )
			{
				if ( ( nBoxIndex < pParticles->ControlPointHitBox( m_nControlPointNumber ).m_nNumHitBoxes ) &&
					 ( nBoxIndex < pParticles->ControlPointHitBox( m_nControlPointNumber ).m_nNumPrevHitBoxes ) &&
					 ( nBoxIndex >= 0 ) )
				{
					ModelHitBoxInfo_t const &hb = pParticles->ControlPointHitBox( m_nControlPointNumber ).m_pHitBoxes[ nBoxIndex ];
					Vector vecParticlePosition;
					vecParticlePosition.x = Lerp( pUVW[0], hb.m_vecBoxMins.x, hb.m_vecBoxMaxes.x );
					vecParticlePosition.y = Lerp( pUVW[4], hb.m_vecBoxMins.y, hb.m_vecBoxMaxes.y );
					vecParticlePosition.z = Lerp( pUVW[8], hb.m_vecBoxMins.z, hb.m_vecBoxMaxes.z );
					Vector vecWorldPosition;
					VectorTransform( vecParticlePosition, hb.m_Transform, vecWorldPosition );
					if ( flAge > m_flLifeTimeLerpStart )
					{
						float flT = ( ( flAge - m_flLifeTimeLerpStart ) * flIScale ) * flStrength;
						pXYZ[0] = Lerp( flT, pXYZ[0], vecWorldPosition.x );
						pXYZ[4] = Lerp( flT, pXYZ[4], vecWorldPosition.y );
						pXYZ[8] = Lerp( flT, pXYZ[8], vecWorldPosition.z );
						pPrevXYZ[0] = Lerp( flT, pPrevXYZ[0], vecWorldPosition.x );
						pPrevXYZ[4] = Lerp( flT, pPrevXYZ[4], vecWorldPosition.y );
						pPrevXYZ[8] = Lerp( flT, pPrevXYZ[8], vecWorldPosition.z );
					}
				}
			}
		}
	}
}


//-----------------------------------------------------------------------------
// Movement lock to bone
//-----------------------------------------------------------------------------
class C_OP_LockToBone : public CParticleOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_OP_LockToBone );

	uint32 GetWrittenAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_XYZ_MASK | PARTICLE_ATTRIBUTE_PREV_XYZ_MASK;
	}

	uint32 GetReadAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_XYZ_MASK | PARTICLE_ATTRIBUTE_PREV_XYZ_MASK |
			PARTICLE_ATTRIBUTE_CREATION_TIME_MASK | PARTICLE_ATTRIBUTE_HITBOX_INDEX_MASK |
			PARTICLE_ATTRIBUTE_HITBOX_RELATIVE_XYZ_MASK;
	}

	virtual uint64 GetReadControlPointMask() const
	{
		return 1ULL << m_nControlPointNumber;
	}

	virtual void InitParams( CParticleSystemDefinition *pDef )
	{
		m_nControlPointNumber = clamp( m_nControlPointNumber, 0, MAX_PARTICLE_CONTROL_POINTS - 1 );
	}

	virtual void Operate( CParticleCollection *pParticles, float flStrength, void *pContext ) const;

	int m_nControlPointNumber;
	float m_flLifeTimeFadeStart;
	float m_flLifeTimeFadeEnd;
};

DEFINE_PARTICLE_OPERATOR( C_OP_LockToBone, "Movement Lock to Bone", OPERATOR_GENERIC );

BEGIN_PARTICLE_OPERATOR_UNPACK( C_OP_LockToBone )
	DMXELEMENT_UNPACK_FIELD( "control_point_number", "0", int, m_nControlPointNumber )
	DMXELEMENT_UNPACK_FIELD( "lifetime fade start", "0", float, m_flLifeTimeFadeStart )
	DMXELEMENT_UNPACK_FIELD( "lifetime fade end", "0", float, m_flLifeTimeFadeEnd )
END_PARTICLE_OPERATOR_UNPACK( C_OP_LockToBone )

void C_OP_LockToBone::Operate( CParticleCollection *pParticles, float flStrength, void *pContext ) const
{
	pParticles->UpdateHitBoxInfo( m_nControlPointNumber );
	if ( pParticles->ControlPointHitBox( m_nControlPointNumber ).CurAndPrevValid() )
	{
		float flAgeThreshold = m_flLifeTimeFadeEnd;
		if ( flAgeThreshold <= 0.0f )
			flAgeThreshold = 1.0e20f;
		float flIScale = 0.0f;
		if ( m_flLifeTimeFadeEnd > m_flLifeTimeFadeStart )
			flIScale = 1.0f / ( m_flLifeTimeFadeEnd - m_flLifeTimeFadeStart );

		for( int i = 0; i < pParticles->m_nActiveParticles; i++ )
		{
			float *pXYZ = pParticles->GetFloatAttributePtrForWrite( PARTICLE_ATTRIBUTE_XYZ, i );
			float *pPrevXYZ = pParticles->GetFloatAttributePtrForWrite( PARTICLE_ATTRIBUTE_PREV_XYZ, i );
			const float *pUVW = pParticles->GetFloatAttributePtr( PARTICLE_ATTRIBUTE_HITBOX_RELATIVE_XYZ, i );
			const int nBoxIndex = *pParticles->GetIntAttributePtr( PARTICLE_ATTRIBUTE_HITBOX_INDEX, i );
			const float *pCreationTime = pParticles->GetFloatAttributePtr( PARTICLE_ATTRIBUTE_CREATION_TIME, i );
			float flAge = pParticles->m_flCurTime - *pCreationTime;
			if ( flAge < flAgeThreshold )
			{
				if ( ( nBoxIndex < pParticles->ControlPointHitBox( m_nControlPointNumber ).m_nNumHitBoxes ) &&
					 ( nBoxIndex < pParticles->ControlPointHitBox( m_nControlPointNumber ).m_nNumPrevHitBoxes ) &&
					 ( nBoxIndex >= 0 ) )
				{
					ModelHitBoxInfo_t const &hb = pParticles->ControlPointHitBox( m_nControlPointNumber ).m_pHitBoxes[ nBoxIndex ];
					Vector vecParticlePosition;
					vecParticlePosition.x = Lerp( pUVW[0], hb.m_vecBoxMins.x, hb.m_vecBoxMaxes.x );
					vecParticlePosition.y = Lerp( pUVW[4], hb.m_vecBoxMins.y, hb.m_vecBoxMaxes.y );
					vecParticlePosition.z = Lerp( pUVW[8], hb.m_vecBoxMins.z, hb.m_vecBoxMaxes.z );
					Vector vecWorldPosition;
					VectorTransform( vecParticlePosition, hb.m_Transform, vecWorldPosition );

					ModelHitBoxInfo_t phb = pParticles->ControlPointHitBox( m_nControlPointNumber ).m_pPrevBoxes[ nBoxIndex ];
					Vector vecPrevParticlePosition;
					vecPrevParticlePosition.x = Lerp( pUVW[0], phb.m_vecBoxMins.x, phb.m_vecBoxMaxes.x );
					vecPrevParticlePosition.y = Lerp( pUVW[4], phb.m_vecBoxMins.y, phb.m_vecBoxMaxes.y );
					vecPrevParticlePosition.z = Lerp( pUVW[8], phb.m_vecBoxMins.z, phb.m_vecBoxMaxes.z );
					Vector vecPrevWorldPosition;
					VectorTransform( vecPrevParticlePosition, phb.m_Transform, vecPrevWorldPosition );

					Vector vecDelta = vecWorldPosition - vecPrevWorldPosition;
					// NOTE: shipped code only applies flStrength once the fade has started
					if ( flAge > m_flLifeTimeFadeStart )
					{
						vecDelta *= ( 1.0f - ( flAge - m_flLifeTimeFadeStart ) * flIScale ) * flStrength;
					}

					pXYZ[0] += vecDelta.x;
					pXYZ[4] += vecDelta.y;
					pXYZ[8] += vecDelta.z;
					pPrevXYZ[0] += vecDelta.x;
					pPrevXYZ[4] += vecDelta.y;
					pPrevXYZ[8] += vecDelta.z;
				}
			}
		}
	}
}


//-----------------------------------------------------------------------------
// Cull when crossing a plane
//-----------------------------------------------------------------------------
class C_OP_PlaneCull : public CParticleOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_OP_PlaneCull );

	uint32 GetWrittenAttributes( void ) const
	{
		return 0;
	}

	uint32 GetReadAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_XYZ_MASK;
	}

	virtual uint32 GetFilter( void ) const
	{
		return FILTER_LIFE_DURATION_MASK;
	}

	virtual uint64 GetReadControlPointMask() const
	{
		return 1ULL << m_nPlaneControlPoint;
	}

	virtual void Operate( CParticleCollection *pParticles, float flStrength, void *pContext ) const;

	int m_nPlaneControlPoint;
	Vector m_vecPlaneDirection;
	float m_flPlaneOffset;
};

DEFINE_PARTICLE_OPERATOR( C_OP_PlaneCull, "Cull when crossing plane", OPERATOR_GENERIC );

BEGIN_PARTICLE_OPERATOR_UNPACK( C_OP_PlaneCull )
	DMXELEMENT_UNPACK_FIELD( "Control Point for point on plane", "0", int, m_nPlaneControlPoint )
	DMXELEMENT_UNPACK_FIELD( "Cull plane offset", "0", float, m_flPlaneOffset )
	DMXELEMENT_UNPACK_FIELD( "Plane Normal", "0 0 1", Vector, m_vecPlaneDirection )
END_PARTICLE_OPERATOR_UNPACK( C_OP_PlaneCull )

void C_OP_PlaneCull::Operate( CParticleCollection *pParticles, float flStrength, void *pContext ) const
{
	C4VAttributeIterator pXYZ( PARTICLE_ATTRIBUTE_XYZ, pParticles );
	int nLimit = pParticles->m_nPaddedActiveParticles << 2;

	// setup vars
	FourVectors v4N;
	v4N.DuplicateVector( m_vecPlaneDirection );
	v4N.VectorNormalize();
	FourVectors v4Pnt;
	v4Pnt.DuplicateVector( pParticles->GetControlPointAtCurrentTime( m_nPlaneControlPoint ) );
	FourVectors ofs = v4N;
	ofs *= ReplicateX4( m_flPlaneOffset );
	v4Pnt -= ofs;

	for ( int i = 0; i < nLimit; i += 4 )
	{
		FourVectors f4PlaneRel = ( *pXYZ );
		f4PlaneRel -= v4Pnt;
		fltx4 fl4PlaneEq = ( f4PlaneRel * v4N );
		if ( IsAnyNegative( fl4PlaneEq ) )
		{
			// not especially pretty - we need to kill some particles.
			int nMask = TestSignSIMD( fl4PlaneEq );
			if ( nMask & 1 )
				pParticles->KillParticle( i );
			if ( nMask & 2 )
				pParticles->KillParticle( i + 1 );
			if ( nMask & 4 )
				pParticles->KillParticle( i + 2 );
			if ( nMask & 8 )
				pParticles->KillParticle( i + 3 );
		}
		++pXYZ;
	}
}


//-----------------------------------------------------------------------------
// Cull relative to model
//-----------------------------------------------------------------------------
class C_OP_ModelCull : public CParticleOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_OP_ModelCull );

	uint32 GetWrittenAttributes( void ) const
	{
		return 0;
	}

	uint32 GetReadAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_XYZ_MASK;
	}

	virtual uint32 GetFilter( void ) const
	{
		return FILTER_POSITION_AND_VELOCITY_MASK | FILTER_LIFE_DURATION_MASK;
	}

	virtual void InitParams( CParticleSystemDefinition *pDef )
	{
		m_nControlPointNumber = clamp( m_nControlPointNumber, 0, MAX_PARTICLE_CONTROL_POINTS - 1 );
	}

	virtual void Operate( CParticleCollection *pParticles, float flStrength, void *pContext ) const;

	int m_nControlPointNumber;
	bool m_bBoundBox;
	bool m_bCullOutside;
};

DEFINE_PARTICLE_OPERATOR( C_OP_ModelCull, "Cull relative to model", OPERATOR_GENERIC );

BEGIN_PARTICLE_OPERATOR_UNPACK( C_OP_ModelCull )
	DMXELEMENT_UNPACK_FIELD( "control_point_number", "0", int, m_nControlPointNumber )
	DMXELEMENT_UNPACK_FIELD( "use only bounding box", "0", bool, m_bBoundBox )
	DMXELEMENT_UNPACK_FIELD( "cull outside instead of inside", "0", bool, m_bCullOutside )
END_PARTICLE_OPERATOR_UNPACK( C_OP_ModelCull )

void C_OP_ModelCull::Operate( CParticleCollection *pParticles, float flStrength, void *pContext ) const
{
	if ( pParticles->ControlPoint( m_nControlPointNumber ).m_pObject != NULL )
	{
		pParticles->UpdateHitBoxInfo( m_nControlPointNumber );
		if ( pParticles->ControlPointHitBox( m_nControlPointNumber ).CurAndPrevValid() )
		{
			for ( int i = 0; i < pParticles->m_nActiveParticles; ++i )
			{
				const float *pXYZ = pParticles->GetFloatAttributePtr( PARTICLE_ATTRIBUTE_XYZ, i );
				Vector vecParticlePosition( pXYZ[0], pXYZ[4], pXYZ[8] );

				bool bInside = g_pParticleSystemMgr->Query()->IsPointInControllingObjectHitBox( pParticles, m_nControlPointNumber, vecParticlePosition, m_bBoundBox );
				if ( ( bInside && !m_bCullOutside ) || ( !bInside && m_bCullOutside ) )
				{
					pParticles->KillParticle( i );
				}
			}
		}
	}
}


//-----------------------------------------------------------------------------
// Set a control point to the center of the particles
//-----------------------------------------------------------------------------
class C_OP_SetControlPointToCenter : public CParticleOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_OP_SetControlPointToCenter );

	uint32 GetWrittenAttributes( void ) const
	{
		return 0;
	}

	uint32 GetReadAttributes( void ) const
	{
		return 0;
	}

	virtual uint32 GetFilter( void ) const
	{
		return FILTER_CONTROL_POINTS_MASK;
	}

	virtual void InitParams( CParticleSystemDefinition *pDef )
	{
		m_nCP1 = clamp( m_nCP1, 0, MAX_PARTICLE_CONTROL_POINTS - 1 );
	}

	virtual bool ShouldRunBeforeEmitters( void ) const
	{
		return true;
	}

	virtual void Operate( CParticleCollection *pParticles, float flStrength, void *pContext ) const;

	int m_nCP1;
	Vector m_vecCP1Pos;
};

DEFINE_PARTICLE_OPERATOR( C_OP_SetControlPointToCenter, "Set Control Point To Particles' Center", OPERATOR_GENERIC );

BEGIN_PARTICLE_OPERATOR_UNPACK( C_OP_SetControlPointToCenter )
	DMXELEMENT_UNPACK_FIELD( "Control Point Number to Set", "1", int, m_nCP1 )
	DMXELEMENT_UNPACK_FIELD( "Center Offset", "0 0 0", Vector, m_vecCP1Pos )
END_PARTICLE_OPERATOR_UNPACK( C_OP_SetControlPointToCenter )

void C_OP_SetControlPointToCenter::Operate( CParticleCollection *pParticles, float flStrength, void *pContext ) const
{
	Vector vecMinBounds;
	Vector vecMaxBounds;

	pParticles->GetBounds( &vecMinBounds, &vecMaxBounds );

	Vector vecCenter = ( ( vecMaxBounds + vecMinBounds ) * 0.5f );

	pParticles->SetControlPoint( m_nCP1, m_vecCP1Pos + vecCenter );
}


//-----------------------------------------------------------------------------
// Movement match particle velocities
//-----------------------------------------------------------------------------
struct VelocityMatchingForceContext_t
{
	Vector m_vecAvgVelocity;
	float m_flAvgSpeed;
};

// VectorNormalizeFast with the length summed in the order the shipped build used ( z, y, x, epsilon )
static FORCEINLINE void VectorNormalizeFast_ZYX( Vector &vec )
{
	float ool = FastRSqrt( ( ( vec.z * vec.z + vec.y * vec.y ) + vec.x * vec.x ) + FLT_EPSILON );
	vec.x *= ool;
	vec.y *= ool;
	vec.z *= ool;
}

class C_OP_VelocityMatchingForce : public CParticleOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_OP_VelocityMatchingForce );

	uint32 GetWrittenAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_XYZ_MASK | PARTICLE_ATTRIBUTE_PREV_XYZ_MASK;
	}

	uint32 GetReadAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_XYZ_MASK | PARTICLE_ATTRIBUTE_PREV_XYZ_MASK;
	}

	virtual void InitializeContextData( CParticleCollection *pParticles, void *pContext ) const
	{
		VelocityMatchingForceContext_t *pCtx = ( VelocityMatchingForceContext_t * )pContext;
		pCtx->m_vecAvgVelocity = vec3_origin;
		pCtx->m_flAvgSpeed = 0;
	}

	size_t GetRequiredContextBytes( void ) const
	{
		return sizeof( VelocityMatchingForceContext_t );
	}

	virtual void Operate( CParticleCollection *pParticles, float flStrength, void *pContext ) const;

	float m_flDirScale;
	float m_flSpdScale;
	int m_nCPBroadcast;
};

DEFINE_PARTICLE_OPERATOR( C_OP_VelocityMatchingForce, "Movement Match Particle Velocities", OPERATOR_GENERIC );

BEGIN_PARTICLE_OPERATOR_UNPACK( C_OP_VelocityMatchingForce )
	DMXELEMENT_UNPACK_FIELD( "Direction Matching Strength", "0.25", float, m_flDirScale )
	DMXELEMENT_UNPACK_FIELD( "Speed Matching Strength", "0.25", float, m_flSpdScale )
	DMXELEMENT_UNPACK_FIELD( "Control Point to Broadcast Speed and Direction To", "-1", int, m_nCPBroadcast )
END_PARTICLE_OPERATOR_UNPACK( C_OP_VelocityMatchingForce )

void C_OP_VelocityMatchingForce::Operate( CParticleCollection *pParticles, float flStrength, void *pContext ) const
{
	VelocityMatchingForceContext_t *pCtx = ( VelocityMatchingForceContext_t * )pContext;

	Vector vecVelocityAvg = vec3_origin;
	float flAvgSpeed = 0;

	// FIXME: SSE-ize
	for ( int i = 0; i < pParticles->m_nActiveParticles; ++i )
	{
		float *xyz = pParticles->GetFloatAttributePtrForWrite( PARTICLE_ATTRIBUTE_XYZ, i );
		const float *pxyz = pParticles->GetFloatAttributePtr( PARTICLE_ATTRIBUTE_PREV_XYZ, i );

		Vector vecXYZ( xyz[0], xyz[4], xyz[8] );
		Vector vecPXYZ( pxyz[0], pxyz[4], pxyz[8] );
		Vector vecVelocityCur = ( ( vecXYZ - vecPXYZ ) / pParticles->m_flDt );
		vecVelocityAvg += vecVelocityCur;
		float flSpeed = sqrt( ( vecVelocityCur.z * vecVelocityCur.z + vecVelocityCur.y * vecVelocityCur.y ) + vecVelocityCur.x * vecVelocityCur.x );
		flAvgSpeed += flSpeed;

		if ( pCtx->m_vecAvgVelocity != vec3_origin )
		{
			Vector vecScaledXYZ;
			VectorNormalizeFast_ZYX( vecVelocityCur );
			VectorLerp( vecVelocityCur, pCtx->m_vecAvgVelocity, m_flDirScale, vecVelocityCur );
			VectorNormalizeFast_ZYX( vecVelocityCur );
			flSpeed = Lerp( m_flSpdScale, flSpeed, pCtx->m_flAvgSpeed );
			vecVelocityCur *= flSpeed;
			VectorMA( vecPXYZ, pParticles->m_flDt, vecVelocityCur, vecScaledXYZ );
			xyz[0] = vecScaledXYZ.x;
			xyz[4] = vecScaledXYZ.y;
			xyz[8] = vecScaledXYZ.z;
		}
	}

	VectorNormalizeFast_ZYX( vecVelocityAvg );
	pCtx->m_vecAvgVelocity = vecVelocityAvg;
	pCtx->m_flAvgSpeed = flAvgSpeed * ( 1.0f / pParticles->m_nActiveParticles );
	if ( m_nCPBroadcast != -1 )
	{
		pParticles->SetControlPoint( m_nCPBroadcast, Vector( pCtx->m_flAvgSpeed, pCtx->m_flAvgSpeed, pCtx->m_flAvgSpeed ) );
		pParticles->SetControlPointForwardVector( m_nCPBroadcast, pCtx->m_vecAvgVelocity );
	}
}


//-----------------------------------------------------------------------------
// Movement maintain offset
//-----------------------------------------------------------------------------
class C_OP_MovementMaintainOffset : public CParticleOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_OP_MovementMaintainOffset );

	uint32 GetWrittenAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_XYZ_MASK | PARTICLE_ATTRIBUTE_PREV_XYZ_MASK;
	}

	uint32 GetReadAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_XYZ_MASK | PARTICLE_ATTRIBUTE_PREV_XYZ_MASK;
	}

	virtual void Operate( CParticleCollection *pParticles, float flStrength, void *pContext ) const;

	Vector m_vecOffset;
	int m_nCP;
	bool m_bRadiusScale;
};

DEFINE_PARTICLE_OPERATOR( C_OP_MovementMaintainOffset, "Movement Maintain Offset", OPERATOR_GENERIC );

BEGIN_PARTICLE_OPERATOR_UNPACK( C_OP_MovementMaintainOffset )
	DMXELEMENT_UNPACK_FIELD( "Local Space CP", "-1", int, m_nCP )
	DMXELEMENT_UNPACK_FIELD( "Desired Offset", "0 0 0", Vector, m_vecOffset )
	DMXELEMENT_UNPACK_FIELD( "Scale by Radius", "0", bool, m_bRadiusScale )
END_PARTICLE_OPERATOR_UNPACK( C_OP_MovementMaintainOffset )

void C_OP_MovementMaintainOffset::Operate( CParticleCollection *pParticles, float flStrength, void *pContext ) const
{
	Vector vecLocalOffset = m_vecOffset;
	if ( m_nCP > -1 )
	{
		matrix3x4_t mat;
		pParticles->GetControlPointTransformAtCurrentTime( m_nCP, &mat );
		VectorRotate( m_vecOffset, mat, vecLocalOffset );
	}

	Vector vecOffsetPos = ( vecLocalOffset * (float)( pParticles->m_nActiveParticles - 1 ) ) * 0.5f;

	Vector vecCurAvgPos = vec3_origin;
	Vector vecCurAvgPrevPos = vec3_origin;
	for ( int i = 0; i < pParticles->m_nActiveParticles; ++i )
	{
		const float *xyz = pParticles->GetFloatAttributePtr( PARTICLE_ATTRIBUTE_XYZ, i );
		const float *pxyz = pParticles->GetFloatAttributePtr( PARTICLE_ATTRIBUTE_PREV_XYZ, i );
		Vector vecXYZ( xyz[0], xyz[4], xyz[8] );
		Vector vecPXYZ( pxyz[0], pxyz[4], pxyz[8] );
		vecCurAvgPos += vecXYZ;
		vecCurAvgPrevPos += vecPXYZ;
	}
	vecCurAvgPos /= (float)pParticles->m_nActiveParticles;
	vecCurAvgPrevPos /= (float)pParticles->m_nActiveParticles;

	for ( int i = 0; i < pParticles->m_nActiveParticles; ++i )
	{
		float *xyz = pParticles->GetFloatAttributePtrForWrite( PARTICLE_ATTRIBUTE_XYZ, i );
		float *pxyz = pParticles->GetFloatAttributePtrForWrite( PARTICLE_ATTRIBUTE_PREV_XYZ, i );

		float flRadiusScale = 1.0f;
		if ( m_bRadiusScale )
		{
			flRadiusScale = *pParticles->GetFloatAttributePtr( PARTICLE_ATTRIBUTE_RADIUS, i );
		}
		Vector vecScaledOffsetPos = vecOffsetPos * flRadiusScale;

		Vector vecXYZ = vecCurAvgPos + vecScaledOffsetPos;
		Vector vecPXYZ = vecCurAvgPrevPos + vecScaledOffsetPos;
		xyz[0] = vecXYZ.x;
		xyz[4] = vecXYZ.y;
		xyz[8] = vecXYZ.z;
		pxyz[0] = vecPXYZ.x;
		pxyz[4] = vecPXYZ.y;
		pxyz[8] = vecPXYZ.z;

		vecOffsetPos -= vecLocalOffset;
	}
}


//-----------------------------------------------------------------------------
// Movement place on ground
//-----------------------------------------------------------------------------
struct PlaceOnGroundContext_t
{
	Vector m_vecPrevPos1;
	Vector m_vecPrevPos2;
};

class C_OP_MovementPlaceOnGround : public CParticleOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_OP_MovementPlaceOnGround );

	uint32 GetWrittenAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_XYZ_MASK | PARTICLE_ATTRIBUTE_PREV_XYZ_MASK | PARTICLE_ATTRIBUTE_LIFE_DURATION_MASK | PARTICLE_ATTRIBUTE_NORMAL_MASK;
	}

	uint32 GetReadAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_XYZ_MASK | PARTICLE_ATTRIBUTE_PREV_XYZ_MASK;
	}

	virtual void InitParams( CParticleSystemDefinition *pDef )
	{
		m_nCollisionGroupNumber = g_pParticleSystemMgr->Query()->GetCollisionGroupFromName( m_CollisionGroupName );
		m_CollisionMask = m_bIncludeWater ? ( MASK_SHOT_HULL | MASK_WATER ) : MASK_SHOT_HULL;

		if ( ( m_nRefCP1 > -1 || m_nRefCP2 > -1 ) && m_flTolerance > 0.0f )
			m_bUsesCPs = true;
		else
			m_bUsesCPs = false;
	}

	virtual void InitializeContextData( CParticleCollection *pParticles, void *pContext ) const
	{
		PlaceOnGroundContext_t *pCtx = ( PlaceOnGroundContext_t * )pContext;
		pCtx->m_vecPrevPos1 = vec3_invalid;
		pCtx->m_vecPrevPos2 = vec3_invalid;
	}

	size_t GetRequiredContextBytes( void ) const
	{
		return sizeof( PlaceOnGroundContext_t );
	}

	virtual void Operate( CParticleCollection *pParticles, float flStrength, void *pContext ) const;

	float m_flOffset;
	float m_flMaxTraceLength;
	float m_flTolerance;
	float m_flTraceOffset;
	char m_CollisionGroupName[128];
	int m_nCollisionGroupNumber;
	int m_nRefCP1;
	int m_nRefCP2;
	unsigned int m_CollisionMask;
	bool m_bKill;
	bool m_bIncludeWater;
	bool m_bUsesCPs;
};

DEFINE_PARTICLE_OPERATOR( C_OP_MovementPlaceOnGround, "Movement Place On Ground", OPERATOR_GENERIC );

BEGIN_PARTICLE_OPERATOR_UNPACK( C_OP_MovementPlaceOnGround )
	DMXELEMENT_UNPACK_FIELD( "offset", "0", float, m_flOffset )
	DMXELEMENT_UNPACK_FIELD( "kill on no collision", "0", bool, m_bKill )
	DMXELEMENT_UNPACK_FIELD( "include water", "0", bool, m_bIncludeWater )
	DMXELEMENT_UNPACK_FIELD( "max trace length", "128", float, m_flMaxTraceLength )
	DMXELEMENT_UNPACK_FIELD( "trace offset", "64", float, m_flTraceOffset )
	DMXELEMENT_UNPACK_FIELD_STRING( "collision group", "NONE", m_CollisionGroupName )
	DMXELEMENT_UNPACK_FIELD( "reference CP 1", "-1", int, m_nRefCP1 )
	DMXELEMENT_UNPACK_FIELD( "reference CP 2", "-1", int, m_nRefCP2 )
	DMXELEMENT_UNPACK_FIELD( "CP movement tolerance", "32", float, m_flTolerance )
END_PARTICLE_OPERATOR_UNPACK( C_OP_MovementPlaceOnGround )

void C_OP_MovementPlaceOnGround::Operate( CParticleCollection *pParticles, float flStrength, void *pContext ) const
{
	PlaceOnGroundContext_t *pCtx = ( PlaceOnGroundContext_t * )pContext;
	bool bDirty = false;

	// NOTE: shipped code only ever places particles when reference control points are in use and have moved
	if ( m_bUsesCPs )
	{
		if ( m_nRefCP1 > -1 )
		{
			Vector vecDelta = pParticles->GetControlPointAtCurrentTime( m_nRefCP1 ) - pCtx->m_vecPrevPos1;
			float flDist = sqrt( ( vecDelta.y * vecDelta.y + vecDelta.z * vecDelta.z ) + vecDelta.x * vecDelta.x );
			if ( flDist > m_flTolerance )
			{
				bDirty = true;
				pCtx->m_vecPrevPos1 = pParticles->GetControlPointAtCurrentTime( m_nRefCP1 );
			}
		}
		if ( m_nRefCP2 > -1 )
		{
			Vector vecDelta = pParticles->GetControlPointAtCurrentTime( m_nRefCP2 ) - pCtx->m_vecPrevPos2;
			float flDist = sqrt( ( vecDelta.y * vecDelta.y + vecDelta.z * vecDelta.z ) + vecDelta.x * vecDelta.x );
			if ( flDist > m_flTolerance )
			{
				bDirty = true;
				pCtx->m_vecPrevPos2 = pParticles->GetControlPointAtCurrentTime( m_nRefCP2 );
			}
		}
	}
	if ( !bDirty )
		return;

	for ( int i = 0; i < pParticles->m_nActiveParticles; ++i )
	{
		float *xyz = pParticles->GetFloatAttributePtrForWrite( PARTICLE_ATTRIBUTE_XYZ, i );
		float *pxyz = pParticles->GetFloatAttributePtrForWrite( PARTICLE_ATTRIBUTE_PREV_XYZ, i );
		float *plife = pParticles->GetFloatAttributePtrForWrite( PARTICLE_ATTRIBUTE_LIFE_DURATION, i );

		Vector vecStartPos( xyz[0], xyz[4], xyz[8] + m_flTraceOffset );
		Vector vecEndPos = vecStartPos + Vector( 0, 0, -1 ) * m_flMaxTraceLength;

		CBaseTrace tr;
		g_pParticleSystemMgr->Query()->TraceLine( vecStartPos, vecEndPos, m_CollisionMask, NULL, m_nCollisionGroupNumber, &tr );

		if ( tr.fraction == 1.0f && m_bKill )
		{
			*plife = -1.0f;
		}
		else
		{
			Vector vecPos = tr.endpos;
			vecPos.z += m_flOffset;
			xyz[0] = vecPos.x;
			xyz[4] = vecPos.y;
			xyz[8] = vecPos.z;
			pxyz[0] = vecPos.x;
			pxyz[4] = vecPos.y;
			pxyz[8] = vecPos.z;
		}
	}
}

// 1ULL << n with the result the MSVC __allshl helper gives for out-of-range counts
// (count taken from the low byte; >= 64 yields 0). Used where the CP index may be -1.
static inline uint64 AllShl1( int n )
{
	unsigned int nCount = (unsigned int)n & 0xff;
	return ( nCount >= 64 ) ? 0 : ( 1ULL << nCount );
}



//-----------------------------------------------------------------------------
// Rotation Orient to 2D Direction
//-----------------------------------------------------------------------------
class C_OP_OrientTo2dDirection : public CParticleOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_OP_OrientTo2dDirection );

	uint32 GetWrittenAttributes( void ) const
	{
		return 1 << m_nFieldOutput;
	}

	uint32 GetReadAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_XYZ_MASK | PARTICLE_ATTRIBUTE_PREV_XYZ_MASK;
	}

	virtual void Operate( CParticleCollection *pParticles, float flStrength, void *pContext ) const;

	float m_flRotOffset;
	float m_flSpinStrength;
	int m_nFieldOutput;
};

DEFINE_PARTICLE_OPERATOR( C_OP_OrientTo2dDirection, "Rotation Orient to 2D Direction", OPERATOR_GENERIC );

BEGIN_PARTICLE_OPERATOR_UNPACK( C_OP_OrientTo2dDirection )
	DMXELEMENT_UNPACK_FIELD( "Rotation Offset", "0", float, m_flRotOffset )
	DMXELEMENT_UNPACK_FIELD( "Spin Strength", "1", float, m_flSpinStrength )
	DMXELEMENT_UNPACK_FIELD_USERDATA( "rotation field", "4", int, m_nFieldOutput, "intchoice particlefield_rotation" )
END_PARTICLE_OPERATOR_UNPACK( C_OP_OrientTo2dDirection )

void C_OP_OrientTo2dDirection::Operate( CParticleCollection *pParticles, float flStrength, void *pContext ) const
{
	float flRotOffset = m_flRotOffset * ( M_PI / 180.0f );

	for( int i = 0; i < pParticles->m_nActiveParticles; ++i )
	{
		const float *xyz = pParticles->GetFloatAttributePtr( PARTICLE_ATTRIBUTE_XYZ, i );
		const float *xyz_prev = pParticles->GetFloatAttributePtr( PARTICLE_ATTRIBUTE_PREV_XYZ, i );
		float *roll = pParticles->GetFloatAttributePtrForWrite( m_nFieldOutput, i );

		float flVelX = xyz[0] - xyz_prev[0];
		float flVelY = xyz[4] - xyz_prev[4];

		float flOOLen = FastRSqrt( flVelX * flVelX + flVelY * flVelY + FLT_EPSILON );
		flVelX *= flOOLen;
		flVelY *= flOOLen;

		float flCurRot = *roll;

		float flVelRot = atan2( flVelY, flVelX ) + M_PI;
		flVelRot += flRotOffset;

		*roll = flCurRot + ( flVelRot - flCurRot ) * m_flSpinStrength;
	}
}


//-----------------------------------------------------------------------------
// Stop Effect after Duration
//-----------------------------------------------------------------------------
class C_OP_StopAfterCPDuration : public CParticleOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_OP_StopAfterCPDuration );

	uint32 GetWrittenAttributes( void ) const
	{
		return 0;
	}

	uint32 GetReadAttributes( void ) const
	{
		return 0;
	}

	virtual uint64 GetReadControlPointMask() const
	{
		return AllShl1( m_nCP );
	}

	void InitParams( CParticleSystemDefinition *pDef )
	{
		m_nCP = clamp( m_nCP, -1, MAX_PARTICLE_CONTROL_POINTS );
		m_nCPField = clamp( m_nCPField, 0, 2 );
	}

	virtual void Operate( CParticleCollection *pParticles, float flStrength, void *pContext ) const;

	float m_flDuration;
	int m_nCP;
	int m_nCPField;
	bool m_bDestroyImmediately;
	bool m_bPlayEndCap;
};

DEFINE_PARTICLE_OPERATOR( C_OP_StopAfterCPDuration, "Stop Effect after Duration", OPERATOR_GENERIC );

BEGIN_PARTICLE_OPERATOR_UNPACK( C_OP_StopAfterCPDuration )
	DMXELEMENT_UNPACK_FIELD( "Duration at which to Stop", "1", float, m_flDuration )
	DMXELEMENT_UNPACK_FIELD( "Control Point to Scale Duration", "-1", int, m_nCP )
	DMXELEMENT_UNPACK_FIELD( "Control Point Field X/Y/Z", "0", int, m_nCPField )
	DMXELEMENT_UNPACK_FIELD( "Destroy All Particles Immediately", "0", bool, m_bDestroyImmediately )
	DMXELEMENT_UNPACK_FIELD( "Play End Cap Effect", "1", bool, m_bPlayEndCap )
END_PARTICLE_OPERATOR_UNPACK( C_OP_StopAfterCPDuration )

void C_OP_StopAfterCPDuration::Operate( CParticleCollection *pParticles, float flStrength, void *pContext ) const
{
	if ( !pParticles->m_bInEndCap )
	{
		float flDuration = m_flDuration;
		if ( m_nCP > -1 )
		{
			flDuration = pParticles->GetControlPointAtCurrentTime( m_nCP )[m_nCPField] * flDuration;
		}
		if ( pParticles->m_flCurTime > flDuration )
		{
			pParticles->StopEmission( false, m_bDestroyImmediately, true, m_bPlayEndCap );
		}
	}
}


//-----------------------------------------------------------------------------
// Rotation Orient Relative to CP
//-----------------------------------------------------------------------------
class C_OP_Orient2DRelToCP : public CParticleOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_OP_Orient2DRelToCP );

	uint32 GetWrittenAttributes( void ) const
	{
		return 1 << m_nFieldOutput;
	}

	uint32 GetReadAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_XYZ_MASK;
	}

	virtual uint64 GetReadControlPointMask() const
	{
		return 1ULL << m_nCP;
	}

	virtual void Operate( CParticleCollection *pParticles, float flStrength, void *pContext ) const;

	float m_flRotOffset;
	float m_flSpinStrength;
	int m_nCP;
	int m_nFieldOutput;
};

DEFINE_PARTICLE_OPERATOR( C_OP_Orient2DRelToCP, "Rotation Orient Relative to CP", OPERATOR_GENERIC );

BEGIN_PARTICLE_OPERATOR_UNPACK( C_OP_Orient2DRelToCP )
	DMXELEMENT_UNPACK_FIELD( "Rotation Offset", "0", float, m_flRotOffset )
	DMXELEMENT_UNPACK_FIELD( "Spin Strength", "1", float, m_flSpinStrength )
	DMXELEMENT_UNPACK_FIELD( "Control Point", "0", int, m_nCP )
	DMXELEMENT_UNPACK_FIELD_USERDATA( "rotation field", "4", int, m_nFieldOutput, "intchoice particlefield_rotation" )
END_PARTICLE_OPERATOR_UNPACK( C_OP_Orient2DRelToCP )

void C_OP_Orient2DRelToCP::Operate( CParticleCollection *pParticles, float flStrength, void *pContext ) const
{
	float flRotOffset = m_flRotOffset * ( M_PI / 180.0f );

	for( int i = 0; i < pParticles->m_nActiveParticles; ++i )
	{
		const float *xyz = pParticles->GetFloatAttributePtr( PARTICLE_ATTRIBUTE_XYZ, i );
		float *roll = pParticles->GetFloatAttributePtrForWrite( m_nFieldOutput, i );

		const Vector &vecCP = pParticles->GetControlPointAtCurrentTime( m_nCP );
		float flX = xyz[0];
		float flY = xyz[4];

		float flDirX = flX - vecCP.x;
		float flDirY = flY - vecCP.y;

		float flOOLen = FastRSqrt( flDirX * flDirX + flDirY * flDirY + FLT_EPSILON );
		flDirX *= flOOLen;
		flDirY *= flOOLen;

		float flCurRot = *roll;

		float flVelRot = atan2( flDirY, flDirX ) + M_PI;
		flVelRot += flRotOffset;

		*roll = flCurRot + ( flVelRot - flCurRot ) * m_flSpinStrength;
	}
}


//-----------------------------------------------------------------------------
// Set Control Point Rotation
//-----------------------------------------------------------------------------
class C_OP_SetControlPointRotation : public CParticleOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_OP_SetControlPointRotation );

	uint32 GetWrittenAttributes( void ) const
	{
		return 0;
	}

	uint32 GetReadAttributes( void ) const
	{
		return 0;
	}

	virtual uint64 GetReadControlPointMask() const
	{
		uint64 nStartMask = ( 1ULL << m_nCP );
		if ( m_nLocalCP != -1 )
		{
			nStartMask |= ( 1ULL << m_nLocalCP );
		}
		return nStartMask;
	}

	bool ShouldRunBeforeEmitters( void ) const
	{
		return true;
	}

	void InitParams( CParticleSystemDefinition *pDef )
	{
		VectorNormalize( m_vecRotAxis );
	}

	virtual void Operate( CParticleCollection *pParticles, float flStrength, void *pContext ) const;

	Vector m_vecRotAxis;
	float m_flRotRate;
	int m_nCP;
	int m_nLocalCP;
};

DEFINE_PARTICLE_OPERATOR( C_OP_SetControlPointRotation, "Set Control Point Rotation", OPERATOR_GENERIC );

BEGIN_PARTICLE_OPERATOR_UNPACK( C_OP_SetControlPointRotation )
	DMXELEMENT_UNPACK_FIELD( "Rotation Axis", "0 0 1", Vector, m_vecRotAxis )
	DMXELEMENT_UNPACK_FIELD( "Rotation Rate", "180", float, m_flRotRate )
	DMXELEMENT_UNPACK_FIELD( "Control Point", "0", int, m_nCP )
	DMXELEMENT_UNPACK_FIELD( "Local Space Control Point", "-1", int, m_nLocalCP )
END_PARTICLE_OPERATOR_UNPACK( C_OP_SetControlPointRotation )

void C_OP_SetControlPointRotation::Operate( CParticleCollection *pParticles, float flStrength, void *pContext ) const
{
	float flRotRate = m_flRotRate * pParticles->m_flDt;

	Vector vecRotAxis = m_vecRotAxis;
	if ( m_nLocalCP > -1 )
	{
		matrix3x4_t matLocalCP;
		pParticles->GetControlPointTransformAtCurrentTime( m_nLocalCP, &matLocalCP );
		VectorRotate( m_vecRotAxis, matLocalCP, vecRotAxis );
	}

	matrix3x4_t matCP;
	pParticles->GetControlPointTransformAtCurrentTime( m_nCP, &matCP );

	matrix3x4_t matRot;
	MatrixBuildRotationAboutAxis( vecRotAxis, flRotRate, matRot );

	ConcatTransforms( matCP, matRot, matCP );

	// Orthonormalise through a quaternion round trip
	Quaternion quatRot;
	MatrixQuaternion( matCP, quatRot );
	QuaternionMatrix( quatRot, matRot );

	Vector vecForward, vecRight, vecUp;
	MatrixVectors( matRot, &vecForward, &vecRight, &vecUp );
	pParticles->SetControlPointOrientation( m_nCP, vecForward, vecRight, vecUp );
}


//-----------------------------------------------------------------------------
// Rotate Vector Random
//-----------------------------------------------------------------------------
class C_OP_RotateVector : public CParticleOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_OP_RotateVector );

	uint32 GetWrittenAttributes( void ) const
	{
		return 1 << m_nFieldOutput;
	}

	uint32 GetReadAttributes( void ) const
	{
		return ( 1 << m_nFieldOutput ) | PARTICLE_ATTRIBUTE_PARTICLE_ID_MASK;
	}

	virtual uint64 GetReadControlPointMask() const
	{
		return 0;
	}

	void InitParams( CParticleSystemDefinition *pDef )
	{
		VectorNormalize( m_vecRotAxisMin );
		VectorNormalize( m_vecRotAxisMax );
	}

	virtual void Operate( CParticleCollection *pParticles, float flStrength, void *pContext ) const;

	int m_nFieldOutput;
	Vector m_vecRotAxisMin;
	Vector m_vecRotAxisMax;
	float m_flRotRateMin;
	float m_flRotRateMax;
	bool m_bNormalize;
};

DEFINE_PARTICLE_OPERATOR( C_OP_RotateVector, "Rotate Vector Random", OPERATOR_GENERIC );

BEGIN_PARTICLE_OPERATOR_UNPACK( C_OP_RotateVector )
	DMXELEMENT_UNPACK_FIELD_USERDATA( "output field", "21", int, m_nFieldOutput, "intchoice particlefield_vector" )
	DMXELEMENT_UNPACK_FIELD( "Rotation Axis Min", "0 0 1", Vector, m_vecRotAxisMin )
	DMXELEMENT_UNPACK_FIELD( "Rotation Axis Max", "0 0 1", Vector, m_vecRotAxisMax )
	DMXELEMENT_UNPACK_FIELD( "Rotation Rate Min", "180", float, m_flRotRateMin )
	DMXELEMENT_UNPACK_FIELD( "Rotation Rate Max", "180", float, m_flRotRateMax )
	DMXELEMENT_UNPACK_FIELD( "Normalize Ouput", "0", float, m_bNormalize )
END_PARTICLE_OPERATOR_UNPACK( C_OP_RotateVector )

void C_OP_RotateVector::Operate( CParticleCollection *pParticles, float flStrength, void *pContext ) const
{
	for ( int i = 0; i < pParticles->m_nActiveParticles; ++i )
	{
		const int *pParticleID = pParticles->GetIntAttributePtr( PARTICLE_ATTRIBUTE_PARTICLE_ID, i );
		float flRotRate = pParticles->RandomFloat( *pParticleID, m_flRotRateMin, m_flRotRateMax );

		flRotRate *= pParticles->m_flDt;
		Vector vecRotAxis;
		vecRotAxis.x = pParticles->RandomFloat( *pParticleID, m_vecRotAxisMin.x, m_vecRotAxisMax.x );
		vecRotAxis.y = pParticles->RandomFloat( *pParticleID, m_vecRotAxisMin.y, m_vecRotAxisMax.y );
		vecRotAxis.z = pParticles->RandomFloat( *pParticleID, m_vecRotAxisMin.z, m_vecRotAxisMax.z );
		VectorNormalize( vecRotAxis );

		float *pXYZ = pParticles->GetFloatAttributePtrForWrite( m_nFieldOutput, i );
		Vector vecInput( pXYZ[0], pXYZ[4], pXYZ[8] );

		matrix3x4_t matRot;
		MatrixBuildRotationAboutAxis( vecRotAxis, flRotRate, matRot );
		Vector vecOutput;
		VectorRotate( vecInput, matRot, vecOutput );

		if ( m_bNormalize )
			VectorNormalize( vecOutput );

		vecOutput.x = ( vecOutput.x - vecInput.x ) * flStrength + vecInput.x;
		vecOutput.y = ( vecOutput.y - vecInput.y ) * flStrength + vecInput.y;
		vecOutput.z = ( vecOutput.z - vecInput.z ) * flStrength + vecInput.z;

		pXYZ[0] = vecOutput.x;
		pXYZ[4] = vecOutput.y;
		pXYZ[8] = vecOutput.z;
	}
}


//-----------------------------------------------------------------------------
// Movement Max Velocity
//-----------------------------------------------------------------------------
class C_OP_MaxVelocity : public CParticleOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_OP_MaxVelocity );

	uint32 GetWrittenAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_XYZ_MASK | PARTICLE_ATTRIBUTE_PREV_XYZ_MASK;
	}

	uint32 GetReadAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_XYZ_MASK | PARTICLE_ATTRIBUTE_PREV_XYZ_MASK;
	}

	virtual uint64 GetReadControlPointMask() const
	{
		return AllShl1( m_nOverrideCP );
	}

	virtual void Operate( CParticleCollection *pParticles, float flStrength, void *pContext ) const;

	float m_flMaxVelocity;
	int m_nOverrideCP;
};

DEFINE_PARTICLE_OPERATOR( C_OP_MaxVelocity, "Movement Max Velocity", OPERATOR_GENERIC );

BEGIN_PARTICLE_OPERATOR_UNPACK( C_OP_MaxVelocity )
	DMXELEMENT_UNPACK_FIELD( "Maximum Velocity", "0", float, m_flMaxVelocity )
	DMXELEMENT_UNPACK_FIELD( "Override Max Velocity from this CP", "-1", int, m_nOverrideCP )
END_PARTICLE_OPERATOR_UNPACK( C_OP_MaxVelocity )

void C_OP_MaxVelocity::Operate( CParticleCollection *pParticles, float flStrength, void *pContext ) const
{
	float flMaxVelocity = m_flMaxVelocity;
	if ( m_nOverrideCP >= 0 )
	{
		Vector vecVelocityOverride = pParticles->GetControlPointAtCurrentTime( m_nOverrideCP );
		flMaxVelocity = vecVelocityOverride.x;
	}

	for ( int i = 0; i < pParticles->m_nActiveParticles; ++i )
	{
		float *xyz = pParticles->GetFloatAttributePtrForWrite( PARTICLE_ATTRIBUTE_XYZ, i );
		const float *xyz_prev = pParticles->GetFloatAttributePtr( PARTICLE_ATTRIBUTE_PREV_XYZ, i );

		Vector vecXYZ( xyz[0], xyz[4], xyz[8] );
		Vector vecPXYZ( xyz_prev[0], xyz_prev[4], xyz_prev[8] );
		Vector vecVelocityCur = vecXYZ - vecPXYZ;

		// NOTE: shipped code sums the squares z, y, x and reuses the sum for the fast normalize
		float flLenSqr = vecVelocityCur.z * vecVelocityCur.z + vecVelocityCur.y * vecVelocityCur.y + vecVelocityCur.x * vecVelocityCur.x;
		float flSpeed = sqrtf( flLenSqr );
		float flOOLen = FastRSqrt( flLenSqr + FLT_EPSILON );
		vecVelocityCur *= flOOLen;

		float flMaxVelocityNormalized = flMaxVelocity * pParticles->m_flDt;
		vecVelocityCur *= MIN( flSpeed, flMaxVelocityNormalized );
		vecXYZ = vecPXYZ + vecVelocityCur;

		xyz[0] = vecXYZ.x;
		xyz[4] = vecXYZ.y;
		xyz[8] = vecXYZ.z;
	}
}


//-----------------------------------------------------------------------------
// Movement Maintain Position Along Path
//-----------------------------------------------------------------------------

// The shipped binary sums the squared components in z, y, x order
static inline float LengthSqrZYX( const Vector &v )
{
	return ( v.z * v.z + v.y * v.y ) + v.x * v.x;
}

struct MaintainSequentialPathContext_t
{
	int m_nParticleCount;
	float m_flStep;
	int m_nCountAmount;
	int m_nUnused;
	Vector m_vecLastStart;
	Vector m_vecLastEnd;
};

class C_OP_MaintainSequentialPath : public CParticleOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_OP_MaintainSequentialPath );

	uint32 GetWrittenAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_XYZ_MASK | PARTICLE_ATTRIBUTE_PREV_XYZ_MASK;
	}

	uint32 GetReadAttributes( void ) const
	{
		return 0;
	}

	virtual uint64 GetReadControlPointMask() const
	{
		uint64 nStartMask = AllShl1( m_PathParams.m_nStartControlPointNumber ) - 1;
		uint64 nEndMask = AllShl1( m_PathParams.m_nEndControlPointNumber + 1 ) - 1;
		return nEndMask & (~nStartMask);
	}

	virtual void InitializeContextData( CParticleCollection *pParticles, void *pContext ) const
	{
		MaintainSequentialPathContext_t *pCtx = reinterpret_cast<MaintainSequentialPathContext_t *>( pContext );
		pCtx->m_nParticleCount = 0;
		if ( m_flNumToAssign > 1.0f )
		{
			pCtx->m_flStep = 1.0f / ( m_flNumToAssign - 1 );
		}
		else
		{
			pCtx->m_flStep = 0.0f;
		}
		pCtx->m_nCountAmount = 1;
		if ( m_flTolerance > 0.0f )
		{
			pCtx->m_vecLastStart = vec3_invalid;
			pCtx->m_vecLastEnd = vec3_invalid;
		}
	}

	void InitParams( CParticleSystemDefinition *pDef )
	{
		m_PathParams.ClampControlPointIndices();
	}

	size_t GetRequiredContextBytes( void ) const
	{
		return sizeof( MaintainSequentialPathContext_t );
	}

	virtual void Operate( CParticleCollection *pParticles, float flStrength, void *pContext ) const;

	float m_fMaxDistance;
	float m_flNumToAssign;
	float m_flCohesionStrength;
	float m_flTolerance;
	bool m_bLoop;
	bool m_bUseParticleCount;
	struct CPathParameters m_PathParams;
};

DEFINE_PARTICLE_OPERATOR( C_OP_MaintainSequentialPath, "Movement Maintain Position Along Path", OPERATOR_GENERIC );

BEGIN_PARTICLE_OPERATOR_UNPACK( C_OP_MaintainSequentialPath )
	DMXELEMENT_UNPACK_FIELD( "maximum distance", "0", float, m_fMaxDistance )
	DMXELEMENT_UNPACK_FIELD( "bulge", "0", float, m_PathParams.m_flBulge )
	DMXELEMENT_UNPACK_FIELD( "start control point number", "0", int, m_PathParams.m_nStartControlPointNumber )
	DMXELEMENT_UNPACK_FIELD( "end control point number", "0", int, m_PathParams.m_nEndControlPointNumber )
	DMXELEMENT_UNPACK_FIELD( "bulge control 0=random 1=orientation of start pnt 2=orientation of end point", "0", int, m_PathParams.m_nBulgeControl )
	DMXELEMENT_UNPACK_FIELD( "mid point position", "0.5", float, m_PathParams.m_flMidPoint )
	DMXELEMENT_UNPACK_FIELD( "particles to map from start to end", "100", float, m_flNumToAssign )
	DMXELEMENT_UNPACK_FIELD( "restart behavior (0 = bounce, 1 = loop )", "1", bool, m_bLoop )
	DMXELEMENT_UNPACK_FIELD( "cohesion strength", "1", float, m_flCohesionStrength )
	DMXELEMENT_UNPACK_FIELD( "use existing particle count", "0", bool, m_bUseParticleCount )
	DMXELEMENT_UNPACK_FIELD( "control point movement tolerance", "0", float, m_flTolerance )
END_PARTICLE_OPERATOR_UNPACK( C_OP_MaintainSequentialPath )

void C_OP_MaintainSequentialPath::Operate( CParticleCollection *pParticles, float flStrength, void *pContext ) const
{
	MaintainSequentialPathContext_t *pCtx = reinterpret_cast<MaintainSequentialPathContext_t *>( pContext );

	if ( m_flTolerance > 0.0f )
	{
		// Only update when either end of the path has moved further than the tolerance
		if ( ( sqrtf( LengthSqrZYX( pParticles->GetControlPointAtCurrentTime( m_PathParams.m_nStartControlPointNumber ) - pCtx->m_vecLastStart ) ) < m_flTolerance ) &&
			 ( sqrtf( LengthSqrZYX( pParticles->GetControlPointAtCurrentTime( m_PathParams.m_nEndControlPointNumber ) - pCtx->m_vecLastEnd ) ) < m_flTolerance ) )
			return;

		pCtx->m_vecLastStart = pParticles->GetControlPointAtCurrentTime( m_PathParams.m_nStartControlPointNumber );
		pCtx->m_vecLastEnd = pParticles->GetControlPointAtCurrentTime( m_PathParams.m_nEndControlPointNumber );
	}

	float fl_Cohesion = ( 1.0f - m_flCohesionStrength );
	float flNumToAssign = m_flNumToAssign;
	if ( m_bUseParticleCount )
	{
		flNumToAssign = pParticles->m_nActiveParticles;
		if ( flNumToAssign > 1.0f )
		{
			pCtx->m_flStep = 1.0f / ( flNumToAssign - 1 );
		}
		else
		{
			pCtx->m_flStep = 0.0f;
		}
	}

	for ( int i = 0; i < pParticles->m_nActiveParticles; ++i )
	{
		float *xyz = pParticles->GetFloatAttributePtrForWrite( PARTICLE_ATTRIBUTE_XYZ, i );
		float *pxyz = pParticles->GetFloatAttributePtrForWrite( PARTICLE_ATTRIBUTE_PREV_XYZ, i );

		Vector StartPnt, MidP, EndPnt;
		pParticles->CalculatePathValues( m_PathParams, pParticles->m_flCurTime, &StartPnt, &MidP, &EndPnt );

		if ( ( pCtx->m_nParticleCount >= flNumToAssign ) || ( pCtx->m_nParticleCount < 0 ) )
		{
			if ( m_bLoop )
			{
				pCtx->m_nParticleCount = 0;
			}
			else
			{
				pCtx->m_nCountAmount *= -1;
				pCtx->m_nParticleCount = MIN( pCtx->m_nParticleCount, flNumToAssign - 1 );
				pCtx->m_nParticleCount = MAX( pCtx->m_nParticleCount, 1 );
			}
		}

		float t = pCtx->m_nParticleCount * pCtx->m_flStep;

		// form delta terms needed for quadratic bezier
		Vector Delta0 = MidP - StartPnt;
		Vector Delta1 = EndPnt - MidP;

		Vector L0 = StartPnt + t * Delta0;
		Vector L1 = MidP + t * Delta1;

		Vector Pnt = L0 + ( L1 - L0 ) * t;

		// Allow an offset from the path position
		Vector vecXYZ( xyz[0], xyz[4], xyz[8] );
		Vector vecPXYZ( pxyz[0], pxyz[4], pxyz[8] );
		vecXYZ -= Pnt;
		vecPXYZ -= Pnt;

		float flXYZLenSqr = LengthSqrZYX( vecXYZ );
		float flXYZOffset = MIN( sqrtf( flXYZLenSqr ), m_fMaxDistance );
		float flPXYZLenSqr = LengthSqrZYX( vecPXYZ );
		float flPXYZOffset = MIN( sqrtf( flPXYZLenSqr ), m_fMaxDistance );

		vecXYZ *= FastRSqrt( flXYZLenSqr + FLT_EPSILON );
		vecXYZ *= flXYZOffset * fl_Cohesion;
		vecPXYZ *= FastRSqrt( flPXYZLenSqr + FLT_EPSILON );
		vecPXYZ *= flPXYZOffset * fl_Cohesion;

		vecXYZ += Pnt;
		vecPXYZ += Pnt;

		xyz[0] = vecXYZ.x;
		xyz[4] = vecXYZ.y;
		xyz[8] = vecXYZ.z;
		pxyz[0] = vecPXYZ.x;
		pxyz[4] = vecPXYZ.y;
		pxyz[8] = vecPXYZ.z;

		pCtx->m_nParticleCount += pCtx->m_nCountAmount;
	}
}


//-----------------------------------------------------------------------------
// Remap Dot Product to Scalar
//-----------------------------------------------------------------------------
class C_OP_RemapDotProductToScalar : public CParticleOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_OP_RemapDotProductToScalar );

	uint32 GetWrittenAttributes( void ) const
	{
		return 1 << m_nFieldOutput;
	}

	uint32 GetReadAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_XYZ_MASK | PARTICLE_ATTRIBUTE_PREV_XYZ_MASK;
	}

	uint32 GetReadInitialAttributes( void ) const
	{
		if ( m_bScaleInitialRange )
			return 1 << m_nFieldOutput;
		return 0;
	}

	virtual uint32 GetFilter( void ) const
	{
		return FILTER_PARAMETER_REMAPPING_MASK;
	}

	virtual uint64 GetReadControlPointMask() const
	{
		return AllShl1( m_nInputCP1 ) | AllShl1( m_nInputCP2 );
	}

	virtual void Operate( CParticleCollection *pParticles, float flStrength, void *pContext ) const;

	int m_nInputCP1;
	int m_nInputCP2;
	int m_nFieldOutput;
	float m_flInputMin;
	float m_flInputMax;
	float m_flOutputMin;
	float m_flOutputMax;
	bool m_bUseParticleVelocity;
	bool m_bScaleInitialRange;
	bool m_bScaleCurrent;
	bool m_bActiveRange;
};

DEFINE_PARTICLE_OPERATOR( C_OP_RemapDotProductToScalar, "Remap Dot Product to Scalar", OPERATOR_GENERIC );

BEGIN_PARTICLE_OPERATOR_UNPACK( C_OP_RemapDotProductToScalar )
	DMXELEMENT_UNPACK_FIELD( "use particle velocity for first input", "0", bool, m_bUseParticleVelocity )
	DMXELEMENT_UNPACK_FIELD( "first input control point", "0", int, m_nInputCP1 )
	DMXELEMENT_UNPACK_FIELD( "second input control point", "0", int, m_nInputCP2 )
	DMXELEMENT_UNPACK_FIELD( "input minimum (-1 to 1)", "0", float, m_flInputMin )
	DMXELEMENT_UNPACK_FIELD( "input maximum (-1 to 1)", "1", float, m_flInputMax )
	DMXELEMENT_UNPACK_FIELD_USERDATA( "output field", "3", int, m_nFieldOutput, "intchoice particlefield_scalar" )
	DMXELEMENT_UNPACK_FIELD( "output minimum", "0", float, m_flOutputMin )
	DMXELEMENT_UNPACK_FIELD( "output maximum", "1", float, m_flOutputMax )
	DMXELEMENT_UNPACK_FIELD( "output is scalar of initial random range", "0", bool, m_bScaleInitialRange )
	DMXELEMENT_UNPACK_FIELD( "output is scalar of current value", "0", bool, m_bScaleCurrent )
	DMXELEMENT_UNPACK_FIELD( "only active within specified input range", "0", bool, m_bActiveRange )
END_PARTICLE_OPERATOR_UNPACK( C_OP_RemapDotProductToScalar )

// The shipped binary sums the dot product terms in z, y, x order
static inline float DotProductZYX( const Vector &a, const Vector &b )
{
	return ( a.z * b.z + a.y * b.y ) + a.x * b.x;
}

void C_OP_RemapDotProductToScalar::Operate( CParticleCollection *pParticles, float flStrength, void *pContext ) const
{
	// clamp the result to 0 and 1 if it's alpha
	float flMin = m_flOutputMin;
	float flMax = m_flOutputMax;
	if ( ATTRIBUTES_WHICH_ARE_0_TO_1 & ( 1 << m_nFieldOutput ) )
	{
		flMin = clamp( m_flOutputMin, 0.0f, 1.0f );
		flMax = clamp( m_flOutputMax, 0.0f, 1.0f );
	}

	CParticleSIMDTransformation pXForm1;
	CParticleSIMDTransformation pXForm2;
	pParticles->GetControlPointTransformAtTime( m_nInputCP1, pParticles->m_flCurTime, &pXForm1 );
	pParticles->GetControlPointTransformAtTime( m_nInputCP2, pParticles->m_flCurTime, &pXForm2 );

	Vector vecInput1 = pXForm1.m_v4Fwd.Vec( 0 );
	Vector vecInput2 = pXForm2.m_v4Fwd.Vec( 0 );

	float flInput = DotProductZYX( vecInput2, vecInput1 );

	// only use within start/end time frame and, if set, active input range
	if ( m_bActiveRange && !m_bUseParticleVelocity && ( ( flInput < m_flInputMin ) || ( flInput > m_flInputMax ) ) )
		return;

	for ( int i = 0; i < pParticles->m_nActiveParticles; ++i )
	{
		if ( m_bUseParticleVelocity )
		{
			const float *xyz = pParticles->GetFloatAttributePtr( PARTICLE_ATTRIBUTE_XYZ, i );
			const float *xyz_prev = pParticles->GetFloatAttributePtr( PARTICLE_ATTRIBUTE_PREV_XYZ, i );

			Vector vecVel( xyz[0] - xyz_prev[0], xyz[4] - xyz_prev[4], xyz[8] - xyz_prev[8] );
			vecVel *= FastRSqrt( LengthSqrZYX( vecVel ) + FLT_EPSILON );

			// NOTE: shipped code declares a new flInput here, so the velocity based dot product
			// only gates the active range and the remap below still uses the control point input
			float flInput = DotProductZYX( vecInput2, vecVel );
			if ( m_bActiveRange && ( ( flInput < m_flInputMin ) || ( flInput > m_flInputMax ) ) )
				continue;
		}

		float *pOutput = pParticles->GetFloatAttributePtrForWrite( m_nFieldOutput, i );
		float flOutput = RemapValClamped( flInput, m_flInputMin, m_flInputMax, flMin, flMax );
		if ( m_bScaleInitialRange )
		{
			const float *pInitialOutput = pParticles->GetInitialFloatAttributePtr( m_nFieldOutput, i );
			flOutput = *pInitialOutput * flOutput;
		}
		if ( m_bScaleCurrent )
		{
			flOutput = *pOutput * flOutput;
		}
		if ( ATTRIBUTES_WHICH_ARE_INTS & ( 1 << m_nFieldOutput ) )
		{
			*pOutput = int ( flOutput );
		}
		else
		{
			*pOutput = flOutput;
		}
	}
}


//-----------------------------------------------------------------------------
// Remap Control Point to Scalar
//-----------------------------------------------------------------------------
class C_OP_RemapCPtoScalar : public CParticleOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_OP_RemapCPtoScalar );

	uint32 GetWrittenAttributes( void ) const
	{
		return 1 << m_nFieldOutput;
	}

	uint32 GetReadAttributes( void ) const
	{
		return 0;
	}

	uint32 GetReadInitialAttributes( void ) const
	{
		if ( m_bScaleInitialRange )
			return 1 << m_nFieldOutput;
		return 0;
	}

	virtual uint32 GetFilter( void ) const
	{
		return FILTER_PARAMETER_REMAPPING_MASK;
	}

	virtual uint64 GetReadControlPointMask() const
	{
		return AllShl1( m_nCPInput );
	}

	void InitParams( CParticleSystemDefinition *pDef )
	{
		m_nField = clamp( m_nField, 0, 2 );
	}

	virtual void Operate( CParticleCollection *pParticles, float flStrength, void *pContext ) const;

	int m_nCPInput;
	int m_nFieldOutput;
	int m_nField;
	float m_flInputMin;
	float m_flInputMax;
	float m_flOutputMin;
	float m_flOutputMax;
	float m_flStartTime;
	float m_flEndTime;
	bool m_bScaleInitialRange;
	bool m_bScaleCurrent;
};

DEFINE_PARTICLE_OPERATOR( C_OP_RemapCPtoScalar, "Remap Control Point to Scalar", OPERATOR_GENERIC );

BEGIN_PARTICLE_OPERATOR_UNPACK( C_OP_RemapCPtoScalar )
	DMXELEMENT_UNPACK_FIELD( "emitter lifetime start time (seconds)", "-1", float, m_flStartTime )
	DMXELEMENT_UNPACK_FIELD( "emitter lifetime end time (seconds)", "-1", float, m_flEndTime )
	DMXELEMENT_UNPACK_FIELD( "input control point number", "0", int, m_nCPInput )
	DMXELEMENT_UNPACK_FIELD( "input minimum", "0", float, m_flInputMin )
	DMXELEMENT_UNPACK_FIELD( "input maximum", "1", float, m_flInputMax )
	DMXELEMENT_UNPACK_FIELD( "input field 0-2 X/Y/Z", "0", int, m_nField )
	DMXELEMENT_UNPACK_FIELD_USERDATA( "output field", "3", int, m_nFieldOutput, "intchoice particlefield_scalar" )
	DMXELEMENT_UNPACK_FIELD( "output minimum", "0", float, m_flOutputMin )
	DMXELEMENT_UNPACK_FIELD( "output maximum", "1", float, m_flOutputMax )
	DMXELEMENT_UNPACK_FIELD( "output is scalar of initial random range", "0", bool, m_bScaleInitialRange )
	DMXELEMENT_UNPACK_FIELD( "output is scalar of current value", "0", bool, m_bScaleCurrent )
END_PARTICLE_OPERATOR_UNPACK( C_OP_RemapCPtoScalar )

void C_OP_RemapCPtoScalar::Operate( CParticleCollection *pParticles, float flStrength, void *pContext ) const
{
	// clamp the result to 0 and 1 if it's alpha
	float flMin = m_flOutputMin;
	float flMax = m_flOutputMax;
	if ( ATTRIBUTES_WHICH_ARE_0_TO_1 & ( 1 << m_nFieldOutput ) )
	{
		flMin = clamp( m_flOutputMin, 0.0f, 1.0f );
		flMax = clamp( m_flOutputMax, 0.0f, 1.0f );
	}

	Vector vecControlPoint = pParticles->GetControlPointAtCurrentTime( m_nCPInput );

	float flInput = vecControlPoint[m_nField];

	for ( int i = 0; i < pParticles->m_nActiveParticles; ++i )
	{
		const float *pCreationTime = pParticles->GetFloatAttributePtr( PARTICLE_ATTRIBUTE_CREATION_TIME, i );
		// using raw creation time to map to emitter lifespan
		float flLifeTime = *pCreationTime;

		// only use within start/end time frame
		if ( ( ( flLifeTime < m_flStartTime ) || ( flLifeTime >= m_flEndTime ) ) && ( ( m_flStartTime != -1.0f ) && ( m_flEndTime != -1.0f ) ) )
			continue;

		float *pOutput = pParticles->GetFloatAttributePtrForWrite( m_nFieldOutput, i );
		float flOutput = RemapValClamped( flInput, m_flInputMin, m_flInputMax, flMin, flMax );
		if ( m_bScaleInitialRange )
		{
			const float *pInitialOutput = pParticles->GetInitialFloatAttributePtr( m_nFieldOutput, i );
			flOutput = *pInitialOutput * flOutput;
		}
		if ( m_bScaleCurrent )
		{
			flOutput = *pOutput * flOutput;
		}
		if ( ATTRIBUTES_WHICH_ARE_INTS & ( 1 << m_nFieldOutput ) )
		{
			*pOutput = int ( flOutput );
		}
		else
		{
			*pOutput = flOutput;
		}
	}
}


//-----------------------------------------------------------------------------
// Normal Lock to Control Point
//-----------------------------------------------------------------------------
class C_OP_NormalLock : public CParticleOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_OP_NormalLock );

	uint32 GetWrittenAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_NORMAL_MASK;
	}

	uint32 GetReadAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_CREATION_TIME_MASK;
	}

	virtual uint64 GetReadControlPointMask() const
	{
		return AllShl1( m_nControlPointNumber );
	}

	void InitParams( CParticleSystemDefinition *pDef )
	{
		m_nControlPointNumber = MAX( 0, MIN( MAX_PARTICLE_CONTROL_POINTS-1, m_nControlPointNumber ) );
	}

	size_t GetRequiredContextBytes( void ) const
	{
		return sizeof( matrix3x4_t );
	}

	virtual void InitializeContextData( CParticleCollection *pParticles, void *pContext ) const
	{
		matrix3x4_t *pMat = reinterpret_cast<matrix3x4_t *>( pContext );
		pParticles->GetControlPointTransformAtTime( m_nControlPointNumber, pParticles->m_flCurTime, pMat );
	}

	virtual void Operate( CParticleCollection *pParticles, float flStrength, void *pContext ) const;

	int m_nControlPointNumber;
};

DEFINE_PARTICLE_OPERATOR( C_OP_NormalLock, "Normal Lock to Control Point", OPERATOR_GENERIC );

BEGIN_PARTICLE_OPERATOR_UNPACK( C_OP_NormalLock )
	DMXELEMENT_UNPACK_FIELD( "control_point_number", "0", int, m_nControlPointNumber )
END_PARTICLE_OPERATOR_UNPACK( C_OP_NormalLock )

void C_OP_NormalLock::Operate( CParticleCollection *pParticles, float flStrength, void *pContext ) const
{
	matrix3x4_t *pPrevMat = reinterpret_cast<matrix3x4_t *>( pContext );

	matrix3x4_t matCurrentTransform;
	pParticles->GetControlPointTransformAtTime( m_nControlPointNumber, pParticles->m_flCurTime, &matCurrentTransform );

	matrix3x4_t matPrev;
	MatrixInvert( *pPrevMat, matPrev );
	matrix3x4_t matTransformLock;
	ConcatTransforms( matCurrentTransform, matPrev, matTransformLock );

	for ( int i = 0; i < pParticles->m_nActiveParticles; ++i )
	{
		const float *pCreationTime = pParticles->GetFloatAttributePtr( PARTICLE_ATTRIBUTE_CREATION_TIME, i );
		// particles created this frame only get part of the frame's movement
		float flAge = pParticles->m_flCurTime - *pCreationTime;
		float flCreationFrameBias = ( 1.0f / pParticles->m_flDt ) * MIN( flAge, pParticles->m_flDt );

		float *pNormal = pParticles->GetFloatAttributePtrForWrite( PARTICLE_ATTRIBUTE_NORMAL, i );
		Vector vecNormalOld( pNormal[0], pNormal[4], pNormal[8] );
		Vector vecNormalNew;
		VectorRotate( vecNormalOld, matTransformLock, vecNormalNew );

		vecNormalNew.x = ( vecNormalNew.x - vecNormalOld.x ) * flCreationFrameBias + vecNormalOld.x;
		vecNormalNew.y = ( vecNormalNew.y - vecNormalOld.y ) * flCreationFrameBias + vecNormalOld.y;
		vecNormalNew.z = ( vecNormalNew.z - vecNormalOld.z ) * flCreationFrameBias + vecNormalOld.z;

		pNormal[0] = vecNormalNew.x;
		pNormal[4] = vecNormalNew.y;
		pNormal[8] = vecNormalNew.z;
	}

	*pPrevMat = matCurrentTransform;
}


//-----------------------------------------------------------------------------
// Set Control Point to Impact Point
//-----------------------------------------------------------------------------
class C_OP_SetControlPointToImpactPoint : public CParticleOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_OP_SetControlPointToImpactPoint );

	uint32 GetWrittenAttributes( void ) const
	{
		return 0;
	}

	uint32 GetReadAttributes( void ) const
	{
		return 0;
	}

	bool ShouldRunBeforeEmitters( void ) const
	{
		return true;
	}

	void InitParams( CParticleSystemDefinition *pDef )
	{
		m_nCollisionGroupNumber = g_pParticleSystemMgr->Query()->GetCollisionGroupFromName( m_CollisionGroupName );
		m_nCPIn = MAX( 0, MIN( MAX_PARTICLE_CONTROL_POINTS-1, m_nCPIn ) );
		m_nCPOut = MAX( 0, MIN( MAX_PARTICLE_CONTROL_POINTS-1, m_nCPOut ) );
	}

	size_t GetRequiredContextBytes( void ) const
	{
		return sizeof( float );
	}

	virtual void InitializeContextData( CParticleCollection *pParticles, void *pContext ) const
	{
		float *pflLastUpdate = reinterpret_cast<float *>( pContext );
		*pflLastUpdate = -m_flUpdateRate;
	}

	virtual void Operate( CParticleCollection *pParticles, float flStrength, void *pContext ) const;

	int m_nCPOut;
	int m_nCPIn;
	int m_nCollisionGroupNumber;
	float m_flUpdateRate;
	float m_flTraceLength;
	Vector m_vecTraceDir;
	char m_CollisionGroupName[128];
};

DEFINE_PARTICLE_OPERATOR( C_OP_SetControlPointToImpactPoint, "Set Control Point to Impact Point", OPERATOR_GENERIC );

BEGIN_PARTICLE_OPERATOR_UNPACK( C_OP_SetControlPointToImpactPoint )
	DMXELEMENT_UNPACK_FIELD( "Control Point to Set", "1", int, m_nCPOut )
	DMXELEMENT_UNPACK_FIELD( "Control Point to Trace From", "1", int, m_nCPIn )
	DMXELEMENT_UNPACK_FIELD( "Trace Direction Override", "0 0 0", Vector, m_vecTraceDir )
	DMXELEMENT_UNPACK_FIELD( "Trace Update Rate", "0.5", float, m_flUpdateRate )
	DMXELEMENT_UNPACK_FIELD( "Max Trace Length", "1024", float, m_flTraceLength )
	DMXELEMENT_UNPACK_FIELD_STRING( "trace collision group", "NONE", m_CollisionGroupName )
END_PARTICLE_OPERATOR_UNPACK( C_OP_SetControlPointToImpactPoint )

void C_OP_SetControlPointToImpactPoint::Operate( CParticleCollection *pParticles, float flStrength, void *pContext ) const
{
	float *pflLastUpdate = reinterpret_cast<float *>( pContext );
	float flCurTime = pParticles->m_flCurTime;
	if ( flCurTime >= *pflLastUpdate )
	{
		Vector pForward = m_vecTraceDir;
		Vector pRight, pUp;
		// use the CP's forward direction unless a trace direction override is set
		if ( m_vecTraceDir == vec3_origin )
		{
			pParticles->GetControlPointOrientationAtTime( m_nCPIn, flCurTime, &pForward, &pRight, &pUp );
		}

		Vector vecStartPnt = pParticles->GetControlPointAtCurrentTime( m_nCPIn );
		Vector vecEndPnt = ( pForward * m_flTraceLength ) + vecStartPnt;

		CBaseTrace tr;
		g_pParticleSystemMgr->Query()->TraceLine( vecStartPnt, vecEndPnt, MASK_ALL, NULL, m_nCollisionGroupNumber, &tr );

		Vector vecForward = tr.plane.normal;
		Vector vecRight, vecUp;
		VectorVectors( vecForward, vecRight, vecUp );
		pParticles->SetControlPoint( m_nCPOut, tr.endpos );
		pParticles->SetControlPointOrientation( m_nCPOut, vecForward, vecRight, vecUp );
		*pflLastUpdate = m_flUpdateRate + pParticles->m_flCurTime;
	}
}


//-----------------------------------------------------------------------------
// Remap Control Point to Vector
//-----------------------------------------------------------------------------
class C_OP_RemapCPtoVector : public CParticleOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_OP_RemapCPtoVector );

	uint32 GetWrittenAttributes( void ) const
	{
		return ( 1 << m_nFieldOutput ) | PARTICLE_ATTRIBUTE_PREV_XYZ_MASK;
	}

	uint32 GetReadAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_CREATION_TIME_MASK;
	}

	uint32 GetReadInitialAttributes( void ) const
	{
		if ( m_bScaleInitialRange )
			return 1 << m_nFieldOutput;
		return 0;
	}

	virtual uint64 GetReadControlPointMask() const
	{
		uint64 nMask = AllShl1( m_nCPInput );
		if ( m_nLocalSpaceCP != -1 )
		{
			nMask |= AllShl1( m_nLocalSpaceCP );
		}
		return nMask;
	}

	virtual void Operate( CParticleCollection *pParticles, float flStrength, void *pContext ) const;

	int m_nCPInput;
	int m_nFieldOutput;
	int m_nField;
	int m_nLocalSpaceCP;
	Vector m_vInputMin;
	Vector m_vInputMax;
	Vector m_vOutputMin;
	Vector m_vOutputMax;
	float m_flStartTime;
	float m_flEndTime;
	bool m_bScaleInitialRange;
	bool m_bScaleCurrent;
	bool m_bOffset;
	bool m_bAccelerate;
};

DEFINE_PARTICLE_OPERATOR( C_OP_RemapCPtoVector, "Remap Control Point to Vector", OPERATOR_GENERIC );

BEGIN_PARTICLE_OPERATOR_UNPACK( C_OP_RemapCPtoVector )
	DMXELEMENT_UNPACK_FIELD( "emitter lifetime start time (seconds)", "-1", float, m_flStartTime )
	DMXELEMENT_UNPACK_FIELD( "emitter lifetime end time (seconds)", "-1", float, m_flEndTime )
	DMXELEMENT_UNPACK_FIELD( "input control point number", "0", int, m_nCPInput )
	DMXELEMENT_UNPACK_FIELD( "input minimum", "0 0 0", Vector, m_vInputMin )
	DMXELEMENT_UNPACK_FIELD( "input maximum", "0 0 0", Vector, m_vInputMax )
	DMXELEMENT_UNPACK_FIELD_USERDATA( "output field", "0", int, m_nFieldOutput, "intchoice particlefield_vector" )
	DMXELEMENT_UNPACK_FIELD( "output minimum", "0 0 0", Vector, m_vOutputMin )
	DMXELEMENT_UNPACK_FIELD( "output maximum", "0 0 0", Vector, m_vOutputMax )
	DMXELEMENT_UNPACK_FIELD( "output is scalar of initial random range", "0", bool, m_bScaleInitialRange )
	DMXELEMENT_UNPACK_FIELD( "output is scalar of current value", "0", bool, m_bScaleCurrent )
	DMXELEMENT_UNPACK_FIELD( "offset position", "0", bool, m_bOffset )
	DMXELEMENT_UNPACK_FIELD( "accelerate position", "0", bool, m_bAccelerate )
	DMXELEMENT_UNPACK_FIELD( "local space CP", "-1", int, m_nLocalSpaceCP )
END_PARTICLE_OPERATOR_UNPACK( C_OP_RemapCPtoVector )

void C_OP_RemapCPtoVector::Operate( CParticleCollection *pParticles, float flStrength, void *pContext ) const
{
	Vector vecControlPoint;
	pParticles->GetControlPointAtTime( m_nCPInput, pParticles->m_flCurTime, &vecControlPoint );

	Vector vOutputMinLocal = m_vOutputMin;
	Vector vOutputMaxLocal = m_vOutputMax;
	if ( m_nLocalSpaceCP != -1 )
	{
		matrix3x4_t mat;
		pParticles->GetControlPointTransformAtTime( m_nLocalSpaceCP, pParticles->m_flCurTime, &mat );
		Vector vecTransformLocal = vec3_origin;
		VectorRotate( vOutputMinLocal, mat, vecTransformLocal );
		vOutputMinLocal = vecTransformLocal;
		VectorRotate( vOutputMaxLocal, mat, vecTransformLocal );
		vOutputMaxLocal = vecTransformLocal;
	}

	for ( int i = 0; i < pParticles->m_nActiveParticles; ++i )
	{
		const float *pCreationTime = pParticles->GetFloatAttributePtr( PARTICLE_ATTRIBUTE_CREATION_TIME, i );

		// only use within start/end time frame
		if ( ( ( *pCreationTime < m_flStartTime ) || ( *pCreationTime >= m_flEndTime ) ) && ( ( m_flStartTime != -1.0f ) && ( m_flEndTime != -1.0f ) ) )
			continue;

		float *pOutput = pParticles->GetFloatAttributePtrForWrite( m_nFieldOutput, i );

		Vector vOutput;
		vOutput.x = RemapValClamped( vecControlPoint.x, m_vInputMin.x, m_vInputMax.x, vOutputMinLocal.x, vOutputMaxLocal.x );
		vOutput.y = RemapValClamped( vecControlPoint.y, m_vInputMin.y, m_vInputMax.y, vOutputMinLocal.y, vOutputMaxLocal.y );
		vOutput.z = RemapValClamped( vecControlPoint.z, m_vInputMin.z, m_vInputMax.z, vOutputMinLocal.z, vOutputMaxLocal.z );

		if ( m_bScaleInitialRange )
		{
			const float *pInitialOutput = pParticles->GetInitialFloatAttributePtr( m_nFieldOutput, i );
			Vector vOrgValue;
			SetVectorFromAttribute( vOrgValue, pInitialOutput );
			vOutput *= vOrgValue;
		}
		if ( m_bScaleCurrent )
		{
			// NOTE: shipped code scales all three components by the first component of the current value
			vOutput *= *pOutput;
		}

		// Unlike the initializer, the operator tests the field's mask bit correctly
		if ( ATTRIBUTES_WHICH_ARE_COLOR_AND_OPACITY & ( 1 << m_nFieldOutput ) )
		{
			pOutput[0] = MAX( 0.0f, MIN( vOutput.x, 1.0f ) );
			pOutput[4] = MAX( 0.0f, MIN( vOutput.y, 1.0f ) );
			pOutput[8] = MAX( 0.0f, MIN( vOutput.z, 1.0f ) );
		}
		else
		{
			float *pXYZ_Prev = pParticles->GetFloatAttributePtrForWrite( PARTICLE_ATTRIBUTE_PREV_XYZ, i );
			if ( m_bAccelerate )
			{
				if ( m_bOffset )
				{
					Vector vOrgValue;
					SetVectorFromAttribute( vOrgValue, pOutput );
					Vector vXYZPrev;
					SetVectorFromAttribute( vXYZPrev, pXYZ_Prev );
					vOutput += vOrgValue;
					vXYZPrev += vOutput;
					vOutput += vOutput * pParticles->m_flDt;
					SetVectorAttribute( pOutput, vOutput );
					SetVectorAttribute( pXYZ_Prev, vXYZPrev );
				}
				else
				{
					vOutput *= pParticles->m_flDt;
					SetVectorAttribute( pOutput, vOutput );
				}
			}
			else
			{
				// NOTE: shipped code writes the previous position even without offset,
				// in which case it receives the remapped output itself
				Vector vXYZPrev = vOutput;
				if ( m_bOffset )
				{
					Vector vOrgValue;
					SetVectorFromAttribute( vOrgValue, pOutput );
					SetVectorFromAttribute( vXYZPrev, pXYZ_Prev );
					vOutput += vOrgValue;
					vXYZPrev += vOutput;
				}
				SetVectorAttribute( pOutput, vOutput );
				SetVectorAttribute( pXYZ_Prev, vXYZPrev );
			}
		}
	}
}


//-----------------------------------------------------------------------------
// Remap Velocity to Vector
//-----------------------------------------------------------------------------
class C_OP_RemapVelocityToVector : public CParticleOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_OP_RemapVelocityToVector );

	uint32 GetWrittenAttributes( void ) const
	{
		return 1 << m_nFieldOutput;
	}

	uint32 GetReadAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_XYZ_MASK | PARTICLE_ATTRIBUTE_LIFE_DURATION_MASK;
	}

	virtual void Operate( CParticleCollection *pParticles, float flStrength, void *pContext ) const;

	int m_nFieldOutput;
	float m_flScale;
	bool m_bNormalize;
};

DEFINE_PARTICLE_OPERATOR( C_OP_RemapVelocityToVector, "Remap Velocity to Vector", OPERATOR_GENERIC );

BEGIN_PARTICLE_OPERATOR_UNPACK( C_OP_RemapVelocityToVector )
	DMXELEMENT_UNPACK_FIELD_USERDATA( "output field", "0", int, m_nFieldOutput, "intchoice particlefield_vector" )
	DMXELEMENT_UNPACK_FIELD( "normalize", "0", bool, m_bNormalize )
	DMXELEMENT_UNPACK_FIELD( "scale factor", "1", float, m_flScale )
END_PARTICLE_OPERATOR_UNPACK( C_OP_RemapVelocityToVector )

void C_OP_RemapVelocityToVector::Operate( CParticleCollection *pParticles, float flStrength, void *pContext ) const
{
	C4VAttributeIterator pPrevXYZ( PARTICLE_ATTRIBUTE_PREV_XYZ, pParticles );
	C4VAttributeIterator pXYZ( PARTICLE_ATTRIBUTE_XYZ, pParticles );
	C4VAttributeWriteIterator pOutField( m_nFieldOutput, pParticles );

	int nCtr = pParticles->m_nPaddedActiveParticles;
	if ( m_bNormalize )
	{
		fltx4 fl4Scale = ReplicateX4( m_flScale );
		do
		{
			FourVectors v4Vel = *pXYZ;
			v4Vel -= *pPrevXYZ;
			v4Vel.VectorNormalize();
			v4Vel *= fl4Scale;
			*pOutField = v4Vel;
			++pOutField;
			++pXYZ;
			++pPrevXYZ;
		} while ( --nCtr );
	}
	else
	{
		// NOTE: shipped code divides by the previous frame's dt
		fltx4 fl4Scale = ReplicateX4( (float)( m_flScale / MAX( 1.0e-20, pParticles->m_flPreviousDt ) ) );
		do
		{
			FourVectors v4Vel = *pXYZ;
			v4Vel -= *pPrevXYZ;
			v4Vel *= fl4Scale;
			*pOutField = v4Vel;
			++pOutField;
			++pXYZ;
			++pPrevXYZ;
		} while ( --nCtr );
	}
}


//-----------------------------------------------------------------------------
// Remap CP Velocity to Vector
//-----------------------------------------------------------------------------
class C_OP_RemapCPVelocityToVector : public CParticleOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_OP_RemapCPVelocityToVector );

	uint32 GetWrittenAttributes( void ) const
	{
		return 1 << m_nFieldOutput;
	}

	uint32 GetReadAttributes( void ) const
	{
		return 0;
	}

	virtual uint64 GetReadControlPointMask() const
	{
		return AllShl1( m_nControlPoint );
	}

	virtual void Operate( CParticleCollection *pParticles, float flStrength, void *pContext ) const;

	int m_nControlPoint;
	int m_nFieldOutput;
	float m_flScale;
	bool m_bNormalize;
};

DEFINE_PARTICLE_OPERATOR( C_OP_RemapCPVelocityToVector, "Remap CP Velocity to Vector", OPERATOR_GENERIC );

BEGIN_PARTICLE_OPERATOR_UNPACK( C_OP_RemapCPVelocityToVector )
	DMXELEMENT_UNPACK_FIELD_USERDATA( "output field", "0", int, m_nFieldOutput, "intchoice particlefield_vector" )
	DMXELEMENT_UNPACK_FIELD( "control point", "0", int, m_nControlPoint )
	DMXELEMENT_UNPACK_FIELD( "normalize", "0", bool, m_bNormalize )
	DMXELEMENT_UNPACK_FIELD( "scale factor", "1", float, m_flScale )
END_PARTICLE_OPERATOR_UNPACK( C_OP_RemapCPVelocityToVector )

void C_OP_RemapCPVelocityToVector::Operate( CParticleCollection *pParticles, float flStrength, void *pContext ) const
{
	Vector vecCPPos = pParticles->GetControlPointAtCurrentTime( m_nControlPoint );
	Vector vecCPPrevPos;
	pParticles->GetControlPointAtPrevTime( m_nControlPoint, &vecCPPrevPos );

	Vector vecDelta = vecCPPos - vecCPPrevPos;
	float flScale;
	if ( m_bNormalize )
	{
		VectorNormalize( vecDelta );
		flScale = m_flScale;
	}
	else
	{
		// NOTE: shipped code divides by the previous frame's dt
		flScale = m_flScale / MAX( 1.0e-20, pParticles->m_flPreviousDt );
	}
	vecDelta *= flScale;

	FourVectors v4Delta;
	v4Delta.DuplicateVector( vecDelta );

	C4VAttributeWriteIterator pOutField( m_nFieldOutput, pParticles );
	int nCtr = pParticles->m_nPaddedActiveParticles;
	do
	{
		*pOutField = v4Delta;
		++pOutField;
	} while ( --nCtr );
}


//-----------------------------------------------------------------------------
// Set CP Orientation to CP Direction
//-----------------------------------------------------------------------------
class C_OP_SetCPOrientationToDirection : public CParticleOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_OP_SetCPOrientationToDirection );

	uint32 GetWrittenAttributes( void ) const
	{
		return 0;
	}

	uint32 GetReadAttributes( void ) const
	{
		return 0;
	}

	virtual uint64 GetReadControlPointMask() const
	{
		return AllShl1( m_nOutputControlPoint ) | AllShl1( m_nInputControlPoint );
	}

	virtual void Operate( CParticleCollection *pParticles, float flStrength, void *pContext ) const;

	int m_nInputControlPoint;
	int m_nOutputControlPoint;
};

DEFINE_PARTICLE_OPERATOR( C_OP_SetCPOrientationToDirection, "Set CP Orientation to CP Direction", OPERATOR_GENERIC );

BEGIN_PARTICLE_OPERATOR_UNPACK( C_OP_SetCPOrientationToDirection )
	DMXELEMENT_UNPACK_FIELD( "input control point", "0", int, m_nInputControlPoint )
	DMXELEMENT_UNPACK_FIELD( "output control point", "0", int, m_nOutputControlPoint )
END_PARTICLE_OPERATOR_UNPACK( C_OP_SetCPOrientationToDirection )

void C_OP_SetCPOrientationToDirection::Operate( CParticleCollection *pParticles, float flStrength, void *pContext ) const
{
	Vector vecCPPos = pParticles->GetControlPointAtCurrentTime( m_nInputControlPoint );
	Vector vecCPPrevPos;
	pParticles->GetControlPointAtPrevTime( m_nInputControlPoint, &vecCPPrevPos );

	Vector vecFwd = vecCPPos - vecCPPrevPos;
	VectorNormalize( vecFwd );

	Vector vecRight, vecUp;
	VectorVectors( vecFwd, vecRight, vecUp );
	pParticles->SetControlPointOrientation( m_nOutputControlPoint, vecFwd, vecRight, vecUp );
}


//-----------------------------------------------------------------------------
// Remap Direction to CP to Vector
//-----------------------------------------------------------------------------
class C_OP_RemapDirectionToCPToVector : public CParticleOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_OP_RemapDirectionToCPToVector );

	uint32 GetWrittenAttributes( void ) const
	{
		return 1 << m_nFieldOutput;
	}

	uint32 GetReadAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_XYZ_MASK | PARTICLE_ATTRIBUTE_LIFE_DURATION_MASK;
	}

	virtual void Operate( CParticleCollection *pParticles, float flStrength, void *pContext ) const;

	int m_nCP;
	int m_nFieldOutput;
	float m_flScale;
	float m_flOffsetRot;
	Vector m_vecOffsetAxis;
	bool m_bNormalize;
};

DEFINE_PARTICLE_OPERATOR( C_OP_RemapDirectionToCPToVector, "Remap Direction to CP to Vector", OPERATOR_GENERIC );

BEGIN_PARTICLE_OPERATOR_UNPACK( C_OP_RemapDirectionToCPToVector )
	DMXELEMENT_UNPACK_FIELD( "control point", "0", int, m_nCP )
	DMXELEMENT_UNPACK_FIELD_USERDATA( "output field", "0", int, m_nFieldOutput, "intchoice particlefield_vector" )
	DMXELEMENT_UNPACK_FIELD( "normalize", "0", bool, m_bNormalize )
	DMXELEMENT_UNPACK_FIELD( "offset axis", "0 0 0", Vector, m_vecOffsetAxis )
	DMXELEMENT_UNPACK_FIELD( "offset rotation", "0", float, m_flOffsetRot )
	DMXELEMENT_UNPACK_FIELD( "scale factor", "1", float, m_flScale )
END_PARTICLE_OPERATOR_UNPACK( C_OP_RemapDirectionToCPToVector )

void C_OP_RemapDirectionToCPToVector::Operate( CParticleCollection *pParticles, float flStrength, void *pContext ) const
{
	C4VAttributeIterator pXYZ( PARTICLE_ATTRIBUTE_XYZ, pParticles );
	C4VAttributeWriteIterator pOutField( m_nFieldOutput, pParticles );
	int nCtr = pParticles->m_nPaddedActiveParticles;

	matrix3x4_t matRot;
	MatrixBuildRotationAboutAxis( m_vecOffsetAxis, m_flOffsetRot, matRot );

	FourVectors v4CPPosition;
	v4CPPosition.DuplicateVector( pParticles->GetControlPointAtCurrentTime( m_nCP ) );

	if ( m_bNormalize )
	{
		fltx4 fl4Scale = ReplicateX4( m_flScale );
		do
		{
			FourVectors v4Offset = *pXYZ;
			v4Offset -= v4CPPosition;
			v4Offset.RotateBy( matRot );
			v4Offset.VectorNormalize();
			v4Offset *= fl4Scale;
			*pOutField = v4Offset;
			++pXYZ;
			++pOutField;
		} while ( --nCtr );
	}
	else
	{
		// NOTE: shipped code divides by the previous frame's dt
		fltx4 fl4Scale = ReplicateX4( (float)( m_flScale / MAX( 1.0e-20, pParticles->m_flPreviousDt ) ) );
		// NOTE: shipped code adds each particle's offset into a vector that is never reset
		// (uninitialized on entry), so the rotated offsets accumulate from particle to particle
		FourVectors v4Vel;
		v4Vel.x = Four_Zeros;
		v4Vel.y = Four_Zeros;
		v4Vel.z = Four_Zeros;
		do
		{
			FourVectors v4Offset = *pXYZ;
			v4Offset -= v4CPPosition;
			v4Vel.x = AddSIMD( v4Vel.x, v4Offset.x );
			v4Vel.y = AddSIMD( v4Vel.y, v4Offset.y );
			v4Vel.z = AddSIMD( v4Vel.z, v4Offset.z );
			v4Vel.RotateBy( matRot );
			FourVectors v4Out = v4Vel;
			v4Out *= fl4Scale;
			*pOutField = v4Out;
			++pXYZ;
			++pOutField;
		} while ( --nCtr );
	}
}


//-----------------------------------------------------------------------------
// Normalize Vector
//-----------------------------------------------------------------------------
class C_OP_NormalizeVector : public CParticleOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_OP_NormalizeVector );

	uint32 GetWrittenAttributes( void ) const
	{
		return 1 << m_nFieldOutput;
	}

	uint32 GetReadAttributes( void ) const
	{
		return 1 << m_nFieldOutput;
	}

	virtual void Operate( CParticleCollection *pParticles, float flStrength, void *pContext ) const;

	int m_nFieldOutput;
	float m_flScale;
};

DEFINE_PARTICLE_OPERATOR( C_OP_NormalizeVector, "Normalize Vector", OPERATOR_GENERIC );

BEGIN_PARTICLE_OPERATOR_UNPACK( C_OP_NormalizeVector )
	DMXELEMENT_UNPACK_FIELD_USERDATA( "output field", "0", int, m_nFieldOutput, "intchoice particlefield_vector" )
	DMXELEMENT_UNPACK_FIELD( "scale factor", "1", float, m_flScale )
END_PARTICLE_OPERATOR_UNPACK( C_OP_NormalizeVector )

void C_OP_NormalizeVector::Operate( CParticleCollection *pParticles, float flStrength, void *pContext ) const
{
	C4VAttributeWriteIterator pOutField( m_nFieldOutput, pParticles );
	fltx4 fl4Scale = ReplicateX4( m_flScale );
	int nCtr = pParticles->m_nPaddedActiveParticles;
	do
	{
		FourVectors v4Vec = *pOutField;
		v4Vec.VectorNormalize();
		v4Vec *= fl4Scale;
		*pOutField = v4Vec;
		++pOutField;
	} while ( --nCtr );
}


//-----------------------------------------------------------------------------
// Remap Control Point Direction to Vector
//-----------------------------------------------------------------------------
class C_OP_RemapControlPointDirectionToVector : public CParticleOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_OP_RemapControlPointDirectionToVector );

	uint32 GetWrittenAttributes( void ) const
	{
		return 1 << m_nFieldOutput;
	}

	uint32 GetReadAttributes( void ) const
	{
		return 0;
	}

	virtual uint64 GetReadControlPointMask() const
	{
		return AllShl1( m_nControlPointNumber );
	}

	virtual void Operate( CParticleCollection *pParticles, float flStrength, void *pContext ) const;

	int m_nFieldOutput;
	float m_flScale;
	int m_nControlPointNumber;
};

DEFINE_PARTICLE_OPERATOR( C_OP_RemapControlPointDirectionToVector, "Remap Control Point Direction to Vector", OPERATOR_GENERIC );

BEGIN_PARTICLE_OPERATOR_UNPACK( C_OP_RemapControlPointDirectionToVector )
	DMXELEMENT_UNPACK_FIELD_USERDATA( "output field", "0", int, m_nFieldOutput, "intchoice particlefield_vector" )
	DMXELEMENT_UNPACK_FIELD( "control point number", "0", int, m_nControlPointNumber )
	DMXELEMENT_UNPACK_FIELD( "scale factor", "1", float, m_flScale )
END_PARTICLE_OPERATOR_UNPACK( C_OP_RemapControlPointDirectionToVector )

void C_OP_RemapControlPointDirectionToVector::Operate( CParticleCollection *pParticles, float flStrength, void *pContext ) const
{
	C4VAttributeWriteIterator pOutField( m_nFieldOutput, pParticles );
	int nCtr = pParticles->m_nPaddedActiveParticles;

	Vector vecFwd = pParticles->ControlPoint( m_nControlPointNumber ).m_ForwardVector * m_flScale;
	FourVectors v4Fwd;
	v4Fwd.DuplicateVector( vecFwd );

	do
	{
		*pOutField = v4Fwd;
		++pOutField;
	} while ( --nCtr );
}


//@@REGISTRATION@@
//-----------------------------------------------------------------------------
// Registration
//-----------------------------------------------------------------------------
void AddBuiltInParticleOperators( void )
{
	REGISTER_PARTICLE_OPERATOR( FUNCTION_OPERATOR, C_OP_BasicMovement );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_OPERATOR, C_OP_Decay );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_OPERATOR, C_OP_DecayMaintainCount );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_OPERATOR, C_OP_VelocityDecay );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_OPERATOR, C_OP_AlphaDecay );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_OPERATOR, C_OP_FadeAndKill );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_OPERATOR, C_OP_FadeAndKillForTracers );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_OPERATOR, C_OP_FadeIn );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_OPERATOR, C_OP_FadeInSimple );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_OPERATOR, C_OP_FadeOut );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_OPERATOR, C_OP_FadeOutSimple );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_OPERATOR, C_OP_Spin );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_OPERATOR, C_OP_SpinUpdate );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_OPERATOR, C_OP_SpinYaw );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_OPERATOR, C_OP_OrientTo2dDirection );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_OPERATOR, C_OP_Orient2DRelToCP );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_OPERATOR, C_OP_InterpolateRadius );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_OPERATOR, C_OP_ColorInterpolate );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_OPERATOR, C_OP_OscillateScalar );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_OPERATOR, C_OP_OscillateScalarSimple );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_OPERATOR, C_OP_OscillateVector );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_OPERATOR, C_OP_OscillateVectorSimple );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_OPERATOR, C_OP_DampenToCP );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_OPERATOR, C_OP_PositionLock );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_OPERATOR, C_OP_LockToBone );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_OPERATOR, C_OP_DistanceBetweenCPs );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_OPERATOR, C_OP_DistanceBetweenCPsToCP );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_OPERATOR, C_OP_PercentageBetweenCPs );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_OPERATOR, C_OP_PercentageBetweenCPsVector );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_OPERATOR, C_OP_DistanceToCP );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_OPERATOR, C_OP_SetControlPointToPlayer );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_OPERATOR, C_OP_SetControlPointToCenter );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_OPERATOR, C_OP_SetChildControlPoints );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_OPERATOR, C_OP_SetControlPointPositions );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_OPERATOR, C_OP_SetControlPointToImpactPoint );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_OPERATOR, C_OP_PlaneCull );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_OPERATOR, C_OP_ModelCull );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_OPERATOR, C_OP_Cull );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_OPERATOR, C_OP_ControlpointLight );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_OPERATOR, C_OP_RemapScalar );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_OPERATOR, C_OP_RemapSpeed );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_OPERATOR, C_OP_RemapSpeedtoCP );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_OPERATOR, C_OP_Noise );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_OPERATOR, C_OP_VectorNoise );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_OPERATOR, C_OP_VelocityMatchingForce );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_OPERATOR, C_OP_MaxVelocity );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_OPERATOR, C_OP_MaintainSequentialPath );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_OPERATOR, C_OP_MovementMaintainOffset );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_OPERATOR, C_OP_MovementPlaceOnGround );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_OPERATOR, C_OP_RemapDotProductToScalar );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_OPERATOR, C_OP_RemapCPtoScalar );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_OPERATOR, C_OP_RemapCPtoVector );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_OPERATOR, C_OP_RemapDirectionToCPToVector );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_OPERATOR, C_OP_RemapModelVolumetoCP );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_OPERATOR, C_OP_RemapBoundingVolumetoCP );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_OPERATOR, C_OP_RemapVelocityToVector );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_OPERATOR, C_OP_RemapCPVelocityToVector );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_OPERATOR, C_OP_RemapControlPointDirectionToVector );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_OPERATOR, C_OP_RemapAverageScalarValuetoCP );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_OPERATOR, C_OP_RampScalarLinear );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_OPERATOR, C_OP_RampScalarSpline );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_OPERATOR, C_OP_RampScalarSplineSimple );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_OPERATOR, C_OP_RampScalarLinearSimple );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_OPERATOR, C_OP_NormalLock );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_OPERATOR, C_OP_NormalizeVector );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_OPERATOR, C_OP_RotateVector );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_OPERATOR, C_OP_SetControlPointRotation );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_OPERATOR, C_OP_SetCPOrientationToDirection );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_OPERATOR, C_OP_StopAfterCPDuration );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_OPERATOR, C_OP_MoveToHitbox );
}
