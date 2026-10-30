//===== Copyright (c) Valve Corporation, All rights reserved. ======//
//
// Purpose: particle force generators
//
// Reconstructed from the Alien Swarm SDK particles.lib (builtin_particle_forces.obj).
//
//===========================================================================//

#include "tier0/platform.h"
#include "particles/particles.h"
#include "filesystem.h"
#include "tier2/tier2.h"
#include "tier2/fileutils.h"
#include "mathlib/ssemath.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"


//-----------------------------------------------------------------------------
// Random force
//-----------------------------------------------------------------------------
class C_OP_RandomForce : public CParticleOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_OP_RandomForce );

	uint32 GetWrittenAttributes( void ) const
	{
		return 0;
	}

	uint32 GetReadAttributes( void ) const
	{
		return 0;
	}

	virtual void AddForces( FourVectors *pAccumulatedForces,
							CParticleCollection *pParticles,
							int nBlocks,
							float flStrength,
							void *pContext ) const;

	Vector m_MinForce;
	Vector m_MaxForce;
};

void C_OP_RandomForce::AddForces( FourVectors *pAccumulatedForces,
								  CParticleCollection *pParticles,
								  int nBlocks,
								  float flStrength,
								  void *pContext ) const
{
	FourVectors box_min, box_max;
	box_min.DuplicateVector( m_MinForce * flStrength );
	box_max.DuplicateVector( m_MaxForce * flStrength );
	box_max -= box_min;

	int nContext = GetSIMDRandContext();
	for( int i = 0; i < nBlocks; i++ )
	{
		pAccumulatedForces->x = AddSIMD(
			pAccumulatedForces->x, AddSIMD( box_min.x, MulSIMD( box_max.x, RandSIMD( nContext ) ) ) );
		pAccumulatedForces->y = AddSIMD(
			pAccumulatedForces->y, AddSIMD( box_min.y, MulSIMD( box_max.y, RandSIMD( nContext ) ) ) );
		pAccumulatedForces->z = AddSIMD(
			pAccumulatedForces->z, AddSIMD( box_min.z, MulSIMD( box_max.z, RandSIMD( nContext ) ) ) );
		pAccumulatedForces++;
	}
	ReleaseSIMDRandContext( nContext );
}

DEFINE_PARTICLE_OPERATOR( C_OP_RandomForce, "random force", OPERATOR_GENERIC );

BEGIN_PARTICLE_OPERATOR_UNPACK( C_OP_RandomForce )
	DMXELEMENT_UNPACK_FIELD( "min force", "0 0 0", Vector, m_MinForce )
	DMXELEMENT_UNPACK_FIELD( "max force", "0 0 0", Vector, m_MaxForce )
END_PARTICLE_OPERATOR_UNPACK( C_OP_RandomForce )


//-----------------------------------------------------------------------------
// Create vortices from parent particles
//-----------------------------------------------------------------------------
class C_OP_ParentVortices : public CParticleOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_OP_ParentVortices );

	uint32 GetWrittenAttributes( void ) const
	{
		return 0;
	}

	uint32 GetReadAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_XYZ_MASK;
	}

	virtual void AddForces( FourVectors *pAccumulatedForces,
							CParticleCollection *pParticles,
							int nBlocks,
							float flStrength,
							void *pContext ) const;

	float m_flForceScale;
	Vector m_vecTwistAxis;
	bool m_bFlipBasedOnYaw;
};

struct VortexParticle_t
{
	FourVectors m_v4Center;
	FourVectors m_v4TwistAxis;
	fltx4 m_fl4OORadius;									// NOTE: holds 1/radius^2
	fltx4 m_fl4Strength;
};

void C_OP_ParentVortices::AddForces( FourVectors *pAccumulatedForces,
									 CParticleCollection *pParticles,
									 int nBlocks,
									 float flStrength,
									 void *pContext ) const
{
	if ( !pParticles->m_pParent )
		return;

	int nVortices = pParticles->m_pParent->m_nActiveParticles;
	if ( !nVortices )
		return;

	FourVectors Twist_AxisInWorldSpace;
	Twist_AxisInWorldSpace.DuplicateVector( m_vecTwistAxis );
	Twist_AxisInWorldSpace.VectorNormalize();

	// gather the parent particles into simd-friendly form
	VortexParticle_t *pVortices = ( VortexParticle_t * ) stackalloc( nVortices * sizeof( VortexParticle_t ) );
	for( int i = 0; i < nVortices; i++ )
	{
		pVortices[i].m_v4TwistAxis = Twist_AxisInWorldSpace;
		const float *pRadius = pParticles->m_pParent->GetFloatAttributePtr( PARTICLE_ATTRIBUTE_RADIUS, i );
		fltx4 fl4OORadius = ReplicateX4( 1.0 / ( 1.0e-5 + pRadius[0] ) );
		pVortices[i].m_fl4OORadius = MulSIMD( fl4OORadius, fl4OORadius );
		pVortices[i].m_fl4Strength = ReplicateX4(
			pParticles->m_pParent->GetFloatAttributePtr( PARTICLE_ATTRIBUTE_ALPHA, i )[0] * m_flForceScale * flStrength );
		const float *pXYZ = pParticles->m_pParent->GetFloatAttributePtr( PARTICLE_ATTRIBUTE_XYZ, i );
		pVortices[i].m_v4Center.x = ReplicateX4( pXYZ[0] );
		pVortices[i].m_v4Center.y = ReplicateX4( pXYZ[4] );
		pVortices[i].m_v4Center.z = ReplicateX4( pXYZ[8] );
		if ( m_bFlipBasedOnYaw )
		{
			const float *pYaw = pParticles->m_pParent->GetFloatAttributePtr( PARTICLE_ATTRIBUTE_YAW, i );
			if ( pYaw[0] >= M_PI )
			{
				// NOTE: the shipped library negates the center here, not the twist axis
				pVortices[i].m_v4Center *= Four_NegativeOnes;
			}
		}
	}

	C4VAttributeIterator pXYZ( PARTICLE_ATTRIBUTE_XYZ, pParticles );
	for( int i = 0; i < nBlocks; i++ )
	{
		FourVectors v4SumForces;
		v4SumForces.x = Four_Zeros;
		v4SumForces.y = Four_Zeros;
		v4SumForces.z = Four_Zeros;
		for( int v = 0; v < nVortices; v++ )
		{
			FourVectors v4Ofs = *pXYZ;
			v4Ofs -= pVortices[v].m_v4Center;

			// NOTE: the "parallel component" is taken along the offset itself, not along the
			// twist axis. This is what the shipped library does.
			fltx4 v4DistSQ = v4Ofs * v4Ofs;
			v4Ofs.VectorNormalize();
			FourVectors v4Parallel_comp = v4Ofs;
			v4Parallel_comp *= ( v4Ofs * pVortices[v].m_v4TwistAxis );
			v4Ofs -= v4Parallel_comp;
			fltx4 fl4Good = AndSIMD( CmpGtSIMD( v4Ofs * v4Ofs, Four_Epsilons ), CmpGtSIMD( v4DistSQ, Four_Epsilons ) );
			v4Ofs.VectorNormalize();
			FourVectors v4Force = v4Ofs ^ pVortices[v].m_v4TwistAxis;

			fltx4 fl4Falloff = MaxSIMD( Four_Zeros, SubSIMD( Four_Ones, MulSIMD( pVortices[v].m_fl4OORadius, v4DistSQ ) ) );
			v4Force *= MulSIMD( fl4Falloff, pVortices[v].m_fl4Strength );
			v4Force.x = AndSIMD( v4Force.x, fl4Good );
			v4Force.y = AndSIMD( v4Force.y, fl4Good );
			v4Force.z = AndSIMD( v4Force.z, fl4Good );
			v4SumForces += v4Force;
		}
		*( pAccumulatedForces ) += v4SumForces;
		++pAccumulatedForces;
		++pXYZ;
	}
}

DEFINE_PARTICLE_OPERATOR( C_OP_ParentVortices, "Create vortices from parent particles", OPERATOR_GENERIC );

BEGIN_PARTICLE_OPERATOR_UNPACK( C_OP_ParentVortices )
	DMXELEMENT_UNPACK_FIELD( "amount of force", "0", float, m_flForceScale )
	DMXELEMENT_UNPACK_FIELD( "twist axis", "0 0 1", Vector, m_vecTwistAxis )
	DMXELEMENT_UNPACK_FIELD( "flip twist axis with yaw", "0", bool, m_bFlipBasedOnYaw )
END_PARTICLE_OPERATOR_UNPACK( C_OP_ParentVortices )


//-----------------------------------------------------------------------------
// Twist around axis
//-----------------------------------------------------------------------------
class C_OP_TwistAroundAxis : public CParticleOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_OP_TwistAroundAxis );

	uint32 GetWrittenAttributes( void ) const
	{
		return 0;
	}

	uint32 GetReadAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_XYZ_MASK;
	}

	virtual void AddForces( FourVectors *pAccumulatedForces,
							CParticleCollection *pParticles,
							int nBlocks,
							float flStrength,
							void *pContext ) const;

	float m_fForceAmount;
	Vector m_TwistAxis;
	bool m_bLocalSpace;
};

void C_OP_TwistAroundAxis::AddForces( FourVectors *pAccumulatedForces,
									  CParticleCollection *pParticles,
									  int nBlocks,
									  float flStrength,
									  void *pContext ) const
{
	FourVectors Twist_AxisInWorldSpace;
	Twist_AxisInWorldSpace.DuplicateVector( pParticles->TransformAxis( m_TwistAxis, m_bLocalSpace ) );
	Twist_AxisInWorldSpace.VectorNormalize();

	Vector vecCenter;
	pParticles->GetControlPointAtTime( 0, pParticles->m_flCurTime, &vecCenter );
	FourVectors Center;
	Center.DuplicateVector( vecCenter );

	fltx4 ForceScale = ReplicateX4( m_fForceAmount * flStrength );

	C4VAttributeIterator pXYZ( PARTICLE_ATTRIBUTE_XYZ, pParticles );
	for( int i = 0; i < nBlocks; i++ )
	{
		// calculate a vector which is perpendicular to the axis of rotation
		FourVectors ofs = *pXYZ;
		ofs -= Center;
		fltx4 fl4DistSQ = ofs * ofs;
		ofs.VectorNormalize();

		// NOTE: as in C_OP_ParentVortices, the "parallel component" is taken along the offset
		FourVectors parallel_comp = ofs;
		parallel_comp *= ( ofs * Twist_AxisInWorldSpace );
		ofs -= parallel_comp;
		fltx4 fl4Good = AndSIMD( CmpGtSIMD( fl4DistSQ, Four_Epsilons ), CmpGtSIMD( ofs * ofs, Four_Epsilons ) );
		ofs.VectorNormalize();

		// now, cross product with the axis
		FourVectors cross = ofs ^ Twist_AxisInWorldSpace;
		cross *= ForceScale;
		pAccumulatedForces->x = AddSIMD( pAccumulatedForces->x, AndSIMD( cross.x, fl4Good ) );
		pAccumulatedForces->y = AddSIMD( pAccumulatedForces->y, AndSIMD( cross.y, fl4Good ) );
		pAccumulatedForces->z = AddSIMD( pAccumulatedForces->z, AndSIMD( cross.z, fl4Good ) );
		++pAccumulatedForces;
		++pXYZ;
	}
}

DEFINE_PARTICLE_OPERATOR( C_OP_TwistAroundAxis, "twist around axis", OPERATOR_GENERIC );

BEGIN_PARTICLE_OPERATOR_UNPACK( C_OP_TwistAroundAxis )
	DMXELEMENT_UNPACK_FIELD( "amount of force", "0", float, m_fForceAmount )
	DMXELEMENT_UNPACK_FIELD( "twist axis", "0 0 1", Vector, m_TwistAxis )
	DMXELEMENT_UNPACK_FIELD( "object local space axis 0/1", "0", bool, m_bLocalSpace )
END_PARTICLE_OPERATOR_UNPACK( C_OP_TwistAroundAxis )


//-----------------------------------------------------------------------------
// Pull towards control point
//-----------------------------------------------------------------------------
class C_OP_AttractToControlPoint : public CParticleOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_OP_AttractToControlPoint );

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
		return 1ULL << m_nControlPointNumber;
	}

	virtual void AddForces( FourVectors *pAccumulatedForces,
							CParticleCollection *pParticles,
							int nBlocks,
							float flStrength,
							void *pContext ) const;

	float m_fForceAmount;
	float m_fFalloffPower;
	int m_nControlPointNumber;
};

void C_OP_AttractToControlPoint::AddForces( FourVectors *pAccumulatedForces,
											CParticleCollection *pParticles,
											int nBlocks,
											float flStrength,
											void *pContext ) const
{
	int power_frac = -4.0f * m_fFalloffPower;				// convert to what pow_fixedpoint_exponent_simd wants
	fltx4 fForceScale = ReplicateX4( -m_fForceAmount * flStrength );

	Vector vecCenter;
	pParticles->GetControlPointAtTime( m_nControlPointNumber, pParticles->m_flCurTime, &vecCenter );

	FourVectors Center;
	Center.DuplicateVector( vecCenter );

	C4VAttributeIterator pXYZ( PARTICLE_ATTRIBUTE_XYZ, pParticles );
	for( int i = 0; i < nBlocks; i++ )
	{
		FourVectors ofs = *pXYZ;
		ofs -= Center;

		fltx4 len = ofs.length();
		ofs *= MulSIMD( fForceScale, ReciprocalSaturateSIMD( len ) ); // normalize and scale
		ofs *= Pow_FixedPoint_Exponent_SIMD( len, power_frac );		// * 1/pow(dist, exponent)

		fltx4 fl4Good = CmpGtSIMD( len, Four_Epsilons );
		pAccumulatedForces->x = AddSIMD( AndSIMD( ofs.x, fl4Good ), pAccumulatedForces->x );
		pAccumulatedForces->y = AddSIMD( AndSIMD( ofs.y, fl4Good ), pAccumulatedForces->y );
		pAccumulatedForces->z = AddSIMD( AndSIMD( ofs.z, fl4Good ), pAccumulatedForces->z );
		++pAccumulatedForces;
		++pXYZ;
	}
}

DEFINE_PARTICLE_OPERATOR( C_OP_AttractToControlPoint, "Pull towards control point", OPERATOR_GENERIC );

BEGIN_PARTICLE_OPERATOR_UNPACK( C_OP_AttractToControlPoint )
	DMXELEMENT_UNPACK_FIELD( "amount of force", "0", float, m_fForceAmount )
	DMXELEMENT_UNPACK_FIELD( "falloff power", "2", float, m_fFalloffPower )
	DMXELEMENT_UNPACK_FIELD( "control point number", "0", int, m_nControlPointNumber )
END_PARTICLE_OPERATOR_UNPACK( C_OP_AttractToControlPoint )


//-----------------------------------------------------------------------------
// Force based on distance from plane
//-----------------------------------------------------------------------------
class C_OP_ForceBasedOnDistanceToPlane : public CParticleOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_OP_ForceBasedOnDistanceToPlane );

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
		return 1ULL << m_nControlPointNumber;
	}

	virtual void AddForces( FourVectors *pAccumulatedForces,
							CParticleCollection *pParticles,
							int nBlocks,
							float flStrength,
							void *pContext ) const;

	float m_flMinDist;
	Vector m_vecForceAtMinDist;
	float m_flMaxDist;
	Vector m_vecForceAtMaxDist;
	Vector m_vecPlaneNormal;
	int m_nControlPointNumber;
	float m_flExponent;
};

void C_OP_ForceBasedOnDistanceToPlane::AddForces( FourVectors *pAccumulatedForces,
												  CParticleCollection *pParticles,
												  int nBlocks,
												  float flStrength,
												  void *pContext ) const
{
	float flDeltaDistances = m_flMaxDist - m_flMinDist;
	fltx4 fl4OORange = Four_Zeros;
	if ( flDeltaDistances )
	{
		fl4OORange = ReplicateX4( 1.0f / flDeltaDistances );
	}

	Vector vecPointOnPlane = pParticles->GetControlPointAtCurrentTime( m_nControlPointNumber );
	FourVectors v4PointOnPlane;
	v4PointOnPlane.DuplicateVector( vecPointOnPlane );
	FourVectors v4PlaneNormal;
	v4PlaneNormal.DuplicateVector( m_vecPlaneNormal );
	fltx4 fl4MinDist = ReplicateX4( m_flMinDist );
	FourVectors v4Force0;
	v4Force0.DuplicateVector( m_vecForceAtMinDist );
	FourVectors v4ForceDelta;
	v4ForceDelta.DuplicateVector( m_vecForceAtMaxDist - m_vecForceAtMinDist );
	C4VAttributeIterator pXYZ( PARTICLE_ATTRIBUTE_XYZ, pParticles );

	int nPowValue = 4.0f * m_flExponent;
	for( int i = 0; i < nBlocks; i++ )
	{
		FourVectors v4DistFromPlane = *pXYZ;
		v4DistFromPlane -= v4PointOnPlane;
		fltx4 fl4DistanceFromPlane = v4DistFromPlane * v4PlaneNormal;

		// now, map to 0..1 and raise to the exponent
		fl4DistanceFromPlane = MulSIMD( SubSIMD( fl4DistanceFromPlane, fl4MinDist ), fl4OORange );
		fl4DistanceFromPlane = ClampVectorSIMD( fl4DistanceFromPlane, Four_Zeros, Four_Ones );
		fl4DistanceFromPlane = Pow_FixedPoint_Exponent_SIMD( fl4DistanceFromPlane, nPowValue );

		FourVectors v4Force = v4ForceDelta;
		v4Force *= fl4DistanceFromPlane;
		v4Force += v4Force0;
		*( pAccumulatedForces ) += v4Force;
		++pAccumulatedForces;
		++pXYZ;
	}
}

DEFINE_PARTICLE_OPERATOR( C_OP_ForceBasedOnDistanceToPlane, "Force based on distance from plane", OPERATOR_GENERIC );

BEGIN_PARTICLE_OPERATOR_UNPACK( C_OP_ForceBasedOnDistanceToPlane )
	DMXELEMENT_UNPACK_FIELD( "Min distance from plane", "0", float, m_flMinDist )
	DMXELEMENT_UNPACK_FIELD( "Force at Min distance", "0 0 0", Vector, m_vecForceAtMinDist )
	DMXELEMENT_UNPACK_FIELD( "Max Distance from plane", "1", float, m_flMaxDist )
	DMXELEMENT_UNPACK_FIELD( "Force at Max distance", "0 0 0", Vector, m_vecForceAtMaxDist )
	DMXELEMENT_UNPACK_FIELD( "Plane Normal", "0 0 1", Vector, m_vecPlaneNormal )
	DMXELEMENT_UNPACK_FIELD( "Control point number", "0", int, m_nControlPointNumber )
	DMXELEMENT_UNPACK_FIELD( "Exponent", "1", float, m_flExponent )
END_PARTICLE_OPERATOR_UNPACK( C_OP_ForceBasedOnDistanceToPlane )


//-----------------------------------------------------------------------------
// Time varying force
//-----------------------------------------------------------------------------
class C_OP_TimeVaryingForce : public CParticleOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_OP_TimeVaryingForce );

	uint32 GetWrittenAttributes( void ) const
	{
		return 0;
	}

	uint32 GetReadAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_CREATION_TIME_MASK;
	}

	virtual void AddForces( FourVectors *pAccumulatedForces,
							CParticleCollection *pParticles,
							int nBlocks,
							float flStrength,
							void *pContext ) const;

	float m_flStartLerpTime;
	Vector m_StartingForce;
	float m_flEndLerpTime;
	Vector m_EndingForce;
};

void C_OP_TimeVaryingForce::AddForces( FourVectors *pAccumulatedForces,
									   CParticleCollection *pParticles,
									   int nBlocks,
									   float flStrength,
									   void *pContext ) const
{
	FourVectors box_min, box_max;
	box_min.DuplicateVector( m_StartingForce * flStrength );
	box_max.DuplicateVector( m_EndingForce * flStrength );
	box_max -= box_min;

	fltx4 fl4StartTime = ReplicateX4( m_flStartLerpTime );
	fltx4 fl4OODuration = ReplicateX4( 1.0f / ( m_flEndLerpTime - m_flStartLerpTime ) );

	CM128AttributeIterator pCreationTime( PARTICLE_ATTRIBUTE_CREATION_TIME, pParticles );
	fltx4 fl4CurTime = pParticles->m_fl4CurTime;
	for( int i = 0; i < nBlocks; i++ )
	{
		fltx4 fl4Age = SubSIMD( fl4CurTime, *pCreationTime );
		fltx4 fl4LerpFactor = MulSIMD( SubSIMD( fl4Age, fl4StartTime ), fl4OODuration );
		fl4LerpFactor = MaxSIMD( Four_Zeros, MinSIMD( Four_Ones, fl4LerpFactor ) );

		pAccumulatedForces->x = AddSIMD( pAccumulatedForces->x, AddSIMD( MulSIMD( box_max.x, fl4LerpFactor ), box_min.x ) );
		pAccumulatedForces->y = AddSIMD( pAccumulatedForces->y, AddSIMD( MulSIMD( box_max.y, fl4LerpFactor ), box_min.y ) );
		pAccumulatedForces->z = AddSIMD( pAccumulatedForces->z, AddSIMD( MulSIMD( box_max.z, fl4LerpFactor ), box_min.z ) );
		pAccumulatedForces++;
		++pCreationTime;
	}
}

DEFINE_PARTICLE_OPERATOR( C_OP_TimeVaryingForce, "time varying force", OPERATOR_GENERIC );

BEGIN_PARTICLE_OPERATOR_UNPACK( C_OP_TimeVaryingForce )
	DMXELEMENT_UNPACK_FIELD( "time to start transition", "0", float, m_flStartLerpTime )
	DMXELEMENT_UNPACK_FIELD( "starting force", "0 0 0", Vector, m_StartingForce )
	DMXELEMENT_UNPACK_FIELD( "time to end transition", "10", float, m_flEndLerpTime )
	DMXELEMENT_UNPACK_FIELD( "ending force", "0 0 0", Vector, m_EndingForce )
END_PARTICLE_OPERATOR_UNPACK( C_OP_TimeVaryingForce )


//-----------------------------------------------------------------------------
// Turbulent force (sum of 4 octaves of noise)
//-----------------------------------------------------------------------------
class C_OP_TurbulenceForce : public CParticleOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_OP_TurbulenceForce );

	uint32 GetWrittenAttributes( void ) const
	{
		return 0;
	}

	uint32 GetReadAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_XYZ_MASK;
	}

	virtual void AddForces( FourVectors *pAccumulatedForces,
							CParticleCollection *pParticles,
							int nBlocks,
							float flStrength,
							void *pContext ) const;

	float m_flNoiseCoordScale[4];
	Vector m_vecNoiseAmount[4];
};

void C_OP_TurbulenceForce::AddForces( FourVectors *pAccumulatedForces,
									  CParticleCollection *pParticles,
									  int nBlocks,
									  float flStrength,
									  void *pContext ) const
{
	C4VAttributeIterator pXYZ( PARTICLE_ATTRIBUTE_XYZ, pParticles );

	fltx4 fl4Scales[4];
	FourVectors v4Amounts[4];
	for( int i = 0; i < 4; i++ )
	{
		fl4Scales[i] = ReplicateX4( m_flNoiseCoordScale[i] );
		v4Amounts[i].DuplicateVector( m_vecNoiseAmount[i] );
	}

	for( int i = 0; i < nBlocks; i++ )
	{
		for( int nOctave = 0; nOctave < 4; nOctave++ )
		{
			FourVectors ppos = *pXYZ;
			ppos *= fl4Scales[nOctave];
			FourVectors noise = DNoiseSIMD( ppos );
			noise *= v4Amounts[nOctave];
			*( pAccumulatedForces ) += noise;
		}
		++pXYZ;
		++pAccumulatedForces;
	}
}

DEFINE_PARTICLE_OPERATOR( C_OP_TurbulenceForce, "turbulent force", OPERATOR_GENERIC );

BEGIN_PARTICLE_OPERATOR_UNPACK( C_OP_TurbulenceForce )
	DMXELEMENT_UNPACK_FIELD( "Noise scale 0", "1", float, m_flNoiseCoordScale[0] )
	DMXELEMENT_UNPACK_FIELD( "Noise amount 0", "1 1 1", Vector, m_vecNoiseAmount[0] )
	DMXELEMENT_UNPACK_FIELD( "Noise scale 1", "0", float, m_flNoiseCoordScale[1] )
	DMXELEMENT_UNPACK_FIELD( "Noise amount 1", ".5 .5 .5", Vector, m_vecNoiseAmount[1] )
	DMXELEMENT_UNPACK_FIELD( "Noise scale 2", "0", float, m_flNoiseCoordScale[2] )
	DMXELEMENT_UNPACK_FIELD( "Noise amount 2", ".25 .25 .25", Vector, m_vecNoiseAmount[2] )
	DMXELEMENT_UNPACK_FIELD( "Noise scale 3", "0", float, m_flNoiseCoordScale[3] )
	DMXELEMENT_UNPACK_FIELD( "Noise amount 3", ".125 .125 .125", Vector, m_vecNoiseAmount[3] )
END_PARTICLE_OPERATOR_UNPACK( C_OP_TurbulenceForce )


//-----------------------------------------------------------------------------
// Registration
//-----------------------------------------------------------------------------
void AddBuiltInParticleForceGenerators( void )
{
	REGISTER_PARTICLE_OPERATOR( FUNCTION_FORCEGENERATOR, C_OP_RandomForce );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_FORCEGENERATOR, C_OP_TwistAroundAxis );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_FORCEGENERATOR, C_OP_ParentVortices );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_FORCEGENERATOR, C_OP_AttractToControlPoint );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_FORCEGENERATOR, C_OP_TimeVaryingForce );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_FORCEGENERATOR, C_OP_TurbulenceForce );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_FORCEGENERATOR, C_OP_ForceBasedOnDistanceToPlane );
}
