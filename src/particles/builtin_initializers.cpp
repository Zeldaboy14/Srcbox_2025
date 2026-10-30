//===== Copyright (c) Valve Corporation, All rights reserved. ======//
//
// Purpose: particle system initializers
//
// Reconstructed from the Alien Swarm SDK particles.lib (builtin_initializers.obj).
//
//===========================================================================//

#include "tier0/platform.h"
#include "particles/particles.h"
#include "filesystem.h"
#include "tier2/tier2.h"
#include "tier2/fileutils.h"
#include "tier2/renderutils.h"
#include "tier1/UtlStringMap.h"
#include "tier1/utlsoacontainer.h"
#include "tier1/strtools.h"
#include "bspflags.h"
#include "const.h"
#include "mathlib/ssemath.h"
#include "mathlib/noise.h"
#include "mathlib/vmatrix.h"
#include "particles_internal.h"
#include "tier1/convar.h"
#include "bitmap/psheet.h"
#include "raytrace.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"


ConVar r_sse_s( "r_sse_s", "1", 0, "sse ins for particle sphere create" );


//-----------------------------------------------------------------------------
// Shared helpers for initializers that write a random value into a scalar attribute
//-----------------------------------------------------------------------------
void CParticleOperatorInstance::InitScalarAttributeRandomRangeExpScalar( int nAttrNum, float flMin, float flMax, float flExp,
																		 CParticleCollection *pParticles, int nStartParticle, int nParticleCount ) const
{
	if ( !m_bStrengthFastPath )
	{
		float flStrength;
		pParticles->CheckIfOperatorShouldRun( this, &flStrength );
		flMin *= flStrength;
		flMax *= flStrength;
	}

	if ( flExp != 1.0f )
	{
		for( ; nParticleCount--; nStartParticle++ )
		{
			float *pAttr = pParticles->GetFloatAttributePtrForWrite( nAttrNum, nStartParticle );
			*pAttr = pParticles->RandomFloatExp( flMin, flMax, flExp );
		}
	}
	else
	{
		for( ; nParticleCount--; nStartParticle++ )
		{
			float *pAttr = pParticles->GetFloatAttributePtrForWrite( nAttrNum, nStartParticle );
			*pAttr = pParticles->RandomFloat( flMin, flMax );
		}
	}
}

void CParticleOperatorInstance::InitScalarAttributeRandomRangeExpBlock( int attr_num, float fMin, float fMax, float fExp,
																		CParticleCollection *pParticles, int start_block, int n_blocks ) const
{
	if ( !m_bStrengthFastPath )
	{
		float flStrength;
		pParticles->CheckIfOperatorShouldRun( this, &flStrength );
		fMin *= flStrength;
		fMax *= flStrength;
	}

	size_t attr_stride;
	fltx4 *pAttr = pParticles->GetM128AttributePtrForWrite( attr_num, &attr_stride );
	pAttr += attr_stride * start_block;
	fltx4 val0 = ReplicateX4( fMin );
	fltx4 val_d = ReplicateX4( fMax - fMin );
	int nRandContext = GetSIMDRandContext();
	if ( fExp == 1.0f )
	{
		while( n_blocks-- )
		{
			*( pAttr ) = AddSIMD( val0, MulSIMD( RandSIMD( nRandContext ), val_d ) );
			pAttr += attr_stride;
		}
	}
	else
	{
		int nExp = (int)fExp;
		while( n_blocks-- )
		{
			*( pAttr ) = AddSIMD( val0, MulSIMD( Pow_FixedPoint_Exponent_SIMD( RandSIMD( nRandContext ), nExp ), val_d ) );
			pAttr += attr_stride;
		}
	}
	ReleaseSIMDRandContext( nRandContext );
}

void CParticleOperatorInstance::AddScalarAttributeRandomRangeBlock( int nAttributeId, float fMin, float fMax, float fExp,
																	CParticleCollection *pParticles, int nStartBlock, int nBlockCount, bool bRandomlyInvert ) const
{
	size_t nAttrStride;
	fltx4 *pAttr = pParticles->GetM128AttributePtrForWrite( nAttributeId, &nAttrStride );
	pAttr += nAttrStride * nStartBlock;
	fltx4 val0 = ReplicateX4( fMin );
	fltx4 val_d = ReplicateX4( fMax - fMin );
	int nRandContext = GetSIMDRandContext();
	if ( !bRandomlyInvert )
	{
		if ( fExp != 1.0f )
		{
			int nExp = (int)fExp;
			while( nBlockCount-- )
			{
				*( pAttr ) = AddSIMD( *pAttr, AddSIMD( val0, MulSIMD( Pow_FixedPoint_Exponent_SIMD( RandSIMD( nRandContext ), nExp ), val_d ) ) );
				pAttr += nAttrStride;
			}
		}
		else
		{
			while( nBlockCount-- )
			{
				*( pAttr ) = AddSIMD( *pAttr, AddSIMD( val0, MulSIMD( RandSIMD( nRandContext ), val_d ) ) );
				pAttr += nAttrStride;
			}
		}
	}
	else
	{
		fltx4 fl4NegOne = ReplicateX4( -1.0f );
		if ( fExp != 1.0f )
		{
			int nExp = (int)fExp;
			while( nBlockCount-- )
			{
				fltx4 fl4RandVal = AddSIMD( val0, MulSIMD( Pow_FixedPoint_Exponent_SIMD( RandSIMD( nRandContext ), nExp ), val_d ) );
				fltx4 fl4Sign = MaskedAssign( CmpGeSIMD( RandSIMD( nRandContext ), Four_PointFives ), Four_Ones, fl4NegOne );
				*( pAttr ) = AddSIMD( *pAttr, MulSIMD( fl4RandVal, fl4Sign ) );
				pAttr += nAttrStride;
			}
		}
		else
		{
			while( nBlockCount-- )
			{
				fltx4 fl4RandVal = AddSIMD( val0, MulSIMD( RandSIMD( nRandContext ), val_d ) );
				fltx4 fl4Sign = MaskedAssign( CmpGeSIMD( RandSIMD( nRandContext ), Four_PointFives ), Four_Ones, fl4NegOne );
				*( pAttr ) = AddSIMD( *pAttr, MulSIMD( fl4RandVal, fl4Sign ) );
				pAttr += nAttrStride;
			}
		}
	}
	ReleaseSIMDRandContext( nRandContext );
}


//-----------------------------------------------------------------------------
// Position on a ring
//-----------------------------------------------------------------------------
class C_INIT_RingWave : public CParticleInitializerOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_INIT_RingWave );

	uint32 GetWrittenAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_XYZ_MASK | PARTICLE_ATTRIBUTE_PREV_XYZ_MASK;
	}

	uint32 GetReadAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_CREATION_TIME_MASK;
	}

	virtual uint64 GetReadControlPointMask() const
	{
		uint64 nMask = ( 1ULL << m_nControlPointNumber );
		if ( m_nOverrideCP != -1 )
		{
			nMask |= ( 1ULL << m_nOverrideCP );
		}
		return nMask;
	}

	virtual void InitParams( CParticleSystemDefinition *pDef )
	{
		m_nControlPointNumber = clamp( m_nControlPointNumber, 0, MAX_PARTICLE_CONTROL_POINTS - 1 );
		m_nOverrideCP = clamp( m_nOverrideCP, -1, MAX_PARTICLE_CONTROL_POINTS - 1 );
		m_flInitialSpeedMax = MAX( m_flInitialSpeedMin, m_flInitialSpeedMax );
		m_flInitialRadius = MAX( 0.0f, m_flInitialRadius );
		m_flRoll *= ( M_PI / 180.0 );
		m_flPitch *= ( M_PI / 180.0 );
	}

	virtual void InitNewParticlesScalar( CParticleCollection *pParticles, int start_p,
										 int nParticleCount, int nAttributeWriteMask,
										 void *pContext) const;

	int m_nControlPointNumber;
	int m_nOverrideCP;
	float m_flParticlesPerOrbit;
	float m_flInitialRadius;
	float m_flThickness;
	float m_flInitialSpeedMin;
	float m_flInitialSpeedMax;
	float m_flRoll;
	float m_flPitch;
	bool m_bEvenDistribution;
	bool m_bXYVelocityOnly;
};

DEFINE_PARTICLE_OPERATOR( C_INIT_RingWave, "Position Along Ring", OPERATOR_PI_POSITION );

BEGIN_PARTICLE_INITIALIZER_OPERATOR_UNPACK( C_INIT_RingWave )
	DMXELEMENT_UNPACK_FIELD( "control point number", "0", int, m_nControlPointNumber )
	DMXELEMENT_UNPACK_FIELD( "initial radius", "0", float, m_flInitialRadius )
	DMXELEMENT_UNPACK_FIELD( "thickness", "0", float, m_flThickness )
	DMXELEMENT_UNPACK_FIELD( "min initial speed", "0", float, m_flInitialSpeedMin )
	DMXELEMENT_UNPACK_FIELD( "max initial speed", "0", float, m_flInitialSpeedMax )
	DMXELEMENT_UNPACK_FIELD( "roll", "0", float, m_flRoll )
	DMXELEMENT_UNPACK_FIELD( "pitch", "0", float, m_flPitch )
	DMXELEMENT_UNPACK_FIELD( "even distribution", "0", bool, m_bEvenDistribution )
	DMXELEMENT_UNPACK_FIELD( "even distribution count", "-1", float, m_flParticlesPerOrbit )
	DMXELEMENT_UNPACK_FIELD( "XY velocity only", "1", bool, m_bXYVelocityOnly )
	DMXELEMENT_UNPACK_FIELD( "Override CP (X/Y/Z *= Radius/Thickness/Speed)", "-1", int, m_nOverrideCP )
END_PARTICLE_OPERATOR_UNPACK( C_INIT_RingWave )

void C_INIT_RingWave::InitNewParticlesScalar(
	CParticleCollection *pParticles, int start_p,
	int nParticleCount, int nAttributeWriteMask, void *pContext ) const
{
	float flRadius = m_flInitialRadius;
	float flThickness = m_flThickness;
	float flSpeedMin = m_flInitialSpeedMin;
	float flSpeedMax = m_flInitialSpeedMax;
	if ( m_nOverrideCP != -1 )
	{
		Vector vecOverride = pParticles->GetControlPointAtCurrentTime( m_nOverrideCP );
		flRadius *= vecOverride.x;
		flThickness *= vecOverride.y;
		flSpeedMin *= vecOverride.z;
		flSpeedMax *= vecOverride.z;
	}

	float flParticlesPerOrbit = m_flParticlesPerOrbit;
	if ( !m_bEvenDistribution || ( flParticlesPerOrbit == -1 ) )
	{
		flParticlesPerOrbit = nParticleCount;
	}

	float flThetaStep = ( 1.0f / flParticlesPerOrbit ) * 2.0 * M_PI;
	QAngle qatAngles( m_flRoll, m_flPitch, 0 );

	for( ; nParticleCount--; start_p++ )
	{
		float *xyz = pParticles->GetFloatAttributePtrForWrite( PARTICLE_ATTRIBUTE_XYZ, start_p );
		const float *ct = pParticles->GetFloatAttributePtr( PARTICLE_ATTRIBUTE_CREATION_TIME, start_p );
		float *pxyz = pParticles->GetFloatAttributePtrForWrite( PARTICLE_ATTRIBUTE_PREV_XYZ, start_p );

		float sinTheta = 0.0f;
		float cosTheta = 0.0f;

		Vector randPos;
		pParticles->RandomVectorInUnitSphere( &randPos );

		matrix3x4_t mat;
		pParticles->GetControlPointTransformAtTime( m_nControlPointNumber, *ct, &mat );
		Vector vecControlPoint;
		MatrixGetColumn( mat, 3, vecControlPoint );

		float flTheta = m_bEvenDistribution ? flThetaStep : pParticles->RandomFloat( 0.0f, 2.0f * M_PI );
		float theta = ( start_p + 1 ) * flTheta;
		SinCos( theta, &sinTheta, &cosTheta );

		Vector pointOnCircle( flRadius * cosTheta, flRadius * sinTheta, flRadius * 0.0f );
		Vector vecTransformLocal;
		VectorRotate( pointOnCircle, qatAngles, vecTransformLocal );
		float flSpeed = pParticles->RandomFloat( flSpeedMin, flSpeedMax );
		randPos = vecTransformLocal + randPos * flThickness;

		Vector vecTransformed;
		VectorTransform( randPos, mat, vecTransformed );
		randPos = vecTransformed;
		Vector vecVelocity = randPos - vecControlPoint;
		VectorNormalize( vecVelocity );
		vecVelocity *= flSpeed * pParticles->m_flPreviousDt;

		xyz[0] = randPos.x;
		xyz[4] = randPos.y;
		xyz[8] = randPos.z;

		if ( pxyz && ( nAttributeWriteMask & PARTICLE_ATTRIBUTE_PREV_XYZ_MASK ) )
		{
			pxyz[0] = randPos.x - vecVelocity.x;
			pxyz[4] = randPos.y - vecVelocity.y;
			pxyz[8] = m_bXYVelocityOnly ? randPos.z : ( randPos.z - vecVelocity.z );
		}
	}
}


//-----------------------------------------------------------------------------
// Position on the surface of the controlling object's hitboxes
//-----------------------------------------------------------------------------
class C_INIT_CreateOnModel : public CParticleInitializerOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_INIT_CreateOnModel );

	int m_nControlPointNumber;
	int m_nForceInModel;
	int m_nDesiredHitbox;
	float m_flHitBoxScale;
	Vector m_vecDirectionBias;

	uint32 GetWrittenAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_XYZ_MASK | PARTICLE_ATTRIBUTE_PREV_XYZ_MASK |
			PARTICLE_ATTRIBUTE_HITBOX_INDEX_MASK | PARTICLE_ATTRIBUTE_HITBOX_RELATIVE_XYZ_MASK;
	}

	uint32 GetReadAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_CREATION_TIME_MASK;
	}

	virtual uint64 GetReadControlPointMask() const
	{
		return 1ULL << m_nControlPointNumber;
	}

	virtual void InitNewParticlesScalar( CParticleCollection *pParticles, int start_p,
										 int nParticleCount, int nAttributeWriteMask,
										 void *pContext) const;
};

DEFINE_PARTICLE_OPERATOR( C_INIT_CreateOnModel, "Position on Model Random", OPERATOR_PI_POSITION );

BEGIN_PARTICLE_INITIALIZER_OPERATOR_UNPACK( C_INIT_CreateOnModel )
	DMXELEMENT_UNPACK_FIELD( "control_point_number", "0", int, m_nControlPointNumber )
	DMXELEMENT_UNPACK_FIELD( "force to be inside model", "0", int, m_nForceInModel )
	DMXELEMENT_UNPACK_FIELD( "model hitbox scale", "1.0", float, m_flHitBoxScale )
	DMXELEMENT_UNPACK_FIELD( "direction bias", "0 0 0", Vector, m_vecDirectionBias )
	DMXELEMENT_UNPACK_FIELD( "desired hitbox", "-1", int, m_nDesiredHitbox )
END_PARTICLE_OPERATOR_UNPACK( C_INIT_CreateOnModel )

void C_INIT_CreateOnModel::InitNewParticlesScalar(
	CParticleCollection *pParticles, int start_p,
	int nParticleCount, int nAttributeWriteMask, void *pContext ) const
{
	pParticles->UpdateHitBoxInfo( m_nControlPointNumber );
	while( nParticleCount )
	{
		Vector vecPnts[100];								// minimize stack usage
		Vector vecUVW[100];
		int nHitBoxIndex[100];
		int nToDo = MIN( nParticleCount, ARRAYSIZE( vecPnts ) );

		Assert( m_nControlPointNumber <= pParticles->GetHighestControlPoint() );

		g_pParticleSystemMgr->Query()->GetRandomPointsOnControllingObjectHitBox(
			pParticles, m_nControlPointNumber, nToDo, m_flHitBoxScale, m_nForceInModel,
			vecPnts, m_vecDirectionBias, vecUVW, nHitBoxIndex, m_nDesiredHitbox );

		for( int i = 0; i < nToDo; i++ )
		{
			float *xyz = pParticles->GetFloatAttributePtrForWrite( PARTICLE_ATTRIBUTE_XYZ, start_p );
			float *pxyz = pParticles->GetFloatAttributePtrForWrite( PARTICLE_ATTRIBUTE_PREV_XYZ, start_p );
			float *pHitboxRelXYZ = pParticles->GetFloatAttributePtrForWrite( PARTICLE_ATTRIBUTE_HITBOX_RELATIVE_XYZ, start_p );
			int *pHitboxIndex = pParticles->GetIntAttributePtrForWrite( PARTICLE_ATTRIBUTE_HITBOX_INDEX, start_p );

			Vector randpos = vecPnts[i];
			xyz[0] = randpos.x;
			xyz[4] = randpos.y;
			xyz[8] = randpos.z;
			if ( pxyz && ( nAttributeWriteMask & PARTICLE_ATTRIBUTE_PREV_XYZ_MASK ) )
			{
				pxyz[0] = randpos.x;
				pxyz[4] = randpos.y;
				pxyz[8] = randpos.z;
			}
			if ( pHitboxRelXYZ && ( nAttributeWriteMask & PARTICLE_ATTRIBUTE_HITBOX_RELATIVE_XYZ_MASK ) )
			{
				pHitboxRelXYZ[0] = vecUVW[i].x;
				pHitboxRelXYZ[4] = vecUVW[i].y;
				pHitboxRelXYZ[8] = vecUVW[i].z;
			}
			if ( pHitboxIndex && ( nAttributeWriteMask & PARTICLE_ATTRIBUTE_HITBOX_INDEX_MASK ) )
			{
				*pHitboxIndex = nHitBoxIndex[i];
			}
			start_p++;
		}
		nParticleCount -= nToDo;
	}
}


//-----------------------------------------------------------------------------
// Picks a hitbox (and hitbox-relative position) without moving the particle
//-----------------------------------------------------------------------------
class C_INIT_SetHitboxToModel : public CParticleInitializerOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_INIT_SetHitboxToModel );

	int m_nControlPointNumber;
	int m_nForceInModel;
	int m_nDesiredHitbox;
	float m_flHitBoxScale;
	Vector m_vecDirectionBias;
	bool m_bMaintainHitbox;

	uint32 GetWrittenAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_HITBOX_INDEX_MASK | PARTICLE_ATTRIBUTE_HITBOX_RELATIVE_XYZ_MASK;
	}

	uint32 GetReadAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_CREATION_TIME_MASK;
	}

	virtual uint64 GetReadControlPointMask() const
	{
		return 1ULL << m_nControlPointNumber;
	}

	virtual void InitNewParticlesScalar( CParticleCollection *pParticles, int start_p,
										 int nParticleCount, int nAttributeWriteMask,
										 void *pContext) const;
};

DEFINE_PARTICLE_OPERATOR( C_INIT_SetHitboxToModel, "Set Hitbox Position on Model", OPERATOR_GENERIC );

BEGIN_PARTICLE_INITIALIZER_OPERATOR_UNPACK( C_INIT_SetHitboxToModel )
	DMXELEMENT_UNPACK_FIELD( "control_point_number", "0", int, m_nControlPointNumber )
	DMXELEMENT_UNPACK_FIELD( "force to be inside model", "0", int, m_nForceInModel )
	DMXELEMENT_UNPACK_FIELD( "model hitbox scale", "1.0", float, m_flHitBoxScale )
	DMXELEMENT_UNPACK_FIELD( "direction bias", "0 0 0", Vector, m_vecDirectionBias )
	DMXELEMENT_UNPACK_FIELD( "desired hitbox", "-1", int, m_nDesiredHitbox )
	DMXELEMENT_UNPACK_FIELD( "maintain existing hitbox", "0", bool, m_bMaintainHitbox )
END_PARTICLE_OPERATOR_UNPACK( C_INIT_SetHitboxToModel )

void C_INIT_SetHitboxToModel::InitNewParticlesScalar(
	CParticleCollection *pParticles, int start_p,
	int nParticleCount, int nAttributeWriteMask, void *pContext ) const
{
	pParticles->UpdateHitBoxInfo( m_nControlPointNumber );
	while( nParticleCount )
	{
		Vector vecPnts[100];								// minimize stack usage
		Vector vecUVW[100];
		int nHitBoxIndex[100];
		int nToDo = MIN( nParticleCount, ARRAYSIZE( vecPnts ) );

		Assert( m_nControlPointNumber <= pParticles->GetHighestControlPoint() );

		int nDesiredHitbox = m_nDesiredHitbox;
		if ( m_bMaintainHitbox )
		{
			nDesiredHitbox = *pParticles->GetIntAttributePtr( PARTICLE_ATTRIBUTE_HITBOX_INDEX, start_p );
		}

		g_pParticleSystemMgr->Query()->GetRandomPointsOnControllingObjectHitBox(
			pParticles, m_nControlPointNumber, nToDo, m_flHitBoxScale, m_nForceInModel,
			vecPnts, m_vecDirectionBias, vecUVW, nHitBoxIndex, nDesiredHitbox );

		for( int i = 0; i < nToDo; i++ )
		{
			float *pHitboxRelXYZ = pParticles->GetFloatAttributePtrForWrite( PARTICLE_ATTRIBUTE_HITBOX_RELATIVE_XYZ, start_p );
			int *pHitboxIndex = pParticles->GetIntAttributePtrForWrite( PARTICLE_ATTRIBUTE_HITBOX_INDEX, start_p );

			if ( pHitboxRelXYZ && ( nAttributeWriteMask & PARTICLE_ATTRIBUTE_HITBOX_RELATIVE_XYZ_MASK ) )
			{
				pHitboxRelXYZ[0] = vecUVW[i].x;
				pHitboxRelXYZ[4] = vecUVW[i].y;
				pHitboxRelXYZ[8] = vecUVW[i].z;
			}
			if ( pHitboxIndex && ( nAttributeWriteMask & PARTICLE_ATTRIBUTE_HITBOX_INDEX_MASK ) )
			{
				*pHitboxIndex = nHitBoxIndex[i];
			}
			start_p++;
		}
		nParticleCount -= nToDo;
	}
}


//-----------------------------------------------------------------------------
// Random position within a sphere
//-----------------------------------------------------------------------------
static void RandomPointInUnitSphere( int nRandContext, FourVectors &out )
{
	// generate 4 random points in the unit sphere. uses 4 * 3 * (rejection_rate) random numbers
	out.x = SubSIMD( MulSIMD( Four_Twos, RandSIMD( nRandContext ) ), Four_Ones );
	out.y = SubSIMD( MulSIMD( Four_Twos, RandSIMD( nRandContext ) ), Four_Ones );
	out.z = SubSIMD( MulSIMD( Four_Twos, RandSIMD( nRandContext ) ), Four_Ones );

	// now, we have to find the bad ones and re-generate them
	fltx4 badMask = CmpGtSIMD( out * out, Four_Ones );
	while ( IsAnyNegative( badMask ) )
	{
		out.x = MaskedAssign( badMask, SubSIMD( MulSIMD( Four_Twos, RandSIMD( nRandContext ) ), Four_Ones ), out.x );
		out.y = MaskedAssign( badMask, SubSIMD( MulSIMD( Four_Twos, RandSIMD( nRandContext ) ), Four_Ones ), out.y );
		out.z = MaskedAssign( badMask, SubSIMD( MulSIMD( Four_Twos, RandSIMD( nRandContext ) ), Four_Ones ), out.z );
		badMask = CmpGeSIMD( out * out, Four_Ones );
	}
}

class C_INIT_CreateWithinSphere : public CParticleInitializerOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_INIT_CreateWithinSphere );

	float m_fRadiusMin;
	float m_fRadiusMax;
	Vector m_vecDistanceBias, m_vecDistanceBiasAbs;
	int m_nControlPointNumber;
	float m_fSpeedMin;
	float m_fSpeedMax;
	float m_fSpeedRandExp;
	bool m_bLocalCoords;
	bool m_bDistanceBiasAbs;
	bool m_bUseHighestEndCP;
	bool m_bDistanceBias;
	float m_flEndCPGrowthTime;
	Vector m_LocalCoordinateSystemSpeedMin;
	Vector m_LocalCoordinateSystemSpeedMax;

	uint32 GetWrittenAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_XYZ_MASK | PARTICLE_ATTRIBUTE_PREV_XYZ_MASK;
	}

	uint32 GetReadAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_CREATION_TIME_MASK;
	}

	virtual uint64 GetReadControlPointMask() const
	{
		if ( !m_bUseHighestEndCP )
			return 1ULL << m_nControlPointNumber;
		return ~( ( 1ULL << m_nControlPointNumber ) - 1 );
	}

	virtual void InitNewParticlesScalar( CParticleCollection *pParticles, int start_p,
										 int nParticleCount, int nAttributeWriteMask,
										 void *pContext) const;

	virtual void InitNewParticlesBlock( CParticleCollection *pParticles,
										int start_block, int n_blocks, int nAttributeWriteMask,
										void *pContext ) const;

	virtual void InitParams( CParticleSystemDefinition *pDef )
	{
		m_nControlPointNumber = clamp( m_nControlPointNumber, 0, MAX_PARTICLE_CONTROL_POINTS - 1 );
		m_bDistanceBias = ( m_vecDistanceBias.x != 1.0f ) || ( m_vecDistanceBias.y != 1.0f ) || ( m_vecDistanceBias.z != 1.0f );
		m_bDistanceBiasAbs = ( m_vecDistanceBiasAbs.x != 0.0f ) || ( m_vecDistanceBiasAbs.y != 0.0f ) || ( m_vecDistanceBiasAbs.z != 0.0f );
	}

	virtual void Render( CParticleCollection *pParticles ) const;
};

DEFINE_PARTICLE_OPERATOR( C_INIT_CreateWithinSphere, "Position Within Sphere Random", OPERATOR_PI_POSITION );

BEGIN_PARTICLE_INITIALIZER_OPERATOR_UNPACK( C_INIT_CreateWithinSphere )
	DMXELEMENT_UNPACK_FIELD( "distance_min", "0", float, m_fRadiusMin )
	DMXELEMENT_UNPACK_FIELD( "distance_max", "0", float, m_fRadiusMax )
	DMXELEMENT_UNPACK_FIELD( "distance_bias", "1 1 1", Vector, m_vecDistanceBias )
	DMXELEMENT_UNPACK_FIELD( "distance_bias_absolute_value", "0 0 0", Vector, m_vecDistanceBiasAbs )
	DMXELEMENT_UNPACK_FIELD( "bias in local system", "0", bool, m_bLocalCoords )
	DMXELEMENT_UNPACK_FIELD( "control_point_number", "0", int, m_nControlPointNumber )
	DMXELEMENT_UNPACK_FIELD( "speed_min", "0", float, m_fSpeedMin )
	DMXELEMENT_UNPACK_FIELD( "speed_max", "0", float, m_fSpeedMax )
	DMXELEMENT_UNPACK_FIELD( "speed_random_exponent", "1", float, m_fSpeedRandExp )
	DMXELEMENT_UNPACK_FIELD( "speed_in_local_coordinate_system_min", "0 0 0", Vector, m_LocalCoordinateSystemSpeedMin )
	DMXELEMENT_UNPACK_FIELD( "speed_in_local_coordinate_system_max", "0 0 0", Vector, m_LocalCoordinateSystemSpeedMax )
	DMXELEMENT_UNPACK_FIELD( "randomly distribute to highest supplied Control Point", "0", bool, m_bUseHighestEndCP )
	DMXELEMENT_UNPACK_FIELD( "randomly distribution growth time", "0", float, m_flEndCPGrowthTime )
END_PARTICLE_OPERATOR_UNPACK( C_INIT_CreateWithinSphere )

void C_INIT_CreateWithinSphere::Render( CParticleCollection *pParticles ) const
{
	Vector vecOrigin;
	pParticles->GetControlPointAtTime( m_nControlPointNumber, pParticles->m_flCurTime, &vecOrigin );
	RenderWireframeSphere( vecOrigin, m_fRadiusMin, 16, 8, Color( 192, 192, 0, 255 ), false );
	RenderWireframeSphere( vecOrigin, m_fRadiusMax, 16, 8, Color( 128, 128, 0, 255 ), false );
}

void C_INIT_CreateWithinSphere::InitNewParticlesScalar(
	CParticleCollection *pParticles, int start_p,
	int nParticleCount, int nAttributeWriteMask, void *pContext ) const
{
	for( ; nParticleCount--; start_p++ )
	{
		float *xyz = pParticles->GetFloatAttributePtrForWrite( PARTICLE_ATTRIBUTE_XYZ, start_p );
		float ct = *pParticles->GetFloatAttributePtr( PARTICLE_ATTRIBUTE_CREATION_TIME, start_p );
		ct = MAX( ct, pParticles->m_flCurTime - pParticles->m_flDt );
		float *pxyz = pParticles->GetFloatAttributePtrForWrite( PARTICLE_ATTRIBUTE_PREV_XYZ, start_p );

		int nCurrentControlPoint = m_nControlPointNumber;
		if ( m_bUseHighestEndCP )
		{
			// hack for growth time instead of using strength as currenly initializers don't support it.
			float flStrength = 1.0f;
			if ( m_flEndCPGrowthTime != 0.0f )
			{
				flStrength = MIN( pParticles->m_flCurTime, m_flEndCPGrowthTime ) / m_flEndCPGrowthTime;
			}
			int nHighestControlPoint = floor( pParticles->GetHighestControlPoint() * flStrength );
			nCurrentControlPoint = pParticles->RandomInt( m_nControlPointNumber, nHighestControlPoint );
		}

		Vector randpos, randDir;
		float flLength = pParticles->RandomVectorInUnitSphere( &randpos );

		// Absolute value and biasing for creating hemispheres and ovoids.
		if ( m_bDistanceBiasAbs )
		{
			if ( m_vecDistanceBiasAbs.x != 0.0f )
			{
				randpos.x = fabs( randpos.x );
			}
			if ( m_vecDistanceBiasAbs.y != 0.0f )
			{
				randpos.y = fabs( randpos.y );
			}
			if ( m_vecDistanceBiasAbs.z != 0.0f )
			{
				randpos.z = fabs( randpos.z );
			}
		}
		randpos *= m_vecDistanceBias;
		randpos.NormalizeInPlace();

		randDir = randpos;
		randpos *= Lerp( flLength, m_fRadiusMin, m_fRadiusMax );

		if ( !m_bDistanceBias || !m_bLocalCoords )
		{
			Vector vecControlPoint;
			pParticles->GetControlPointAtTime( nCurrentControlPoint, ct, &vecControlPoint );
			randpos += vecControlPoint;
		}
		else
		{
			matrix3x4_t mat;
			pParticles->GetControlPointTransformAtTime( nCurrentControlPoint, ct, &mat );
			Vector vecTransformLocal = vec3_origin;
			VectorTransform( randpos, mat, vecTransformLocal );
			randpos = vecTransformLocal;
		}

		xyz[0] = randpos.x;
		xyz[4] = randpos.y;
		xyz[8] = randpos.z;

		// FIXME: Remove this into a speed setting initializer
		if ( pxyz && ( nAttributeWriteMask & PARTICLE_ATTRIBUTE_PREV_XYZ_MASK ) )
		{
			Vector poffset( 0, 0, 0 );
			if ( m_fSpeedMax > 0.0 )
			{
				float rand_speed = pParticles->RandomFloatExp( m_fSpeedMin, m_fSpeedMax, m_fSpeedRandExp );
				poffset.x -= rand_speed * randDir.x;
				poffset.y -= rand_speed * randDir.y;
				poffset.z -= rand_speed * randDir.z;
			}
			poffset -=
				pParticles->RandomFloat( m_LocalCoordinateSystemSpeedMin.x, m_LocalCoordinateSystemSpeedMax.x ) *
				pParticles->ControlPoint( nCurrentControlPoint ).m_ForwardVector;
			poffset -=
				pParticles->RandomFloat( m_LocalCoordinateSystemSpeedMin.y, m_LocalCoordinateSystemSpeedMax.y ) *
				pParticles->ControlPoint( nCurrentControlPoint ).m_RightVector;
			poffset -=
				pParticles->RandomFloat( m_LocalCoordinateSystemSpeedMin.z, m_LocalCoordinateSystemSpeedMax.z ) *
				pParticles->ControlPoint( nCurrentControlPoint ).m_UpVector;

			poffset *= pParticles->m_flPreviousDt;
			randpos += poffset;
			pxyz[0] = randpos.x;
			pxyz[4] = randpos.y;
			pxyz[8] = randpos.z;
		}
	}
}

void C_INIT_CreateWithinSphere::InitNewParticlesBlock( CParticleCollection *pParticles,
													  int start_block, int n_blocks, int nAttributeWriteMask,
													  void *pContext ) const
{
	// sse-favorable settings
	bool bMustUseScalar = ( m_bDistanceBias && m_bLocalCoords ) || m_bUseHighestEndCP;
	if ( !bMustUseScalar )
	{
		C4VAttributeWriteIterator pXYZ( PARTICLE_ATTRIBUTE_XYZ, pParticles );
		pXYZ += start_block;
		C4VAttributeWriteIterator pPrevXYZ( PARTICLE_ATTRIBUTE_PREV_XYZ, pParticles );
		pPrevXYZ += start_block;
		CM128AttributeIterator pCT( PARTICLE_ATTRIBUTE_CREATION_TIME, pParticles );
		pCT += start_block;

		// now, calculate the terms we need for interpolating control points
		FourVectors v4PrevControlPointPosition;
		v4PrevControlPointPosition.DuplicateVector( pParticles->ControlPoint( m_nControlPointNumber ).m_PrevPosition );
		FourVectors v4ControlPointDelta;
		v4ControlPointDelta.DuplicateVector( pParticles->ControlPoint( m_nControlPointNumber ).m_Position );
		v4ControlPointDelta -= v4PrevControlPointPosition;

		float flOODT = ( pParticles->m_flDt > 0.0 ) ? ( 1.0f / pParticles->m_flDt ) : 0.0;
		fltx4 fl4OODt = ReplicateX4( flOODT );
		fltx4 fl4PrevTime = ReplicateX4( pParticles->m_flCurTime - pParticles->m_flDt );
		int nContext = GetSIMDRandContext();

		FourVectors v4DistanceBias;
		v4DistanceBias.DuplicateVector( m_vecDistanceBias );
		FourVectors v4ConditionalAbsMask;
		for( int nComp = 0 ; nComp < 3; nComp++ )
		{
			v4ConditionalAbsMask[nComp] = ( m_vecDistanceBiasAbs[nComp] > 0 ) ?
				LoadAlignedSIMD( ( const float *) g_SIMD_clear_signmask ) :
				LoadAlignedSIMD( ( const float *) g_SIMD_AllOnesMask );
		}
		fltx4 fl4RadiusMin = ReplicateX4( m_fRadiusMin );
		fltx4 fl4RadiusSpread = ReplicateX4( m_fRadiusMax - m_fRadiusMin );
		int nPowSSEMask = 4.0f * m_fSpeedRandExp;

		bool bDoRandSpeed =
			( m_fSpeedMax > 0. ) ||
			( m_LocalCoordinateSystemSpeedMax.x != 0 ) ||
			( m_LocalCoordinateSystemSpeedMax.y != 0 ) ||
			( m_LocalCoordinateSystemSpeedMax.z != 0 ) ||
			( m_LocalCoordinateSystemSpeedMin.x != 0 ) ||
			( m_LocalCoordinateSystemSpeedMin.y != 0 ) ||
			( m_LocalCoordinateSystemSpeedMin.z != 0 );

		fltx4 fl4SpeedMin = ReplicateX4( m_fSpeedMin );
		fltx4 fl4SpeedRange = ReplicateX4( m_fSpeedMax - m_fSpeedMin );

		fltx4 fl4LocalSpeedMinX = ReplicateX4( m_LocalCoordinateSystemSpeedMin.x );
		fltx4 fl4LocalSpeedXSpread = ReplicateX4( m_LocalCoordinateSystemSpeedMax.x - m_LocalCoordinateSystemSpeedMin.x );
		fltx4 fl4LocalSpeedMinY = ReplicateX4( m_LocalCoordinateSystemSpeedMin.y );
		fltx4 fl4LocalSpeedYSpread = ReplicateX4( m_LocalCoordinateSystemSpeedMax.y - m_LocalCoordinateSystemSpeedMin.y );
		fltx4 fl4LocalSpeedMinZ = ReplicateX4( m_LocalCoordinateSystemSpeedMin.z );
		fltx4 fl4LocalSpeedZSpread = ReplicateX4( m_LocalCoordinateSystemSpeedMax.z - m_LocalCoordinateSystemSpeedMin.z );

		FourVectors v4CPForward;
		v4CPForward.DuplicateVector( pParticles->ControlPoint( m_nControlPointNumber ).m_ForwardVector );
		FourVectors v4CPUp;
		v4CPUp.DuplicateVector( pParticles->ControlPoint( m_nControlPointNumber ).m_UpVector );
		FourVectors v4CPRight;
		v4CPRight.DuplicateVector( pParticles->ControlPoint( m_nControlPointNumber ).m_RightVector );

		fltx4 fl4PreviousDt = ReplicateX4( pParticles->m_flPreviousDt );

		while( n_blocks-- )
		{
			FourVectors v4RandPos;
			RandomPointInUnitSphere( nContext, v4RandPos );

			fltx4 fl4Length = SqrtSIMD( v4RandPos * v4RandPos );

			// Absolute value and biasing for creating hemispheres and ovoids.
			v4RandPos.x = AndSIMD( v4RandPos.x, v4ConditionalAbsMask.x );
			v4RandPos.y = AndSIMD( v4RandPos.y, v4ConditionalAbsMask.y );
			v4RandPos.z = AndSIMD( v4RandPos.z, v4ConditionalAbsMask.z );
			v4RandPos *= v4DistanceBias;
			v4RandPos.VectorNormalizeFast();

			FourVectors v4randDir = v4RandPos;

			// lerp radius
			v4RandPos *= AddSIMD( fl4RadiusMin, MulSIMD( fl4Length, fl4RadiusSpread ) );
			v4RandPos += v4PrevControlPointPosition;
			FourVectors cpnt = v4ControlPointDelta;
			cpnt *= MulSIMD( SubSIMD( *pCT, fl4PrevTime ), fl4OODt );
			v4RandPos += cpnt;
			*( pXYZ ) = v4RandPos;

			if ( nAttributeWriteMask & PARTICLE_ATTRIBUTE_PREV_XYZ_MASK )
			{
				if ( bDoRandSpeed )
				{
					fltx4 fl4Rand_speed = Pow_FixedPoint_Exponent_SIMD( RandSIMD( nContext ), nPowSSEMask );
					fl4Rand_speed = AddSIMD( fl4SpeedMin, MulSIMD( fl4SpeedRange, fl4Rand_speed ) );
					v4randDir *= fl4Rand_speed;

					// local speed
					FourVectors v4LocalOffset = v4CPForward;
					v4LocalOffset *= AddSIMD( fl4LocalSpeedMinX, MulSIMD( fl4LocalSpeedXSpread, RandSIMD( nContext ) ) );
					v4randDir += v4LocalOffset;

					v4LocalOffset = v4CPRight;
					v4LocalOffset *= AddSIMD( fl4LocalSpeedMinY, MulSIMD( fl4LocalSpeedYSpread, RandSIMD( nContext ) ) );
					v4randDir += v4LocalOffset;

					v4LocalOffset = v4CPUp;
					v4LocalOffset *= AddSIMD( fl4LocalSpeedMinZ, MulSIMD( fl4LocalSpeedZSpread, RandSIMD( nContext ) ) );
					v4randDir += v4LocalOffset;

					v4randDir *= fl4PreviousDt;
					v4RandPos -= v4randDir;
				}
				*( pPrevXYZ ) = v4RandPos;
			}
			++pXYZ;
			++pPrevXYZ;
			++pCT;
		}
		ReleaseSIMDRandContext( nContext );
	}
	else
	{
		InitNewParticlesScalar( pParticles, 4*start_block, 4*n_blocks, nAttributeWriteMask, pContext );
	}
}


//-----------------------------------------------------------------------------
// Random position within a box
//-----------------------------------------------------------------------------
class C_INIT_CreateWithinBox : public CParticleInitializerOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_INIT_CreateWithinBox );

	Vector m_vecMin;
	Vector m_vecMax;
	int m_nControlPointNumber;
	bool m_bLocalSpace;

	uint32 GetWrittenAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_XYZ_MASK | PARTICLE_ATTRIBUTE_PREV_XYZ_MASK;
	}

	uint32 GetReadAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_CREATION_TIME_MASK;
	}

	virtual uint64 GetReadControlPointMask() const
	{
		return 1ULL << m_nControlPointNumber;
	}

	virtual void InitNewParticlesScalar( CParticleCollection *pParticles, int start_p,
										 int nParticleCount, int nAttributeWriteMask,
										 void *pContext) const;

	virtual void Render( CParticleCollection *pParticles ) const;
};

DEFINE_PARTICLE_OPERATOR( C_INIT_CreateWithinBox, "Position Within Box Random", OPERATOR_PI_POSITION );

BEGIN_PARTICLE_INITIALIZER_OPERATOR_UNPACK( C_INIT_CreateWithinBox )
	DMXELEMENT_UNPACK_FIELD( "min", "0 0 0", Vector, m_vecMin )
	DMXELEMENT_UNPACK_FIELD( "max", "0 0 0", Vector, m_vecMax )
	DMXELEMENT_UNPACK_FIELD( "control point number", "0", int, m_nControlPointNumber )
	DMXELEMENT_UNPACK_FIELD( "use local space", "0", bool, m_bLocalSpace )
END_PARTICLE_OPERATOR_UNPACK( C_INIT_CreateWithinBox )

void C_INIT_CreateWithinBox::InitNewParticlesScalar(
	CParticleCollection *pParticles, int start_p,
	int nParticleCount, int nAttributeWriteMask, void *pContext ) const
{
	int nControlPointNumber = clamp( m_nControlPointNumber, 0, MAX_PARTICLE_CONTROL_POINTS - 1 );
	for( ; nParticleCount--; start_p++ )
	{
		float *xyz = pParticles->GetFloatAttributePtrForWrite( PARTICLE_ATTRIBUTE_XYZ, start_p );
		const float *ct = pParticles->GetFloatAttributePtr( PARTICLE_ATTRIBUTE_CREATION_TIME, start_p );
		float *pxyz = pParticles->GetFloatAttributePtrForWrite( PARTICLE_ATTRIBUTE_PREV_XYZ, start_p );

		Vector randpos;
		pParticles->RandomVector( m_vecMin, m_vecMax, &randpos );

		if ( m_bLocalSpace )
		{
			matrix3x4_t mat;
			pParticles->GetControlPointTransformAtTime( m_nControlPointNumber, *ct, &mat );
			Vector vecTransformLocal = vec3_origin;
			VectorRotate( randpos, mat, vecTransformLocal );
			randpos = vecTransformLocal;
		}
		else
		{
			Vector vecControlPoint;
			pParticles->GetControlPointAtTime( nControlPointNumber, *ct, &vecControlPoint );
			randpos += vecControlPoint;
		}

		xyz[0] = randpos.x;
		xyz[4] = randpos.y;
		xyz[8] = randpos.z;
		if ( pxyz && ( nAttributeWriteMask & PARTICLE_ATTRIBUTE_PREV_XYZ_MASK ) )
		{
			pxyz[0] = randpos.x;
			pxyz[4] = randpos.y;
			pxyz[8] = randpos.z;
		}
	}
}

void C_INIT_CreateWithinBox::Render( CParticleCollection *pParticles ) const
{
	Vector vecOrigin;
	pParticles->GetControlPointAtTime( m_nControlPointNumber, pParticles->m_flCurTime, &vecOrigin );
	RenderWireframeBox( vecOrigin, vec3_angle, m_vecMin, m_vecMax, Color( 192, 192, 0, 255 ), false );
}


//-----------------------------------------------------------------------------
// Position Offset Initializer
// offsets initial position of particles within a random vector range,
// while still respecting spherical/conical spacial and velocity initialization
//-----------------------------------------------------------------------------
class C_INIT_PositionOffset : public CParticleInitializerOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_INIT_PositionOffset );

	Vector m_OffsetMin;
	Vector m_OffsetMax;
	int m_nControlPointNumber;
	bool m_bLocalCoords;
	bool m_bProportional;

	uint32 GetWrittenAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_XYZ_MASK | PARTICLE_ATTRIBUTE_PREV_XYZ_MASK;
	}

	uint32 GetReadAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_XYZ_MASK | PARTICLE_ATTRIBUTE_PREV_XYZ_MASK | PARTICLE_ATTRIBUTE_CREATION_TIME_MASK | PARTICLE_ATTRIBUTE_RADIUS_MASK;
	}

	virtual uint64 GetReadControlPointMask() const
	{
		return 1ULL << m_nControlPointNumber;
	}

	virtual void InitParams( CParticleSystemDefinition *pDef )
	{
		m_nControlPointNumber = clamp( m_nControlPointNumber, 0, MAX_PARTICLE_CONTROL_POINTS - 1 );
	}

	bool InitMultipleOverride ( void ) { return true; }

	virtual void InitNewParticlesScalar( CParticleCollection *pParticles, int start_p,
										 int nParticleCount, int nAttributeWriteMask,
										 void *pContext) const;

	virtual void Render( CParticleCollection *pParticles ) const;
};

DEFINE_PARTICLE_OPERATOR( C_INIT_PositionOffset, "Position Modify Offset Random", OPERATOR_GENERIC );

BEGIN_PARTICLE_INITIALIZER_OPERATOR_UNPACK( C_INIT_PositionOffset )
	DMXELEMENT_UNPACK_FIELD( "control_point_number", "0", int, m_nControlPointNumber )
	DMXELEMENT_UNPACK_FIELD( "offset min", "0 0 0", Vector, m_OffsetMin )
	DMXELEMENT_UNPACK_FIELD( "offset max", "0 0 0", Vector, m_OffsetMax )
	DMXELEMENT_UNPACK_FIELD( "offset in local space 0/1", "0", bool, m_bLocalCoords )
	DMXELEMENT_UNPACK_FIELD( "offset proportional to radius 0/1", "0", bool, m_bProportional )
END_PARTICLE_OPERATOR_UNPACK( C_INIT_PositionOffset )

void C_INIT_PositionOffset::InitNewParticlesScalar(
	CParticleCollection *pParticles, int start_p,
	int nParticleCount, int nAttributeWriteMask, void *pContext ) const
{
	for( ; nParticleCount--; start_p++ )
	{
		float *xyz = pParticles->GetFloatAttributePtrForWrite( PARTICLE_ATTRIBUTE_XYZ, start_p );
		float *pxyz = pParticles->GetFloatAttributePtrForWrite( PARTICLE_ATTRIBUTE_PREV_XYZ, start_p );
		const float *ct = pParticles->GetFloatAttributePtr( PARTICLE_ATTRIBUTE_CREATION_TIME, start_p );
		const float *radius = pParticles->GetFloatAttributePtr( PARTICLE_ATTRIBUTE_RADIUS, start_p );

		Vector randpos;
		if ( m_bProportional )
		{
			pParticles->RandomVector( ( m_OffsetMin * *radius ), ( m_OffsetMax * *radius ), &randpos );
		}
		else
		{
			pParticles->RandomVector( m_OffsetMin, m_OffsetMax, &randpos );
		}

		if ( m_bLocalCoords )
		{
			matrix3x4_t mat;
			pParticles->GetControlPointTransformAtTime( m_nControlPointNumber, *ct, &mat );
			Vector vecTransformLocal = vec3_origin;
			VectorRotate( randpos, mat, vecTransformLocal );
			randpos = vecTransformLocal;
		}

		xyz[0] += randpos.x;
		xyz[4] += randpos.y;
		xyz[8] += randpos.z;
		pxyz[0] += randpos.x;
		pxyz[4] += randpos.y;
		pxyz[8] += randpos.z;
	}
}

void C_INIT_PositionOffset::Render( CParticleCollection *pParticles ) const
{
	Vector vecOrigin( 0, 0, 0 );
	Vector vecMinExtent = m_OffsetMin;
	Vector vecMaxExtent = m_OffsetMax;
	if ( m_bLocalCoords )
	{
		matrix3x4_t mat;
		pParticles->GetControlPointTransformAtTime( m_nControlPointNumber, pParticles->m_flCurTime, &mat );
		VectorRotate( m_OffsetMin, mat, vecMinExtent );
		VectorRotate( m_OffsetMax, mat, vecMaxExtent );
	}
	else
	{
		pParticles->GetControlPointAtTime( m_nControlPointNumber, pParticles->m_flCurTime, &vecOrigin );
	}
	RenderWireframeBox( vecOrigin, vec3_angle, vecMinExtent, vecMaxExtent, Color( 192, 192, 0, 255 ), false );
}


//-----------------------------------------------------------------------------
// Moves particles down onto the ground
//-----------------------------------------------------------------------------
class C_INIT_PositionPlaceOnGround : public CParticleInitializerOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_INIT_PositionPlaceOnGround );

	float m_flOffset;
	float m_flMaxTraceLength;
	char m_CollisionGroupName[128];
	int m_nCollisionGroupNumber;
	unsigned int m_CollisionMask;
	bool m_bKill;
	bool m_bIncludeWater;

	uint32 GetWrittenAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_XYZ_MASK | PARTICLE_ATTRIBUTE_PREV_XYZ_MASK | PARTICLE_ATTRIBUTE_LIFE_DURATION_MASK;
	}

	uint32 GetReadAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_XYZ_MASK | PARTICLE_ATTRIBUTE_PREV_XYZ_MASK;
	}

	virtual void InitParams( CParticleSystemDefinition *pDef )
	{
		m_nCollisionGroupNumber = g_pParticleSystemMgr->Query()->GetCollisionGroupFromName( m_CollisionGroupName );
		m_CollisionMask = m_bIncludeWater ? ( MASK_SHOT_HULL | MASK_SPLITAREAPORTAL ) : MASK_SHOT_HULL;
	}

	bool InitMultipleOverride ( void ) { return true; }

	virtual void InitNewParticlesScalar( CParticleCollection *pParticles, int start_p,
										 int nParticleCount, int nAttributeWriteMask,
										 void *pContext) const;
};

DEFINE_PARTICLE_OPERATOR( C_INIT_PositionPlaceOnGround, "Position Modify Place On Ground", OPERATOR_GENERIC );

BEGIN_PARTICLE_INITIALIZER_OPERATOR_UNPACK( C_INIT_PositionPlaceOnGround )
	DMXELEMENT_UNPACK_FIELD( "offset", "0", float, m_flOffset )
	DMXELEMENT_UNPACK_FIELD( "kill on no collision", "0", bool, m_bKill )
	DMXELEMENT_UNPACK_FIELD( "include water", "0", bool, m_bIncludeWater )
	DMXELEMENT_UNPACK_FIELD( "max trace length", "128", float, m_flMaxTraceLength )
	DMXELEMENT_UNPACK_FIELD_STRING( "collision group", "NONE", m_CollisionGroupName )
END_PARTICLE_OPERATOR_UNPACK( C_INIT_PositionPlaceOnGround )

void C_INIT_PositionPlaceOnGround::InitNewParticlesScalar(
	CParticleCollection *pParticles, int start_p,
	int nParticleCount, int nAttributeWriteMask, void *pContext ) const
{
	for( ; nParticleCount--; start_p++ )
	{
		float *xyz = pParticles->GetFloatAttributePtrForWrite( PARTICLE_ATTRIBUTE_XYZ, start_p );
		float *pxyz = pParticles->GetFloatAttributePtrForWrite( PARTICLE_ATTRIBUTE_PREV_XYZ, start_p );
		float *plife = pParticles->GetFloatAttributePtrForWrite( PARTICLE_ATTRIBUTE_LIFE_DURATION, start_p );

		Vector vecStartPos( xyz[0], xyz[4], xyz[8] );

		CBaseTrace tr;
		g_pParticleSystemMgr->Query()->TraceLine( vecStartPos,
			( vecStartPos + ( Vector( 0, 0, -1 ) * m_flMaxTraceLength ) ),
			m_CollisionMask, NULL, m_nCollisionGroupNumber, &tr );
		if ( tr.fraction == 1.0 && m_bKill )
		{
			*plife = -1.0;
		}
		else
		{
			float flOffset = ( m_flMaxTraceLength * tr.fraction ) - m_flOffset;
			xyz[8] -= flOffset;
			pxyz[8] -= flOffset;
		}
	}
}


//-----------------------------------------------------------------------------
// Velocity initializer
//-----------------------------------------------------------------------------
class C_INIT_VelocityRandom : public CParticleInitializerOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_INIT_VelocityRandom );

	int m_nControlPointNumber;
	float m_fSpeedMin;
	float m_fSpeedMax;
	Vector m_LocalCoordinateSystemSpeedMin;
	Vector m_LocalCoordinateSystemSpeedMax;
	bool m_bHasLocalSpeed;

	uint32 GetWrittenAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_PREV_XYZ_MASK;
	}

	uint32 GetReadAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_PREV_XYZ_MASK | PARTICLE_ATTRIBUTE_CREATION_TIME_MASK;
	}

	virtual uint64 GetReadControlPointMask() const
	{
		if ( m_bHasLocalSpeed )
			return 1ULL << m_nControlPointNumber;
		return 0;
	}

	bool InitMultipleOverride ( void ) { return true; }

	virtual void InitParams( CParticleSystemDefinition *pDef )
	{
		m_nControlPointNumber = clamp( m_nControlPointNumber, 0, MAX_PARTICLE_CONTROL_POINTS - 1 );
		m_bHasLocalSpeed = ( m_LocalCoordinateSystemSpeedMin != vec3_origin ) || ( m_LocalCoordinateSystemSpeedMax != vec3_origin );
		if ( m_fSpeedMin > m_fSpeedMax )
		{
			V_swap( m_fSpeedMin, m_fSpeedMax );
		}
	}

	virtual void InitNewParticlesScalar( CParticleCollection *pParticles, int start_p,
										 int nParticleCount, int nAttributeWriteMask,
										 void *pContext) const;
};

DEFINE_PARTICLE_OPERATOR( C_INIT_VelocityRandom, "Velocity Random", OPERATOR_GENERIC );

BEGIN_PARTICLE_INITIALIZER_OPERATOR_UNPACK( C_INIT_VelocityRandom )
	DMXELEMENT_UNPACK_FIELD( "control_point_number", "0", int, m_nControlPointNumber )
	DMXELEMENT_UNPACK_FIELD( "random_speed_min", "0", float, m_fSpeedMin )
	DMXELEMENT_UNPACK_FIELD( "random_speed_max", "0", float, m_fSpeedMax )
	DMXELEMENT_UNPACK_FIELD( "speed_in_local_coordinate_system_min", "0 0 0", Vector, m_LocalCoordinateSystemSpeedMin )
	DMXELEMENT_UNPACK_FIELD( "speed_in_local_coordinate_system_max", "0 0 0", Vector, m_LocalCoordinateSystemSpeedMax )
END_PARTICLE_OPERATOR_UNPACK( C_INIT_VelocityRandom )

void C_INIT_VelocityRandom::InitNewParticlesScalar(
	CParticleCollection *pParticles, int start_p,
	int nParticleCount, int nAttributeWriteMask, void *pContext ) const
{
	for( ; nParticleCount--; start_p++ )
	{
		const float *ct = pParticles->GetFloatAttributePtr( PARTICLE_ATTRIBUTE_CREATION_TIME, start_p );
		float *pxyz = pParticles->GetFloatAttributePtrForWrite( PARTICLE_ATTRIBUTE_PREV_XYZ, start_p );

		Vector vecVelocity( 0.0f, 0.0f, 0.0f );
		if ( m_bHasLocalSpeed )
		{
			Vector vecRandomSpeed, vecForward, vecUp, vecRight;
			pParticles->RandomVector( m_LocalCoordinateSystemSpeedMin, m_LocalCoordinateSystemSpeedMax, &vecRandomSpeed );
			pParticles->GetControlPointOrientationAtTime( m_nControlPointNumber, *ct, &vecForward, &vecRight, &vecUp );
			VectorScale( vecForward, vecRandomSpeed.x, vecVelocity );
			VectorMA( vecVelocity, -vecRandomSpeed.y, vecRight, vecVelocity );
			VectorMA( vecVelocity, vecRandomSpeed.z, vecUp, vecVelocity );
		}

		if ( m_fSpeedMax > 0.0f )
		{
			Vector vecRandomSpeed;
			pParticles->RandomVector( m_fSpeedMin, m_fSpeedMax, &vecRandomSpeed );
			vecVelocity += vecRandomSpeed;
		}

		vecVelocity *= pParticles->m_flPreviousDt;
		pxyz[0] -= vecVelocity.x;
		pxyz[4] -= vecVelocity.y;
		pxyz[8] -= vecVelocity.z;
	}
}


//-----------------------------------------------------------------------------
// Initial Velocity Noise Operator
//-----------------------------------------------------------------------------
class C_INIT_InitialVelocityNoise : public CParticleInitializerOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_INIT_InitialVelocityNoise );

	uint32 GetWrittenAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_PREV_XYZ_MASK;
	}

	uint32 GetReadAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_XYZ_MASK | PARTICLE_ATTRIBUTE_PREV_XYZ_MASK | PARTICLE_ATTRIBUTE_CREATION_TIME_MASK;
	}

	virtual uint64 GetReadControlPointMask() const
	{
		return 1ULL << m_nControlPointNumber;
	}

	virtual void InitParams( CParticleSystemDefinition *pDef )
	{
		m_nControlPointNumber = clamp( m_nControlPointNumber, 0, MAX_PARTICLE_CONTROL_POINTS - 1 );
	}

	bool InitMultipleOverride ( void ) { return true; }

	virtual void InitNewParticlesScalar( CParticleCollection *pParticles, int start_p,
										 int nParticleCount, int nAttributeWriteMask,
										 void *pContext) const;

	virtual void InitNewParticlesBlock( CParticleCollection *pParticles,
										int start_block, int n_blocks, int nAttributeWriteMask,
										void *pContext ) const;

	Vector	m_vecAbsVal, m_vecAbsValInv, m_vecOffsetLoc;
	float	m_flOffset;
	Vector	m_vecOutputMin;
	Vector	m_vecOutputMax;
	float	m_flNoiseScale, m_flNoiseScaleLoc;
	int		nRemainingBlocks, m_nControlPointNumber;
	bool	m_bLocalSpace;
};

DEFINE_PARTICLE_OPERATOR( C_INIT_InitialVelocityNoise, "Velocity Noise", OPERATOR_GENERIC );

BEGIN_PARTICLE_INITIALIZER_OPERATOR_UNPACK( C_INIT_InitialVelocityNoise )
	DMXELEMENT_UNPACK_FIELD( "Control Point Number", "0", int, m_nControlPointNumber )
	DMXELEMENT_UNPACK_FIELD( "Time Noise Coordinate Scale", "1", float, m_flNoiseScale )
	DMXELEMENT_UNPACK_FIELD( "Spatial Noise Coordinate Scale", "0.01", float, m_flNoiseScaleLoc )
	DMXELEMENT_UNPACK_FIELD( "Time Coordinate Offset", "0", float, m_flOffset )
	DMXELEMENT_UNPACK_FIELD( "Spatial Coordinate Offset", "0 0 0", Vector, m_vecOffsetLoc )
	DMXELEMENT_UNPACK_FIELD( "Absolute Value", "0 0 0", Vector, m_vecAbsVal )
	DMXELEMENT_UNPACK_FIELD( "Invert Abs Value", "0 0 0", Vector, m_vecAbsValInv )
	DMXELEMENT_UNPACK_FIELD( "output minimum", "0 0 0", Vector, m_vecOutputMin )
	DMXELEMENT_UNPACK_FIELD( "output maximum", "1 1 1", Vector, m_vecOutputMax )
	DMXELEMENT_UNPACK_FIELD( "Apply Velocity in Local Space (0/1)", "0", bool, m_bLocalSpace )
END_PARTICLE_OPERATOR_UNPACK( C_INIT_InitialVelocityNoise )

void C_INIT_InitialVelocityNoise::InitNewParticlesBlock( CParticleCollection *pParticles,
														int start_block, int n_blocks, int nAttributeWriteMask,
														void *pContext ) const
{
	// Set up the masks for absolute values
	fltx4 fl4AbsValX = CmpEqSIMD( Four_Zeros, Four_Zeros );
	fltx4 fl4AbsValY = fl4AbsValX;
	fltx4 fl4AbsValZ = fl4AbsValX;
	float flAbsScaleX = 0.5f;
	float flAbsScaleY = 0.5f;
	float flAbsScaleZ = 0.5f;

	bool m_bNoiseAbs = ( m_vecAbsValInv.x != 0.0f ) || ( m_vecAbsValInv.y != 0.0f ) || ( m_vecAbsValInv.z != 0.0f );

	// Set up values for any absolute value operations
	if ( m_vecAbsVal.x != 0.0f )
	{
		fl4AbsValX = LoadAlignedSIMD( (const float *) g_SIMD_clear_signmask );
		flAbsScaleX = 1.0f;
	}
	if ( m_vecAbsVal.y != 0.0f )
	{
		fl4AbsValY = LoadAlignedSIMD( (const float *) g_SIMD_clear_signmask );
		flAbsScaleY = 1.0f;
	}
	if ( m_vecAbsVal.z != 0.0f )
	{
		fl4AbsValZ = LoadAlignedSIMD( (const float *) g_SIMD_clear_signmask );
		flAbsScaleZ = 1.0f;
	}

	// Set up the output ranges
	fltx4 fl4ValueBaseX = ReplicateX4( m_vecOutputMin.x + ( ( 1.0f - flAbsScaleX ) * ( m_vecOutputMax.x - m_vecOutputMin.x ) ) );
	fltx4 fl4ValueBaseY = ReplicateX4( m_vecOutputMin.y + ( ( 1.0f - flAbsScaleY ) * ( m_vecOutputMax.y - m_vecOutputMin.y ) ) );
	fltx4 fl4ValueBaseZ = ReplicateX4( m_vecOutputMin.z + ( ( 1.0f - flAbsScaleZ ) * ( m_vecOutputMax.z - m_vecOutputMin.z ) ) );
	fltx4 fl4ValueScaleX = ReplicateX4( flAbsScaleX * ( m_vecOutputMax.x - m_vecOutputMin.x ) );
	fltx4 fl4ValueScaleY = ReplicateX4( flAbsScaleY * ( m_vecOutputMax.y - m_vecOutputMin.y ) );
	fltx4 fl4ValueScaleZ = ReplicateX4( flAbsScaleZ * ( m_vecOutputMax.z - m_vecOutputMin.z ) );

	float CoordScale = m_flNoiseScale;
	float CoordScaleLoc = m_flNoiseScaleLoc;

	size_t attr_stride;
	const FourVectors *xyz = pParticles->Get4VAttributePtr( PARTICLE_ATTRIBUTE_XYZ, &attr_stride );
	xyz += attr_stride * start_block;
	FourVectors *pxyz = pParticles->Get4VAttributePtrForWrite( PARTICLE_ATTRIBUTE_PREV_XYZ, &attr_stride );
	pxyz += attr_stride * start_block;
	const fltx4 *pCreationTime = pParticles->GetM128AttributePtr( PARTICLE_ATTRIBUTE_CREATION_TIME, &attr_stride );
	pCreationTime += attr_stride * start_block;

	// setup
	fltx4 fl4Offset = ReplicateX4( m_flOffset );
	FourVectors fvOffsetLoc;
	fvOffsetLoc.DuplicateVector( m_vecOffsetLoc );
	CParticleSIMDTransformation CPTransform;
	float flCreationTime = SubFloat( *pCreationTime, 0 );
	pParticles->GetControlPointTransformAtTime( m_nControlPointNumber, flCreationTime, &CPTransform );

	while( n_blocks-- )
	{
		FourVectors fvCoordLoc = *xyz;
		fvCoordLoc += fvOffsetLoc;
		fvCoordLoc *= ReplicateX4( CoordScaleLoc );

		fltx4 fl4OffsetTime = MulSIMD( AddSIMD( *pCreationTime, fl4Offset ), ReplicateX4( CoordScale ) );
		FourVectors fvCoord;
		fvCoord.x = AddSIMD( fvCoordLoc.x, fl4OffsetTime );
		fvCoord.y = AddSIMD( fvCoordLoc.y, fl4OffsetTime );
		fvCoord.z = AddSIMD( fvCoordLoc.z, fl4OffsetTime );

		FourVectors fvCoord2 = fvCoord;
		FourVectors fvOffsetTemp;
		fvOffsetTemp.DuplicateVector( Vector( 100000.5f, 300000.25f, 9000001.0f ) );
		fvCoord2 += fvOffsetTemp;
		FourVectors fvCoord3 = fvCoord;
		fvOffsetTemp.DuplicateVector( Vector( 110000.25f, 310000.75f, 9100000.0f ) );
		fvCoord3 += fvOffsetTemp;

		fltx4 fl4NoiseX = NoiseSIMD( fvCoord );
		fltx4 fl4NoiseY = NoiseSIMD( fvCoord2 );

		fl4NoiseX = AndSIMD( fl4NoiseX, fl4AbsValX );
		fl4NoiseY = AndSIMD( fl4NoiseY, fl4AbsValY );
		fltx4 fl4NoiseZ = NoiseSIMD( fvCoord3 );
		fl4NoiseZ = AndSIMD( fl4NoiseZ, fl4AbsValZ );

		// If we're inverting the absolute values, do it here
		if ( m_bNoiseAbs )
		{
			if ( m_vecAbsValInv.x != 0.0f )
			{
				fl4NoiseX = SubSIMD( Four_Ones, fl4NoiseX );
			}
			if ( m_vecAbsValInv.y != 0.0f )
			{
				fl4NoiseY = SubSIMD( Four_Ones, fl4NoiseY );
			}
			if ( m_vecAbsValInv.z != 0.0f )
			{
				fl4NoiseZ = SubSIMD( Four_Ones, fl4NoiseZ );
			}
		}

		FourVectors fvOffset;
		fvOffset.x = AddSIMD( fl4ValueBaseX, ( MulSIMD( fl4ValueScaleX, fl4NoiseX ) ) );
		fvOffset.y = AddSIMD( fl4ValueBaseY, ( MulSIMD( fl4ValueScaleY, fl4NoiseY ) ) );
		fvOffset.z = AddSIMD( fl4ValueBaseZ, ( MulSIMD( fl4ValueScaleZ, fl4NoiseZ ) ) );

		fvOffset *= pParticles->m_flPreviousDt;

		if ( m_bLocalSpace )
		{
			CPTransform.VectorRotate( fvOffset );
		}

		*pxyz -= fvOffset;

		xyz += attr_stride;
		pxyz += attr_stride;
		pCreationTime += attr_stride;
	}
}

void C_INIT_InitialVelocityNoise::InitNewParticlesScalar(
	CParticleCollection *pParticles, int start_p,
	int nParticleCount, int nAttributeWriteMask, void *pContext ) const
{
	int nAbsValX = 0xffffffff;
	int nAbsValY = 0xffffffff;
	int nAbsValZ = 0xffffffff;
	float flAbsScaleX = 0.5f;
	float flAbsScaleY = 0.5f;
	float flAbsScaleZ = 0.5f;

	bool m_bNoiseAbs = ( m_vecAbsValInv.x != 0.0f ) || ( m_vecAbsValInv.y != 0.0f ) || ( m_vecAbsValInv.z != 0.0f );

	// Set up values for any absolute value operations
	if ( m_vecAbsVal.x != 0.0f )
	{
		nAbsValX = 0x7FFFFFFF;
		flAbsScaleX = 1.0f;
	}
	if ( m_vecAbsVal.y != 0.0f )
	{
		nAbsValY = 0x7FFFFFFF;
		flAbsScaleY = 1.0f;
	}
	if ( m_vecAbsVal.z != 0.0f )
	{
		nAbsValZ = 0x7FFFFFFF;
		flAbsScaleZ = 1.0f;
	}

	float ValueScaleX = flAbsScaleX * ( m_vecOutputMax.x - m_vecOutputMin.x );
	float ValueBaseX = m_vecOutputMin.x + ( ( 1.0f - flAbsScaleX ) * ( m_vecOutputMax.x - m_vecOutputMin.x ) );
	float ValueScaleY = flAbsScaleY * ( m_vecOutputMax.y - m_vecOutputMin.y );
	float ValueBaseY = m_vecOutputMin.y + ( ( 1.0f - flAbsScaleY ) * ( m_vecOutputMax.y - m_vecOutputMin.y ) );
	float ValueScaleZ = flAbsScaleZ * ( m_vecOutputMax.z - m_vecOutputMin.z );
	float ValueBaseZ = m_vecOutputMin.z + ( ( 1.0f - flAbsScaleZ ) * ( m_vecOutputMax.z - m_vecOutputMin.z ) );

	float CoordScale = m_flNoiseScale;
	float CoordScaleLoc = m_flNoiseScaleLoc;

	for( ; nParticleCount--; start_p++ )
	{
		const float *xyz = pParticles->GetFloatAttributePtr( PARTICLE_ATTRIBUTE_XYZ, start_p );
		float *pxyz = pParticles->GetFloatAttributePtrForWrite( PARTICLE_ATTRIBUTE_PREV_XYZ, start_p );
		const float *pCreationTime = pParticles->GetFloatAttributePtr( PARTICLE_ATTRIBUTE_CREATION_TIME, start_p );

		Vector Coord, CoordLoc;
		CoordLoc.x = m_vecOffsetLoc.x + xyz[0];
		CoordLoc.y = xyz[4] + m_vecOffsetLoc.y;
		CoordLoc.z = xyz[8] + m_vecOffsetLoc.z;
		CoordLoc *= CoordScaleLoc;
		float flOffsetTime = ( *pCreationTime + m_flOffset ) * CoordScale;
		Coord.x = flOffsetTime + CoordLoc.x;
		Coord.y = flOffsetTime + CoordLoc.y;
		Coord.z = flOffsetTime + CoordLoc.z;

		FourVectors fvNoise;
		fvNoise.DuplicateVector( Coord );
		fltx4 flNoise128 = NoiseSIMD( fvNoise );
		float flNoiseX = SubFloat( flNoise128, 0 );

		fvNoise.DuplicateVector( Coord + Vector( 100000.5f, 300000.25f, 9000001.0f ) );
		flNoise128 = NoiseSIMD( fvNoise );
		float flNoiseY = SubFloat( flNoise128, 0 );

		fvNoise.DuplicateVector( Coord + Vector( 110000.25f, 310000.75f, 9100000.0f ) );
		flNoise128 = NoiseSIMD( fvNoise );
		float flNoiseZ = SubFloat( flNoise128, 0 );

		*( (int *) &flNoiseX ) &= nAbsValX;
		*( (int *) &flNoiseY ) &= nAbsValY;
		*( (int *) &flNoiseZ ) &= nAbsValZ;

		if ( m_bNoiseAbs )
		{
			if ( m_vecAbsValInv.x != 0.0f )
			{
				flNoiseX = 1.0f - flNoiseX;
			}
			if ( m_vecAbsValInv.y != 0.0f )
			{
				flNoiseY = 1.0f - flNoiseY;
			}
			if ( m_vecAbsValInv.z != 0.0f )
			{
				flNoiseZ = 1.0f - flNoiseZ;
			}
		}

		Vector poffset;
		poffset.x = ( ValueBaseX + ( ValueScaleX * flNoiseX ) );
		poffset.y = ( ValueBaseY + ( ValueScaleY * flNoiseY ) );
		poffset.z = ( ValueBaseZ + ( ValueScaleZ * flNoiseZ ) );

		poffset *= pParticles->m_flPreviousDt;

		if ( m_bLocalSpace )
		{
			matrix3x4_t mat;
			pParticles->GetControlPointTransformAtTime( m_nControlPointNumber, *pCreationTime, &mat );
			Vector vecTransformLocal = vec3_origin;
			VectorRotate( poffset, mat, vecTransformLocal );
			poffset = vecTransformLocal;
		}
		pxyz[0] -= poffset.x;
		pxyz[4] -= poffset.y;
		pxyz[8] -= poffset.z;
	}
}


//-----------------------------------------------------------------------------
// Random lifetime
//-----------------------------------------------------------------------------
class C_INIT_RandomLifeTime : public CParticleInitializerOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_INIT_RandomLifeTime );

	uint32 GetWrittenAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_LIFE_DURATION_MASK;
	}

	uint32 GetReadAttributes( void ) const
	{
		return 0;
	}

	virtual void InitNewParticlesScalar( CParticleCollection *pParticles, int start_p,
										 int nParticleCount, int nAttributeWriteMask,
										 void *pContext) const;

	virtual void InitNewParticlesBlock( CParticleCollection *pParticles,
										int start_block, int n_blocks, int nAttributeWriteMask,
										void *pContext ) const
	{
		InitScalarAttributeRandomRangeExpBlock( PARTICLE_ATTRIBUTE_LIFE_DURATION,
			m_fLifetimeMin, m_fLifetimeMax, m_fLifetimeRandExponent,
			pParticles, start_block, n_blocks );
	}

	float m_fLifetimeMin;
	float m_fLifetimeMax;
	float m_fLifetimeRandExponent;
};

DEFINE_PARTICLE_OPERATOR( C_INIT_RandomLifeTime, "Lifetime Random", OPERATOR_GENERIC );

BEGIN_PARTICLE_INITIALIZER_OPERATOR_UNPACK( C_INIT_RandomLifeTime )
	DMXELEMENT_UNPACK_FIELD( "lifetime_min", "0", float, m_fLifetimeMin )
	DMXELEMENT_UNPACK_FIELD( "lifetime_max", "0", float, m_fLifetimeMax )
	DMXELEMENT_UNPACK_FIELD( "lifetime_random_exponent", "1", float, m_fLifetimeRandExponent )
END_PARTICLE_OPERATOR_UNPACK( C_INIT_RandomLifeTime )

void C_INIT_RandomLifeTime::InitNewParticlesScalar(
	CParticleCollection *pParticles, int start_p,
	int nParticleCount, int nAttributeWriteMask, void *pContext ) const
{
	InitScalarAttributeRandomRangeExpScalar( PARTICLE_ATTRIBUTE_LIFE_DURATION, m_fLifetimeMin, m_fLifetimeMax, m_fLifetimeRandExponent,
		pParticles, start_p, nParticleCount );
}


//-----------------------------------------------------------------------------
// Random radius
//-----------------------------------------------------------------------------
class C_INIT_RandomRadius : public CParticleInitializerOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_INIT_RandomRadius );

	uint32 GetWrittenAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_RADIUS_MASK;
	}

	uint32 GetReadAttributes( void ) const
	{
		return 0;
	}

	virtual void InitNewParticlesScalar( CParticleCollection *pParticles, int start_p,
										 int nParticleCount, int nAttributeWriteMask,
										 void *pContext) const
	{
		InitScalarAttributeRandomRangeExpScalar( PARTICLE_ATTRIBUTE_RADIUS, m_flRadiusMin, m_flRadiusMax, m_flRadiusRandExponent,
			pParticles, start_p, nParticleCount );
	}

	virtual void InitNewParticlesBlock( CParticleCollection *pParticles,
										int start_block, int n_blocks, int nAttributeWriteMask,
										void *pContext ) const
	{
		InitScalarAttributeRandomRangeExpBlock( PARTICLE_ATTRIBUTE_RADIUS, m_flRadiusMin, m_flRadiusMax, m_flRadiusRandExponent,
			pParticles, start_block, n_blocks );
	}

	float m_flRadiusMin;
	float m_flRadiusMax;
	float m_flRadiusRandExponent;
};

DEFINE_PARTICLE_OPERATOR( C_INIT_RandomRadius, "Radius Random", OPERATOR_PI_RADIUS );

BEGIN_PARTICLE_INITIALIZER_OPERATOR_UNPACK( C_INIT_RandomRadius )
	DMXELEMENT_UNPACK_FIELD( "radius_min", "1", float, m_flRadiusMin )
	DMXELEMENT_UNPACK_FIELD( "radius_max", "1", float, m_flRadiusMax )
	DMXELEMENT_UNPACK_FIELD( "radius_random_exponent", "1", float, m_flRadiusRandExponent )
END_PARTICLE_OPERATOR_UNPACK( C_INIT_RandomRadius )


//-----------------------------------------------------------------------------
// Random alpha
//-----------------------------------------------------------------------------
class C_INIT_RandomAlpha : public CParticleInitializerOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_INIT_RandomAlpha );

	uint32 GetWrittenAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_ALPHA_MASK;
	}

	uint32 GetReadAttributes( void ) const
	{
		return 0;
	}

	virtual void InitParams( CParticleSystemDefinition *pDef )
	{
		m_flAlphaMin = m_nAlphaMin * ( 1.0f / 255.0f );
		m_flAlphaMax = m_nAlphaMax * ( 1.0f / 255.0f );
	}

	virtual void InitNewParticlesBlock( CParticleCollection *pParticles,
										int start_block, int n_blocks, int nAttributeWriteMask,
										void *pContext ) const
	{
		InitScalarAttributeRandomRangeExpBlock( PARTICLE_ATTRIBUTE_ALPHA, m_flAlphaMin, m_flAlphaMax, m_flAlphaRandExponent,
			pParticles, start_block, n_blocks );
	}

	virtual void InitNewParticlesScalar( CParticleCollection *pParticles, int start_p,
										 int nParticleCount, int nAttributeWriteMask,
										 void *pContext) const
	{
		InitScalarAttributeRandomRangeExpScalar( PARTICLE_ATTRIBUTE_ALPHA, m_flAlphaMin, m_flAlphaMax, m_flAlphaRandExponent,
			pParticles, start_p, nParticleCount );
	}

	int m_nAlphaMin;
	int m_nAlphaMax;
	float m_flAlphaMin;
	float m_flAlphaMax;
	float m_flAlphaRandExponent;
};

DEFINE_PARTICLE_OPERATOR( C_INIT_RandomAlpha, "Alpha Random", OPERATOR_PI_ALPHA );

BEGIN_PARTICLE_INITIALIZER_OPERATOR_UNPACK( C_INIT_RandomAlpha )
	DMXELEMENT_UNPACK_FIELD( "alpha_min", "255", int, m_nAlphaMin )
	DMXELEMENT_UNPACK_FIELD( "alpha_max", "255", int, m_nAlphaMax )
	DMXELEMENT_UNPACK_FIELD( "alpha_random_exponent", "1", float, m_flAlphaRandExponent )
END_PARTICLE_OPERATOR_UNPACK( C_INIT_RandomAlpha )


//-----------------------------------------------------------------------------
// Random rotation (base classes)
//-----------------------------------------------------------------------------
class CGeneralRandomRotation : public CParticleInitializerOperatorInstance
{
protected:
	virtual int GetAttributeToInit( void ) const = 0;

	uint32 GetWrittenAttributes( void ) const
	{
		return (1 << GetAttributeToInit() );
	}

	uint32 GetReadAttributes( void ) const
	{
		return 0;
	}

	virtual void InitParams( CParticleSystemDefinition *pDef )
	{
		m_flRadians = m_flDegrees * ( M_PI / 180.0 );
		m_flRadiansMin = m_flDegreesMin * ( M_PI / 180.0 );
		m_flRadiansMax = m_flDegreesMax * ( M_PI / 180.0 );
	}

	virtual void InitNewParticlesBlock( CParticleCollection *pParticles,
										int start_block, int n_blocks, int nAttributeWriteMask,
										void *pContext ) const
	{
		InitScalarAttributeRandomRangeExpBlock( GetAttributeToInit(), m_flRadiansMin, m_flRadiansMax, m_flRotationRandExponent,
			pParticles, start_block, n_blocks );
	}

	virtual void InitNewParticlesScalar( CParticleCollection *pParticles, int start_p,
										 int nParticleCount, int nAttributeWriteMask,
										 void *pContext) const
	{
		for( ; nParticleCount--; start_p++ )
		{
			float *drot = pParticles->GetFloatAttributePtrForWrite( GetAttributeToInit(), start_p );
			*drot = pParticles->RandomFloatExp( m_flRadiansMin, m_flRadiansMax, m_flRotationRandExponent ) + m_flRadians;
		}
	}

	// User-specified range
	float m_flDegreesMin;
	float m_flDegreesMax;
	float m_flDegrees;

	// Converted range
	float m_flRadiansMin;
	float m_flRadiansMax;
	float m_flRadians;
	float m_flRotationRandExponent;
};

class CAddGeneralRandomRotation : public CParticleInitializerOperatorInstance
{
protected:
	virtual int GetAttributeToInit( void ) const = 0;

	uint32 GetWrittenAttributes( void ) const
	{
		return (1 << GetAttributeToInit() );
	}

	uint32 GetReadAttributes( void ) const
	{
		return (1 << GetAttributeToInit() );
	}

	bool InitMultipleOverride ( void ) { return true; }

	virtual void InitParams( CParticleSystemDefinition *pDef )
	{
		m_flRadians = m_flDegrees * ( M_PI / 180.0 );
		m_flRadiansMin = m_flDegreesMin * ( M_PI / 180.0 );
		m_flRadiansMax = m_flDegreesMax * ( M_PI / 180.0 );
	}

	virtual void InitNewParticlesBlock( CParticleCollection *pParticles,
										int start_block, int n_blocks, int nAttributeWriteMask,
										void *pContext ) const
	{
		AddScalarAttributeRandomRangeBlock( GetAttributeToInit(), m_flRadiansMin, m_flRadiansMax, m_flRotationRandExponent,
			pParticles, start_block, n_blocks, m_bRandomlyFlipDirection );
	}

	virtual void InitNewParticlesScalar( CParticleCollection *pParticles, int start_p,
										 int nParticleCount, int nAttributeWriteMask,
										 void *pContext) const
	{
		if ( !m_bRandomlyFlipDirection )
		{
			for( ; nParticleCount--; start_p++ )
			{
				float *pAttr = pParticles->GetFloatAttributePtrForWrite( GetAttributeToInit(), start_p );
				*pAttr = ( pParticles->RandomFloatExp( m_flRadiansMin, m_flRadiansMax, m_flRotationRandExponent ) + *pAttr ) + m_flRadians;
			}
		}
		else
		{
			for( ; nParticleCount--; start_p++ )
			{
				float *pAttr = pParticles->GetFloatAttributePtrForWrite( GetAttributeToInit(), start_p );
				float flSpeed = pParticles->RandomFloatExp( m_flRadiansMin, m_flRadiansMax, m_flRotationRandExponent ) + m_flRadians;
				bool bFlip = ( pParticles->RandomFloat( -1.0f, 1.0f ) >= 0.0f );
				*pAttr += bFlip ? -flSpeed : flSpeed;
			}
		}
	}

	// User-specified range
	float m_flDegreesMin;
	float m_flDegreesMax;
	float m_flDegrees;

	// Converted range
	float m_flRadiansMin;
	float m_flRadiansMax;
	float m_flRadians;
	float m_flRotationRandExponent;
	bool m_bRandomlyFlipDirection;
};


//-----------------------------------------------------------------------------
// Random rotation
//-----------------------------------------------------------------------------
class C_INIT_RandomRotation : public CGeneralRandomRotation
{
	DECLARE_PARTICLE_OPERATOR( C_INIT_RandomRotation );

	virtual int GetAttributeToInit( void ) const
	{
		return PARTICLE_ATTRIBUTE_ROTATION;
	}
};

DEFINE_PARTICLE_OPERATOR( C_INIT_RandomRotation, "Rotation Random", OPERATOR_PI_ROTATION );

BEGIN_PARTICLE_INITIALIZER_OPERATOR_UNPACK( C_INIT_RandomRotation )
	DMXELEMENT_UNPACK_FIELD( "rotation_initial", "0", float, m_flDegrees )
	DMXELEMENT_UNPACK_FIELD( "rotation_offset_min", "0", float, m_flDegreesMin )
	DMXELEMENT_UNPACK_FIELD( "rotation_offset_max", "360", float, m_flDegreesMax )
	DMXELEMENT_UNPACK_FIELD( "rotation_random_exponent", "1", float, m_flRotationRandExponent )
END_PARTICLE_OPERATOR_UNPACK( C_INIT_RandomRotation )


//-----------------------------------------------------------------------------
// Random rotation speed
//-----------------------------------------------------------------------------
class C_INIT_RandomRotationSpeed : public CAddGeneralRandomRotation
{
	DECLARE_PARTICLE_OPERATOR( C_INIT_RandomRotationSpeed );

	virtual int GetAttributeToInit( void ) const
	{
		return PARTICLE_ATTRIBUTE_ROTATION_SPEED;
	}
};

DEFINE_PARTICLE_OPERATOR( C_INIT_RandomRotationSpeed, "Rotation Speed Random", OPERATOR_GENERIC );

BEGIN_PARTICLE_INITIALIZER_OPERATOR_UNPACK( C_INIT_RandomRotationSpeed )
	DMXELEMENT_UNPACK_FIELD( "rotation_speed_constant", "0", float, m_flDegrees )
	DMXELEMENT_UNPACK_FIELD( "rotation_speed_random_min", "0", float, m_flDegreesMin )
	DMXELEMENT_UNPACK_FIELD( "rotation_speed_random_max", "360", float, m_flDegreesMax )
	DMXELEMENT_UNPACK_FIELD( "rotation_speed_random_exponent", "1", float, m_flRotationRandExponent )
	DMXELEMENT_UNPACK_FIELD( "randomly_flip_direction", "1", bool, m_bRandomlyFlipDirection )
END_PARTICLE_OPERATOR_UNPACK( C_INIT_RandomRotationSpeed )


//-----------------------------------------------------------------------------
// Random yaw
//-----------------------------------------------------------------------------
class C_INIT_RandomYaw : public CGeneralRandomRotation
{
	DECLARE_PARTICLE_OPERATOR( C_INIT_RandomYaw );

	virtual int GetAttributeToInit( void ) const
	{
		return PARTICLE_ATTRIBUTE_YAW;
	}
};

DEFINE_PARTICLE_OPERATOR( C_INIT_RandomYaw, "Rotation Yaw Random", OPERATOR_PI_YAW );

BEGIN_PARTICLE_INITIALIZER_OPERATOR_UNPACK( C_INIT_RandomYaw )
	DMXELEMENT_UNPACK_FIELD( "yaw_initial", "0", float, m_flDegrees )
	DMXELEMENT_UNPACK_FIELD( "yaw_offset_min", "0", float, m_flDegreesMin )
	DMXELEMENT_UNPACK_FIELD( "yaw_offset_max", "360", float, m_flDegreesMax )
	DMXELEMENT_UNPACK_FIELD( "yaw_random_exponent", "1", float, m_flRotationRandExponent )
END_PARTICLE_OPERATOR_UNPACK( C_INIT_RandomYaw )


//-----------------------------------------------------------------------------
// Random color
//-----------------------------------------------------------------------------
void ComputeLitParticleColor( float *pColorToWrite, const float *const flBaseColor1, const float *const flBaseColor2, float flRandomValue,
							  const Vector &vLightingColor, float flTintFraction, int nBlendMode )
{
	pColorToWrite[0] = Lerp( flRandomValue, flBaseColor1[0], flBaseColor2[0] );
	pColorToWrite[4] = Lerp( flRandomValue, flBaseColor1[1], flBaseColor2[1] );
	pColorToWrite[8] = Lerp( flRandomValue, flBaseColor1[2], flBaseColor2[2] );

	Vector vLightingTint;
	switch( nBlendMode )
	{
	case 0:		// normal
		vLightingTint = vLightingColor;
		break;

	case 1:		// overlay
		vLightingTint.x = ( vLightingColor.x < 0.5f ) ? ( pColorToWrite[0] * vLightingColor.x * 2.0f ) :
			( 1.0f - ( ( 1.0f - pColorToWrite[0] ) * ( 1.0f - vLightingColor.x ) * 2.0f ) );
		vLightingTint.y = ( vLightingColor.y < 0.5f ) ? ( vLightingColor.y * pColorToWrite[4] * 2.0f ) :
			( 1.0f - ( ( 1.0f - vLightingColor.y ) * ( 1.0f - pColorToWrite[4] ) * 2.0f ) );
		vLightingTint.z = ( vLightingColor.z < 0.5f ) ? ( vLightingColor.z * pColorToWrite[8] * 2.0f ) :
			( 1.0f - ( ( 1.0f - vLightingColor.z ) * ( 1.0f - pColorToWrite[8] ) * 2.0f ) );
		break;

	case 2:		// darken
		vLightingTint.x = ( vLightingColor.x > pColorToWrite[0] ) ? pColorToWrite[0] : vLightingColor.x;
		vLightingTint.y = ( vLightingColor.y > pColorToWrite[4] ) ? pColorToWrite[4] : vLightingColor.y;
		vLightingTint.z = ( vLightingColor.z > pColorToWrite[8] ) ? pColorToWrite[8] : vLightingColor.z;
		break;

	case 3:		// lighten
		vLightingTint.x = ( pColorToWrite[0] > vLightingColor.x ) ? pColorToWrite[0] : vLightingColor.x;
		vLightingTint.y = ( pColorToWrite[4] > vLightingColor.y ) ? pColorToWrite[4] : vLightingColor.y;
		vLightingTint.z = ( pColorToWrite[8] > vLightingColor.z ) ? pColorToWrite[8] : vLightingColor.z;
		break;

	case 4:		// multiply
		vLightingTint.x = pColorToWrite[0] * vLightingColor.x;
		vLightingTint.y = vLightingColor.y * pColorToWrite[4];
		vLightingTint.z = vLightingColor.z * pColorToWrite[8];
		break;
	}

	pColorToWrite[0] = Lerp( flTintFraction, pColorToWrite[0], vLightingTint.x );
	pColorToWrite[4] = Lerp( flTintFraction, pColorToWrite[4], vLightingTint.y );
	pColorToWrite[8] = Lerp( flTintFraction, pColorToWrite[8], vLightingTint.z );

	pColorToWrite[0] = MIN( pColorToWrite[0], 1.0f );
	pColorToWrite[4] = MIN( pColorToWrite[4], 1.0f );
	pColorToWrite[8] = MIN( pColorToWrite[8], 1.0f );
}

static inline unsigned char ClampTintChannel( unsigned char nValue, unsigned char nMin, unsigned char nMax )
{
	return MAX( nMin, MIN( nValue, nMax ) );
}

class C_INIT_RandomColor : public CParticleInitializerOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_INIT_RandomColor );

	uint32 GetWrittenAttributes( void ) const
	{
		return 1 << m_nFieldOutput;
	}

	uint32 GetReadAttributes( void ) const
	{
		return 0;
	}

	virtual uint64 GetReadControlPointMask() const;

	//virtual uint32 GetRequiredContextBytes( void ) const
	virtual size_t GetRequiredContextBytes( void ) const
	{
		return sizeof( Vector );
	}

	virtual void InitializeContextData( CParticleCollection *pParticles, void *pContext ) const
	{
		Vector *pPrevPosition = (Vector *) pContext;
		*pPrevPosition = vec3_origin;
	}

	virtual void InitParams( CParticleSystemDefinition *pDef )
	{
		m_flNormColorMin[0] = (float) m_ColorMin[0] * ( 1.0f / 255.0f );
		m_flNormColorMin[1] = (float) m_ColorMin[1] * ( 1.0f / 255.0f );
		m_flNormColorMin[2] = (float) m_ColorMin[2] * ( 1.0f / 255.0f );

		m_flNormColorMax[0] = (float) m_ColorMax[0] * ( 1.0f / 255.0f );
		m_flNormColorMax[1] = (float) m_ColorMax[1] * ( 1.0f / 255.0f );
		m_flNormColorMax[2] = (float) m_ColorMax[2] * ( 1.0f / 255.0f );
	}

	virtual void InitNewParticlesScalar( CParticleCollection *pParticles, int start_p,
										 int nParticleCount, int nAttributeWriteMask,
										 void *pContext) const;

	virtual void InitNewParticlesBlock( CParticleCollection *pParticles,
										int start_block, int n_blocks, int nAttributeWriteMask,
										void *pContext ) const;

	void GetLocalLighting( CParticleCollection *pParticles, void *pContext, Color &tint ) const;

	float m_flNormColorMin[3];
	float m_flNormColorMax[3];
	Color m_ColorMin;
	Color m_ColorMax;
	Color m_TintMin;
	Color m_TintMax;
	float m_flTintPerc;
	float m_flUpdateThreshold;
	int m_nTintCP;
	int m_nFieldOutput;
	int m_nTintBlendMode;
	float m_flLightAmplification;
};

DEFINE_PARTICLE_OPERATOR( C_INIT_RandomColor, "Color Random", OPERATOR_GENERIC );

BEGIN_PARTICLE_INITIALIZER_OPERATOR_UNPACK( C_INIT_RandomColor )
	DMXELEMENT_UNPACK_FIELD( "color1", "255 255 255 255", Color, m_ColorMin )
	DMXELEMENT_UNPACK_FIELD( "color2", "255 255 255 255", Color, m_ColorMax )
	DMXELEMENT_UNPACK_FIELD( "tint_perc", "0.0", float, m_flTintPerc )
	DMXELEMENT_UNPACK_FIELD( "tint control point", "0", int, m_nTintCP )
	DMXELEMENT_UNPACK_FIELD( "tint clamp min", "0 0 0 0", Color, m_TintMin )
	DMXELEMENT_UNPACK_FIELD( "tint clamp max", "255 255 255 255", Color, m_TintMax )
	DMXELEMENT_UNPACK_FIELD( "tint update movement threshold", "32", float, m_flUpdateThreshold )
	DMXELEMENT_UNPACK_FIELD( "tint blend mode", "0", int, m_nTintBlendMode )
	DMXELEMENT_UNPACK_FIELD( "light amplification amount", "1", float, m_flLightAmplification )
	DMXELEMENT_UNPACK_FIELD_USERDATA( "output field", "6", int, m_nFieldOutput, "intchoice particlefield_vector" )
END_PARTICLE_OPERATOR_UNPACK( C_INIT_RandomColor )

// Finds the lighting color for the tint control point, re-querying it only when the control point moved enough
FORCEINLINE void C_INIT_RandomColor::GetLocalLighting( CParticleCollection *pParticles, void *pContext, Color &tint ) const
{
	// Initially use the color of the light on the control point
	if ( pParticles->m_pParent && ( pParticles->m_pParent->m_LocalLightingCP == m_nTintCP ) )
	{
		tint = pParticles->m_pParent->m_LocalLighting;
	}
	else
	{
		Vector vecOrigin;
		pParticles->GetControlPointAtTime( m_nTintCP, pParticles->m_flCurTime, &vecOrigin );

		Vector *pPrevPosition = (Vector *) pContext;
		Vector vecDelta = *pPrevPosition - vecOrigin;
		float flDist = sqrt( ( vecDelta.y * vecDelta.y + vecDelta.z * vecDelta.z ) + vecDelta.x * vecDelta.x );
		if ( ( flDist >= m_flUpdateThreshold ) || ( pParticles->m_LocalLightingCP == -1 ) )
		{
			g_pParticleSystemMgr->Query()->GetLightingAtPoint( vecOrigin, tint );
			pParticles->m_LocalLighting = tint;
			pParticles->m_LocalLightingCP = m_nTintCP;
			*pPrevPosition = vecOrigin;
		}
		else
		{
			tint = pParticles->m_LocalLighting;
		}
	}

	tint[0] = ClampTintChannel( tint[0], m_TintMin[0], m_TintMax[0] );
	tint[1] = ClampTintChannel( tint[1], m_TintMin[1], m_TintMax[1] );
	tint[2] = ClampTintChannel( tint[2], m_TintMin[2], m_TintMax[2] );
}

void C_INIT_RandomColor::InitNewParticlesScalar(
	CParticleCollection *pParticles, int start_p,
	int nParticleCount, int nAttributeWriteMask, void *pContext ) const
{
	Color tint( 255, 255, 255, 255 );

	// If we're factoring in the tint, find the color at our position
	if ( m_flTintPerc )
	{
		GetLocalLighting( pParticles, pContext, tint );

		float flLightScale = m_flLightAmplification * ( 1.0f / 255.0f );
		Vector vecLightingColor( flLightScale * tint.r(), tint.g() * flLightScale, tint.b() * flLightScale );

		for( ; nParticleCount--; start_p++ )
		{
			float *pColor = pParticles->GetFloatAttributePtrForWrite( m_nFieldOutput, start_p );
			float randomPerc = pParticles->RandomFloat( 0.0f, 1.0f );
			ComputeLitParticleColor( pColor, m_flNormColorMin, m_flNormColorMax, randomPerc, vecLightingColor, m_flTintPerc, m_nTintBlendMode );
		}
	}
	else
	{
		for( ; nParticleCount--; start_p++ )
		{
			float *pColor = pParticles->GetFloatAttributePtrForWrite( m_nFieldOutput, start_p );
			float randomPerc = pParticles->RandomFloat( 0.0f, 1.0f );

			// Randomly choose a range between the two colors
			pColor[0] = m_flNormColorMin[0] + ( ( m_flNormColorMax[0] - m_flNormColorMin[0] ) * randomPerc );
			pColor[4] = m_flNormColorMin[1] + ( ( m_flNormColorMax[1] - m_flNormColorMin[1] ) * randomPerc );
			pColor[8] = m_flNormColorMin[2] + ( ( m_flNormColorMax[2] - m_flNormColorMin[2] ) * randomPerc );
		}
	}
}

void C_INIT_RandomColor::InitNewParticlesBlock( CParticleCollection *pParticles,
											   int start_block, int n_blocks, int nAttributeWriteMask,
											   void *pContext ) const
{
	Color tint( 255, 255, 255, 255 );

	size_t attr_stride;
	FourVectors *pColor = pParticles->Get4VAttributePtrForWrite( m_nFieldOutput, &attr_stride );
	pColor += attr_stride * start_block;

	FourVectors fvColorMin;
	fvColorMin.DuplicateVector( Vector( m_flNormColorMin[0], m_flNormColorMin[1], m_flNormColorMin[2] ) );
	FourVectors fvColorWidth;
	fvColorWidth.DuplicateVector( Vector( m_flNormColorMax[0] - m_flNormColorMin[0],
		m_flNormColorMax[1] - m_flNormColorMin[1],
		m_flNormColorMax[2] - m_flNormColorMin[2] ) );

	int nRandContext = GetSIMDRandContext();

	// If we're factoring in the tint, find the color at our position
	if ( m_flTintPerc )
	{
		GetLocalLighting( pParticles, pContext, tint );

		// Get the light's color
		FourVectors fvTint;
		fvTint.DuplicateVector( Vector( tint.r(), tint.g(), tint.b() ) );
		fltx4 fl4Divisor = ReplicateX4( m_flLightAmplification * ( 1.0f / 255.0f ) );
		fvTint *= fl4Divisor;
		fltx4 fl4TintPrc = ReplicateX4( m_flTintPerc );

		// NOTE: the shipped library never advances pColor inside these loops
		switch( m_nTintBlendMode )
		{
		case 0:		// normal
			while( n_blocks-- )
			{
				fltx4 fl4Rand = RandSIMD( nRandContext );
				FourVectors fvColor;
				fvColor.x = AddSIMD( MulSIMD( fvColorWidth.x, fl4Rand ), fvColorMin.x );
				fvColor.y = AddSIMD( MulSIMD( fvColorWidth.y, fl4Rand ), fvColorMin.y );
				fvColor.z = AddSIMD( MulSIMD( fvColorWidth.z, fl4Rand ), fvColorMin.z );
				pColor->x = AddSIMD( MulSIMD( SubSIMD( fvTint.x, fvColor.x ), fl4TintPrc ), fvColor.x );
				pColor->y = AddSIMD( MulSIMD( SubSIMD( fvTint.y, fvColor.y ), fl4TintPrc ), fvColor.y );
				pColor->z = AddSIMD( MulSIMD( SubSIMD( fvTint.z, fvColor.z ), fl4TintPrc ), fvColor.z );
			}
			break;

		case 1:		// overlay
			while( n_blocks-- )
			{
				fltx4 fl4Rand = RandSIMD( nRandContext );
				FourVectors fvColor;
				fvColor.x = AddSIMD( MulSIMD( fvColorWidth.x, fl4Rand ), fvColorMin.x );
				fvColor.y = AddSIMD( MulSIMD( fvColorWidth.y, fl4Rand ), fvColorMin.y );
				fvColor.z = AddSIMD( MulSIMD( fvColorWidth.z, fl4Rand ), fvColorMin.z );

				fltx4 fl4BlendMaskRed = CmpLtSIMD( fvTint.x, Four_PointFives );
				fltx4 fl4BlendMaskGreen = CmpLtSIMD( fvTint.y, Four_PointFives );
				fltx4 fl4BlendMaskBlue = CmpLtSIMD( fvTint.z, Four_PointFives );

				FourVectors fvBlend1;
				fvBlend1.x = MulSIMD( MulSIMD( fvColor.x, fvTint.x ), Four_Twos );
				fvBlend1.y = MulSIMD( MulSIMD( fvColor.y, fvTint.y ), Four_Twos );
				fvBlend1.z = MulSIMD( MulSIMD( fvColor.z, fvTint.z ), Four_Twos );

				FourVectors fvBlend2;
				fvBlend2.x = SubSIMD( Four_Ones, MulSIMD( MulSIMD( SubSIMD( Four_Ones, fvColor.x ), SubSIMD( Four_Ones, fvTint.x ) ), Four_Twos ) );
				fvBlend2.y = SubSIMD( Four_Ones, MulSIMD( MulSIMD( SubSIMD( Four_Ones, fvColor.y ), SubSIMD( Four_Ones, fvTint.y ) ), Four_Twos ) );
				fvBlend2.z = SubSIMD( Four_Ones, MulSIMD( MulSIMD( SubSIMD( Four_Ones, fvColor.z ), SubSIMD( Four_Ones, fvTint.z ) ), Four_Twos ) );

				FourVectors fvBlend;
				fvBlend.x = MaskedAssign( fl4BlendMaskRed, fvBlend1.x, fvBlend2.x );
				fvBlend.y = MaskedAssign( fl4BlendMaskGreen, fvBlend1.y, fvBlend2.y );
				fvBlend.z = MaskedAssign( fl4BlendMaskBlue, fvBlend1.z, fvBlend2.z );

				pColor->x = AddSIMD( fvColor.x, MulSIMD( SubSIMD( fvBlend.x, fvColor.x ), fl4TintPrc ) );
				pColor->y = AddSIMD( fvColor.y, MulSIMD( SubSIMD( fvBlend.y, fvColor.y ), fl4TintPrc ) );
				pColor->z = AddSIMD( fvColor.z, MulSIMD( SubSIMD( fvBlend.z, fvColor.z ), fl4TintPrc ) );
			}
			break;

		case 2:		// darken
			while( n_blocks-- )
			{
				fltx4 fl4Rand = RandSIMD( nRandContext );
				FourVectors fvColor;
				fvColor.x = AddSIMD( MulSIMD( fvColorWidth.x, fl4Rand ), fvColorMin.x );
				fvColor.y = AddSIMD( MulSIMD( fvColorWidth.y, fl4Rand ), fvColorMin.y );
				fvColor.z = AddSIMD( MulSIMD( fvColorWidth.z, fl4Rand ), fvColorMin.z );
				pColor->x = AddSIMD( fvColor.x, MulSIMD( SubSIMD( MinSIMD( fvColor.x, fvTint.x ), fvColor.x ), fl4TintPrc ) );
				pColor->y = AddSIMD( fvColor.y, MulSIMD( SubSIMD( MinSIMD( fvColor.y, fvTint.y ), fvColor.y ), fl4TintPrc ) );
				pColor->z = AddSIMD( fvColor.z, MulSIMD( SubSIMD( MinSIMD( fvColor.z, fvTint.z ), fvColor.z ), fl4TintPrc ) );
			}
			break;

		case 3:		// lighten
			while( n_blocks-- )
			{
				fltx4 fl4Rand = RandSIMD( nRandContext );
				FourVectors fvColor;
				fvColor.x = AddSIMD( MulSIMD( fvColorWidth.x, fl4Rand ), fvColorMin.x );
				fvColor.y = AddSIMD( MulSIMD( fvColorWidth.y, fl4Rand ), fvColorMin.y );
				fvColor.z = AddSIMD( MulSIMD( fvColorWidth.z, fl4Rand ), fvColorMin.z );
				pColor->x = AddSIMD( fvColor.x, MulSIMD( SubSIMD( MaxSIMD( fvColor.x, fvTint.x ), fvColor.x ), fl4TintPrc ) );
				pColor->y = AddSIMD( fvColor.y, MulSIMD( SubSIMD( MaxSIMD( fvColor.y, fvTint.y ), fvColor.y ), fl4TintPrc ) );
				pColor->z = AddSIMD( fvColor.z, MulSIMD( SubSIMD( MaxSIMD( fvColor.z, fvTint.z ), fvColor.z ), fl4TintPrc ) );
			}
			break;

		case 4:		// multiply
			while( n_blocks-- )
			{
				fltx4 fl4Rand = RandSIMD( nRandContext );
				FourVectors fvColor;
				fvColor.x = AddSIMD( MulSIMD( fvColorWidth.x, fl4Rand ), fvColorMin.x );
				fvColor.y = AddSIMD( MulSIMD( fvColorWidth.y, fl4Rand ), fvColorMin.y );
				fvColor.z = AddSIMD( MulSIMD( fvColorWidth.z, fl4Rand ), fvColorMin.z );
				pColor->x = AddSIMD( MulSIMD( SubSIMD( MulSIMD( fvColor.x, fvTint.x ), fvColor.x ), fl4TintPrc ), fvColor.x );
				pColor->y = AddSIMD( MulSIMD( SubSIMD( MulSIMD( fvColor.y, fvTint.y ), fvColor.y ), fl4TintPrc ), fvColor.y );
				pColor->z = AddSIMD( MulSIMD( SubSIMD( MulSIMD( fvColor.z, fvTint.z ), fvColor.z ), fl4TintPrc ), fvColor.z );
			}
			break;
		}

		// NOTE: the shipped library clamps z from the x component here
		pColor->x = MinSIMD( pColor->x, Four_Ones );
		pColor->y = MinSIMD( pColor->y, Four_Ones );
		pColor->z = MinSIMD( pColor->x, Four_Ones );
	}
	else
	{
		while( n_blocks-- )
		{
			fltx4 fl4Rand = RandSIMD( nRandContext );
			pColor->x = AddSIMD( MulSIMD( fvColorWidth.x, fl4Rand ), fvColorMin.x );
			pColor->y = AddSIMD( MulSIMD( fvColorWidth.y, fl4Rand ), fvColorMin.y );
			pColor->z = AddSIMD( MulSIMD( fvColorWidth.z, fl4Rand ), fvColorMin.z );
			pColor += attr_stride;
		}
	}

	ReleaseSIMDRandContext( nRandContext );
}

uint64 C_INIT_RandomColor::GetReadControlPointMask() const
{
	return 1ULL << m_nTintCP;
}


//-----------------------------------------------------------------------------
// Color lit per particle
//-----------------------------------------------------------------------------
class C_INIT_ColorLitPerParticle : public CParticleInitializerOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_INIT_ColorLitPerParticle );

	uint32 GetWrittenAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_TINT_RGB_MASK;
	}

	uint32 GetReadAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_XYZ_MASK;
	}

	virtual void InitParams( CParticleSystemDefinition *pDef )
	{
		m_flNormColorMin[0] = (float) m_ColorMin[0] * ( 1.0f / 255.0f );
		m_flNormColorMin[1] = (float) m_ColorMin[1] * ( 1.0f / 255.0f );
		m_flNormColorMin[2] = (float) m_ColorMin[2] * ( 1.0f / 255.0f );

		m_flNormColorMax[0] = (float) m_ColorMax[0] * ( 1.0f / 255.0f );
		m_flNormColorMax[1] = (float) m_ColorMax[1] * ( 1.0f / 255.0f );
		m_flNormColorMax[2] = (float) m_ColorMax[2] * ( 1.0f / 255.0f );
	}

	virtual void InitNewParticlesScalar( CParticleCollection *pParticles, int start_p,
										 int nParticleCount, int nAttributeWriteMask,
										 void *pContext) const;

	float m_flNormColorMin[3];
	float m_flNormColorMax[3];
	Color m_ColorMin;
	Color m_ColorMax;
	Color m_TintMin;
	Color m_TintMax;
	float m_flTintPerc;
	int m_nTintBlendMode;
	float m_flLightAmplification;
};

DEFINE_PARTICLE_OPERATOR( C_INIT_ColorLitPerParticle, "Color Lit Per Particle", OPERATOR_PI_TINT_RGB );

BEGIN_PARTICLE_INITIALIZER_OPERATOR_UNPACK( C_INIT_ColorLitPerParticle )
	DMXELEMENT_UNPACK_FIELD( "color1", "255 255 255 255", Color, m_ColorMin )
	DMXELEMENT_UNPACK_FIELD( "color2", "255 255 255 255", Color, m_ColorMax )
	DMXELEMENT_UNPACK_FIELD( "light bias", "0.0", float, m_flTintPerc )
	DMXELEMENT_UNPACK_FIELD( "tint clamp min", "0 0 0 0", Color, m_TintMin )
	DMXELEMENT_UNPACK_FIELD( "tint clamp max", "255 255 255 255", Color, m_TintMax )
	DMXELEMENT_UNPACK_FIELD( "tint blend mode", "0", int, m_nTintBlendMode )
	DMXELEMENT_UNPACK_FIELD( "light amplification amount", "1", float, m_flLightAmplification )
END_PARTICLE_OPERATOR_UNPACK( C_INIT_ColorLitPerParticle )

void C_INIT_ColorLitPerParticle::InitNewParticlesScalar(
	CParticleCollection *pParticles, int start_p,
	int nParticleCount, int nAttributeWriteMask, void *pContext ) const
{
	Color tint( 255, 255, 255, 255 );

	for( ; nParticleCount--; start_p++ )
	{
		float *pColor = pParticles->GetFloatAttributePtrForWrite( PARTICLE_ATTRIBUTE_TINT_RGB, start_p );
		const float *pXYZ = pParticles->GetFloatAttributePtr( PARTICLE_ATTRIBUTE_XYZ, start_p );

		Vector vecOrigin( pXYZ[0], pXYZ[4], pXYZ[8] );
		g_pParticleSystemMgr->Query()->GetLightingAtPoint( vecOrigin, tint );

		tint[0] = ClampTintChannel( tint[0], m_TintMin[0], m_TintMax[0] );
		tint[1] = ClampTintChannel( tint[1], m_TintMin[1], m_TintMax[1] );
		tint[2] = ClampTintChannel( tint[2], m_TintMin[2], m_TintMax[2] );

		float flLightScale = m_flLightAmplification * ( 1.0f / 255.0f );
		Vector vecLightingColor( flLightScale * tint.r(), tint.g() * flLightScale, tint.b() * flLightScale );
		float randomPerc = pParticles->RandomFloat( 0.0f, 1.0f );
		ComputeLitParticleColor( pColor, m_flNormColorMin, m_flNormColorMax, randomPerc, vecLightingColor, m_flTintPerc, m_nTintBlendMode );
	}
}


//-----------------------------------------------------------------------------
// Trail Length
//-----------------------------------------------------------------------------
class C_INIT_RandomTrailLength : public CParticleInitializerOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_INIT_RandomTrailLength );

	uint32 GetWrittenAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_TRAIL_LENGTH_MASK;
	}

	uint32 GetReadAttributes( void ) const
	{
		return 0;
	}

	virtual void InitParams( CParticleSystemDefinition *pDef )
	{
	}

	virtual void InitNewParticlesBlock( CParticleCollection *pParticles,
										int start_block, int n_blocks, int nAttributeWriteMask,
										void *pContext ) const
	{
		InitScalarAttributeRandomRangeExpBlock( PARTICLE_ATTRIBUTE_TRAIL_LENGTH, m_flMinLength, m_flMaxLength, m_flLengthRandExponent,
			pParticles, start_block, n_blocks );
	}

	virtual void InitNewParticlesScalar( CParticleCollection *pParticles, int start_p,
										 int nParticleCount, int nAttributeWriteMask,
										 void *pContext) const
	{
		InitScalarAttributeRandomRangeExpScalar( PARTICLE_ATTRIBUTE_TRAIL_LENGTH, m_flMinLength, m_flMaxLength, m_flLengthRandExponent,
			pParticles, start_p, nParticleCount );
	}

	float m_flMinLength;
	float m_flMaxLength;
	float m_flLengthRandExponent;
};

DEFINE_PARTICLE_OPERATOR( C_INIT_RandomTrailLength, "Trail Length Random", OPERATOR_GENERIC );

BEGIN_PARTICLE_INITIALIZER_OPERATOR_UNPACK( C_INIT_RandomTrailLength )
	DMXELEMENT_UNPACK_FIELD( "length_min", "0.1", float, m_flMinLength )
	DMXELEMENT_UNPACK_FIELD( "length_max", "0.1", float, m_flMaxLength )
	DMXELEMENT_UNPACK_FIELD( "length_random_exponent", "1", float, m_flLengthRandExponent )
END_PARTICLE_OPERATOR_UNPACK( C_INIT_RandomTrailLength )


//-----------------------------------------------------------------------------
// Random sequence
//-----------------------------------------------------------------------------
struct SequenceParamaters_t
{
	int m_nCurrent;
	int m_nSequences[64];
};

class C_INIT_RandomSequence : public CParticleInitializerOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_INIT_RandomSequence );

	uint32 GetWrittenAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_SEQUENCE_NUMBER_MASK;
	}

	uint32 GetReadAttributes( void ) const
	{
		return 0;
	}

	virtual void InitParams( CParticleSystemDefinition *pDef )
	{
		// The shuffle/linear table only holds 64 sequences (the shipped code overflowed it for larger ranges)
		if ( m_bShuffle || m_bLinear )
		{
			m_nSequenceMin = clamp( m_nSequenceMin, 0, 63 );
			m_nSequenceMax = clamp( m_nSequenceMax, 0, 63 );
		}
	}

	virtual void InitializeContextData( CParticleCollection *pParticles, void *pContext ) const
	{
		if ( m_bShuffle || m_bLinear )
		{
			SequenceParamaters_t *pParams = reinterpret_cast<SequenceParamaters_t *>( pContext );
			pParams->m_nCurrent = m_nSequenceMin;
			for( int i = m_nSequenceMin; i <= m_nSequenceMax; ++i )
			{
				pParams->m_nSequences[i] = i;
			}
			if ( m_bShuffle )
			{
				for( int i = m_nSequenceMax; i > m_nSequenceMin; --i )
				{
					int j = pParticles->RandomInt( m_nSequenceMin, i );
					V_swap( pParams->m_nSequences[i], pParams->m_nSequences[j] );
				}
			}
		}
	}

	//virtual uint32 GetRequiredContextBytes( ) const
	virtual size_t GetRequiredContextBytes( ) const
	{
		return sizeof( SequenceParamaters_t );
	}

	virtual void InitNewParticlesScalar( CParticleCollection *pParticles, int start_p,
										 int nParticleCount, int nAttributeWriteMask,
										 void *pContext) const;

	int m_nSequenceMin;
	int m_nSequenceMax;
	bool m_bShuffle;
	bool m_bLinear;
};

DEFINE_PARTICLE_OPERATOR( C_INIT_RandomSequence, "Sequence Random", OPERATOR_GENERIC );

BEGIN_PARTICLE_INITIALIZER_OPERATOR_UNPACK( C_INIT_RandomSequence )
	DMXELEMENT_UNPACK_FIELD_USERDATA( "sequence_min", "0", int, m_nSequenceMin, "sheetsequencepicker" )
	DMXELEMENT_UNPACK_FIELD_USERDATA( "sequence_max", "0", int, m_nSequenceMax, "sheetsequencepicker" )
	DMXELEMENT_UNPACK_FIELD( "shuffle", "0", bool, m_bShuffle )
	DMXELEMENT_UNPACK_FIELD( "linear", "0", bool, m_bLinear )
END_PARTICLE_OPERATOR_UNPACK( C_INIT_RandomSequence )

void C_INIT_RandomSequence::InitNewParticlesScalar(
	CParticleCollection *pParticles, int start_p,
	int nParticleCount, int nAttributeWriteMask, void *pContext ) const
{
	if ( m_bShuffle || m_bLinear )
	{
		SequenceParamaters_t *pParams = reinterpret_cast<SequenceParamaters_t *>( pContext );
		for( ; nParticleCount--; start_p++ )
		{
			if ( pParams->m_nCurrent > m_nSequenceMax )
			{
				if ( m_bShuffle )
				{
					for( int i = m_nSequenceMax; i > m_nSequenceMin; --i )
					{
						int j = pParticles->RandomInt( m_nSequenceMin, i );
						int nTemp = pParams->m_nSequences[i];
						pParams->m_nSequences[i] = pParams->m_nSequences[j];
						pParams->m_nSequences[j] = nTemp;
					}
				}
				pParams->m_nCurrent = m_nSequenceMin;
			}
			float *pSequence = pParticles->GetFloatAttributePtrForWrite( PARTICLE_ATTRIBUTE_SEQUENCE_NUMBER, start_p );
			*pSequence = pParams->m_nSequences[pParams->m_nCurrent];
			++pParams->m_nCurrent;
		}
	}
	else
	{
		for( ; nParticleCount--; start_p++ )
		{
			float *pSequence = pParticles->GetFloatAttributePtrForWrite( PARTICLE_ATTRIBUTE_SEQUENCE_NUMBER, start_p );
			*pSequence = pParticles->RandomInt( m_nSequenceMin, m_nSequenceMax );
		}
	}
}


//-----------------------------------------------------------------------------
// Sequence from the digits of a control point's components
//-----------------------------------------------------------------------------
class C_INIT_SequenceFromCP : public CParticleInitializerOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_INIT_SequenceFromCP );

	uint32 GetWrittenAttributes( void ) const
	{
		uint32 nWriteAttributes = PARTICLE_ATTRIBUTE_SEQUENCE_NUMBER_MASK;
		if ( m_vecOffset != vec3_origin )
			nWriteAttributes = PARTICLE_ATTRIBUTE_SEQUENCE_NUMBER_MASK | PARTICLE_ATTRIBUTE_XYZ_MASK | PARTICLE_ATTRIBUTE_PREV_XYZ_MASK;
		if ( m_bKillUnused == true )
			nWriteAttributes |= PARTICLE_ATTRIBUTE_LIFE_DURATION_MASK;
		return nWriteAttributes;
	}

	uint32 GetReadAttributes( void ) const
	{
		if ( m_vecOffset != vec3_origin )
			return PARTICLE_ATTRIBUTE_PREV_XYZ_MASK | PARTICLE_ATTRIBUTE_RADIUS_MASK;
		else
			return 0;
	}

	virtual uint64 GetReadControlPointMask() const
	{
		return 1ULL << m_nCP;
	}

	bool InitMultipleOverride ( void ) { return true; }

	virtual void InitNewParticlesScalar( CParticleCollection *pParticles, int start_p,
										 int nParticleCount, int nAttributeWriteMask,
										 void *pContext) const;

	bool m_bKillUnused;
	bool m_bRadiusScale;
	int m_nCP;
	Vector m_vecOffset;
};

DEFINE_PARTICLE_OPERATOR( C_INIT_SequenceFromCP, "Sequence From Control Point", OPERATOR_GENERIC );

BEGIN_PARTICLE_INITIALIZER_OPERATOR_UNPACK( C_INIT_SequenceFromCP )
	DMXELEMENT_UNPACK_FIELD( "control point", "1", int, m_nCP )
	DMXELEMENT_UNPACK_FIELD( "per particle spatial offset", "0 0 0", Vector, m_vecOffset )
	DMXELEMENT_UNPACK_FIELD( "offset propotional to radius", "0", bool, m_bRadiusScale )
END_PARTICLE_OPERATOR_UNPACK( C_INIT_SequenceFromCP )

void C_INIT_SequenceFromCP::InitNewParticlesScalar(
	CParticleCollection *pParticles, int start_p,
	int nParticleCount, int nAttributeWriteMask, void *pContext ) const
{
	// Each digit of each component is a sequence number
	Vector vecCP = pParticles->GetControlPointAtCurrentTime( m_nCP );
	int nComponent = 2;
	int nNumDigits = MAX( 0, (int)log10( vecCP.x * 10 ) );
	nNumDigits += MAX( 0, (int)log10( vecCP.y * 10 ) );
	nNumDigits += MAX( 0, (int)log10( vecCP.z * 10 ) );
	nNumDigits = MIN( nNumDigits, nParticleCount );
	Vector vecOffsetPos = m_vecOffset * ( nNumDigits - 1 ) * 0.5f;

	for( ; nParticleCount--; start_p++ )
	{
		while ( ( vecCP[nComponent] < 1 ) && ( nComponent >= 0 ) )
		{
			nComponent--;
		}
		float flValue = vecCP[nComponent];
		if ( ( flValue > 0 ) && ( nComponent >= 0 ) )
		{
			float *pSequence = pParticles->GetFloatAttributePtrForWrite( PARTICLE_ATTRIBUTE_SEQUENCE_NUMBER, start_p );
			*pSequence = fmod( (double)flValue, 10.0 ) + ( nComponent * 10 );
			vecCP[nComponent] = floor( flValue * 0.1f );

			if ( m_vecOffset != vec3_origin )
			{
				float *xyz = pParticles->GetFloatAttributePtrForWrite( PARTICLE_ATTRIBUTE_XYZ, start_p );
				float *pxyz = pParticles->GetFloatAttributePtrForWrite( PARTICLE_ATTRIBUTE_PREV_XYZ, start_p );
				float flRadius = 1.0f;
				if ( m_bRadiusScale )
				{
					flRadius = *pParticles->GetFloatAttributePtr( PARTICLE_ATTRIBUTE_RADIUS, start_p );
				}
				Vector vecScaledOffset = vecOffsetPos * flRadius;
				xyz[0] += vecScaledOffset.x;
				xyz[4] += vecScaledOffset.y;
				xyz[8] += vecScaledOffset.z;
				pxyz[0] += vecScaledOffset.x;
				pxyz[4] += vecScaledOffset.y;
				pxyz[8] += vecScaledOffset.z;
				vecOffsetPos -= m_vecOffset;
			}
		}
		else
		{
			float *plife = pParticles->GetFloatAttributePtrForWrite( PARTICLE_ATTRIBUTE_LIFE_DURATION, start_p );
			*plife = -1.0f;
		}
	}
}


//-----------------------------------------------------------------------------
// Position Warp Initializer
// Scales initial position and velocity of particles within a random vector range
//-----------------------------------------------------------------------------
class C_INIT_PositionWarp : public CParticleInitializerOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_INIT_PositionWarp );

	Vector m_vecWarpMin;
	Vector m_vecWarpMax;
	int m_nControlPointNumber;
	float m_flWarpTime, m_flWarpStartTime;
	bool m_bInvertWarp;

	uint32 GetWrittenAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_XYZ_MASK | PARTICLE_ATTRIBUTE_PREV_XYZ_MASK;
	}

	uint32 GetReadAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_RADIUS_MASK;
	}

	virtual uint64 GetReadControlPointMask() const
	{
		return 1ULL << m_nControlPointNumber;
	}

	virtual void InitParams( CParticleSystemDefinition *pDef )
	{
		m_nControlPointNumber = clamp( m_nControlPointNumber, 0, MAX_PARTICLE_CONTROL_POINTS - 1 );
	}

	bool InitMultipleOverride ( void ) { return true; }

	virtual void InitNewParticlesScalar( CParticleCollection *pParticles, int start_p,
										 int nParticleCount, int nAttributeWriteMask,
										 void *pContext) const;
};

DEFINE_PARTICLE_OPERATOR( C_INIT_PositionWarp, "Position Modify Warp Random", OPERATOR_GENERIC );

BEGIN_PARTICLE_INITIALIZER_OPERATOR_UNPACK( C_INIT_PositionWarp )
	DMXELEMENT_UNPACK_FIELD( "control point number", "0", int, m_nControlPointNumber )
	DMXELEMENT_UNPACK_FIELD( "warp min", "1 1 1", Vector, m_vecWarpMin )
	DMXELEMENT_UNPACK_FIELD( "warp max", "1 1 1", Vector, m_vecWarpMax )
	DMXELEMENT_UNPACK_FIELD( "warp transition time (treats min/max as start/end sizes)", "0", float, m_flWarpTime )
	DMXELEMENT_UNPACK_FIELD( "warp transition start time", "0", float, m_flWarpStartTime )
	DMXELEMENT_UNPACK_FIELD( "reverse warp (0/1)", "0", bool, m_bInvertWarp )
END_PARTICLE_OPERATOR_UNPACK( C_INIT_PositionWarp )

void C_INIT_PositionWarp::InitNewParticlesScalar(
	CParticleCollection *pParticles, int start_p,
	int nParticleCount, int nAttributeWriteMask, void *pContext ) const
{
	Vector vecWarpStart = m_vecWarpMin;
	Vector vecWarpEnd = m_vecWarpMax;
	if ( m_bInvertWarp )
	{
		vecWarpStart = m_vecWarpMax;
		vecWarpEnd = m_vecWarpMin;
	}

	for( ; nParticleCount--; start_p++ )
	{
		float *xyz = pParticles->GetFloatAttributePtrForWrite( PARTICLE_ATTRIBUTE_XYZ, start_p );
		float *pxyz = pParticles->GetFloatAttributePtrForWrite( PARTICLE_ATTRIBUTE_PREV_XYZ, start_p );
		const float *ct = pParticles->GetFloatAttributePtr( PARTICLE_ATTRIBUTE_CREATION_TIME, start_p );

		Vector randpos;
		if ( m_flWarpTime != 0.0f )
		{
			float flWarpEnd = m_flWarpStartTime + m_flWarpTime;
			float flPercentage = RemapValClamped( *ct, m_flWarpStartTime, flWarpEnd, 0.0, 1.0 );
			VectorLerp( vecWarpStart, vecWarpEnd, flPercentage, randpos );
		}
		else
		{
			pParticles->RandomVector( m_vecWarpMin, m_vecWarpMax, &randpos );
		}

		matrix3x4_t mat;
		pParticles->GetControlPointTransformAtTime( m_nControlPointNumber, *ct, &mat );
		Vector vecTransformLocal = vec3_origin;
		Vector vecParticlePosition, vecParticlePosition_prev;
		vecParticlePosition.Init( xyz[0], xyz[4], xyz[8] );
		vecParticlePosition_prev.Init( pxyz[0], pxyz[4], pxyz[8] );

		// rotate particles from world space into local
		VectorITransform( vecParticlePosition, mat, vecTransformLocal );
		// multiply position by desired amount
		vecTransformLocal.x *= randpos.x;
		vecTransformLocal.y *= randpos.y;
		vecTransformLocal.z *= randpos.z;
		// rotate back into world space
		VectorTransform( vecTransformLocal, mat, vecParticlePosition );
		// rinse, repeat
		VectorITransform( vecParticlePosition_prev, mat, vecTransformLocal );
		vecTransformLocal.x *= randpos.x;
		vecTransformLocal.y *= randpos.y;
		vecTransformLocal.z *= randpos.z;
		VectorTransform( vecTransformLocal, mat, vecParticlePosition_prev );

		// set positions into floats
		xyz[0] = vecParticlePosition.x;
		xyz[4] = vecParticlePosition.y;
		xyz[8] = vecParticlePosition.z;
		pxyz[0] = vecParticlePosition_prev.x;
		pxyz[4] = vecParticlePosition_prev.y;
		pxyz[8] = vecParticlePosition_prev.z;
	}
}


//-----------------------------------------------------------------------------
// Remap noise to scalar
//-----------------------------------------------------------------------------
class C_INIT_CreationNoise : public CParticleInitializerOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_INIT_CreationNoise );

	uint32 GetWrittenAttributes( void ) const
	{
		return 1 << m_nFieldOutput;
	}

	uint32 GetReadAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_XYZ_MASK | PARTICLE_ATTRIBUTE_CREATION_TIME_MASK;
	}

	virtual uint32 GetFilter( void ) const
	{
		return FILTER_PARAMETER_REMAPPING_MASK;
	}

	virtual bool IsScrubSafe() { return true; }

	virtual void InitNewParticlesScalar( CParticleCollection *pParticles, int start_p,
										 int nParticleCount, int nAttributeWriteMask,
										 void *pContext) const;

	virtual void InitNewParticlesBlock( CParticleCollection *pParticles,
										int start_block, int n_blocks, int nAttributeWriteMask,
										void *pContext ) const;

	int m_nFieldOutput;
	bool m_bAbsVal, m_bAbsValInv;
	float m_flOffset;
	float m_flOutputMin;
	float m_flOutputMax;
	float m_flNoiseScale, m_flNoiseScaleLoc;
	Vector m_vecOffsetLoc;
	float m_flWorldTimeScale;
};

DEFINE_PARTICLE_OPERATOR( C_INIT_CreationNoise, "Remap Noise to Scalar", OPERATOR_GENERIC );

BEGIN_PARTICLE_INITIALIZER_OPERATOR_UNPACK( C_INIT_CreationNoise )
	DMXELEMENT_UNPACK_FIELD( "time noise coordinate scale", "0.1", float, m_flNoiseScale )
	DMXELEMENT_UNPACK_FIELD( "spatial noise coordinate scale", "0.001", float, m_flNoiseScaleLoc )
	DMXELEMENT_UNPACK_FIELD_USERDATA( "output field", "3", int, m_nFieldOutput, "intchoice particlefield_scalar" )
	DMXELEMENT_UNPACK_FIELD( "time coordinate offset", "0", float, m_flOffset )
	DMXELEMENT_UNPACK_FIELD( "spatial coordinate offset", "0 0 0", Vector, m_vecOffsetLoc )
	DMXELEMENT_UNPACK_FIELD( "absolute value", "0", bool, m_bAbsVal )
	DMXELEMENT_UNPACK_FIELD( "invert absolute value", "0", bool, m_bAbsValInv )
	DMXELEMENT_UNPACK_FIELD( "output minimum", "0", float, m_flOutputMin )
	DMXELEMENT_UNPACK_FIELD( "output maximum", "1", float, m_flOutputMax )
	DMXELEMENT_UNPACK_FIELD( "world time noise coordinate scale", "0", float, m_flWorldTimeScale )
END_PARTICLE_OPERATOR_UNPACK( C_INIT_CreationNoise )

void C_INIT_CreationNoise::InitNewParticlesBlock( CParticleCollection *pParticles,
												  int start_block, int n_blocks, int nAttributeWriteMask,
												  void *pContext ) const
{
	fltx4 fl4AbsVal = CmpEqSIMD( Four_Zeros, Four_Zeros );
	float flAbsScale = 0.5f;

	if ( m_bAbsVal )
	{
		fl4AbsVal = LoadAlignedSIMD( (const float *) g_SIMD_clear_signmask );
		flAbsScale = 1.0f;
	}

	float fMin = m_flOutputMin;
	float fMax = m_flOutputMax;

	if ( ATTRIBUTES_WHICH_ARE_ANGLES & ( 1 << m_nFieldOutput ) )
	{
		fMin *= ( M_PI / 180.0 );
		fMax *= ( M_PI / 180.0 );
	}

	size_t attr_stride;
	fltx4 *pAttr = pParticles->GetM128AttributePtrForWrite( m_nFieldOutput, &attr_stride );
	pAttr += attr_stride * start_block;
	const FourVectors *pxyz = pParticles->Get4VAttributePtr( PARTICLE_ATTRIBUTE_XYZ, &attr_stride );
	pxyz += attr_stride * start_block;
	const fltx4 *pCreationTime = pParticles->GetM128AttributePtr( PARTICLE_ATTRIBUTE_CREATION_TIME, &attr_stride );
	pCreationTime += attr_stride * start_block;

	// setup
	fltx4 fl4ValueScale = ReplicateX4( flAbsScale * ( fMax - fMin ) );
	fltx4 fl4ValueBase = ReplicateX4( fMin + ( ( 1.0f - flAbsScale ) * ( fMax - fMin ) ) );
	FourVectors fvOffsetLoc;
	fvOffsetLoc.DuplicateVector( m_vecOffsetLoc );

	FourVectors fvCoordBase;
	fvCoordBase.x = AddSIMD( *pCreationTime, ReplicateX4( m_flOffset ) );
	fvCoordBase.y = fvCoordBase.x;
	fvCoordBase.z = fvCoordBase.x;
	fvCoordBase *= ReplicateX4( m_flNoiseScale );

	while( n_blocks-- )
	{
		FourVectors fvCoord = *pxyz;
		fvCoord += fvOffsetLoc;
		fvCoord *= ReplicateX4( m_flNoiseScaleLoc );
		fvCoord += fvCoordBase;

		fltx4 fl4Noise = NoiseSIMD( fvCoord );
		fl4Noise = AndSIMD( fl4Noise, fl4AbsVal );

		if ( m_bAbsValInv )
		{
			fl4Noise = SubSIMD( Four_Ones, fl4Noise );
		}

		fltx4 fl4InitialNoise = AddSIMD( fl4ValueBase, MulSIMD( fl4ValueScale, fl4Noise ) );

		if ( ATTRIBUTES_WHICH_ARE_0_TO_1 & ( 1 << m_nFieldOutput ) )
		{
			fl4InitialNoise = MinSIMD( Four_Ones, fl4InitialNoise );
			fl4InitialNoise = MaxSIMD( Four_Zeros, fl4InitialNoise );
		}

		*( pAttr ) = fl4InitialNoise;

		// NOTE: shipped code advances every pointer by the creation time stride
		pAttr += attr_stride;
		pxyz += attr_stride;
	}
}

void C_INIT_CreationNoise::InitNewParticlesScalar(
	CParticleCollection *pParticles, int start_p,
	int nParticleCount, int nAttributeWriteMask, void *pContext ) const
{
	float flAbsScale;
	int nAbsVal;
	nAbsVal = 0xffffffff;
	flAbsScale = 0.5;
	if ( m_bAbsVal )
	{
		nAbsVal = 0x7fffffff;
		flAbsScale = 1.0;
	}

	float fMin = m_flOutputMin;
	float fMax = m_flOutputMax;

	if ( ATTRIBUTES_WHICH_ARE_ANGLES & ( 1 << m_nFieldOutput ) )
	{
		fMin *= ( M_PI / 180.0 );
		fMax *= ( M_PI / 180.0 );
	}

	float CoordScaleLoc = m_flNoiseScaleLoc;

	float ValueScale, ValueBase;
	ValueScale = ( flAbsScale * ( fMax - fMin ) );
	ValueBase = ( fMin + ( ( 1.0 - flAbsScale ) * ( fMax - fMin ) ) );

	Vector CoordBase;
	const float *pCreationTime = pParticles->GetFloatAttributePtr( PARTICLE_ATTRIBUTE_CREATION_TIME, start_p );
	float flOffset = ( *pCreationTime + m_flOffset ) * m_flNoiseScale;
	CoordBase.Init( flOffset, flOffset, flOffset );
	CoordBase.x += Plat_MSTime() * m_flWorldTimeScale;
	CoordBase.y += Plat_MSTime() * m_flWorldTimeScale;
	CoordBase.z += Plat_MSTime() * m_flWorldTimeScale;

	for( ; nParticleCount--; start_p++ )
	{
		const float *pxyz = pParticles->GetFloatAttributePtr( PARTICLE_ATTRIBUTE_XYZ, start_p );
		float *pAttr = pParticles->GetFloatAttributePtrForWrite( m_nFieldOutput, start_p );

		Vector Coord = CoordBase;
		Vector CoordLoc;
		CoordLoc.x = pxyz[0];
		CoordLoc.y = pxyz[4];
		CoordLoc.z = pxyz[8];
		CoordLoc += m_vecOffsetLoc;
		CoordLoc *= CoordScaleLoc;
		Coord += CoordLoc;

		FourVectors fvNoise;
		fvNoise.DuplicateVector( Coord );
		fltx4 flNoise128 = NoiseSIMD( fvNoise );
		float flNoise = SubFloat( flNoise128, 0 );

		*( (int *) &flNoise ) &= nAbsVal;

		if ( m_bAbsValInv )
		{
			flNoise = 1.0 - flNoise;
		}

		float flInitialNoise = ( ValueBase + ( ValueScale * flNoise ) );

		if ( ATTRIBUTES_WHICH_ARE_0_TO_1 & ( 1 << m_nFieldOutput ) )
		{
			flInitialNoise = clamp( flInitialNoise, 0.0f, 1.0f );
		}

		*( pAttr ) = flInitialNoise;
	}
}


//-----------------------------------------------------------------------------
// Position along a path
//-----------------------------------------------------------------------------
class C_INIT_CreateAlongPath : public CParticleInitializerOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_INIT_CreateAlongPath );

	uint32 GetWrittenAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_XYZ_MASK | PARTICLE_ATTRIBUTE_PREV_XYZ_MASK;
	}

	uint32 GetReadAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_CREATION_TIME_MASK;
	}

	virtual uint64 GetReadControlPointMask() const
	{
		uint64 nStartMask = ( 1ULL << m_PathParams.m_nStartControlPointNumber ) - 1;
		uint64 nEndMask = ( 1ULL << ( m_PathParams.m_nEndControlPointNumber + 1 ) ) - 1;
		return nEndMask & (~nStartMask);
	}

	virtual void InitParams( CParticleSystemDefinition *pDef )
	{
		m_PathParams.ClampControlPointIndices();
	}

	virtual void InitNewParticlesScalar( CParticleCollection *pParticles, int start_p,
										 int nParticleCount, int nAttributeWriteMask,
										 void *pContext) const;

	float m_fMaxDistance;
	struct CPathParameters m_PathParams;
	bool m_bUseRandomCPs;
};

DEFINE_PARTICLE_OPERATOR( C_INIT_CreateAlongPath, "Position Along Path Random", OPERATOR_PI_POSITION );

BEGIN_PARTICLE_INITIALIZER_OPERATOR_UNPACK( C_INIT_CreateAlongPath )
	DMXELEMENT_UNPACK_FIELD( "maximum distance", "0", float, m_fMaxDistance )
	DMXELEMENT_UNPACK_FIELD( "bulge", "0", float, m_PathParams.m_flBulge )
	DMXELEMENT_UNPACK_FIELD( "start control point number", "0", int, m_PathParams.m_nStartControlPointNumber )
	DMXELEMENT_UNPACK_FIELD( "end control point number", "0", int, m_PathParams.m_nEndControlPointNumber )
	DMXELEMENT_UNPACK_FIELD( "bulge control 0=random 1=orientation of start pnt 2=orientation of end point", "0", int, m_PathParams.m_nBulgeControl )
	DMXELEMENT_UNPACK_FIELD( "mid point position", "0.5", float, m_PathParams.m_flMidPoint )
	DMXELEMENT_UNPACK_FIELD( "randomly select sequential CP pairs between start and end points", "0", bool, m_bUseRandomCPs )
END_PARTICLE_OPERATOR_UNPACK( C_INIT_CreateAlongPath )

void C_INIT_CreateAlongPath::InitNewParticlesScalar(
	CParticleCollection *pParticles, int start_p,
	int nParticleCount, int nAttributeWriteMask, void *pContext ) const
{
	for( ; nParticleCount--; start_p++ )
	{
		float *xyz = pParticles->GetFloatAttributePtrForWrite( PARTICLE_ATTRIBUTE_XYZ, start_p );
		const float *ct = pParticles->GetFloatAttributePtr( PARTICLE_ATTRIBUTE_CREATION_TIME, start_p );
		float *pxyz = pParticles->GetFloatAttributePtrForWrite( PARTICLE_ATTRIBUTE_PREV_XYZ, start_p );

		CPathParameters PathParams = m_PathParams;
		if ( m_bUseRandomCPs )
		{
			PathParams.m_nEndControlPointNumber = pParticles->RandomInt( PathParams.m_nStartControlPointNumber + 1, PathParams.m_nEndControlPointNumber );
			PathParams.m_nStartControlPointNumber = PathParams.m_nEndControlPointNumber - 1;
		}

		Vector StartPnt, MidP, EndPnt;
		pParticles->CalculatePathValues( PathParams, *ct, &StartPnt, &MidP, &EndPnt );

		float t = pParticles->RandomFloat( 0.0, 1.0 );

		Vector randpos;
		pParticles->RandomVector( -m_fMaxDistance, m_fMaxDistance, &randpos );

		// form delta terms needed for quadratic bezier
		Vector Delta0 = MidP - StartPnt;
		Vector Delta1 = EndPnt - MidP;

		Vector L0 = StartPnt + t * Delta0;
		Vector L1 = MidP + t * Delta1;

		Vector Pnt = L0 + ( L1 - L0 ) * t;

		Pnt += randpos;

		xyz[0] = Pnt.x;
		xyz[4] = Pnt.y;
		xyz[8] = Pnt.z;
		if ( pxyz && ( nAttributeWriteMask & PARTICLE_ATTRIBUTE_PREV_XYZ_MASK ) )
		{
			pxyz[0] = Pnt.x;
			pxyz[4] = Pnt.y;
			pxyz[8] = Pnt.z;
		}
	}
}


//-----------------------------------------------------------------------------
// Move particles between two control points
//-----------------------------------------------------------------------------
// The shipped binary sums the squared components in z, y, x order
static inline float MoveBetweenPointsLength( const Vector &vecDelta )
{
	return sqrt( ( vecDelta.z * vecDelta.z + vecDelta.y * vecDelta.y ) + vecDelta.x * vecDelta.x );
}

class C_INIT_MoveBetweenPoints : public CParticleInitializerOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_INIT_MoveBetweenPoints );

	uint32 GetWrittenAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_XYZ_MASK | PARTICLE_ATTRIBUTE_PREV_XYZ_MASK | PARTICLE_ATTRIBUTE_LIFE_DURATION_MASK;
	}

	uint32 GetReadAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_XYZ_MASK | PARTICLE_ATTRIBUTE_PREV_XYZ_MASK | PARTICLE_ATTRIBUTE_CREATION_TIME_MASK | PARTICLE_ATTRIBUTE_TRAIL_LENGTH_MASK;
	}

	virtual uint64 GetReadControlPointMask() const
	{
		return 1ULL << m_nEndControlPointNumber;
	}

	bool InitMultipleOverride ( void )
	{
		if ( m_bTrailBias )
			return true;
		else
			return false;
	}

	virtual void InitNewParticlesScalar( CParticleCollection *pParticles, int start_p,
										 int nParticleCount, int nAttributeWriteMask,
										 void *pContext) const;

	float m_flSpeedMin, m_flSpeedMax;
	float m_flEndSpread;
	float m_flStartOffset;
	float m_flEndOffset;
	int m_nEndControlPointNumber;
	bool m_bTrailBias;
};

DEFINE_PARTICLE_OPERATOR( C_INIT_MoveBetweenPoints, "Move Particles Between 2 Control Points", OPERATOR_GENERIC );

BEGIN_PARTICLE_INITIALIZER_OPERATOR_UNPACK( C_INIT_MoveBetweenPoints )
	DMXELEMENT_UNPACK_FIELD( "minimum speed", "1", float, m_flSpeedMin )
	DMXELEMENT_UNPACK_FIELD( "maximum speed", "1", float, m_flSpeedMax )
	DMXELEMENT_UNPACK_FIELD( "end spread", "0", float, m_flEndSpread )
	DMXELEMENT_UNPACK_FIELD( "start offset", "0", float, m_flStartOffset )
	DMXELEMENT_UNPACK_FIELD( "end offset", "0", float, m_flEndOffset )
	DMXELEMENT_UNPACK_FIELD( "bias lifetime by trail length", "0", bool, m_bTrailBias )
	DMXELEMENT_UNPACK_FIELD( "end control point", "1", int, m_nEndControlPointNumber )
END_PARTICLE_OPERATOR_UNPACK( C_INIT_MoveBetweenPoints )

void C_INIT_MoveBetweenPoints::InitNewParticlesScalar(
	CParticleCollection *pParticles, int start_p,
	int nParticleCount, int nAttributeWriteMask, void *pContext ) const
{
	bool bMoveStartPoint = ( m_flStartOffset != 0.0f );
	bool bMoveEndPoint = ( m_flEndOffset != 0.0f );

	for( ; nParticleCount--; start_p++ )
	{
		float *pxyz = pParticles->GetFloatAttributePtrForWrite( PARTICLE_ATTRIBUTE_XYZ, start_p );
		float *pPrevXYZ = pParticles->GetFloatAttributePtrForWrite( PARTICLE_ATTRIBUTE_PREV_XYZ, start_p );
		float *dtime = pParticles->GetFloatAttributePtrForWrite( PARTICLE_ATTRIBUTE_LIFE_DURATION, start_p );
		const float *ct = pParticles->GetFloatAttributePtr( PARTICLE_ATTRIBUTE_CREATION_TIME, start_p );

		Vector StartPnt( pxyz[0], pxyz[4], pxyz[8] );

		Vector vecEndPoint;
		pParticles->GetControlPointAtTime( m_nEndControlPointNumber, *ct, &vecEndPoint );

		Vector randpos( 0, 0, 0 );
		if ( m_flEndSpread > 0.0f )
		{
			pParticles->RandomVectorInUnitSphere( &randpos );
			randpos *= m_flEndSpread;
		}
		vecEndPoint += randpos;

		float flLength = MoveBetweenPointsLength( vecEndPoint - StartPnt );
		if ( bMoveEndPoint )
		{
			vecEndPoint += ( vecEndPoint - StartPnt ) * ( m_flEndOffset / ( flLength + FLT_EPSILON ) );
			flLength = MoveBetweenPointsLength( vecEndPoint - StartPnt );
		}
		if ( bMoveStartPoint )
		{
			StartPnt += ( vecEndPoint - StartPnt ) * ( m_flStartOffset / ( flLength + FLT_EPSILON ) );
			flLength = MoveBetweenPointsLength( vecEndPoint - StartPnt );
		}

		float flVelocity = pParticles->RandomFloat( m_flSpeedMin, m_flSpeedMax );

		if ( m_bTrailBias )
		{
			const float *pTrailLength = pParticles->GetFloatAttributePtr( PARTICLE_ATTRIBUTE_TRAIL_LENGTH, start_p );
			*dtime = ( flLength / ( flVelocity + FLT_EPSILON ) ) + *pTrailLength;
		}
		else
		{
			*dtime = flLength / ( flVelocity + FLT_EPSILON );
		}

		Vector vecVelocity = ( vecEndPoint - StartPnt ) * ( flVelocity / ( flLength + FLT_EPSILON ) );
		vecVelocity *= pParticles->m_flPreviousDt;

		if ( bMoveStartPoint )
		{
			// NOTE: shipped code writes the start point with a stride of 1 instead of 4
			pxyz[0] = StartPnt.x;
			pxyz[1] = StartPnt.y;
			pxyz[2] = StartPnt.z;
		}

		pPrevXYZ[0] = pxyz[0] - vecVelocity.x;
		pPrevXYZ[4] = pxyz[4] - vecVelocity.y;
		pPrevXYZ[8] = pxyz[8] - vecVelocity.z;
	}
}


//-----------------------------------------------------------------------------
// Remap initial scalar
//-----------------------------------------------------------------------------
class C_INIT_RemapScalar : public CParticleInitializerOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_INIT_RemapScalar );

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

	bool InitMultipleOverride ( void ) { return true; }

	virtual void InitNewParticlesScalar( CParticleCollection *pParticles, int start_p,
										 int nParticleCount, int nAttributeWriteMask,
										 void *pContext) const;

	int m_nFieldInput;
	int m_nFieldOutput;
	float m_flInputMin;
	float m_flInputMax;
	float m_flOutputMin;
	float m_flOutputMax;
	float m_flStartTime;
	float m_flEndTime;
	bool m_bScaleInitialRange;
	bool m_bActiveRange;
};

DEFINE_PARTICLE_OPERATOR( C_INIT_RemapScalar, "Remap Initial Scalar", OPERATOR_GENERIC );

BEGIN_PARTICLE_INITIALIZER_OPERATOR_UNPACK( C_INIT_RemapScalar )
	DMXELEMENT_UNPACK_FIELD( "emitter lifetime start time (seconds)", "-1", float, m_flStartTime )
	DMXELEMENT_UNPACK_FIELD( "emitter lifetime end time (seconds)", "-1", float, m_flEndTime )
	DMXELEMENT_UNPACK_FIELD_USERDATA( "input field", "8", int, m_nFieldInput, "intchoice particlefield_scalar" )
	DMXELEMENT_UNPACK_FIELD( "input minimum", "0", float, m_flInputMin )
	DMXELEMENT_UNPACK_FIELD( "input maximum", "1", float, m_flInputMax )
	DMXELEMENT_UNPACK_FIELD_USERDATA( "output field", "3", int, m_nFieldOutput, "intchoice particlefield_scalar" )
	DMXELEMENT_UNPACK_FIELD( "output minimum", "0", float, m_flOutputMin )
	DMXELEMENT_UNPACK_FIELD( "output maximum", "1", float, m_flOutputMax )
	DMXELEMENT_UNPACK_FIELD( "output is scalar of initial random range", "0", bool, m_bScaleInitialRange )
	DMXELEMENT_UNPACK_FIELD( "only active within specified input range", "0", bool, m_bActiveRange )
END_PARTICLE_OPERATOR_UNPACK( C_INIT_RemapScalar )

void C_INIT_RemapScalar::InitNewParticlesScalar(
	CParticleCollection *pParticles, int start_p,
	int nParticleCount, int nAttributeWriteMask, void *pContext ) const
{
	float flMin = m_flOutputMin;
	float flMax = m_flOutputMax;
	if ( ATTRIBUTES_WHICH_ARE_0_TO_1 & ( 1 << m_nFieldOutput ) )
	{
		flMin = ( flMin > 1.0f ) ? 1.0f : ( ( flMin < 0.0f ) ? 0.0f : flMin );
		flMax = ( flMax > 1.0f ) ? 1.0f : ( ( flMax < 0.0f ) ? 0.0f : flMax );
	}

	for( ; nParticleCount--; start_p++ )
	{
		const float *pCreationTime = pParticles->GetFloatAttributePtr( PARTICLE_ATTRIBUTE_CREATION_TIME, start_p );

		float flInput;
		if ( ATTRIBUTES_WHICH_ARE_INTS & ( 1 << m_nFieldInput ) )
		{
			const int *pInput = pParticles->GetIntAttributePtr( m_nFieldInput, start_p );
			flInput = float( *pInput );
		}
		else
		{
			const float *pInput = pParticles->GetFloatAttributePtr( m_nFieldInput, start_p );
			flInput = *pInput;
		}

		// only use within start/end time frame and, if set, active input range
		if ( ( ( *pCreationTime < m_flStartTime ) || ( *pCreationTime >= m_flEndTime ) ) && ( ( m_flStartTime != -1.0f ) && ( m_flEndTime != -1.0f ) ) ||
			 ( m_bActiveRange && ( flInput < m_flInputMin || flInput > m_flInputMax ) ) )
			continue;

		float *pOutput = pParticles->GetFloatAttributePtrForWrite( m_nFieldOutput, start_p );
		float flOutput = RemapValClamped( flInput, m_flInputMin, m_flInputMax, flMin, flMax );
		if ( m_bScaleInitialRange )
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
// Inherit velocity from a control point
//-----------------------------------------------------------------------------
class C_INIT_InheritVelocity : public CParticleInitializerOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_INIT_InheritVelocity );

	uint32 GetWrittenAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_PREV_XYZ_MASK;
	}

	uint32 GetReadAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_PREV_XYZ_MASK;
	}

	virtual uint64 GetReadControlPointMask() const
	{
		return 1ULL << m_nControlPointNumber;
	}

	virtual void InitParams( CParticleSystemDefinition *pDef )
	{
		m_nControlPointNumber = clamp( m_nControlPointNumber, 0, MAX_PARTICLE_CONTROL_POINTS - 1 );
	}

	bool InitMultipleOverride ( void ) { return true; }

	virtual void InitNewParticlesScalar( CParticleCollection *pParticles, int start_p,
										 int nParticleCount, int nAttributeWriteMask,
										 void *pContext) const;

	int m_nControlPointNumber;
	float m_flVelocityScale;
};

DEFINE_PARTICLE_OPERATOR( C_INIT_InheritVelocity, "Velocity Inherit from Control Point", OPERATOR_GENERIC );

BEGIN_PARTICLE_INITIALIZER_OPERATOR_UNPACK( C_INIT_InheritVelocity )
	DMXELEMENT_UNPACK_FIELD( "control point number", "0", int, m_nControlPointNumber )
	DMXELEMENT_UNPACK_FIELD( "velocity scale", "1", float, m_flVelocityScale )
END_PARTICLE_OPERATOR_UNPACK( C_INIT_InheritVelocity )

void C_INIT_InheritVelocity::InitNewParticlesScalar(
	CParticleCollection *pParticles, int start_p,
	int nParticleCount, int nAttributeWriteMask, void *pContext ) const
{
	Vector vecControlPointPrev;
	pParticles->GetControlPointAtPrevTime( m_nControlPointNumber, &vecControlPointPrev );
	Vector vecDeltaPos = pParticles->GetControlPointAtCurrentTime( m_nControlPointNumber ) - vecControlPointPrev;
	vecDeltaPos *= m_flVelocityScale;

	for( ; nParticleCount--; start_p++ )
	{
		float *pxyz_prev = pParticles->GetFloatAttributePtrForWrite( PARTICLE_ATTRIBUTE_PREV_XYZ, start_p );

		pxyz_prev[0] -= vecDeltaPos.x;
		pxyz_prev[4] -= vecDeltaPos.y;
		pxyz_prev[8] -= vecDeltaPos.z;
	}
}


//-----------------------------------------------------------------------------
// Set velocity from a control point
//-----------------------------------------------------------------------------
class C_INIT_VelocityFromCP : public CParticleInitializerOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_INIT_VelocityFromCP );

	uint32 GetWrittenAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_PREV_XYZ_MASK;
	}

	uint32 GetReadAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_PREV_XYZ_MASK;
	}

	virtual uint64 GetReadControlPointMask() const
	{
		uint64 nMask = 1ULL << m_nControlPoint;
		if ( m_nControlPointCompare != -1 )
		{
			nMask |= 1ULL << m_nControlPointCompare;
		}
		if ( m_nControlPointLocal != -1 )
		{
			nMask |= 1ULL << m_nControlPointLocal;
		}
		return nMask;
	}

	bool InitMultipleOverride ( void ) { return true; }

	virtual void InitNewParticlesScalar( CParticleCollection *pParticles, int start_p,
										 int nParticleCount, int nAttributeWriteMask,
										 void *pContext) const;

	int m_nControlPoint;
	int m_nControlPointCompare;
	int m_nControlPointLocal;
	float m_flVelocityScale;
	bool m_bDirectionOnly;
};

DEFINE_PARTICLE_OPERATOR( C_INIT_VelocityFromCP, "Velocity Set from Control Point", OPERATOR_GENERIC );

BEGIN_PARTICLE_INITIALIZER_OPERATOR_UNPACK( C_INIT_VelocityFromCP )
	DMXELEMENT_UNPACK_FIELD( "control point number", "0", int, m_nControlPoint )
	DMXELEMENT_UNPACK_FIELD( "velocity scale", "1", float, m_flVelocityScale )
	DMXELEMENT_UNPACK_FIELD( "comparison control point number", "-1", int, m_nControlPointCompare )
	DMXELEMENT_UNPACK_FIELD( "local space control point number", "-1", int, m_nControlPointLocal )
	DMXELEMENT_UNPACK_FIELD( "direction only", "0", bool, m_bDirectionOnly )
END_PARTICLE_OPERATOR_UNPACK( C_INIT_VelocityFromCP )

void C_INIT_VelocityFromCP::InitNewParticlesScalar(
	CParticleCollection *pParticles, int start_p,
	int nParticleCount, int nAttributeWriteMask, void *pContext ) const
{
	Vector vecVelocity;
	if ( m_nControlPointCompare > -1 )
	{
		vecVelocity = pParticles->GetControlPointAtCurrentTime( m_nControlPoint ) - pParticles->GetControlPointAtCurrentTime( m_nControlPointCompare );
	}
	else
	{
		vecVelocity = pParticles->GetControlPointAtCurrentTime( m_nControlPoint );
	}

	if ( m_nControlPointLocal > -1 )
	{
		Vector vecInput = vecVelocity;
		matrix3x4_t mat;
		pParticles->GetControlPointTransformAtCurrentTime( m_nControlPointLocal, &mat );
		VectorRotate( vecInput, mat, vecVelocity );
	}

	if ( m_bDirectionOnly )
	{
		VectorNormalize( vecVelocity );
	}

	float flPrevDt = pParticles->m_flPreviousDt;
	vecVelocity *= flPrevDt;
	vecVelocity *= m_flVelocityScale;

	for( ; nParticleCount--; start_p++ )
	{
		float *pxyz_prev = pParticles->GetFloatAttributePtrForWrite( PARTICLE_ATTRIBUTE_PREV_XYZ, start_p );

		pxyz_prev[0] -= vecVelocity.x;
		pxyz_prev[4] -= vecVelocity.y;
		pxyz_prev[8] -= vecVelocity.z;
	}
}


//-----------------------------------------------------------------------------
// Pre-age noise
//-----------------------------------------------------------------------------
class C_INIT_AgeNoise : public CParticleInitializerOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_INIT_AgeNoise );

	uint32 GetWrittenAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_CREATION_TIME_MASK;
	}

	uint32 GetReadAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_CREATION_TIME_MASK | PARTICLE_ATTRIBUTE_XYZ_MASK | PARTICLE_ATTRIBUTE_LIFE_DURATION_MASK;
	}

	bool InitMultipleOverride ( void ) { return true; }

	virtual void InitNewParticlesScalar( CParticleCollection *pParticles, int start_p,
										 int nParticleCount, int nAttributeWriteMask,
										 void *pContext) const;

	bool m_bAbsVal, m_bAbsValInv;
	float m_flOffset;
	float m_flAgeMin;
	float m_flAgeMax;
	float m_flNoiseScale, m_flNoiseScaleLoc;
	Vector m_vecOffsetLoc;
};

DEFINE_PARTICLE_OPERATOR( C_INIT_AgeNoise, "Lifetime Pre-Age Noise", OPERATOR_GENERIC );

BEGIN_PARTICLE_INITIALIZER_OPERATOR_UNPACK( C_INIT_AgeNoise )
	DMXELEMENT_UNPACK_FIELD( "time noise coordinate scale", "1.0", float, m_flNoiseScale )
	DMXELEMENT_UNPACK_FIELD( "spatial noise coordinate scale", "1.0", float, m_flNoiseScaleLoc )
	DMXELEMENT_UNPACK_FIELD( "time coordinate offset", "0", float, m_flOffset )
	DMXELEMENT_UNPACK_FIELD( "spatial coordinate offset", "0 0 0", Vector, m_vecOffsetLoc )
	DMXELEMENT_UNPACK_FIELD( "absolute value", "0", bool, m_bAbsVal )
	DMXELEMENT_UNPACK_FIELD( "invert absolute value", "0", bool, m_bAbsValInv )
	DMXELEMENT_UNPACK_FIELD( "start age minimum", "0", float, m_flAgeMin )
	DMXELEMENT_UNPACK_FIELD( "start age maximum", "1", float, m_flAgeMax )
END_PARTICLE_OPERATOR_UNPACK( C_INIT_AgeNoise )

void C_INIT_AgeNoise::InitNewParticlesScalar(
	CParticleCollection *pParticles, int start_p,
	int nParticleCount, int nAttributeWriteMask, void *pContext ) const
{
	float flAbsScale;
	int nAbsVal;
	nAbsVal = 0xffffffff;
	flAbsScale = 0.5;
	if ( m_bAbsVal )
	{
		nAbsVal = 0x7fffffff;
		flAbsScale = 1.0;
	}

	float fMin = m_flAgeMin;
	float fMax = m_flAgeMax;

	float CoordScale = m_flNoiseScale;
	float CoordScaleLoc = m_flNoiseScaleLoc;

	float ValueScale, ValueBase;
	ValueScale = ( flAbsScale * ( fMax - fMin ) );
	ValueBase = ( fMin + ( ( 1.0 - flAbsScale ) * ( fMax - fMin ) ) );

	for( ; nParticleCount--; start_p++ )
	{
		const float *pxyz = pParticles->GetFloatAttributePtr( PARTICLE_ATTRIBUTE_XYZ, start_p );
		const float *pCreationTime = pParticles->GetFloatAttributePtr( PARTICLE_ATTRIBUTE_CREATION_TIME, start_p );
		const float *pLifespan = pParticles->GetFloatAttributePtr( PARTICLE_ATTRIBUTE_LIFE_DURATION, start_p );
		float *pAttr = pParticles->GetFloatAttributePtrForWrite( PARTICLE_ATTRIBUTE_CREATION_TIME, start_p );

		float Offset = m_flOffset;
		Vector Coord, CoordLoc;
		CoordLoc.x = pxyz[0];
		CoordLoc.y = pxyz[4];
		CoordLoc.z = pxyz[8];
		CoordLoc += m_vecOffsetLoc;

		float Offset2 = *pCreationTime + Offset;
		Coord.Init( Offset2, Offset2, Offset2 );
		Coord *= CoordScale;
		CoordLoc *= CoordScaleLoc;
		Coord += CoordLoc;

		FourVectors fvNoise;
		fvNoise.DuplicateVector( Coord );
		fltx4 flNoise128 = NoiseSIMD( fvNoise );
		float flNoise = SubFloat( flNoise128, 0 );

		*( (int *) &flNoise ) &= nAbsVal;

		if ( m_bAbsValInv )
		{
			flNoise = 1.0 - flNoise;
		}

		float flAge = ( ValueBase + ( ValueScale * flNoise ) );
		flAge = clamp( flAge, 0.0f, 1.0f );

		flAge *= *pLifespan;

		*( pAttr ) = *pCreationTime - flAge;
	}
}


//-----------------------------------------------------------------------------
// Lifetime from sequence length
//-----------------------------------------------------------------------------
class C_INIT_SequenceLifeTime : public CParticleInitializerOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_INIT_SequenceLifeTime );

	uint32 GetWrittenAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_LIFE_DURATION_MASK;
	}

	uint32 GetReadAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_SEQUENCE_NUMBER_MASK;
	}

	bool InitMultipleOverride ( void ) { return true; }

	virtual void InitNewParticlesScalar( CParticleCollection *pParticles, int start_p,
										 int nParticleCount, int nAttributeWriteMask,
										 void *pContext) const;

	float m_flFramerate;
};

DEFINE_PARTICLE_OPERATOR( C_INIT_SequenceLifeTime, "Lifetime From Sequence", OPERATOR_GENERIC );

BEGIN_PARTICLE_INITIALIZER_OPERATOR_UNPACK( C_INIT_SequenceLifeTime )
	DMXELEMENT_UNPACK_FIELD( "Frames Per Second", "30", float, m_flFramerate )
END_PARTICLE_OPERATOR_UNPACK( C_INIT_SequenceLifeTime )

void C_INIT_SequenceLifeTime::InitNewParticlesScalar(
	CParticleCollection *pParticles, int start_p,
	int nParticleCount, int nAttributeWriteMask, void *pContext ) const
{
	if ( ( m_flFramerate != 0.0f ) && ( pParticles->m_Sheet() ) )
	{
		for( ; nParticleCount--; start_p++ )
		{
			float *dtime = pParticles->GetFloatAttributePtrForWrite( PARTICLE_ATTRIBUTE_LIFE_DURATION, start_p );
			const float *nSequence = pParticles->GetFloatAttributePtr( PARTICLE_ATTRIBUTE_SEQUENCE_NUMBER, start_p );

			if ( pParticles->m_Sheet()->m_SheetInfo[(int)*nSequence].m_flFrameSpan != 0 )
			{
				*dtime = pParticles->m_Sheet()->m_SheetInfo[(int)*nSequence].m_flFrameSpan / m_flFramerate;
			}
			else
			{
				*dtime = 1.0;
			}
		}
	}
}


//-----------------------------------------------------------------------------
// Position in control point hierarchy
//-----------------------------------------------------------------------------
class C_INIT_CreateInHierarchy : public CParticleInitializerOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_INIT_CreateInHierarchy );

	uint32 GetWrittenAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_XYZ_MASK | PARTICLE_ATTRIBUTE_PREV_XYZ_MASK;
	}

	uint32 GetReadAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_CREATION_TIME_MASK;
	}

	virtual uint64 GetReadControlPointMask() const
	{
		uint64 nStartMask = ( 1ULL << m_nDesiredStartPoint ) - 1;
		uint64 nEndMask = m_bUseHighestEndCP ? 0xFFFFFFFFFFFFFFFFULL : ( 1ULL << ( m_nDesiredEndPoint + 1 ) ) - 1;
		return nEndMask & (~nStartMask);
	}

	virtual void InitParams( CParticleSystemDefinition *pDef )
	{
		m_bDistanceBias = ( m_vecDistanceBias.x != 1.0f ) || ( m_vecDistanceBias.y != 1.0f ) || ( m_vecDistanceBias.z != 1.0f );
		m_bDistanceBiasAbs = ( m_vecDistanceBiasAbs.x != 0.0f ) || ( m_vecDistanceBiasAbs.y != 0.0f ) || ( m_vecDistanceBiasAbs.z != 0.0f );
	}

	virtual void InitNewParticlesScalar( CParticleCollection *pParticles, int start_p,
										 int nParticleCount, int nAttributeWriteMask,
										 void *pContext) const;

	float m_fMaxDistance;
	float m_flGrowthTime;
	float m_flDesiredMidPoint;
	int m_nOrientation;
	float m_flBulgeFactor;
	int m_nDesiredEndPoint;
	int m_nDesiredStartPoint;
	bool m_bUseHighestEndCP;
	Vector m_vecDistanceBias, m_vecDistanceBiasAbs;
	bool m_bDistanceBias, m_bDistanceBiasAbs;
};

DEFINE_PARTICLE_OPERATOR( C_INIT_CreateInHierarchy, "Position In CP Hierarchy", OPERATOR_PI_POSITION );

BEGIN_PARTICLE_INITIALIZER_OPERATOR_UNPACK( C_INIT_CreateInHierarchy )
	DMXELEMENT_UNPACK_FIELD( "maximum distance", "0", float, m_fMaxDistance )
	DMXELEMENT_UNPACK_FIELD( "bulge", "0", float, m_flBulgeFactor )
	DMXELEMENT_UNPACK_FIELD( "start control point number", "0", int, m_nDesiredStartPoint )
	DMXELEMENT_UNPACK_FIELD( "end control point number", "1", int, m_nDesiredEndPoint )
	DMXELEMENT_UNPACK_FIELD( "bulge control 0=random 1=orientation of start pnt 2=orientation of end point", "0", int, m_nOrientation )
	DMXELEMENT_UNPACK_FIELD( "mid point position", "0.5", float, m_flDesiredMidPoint )
	DMXELEMENT_UNPACK_FIELD( "growth time", "0.0", float, m_flGrowthTime )
	DMXELEMENT_UNPACK_FIELD( "use highest supplied end point", "0", bool, m_bUseHighestEndCP )
	DMXELEMENT_UNPACK_FIELD( "distance_bias", "1 1 1", Vector, m_vecDistanceBias )
	DMXELEMENT_UNPACK_FIELD( "distance_bias_absolute_value", "0 0 0", Vector, m_vecDistanceBiasAbs )
END_PARTICLE_OPERATOR_UNPACK( C_INIT_CreateInHierarchy )

void C_INIT_CreateInHierarchy::InitNewParticlesScalar(
	CParticleCollection *pParticles, int start_p,
	int nParticleCount, int nAttributeWriteMask, void *pContext ) const
{
	int nEndCP;
	float flGrowth;
	CPathParameters PathParams;
	PathParams.m_flBulge = m_flBulgeFactor;
	PathParams.m_nBulgeControl = m_nOrientation;
	PathParams.m_flMidPoint = m_flDesiredMidPoint;
	int nRealEndPoint;
	if ( m_bUseHighestEndCP )
	{
		nRealEndPoint = pParticles->GetHighestControlPoint();
	}
	else
	{
		nRealEndPoint = m_nDesiredEndPoint;
	}

	for( ; nParticleCount--; start_p++ )
	{
		float *xyz = pParticles->GetFloatAttributePtrForWrite( PARTICLE_ATTRIBUTE_XYZ, start_p );
		const float *ct = pParticles->GetFloatAttributePtr( PARTICLE_ATTRIBUTE_CREATION_TIME, start_p );
		float *pxyz = pParticles->GetFloatAttributePtrForWrite( PARTICLE_ATTRIBUTE_PREV_XYZ, start_p );

		if ( ( pParticles->m_flCurTime <= m_flGrowthTime ) && ( nRealEndPoint > 0 ) )
		{
			float flCurrMaxLevel = RemapValClamped( *ct, 0.0f, m_flGrowthTime, MIN( m_nDesiredStartPoint + 1, nRealEndPoint ), nRealEndPoint );
			nEndCP = pParticles->RandomInt( MIN( m_nDesiredStartPoint + 1, flCurrMaxLevel ), flCurrMaxLevel );
			flGrowth = RemapValClamped( *ct, 0.0f, m_flGrowthTime, 0.0f, 1.0f ) * ( flCurrMaxLevel / nRealEndPoint );
		}
		else
		{
			nEndCP = pParticles->RandomInt( MIN( m_nDesiredStartPoint + 1, nRealEndPoint ), nRealEndPoint );
			flGrowth = 1.0;
		}

		// Set up the path from the chosen point to its parent
		PathParams.m_nStartControlPointNumber = pParticles->GetControlPointParent( nEndCP );
		PathParams.m_nEndControlPointNumber = nEndCP;

		Vector StartPnt, MidP, EndPnt;
		pParticles->CalculatePathValues( PathParams, *ct, &StartPnt, &MidP, &EndPnt );
		EndPnt *= flGrowth;

		float t = pParticles->RandomFloat( 0.0, 1.0 );

		Vector randpos;
		pParticles->RandomVector( -m_fMaxDistance, m_fMaxDistance, &randpos );

		if ( m_bDistanceBiasAbs )
		{
			if ( m_vecDistanceBiasAbs.x != 0.0f )
			{
				randpos.x = fabs( randpos.x );
			}
			if ( m_vecDistanceBiasAbs.y != 0.0f )
			{
				randpos.y = fabs( randpos.y );
			}
			if ( m_vecDistanceBiasAbs.z != 0.0f )
			{
				randpos.z = fabs( randpos.z );
			}
		}
		randpos *= m_vecDistanceBias;

		// form delta terms needed for quadratic bezier
		Vector Delta0 = MidP - StartPnt;
		Vector Delta1 = EndPnt - MidP;

		Vector L0 = StartPnt + t * Delta0;
		Vector L1 = MidP + t * Delta1;

		Vector Pnt = L0 + ( L1 - L0 ) * t;

		Pnt += randpos;

		xyz[0] = Pnt.x;
		xyz[4] = Pnt.y;
		xyz[8] = Pnt.z;
		if ( pxyz && ( nAttributeWriteMask & PARTICLE_ATTRIBUTE_PREV_XYZ_MASK ) )
		{
			pxyz[0] = Pnt.x;
			pxyz[4] = Pnt.y;
			pxyz[8] = Pnt.z;
		}
	}
}


//-----------------------------------------------------------------------------
// Remap scalar to vector
//-----------------------------------------------------------------------------
class C_INIT_RemapScalarToVector : public CParticleInitializerOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_INIT_RemapScalarToVector );

	uint32 GetWrittenAttributes( void ) const
	{
		return ( 1 << m_nFieldOutput ) | PARTICLE_ATTRIBUTE_PREV_XYZ_MASK;
	}

	uint32 GetReadAttributes( void ) const
	{
		return 1 << m_nFieldInput;
	}

	virtual uint32 GetFilter( void ) const
	{
		return FILTER_PARAMETER_REMAPPING_MASK;
	}

	virtual uint64 GetReadControlPointMask() const
	{
		return 1ULL << m_nControlPointNumber;
	}

	bool InitMultipleOverride ( void ) { return true; }

	virtual void InitNewParticlesScalar( CParticleCollection *pParticles, int start_p,
										 int nParticleCount, int nAttributeWriteMask,
										 void *pContext) const;

	int m_nFieldInput;
	int m_nFieldOutput;
	float m_flInputMin;
	float m_flInputMax;
	Vector m_vecOutputMin;
	Vector m_vecOutputMax;
	float m_flStartTime;
	float m_flEndTime;
	bool m_bScaleInitialRange;
	int m_nControlPointNumber;
	bool m_bLocalCoords;
};

DEFINE_PARTICLE_OPERATOR( C_INIT_RemapScalarToVector, "Remap Scalar to Vector", OPERATOR_GENERIC );

BEGIN_PARTICLE_INITIALIZER_OPERATOR_UNPACK( C_INIT_RemapScalarToVector )
	DMXELEMENT_UNPACK_FIELD( "emitter lifetime start time (seconds)", "-1", float, m_flStartTime )
	DMXELEMENT_UNPACK_FIELD( "emitter lifetime end time (seconds)", "-1", float, m_flEndTime )
	DMXELEMENT_UNPACK_FIELD_USERDATA( "input field", "8", int, m_nFieldInput, "intchoice particlefield_scalar" )
	DMXELEMENT_UNPACK_FIELD( "input minimum", "0", float, m_flInputMin )
	DMXELEMENT_UNPACK_FIELD( "input maximum", "1", float, m_flInputMax )
	DMXELEMENT_UNPACK_FIELD_USERDATA( "output field", "0", int, m_nFieldOutput, "intchoice particlefield_vector" )
	DMXELEMENT_UNPACK_FIELD( "output minimum", "0 0 0", Vector, m_vecOutputMin )
	DMXELEMENT_UNPACK_FIELD( "output maximum", "1 1 1", Vector, m_vecOutputMax )
	DMXELEMENT_UNPACK_FIELD( "output is scalar of initial random range", "0", bool, m_bScaleInitialRange )
	DMXELEMENT_UNPACK_FIELD( "use local system", "1", bool, m_bLocalCoords )
	DMXELEMENT_UNPACK_FIELD( "control_point_number", "0", int, m_nControlPointNumber )
END_PARTICLE_OPERATOR_UNPACK( C_INIT_RemapScalarToVector )

void C_INIT_RemapScalarToVector::InitNewParticlesScalar(
	CParticleCollection *pParticles, int start_p,
	int nParticleCount, int nAttributeWriteMask, void *pContext ) const
{
	for( ; nParticleCount--; start_p++ )
	{
		const float *pCreationTime = pParticles->GetFloatAttributePtr( PARTICLE_ATTRIBUTE_CREATION_TIME, start_p );

		// only use within start/end time frame
		if ( ( ( *pCreationTime < m_flStartTime ) || ( *pCreationTime >= m_flEndTime ) ) && ( ( m_flStartTime != -1.0f ) && ( m_flEndTime != -1.0f ) ) )
			continue;

		const float *pInput = pParticles->GetFloatAttributePtr( m_nFieldInput, start_p );
		float *pOutput = pParticles->GetFloatAttributePtrForWrite( m_nFieldOutput, start_p );

		Vector vecOutput = vec3_origin;
		vecOutput.x = RemapValClamped( *pInput, m_flInputMin, m_flInputMax, m_vecOutputMin.x, m_vecOutputMax.x );
		vecOutput.y = RemapValClamped( *pInput, m_flInputMin, m_flInputMax, m_vecOutputMin.y, m_vecOutputMax.y );
		vecOutput.z = RemapValClamped( *pInput, m_flInputMin, m_flInputMax, m_vecOutputMin.z, m_vecOutputMax.z );

		if ( m_nFieldOutput == 0 )
		{
			float *pxyz = pParticles->GetFloatAttributePtrForWrite( PARTICLE_ATTRIBUTE_PREV_XYZ, start_p );
			if ( !m_bLocalCoords )
			{
				Vector vecControlPoint;
				pParticles->GetControlPointAtTime( m_nControlPointNumber, *pCreationTime, &vecControlPoint );
				vecOutput += vecControlPoint;
				Vector vecOutputPrev = vecOutput;
				if ( m_bScaleInitialRange )
				{
					Vector vecScaleInitial;
					Vector vecScaleInitialPrev;
					SetVectorFromAttribute( vecScaleInitial, pOutput );
					SetVectorFromAttribute( vecScaleInitialPrev, pxyz );
					vecOutput *= vecScaleInitial;
					vecOutputPrev *= vecScaleInitialPrev;
				}
				SetVectorAttribute( pOutput, vecOutput );
				SetVectorAttribute( pxyz, vecOutputPrev );
			}
			else
			{
				matrix3x4_t mat;
				pParticles->GetControlPointTransformAtTime( m_nControlPointNumber, *pCreationTime, &mat );
				Vector vecTransformLocal = vec3_origin;
				VectorTransform( vecOutput, mat, vecTransformLocal );
				vecOutput = vecTransformLocal;
				if ( m_bScaleInitialRange )
				{
					Vector vecScaleInitial;
					SetVectorFromAttribute( vecScaleInitial, pOutput );
					vecOutput *= vecScaleInitial;
				}
				SetVectorAttribute( pOutput, vecOutput );
				SetVectorAttribute( pxyz, vecOutput );
			}
		}
		else
		{
			if ( m_bScaleInitialRange )
			{
				Vector vecScaleInitial;
				SetVectorFromAttribute( vecScaleInitial, pOutput );
				vecOutput *= vecScaleInitial;
			}
			SetVectorAttribute( pOutput, vecOutput );
		}
	}
}


//-----------------------------------------------------------------------------
// Offset vector to vector
//-----------------------------------------------------------------------------
class C_INIT_OffsetVectorToVector : public CParticleInitializerOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_INIT_OffsetVectorToVector );

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

	virtual uint64 GetReadControlPointMask() const
	{
		return 1ULL << m_nControlPointNumber;
	}

	bool InitMultipleOverride ( void ) { return true; }

	virtual void InitNewParticlesScalar( CParticleCollection *pParticles, int start_p,
										 int nParticleCount, int nAttributeWriteMask,
										 void *pContext) const;

	int m_nFieldInput;
	int m_nFieldOutput;
	float m_flInputMin;
	float m_flInputMax;
	Vector m_vecOutputMin;
	Vector m_vecOutputMax;
	float m_flStartTime;
	float m_flEndTime;
	int m_nControlPointNumber;
};

DEFINE_PARTICLE_OPERATOR( C_INIT_OffsetVectorToVector, "Offset Vector to Vector", OPERATOR_GENERIC );

BEGIN_PARTICLE_INITIALIZER_OPERATOR_UNPACK( C_INIT_OffsetVectorToVector )
	DMXELEMENT_UNPACK_FIELD_USERDATA( "input field", "0", int, m_nFieldInput, "intchoice particlefield_vector" )
	DMXELEMENT_UNPACK_FIELD_USERDATA( "output field", "0", int, m_nFieldOutput, "intchoice particlefield_vector" )
	DMXELEMENT_UNPACK_FIELD( "output offset minimum", "0 0 0", Vector, m_vecOutputMin )
	DMXELEMENT_UNPACK_FIELD( "output offset maximum", "1 1 1", Vector, m_vecOutputMax )
END_PARTICLE_OPERATOR_UNPACK( C_INIT_OffsetVectorToVector )

void C_INIT_OffsetVectorToVector::InitNewParticlesScalar(
	CParticleCollection *pParticles, int start_p,
	int nParticleCount, int nAttributeWriteMask, void *pContext ) const
{
	for( ; nParticleCount--; start_p++ )
	{
		const float *pInput = pParticles->GetFloatAttributePtr( m_nFieldInput, start_p );
		float *pOutput = pParticles->GetFloatAttributePtrForWrite( m_nFieldOutput, start_p );

		Vector vecInput, vecOutput;
		SetVectorFromAttribute( vecInput, pInput );
		pParticles->RandomVector( m_vecOutputMin, m_vecOutputMax, &vecOutput );
		vecOutput += vecInput;
		SetVectorAttribute( pOutput, vecOutput );
	}
}


//-----------------------------------------------------------------------------
// Sequential position along a path
//-----------------------------------------------------------------------------
struct SequentialPathContext_t
{
	int m_nUnused;
	float m_flPathPosition;
	float m_flLoopOffset;
	float m_flCPPairPosition;
	float m_flStep;
	float m_flCPPairStep;
};

class C_INIT_CreateSequentialPath : public CParticleInitializerOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_INIT_CreateSequentialPath );

	uint32 GetWrittenAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_XYZ_MASK | PARTICLE_ATTRIBUTE_PREV_XYZ_MASK;
	}

	uint32 GetReadAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_CREATION_TIME_MASK;
	}

	virtual uint64 GetReadControlPointMask() const
	{
		uint64 nStartMask = ( 1ULL << m_PathParams.m_nStartControlPointNumber ) - 1;
		uint64 nEndMask = m_bCPPairs ? 0xFFFFFFFFFFFFFFFFULL : ( 1ULL << ( m_PathParams.m_nEndControlPointNumber + 1 ) ) - 1;
		return nEndMask & (~nStartMask);
	}

	virtual void InitializeContextData( CParticleCollection *pParticles, void *pContext ) const
	{
		SequentialPathContext_t *pCtx = reinterpret_cast<SequentialPathContext_t *>( pContext );
		pCtx->m_flPathPosition = 0.0f;
		if ( m_flNumToAssign > 1.0f )
		{
			pCtx->m_flStep = 1.0f / ( m_flNumToAssign - 1 );
		}
		else
		{
			pCtx->m_flStep = 0.0f;
		}
		pCtx->m_flLoopOffset = -1.0f;

		if ( m_bCPPairs )
		{
			pCtx->m_flCPPairPosition = m_PathParams.m_nStartControlPointNumber;
			if ( ( m_PathParams.m_nEndControlPointNumber - m_PathParams.m_nStartControlPointNumber > 1 ) && ( m_flNumToAssign > 1.0f ) )
			{
				pCtx->m_flStep = ( m_PathParams.m_nEndControlPointNumber - m_PathParams.m_nStartControlPointNumber ) * pCtx->m_flStep;
				pCtx->m_flCPPairStep = pCtx->m_flStep;
			}
			else
			{
				pCtx->m_flCPPairStep = 0.0f;
			}
		}
	}

	virtual void InitParams( CParticleSystemDefinition *pDef )
	{
		m_PathParams.ClampControlPointIndices();
	}

	size_t GetRequiredContextBytes( void ) const
	{
		return sizeof( SequentialPathContext_t );
	}

	virtual void InitNewParticlesScalar( CParticleCollection *pParticles, int start_p,
										 int nParticleCount, int nAttributeWriteMask,
										 void *pContext) const;

	float m_fMaxDistance;
	float m_flNumToAssign;
	bool m_bLoop;
	bool m_bCPPairs;
	struct CPathParameters m_PathParams;
};

DEFINE_PARTICLE_OPERATOR( C_INIT_CreateSequentialPath, "Position Along Path Sequential", OPERATOR_PI_POSITION );

BEGIN_PARTICLE_INITIALIZER_OPERATOR_UNPACK( C_INIT_CreateSequentialPath )
	DMXELEMENT_UNPACK_FIELD( "maximum distance", "0", float, m_fMaxDistance )
	DMXELEMENT_UNPACK_FIELD( "bulge", "0", float, m_PathParams.m_flBulge )
	DMXELEMENT_UNPACK_FIELD( "start control point number", "0", int, m_PathParams.m_nStartControlPointNumber )
	DMXELEMENT_UNPACK_FIELD( "end control point number", "0", int, m_PathParams.m_nEndControlPointNumber )
	DMXELEMENT_UNPACK_FIELD( "bulge control 0=random 1=orientation of start pnt 2=orientation of end point", "0", int, m_PathParams.m_nBulgeControl )
	DMXELEMENT_UNPACK_FIELD( "mid point position", "0.5", float, m_PathParams.m_flMidPoint )
	DMXELEMENT_UNPACK_FIELD( "particles to map from start to end", "100", float, m_flNumToAssign )
	DMXELEMENT_UNPACK_FIELD( "restart behavior (0 = bounce, 1 = loop )", "1", bool, m_bLoop )
	DMXELEMENT_UNPACK_FIELD( "Use sequential CP pairs between start and end point", "0", bool, m_bCPPairs )
END_PARTICLE_OPERATOR_UNPACK( C_INIT_CreateSequentialPath )

void C_INIT_CreateSequentialPath::InitNewParticlesScalar(
	CParticleCollection *pParticles, int start_p,
	int nParticleCount, int nAttributeWriteMask, void *pContext ) const
{
	SequentialPathContext_t *pCtx = reinterpret_cast<SequentialPathContext_t *>( pContext );

	for( ; nParticleCount--; start_p++ )
	{
		float *xyz = pParticles->GetFloatAttributePtrForWrite( PARTICLE_ATTRIBUTE_XYZ, start_p );
		const float *ct = pParticles->GetFloatAttributePtr( PARTICLE_ATTRIBUTE_CREATION_TIME, start_p );
		float *pxyz = pParticles->GetFloatAttributePtrForWrite( PARTICLE_ATTRIBUTE_PREV_XYZ, start_p );

		CPathParameters PathParams = m_PathParams;

		if ( m_bCPPairs )
		{
			int nNumCPs = PathParams.m_nEndControlPointNumber - PathParams.m_nStartControlPointNumber;
			if ( ( pCtx->m_flCPPairPosition > nNumCPs ) || ( pCtx->m_flCPPairPosition < PathParams.m_nStartControlPointNumber ) )
			{
				if ( m_bLoop )
				{
					pCtx->m_flCPPairPosition -= nNumCPs;
				}
				else
				{
					pCtx->m_flCPPairStep *= -1;
					pCtx->m_flCPPairPosition += pCtx->m_flCPPairStep * 2;
					pCtx->m_flStep *= -1;
					pCtx->m_flLoopOffset *= -1;
					pCtx->m_flPathPosition += pCtx->m_flStep * 2;
				}
			}
			PathParams.m_nStartControlPointNumber = pCtx->m_flCPPairPosition;
			PathParams.m_nEndControlPointNumber = PathParams.m_nStartControlPointNumber + 1;
		}

		if ( ( pCtx->m_flPathPosition > 1.0f + FLT_EPSILON ) || ( pCtx->m_flPathPosition < 0.0f ) )
		{
			if ( m_bLoop )
			{
				pCtx->m_flPathPosition += pCtx->m_flLoopOffset;
			}
			else
			{
				if ( !m_bCPPairs )
				{
					pCtx->m_flStep *= -1;
					pCtx->m_flLoopOffset *= -1;
					pCtx->m_flPathPosition += pCtx->m_flStep * 2;
				}
				else
				{
					pCtx->m_flPathPosition += pCtx->m_flLoopOffset;
				}
			}
		}

		float t = pCtx->m_flPathPosition;

		Vector StartPnt, MidP, EndPnt;
		pParticles->CalculatePathValues( PathParams, *ct, &StartPnt, &MidP, &EndPnt );

		Vector randpos;
		pParticles->RandomVector( -m_fMaxDistance, m_fMaxDistance, &randpos );

		// form delta terms needed for quadratic bezier
		Vector Delta0 = MidP - StartPnt;
		Vector Delta1 = EndPnt - MidP;

		Vector L0 = StartPnt + t * Delta0;
		Vector L1 = MidP + t * Delta1;

		Vector Pnt = L0 + ( L1 - L0 ) * t;

		Pnt += randpos;

		xyz[0] = Pnt.x;
		xyz[4] = Pnt.y;
		xyz[8] = Pnt.z;

		pxyz[0] = Pnt.x;
		pxyz[4] = Pnt.y;
		pxyz[8] = Pnt.z;

		pCtx->m_flPathPosition += pCtx->m_flStep;
		pCtx->m_flCPPairPosition += pCtx->m_flCPPairStep;
	}
}


//-----------------------------------------------------------------------------
// Velocity repulse from world
//-----------------------------------------------------------------------------
class C_INIT_InitialRepulsionVelocity : public CParticleInitializerOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_INIT_InitialRepulsionVelocity );

	uint32 GetWrittenAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_XYZ_MASK | PARTICLE_ATTRIBUTE_PREV_XYZ_MASK;
	}

	uint32 GetReadAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_RADIUS_MASK | PARTICLE_ATTRIBUTE_CREATION_TIME_MASK;
	}

	virtual uint64 GetReadControlPointMask() const
	{
		if ( m_bPerParticle )
			return 0;
		else if ( m_bInherit )
			return ( 1ULL << ( m_nControlPointNumber + 1 ) ) | ( 1ULL << m_nControlPointNumber );
		else
			return 1ULL << m_nControlPointNumber;
	}

	virtual uint64 GetNonPositionalControlPointMask() const
	{
		if ( !m_bPerParticle && m_bInherit )
			return ( 1ULL << ( m_nControlPointNumber + 1 ) ) | ( 1ULL << m_nControlPointNumber );
		return 0;
	}

	virtual void InitParams( CParticleSystemDefinition *pDef )
	{
		m_nCollisionGroupNumber = g_pParticleSystemMgr->Query()->GetCollisionGroupFromName( m_CollisionGroupName );
		m_nControlPointNumber = clamp( m_nControlPointNumber, 0, MAX_PARTICLE_CONTROL_POINTS - 1 );
	}

	bool InitMultipleOverride ( void ) { return true; }

	virtual void InitNewParticlesScalar( CParticleCollection *pParticles, int start_p,
										 int nParticleCount, int nAttributeWriteMask,
										 void *pContext) const;

	char m_CollisionGroupName[128];
	int m_nCollisionGroupNumber;
	Vector m_vecOutputMin;
	Vector m_vecOutputMax;
	int nRemainingBlocks;
	int m_nControlPointNumber;
	bool m_bPerParticle;
	bool m_bTranslate;
	bool m_bProportional;
	float m_flTraceLength;
	bool m_bPerParticleTR;
	bool m_bInherit;
	int m_nChildCP;
	int m_nChildGroupID;
};

DEFINE_PARTICLE_OPERATOR( C_INIT_InitialRepulsionVelocity, "Velocity Repulse from World", OPERATOR_GENERIC );

BEGIN_PARTICLE_INITIALIZER_OPERATOR_UNPACK( C_INIT_InitialRepulsionVelocity )
	DMXELEMENT_UNPACK_FIELD( "minimum velocity", "0 0 0", Vector, m_vecOutputMin )
	DMXELEMENT_UNPACK_FIELD( "maximum velocity", "1 1 1", Vector, m_vecOutputMax )
	DMXELEMENT_UNPACK_FIELD_STRING( "collision group", "NONE", m_CollisionGroupName )
	DMXELEMENT_UNPACK_FIELD( "control_point_number", "0", int, m_nControlPointNumber )
	DMXELEMENT_UNPACK_FIELD( "Per Particle World Collision Tests", "0", bool, m_bPerParticle )
	DMXELEMENT_UNPACK_FIELD( "Use radius for Per Particle Trace Length", "0", bool, m_bPerParticleTR )
	DMXELEMENT_UNPACK_FIELD( "Offset instead of accelerate", "0", bool, m_bTranslate )
	DMXELEMENT_UNPACK_FIELD( "Offset proportional to radius 0/1", "0", bool, m_bProportional )
	DMXELEMENT_UNPACK_FIELD( "Trace Length", "64.0", float, m_flTraceLength )
	DMXELEMENT_UNPACK_FIELD( "Inherit from Parent", "0", bool, m_bInherit )
	DMXELEMENT_UNPACK_FIELD( "control points to broadcast to children (n + 1)", "-1", int, m_nChildCP )
	DMXELEMENT_UNPACK_FIELD( "Child Group ID to affect", "0", int, m_nChildGroupID )
END_PARTICLE_OPERATOR_UNPACK( C_INIT_InitialRepulsionVelocity )

void C_INIT_InitialRepulsionVelocity::InitNewParticlesScalar(
	CParticleCollection *pParticles, int start_p,
	int nParticleCount, int nAttributeWriteMask, void *pContext ) const
{
	Vector d[6];

	// All cardinal directions
	d[0] = Vector(  1,  0,  0 );
	d[1] = Vector( -1,  0,  0 );
	d[2] = Vector(  0,  1,  0 );
	d[3] = Vector(  0, -1,  0 );
	d[4] = Vector(  0,  0,  1 );
	d[5] = Vector(  0,  0, -1 );

	Vector resultDirection;
	float resultForce;

	if ( m_bPerParticle )
	{
		for( ; nParticleCount--; start_p++ )
		{
			float *pxyz = pParticles->GetFloatAttributePtrForWrite( PARTICLE_ATTRIBUTE_XYZ, start_p );
			float *pxyz_prev = pParticles->GetFloatAttributePtrForWrite( PARTICLE_ATTRIBUTE_PREV_XYZ, start_p );
			const float *radius = pParticles->GetFloatAttributePtr( PARTICLE_ATTRIBUTE_RADIUS, start_p );

			Vector vecCurrentPos;
			SetVectorFromAttribute( vecCurrentPos, pxyz );

			resultDirection.Init();
			resultForce = 0.0f;

			// Get the aggregate force vector
			// NOTE: shipped code overwrites d[] in place, so it shrinks from particle to particle
			for ( int i = 0; i < 6; i++ )
			{
				float flTraceDistance = m_flTraceLength;
				if ( m_bPerParticleTR )
				{
					flTraceDistance = *radius;
				}

				Vector endpos = vecCurrentPos + ( d[i] * flTraceDistance );

				CBaseTrace tr;
				g_pParticleSystemMgr->Query()->TraceLine( vecCurrentPos, endpos, CONTENTS_SOLID, NULL, m_nCollisionGroupNumber, &tr );

				// Push back a proportional amount to the probe
				d[i] = -d[i] * ( 1.0f - tr.fraction );

				resultForce += 1.0f - tr.fraction;
				resultDirection += d[i];
			}

			// If we've hit nothing, then point up
			if ( resultDirection == vec3_origin )
			{
				resultDirection = Vector( 0, 0, 1 );
				resultForce = 0.0f;
			}

			VectorNormalize( resultDirection );
			resultDirection *= resultForce;

			Vector vecRepulsionAmount;
			vecRepulsionAmount.x = Lerp( resultForce, m_vecOutputMin.x, m_vecOutputMax.x );
			vecRepulsionAmount.y = Lerp( resultForce, m_vecOutputMin.y, m_vecOutputMax.y );
			vecRepulsionAmount.z = Lerp( resultForce, m_vecOutputMin.z, m_vecOutputMax.z );

			vecRepulsionAmount *= resultDirection;

			if ( m_bProportional )
			{
				vecRepulsionAmount *= *radius;
			}

			pxyz[0] += vecRepulsionAmount.x;
			pxyz[4] += vecRepulsionAmount.y;
			pxyz[8] += vecRepulsionAmount.z;

			if ( m_bTranslate )
			{
				pxyz_prev[0] += vecRepulsionAmount.x;
				pxyz_prev[4] += vecRepulsionAmount.y;
				pxyz_prev[8] += vecRepulsionAmount.z;
			}
		}
	}
	else
	{
		Vector vecRepulsionAmount;

		if ( m_bInherit )
		{
			const float *ct = pParticles->GetFloatAttributePtr( PARTICLE_ATTRIBUTE_CREATION_TIME, start_p );
			pParticles->GetControlPointAtTime( m_nControlPointNumber, *ct, &resultDirection );
			Vector vecPassedForce;
			pParticles->GetControlPointAtTime( m_nControlPointNumber + 1, *ct, &vecPassedForce );

			vecRepulsionAmount.x = Lerp( vecPassedForce.x, m_vecOutputMin.x, m_vecOutputMax.x );
			vecRepulsionAmount.y = Lerp( vecPassedForce.x, m_vecOutputMin.y, m_vecOutputMax.y );
			vecRepulsionAmount.z = Lerp( vecPassedForce.x, m_vecOutputMin.z, m_vecOutputMax.z );

			vecRepulsionAmount *= resultDirection;
		}
		else
		{
			const float *ct = pParticles->GetFloatAttributePtr( PARTICLE_ATTRIBUTE_CREATION_TIME, start_p );
			Vector vecControlPoint;
			pParticles->GetControlPointAtTime( m_nControlPointNumber, *ct, &vecControlPoint );

			Vector vecCurrentPos = vecControlPoint;

			resultDirection.Init();
			resultForce = 0.0f;

			for ( int i = 0; i < 6; i++ )
			{
				Vector endpos = vecCurrentPos + ( d[i] * m_flTraceLength );

				CBaseTrace tr;
				g_pParticleSystemMgr->Query()->TraceLine( vecCurrentPos, endpos, CONTENTS_SOLID, NULL, m_nCollisionGroupNumber, &tr );

				d[i] = -d[i] * ( 1.0f - tr.fraction );

				resultForce += 1.0f - tr.fraction;
				resultDirection += d[i];
			}

			if ( resultDirection == vec3_origin )
			{
				resultDirection = Vector( 0, 0, 1 );
				resultForce = 0.0f;
			}

			VectorNormalize( resultDirection );
			resultDirection *= resultForce;

			vecRepulsionAmount.x = Lerp( resultForce, m_vecOutputMin.x, m_vecOutputMax.x );
			vecRepulsionAmount.y = Lerp( resultForce, m_vecOutputMin.y, m_vecOutputMax.y );
			vecRepulsionAmount.z = Lerp( resultForce, m_vecOutputMin.z, m_vecOutputMax.z );

			vecRepulsionAmount *= resultDirection;

			if ( m_nChildCP != -1 )
			{
				for( CParticleCollection *pChild = pParticles->m_Children.m_pHead; pChild; pChild = pChild->m_pNext )
				{
					if ( pChild->GetGroupID() == m_nChildGroupID )
					{
						Vector vecPassForce = Vector( resultForce, 0, 0 );
						pChild->SetControlPoint( m_nChildCP, resultDirection );
						pChild->SetControlPoint( m_nChildCP + 1, vecPassForce );
					}
				}
			}
		}

		for( ; nParticleCount--; start_p++ )
		{
			float *pxyz = pParticles->GetFloatAttributePtrForWrite( PARTICLE_ATTRIBUTE_XYZ, start_p );
			float *pxyz_prev = pParticles->GetFloatAttributePtrForWrite( PARTICLE_ATTRIBUTE_PREV_XYZ, start_p );

			// NOTE: shipped code scales the shared amount, so the radius scale accumulates per particle
			if ( m_bProportional )
			{
				const float *radius = pParticles->GetFloatAttributePtr( PARTICLE_ATTRIBUTE_RADIUS, start_p );
				vecRepulsionAmount *= *radius;
			}

			pxyz[0] += vecRepulsionAmount.x;
			pxyz[4] += vecRepulsionAmount.y;
			pxyz[8] += vecRepulsionAmount.z;

			if ( m_bTranslate )
			{
				pxyz_prev[0] += vecRepulsionAmount.x;
				pxyz_prev[4] += vecRepulsionAmount.y;
				pxyz_prev[8] += vecRepulsionAmount.z;
			}
		}
	}
}


//-----------------------------------------------------------------------------
// Random yaw flip
//-----------------------------------------------------------------------------
class C_INIT_RandomYawFlip : public CParticleInitializerOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_INIT_RandomYawFlip );

	uint32 GetWrittenAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_YAW_MASK;
	}

	uint32 GetReadAttributes( void ) const
	{
		return 0;
	}

	bool InitMultipleOverride ( void ) { return true; }

	virtual void InitNewParticlesScalar( CParticleCollection *pParticles, int start_p,
										 int nParticleCount, int nAttributeWriteMask,
										 void *pContext) const;

	float m_flPercent;
};

DEFINE_PARTICLE_OPERATOR( C_INIT_RandomYawFlip, "Rotation Yaw Flip Random", OPERATOR_GENERIC );

BEGIN_PARTICLE_INITIALIZER_OPERATOR_UNPACK( C_INIT_RandomYawFlip )
	DMXELEMENT_UNPACK_FIELD( "Flip Percentage", ".5", float, m_flPercent )
END_PARTICLE_OPERATOR_UNPACK( C_INIT_RandomYawFlip )

void C_INIT_RandomYawFlip::InitNewParticlesScalar(
	CParticleCollection *pParticles, int start_p,
	int nParticleCount, int nAttributeWriteMask, void *pContext ) const
{
	for( ; nParticleCount--; start_p++ )
	{
		float flChance = pParticles->RandomFloat( 0.0, 1.0 );
		if ( flChance < m_flPercent )
		{
			float *drot = pParticles->GetFloatAttributePtrForWrite( PARTICLE_ATTRIBUTE_YAW, start_p );
			*drot += (float)M_PI;
		}
	}
}


//-----------------------------------------------------------------------------
// Random second sequence
//-----------------------------------------------------------------------------
class C_INIT_RandomSecondSequence : public CParticleInitializerOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_INIT_RandomSecondSequence );

	uint32 GetWrittenAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_SEQUENCE_NUMBER1_MASK;
	}

	uint32 GetReadAttributes( void ) const
	{
		return 0;
	}

	virtual void InitParams( CParticleSystemDefinition *pDef )
	{
		// TODO: Validate the ranges here!
	}

	virtual void InitNewParticlesScalar( CParticleCollection *pParticles, int start_p,
										 int nParticleCount, int nAttributeWriteMask,
										 void *pContext) const;

	int m_nSequenceMin;
	int m_nSequenceMax;
};

DEFINE_PARTICLE_OPERATOR( C_INIT_RandomSecondSequence, "Sequence Two Random", OPERATOR_GENERIC );

BEGIN_PARTICLE_INITIALIZER_OPERATOR_UNPACK( C_INIT_RandomSecondSequence )
	DMXELEMENT_UNPACK_FIELD_USERDATA( "sequence_min", "0", int, m_nSequenceMin, "sheetsequencepicker_second" )
	DMXELEMENT_UNPACK_FIELD_USERDATA( "sequence_max", "0", int, m_nSequenceMax, "sheetsequencepicker_second" )
END_PARTICLE_OPERATOR_UNPACK( C_INIT_RandomSecondSequence )

void C_INIT_RandomSecondSequence::InitNewParticlesScalar(
	CParticleCollection *pParticles, int start_p,
	int nParticleCount, int nAttributeWriteMask, void *pContext ) const
{
	for( ; nParticleCount--; start_p++ )
	{
		float *pSequence = pParticles->GetFloatAttributePtrForWrite( PARTICLE_ATTRIBUTE_SEQUENCE_NUMBER1, start_p );
		*pSequence = pParticles->RandomInt( m_nSequenceMin, m_nSequenceMax );
	}
}


//-----------------------------------------------------------------------------
// Remap control point to scalar
//-----------------------------------------------------------------------------
class C_INIT_RemapCPtoScalar : public CParticleInitializerOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_INIT_RemapCPtoScalar );

	uint32 GetWrittenAttributes( void ) const
	{
		return 1 << m_nFieldOutput;
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
		return 1ULL << m_nCPInput;
	}

	virtual uint64 GetNonPositionalControlPointMask() const
	{
		return 1ULL << m_nCPInput;
	}

	bool InitMultipleOverride ( void ) { return true; }

	virtual void InitParams( CParticleSystemDefinition *pDef )
	{
		m_nField = int( clamp( m_nField, 0, 2 ) );
	}

	virtual void InitNewParticlesScalar( CParticleCollection *pParticles, int start_p,
										 int nParticleCount, int nAttributeWriteMask,
										 void *pContext) const;

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
};

DEFINE_PARTICLE_OPERATOR( C_INIT_RemapCPtoScalar, "Remap Control Point to Scalar", OPERATOR_GENERIC );

BEGIN_PARTICLE_INITIALIZER_OPERATOR_UNPACK( C_INIT_RemapCPtoScalar )
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
END_PARTICLE_OPERATOR_UNPACK( C_INIT_RemapCPtoScalar )

void C_INIT_RemapCPtoScalar::InitNewParticlesScalar(
	CParticleCollection *pParticles, int start_p,
	int nParticleCount, int nAttributeWriteMask, void *pContext ) const
{
	float flMin = m_flOutputMin;
	float flMax = m_flOutputMax;
	if ( ATTRIBUTES_WHICH_ARE_0_TO_1 & ( 1 << m_nFieldOutput ) )
	{
		flMin = clamp( m_flOutputMin, 0.0f, 1.0f );
		flMax = clamp( m_flOutputMax, 0.0f, 1.0f );
	}

	const float *pCreationTime0 = pParticles->GetFloatAttributePtr( PARTICLE_ATTRIBUTE_CREATION_TIME, start_p );
	Vector vecControlPoint;
	pParticles->GetControlPointAtTime( m_nCPInput, *pCreationTime0, &vecControlPoint );
	float flInput = vecControlPoint[m_nField];

	for( ; nParticleCount--; start_p++ )
	{
		const float *pCreationTime = pParticles->GetFloatAttributePtr( PARTICLE_ATTRIBUTE_CREATION_TIME, start_p );

		// only use within start/end time frame
		if ( ( ( *pCreationTime < m_flStartTime ) || ( *pCreationTime >= m_flEndTime ) ) && ( ( m_flStartTime != -1.0f ) && ( m_flEndTime != -1.0f ) ) )
			continue;

		float *pOutput = pParticles->GetFloatAttributePtrForWrite( m_nFieldOutput, start_p );
		float flOutput = RemapValClamped( flInput, m_flInputMin, m_flInputMax, flMin, flMax );
		if ( m_bScaleInitialRange )
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
// Remap control point to vector
//-----------------------------------------------------------------------------
class C_INIT_RemapCPtoVector : public CParticleInitializerOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_INIT_RemapCPtoVector );

	uint32 GetWrittenAttributes( void ) const
	{
		return ( 1 << m_nFieldOutput ) | PARTICLE_ATTRIBUTE_PREV_XYZ_MASK;
	}

	uint32 GetReadAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_CREATION_TIME_MASK;
	}

	virtual uint32 GetFilter( void ) const
	{
		return FILTER_PARAMETER_REMAPPING_MASK;
	}

	virtual uint64 GetReadControlPointMask() const
	{
		uint64 nMask = 1ULL << m_nCPInput;
		if ( m_nLocalSpaceCP != -1 )
		{
			nMask |= 1ULL << m_nLocalSpaceCP;
		}
		return nMask;
	}

	bool InitMultipleOverride ( void ) { return true; }

	virtual void InitNewParticlesScalar( CParticleCollection *pParticles, int start_p,
										 int nParticleCount, int nAttributeWriteMask,
										 void *pContext) const;

	int m_nCPInput;
	int m_nFieldOutput;
	Vector m_vInputMin;
	Vector m_vInputMax;
	Vector m_vOutputMin;
	Vector m_vOutputMax;
	float m_flStartTime;
	float m_flEndTime;
	bool m_bScaleInitialRange;
	bool m_bOffset;
	bool m_bAccelerate;
	int m_nLocalSpaceCP;
};

DEFINE_PARTICLE_OPERATOR( C_INIT_RemapCPtoVector, "Remap Control Point to Vector", OPERATOR_GENERIC );

BEGIN_PARTICLE_INITIALIZER_OPERATOR_UNPACK( C_INIT_RemapCPtoVector )
	DMXELEMENT_UNPACK_FIELD( "emitter lifetime start time (seconds)", "-1", float, m_flStartTime )
	DMXELEMENT_UNPACK_FIELD( "emitter lifetime end time (seconds)", "-1", float, m_flEndTime )
	DMXELEMENT_UNPACK_FIELD( "input control point number", "0", int, m_nCPInput )
	DMXELEMENT_UNPACK_FIELD( "input minimum", "0 0 0", Vector, m_vInputMin )
	DMXELEMENT_UNPACK_FIELD( "input maximum", "0 0 0", Vector, m_vInputMax )
	DMXELEMENT_UNPACK_FIELD_USERDATA( "output field", "0", int, m_nFieldOutput, "intchoice particlefield_vector" )
	DMXELEMENT_UNPACK_FIELD( "output minimum", "0 0 0", Vector, m_vOutputMin )
	DMXELEMENT_UNPACK_FIELD( "output maximum", "0 0 0", Vector, m_vOutputMax )
	DMXELEMENT_UNPACK_FIELD( "output is scalar of initial random range", "0", bool, m_bScaleInitialRange )
	DMXELEMENT_UNPACK_FIELD( "offset position", "0", bool, m_bOffset )
	DMXELEMENT_UNPACK_FIELD( "accelerate position", "0", bool, m_bAccelerate )
	DMXELEMENT_UNPACK_FIELD( "local space CP", "-1", int, m_nLocalSpaceCP )
END_PARTICLE_OPERATOR_UNPACK( C_INIT_RemapCPtoVector )

void C_INIT_RemapCPtoVector::InitNewParticlesScalar(
	CParticleCollection *pParticles, int start_p,
	int nParticleCount, int nAttributeWriteMask, void *pContext ) const
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

	for( ; nParticleCount--; start_p++ )
	{
		const float *pCreationTime = pParticles->GetFloatAttributePtr( PARTICLE_ATTRIBUTE_CREATION_TIME, start_p );

		// only use within start/end time frame
		if ( ( ( *pCreationTime < m_flStartTime ) || ( *pCreationTime >= m_flEndTime ) ) && ( ( m_flStartTime != -1.0f ) && ( m_flEndTime != -1.0f ) ) )
			continue;

		float *pOutput = pParticles->GetFloatAttributePtrForWrite( m_nFieldOutput, start_p );

		Vector vOutput;
		vOutput.x = RemapValClamped( vecControlPoint.x, m_vInputMin.x, m_vInputMax.x, vOutputMinLocal.x, vOutputMaxLocal.x );
		vOutput.y = RemapValClamped( vecControlPoint.y, m_vInputMin.y, m_vInputMax.y, vOutputMinLocal.y, vOutputMaxLocal.y );
		vOutput.z = RemapValClamped( vecControlPoint.z, m_vInputMin.z, m_vInputMax.z, vOutputMinLocal.z, vOutputMaxLocal.z );

		if ( m_bScaleInitialRange )
		{
			Vector vOrgValue;
			SetVectorFromAttribute( vOrgValue, pOutput );
			vOutput *= vOrgValue;
		}

		// NOTE: shipped code compares the field index against a mask, so the clamp never runs
		if ( m_nFieldOutput == ATTRIBUTES_WHICH_ARE_COLOR_AND_OPACITY )
		{
			pOutput[0] = MAX( 0.0f, MIN( vOutput.x, 1.0f ) );
			pOutput[4] = MAX( 0.0f, MIN( vOutput.y, 1.0f ) );
			pOutput[8] = MAX( 0.0f, MIN( vOutput.z, 1.0f ) );
		}
		else
		{
			float *pXYZ_Prev = pParticles->GetFloatAttributePtrForWrite( PARTICLE_ATTRIBUTE_PREV_XYZ, start_p );
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
				if ( m_bOffset )
				{
					Vector vOrgValue;
					SetVectorFromAttribute( vOrgValue, pOutput );
					Vector vXYZPrev;
					SetVectorFromAttribute( vXYZPrev, pXYZ_Prev );
					vOutput += vOrgValue;
					vXYZPrev += vOutput;
					SetVectorAttribute( pXYZ_Prev, vXYZPrev );
				}
				SetVectorAttribute( pOutput, vOutput );
			}
		}
	}
}


//-----------------------------------------------------------------------------
// Position from chaotic attractor
//-----------------------------------------------------------------------------
class C_INIT_ChaoticAttractor : public CParticleInitializerOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_INIT_ChaoticAttractor );

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
		return 1ULL << m_nBaseCP;
	}

	virtual void InitializeContextData( CParticleCollection *pParticles, void *pContext ) const
	{
		Vector *pCtx = reinterpret_cast<Vector *>( pContext );
		pCtx->Init( 0, 0, 0 );
	}

	size_t GetRequiredContextBytes( void ) const
	{
		return sizeof( Vector );
	}

	virtual void InitNewParticlesScalar( CParticleCollection *pParticles, int start_p,
										 int nParticleCount, int nAttributeWriteMask,
										 void *pContext) const;

	float m_flAParm;
	float m_flBParm;
	float m_flCParm;
	float m_flDParm;
	float m_flScale;
	float m_flSpeedMin;
	float m_flSpeedMax;
	int m_nBaseCP;
	bool m_bUniformSpeed;
};

DEFINE_PARTICLE_OPERATOR( C_INIT_ChaoticAttractor, "Position From Chaotic Attractor", OPERATOR_PI_POSITION );

BEGIN_PARTICLE_INITIALIZER_OPERATOR_UNPACK( C_INIT_ChaoticAttractor )
	DMXELEMENT_UNPACK_FIELD( "Pickover A Parameter", "-0.9629629", float, m_flAParm )
	DMXELEMENT_UNPACK_FIELD( "Pickover B Parameter", "2.791139", float, m_flBParm )
	DMXELEMENT_UNPACK_FIELD( "Pickover C Parameter", "1.85185185", float, m_flCParm )
	DMXELEMENT_UNPACK_FIELD( "Pickover D Parameter", "1.5", float, m_flDParm )
	DMXELEMENT_UNPACK_FIELD( "Speed Min", "0", float, m_flSpeedMin )
	DMXELEMENT_UNPACK_FIELD( "Speed Max", "0", float, m_flSpeedMax )
	DMXELEMENT_UNPACK_FIELD( "Uniform speed", "0", bool, m_bUniformSpeed )
	DMXELEMENT_UNPACK_FIELD( "Relative Control point number", "0", int, m_nBaseCP )
	DMXELEMENT_UNPACK_FIELD( "Scale", "1", float, m_flScale )
END_PARTICLE_OPERATOR_UNPACK( C_INIT_ChaoticAttractor )

void C_INIT_ChaoticAttractor::InitNewParticlesScalar(
	CParticleCollection *pParticles, int start_p,
	int nParticleCount, int nAttributeWriteMask, void *pContext ) const
{
	Vector vecControlPoint = pParticles->GetControlPointAtCurrentTime( m_nBaseCP );
	Vector vecFwd, vecRight, vecUp;
	pParticles->GetControlPointOrientationAtCurrentTime( m_nBaseCP, &vecFwd, &vecRight, &vecUp );

	for( ; nParticleCount--; start_p++ )
	{
		float *xyz = pParticles->GetFloatAttributePtrForWrite( PARTICLE_ATTRIBUTE_XYZ, start_p );
		float *pxyz = pParticles->GetFloatAttributePtrForWrite( PARTICLE_ATTRIBUTE_PREV_XYZ, start_p );
		Vector *pCtx = reinterpret_cast<Vector *>( pContext );

		Vector vecOut = m_flScale * *pCtx;
		Vector vecNewPnt = vecControlPoint + vecOut.x * vecRight + vecOut.y * vecFwd + vecOut.z * vecUp;
		SetVectorAttribute( xyz, vecNewPnt );

		// Pickover attractor; the shipped code goes through the double precision libm calls
		Vector vecNext;
		vecNext.x = (float)sin( (double)( m_flAParm * pCtx->y ) ) - pCtx->z * (float)cos( (double)( m_flBParm * pCtx->x ) );
		vecNext.z = (float)sin( (double)pCtx->x );
		vecNext.y = pCtx->z * (float)sin( (double)( m_flCParm * pCtx->x ) ) - (float)cos( (double)( m_flDParm * pCtx->y ) );

		if ( fabs( m_flSpeedMax ) > 0.0 )
		{
			Vector vecVelocity = vecNext - *pCtx;
			if ( !m_bUniformSpeed )
			{
				float flLen = sqrt( ( vecVelocity.z * vecVelocity.z + vecVelocity.y * vecVelocity.y ) + vecVelocity.x * vecVelocity.x );
				if ( fabs( flLen ) > 0.0 )
				{
					vecVelocity *= 1.0f / flLen;
				}
			}
			vecVelocity *= pParticles->RandomInt( m_flSpeedMin, m_flSpeedMax );
			SetVectorAttribute( pxyz, vecNewPnt + pParticles->m_flPreviousDt * vecVelocity );
		}
		else
		{
			SetVectorAttribute( pxyz, vecNewPnt );
		}
		*pCtx = vecNext;
	}
}


//-----------------------------------------------------------------------------
// Position from parent particles
//-----------------------------------------------------------------------------
class C_INIT_CreateFromParentParticles : public CParticleInitializerOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_INIT_CreateFromParentParticles );

	uint32 GetWrittenAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_XYZ_MASK | PARTICLE_ATTRIBUTE_PREV_XYZ_MASK | PARTICLE_ATTRIBUTE_LIFE_DURATION_MASK;
	}

	uint32 GetReadAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_CREATION_TIME_MASK;
	}

	virtual void InitializeContextData( CParticleCollection *pParticles, void *pContext ) const
	{
		int *pCtx = reinterpret_cast<int *>( pContext );
		*pCtx = 0;
	}

	size_t GetRequiredContextBytes( void ) const
	{
		return sizeof( int );
	}

	virtual void InitNewParticlesScalar( CParticleCollection *pParticles, int start_p,
										 int nParticleCount, int nAttributeWriteMask,
										 void *pContext) const;

	float m_flVelocityScale;
	bool m_bRandomDistribution;
};

DEFINE_PARTICLE_OPERATOR( C_INIT_CreateFromParentParticles, "Position From Parent Particles", OPERATOR_PI_POSITION );

BEGIN_PARTICLE_INITIALIZER_OPERATOR_UNPACK( C_INIT_CreateFromParentParticles )
	DMXELEMENT_UNPACK_FIELD( "Inherited Velocity Scale", "0", float, m_flVelocityScale )
	DMXELEMENT_UNPACK_FIELD( "Random Parent Particle Distribution", "0", bool, m_bRandomDistribution )
END_PARTICLE_OPERATOR_UNPACK( C_INIT_CreateFromParentParticles )

void C_INIT_CreateFromParentParticles::InitNewParticlesScalar(
	CParticleCollection *pParticles, int start_p,
	int nParticleCount, int nAttributeWriteMask, void *pContext ) const
{
	if ( !pParticles->m_pParent )
	{
		for( ; nParticleCount--; start_p++ )
		{
			float *xyz = pParticles->GetFloatAttributePtrForWrite( PARTICLE_ATTRIBUTE_XYZ, start_p );
			float *pxyz = pParticles->GetFloatAttributePtrForWrite( PARTICLE_ATTRIBUTE_PREV_XYZ, start_p );
			SetVectorAttribute( xyz, vec3_origin );
			SetVectorAttribute( pxyz, vec3_origin );
		}
		return;
	}

	int nActiveParticles = pParticles->m_pParent->m_nActiveParticles;

	if ( nActiveParticles == 0 )
	{
		for( ; nParticleCount--; start_p++ )
		{
			float *lifespan = pParticles->GetFloatAttributePtrForWrite( PARTICLE_ATTRIBUTE_LIFE_DURATION, start_p );
			*lifespan = 0.0f;
		}
		return;
	}

	nActiveParticles = MAX( 0, nActiveParticles - 1 );

	for( ; nParticleCount--; start_p++ )
	{
		int *pCtx = reinterpret_cast<int *>( pContext );
		if ( m_bRandomDistribution )
		{
			*pCtx = pParticles->RandomInt( 0, nActiveParticles );
		}
		else if ( *pCtx > nActiveParticles )
		{
			*pCtx = 0;
		}

		float *xyz = pParticles->GetFloatAttributePtrForWrite( PARTICLE_ATTRIBUTE_XYZ, start_p );
		float *pxyz = pParticles->GetFloatAttributePtrForWrite( PARTICLE_ATTRIBUTE_PREV_XYZ, start_p );
		int nParentParticle = *pCtx;
		const float *pParent_xyz = pParticles->m_pParent->GetFloatAttributePtr( PARTICLE_ATTRIBUTE_XYZ, nParentParticle );
		const float *pParent_pxyz = pParticles->m_pParent->GetFloatAttributePtr( PARTICLE_ATTRIBUTE_PREV_XYZ, nParentParticle );
		const float *ct = pParticles->GetFloatAttributePtr( PARTICLE_ATTRIBUTE_CREATION_TIME, start_p );

		// interpolate to the creation time within the parent's last step
		float flPrevTime = pParticles->m_flCurTime - pParticles->m_flDt;
		float flSubFrame = RemapValClamped( *ct, flPrevTime, pParticles->m_flCurTime, 0, 1 );

		Vector vecParentXYZ, vecParentPrevXYZ;
		vecParentPrevXYZ.x = pParent_pxyz[0];
		vecParentPrevXYZ.y = pParent_pxyz[4];
		vecParentPrevXYZ.z = pParent_pxyz[8];
		vecParentXYZ.x = pParent_xyz[0];
		vecParentXYZ.y = pParent_xyz[4];
		vecParentXYZ.z = pParent_xyz[8];

		Vector vecSubFramePos;
		VectorLerp( vecParentPrevXYZ, vecParentXYZ, flSubFrame, vecSubFramePos );

		Vector vecPrevXYZ;
		VectorLerp( vecSubFramePos, vecParentPrevXYZ, m_flVelocityScale, vecPrevXYZ );
		SetVectorAttribute( pxyz, vecPrevXYZ );
		SetVectorAttribute( xyz, vecSubFramePos );

		( *pCtx )++;
	}
}


//-----------------------------------------------------------------------------
// Remap initial distance to control point to scalar
//-----------------------------------------------------------------------------
class C_INIT_DistanceToCPInit : public CParticleInitializerOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_INIT_DistanceToCPInit );

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

	virtual uint64 GetReadControlPointMask() const
	{
		return 1ULL << m_nStartCP;
	}

	bool InitMultipleOverride ( void ) { return true; }

	virtual void InitParams( CParticleSystemDefinition *pDef )
	{
		m_nCollisionGroupNumber = g_pParticleSystemMgr->Query()->GetCollisionGroupFromName( m_CollisionGroupName );
		m_nStartCP = clamp( m_nStartCP, 0, MAX_PARTICLE_CONTROL_POINTS - 1 );
	}

	virtual void InitNewParticlesScalar( CParticleCollection *pParticles, int start_p,
										 int nParticleCount, int nAttributeWriteMask,
										 void *pContext) const;

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
	bool m_bActiveRange;
};

DEFINE_PARTICLE_OPERATOR( C_INIT_DistanceToCPInit, "Remap Initial Distance to Control Point to Scalar", OPERATOR_GENERIC );

BEGIN_PARTICLE_INITIALIZER_OPERATOR_UNPACK( C_INIT_DistanceToCPInit )
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
	DMXELEMENT_UNPACK_FIELD( "only active within specified distance", "0", bool, m_bActiveRange )
END_PARTICLE_OPERATOR_UNPACK( C_INIT_DistanceToCPInit )

void C_INIT_DistanceToCPInit::InitNewParticlesScalar(
	CParticleCollection *pParticles, int start_p,
	int nParticleCount, int nAttributeWriteMask, void *pContext ) const
{
	float flMin = m_flOutputMin;
	float flMax = m_flOutputMax;
	if ( ATTRIBUTES_WHICH_ARE_0_TO_1 & ( 1 << m_nFieldOutput ) )
	{
		flMin = clamp( m_flOutputMin, 0.0f, 1.0f );
		flMax = clamp( m_flOutputMax, 0.0f, 1.0f );
	}

	Vector vecControlPoint1 = pParticles->GetControlPointAtCurrentTime( m_nStartCP );

	for( ; nParticleCount--; start_p++ )
	{
		const float *pXYZ = pParticles->GetFloatAttributePtr( PARTICLE_ATTRIBUTE_XYZ, start_p );
		Vector vecPosition2( pXYZ[0], pXYZ[4], pXYZ[8] );
		Vector vecDelta = vecControlPoint1 - vecPosition2;
		float flDistance = sqrt( ( vecDelta.z * vecDelta.z + vecDelta.y * vecDelta.y ) + vecDelta.x * vecDelta.x );

		if ( m_bActiveRange && ( flDistance < m_flInputMin || flDistance > m_flInputMax ) )
			continue;

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
				flDistance = flDistance * m_flLOSScale * tr.fraction;
			}
		}

		float flOutput = RemapValClamped( flDistance, m_flInputMin, m_flInputMax, flMin, flMax );
		if ( m_bScaleInitialRange )
		{
			const float *pInitialOutput = pParticles->GetFloatAttributePtr( m_nFieldOutput, start_p );
			flOutput = *pInitialOutput * flOutput;
		}
		float *pOutput = pParticles->GetFloatAttributePtrForWrite( m_nFieldOutput, start_p );
		*pOutput = flOutput;
	}
}


//-----------------------------------------------------------------------------
// Lifetime from time to impact
//-----------------------------------------------------------------------------
class C_INIT_LifespanFromVelocity : public CParticleInitializerOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_INIT_LifespanFromVelocity );

	uint32 GetWrittenAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_LIFE_DURATION_MASK;
	}

	uint32 GetReadAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_XYZ_MASK | PARTICLE_ATTRIBUTE_PREV_XYZ_MASK | PARTICLE_ATTRIBUTE_CREATION_TIME_MASK;
	}

	virtual void InitializeContextData( CParticleCollection *pParticles, void *pContext ) const
	{
	}

	size_t GetRequiredContextBytes( void ) const
	{
		return sizeof( CWorldCollideContextData );
	}

	bool InitMultipleOverride ( void ) { return true; }

	virtual void InitParams( CParticleSystemDefinition *pDef )
	{
		m_nCollisionGroupNumber = g_pParticleSystemMgr->Query()->GetCollisionGroupFromName( m_CollisionGroupName );
		m_nAllowedPlanes = ( MIN( MAX_WORLD_PLANAR_CONSTRAINTS, m_nMaxPlanes ) - 1 );
		m_CollisionMask = ( m_bIncludeWater ) ? ( MASK_SOLID | CONTENTS_DEBRIS | CONTENTS_WATER | CONTENTS_SLIME ) : ( MASK_SOLID | CONTENTS_DEBRIS );
	}

	virtual void InitNewParticlesScalar( CParticleCollection *pParticles, int start_p,
										 int nParticleCount, int nAttributeWriteMask,
										 void *pContext) const;

	virtual void InitNewParticlesBlock( CParticleCollection *pParticles,
										int start_block, int n_blocks, int nAttributeWriteMask,
										void *pContext ) const;

	Vector m_vecComponentScale;
	float m_flTraceOffset;
	float m_flMaxTraceLength;
	float m_flTraceTolerance;
	int m_nCollisionGroupNumber;
	int m_nMaxPlanes;
	int m_nAllowedPlanes;
	char m_CollisionGroupName[128];
	unsigned int m_CollisionMask;
	bool m_bIncludeWater;
};

DEFINE_PARTICLE_OPERATOR( C_INIT_LifespanFromVelocity, "Lifetime from Time to Impact", OPERATOR_GENERIC );

BEGIN_PARTICLE_INITIALIZER_OPERATOR_UNPACK( C_INIT_LifespanFromVelocity )
	DMXELEMENT_UNPACK_FIELD_STRING( "trace collision group", "NONE", m_CollisionGroupName )
	DMXELEMENT_UNPACK_FIELD( "maximum trace length", "1024", float, m_flMaxTraceLength )
	DMXELEMENT_UNPACK_FIELD( "trace offset", "0", float, m_flTraceOffset )
	DMXELEMENT_UNPACK_FIELD( "trace recycle tolerance", "64", float, m_flTraceTolerance )
	DMXELEMENT_UNPACK_FIELD( "maximum points to cache", "16", int, m_nMaxPlanes )
	DMXELEMENT_UNPACK_FIELD( "bias distance", "1 1 1", Vector, m_vecComponentScale )
	DMXELEMENT_UNPACK_FIELD( "collide with water", "1", bool, m_bIncludeWater )
END_PARTICLE_OPERATOR_UNPACK( C_INIT_LifespanFromVelocity )

// The shipped binary sums squared FourVectors components in these specific orders
static FORCEINLINE fltx4 LengthSqrZYX( const fltx4 &x, const fltx4 &y, const fltx4 &z )
{
	return AddSIMD( AddSIMD( MulSIMD( z, z ), MulSIMD( y, y ) ), MulSIMD( x, x ) );
}

static FORCEINLINE fltx4 LengthSqrXYZ( const fltx4 &x, const fltx4 &y, const fltx4 &z )
{
	return AddSIMD( AddSIMD( MulSIMD( x, x ), MulSIMD( y, y ) ), MulSIMD( z, z ) );
}

// collision cache slot used by the initializers (constraints use slots 0/1 through m_nCollisionMode)
#define COLLISION_MODE_INITIAL_TRACE_DOWN 2

static CWorldCollideContextData *GetLifespanCollisionCache( CParticleCollection *pParticles )
{
	CWorldCollideContextData **ppCtx;
	if ( pParticles->m_pParent )
		ppCtx = &( pParticles->m_pParent->m_pCollisionCacheData[COLLISION_MODE_INITIAL_TRACE_DOWN] );
	else
		ppCtx = &( pParticles->m_pCollisionCacheData[COLLISION_MODE_INITIAL_TRACE_DOWN] );

	if ( ! *ppCtx )
	{
		*ppCtx = new CWorldCollideContextData;
		(*ppCtx)->m_nActivePlanes = 0;
		(*ppCtx)->m_nActivePlanes = 0;
		(*ppCtx)->m_nNumFixedPlanes = 0;
	}
	return *ppCtx;
}

void C_INIT_LifespanFromVelocity::InitNewParticlesScalar(
	CParticleCollection *pParticles, int start_p,
	int nParticleCount, int nAttributeWriteMask, void *pContext ) const
{
	CWorldCollideContextData *pCtx = GetLifespanCollisionCache( pParticles );

	float flTol = m_flTraceTolerance * m_flTraceTolerance;

	// Trace length takes the max trace and subtracts the offset to get the actual total.
	float flTotalTraceDist = m_flMaxTraceLength - m_flTraceOffset;
	// Offset percentage to account for if we've hit something within the offset (but not spawn) area
	float flOffsetPct = m_flMaxTraceLength / ( flTotalTraceDist + FLT_EPSILON );

	FourVectors v4ComponentScale;
	v4ComponentScale.DuplicateVector( m_vecComponentScale );
	fltx4 fl4TraceOffset = ReplicateX4( m_flTraceOffset );

	for( ; nParticleCount--; start_p++ )
	{
		const float *pxyz = pParticles->GetFloatAttributePtr( PARTICLE_ATTRIBUTE_XYZ, start_p );
		const float *pPrevXYZ = pParticles->GetFloatAttributePtr( PARTICLE_ATTRIBUTE_PREV_XYZ, start_p );
		float *dtime = pParticles->GetFloatAttributePtrForWrite( PARTICLE_ATTRIBUTE_LIFE_DURATION, start_p );

		// Skip particles which have already been killed
		if ( *dtime < 0.0f )
			continue;

		Vector vecXYZ( pxyz[0], pxyz[4], pxyz[8] );
		Vector vecPrevXYZ( pPrevXYZ[0], pPrevXYZ[4], pPrevXYZ[8] );

		float flInvDt = 1.0f / pParticles->m_flPreviousDt;
		Vector vDelta = ( vecXYZ - vecPrevXYZ ) * flInvDt;
		float flVelocity = sqrt( ( vDelta.x * vDelta.x + vDelta.z * vDelta.z ) + vDelta.y * vDelta.y );

		VectorNormalize( vDelta );
		Vector vecStartPnt = vecXYZ + ( vDelta * m_flTraceOffset );
		Vector vecEndPnt = ( vDelta * flTotalTraceDist ) + vecStartPnt;

		FourVectors fvStartPnt;
		fvStartPnt.DuplicateVector( vecStartPnt );
		FourVectors fvEndPnt;
		fvEndPnt.DuplicateVector( vecEndPnt );

		FourVectors v4PointOnPlane;
		fltx4 fl4ClosestDist = Four_FLT_MAX;
		for( int i = 0; i < pCtx->m_nActivePlanes; i++ )
		{
			if ( pCtx->m_bPlaneActive[i] )
			{
				FourVectors v4StartDelta = pCtx->m_TraceStartPnt[i];
				v4StartDelta -= fvStartPnt;
				FourVectors v4EndDelta = pCtx->m_TraceEndPnt[i];
				v4EndDelta -= fvEndPnt;
				fltx4 fl4StartDist = LengthSqrZYX( v4StartDelta.x, v4StartDelta.y, v4StartDelta.z );
				fltx4 fl4EndDist = LengthSqrZYX( v4EndDelta.x, v4EndDelta.y, v4EndDelta.z );
				fltx4 fl4MaxDist = MaxSIMD( fl4StartDist, fl4EndDist );
				if ( IsAnyNegative( CmpLeSIMD( fl4MaxDist, fl4ClosestDist ) ) )
				{
					v4PointOnPlane = pCtx->m_PointOnPlane[i];
					fl4ClosestDist = fl4MaxDist;
				}
			}
		}

		// If we didn't find a close enough cached plane, trace
		if ( ! IsAnyNegative( CmpLeSIMD( fl4ClosestDist, ReplicateX4( flTol ) ) ) )
		{
			CBaseTrace tr;
			g_pParticleSystemMgr->Query()->TraceLine( vecStartPnt, vecEndPnt, m_CollisionMask, NULL, m_nCollisionGroupNumber, &tr );

			Vector vNormal = tr.plane.normal;
			Vector vPointOnPlane = tr.endpos;
			if ( tr.fraction == 1.0f )
			{
				vNormal = vec3_invalid;
			}
			// Hit within the offset area, inside something, or no trace distance - nothing to collide with
			if ( ( tr.fraction <= ( 1.0f - flOffsetPct ) ) || tr.startsolid || ( flTotalTraceDist == 0.0f ) || tr.allsolid )
			{
				vPointOnPlane = vec3_invalid;
				vNormal = vec3_invalid;
			}

			int nIndex = pCtx->m_nNumFixedPlanes;
			pCtx->m_bPlaneActive[nIndex] = true;
			pCtx->m_PointOnPlane[nIndex].DuplicateVector( vPointOnPlane );
			pCtx->m_PlaneNormal[nIndex].DuplicateVector( vNormal );
			pCtx->m_TraceStartPnt[nIndex].DuplicateVector( vecStartPnt );
			pCtx->m_TraceEndPnt[nIndex].DuplicateVector( vecEndPnt );

			v4PointOnPlane.DuplicateVector( vPointOnPlane );

			pCtx->m_nNumFixedPlanes++;
			if ( pCtx->m_nNumFixedPlanes > m_nAllowedPlanes )
				pCtx->m_nNumFixedPlanes = 0;
			pCtx->m_nActivePlanes = MIN( m_nAllowedPlanes, pCtx->m_nActivePlanes + 1 );
		}

		if ( v4PointOnPlane.Vec( 0 ) == vec3_invalid )
		{
			*dtime = -1.0f;
		}
		else
		{
			fvStartPnt -= v4PointOnPlane;
			fvStartPnt *= v4ComponentScale;
			fltx4 fl4Dist = AddSIMD( SqrtSIMD( LengthSqrXYZ( fvStartPnt.x, fvStartPnt.y, fvStartPnt.z ) ), fl4TraceOffset );
			*dtime = SubFloat( fl4Dist, 0 ) / ( flVelocity + FLT_EPSILON );
		}
	}
}

void C_INIT_LifespanFromVelocity::InitNewParticlesBlock( CParticleCollection *pParticles,
														 int start_block, int n_blocks, int nAttributeWriteMask,
														 void *pContext ) const
{
	CWorldCollideContextData *pCtx = GetLifespanCollisionCache( pParticles );

	size_t attr_stride;
	const FourVectors *pXYZ = pParticles->Get4VAttributePtr( PARTICLE_ATTRIBUTE_XYZ, &attr_stride );
	pXYZ += attr_stride * start_block;
	const FourVectors *pPrev_XYZ = pParticles->Get4VAttributePtr( PARTICLE_ATTRIBUTE_PREV_XYZ, &attr_stride );
	pPrev_XYZ += attr_stride * start_block;

	float flTol = m_flTraceTolerance * m_flTraceTolerance;

	// Trace length takes the max trace and subtracts the offset to get the actual total.
	float flTotalTraceDist = m_flMaxTraceLength - m_flTraceOffset;
	fltx4 fl4TotalTraceDist = ReplicateX4( flTotalTraceDist );
	// Offset percentage to account for if we've hit something within the offset (but not spawn) area
	float flOffsetPct = m_flMaxTraceLength / ( flTotalTraceDist + FLT_EPSILON );

	FourVectors v4ComponentScale;
	v4ComponentScale.DuplicateVector( m_vecComponentScale );

	fltx4 *pLifespan = pParticles->GetM128AttributePtrForWrite( PARTICLE_ATTRIBUTE_LIFE_DURATION, &attr_stride );
	pLifespan += attr_stride * start_block;

	fltx4 fl4TraceOffset = ReplicateX4( m_flTraceOffset );
	fltx4 fl4PrevDT = ReplicateX4( pParticles->m_flPreviousDt );

	while( n_blocks-- )
	{
		// Only process particles which haven't already been killed
		fltx4 fl4PassMask = CmpGeSIMD( *pLifespan, Four_Zeros );
		fltx4 fl4SkipMask = CmpLtSIMD( *pLifespan, Four_Zeros );
		if ( IsAnyNegative( fl4PassMask ) )
		{
			FourVectors fvDelta = *pXYZ;
			fvDelta -= *pPrev_XYZ;
			fvDelta *= ReciprocalSIMD( fl4PrevDT );

			fltx4 fl4Velocity = SqrtSIMD( AddSIMD( AddSIMD( MulSIMD( fvDelta.y, fvDelta.y ), MulSIMD( fvDelta.x, fvDelta.x ) ), MulSIMD( fvDelta.z, fvDelta.z ) ) );

			// Normalize with the estimate, as the shipped code does
			fltx4 fl4InvLen = ReciprocalSqrtEstSIMD( AddSIMD( AddSIMD( MulSIMD( fvDelta.y, fvDelta.y ), MulSIMD( fvDelta.x, fvDelta.x ) ), MulSIMD( fvDelta.z, fvDelta.z ) ) );
			fvDelta *= fl4InvLen;

			FourVectors fvOffset = fvDelta;
			fvOffset *= fl4TraceOffset;

			FourVectors fvStartPnt = *pXYZ;
			fvStartPnt += fvOffset;
			FourVectors fvEndPnt = fvDelta;
			fvEndPnt *= fl4TotalTraceDist;
			fvEndPnt += fvStartPnt;

			FourVectors v4PointOnPlane;
			fltx4 fl4ClosestDist = MaskedAssign( fl4SkipMask, Four_Zeros, Four_FLT_MAX );
			for( int i = 0; i < pCtx->m_nActivePlanes; i++ )
			{
				if ( pCtx->m_bPlaneActive[i] )
				{
					FourVectors v4StartDelta = pCtx->m_TraceStartPnt[i];
					v4StartDelta -= fvStartPnt;
					FourVectors v4EndDelta = pCtx->m_TraceEndPnt[i];
					v4EndDelta -= fvEndPnt;
					fltx4 fl4StartDist = LengthSqrZYX( v4StartDelta.x, v4StartDelta.y, v4StartDelta.z );
					fltx4 fl4EndDist = LengthSqrZYX( v4EndDelta.x, v4EndDelta.y, v4EndDelta.z );
					fltx4 fl4MaxDist = MaxSIMD( fl4StartDist, fl4EndDist );
					fltx4 fl4CloserMask = CmpLeSIMD( fl4MaxDist, fl4ClosestDist );
					fl4ClosestDist = MaskedAssign( fl4CloserMask, fl4MaxDist, fl4ClosestDist );
					v4PointOnPlane.x = MaskedAssign( fl4CloserMask, pCtx->m_PointOnPlane[i].x, v4PointOnPlane.x );
					v4PointOnPlane.y = MaskedAssign( fl4CloserMask, pCtx->m_PointOnPlane[i].y, v4PointOnPlane.y );
					v4PointOnPlane.z = MaskedAssign( fl4CloserMask, pCtx->m_PointOnPlane[i].z, v4PointOnPlane.z );
				}
			}

			fltx4 fl4LinearMask = CmpGtSIMD( fl4ClosestDist, ReplicateX4( flTol ) );
			int nMask = TestSignSIMD( fl4LinearMask );
			if ( nMask )
			{
				for( int i = 0; i < 4; i++ )
				{
					if ( nMask & ( 1 << i ) )
					{
						Vector start, end;
						start.x = SubFloat( fvStartPnt.x, i );
						start.y = SubFloat( fvStartPnt.y, i );
						start.z = SubFloat( fvStartPnt.z, i );
						end.x = SubFloat( fvEndPnt.x, i );
						end.y = SubFloat( fvEndPnt.y, i );
						end.z = SubFloat( fvEndPnt.z, i );

						CBaseTrace tr;
						g_pParticleSystemMgr->Query()->TraceLine( start, end, m_CollisionMask, NULL, m_nCollisionGroupNumber, &tr );

						Vector vNormal = tr.plane.normal;
						Vector vPointOnPlane = tr.endpos;
						if ( tr.fraction == 1.0f )
						{
							vNormal = vec3_invalid;
						}
						if ( ( tr.fraction <= ( 1.0f - flOffsetPct ) ) || tr.startsolid || ( flTotalTraceDist == 0.0f ) || tr.allsolid )
						{
							vPointOnPlane = vec3_invalid;
							vNormal = vec3_invalid;
						}

						SubFloat( v4PointOnPlane.x, i ) = vPointOnPlane.x;
						SubFloat( v4PointOnPlane.y, i ) = vPointOnPlane.y;
						SubFloat( v4PointOnPlane.z, i ) = vPointOnPlane.z;

						int nIndex = pCtx->m_nNumFixedPlanes;
						pCtx->m_bPlaneActive[nIndex] = true;
						pCtx->m_PointOnPlane[nIndex].DuplicateVector( vPointOnPlane );
						pCtx->m_PlaneNormal[nIndex].DuplicateVector( vNormal );
						pCtx->m_TraceStartPnt[nIndex].DuplicateVector( start );
						pCtx->m_TraceEndPnt[nIndex].DuplicateVector( end );

						pCtx->m_nNumFixedPlanes++;
						if ( pCtx->m_nNumFixedPlanes > m_nAllowedPlanes )
							pCtx->m_nNumFixedPlanes = 0;
						pCtx->m_nActivePlanes = MIN( m_nAllowedPlanes, pCtx->m_nActivePlanes + 1 );
					}
				}
			}

			fltx4 fl4InvalidMask = OrSIMD( OrSIMD( CmpEqSIMD( ReplicateX4( vec3_invalid.y ), v4PointOnPlane.y ),
												   CmpEqSIMD( ReplicateX4( vec3_invalid.x ), v4PointOnPlane.x ) ),
										   CmpEqSIMD( ReplicateX4( vec3_invalid.z ), v4PointOnPlane.z ) );

			fvStartPnt -= v4PointOnPlane;
			fvStartPnt *= v4ComponentScale;

			fltx4 fl4Dist = AddSIMD( SqrtSIMD( AddSIMD( AddSIMD( MulSIMD( fvStartPnt.y, fvStartPnt.y ), MulSIMD( fvStartPnt.x, fvStartPnt.x ) ), MulSIMD( fvStartPnt.z, fvStartPnt.z ) ) ), fl4TraceOffset );
			fltx4 fl4NewLifespan = DivSIMD( fl4Dist, AddSIMD( Four_Epsilons, fl4Velocity ) );
			fl4NewLifespan = MaskedAssign( fl4InvalidMask, Four_Zeros, fl4NewLifespan );
			*pLifespan = MaskedAssign( fl4PassMask, fl4NewLifespan, *pLifespan );
		}

		// NOTE: shipped code advances every pointer by the lifespan stride
		pXYZ += attr_stride;
		pPrev_XYZ += attr_stride;
		pLifespan += attr_stride;
	}
}


//-----------------------------------------------------------------------------
// Position from the parent's collision plane cache
//-----------------------------------------------------------------------------
class C_INIT_CreateFromPlaneCache : public CParticleInitializerOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_INIT_CreateFromPlaneCache );

	uint32 GetWrittenAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_XYZ_MASK | PARTICLE_ATTRIBUTE_PREV_XYZ_MASK | PARTICLE_ATTRIBUTE_LIFE_DURATION_MASK | PARTICLE_ATTRIBUTE_NORMAL_MASK;
	}

	uint32 GetReadAttributes( void ) const
	{
		return 0;
	}

	size_t GetRequiredContextBytes( void ) const
	{
		return sizeof( CWorldCollideContextData );
	}

	virtual void InitParams( CParticleSystemDefinition *pDef )
	{
		// NOTE: shipped code requires both ends of the range to be non-zero
		bLocalOffset = ( m_vecOffsetMin != vec3_origin && m_vecOffsetMax != vec3_origin );
	}

	virtual void InitNewParticlesScalar( CParticleCollection *pParticles, int start_p,
										 int nParticleCount, int nAttributeWriteMask,
										 void *pContext) const;

	Vector m_vecOffsetMin;
	Vector m_vecOffsetMax;
	bool bLocalOffset;
	bool m_bUseNormal;
};

DEFINE_PARTICLE_OPERATOR( C_INIT_CreateFromPlaneCache, "Position from Parent Cache", OPERATOR_PI_POSITION );

BEGIN_PARTICLE_INITIALIZER_OPERATOR_UNPACK( C_INIT_CreateFromPlaneCache )
	DMXELEMENT_UNPACK_FIELD( "Local Offset Min", "0 0 0", Vector, m_vecOffsetMin )
	DMXELEMENT_UNPACK_FIELD( "Local Offset Max", "0 0 0", Vector, m_vecOffsetMax )
	DMXELEMENT_UNPACK_FIELD( "Set Normal", "0", bool, m_bUseNormal )
END_PARTICLE_OPERATOR_UNPACK( C_INIT_CreateFromPlaneCache )

void C_INIT_CreateFromPlaneCache::InitNewParticlesScalar(
	CParticleCollection *pParticles, int start_p,
	int nParticleCount, int nAttributeWriteMask, void *pContext ) const
{
	if ( !pParticles->m_pParent )
	{
		for( ; nParticleCount--; start_p++ )
		{
			float *xyz = pParticles->GetFloatAttributePtrForWrite( PARTICLE_ATTRIBUTE_XYZ, start_p );
			float *pxyz = pParticles->GetFloatAttributePtrForWrite( PARTICLE_ATTRIBUTE_PREV_XYZ, start_p );
			SetVectorAttribute( xyz, vec3_origin );
			SetVectorAttribute( pxyz, vec3_origin );
		}
		return;
	}

	if ( !pParticles->m_pParent->m_pCollisionCacheData[COLLISION_MODE_INITIAL_TRACE_DOWN] )
	{
		pParticles->m_pParent->m_pCollisionCacheData[COLLISION_MODE_INITIAL_TRACE_DOWN] = new CWorldCollideContextData;
		pParticles->m_pParent->m_pCollisionCacheData[COLLISION_MODE_INITIAL_TRACE_DOWN]->m_nActivePlanes = 0;
		pParticles->m_pParent->m_pCollisionCacheData[COLLISION_MODE_INITIAL_TRACE_DOWN]->m_nNumFixedPlanes = 0;
		pParticles->m_pParent->m_pCollisionCacheData[COLLISION_MODE_INITIAL_TRACE_DOWN]->m_PointOnPlane[0].DuplicateVector( vec3_origin );
	}

	CWorldCollideContextData *pCtx = pParticles->m_pParent->m_pCollisionCacheData[COLLISION_MODE_INITIAL_TRACE_DOWN];
	if ( pCtx->m_nActivePlanes > 0 )
	{
		for( ; nParticleCount--; start_p++ )
		{
			int nIndex = pParticles->RandomInt( 0, pCtx->m_nActivePlanes - 1 );

			Vector vecNormal = pCtx->m_PlaneNormal[nIndex].Vec( 0 );
			if ( vecNormal == vec3_invalid || pCtx->m_PointOnPlane[nIndex].Vec( 0 ) == vec3_invalid )
			{
				float *plifespan = pParticles->GetFloatAttributePtrForWrite( PARTICLE_ATTRIBUTE_LIFE_DURATION, start_p );
				*plifespan = 0.0f;
			}
			else
			{
				float *xyz = pParticles->GetFloatAttributePtrForWrite( PARTICLE_ATTRIBUTE_XYZ, start_p );
				float *pxyz = pParticles->GetFloatAttributePtrForWrite( PARTICLE_ATTRIBUTE_PREV_XYZ, start_p );

				FourVectors fvPoint = pCtx->m_PointOnPlane[nIndex];
				Vector vecPoint = fvPoint.Vec( 0 );
				if ( bLocalOffset )
				{
					Vector randpos;
					pParticles->RandomVector( m_vecOffsetMin, m_vecOffsetMax, &randpos );
					V_swap( randpos.x, randpos.z );
					QAngle LocalCoords;
					VectorAngles( vecNormal, LocalCoords );
					Vector OutputPos;
					VectorRotate( randpos, LocalCoords, OutputPos );
					vecPoint += OutputPos;
				}

				SetVectorAttribute( xyz, vecPoint );
				SetVectorAttribute( pxyz, vecPoint );
				if ( m_bUseNormal )
				{
					float *pNormal = pParticles->GetFloatAttributePtrForWrite( PARTICLE_ATTRIBUTE_NORMAL, start_p );
					SetVectorAttribute( pNormal, vecNormal );
				}
			}
		}
	}
	else
	{
		for( ; nParticleCount--; start_p++ )
		{
			float *xyz = pParticles->GetFloatAttributePtrForWrite( PARTICLE_ATTRIBUTE_XYZ, start_p );
			float *pxyz = pParticles->GetFloatAttributePtrForWrite( PARTICLE_ATTRIBUTE_PREV_XYZ, start_p );
			SetVectorAttribute( xyz, vec3_origin );
			SetVectorAttribute( pxyz, vec3_origin );
		}
	}
}


//-----------------------------------------------------------------------------
// Cull relative to model
//-----------------------------------------------------------------------------
class C_INIT_ModelCull : public CParticleInitializerOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_INIT_ModelCull );

	uint32 GetWrittenAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_XYZ_MASK;
	}

	uint32 GetReadAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_XYZ_MASK;
	}

	virtual uint64 GetReadControlPointMask() const
	{
		return 1ULL << m_nControlPointNumber;
	}

	virtual void InitParams( CParticleSystemDefinition *pDef )
	{
		m_nControlPointNumber = clamp( m_nControlPointNumber, 0, MAX_PARTICLE_CONTROL_POINTS - 1 );
	}

	bool InitMultipleOverride ( void ) { return true; }

	virtual void InitNewParticlesScalar( CParticleCollection *pParticles, int start_p,
										 int nParticleCount, int nAttributeWriteMask,
										 void *pContext) const;

	int m_nControlPointNumber;
	bool m_bBoundBox;
	bool m_bCullOutside;
};

DEFINE_PARTICLE_OPERATOR( C_INIT_ModelCull, "Cull relative to model", OPERATOR_GENERIC );

BEGIN_PARTICLE_INITIALIZER_OPERATOR_UNPACK( C_INIT_ModelCull )
	DMXELEMENT_UNPACK_FIELD( "control_point_number", "0", int, m_nControlPointNumber )
	DMXELEMENT_UNPACK_FIELD( "use only bounding box", "0", bool, m_bBoundBox )
	DMXELEMENT_UNPACK_FIELD( "cull outside instead of inside", "0", bool, m_bCullOutside )
END_PARTICLE_OPERATOR_UNPACK( C_INIT_ModelCull )

void C_INIT_ModelCull::InitNewParticlesScalar(
	CParticleCollection *pParticles, int start_p,
	int nParticleCount, int nAttributeWriteMask, void *pContext ) const
{
	pParticles->UpdateHitBoxInfo( m_nControlPointNumber );
	if ( pParticles->ControlPointHitBox( m_nControlPointNumber ).CurAndPrevValid() )
	{
		for( ; nParticleCount--; start_p++ )
		{
			const float *pXYZ = pParticles->GetFloatAttributePtr( PARTICLE_ATTRIBUTE_XYZ, start_p );
			float *pLifespan = pParticles->GetFloatAttributePtrForWrite( PARTICLE_ATTRIBUTE_LIFE_DURATION, start_p );

			Vector vecParticlePosition;
			SetVectorFromAttribute( vecParticlePosition, pXYZ );

			bool bInside = g_pParticleSystemMgr->Query()->IsPointInControllingObjectHitBox( pParticles, m_nControlPointNumber, vecParticlePosition, m_bBoundBox );
			if ( ( bInside && !m_bCullOutside ) || ( !bInside && m_bCullOutside ) )
			{
				*pLifespan = -1.0f;
			}
		}
	}
}


//-----------------------------------------------------------------------------
// Cull relative to a ray trace environment
//-----------------------------------------------------------------------------
class C_INIT_RtEnvCull : public CParticleInitializerOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_INIT_RtEnvCull );

	uint32 GetWrittenAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_XYZ_MASK;
	}

	uint32 GetReadAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_XYZ_MASK | PARTICLE_ATTRIBUTE_PREV_XYZ_MASK;
	}

	virtual void InitParams( CParticleSystemDefinition *pDef )
	{
		m_nRtEnvNumber = g_pParticleSystemMgr->Query()->GetRayTraceEnvironmentFromName( m_RtEnvName );
	}

	bool InitMultipleOverride ( void ) { return true; }

	virtual void InitNewParticlesScalar( CParticleCollection *pParticles, int start_p,
										 int nParticleCount, int nAttributeWriteMask,
										 void *pContext) const;

	virtual void InitNewParticlesBlock( CParticleCollection *pParticles,
										int start_block, int n_blocks, int nAttributeWriteMask,
										void *pContext ) const;

	Vector m_vecTestDir;
	Vector m_vecTestNormal;
	int m_nRtEnvNumber;
	bool m_bUseVelocity;
	bool m_bCullOnMiss;
	bool m_bLifeAdjust;
	char m_RtEnvName[128];
};

DEFINE_PARTICLE_OPERATOR(C_INIT_RtEnvCull, "Cull relative to Ray Trace Environment", OPERATOR_GENERIC);

BEGIN_PARTICLE_INITIALIZER_OPERATOR_UNPACK( C_INIT_RtEnvCull )
	DMXELEMENT_UNPACK_FIELD( "cull on miss", "0", bool, m_bCullOnMiss )
	DMXELEMENT_UNPACK_FIELD( "velocity test adjust lifespan", "0", bool, m_bLifeAdjust )
	DMXELEMENT_UNPACK_FIELD( "use velocity for test direction", "0", bool, m_bUseVelocity )
	DMXELEMENT_UNPACK_FIELD( "test direction", "0 0 1", Vector, m_vecTestDir )
	DMXELEMENT_UNPACK_FIELD( "cull normal", "0 0 0", Vector, m_vecTestNormal )
	DMXELEMENT_UNPACK_FIELD_STRING( "ray trace environment name", "PRECIPITATION", m_RtEnvName )
END_PARTICLE_OPERATOR_UNPACK( C_INIT_RtEnvCull )

void C_INIT_RtEnvCull::InitNewParticlesScalar(
	CParticleCollection *pParticles, int start_p,
	int nParticleCount, int nAttributeWriteMask, void *pContext ) const
{
	bool bUseNormal = ( m_vecTestNormal != vec3_origin );
	if ( m_bUseVelocity )
	{
		fltx4 fl4PrevDt = ReplicateX4( pParticles->m_flPreviousDt );
		for( ; nParticleCount--; start_p++ )
		{
			const float *pXYZ = pParticles->GetFloatAttributePtr( PARTICLE_ATTRIBUTE_XYZ, start_p );
			const float *pPrevXYZ = pParticles->GetFloatAttributePtr( PARTICLE_ATTRIBUTE_PREV_XYZ, start_p );
			float *pLifespan = pParticles->GetFloatAttributePtrForWrite( PARTICLE_ATTRIBUTE_LIFE_DURATION, start_p );
			if ( *pLifespan != -1.0f )
			{
				Vector vecParticlePosition, vecParticlePosition_prev;
				SetVectorFromAttribute( vecParticlePosition, pXYZ );
				SetVectorFromAttribute( vecParticlePosition_prev, pPrevXYZ );
				Vector vecVelocity = vecParticlePosition - vecParticlePosition_prev;

				FourRays frTr;
				frTr.origin.DuplicateVector( vecParticlePosition );
				frTr.direction.DuplicateVector( vecVelocity );
				fltx4 fl4Len2 = AddSIMD( AddSIMD( MulSIMD( frTr.direction.x, frTr.direction.x ), MulSIMD( frTr.direction.y, frTr.direction.y ) ), MulSIMD( frTr.direction.z, frTr.direction.z ) );
				fltx4 fl4Velocity = DivSIMD( SqrtSIMD( fl4Len2 ), fl4PrevDt );
				frTr.direction *= ReciprocalSqrtEstSIMD( fl4Len2 );
				fltx4 fl4Lifespan = ReplicateX4( *pLifespan );

				RayTracingResult Result;
				g_pParticleSystemMgr->Query()->TraceAgainstRayTraceEnv( m_nRtEnvNumber, frTr, Four_Zeros, MulSIMD( fl4Lifespan, fl4Velocity ), &Result, 0 );

				bool bHit = ( Result.HitIds[0] != -1 );
				if ( bUseNormal )
				{
					bHit = bHit && ( m_vecTestNormal == Result.surface_normal.Vec( 0 ) );
				}

				if ( m_bLifeAdjust && bHit )
				{
					fl4Velocity = AddSIMD( Four_Epsilons, fl4Velocity );
					fl4Lifespan = DivSIMD( Result.HitDistance, fl4Velocity );
					*pLifespan = MIN( *pLifespan, SubFloat( fl4Lifespan, 0 ) );
				}
				else if ( ( bHit && !m_bCullOnMiss ) || ( !bHit && m_bCullOnMiss ) )
				{
					*pLifespan = -1.0f;
				}
			}
		}
	}
	else
	{
		for( ; nParticleCount--; start_p++ )
		{
			const float *pXYZ = pParticles->GetFloatAttributePtr( PARTICLE_ATTRIBUTE_XYZ, start_p );
			float *pLifespan = pParticles->GetFloatAttributePtrForWrite( PARTICLE_ATTRIBUTE_LIFE_DURATION, start_p );
			if ( *pLifespan != -1.0f )
			{
				Vector vecParticlePosition;
				SetVectorFromAttribute( vecParticlePosition, pXYZ );

				FourRays frTr;
				frTr.origin.DuplicateVector( vecParticlePosition );
				frTr.direction.DuplicateVector( m_vecTestDir );

				RayTracingResult Result;
				g_pParticleSystemMgr->Query()->TraceAgainstRayTraceEnv( m_nRtEnvNumber, frTr, Four_Zeros, Four_FLT_MAX, &Result, 0 );

				bool bHit = ( Result.HitIds[0] != -1 );
				if ( bUseNormal )
				{
					bHit = bHit && ( m_vecTestNormal == Result.surface_normal.Vec( 0 ) );
				}

				// NOTE: shipped code also culls normal-matching hits when culling on miss
				if ( ( bHit && ( !m_bCullOnMiss || bUseNormal ) ) || ( !bHit && m_bCullOnMiss ) )
				{
					*pLifespan = -1.0f;
				}
			}
		}
	}
}

static FORCEINLINE fltx4 RtEnvHitIdsToFloat( const RayTracingResult &Result )
{
	fltx4 fl4HitIds;
	SubFloat( fl4HitIds, 0 ) = (float)Result.HitIds[0];
	SubFloat( fl4HitIds, 1 ) = (float)Result.HitIds[1];
	SubFloat( fl4HitIds, 2 ) = (float)Result.HitIds[2];
	SubFloat( fl4HitIds, 3 ) = (float)Result.HitIds[3];
	return fl4HitIds;
}

void C_INIT_RtEnvCull::InitNewParticlesBlock( CParticleCollection *pParticles,
											  int start_block, int n_blocks, int nAttributeWriteMask,
											  void *pContext ) const
{
	bool bUseNormal = ( m_vecTestNormal != vec3_origin );

	size_t attr_stride;
	const FourVectors *pXYZ = pParticles->Get4VAttributePtr( PARTICLE_ATTRIBUTE_XYZ, &attr_stride );
	pXYZ += attr_stride * start_block;
	const FourVectors *pPrev_XYZ = pParticles->Get4VAttributePtr( PARTICLE_ATTRIBUTE_PREV_XYZ, &attr_stride );
	pPrev_XYZ += attr_stride * start_block;
	fltx4 *pLifespan = pParticles->GetM128AttributePtrForWrite( PARTICLE_ATTRIBUTE_LIFE_DURATION, &attr_stride );
	pLifespan += attr_stride * start_block;

	if ( m_bUseVelocity )
	{
		fltx4 fl4PrevDt = ReplicateX4( pParticles->m_flPreviousDt );
		while( n_blocks-- )
		{
			FourRays frTr;
			frTr.origin = *pXYZ;
			frTr.direction = *pXYZ;
			frTr.direction -= *pPrev_XYZ;
			fltx4 fl4Len2 = AddSIMD( AddSIMD( MulSIMD( frTr.direction.y, frTr.direction.y ), MulSIMD( frTr.direction.x, frTr.direction.x ) ), MulSIMD( frTr.direction.z, frTr.direction.z ) );
			fltx4 fl4Velocity = DivSIMD( SqrtSIMD( fl4Len2 ), fl4PrevDt );
			fltx4 fl4Lifespan = *pLifespan;
			fltx4 fl4DeadMask = CmpEqSIMD( Four_NegativeOnes, fl4Lifespan );

			// NOTE: shipped code advances every pointer by the lifespan stride
			if ( TestSignSIMD( fl4DeadMask ) == 0xf )
			{
				pXYZ += attr_stride;
				pPrev_XYZ += attr_stride;
				pLifespan += attr_stride;
				continue;
			}

			frTr.direction *= ReciprocalSqrtSIMD( fl4Len2 );

			RayTracingResult Result;
			g_pParticleSystemMgr->Query()->TraceAgainstRayTraceEnv( m_nRtEnvNumber, frTr, Four_Zeros, MulSIMD( fl4Lifespan, fl4Velocity ), &Result, 0 );

			fltx4 fl4HitIds = RtEnvHitIdsToFloat( Result );
			fltx4 fl4CullMask;
			if ( m_bCullOnMiss )
			{
				fl4CullMask = CmpLtSIMD( fl4HitIds, Four_Zeros );
				if ( bUseNormal )
				{
					FourVectors v4TestNormal;
					v4TestNormal.DuplicateVector( m_vecTestNormal );
					fltx4 fl4NormalMask = AndSIMD( CmpEqSIMD( v4TestNormal.y, Result.surface_normal.y ), CmpEqSIMD( Result.surface_normal.x, v4TestNormal.x ) );
					fl4NormalMask = AndSIMD( fl4NormalMask, CmpEqSIMD( v4TestNormal.z, Result.surface_normal.z ) );
					fl4CullMask = OrSIMD( fl4NormalMask, fl4CullMask );
				}
			}
			else
			{
				fl4CullMask = CmpGeSIMD( fl4HitIds, Four_Zeros );
				if ( bUseNormal )
				{
					FourVectors v4TestNormal;
					v4TestNormal.DuplicateVector( m_vecTestNormal );
					fltx4 fl4NormalMask = AndSIMD( CmpEqSIMD( v4TestNormal.y, Result.surface_normal.y ), CmpEqSIMD( v4TestNormal.x, Result.surface_normal.x ) );
					fl4NormalMask = AndSIMD( fl4NormalMask, CmpEqSIMD( v4TestNormal.z, Result.surface_normal.z ) );
					fl4CullMask = AndSIMD( fl4NormalMask, fl4CullMask );
				}
			}

			if ( m_bLifeAdjust )
			{
				if ( IsAnyNegative( fl4CullMask ) )
				{
					fl4Velocity = AddSIMD( Four_Epsilons, fl4Velocity );
					fl4Lifespan = MaskedAssign( fl4DeadMask, Four_NegativeOnes, DivSIMD( Result.HitDistance, fl4Velocity ) );
				}
			}
			else
			{
				fl4Lifespan = Four_NegativeOnes;
			}

			*pLifespan = MinSIMD( *pLifespan, MaskedAssign( fl4CullMask, fl4Lifespan, *pLifespan ) );

			pXYZ += attr_stride;
			pPrev_XYZ += attr_stride;
			pLifespan += attr_stride;
		}
	}
	else
	{
		while( n_blocks-- )
		{
			FourRays frTr;
			frTr.direction.DuplicateVector( m_vecTestDir );
			frTr.origin = *pXYZ;

			fltx4 fl4DeadMask = CmpEqSIMD( *pLifespan, Four_NegativeOnes );
			if ( TestSignSIMD( fl4DeadMask ) != 0xf )
			{
				RayTracingResult Result;
				g_pParticleSystemMgr->Query()->TraceAgainstRayTraceEnv( m_nRtEnvNumber, frTr, Four_Zeros, Four_FLT_MAX, &Result, 0 );

				fltx4 fl4HitIds = RtEnvHitIdsToFloat( Result );
				fltx4 fl4CullMask;
				if ( m_bCullOnMiss )
				{
					fl4CullMask = CmpLtSIMD( fl4HitIds, Four_Zeros );
					if ( bUseNormal )
					{
						FourVectors v4TestNormal;
						v4TestNormal.DuplicateVector( m_vecTestNormal );
						fltx4 fl4NormalMask = AndSIMD( CmpEqSIMD( v4TestNormal.y, Result.surface_normal.y ), CmpEqSIMD( Result.surface_normal.x, v4TestNormal.x ) );
						fl4NormalMask = AndSIMD( fl4NormalMask, CmpEqSIMD( v4TestNormal.z, Result.surface_normal.z ) );
						fl4CullMask = OrSIMD( fl4NormalMask, fl4CullMask );
					}
				}
				else
				{
					fl4CullMask = CmpGeSIMD( fl4HitIds, Four_Zeros );
					if ( bUseNormal )
					{
						FourVectors v4TestNormal;
						v4TestNormal.DuplicateVector( m_vecTestNormal );
						fltx4 fl4NormalMask = AndSIMD( CmpEqSIMD( v4TestNormal.y, Result.surface_normal.y ), CmpEqSIMD( v4TestNormal.x, Result.surface_normal.x ) );
						fl4NormalMask = AndSIMD( fl4NormalMask, CmpEqSIMD( v4TestNormal.z, Result.surface_normal.z ) );
						fl4CullMask = AndSIMD( fl4NormalMask, fl4CullMask );
					}
				}

				fltx4 fl4Lifespan = Four_NegativeOnes;
				*pLifespan = MaskedAssign( fl4CullMask, fl4Lifespan, *pLifespan );
			}

			pXYZ += attr_stride;
			pLifespan += attr_stride;
		}
	}
}


//-----------------------------------------------------------------------------
// Normal align to control point
//-----------------------------------------------------------------------------
class C_INIT_NormalAlignToCP : public CParticleInitializerOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_INIT_NormalAlignToCP );

	uint32 GetWrittenAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_NORMAL_MASK;
	}

	uint32 GetReadAttributes( void ) const
	{
		return 0;
	}

	virtual uint64 GetReadControlPointMask() const
	{
		return 1ULL << m_nControlPointNumber;
	}

	virtual void InitParams( CParticleSystemDefinition *pDef )
	{
		m_nControlPointNumber = clamp( m_nControlPointNumber, 0, MAX_PARTICLE_CONTROL_POINTS - 1 );
	}

	virtual void InitNewParticlesScalar( CParticleCollection *pParticles, int start_p,
										 int nParticleCount, int nAttributeWriteMask,
										 void *pContext) const;

	int m_nControlPointNumber;
	Vector m_vOffsetMin;
	Vector m_vOffsetMax;
};

DEFINE_PARTICLE_OPERATOR( C_INIT_NormalAlignToCP, "Normal Align to CP", OPERATOR_GENERIC );

BEGIN_PARTICLE_INITIALIZER_OPERATOR_UNPACK( C_INIT_NormalAlignToCP )
	DMXELEMENT_UNPACK_FIELD( "control_point_number", "0", int, m_nControlPointNumber )
END_PARTICLE_OPERATOR_UNPACK( C_INIT_NormalAlignToCP )

void C_INIT_NormalAlignToCP::InitNewParticlesScalar(
	CParticleCollection *pParticles, int start_p,
	int nParticleCount, int nAttributeWriteMask, void *pContext ) const
{
	for( ; nParticleCount--; start_p++ )
	{
		float *pNormal = pParticles->GetFloatAttributePtrForWrite( PARTICLE_ATTRIBUTE_NORMAL, start_p );
		const float *ct = pParticles->GetFloatAttributePtr( PARTICLE_ATTRIBUTE_CREATION_TIME, start_p );

		Vector vecForward, vecRight, vecUp;
		pParticles->GetControlPointOrientationAtTime( m_nControlPointNumber, *ct, &vecForward, &vecRight, &vecUp );
		SetVectorAttribute( pNormal, vecForward );
	}
}


//-----------------------------------------------------------------------------
// Normal modify offset random
//-----------------------------------------------------------------------------
class C_INIT_NormalOffset : public CParticleInitializerOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_INIT_NormalOffset );

	uint32 GetWrittenAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_NORMAL_MASK | PARTICLE_ATTRIBUTE_CREATION_TIME_MASK;
	}

	uint32 GetReadAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_NORMAL_MASK;
	}

	virtual uint64 GetReadControlPointMask() const
	{
		return 1ULL << m_nControlPointNumber;
	}

	virtual void InitParams( CParticleSystemDefinition *pDef )
	{
		m_nControlPointNumber = clamp( m_nControlPointNumber, 0, MAX_PARTICLE_CONTROL_POINTS - 1 );
	}

	bool InitMultipleOverride ( void ) { return true; }

	virtual void InitNewParticlesScalar( CParticleCollection *pParticles, int start_p,
										 int nParticleCount, int nAttributeWriteMask,
										 void *pContext) const;

	Vector m_OffsetMin;
	Vector m_OffsetMax;
	int m_nControlPointNumber;
	bool m_bLocalCoords;
	bool m_bProportional;
	bool m_bNormalize;
};

DEFINE_PARTICLE_OPERATOR( C_INIT_NormalOffset, "Normal Modify Offset Random", OPERATOR_GENERIC );

BEGIN_PARTICLE_INITIALIZER_OPERATOR_UNPACK( C_INIT_NormalOffset )
	DMXELEMENT_UNPACK_FIELD( "control_point_number", "0", int, m_nControlPointNumber )
	DMXELEMENT_UNPACK_FIELD( "offset min", "0 0 0", Vector, m_OffsetMin )
	DMXELEMENT_UNPACK_FIELD( "offset max", "0 0 0", Vector, m_OffsetMax )
	DMXELEMENT_UNPACK_FIELD( "offset in local space 0/1", "0", bool, m_bLocalCoords )
	DMXELEMENT_UNPACK_FIELD( "normalize output 0/1", "0", bool, m_bNormalize )
END_PARTICLE_OPERATOR_UNPACK( C_INIT_NormalOffset )

void C_INIT_NormalOffset::InitNewParticlesScalar(
	CParticleCollection *pParticles, int start_p,
	int nParticleCount, int nAttributeWriteMask, void *pContext ) const
{
	for( ; nParticleCount--; start_p++ )
	{
		const float *ct = pParticles->GetFloatAttributePtr( PARTICLE_ATTRIBUTE_CREATION_TIME, start_p );
		float *normal = pParticles->GetFloatAttributePtrForWrite( PARTICLE_ATTRIBUTE_NORMAL, start_p );

		Vector randpos;
		pParticles->RandomVector( m_OffsetMin, m_OffsetMax, &randpos );

		if ( m_bLocalCoords )
		{
			matrix3x4_t mat;
			pParticles->GetControlPointTransformAtTime( m_nControlPointNumber, *ct, &mat );
			Vector vecTransformLocal = vec3_origin;
			VectorRotate( randpos, mat, vecTransformLocal );
			randpos = vecTransformLocal;
		}

		Vector vecOffset;
		SetVectorFromAttribute( vecOffset, normal );
		vecOffset += randpos;
		if ( m_bNormalize )
		{
			VectorNormalize( vecOffset );
		}
		SetVectorAttribute( normal, vecOffset );
	}
}


//-----------------------------------------------------------------------------
// Remap speed to scalar
//-----------------------------------------------------------------------------
class C_INIT_RemapSpeedToScalar : public CParticleInitializerOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_INIT_RemapSpeedToScalar );

	uint32 GetWrittenAttributes( void ) const
	{
		return 1 << m_nFieldOutput;
	}

	uint32 GetReadAttributes( void ) const
	{
		return ( 1 << m_nFieldOutput ) | PARTICLE_ATTRIBUTE_XYZ_MASK | PARTICLE_ATTRIBUTE_PREV_XYZ_MASK;
	}

	virtual uint32 GetFilter( void ) const
	{
		return FILTER_PARAMETER_REMAPPING_MASK;
	}

	virtual void InitParams( CParticleSystemDefinition *pDef )
	{
		// clamp the input range so we never divide by zero
		m_flInputMin = MAX( 0.001, m_flInputMin );
		m_flInputMax = MAX( 0.001, m_flInputMax );
	}

	bool InitMultipleOverride ( void ) { return true; }

	virtual void InitNewParticlesScalar( CParticleCollection *pParticles, int start_p,
										 int nParticleCount, int nAttributeWriteMask,
										 void *pContext) const;

	virtual void InitNewParticlesBlock( CParticleCollection *pParticles,
										int start_block, int n_blocks, int nAttributeWriteMask,
										void *pContext ) const;

	int m_nFieldOutput;
	int m_nControlPointNumber;
	float m_flStartTime;
	float m_flEndTime;
	float m_flInputMin;
	float m_flInputMax;
	float m_flOutputMin;
	float m_flOutputMax;
	bool m_bScaleInitialRange;
	bool m_bPerParticle;
};

DEFINE_PARTICLE_OPERATOR( C_INIT_RemapSpeedToScalar, "Remap Speed to Scalar", OPERATOR_GENERIC );

BEGIN_PARTICLE_INITIALIZER_OPERATOR_UNPACK( C_INIT_RemapSpeedToScalar )
	DMXELEMENT_UNPACK_FIELD( "emitter lifetime start time (seconds)", "-1", float, m_flStartTime )
	DMXELEMENT_UNPACK_FIELD( "emitter lifetime end time (seconds)", "-1", float, m_flEndTime )
	DMXELEMENT_UNPACK_FIELD( "control point number (ignored if per particle)", "0", int, m_nControlPointNumber )
	DMXELEMENT_UNPACK_FIELD( "per particle", "0", bool, m_bPerParticle )
	DMXELEMENT_UNPACK_FIELD( "input minimum", "0", float, m_flInputMin )
	DMXELEMENT_UNPACK_FIELD( "input maximum", "1", float, m_flInputMax )
	DMXELEMENT_UNPACK_FIELD_USERDATA( "output field", "3", int, m_nFieldOutput, "intchoice particlefield_scalar" )
	DMXELEMENT_UNPACK_FIELD( "output minimum", "0", float, m_flOutputMin )
	DMXELEMENT_UNPACK_FIELD( "output maximum", "1", float, m_flOutputMax )
	DMXELEMENT_UNPACK_FIELD( "output is scalar of initial random range", "0", bool, m_bScaleInitialRange )
END_PARTICLE_OPERATOR_UNPACK( C_INIT_RemapSpeedToScalar )

void C_INIT_RemapSpeedToScalar::InitNewParticlesBlock( CParticleCollection *pParticles,
													   int start_block, int n_blocks, int nAttributeWriteMask,
													   void *pContext ) const
{
	// only use within start/end time frame
	if ( ( ( pParticles->m_flCurTime < m_flStartTime ) || ( pParticles->m_flCurTime >= m_flEndTime ) ) && ( ( m_flStartTime != -1.0f ) && ( m_flEndTime != -1.0f ) ) )
		return;

	size_t attr_stride;
	const FourVectors *pXYZ = pParticles->Get4VAttributePtr( PARTICLE_ATTRIBUTE_XYZ, &attr_stride );
	pXYZ += attr_stride * start_block;
	const FourVectors *pPrevXYZ = pParticles->Get4VAttributePtr( PARTICLE_ATTRIBUTE_PREV_XYZ, &attr_stride );
	pPrevXYZ += attr_stride * start_block;
	fltx4 *pOutput = pParticles->GetM128AttributePtrForWrite( m_nFieldOutput, &attr_stride );
	pOutput += attr_stride * start_block;
	const fltx4 *pInitialOutput = pParticles->GetInitialM128AttributePtr( m_nFieldOutput, &attr_stride );
	pInitialOutput += attr_stride * start_block;

	fltx4 flMin = ReplicateX4( m_flOutputMin );
	fltx4 flMax = ReplicateX4( m_flOutputMax );
	if ( ATTRIBUTES_WHICH_ARE_0_TO_1 & ( 1 << m_nFieldOutput ) )
	{
		flMin = ReplicateX4( clamp( m_flOutputMin, 0.0f, 1.0f ) );
		flMax = ReplicateX4( clamp( m_flOutputMax, 0.0f, 1.0f ) );
	}

	fltx4 fl4Dt = ReplicateX4( pParticles->m_flPreviousDt );
	fltx4 fl4InputMin = ReplicateX4( m_flInputMin );
	fltx4 fl4InputMax = ReplicateX4( m_flInputMax );

	if ( m_bPerParticle )
	{
		while ( n_blocks-- )
		{
			FourVectors v4Delta = *pXYZ;
			v4Delta -= *pPrevXYZ;
			fltx4 fl4Speed = DivSIMD( SqrtSIMD( AddSIMD( AddSIMD( MulSIMD( v4Delta.y, v4Delta.y ), MulSIMD( v4Delta.x, v4Delta.x ) ), MulSIMD( v4Delta.z, v4Delta.z ) ) ), fl4Dt );
			fltx4 fl4Output = RemapValClampedSIMD( fl4Speed, fl4InputMin, fl4InputMax, flMin, flMax );
			if ( m_bScaleInitialRange )
			{
				fl4Output = MulSIMD( *pInitialOutput, fl4Output );
			}
			*pOutput = fl4Output;

			// NOTE: shipped code advances every pointer by the initial output stride
			pXYZ += attr_stride;
			pPrevXYZ += attr_stride;
			pOutput += attr_stride;
			pInitialOutput += attr_stride;
		}
	}
	else
	{
		Vector vecPrevPos;
		pParticles->GetControlPointAtPrevTime( m_nControlPointNumber, &vecPrevPos );
		Vector vecDelta = pParticles->GetControlPointAtCurrentTime( m_nControlPointNumber ) - vecPrevPos;
		FourVectors v4Delta;
		v4Delta.DuplicateVector( vecDelta );
		fltx4 fl4Speed = DivSIMD( SqrtSIMD( AddSIMD( AddSIMD( MulSIMD( v4Delta.x, v4Delta.x ), MulSIMD( v4Delta.y, v4Delta.y ) ), MulSIMD( v4Delta.z, v4Delta.z ) ) ), fl4Dt );
		fltx4 fl4Output = RemapValClampedSIMD( fl4Speed, fl4InputMin, fl4InputMax, flMin, flMax );
		while ( n_blocks-- )
		{
			// NOTE: shipped code scales the shared value, so the initial range scale accumulates
			if ( m_bScaleInitialRange )
			{
				fl4Output = MulSIMD( *pInitialOutput, fl4Output );
			}
			*pOutput = fl4Output;

			pOutput += attr_stride;
			pInitialOutput += attr_stride;
		}
	}
}

void C_INIT_RemapSpeedToScalar::InitNewParticlesScalar(
	CParticleCollection *pParticles, int start_p,
	int nParticleCount, int nAttributeWriteMask, void *pContext ) const
{
	// only use within start/end time frame
	if ( ( ( pParticles->m_flCurTime < m_flStartTime ) || ( pParticles->m_flCurTime >= m_flEndTime ) ) && ( ( m_flStartTime != -1.0f ) && ( m_flEndTime != -1.0f ) ) )
		return;

	float flMin = m_flOutputMin;
	float flMax = m_flOutputMax;
	if ( ATTRIBUTES_WHICH_ARE_0_TO_1 & ( 1 << m_nFieldOutput ) )
	{
		flMin = clamp( m_flOutputMin, 0.0f, 1.0f );
		flMax = clamp( m_flOutputMax, 0.0f, 1.0f );
	}

	if ( m_bPerParticle )
	{
		for( ; nParticleCount--; start_p++ )
		{
			const float *pXYZ = pParticles->GetFloatAttributePtr( PARTICLE_ATTRIBUTE_XYZ, start_p );
			const float *pPrevXYZ = pParticles->GetFloatAttributePtr( PARTICLE_ATTRIBUTE_PREV_XYZ, start_p );
			float *pOutput = pParticles->GetFloatAttributePtrForWrite( m_nFieldOutput, start_p );

			Vector vecXYZ( pXYZ[0], pXYZ[4], pXYZ[8] );
			Vector vecPrevPos( pPrevXYZ[0], pPrevXYZ[4], pPrevXYZ[8] );
			Vector vecDelta = vecXYZ - vecPrevPos;
			float flSpeed = sqrt( ( vecDelta.z * vecDelta.z + vecDelta.y * vecDelta.y ) + vecDelta.x * vecDelta.x ) / pParticles->m_flPreviousDt;
			float flOutput = RemapValClamped( flSpeed, m_flInputMin, m_flInputMax, flMin, flMax );
			if ( m_bScaleInitialRange )
			{
				const float *pInitialOutput = pParticles->GetInitialFloatAttributePtr( m_nFieldOutput, start_p );
				flOutput = *pInitialOutput * flOutput;
			}
			*pOutput = flOutput;
		}
	}
	else
	{
		Vector vecPrevPos;
		pParticles->GetControlPointAtPrevTime( m_nControlPointNumber, &vecPrevPos );
		Vector vecDelta = pParticles->GetControlPointAtCurrentTime( m_nControlPointNumber ) - vecPrevPos;
		float flSpeed = sqrt( ( vecDelta.z * vecDelta.z + vecDelta.y * vecDelta.y ) + vecDelta.x * vecDelta.x ) / pParticles->m_flPreviousDt;
		float flOutput = RemapValClamped( flSpeed, m_flInputMin, m_flInputMax, flMin, flMax );
		for( ; nParticleCount--; start_p++ )
		{
			float *pOutput = pParticles->GetFloatAttributePtrForWrite( m_nFieldOutput, start_p );
			// NOTE: shipped code scales the shared value, so the initial range scale accumulates
			if ( m_bScaleInitialRange )
			{
				const float *pInitialOutput = pParticles->GetInitialFloatAttributePtr( m_nFieldOutput, start_p );
				flOutput = *pInitialOutput * flOutput;
			}
			*pOutput = flOutput;
		}
	}
}


//-----------------------------------------------------------------------------
// Init from control point snapshot
//-----------------------------------------------------------------------------
class C_INIT_InitFromCPSnapshot : public CParticleInitializerOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_INIT_InitFromCPSnapshot );

	uint32 GetWrittenAttributes( void ) const
	{
		return ( m_nAttributeToWrite == -1 ) ? 0 : ( 1 << m_nAttributeToWrite );
	}

	uint32 GetReadAttributes( void ) const
	{
		return 0;
	}

	virtual uint64 GetReadControlPointMask() const
	{
		uint64 nMask = 0;
		if ( m_nControlPointNumber != -1 )
			nMask |= ( 1ULL << m_nControlPointNumber );
		if ( m_nLocalSpaceCP != -1 )
			nMask |= ( 1ULL << m_nLocalSpaceCP );
		return nMask;
	}

	virtual void InitParams( CParticleSystemDefinition *pDef )
	{
		m_nControlPointNumber = clamp( m_nControlPointNumber, 0, MAX_PARTICLE_CONTROL_POINTS - 1 );
		m_nLocalSpaceCP = clamp( m_nLocalSpaceCP, 0, MAX_PARTICLE_CONTROL_POINTS - 1 );

		// By default, read the same attribute we're writing
		if ( m_nAttributeToRead == -1 )
			m_nAttributeToRead = m_nAttributeToWrite;
	}

	virtual void InitNewParticlesScalar( CParticleCollection *pParticles, int start_p,
										 int nParticleCount, int nAttributeWriteMask,
										 void *pContext) const;

	int m_nControlPointNumber;
	int m_nAttributeToRead;
	int m_nAttributeToWrite;
	int m_nLocalSpaceCP;
};

DEFINE_PARTICLE_OPERATOR( C_INIT_InitFromCPSnapshot, "Init From CP Snapshot", OPERATOR_GENERIC );

BEGIN_PARTICLE_INITIALIZER_OPERATOR_UNPACK( C_INIT_InitFromCPSnapshot )
	DMXELEMENT_UNPACK_FIELD( "snapshot control point number", "0", int, m_nControlPointNumber )
	DMXELEMENT_UNPACK_FIELD_USERDATA( "field to write", "0", int, m_nAttributeToWrite, "intchoice particlefield" )
	DMXELEMENT_UNPACK_FIELD_USERDATA( "field to read", "-1", int, m_nAttributeToRead, "intchoice particlefield" )
	DMXELEMENT_UNPACK_FIELD( "local space control point number", "-1", int, m_nLocalSpaceCP )
END_PARTICLE_OPERATOR_UNPACK( C_INIT_InitFromCPSnapshot )

void C_INIT_InitFromCPSnapshot::InitNewParticlesScalar(
	CParticleCollection *pParticles, int start_p,
	int nParticleCount, int nAttributeWriteMask, void *pContext ) const
{
	if ( m_nAttributeToWrite == -1 )
		return;

	CParticleSnapshot *pSnapshot = ( m_nControlPointNumber == -1 ) ? NULL : pParticles->GetControlPointSnapshot( m_nControlPointNumber );
	EAttributeDataType nAttributeType = g_pParticleSystemMgr->GetParticleAttributeDataType( m_nAttributeToWrite );

	// Bail if there's no snapshot, we're not allowed to write the attribute, or the snapshot doesn't have it
	int nAttributeMask = 1 << m_nAttributeToWrite;
	if ( !pSnapshot || !( nAttributeWriteMask & nAttributeMask ) || !pSnapshot->HasAttribute( m_nAttributeToRead, nAttributeType ) )
		return;

	// Transform positions and normals into the local space control point's space
	matrix3x4_t cpTransform;
	bool bUseCPTransform = ( m_nLocalSpaceCP != -1 ) && ( nAttributeMask & ( PARTICLE_ATTRIBUTE_XYZ_MASK | PARTICLE_ATTRIBUTE_PREV_XYZ_MASK | PARTICLE_ATTRIBUTE_NORMAL_MASK ) );
	if ( bUseCPTransform )
	{
		pParticles->GetControlPointTransformAtCurrentTime( m_nLocalSpaceCP, &cpTransform );
		if ( nAttributeMask == PARTICLE_ATTRIBUTE_NORMAL_MASK )
		{
			// Don't translate normals
			cpTransform[0][3] = cpTransform[1][3] = cpTransform[2][3] = 0.0f;
		}
	}

	if ( ( pSnapshot->NumCols() < ( start_p + nParticleCount ) ) || !pParticles->GetFloatAttributePtrForWrite( m_nAttributeToWrite, 0 ) )
		return;

	switch( nAttributeType )
	{
	case ATTRDATATYPE_FLOAT:
		memcpy( pParticles->GetFloatAttributePtrForWrite( m_nAttributeToWrite, start_p ),
				pSnapshot->ElementPointer<float>( m_nAttributeToRead, start_p ),
				nParticleCount*sizeof(float) );
		break;
	case ATTRDATATYPE_INT:
		memcpy( pParticles->GetIntAttributePtrForWrite( m_nAttributeToWrite, start_p ),
				pSnapshot->ElementPointer<int>( m_nAttributeToRead, start_p ),
				nParticleCount*sizeof(int) );
		break;
	case ATTRDATATYPE_4V:
		for( ; nParticleCount--; start_p++ )
		{
			FourVectors *pSrc = pSnapshot->ElementPointer4V( m_nAttributeToRead, start_p );
			float *pDst = pParticles->GetFloatAttributePtrForWrite( m_nAttributeToWrite, start_p );
			Vector srcAttributeData = pSrc->Vec( 0 );
			if ( bUseCPTransform )
			{
				Vector srcAttributeDataTmp;
				VectorTransform( srcAttributeData, cpTransform, srcAttributeDataTmp );
				srcAttributeData = srcAttributeDataTmp;
			}
			SetVectorAttribute( pDst, srcAttributeData );
		}
		break;
	default:
		ExecuteNTimes( 10, Warning( "C_INIT_InitFromCPSnapshot: Unsupported attribute data type! (%d)\n", nAttributeType ) );
		break;
	}
}


//-----------------------------------------------------------------------------
// Init from killed parent particle
//-----------------------------------------------------------------------------
class C_INIT_InitFromParentKilled : public CParticleInitializerOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_INIT_InitFromParentKilled );

	uint32 GetWrittenAttributes( void ) const
	{
		return ( m_nAttributeToCopy == -1 ) ? 0 : ( 1 << m_nAttributeToCopy );
	}

	uint32 GetReadAttributes( void ) const
	{
		// NOTE: reads from the PARENT system's attributes
		return ( m_nAttributeToCopy == -1 ) ? 0 : ( 1 << m_nAttributeToCopy );
	}

	virtual bool ShouldRun( bool bApplyingParentKillList ) const
	{
		// Only run when the parent's kill list is being applied
		return bApplyingParentKillList;
	}

	virtual void InitNewParticlesScalar( CParticleCollection *pParticles, int start_p,
										 int nParticleCount, int nAttributeWriteMask,
										 void *pContext) const;

	int m_nAttributeToCopy;
	char m_AttributeCopyName[128];
};

DEFINE_PARTICLE_OPERATOR( C_INIT_InitFromParentKilled, "Init From Killed Parent Particle", OPERATOR_GENERIC );

BEGIN_PARTICLE_INITIALIZER_OPERATOR_UNPACK( C_INIT_InitFromParentKilled )
	DMXELEMENT_UNPACK_FIELD_USERDATA( "field to init", "-1", int, m_nAttributeToCopy, "intchoice particlefield" )
END_PARTICLE_OPERATOR_UNPACK( C_INIT_InitFromParentKilled )

void C_INIT_InitFromParentKilled::InitNewParticlesScalar(
	CParticleCollection *pParticles, int start_p,
	int nParticleCount, int nAttributeWriteMask, void *pContext ) const
{
	if ( ( m_nAttributeToCopy == -1 ) || !( nAttributeWriteMask & ( 1 << m_nAttributeToCopy ) ) )
		return;

	int nNumParticlesToKill;
	KillListItem_t *pKillList = GetParentKillList( pParticles, nNumParticlesToKill );
	if ( !pKillList || ( nNumParticlesToKill <= 0 ) )
		return;

	// The new particles correspond, in order, to the last nNumParticlesToKill entries of the parent's kill list
	pKillList += nNumParticlesToKill - pParticles->m_nActiveParticles + start_p;

	EAttributeDataType nAttributeType = g_pParticleSystemMgr->GetParticleAttributeDataType( m_nAttributeToCopy );
	for( int i = start_p; i < start_p + nParticleCount; i++ )
	{
		int nParentParticle = pKillList->nIndex;
		pKillList++;
		switch( nAttributeType )
		{
		case ATTRDATATYPE_FLOAT:
			{
				const float *pSrc = pParticles->m_pParent->GetFloatAttributePtr( m_nAttributeToCopy, nParentParticle );
				float *pDst = pParticles->GetFloatAttributePtrForWrite( m_nAttributeToCopy, i );
				*pDst = *pSrc;
			}
			break;
		case ATTRDATATYPE_INT:
			{
				const int *pSrc = pParticles->m_pParent->GetIntAttributePtr( m_nAttributeToCopy, nParentParticle );
				int *pDst = pParticles->GetIntAttributePtrForWrite( m_nAttributeToCopy, i );
				*pDst = *pSrc;
			}
			break;
		case ATTRDATATYPE_4V:
			{
				const float *pSrc = pParticles->m_pParent->GetFloatAttributePtr( m_nAttributeToCopy, nParentParticle );
				float *pDst = pParticles->GetFloatAttributePtrForWrite( m_nAttributeToCopy, i );
				Vector vecSrc;
				SetVectorFromAttribute( vecSrc, pSrc );
				SetVectorAttribute( pDst, vecSrc );
			}
			break;
		default:
			ExecuteNTimes( 10, Warning( "C_INIT_InitFromParentKilled: Unsupported attribute data type! (%d)\n", nAttributeType ) );
			break;
		}
	}
}


//@@REGISTRATION@@
//-----------------------------------------------------------------------------
// Registration
//-----------------------------------------------------------------------------
void AddBuiltInParticleInitializers( void )
{
	REGISTER_PARTICLE_OPERATOR( FUNCTION_INITIALIZER, C_INIT_RingWave );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_INITIALIZER, C_INIT_CreateAlongPath );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_INITIALIZER, C_INIT_MoveBetweenPoints );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_INITIALIZER, C_INIT_CreateWithinSphere );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_INITIALIZER, C_INIT_VelocityRandom );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_INITIALIZER, C_INIT_CreateOnModel );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_INITIALIZER, C_INIT_CreateWithinBox );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_INITIALIZER, C_INIT_RandomRotationSpeed );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_INITIALIZER, C_INIT_RandomLifeTime );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_INITIALIZER, C_INIT_RandomAlpha );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_INITIALIZER, C_INIT_RandomRadius );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_INITIALIZER, C_INIT_RandomRotation );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_INITIALIZER, C_INIT_RandomYaw );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_INITIALIZER, C_INIT_RandomColor );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_INITIALIZER, C_INIT_ColorLitPerParticle );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_INITIALIZER, C_INIT_RandomTrailLength );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_INITIALIZER, C_INIT_RandomSequence );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_INITIALIZER, C_INIT_PositionOffset );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_INITIALIZER, C_INIT_PositionWarp );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_INITIALIZER, C_INIT_PositionPlaceOnGround );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_INITIALIZER, C_INIT_CreationNoise );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_INITIALIZER, C_INIT_InitialVelocityNoise );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_INITIALIZER, C_INIT_RemapScalar );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_INITIALIZER, C_INIT_InheritVelocity );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_INITIALIZER, C_INIT_AgeNoise );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_INITIALIZER, C_INIT_SequenceLifeTime );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_INITIALIZER, C_INIT_CreateInHierarchy );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_INITIALIZER, C_INIT_RemapScalarToVector );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_INITIALIZER, C_INIT_OffsetVectorToVector );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_INITIALIZER, C_INIT_CreateSequentialPath );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_INITIALIZER, C_INIT_InitialRepulsionVelocity );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_INITIALIZER, C_INIT_RandomYawFlip );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_INITIALIZER, C_INIT_RandomSecondSequence );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_INITIALIZER, C_INIT_RemapCPtoScalar );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_INITIALIZER, C_INIT_RemapCPtoVector );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_INITIALIZER, C_INIT_RemapSpeedToScalar );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_INITIALIZER, C_INIT_CreateFromParentParticles );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_INITIALIZER, C_INIT_DistanceToCPInit );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_INITIALIZER, C_INIT_LifespanFromVelocity );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_INITIALIZER, C_INIT_CreateFromPlaneCache );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_INITIALIZER, C_INIT_ChaoticAttractor );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_INITIALIZER, C_INIT_ModelCull );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_INITIALIZER, C_INIT_RtEnvCull );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_INITIALIZER, C_INIT_NormalAlignToCP );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_INITIALIZER, C_INIT_NormalOffset );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_INITIALIZER, C_INIT_VelocityFromCP );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_INITIALIZER, C_INIT_SequenceFromCP );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_INITIALIZER, C_INIT_InitFromCPSnapshot );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_INITIALIZER, C_INIT_InitFromParentKilled );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_INITIALIZER, C_INIT_SetHitboxToModel );
}
