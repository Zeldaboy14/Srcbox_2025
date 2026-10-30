//===== Copyright (c) Valve Corporation, All rights reserved. ======//
//
// Purpose: particle constraints
//
// Reconstructed from the Alien Swarm SDK particles.lib (builtin_constraints.obj).
//
//===========================================================================//

#include "tier0/platform.h"
#include "particles/particles.h"
#include "filesystem.h"
#include "tier2/tier2.h"
#include "tier2/fileutils.h"
#include "bspflags.h"
#include "const.h"
#include "mathlib/ssemath.h"
#include "particles_internal.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"


//-----------------------------------------------------------------------------
// Constrains particles to a distance range from a control point
//-----------------------------------------------------------------------------
class C_OP_ConstrainDistance : public CParticleOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_OP_ConstrainDistance );

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

	bool EnforceConstraint( int nStartBlock,
							int nNumBlocks,
							CParticleCollection *pParticles,
							void *pContext,
							int nNumValidParticlesInLastChunk ) const;

	float m_fMinDistance;
	float m_fMaxDistance;
	int m_nControlPointNumber;
	Vector m_CenterOffset;
	bool m_bGlobalCenter;
};

DEFINE_PARTICLE_OPERATOR( C_OP_ConstrainDistance, "Constrain distance to control point", OPERATOR_GENERIC );

BEGIN_PARTICLE_OPERATOR_UNPACK( C_OP_ConstrainDistance )
	DMXELEMENT_UNPACK_FIELD( "minimum distance", "0", float, m_fMinDistance )
	DMXELEMENT_UNPACK_FIELD( "maximum distance", "100", float, m_fMaxDistance )
	DMXELEMENT_UNPACK_FIELD( "control point number", "0", int, m_nControlPointNumber )
	DMXELEMENT_UNPACK_FIELD( "offset of center", "0 0 0", Vector, m_CenterOffset )
	DMXELEMENT_UNPACK_FIELD( "global center point", "0", bool, m_bGlobalCenter )
END_PARTICLE_OPERATOR_UNPACK( C_OP_ConstrainDistance )

bool C_OP_ConstrainDistance::EnforceConstraint( int nStartBlock,
												int nNumBlocks,
												CParticleCollection *pParticles,
												void *pContext,
												int nNumValidParticlesInLastChunk ) const
{
	fltx4 SIMDMinDist = ReplicateX4( m_fMinDistance );
	fltx4 SIMDMaxDist = ReplicateX4( m_fMaxDistance );
	fltx4 SIMDMinDist2 = ReplicateX4( m_fMinDistance * m_fMinDistance );
	fltx4 SIMDMaxDist2 = ReplicateX4( m_fMaxDistance * m_fMaxDistance );

	size_t nStride;
	FourVectors *pXYZ = pParticles->Get4VAttributePtrForWrite( PARTICLE_ATTRIBUTE_XYZ, &nStride );
	pXYZ += nStride * nStartBlock;

	Vector vecCenter;
	if ( m_bGlobalCenter )
	{
		vecCenter = m_CenterOffset;
	}
	else
	{
		pParticles->GetControlPointAtTime( m_nControlPointNumber, pParticles->m_flCurTime, &vecCenter );
		vecCenter += pParticles->TransformAxis( m_CenterOffset, true, m_nControlPointNumber );
	}

	FourVectors Center;
	Center.DuplicateVector( vecCenter );

	bool bChangedSomething = false;
	do
	{
		FourVectors pts = *( pXYZ );
		pts -= Center;
		fltx4 dist_squared = pts * pts;
		fltx4 TooFarMask = CmpGtSIMD( dist_squared, SIMDMaxDist2 );
		fltx4 TooCloseMask = CmpLtSIMD( dist_squared, SIMDMinDist2 );
		fltx4 NeedAdjust = OrSIMD( TooFarMask, TooCloseMask );
		if ( IsAnyNegative( NeedAdjust ) )
		{
			// normalize
			fltx4 guess = ReciprocalSqrtEstSaturateSIMD( dist_squared );
			// newton iteration for 1/sqrt(x) : y(n+1)=1/2 (y(n)*(3-x*y(n)^2));
			guess = MulSIMD( guess, SubSIMD( Four_Threes, MulSIMD( dist_squared, MulSIMD( guess, guess ) ) ) );
			guess = MulSIMD( Four_PointFives, guess );
			pts *= guess;

			FourVectors clamp_far = pts;
			clamp_far *= SIMDMaxDist;
			clamp_far += Center;
			FourVectors clamp_near = pts;
			clamp_near *= SIMDMinDist;
			clamp_near += Center;
			pts.x = MaskedAssign( TooCloseMask, clamp_near.x, MaskedAssign( TooFarMask, clamp_far.x, pXYZ->x ) );
			pts.y = MaskedAssign( TooCloseMask, clamp_near.y, MaskedAssign( TooFarMask, clamp_far.y, pXYZ->y ) );
			pts.z = MaskedAssign( TooCloseMask, clamp_near.z, MaskedAssign( TooFarMask, clamp_far.z, pXYZ->z ) );
			*( pXYZ ) = pts;
			bChangedSomething = true;
		}
		pXYZ += nStride;
	} while ( --nNumBlocks );
	return bChangedSomething;
}


//-----------------------------------------------------------------------------
// Constrains particles to a distance range from a path between two control points
//-----------------------------------------------------------------------------
class C_OP_ConstrainDistanceToPath : public CParticleOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_OP_ConstrainDistanceToPath );

	uint32 GetWrittenAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_XYZ_MASK;
	}

	uint32 GetReadAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_XYZ_MASK | PARTICLE_ATTRIBUTE_CREATION_TIME_MASK;
	}

	virtual uint64 GetReadControlPointMask() const
	{
		return ( 1ULL << m_PathParameters.m_nStartControlPointNumber ) |
			( 1ULL << m_PathParameters.m_nEndControlPointNumber );
	}

	bool EnforceConstraint( int nStartBlock,
							int nNumBlocks,
							CParticleCollection *pParticles,
							void *pContext,
							int nNumValidParticlesInLastChunk ) const;

	float m_fMinDistance;
	float m_flMaxDistance0;
	float m_flMaxDistanceMid;
	float m_flMaxDistance1;
	CPathParameters m_PathParameters;
	float m_flTravelTime;
};

DEFINE_PARTICLE_OPERATOR( C_OP_ConstrainDistanceToPath, "Constrain distance to path between two control points", OPERATOR_GENERIC );

BEGIN_PARTICLE_OPERATOR_UNPACK( C_OP_ConstrainDistanceToPath )
	DMXELEMENT_UNPACK_FIELD( "minimum distance", "0", float, m_fMinDistance )
	DMXELEMENT_UNPACK_FIELD( "maximum distance", "100", float, m_flMaxDistance0 )
	DMXELEMENT_UNPACK_FIELD( "maximum distance middle", "-1", float, m_flMaxDistanceMid )
	DMXELEMENT_UNPACK_FIELD( "maximum distance end", "-1", float, m_flMaxDistance1 )
	DMXELEMENT_UNPACK_FIELD( "travel time", "10", float, m_flTravelTime )
	DMXELEMENT_UNPACK_FIELD( "random bulge", "0", float, m_PathParameters.m_flBulge )
	DMXELEMENT_UNPACK_FIELD( "start control point number", "0", int, m_PathParameters.m_nStartControlPointNumber )
	DMXELEMENT_UNPACK_FIELD( "end control point number", "0", int, m_PathParameters.m_nEndControlPointNumber )
	DMXELEMENT_UNPACK_FIELD( "bulge control 0=random 1=orientation of start pnt 2=orientation of end point", "0", int, m_PathParameters.m_nBulgeControl )
	DMXELEMENT_UNPACK_FIELD( "mid point position", "0.5", float, m_PathParameters.m_flMidPoint )
END_PARTICLE_OPERATOR_UNPACK( C_OP_ConstrainDistanceToPath )

bool C_OP_ConstrainDistanceToPath::EnforceConstraint( int nStartBlock,
													  int nNumBlocks,
													  CParticleCollection *pParticles,
													  void *pContext,
													  int nNumValidParticlesInLastChunk ) const
{
	C4VAttributeWriteIterator pXYZ( PARTICLE_ATTRIBUTE_XYZ, pParticles );
	pXYZ += nStartBlock;

	CM128AttributeIterator pCreationTime( PARTICLE_ATTRIBUTE_CREATION_TIME, pParticles );
	pCreationTime += nStartBlock;

	Vector StartPnt, MidP, EndPnt;
	pParticles->CalculatePathValues( m_PathParameters, pParticles->m_flCurTime, &StartPnt, &MidP, &EndPnt );

	fltx4 CurTime = ReplicateX4( pParticles->m_flCurTime );
	fltx4 TimeScale = ReplicateX4( 1.0f / MAX( 0.001f, m_flTravelTime ) );

	// calculate radius spline
	bool bConstantRadius = true;
	fltx4 Rad0 = ReplicateX4( m_flMaxDistance0 );
	fltx4 Radm = Rad0;
	if ( m_flMaxDistanceMid >= 0.0 )
	{
		bConstantRadius = ( m_flMaxDistanceMid == m_flMaxDistance0 );
		Radm = ReplicateX4( m_flMaxDistanceMid );
	}
	fltx4 Rad1 = Radm;
	if ( m_flMaxDistance1 >= 0.0 )
	{
		bConstantRadius &= ( m_flMaxDistance1 == m_flMaxDistance0 );
		Rad1 = ReplicateX4( m_flMaxDistance1 );
	}

	fltx4 RadmMinusRad0 = SubSIMD( Radm, Rad0 );
	fltx4 Rad1MinusRadm = SubSIMD( Rad1, Radm );

	fltx4 SIMDMinDist = ReplicateX4( m_fMinDistance );
	fltx4 SIMDMinDist2 = ReplicateX4( m_fMinDistance * m_fMinDistance );

	fltx4 SIMDMaxDist = MaxSIMD( Rad0, MaxSIMD( Radm, Rad1 ) );
	fltx4 SIMDMaxDist2 = MulSIMD( SIMDMaxDist, SIMDMaxDist );

	FourVectors StartP;
	StartP.DuplicateVector( StartPnt );

	FourVectors MiddleP;
	MiddleP.DuplicateVector( MidP );

	// form delta terms needed for quadratic bezier
	FourVectors Delta0;
	Delta0.DuplicateVector( MidP - StartPnt );

	FourVectors Delta1;
	Delta1.DuplicateVector( EndPnt - MidP );

	bool bChangedSomething = false;
	do
	{
		fltx4 TScale = MinSIMD( Four_Ones, MulSIMD( TimeScale, SubSIMD( CurTime, *pCreationTime ) ) );

		// bezier(a,b,c,t)=lerp( lerp(a,b,t),lerp(b,c,t),t)
		FourVectors L0 = Delta0;
		L0 *= TScale;
		L0 += StartP;

		FourVectors L1 = Delta1;
		L1 *= TScale;
		L1 += MiddleP;

		FourVectors Center = L1;
		Center -= L0;
		Center *= TScale;
		Center += L0;

		FourVectors pts = *( pXYZ );
		pts -= Center;

		// calculate radius at the point. !!speed!! - use special case for constant radius
		fltx4 dist_squared = pts * pts;
		fltx4 TooFarMask = CmpGtSIMD( dist_squared, SIMDMaxDist2 );
		if ( ( !bConstantRadius ) && ( !IsAnyNegative( TooFarMask ) ) )
		{
			// need to calculate and adjust for true radius =- we've only trivially rejected note
			// voodoo here - we update simdmaxdist for true radius, but not max dist^2, since
			// that's used only for the trivial reject case, which we've already done
			fltx4 R0 = AddSIMD( Rad0, MulSIMD( RadmMinusRad0, TScale ) );
			fltx4 R1 = AddSIMD( Radm, MulSIMD( Rad1MinusRadm, TScale ) );
			SIMDMaxDist = AddSIMD( R0, MulSIMD( SubSIMD( R1, R0 ), TScale ) );

			// now that we know the true radius, update our mask
			TooFarMask = CmpGtSIMD( dist_squared, MulSIMD( SIMDMaxDist, SIMDMaxDist ) );
		}

		fltx4 TooCloseMask = CmpLtSIMD( dist_squared, SIMDMinDist2 );
		fltx4 NeedAdjust = OrSIMD( TooFarMask, TooCloseMask );
		if ( IsAnyNegative( NeedAdjust ) )
		{
			// normalize
			fltx4 guess = ReciprocalSqrtEstSIMD( dist_squared );
			// newton iteration for 1/sqrt(x) : y(n+1)=1/2 (y(n)*(3-x*y(n)^2));
			guess = MulSIMD( guess, SubSIMD( Four_Threes, MulSIMD( dist_squared, MulSIMD( guess, guess ) ) ) );
			guess = MulSIMD( Four_PointFives, guess );
			pts *= guess;

			FourVectors clamp_far = pts;
			clamp_far *= SIMDMaxDist;
			clamp_far += Center;
			FourVectors clamp_near = pts;
			clamp_near *= SIMDMinDist;
			clamp_near += Center;
			pXYZ->x = MaskedAssign( TooCloseMask, clamp_near.x, MaskedAssign( TooFarMask, clamp_far.x, pXYZ->x ) );
			pXYZ->y = MaskedAssign( TooCloseMask, clamp_near.y, MaskedAssign( TooFarMask, clamp_far.y, pXYZ->y ) );
			pXYZ->z = MaskedAssign( TooCloseMask, clamp_near.z, MaskedAssign( TooFarMask, clamp_far.z, pXYZ->z ) );
			bChangedSomething = true;
		}
		++pXYZ;
		++pCreationTime;
	} while ( --nNumBlocks );
	return bChangedSomething;
}


//-----------------------------------------------------------------------------
// Prevents particles from crossing a plane
//-----------------------------------------------------------------------------
class C_OP_PlanarConstraint : public CParticleOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_OP_PlanarConstraint );

	uint32 GetWrittenAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_XYZ_MASK;
	}

	uint32 GetReadAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_XYZ_MASK | PARTICLE_ATTRIBUTE_RADIUS_MASK;
	}

	virtual uint64 GetReadControlPointMask() const
	{
		return 1ULL << m_nControlPointNumber;
	}

	bool EnforceConstraint( int nStartBlock,
							int nNumBlocks,
							CParticleCollection *pParticles,
							void *pContext,
							int nNumValidParticlesInLastChunk ) const;

	Vector m_PointOnPlane;
	Vector m_PlaneNormal;
	int m_nControlPointNumber;
	bool m_bGlobalOrigin;
	bool m_bGlobalNormal;
};

DEFINE_PARTICLE_OPERATOR( C_OP_PlanarConstraint, "Prevent passing through a plane", OPERATOR_GENERIC );

BEGIN_PARTICLE_OPERATOR_UNPACK( C_OP_PlanarConstraint )
	DMXELEMENT_UNPACK_FIELD( "control point number", "0", int, m_nControlPointNumber )
	DMXELEMENT_UNPACK_FIELD( "plane point", "0 0 0", Vector, m_PointOnPlane )
	DMXELEMENT_UNPACK_FIELD( "plane normal", "0 0 1", Vector, m_PlaneNormal )
	DMXELEMENT_UNPACK_FIELD( "global origin", "0", bool, m_bGlobalOrigin )
	DMXELEMENT_UNPACK_FIELD( "global normal", "0", bool, m_bGlobalNormal )
END_PARTICLE_OPERATOR_UNPACK( C_OP_PlanarConstraint )

bool C_OP_PlanarConstraint::EnforceConstraint( int nStartBlock,
											   int nNumBlocks,
											   CParticleCollection *pParticles,
											   void *pContext,
											   int nNumValidParticlesInLastChunk ) const
{
	C4VAttributeWriteIterator pXYZ( PARTICLE_ATTRIBUTE_XYZ, pParticles );
	pXYZ += nStartBlock;
	CM128AttributeIterator pRadius( PARTICLE_ATTRIBUTE_RADIUS, pParticles );
	pRadius += nStartBlock;

	// now, transform and offset parameters
	FourVectors PlaneNormal;
	PlaneNormal.DuplicateVector( pParticles->TransformAxis( m_PlaneNormal, !m_bGlobalNormal, m_nControlPointNumber ) );
	PlaneNormal.VectorNormalize();

	FourVectors PlanePoint;
	if ( m_bGlobalOrigin )
	{
		PlanePoint.DuplicateVector( m_PointOnPlane );
	}
	else
	{
		Vector ofs = pParticles->TransformAxis( m_PointOnPlane, true, m_nControlPointNumber );

		Vector vecCenter;
		pParticles->GetControlPointAtTime( m_nControlPointNumber, pParticles->m_flCurTime, &vecCenter );
		PlanePoint.DuplicateVector( vecCenter + ofs );
	}

	bool bChangedSomething = false;
	do
	{
		FourVectors pts = *( pXYZ );
		FourVectors ofs = pts;
		ofs -= PlanePoint;
		fltx4 PlaneEq = SubSIMD( ofs * PlaneNormal, *pRadius );
		fltx4 BadPts = CmpLtSIMD( PlaneEq, Four_Zeros );
		if ( IsAnyNegative( BadPts ) )
		{
			// push the particles back out along the plane normal
			PlaneEq = MinSIMD( Four_Zeros, PlaneEq );
			FourVectors v4Delta = PlaneNormal;
			v4Delta *= PlaneEq;
			pts -= v4Delta;
			*( pXYZ ) = pts;
			bChangedSomething = true;
		}
		++pXYZ;
		++pRadius;
	} while ( --nNumBlocks );
	return bChangedSomething;
}


//-----------------------------------------------------------------------------
// Cached world collision planes
//-----------------------------------------------------------------------------
void CWorldCollideContextData::SetBaseTrace( int nIndex, Vector const &rayStart, Vector const &traceDir, int nCollisionGroup, bool bKeepMisses )
{
	CBaseTrace tr;
	Vector rayEnd = rayStart + traceDir;
	g_pParticleSystemMgr->Query()->TraceLine( rayStart, rayEnd, MASK_SOLID, NULL, nCollisionGroup, &tr );
	if ( tr.fraction < 1.0 )
	{
		m_bPlaneActive[nIndex] = true;
		m_PointOnPlane[nIndex].DuplicateVector( rayStart + tr.fraction * traceDir );
		m_PlaneNormal[nIndex].DuplicateVector( tr.plane.normal );
		m_TraceStartPnt[nIndex].DuplicateVector( rayStart );
		m_TraceEndPnt[nIndex].DuplicateVector( rayEnd );
	}
	else
	{
		// no hit. a miss
		if ( bKeepMisses )
		{
			m_PlaneNormal[nIndex].x = Four_Zeros;
			m_PlaneNormal[nIndex].y = Four_Zeros;
			m_PlaneNormal[nIndex].z = Four_Zeros;
			m_TraceStartPnt[nIndex].DuplicateVector( rayStart );
			m_TraceEndPnt[nIndex].DuplicateVector( rayEnd );
			m_bPlaneActive[nIndex] = true;
		}
		else
		{
			m_bPlaneActive[nIndex] = false;
		}
	}
}

//-----------------------------------------------------------------------------
// Collision modes:
//   0: trace every particle every frame
//   1: per-frame plane set (26 traces from control point 0, misses dropped)
//   2: a single trace from control point 0, kept while planes are active
//   3: per-frame plane set with misses kept, used for nearest-plane lookups
//-----------------------------------------------------------------------------
void CWorldCollideContextData::CalculatePlanes( CParticleCollection *pParticles, int nCollisionMode, int nCollisionGroup, Vector const *pCpOffset, float flMovementTolerance )
{
	// mode 2 only traces once
	if ( m_nActivePlanes && ( nCollisionMode == 2 ) )
		return;

	Vector rayStart = pParticles->GetControlPointAtCurrentTime( 0 );
	if ( pCpOffset )
		rayStart += *pCpOffset;

	// don't retrace unless the control point has moved far enough
	if ( m_flLastUpdateTime > 0 )
	{
		if ( ( rayStart - m_vecLastUpdateOrigin ).LengthSqr() < flMovementTolerance * flMovementTolerance )
			return;
	}
	m_vecLastUpdateOrigin = rayStart;
	m_nActivePlanes = 0;
	switch( nCollisionMode )
	{
		case 2:
		{
			// NOTE: the shipped library traces along -X here
			SetBaseTrace( 0, rayStart, Vector( -1000, 0, 0 ), nCollisionGroup, false );
			m_nActivePlanes = 1;
			m_nNumFixedPlanes = 1;
		}
		break;

		case 1:
		{
			int nIndexOut = 0;
			for( int i = -1; i <= 1; i++ )
			{
				for( int j = -1; j <= 1; j++ )
				{
					for( int k = -1; k <= 1; k++ )
					{
						if ( i || j || k )
						{
							SetBaseTrace( nIndexOut++, rayStart, 1000.0f * Vector( i, j, k ), nCollisionGroup, false );
						}
					}
				}
			}
			m_nNumFixedPlanes = nIndexOut;
			m_nActivePlanes = nIndexOut;
		}
		// NOTE: no break in the shipped library - mode 1 falls through into mode 3

		case 3:
		{
			int nIndexOut = 0;
			for( int i = -1; i <= 1; i++ )
			{
				for( int j = -1; j <= 1; j++ )
				{
					for( int k = -1; k <= 1; k++ )
					{
						if ( i || j || k )
						{
							SetBaseTrace( nIndexOut++, rayStart, 1000.0f * Vector( i, j, k ), nCollisionGroup, true );
						}
					}
				}
			}
			m_nNumFixedPlanes = nIndexOut;
			m_nActivePlanes = nIndexOut;
		}
		break;
	}
}


//-----------------------------------------------------------------------------
// Collides particles with the cached world planes
//-----------------------------------------------------------------------------
class C_OP_WorldCollideConstraint : public CParticleOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_OP_WorldCollideConstraint );

	uint32 GetWrittenAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_XYZ_MASK;
	}

	uint32 GetReadAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_XYZ_MASK | PARTICLE_ATTRIBUTE_RADIUS_MASK;
	}

	virtual uint64 GetReadControlPointMask() const
	{
		return 1ULL << 0;
	}

	virtual size_t GetRequiredContextBytes( ) const
	{
		return sizeof( CWorldCollideContextData );
	}

	virtual void SetupConstraintPerFrameData( CParticleCollection *pParticles, void *pContext ) const
	{
		CWorldCollideContextData *pCtx = reinterpret_cast<CWorldCollideContextData *>( pContext );
		pCtx->CalculatePlanes( pParticles, 1, COLLISION_GROUP_NONE );
	}

	bool EnforceConstraint( int nStartBlock,
							int nNumBlocks,
							CParticleCollection *pParticles,
							void *pContext,
							int nNumValidParticlesInLastChunk ) const;
};

DEFINE_PARTICLE_OPERATOR( C_OP_WorldCollideConstraint, "Prevent passing through static part of world", OPERATOR_GENERIC );

BEGIN_PARTICLE_OPERATOR_UNPACK( C_OP_WorldCollideConstraint )
END_PARTICLE_OPERATOR_UNPACK( C_OP_WorldCollideConstraint )

bool C_OP_WorldCollideConstraint::EnforceConstraint( int nStartBlock,
													 int nNumBlocks,
													 CParticleCollection *pParticles,
													 void *pContext,
													 int nNumValidParticlesInLastChunk ) const
{
	C4VAttributeWriteIterator pXYZ( PARTICLE_ATTRIBUTE_XYZ, pParticles );
	pXYZ += nStartBlock;
	CM128AttributeIterator pRadius( PARTICLE_ATTRIBUTE_RADIUS, pParticles );
	pRadius += nStartBlock;

	CWorldCollideContextData *pCtx = reinterpret_cast<CWorldCollideContextData *>( pContext );

	bool bChangedSomething = false;
	do
	{
		for( int i = 0; i < pCtx->m_nActivePlanes; i++ )
		{
			FourVectors pts = *( pXYZ );
			pts -= pCtx->m_PointOnPlane[i];
			fltx4 PlaneEq = pts * pCtx->m_PlaneNormal[i];
			PlaneEq = SubSIMD( PlaneEq, *pRadius );
			fltx4 BadPts = CmpLtSIMD( PlaneEq, Four_Zeros );
			if ( IsAnyNegative( BadPts ) )
			{
				// push the particles back out along the plane normal
				PlaneEq = MinSIMD( Four_Zeros, PlaneEq );
				FourVectors v4Delta = pCtx->m_PlaneNormal[i];
				v4Delta *= PlaneEq;
				*( pXYZ ) -= v4Delta;
				bChangedSomething = true;
			}
		}
		++pXYZ;
		++pRadius;
	} while ( --nNumBlocks );
	return bChangedSomething;
}


//-----------------------------------------------------------------------------
// Collides particles with the world using traces
//-----------------------------------------------------------------------------
struct ISectData_t
{
	fltx4 m_ISectT;										// 0..1 if hit
	fltx4 m_LeftOverT;										// 1-isectt
	FourVectors m_ISectNormal;								// normal at isect pnt
};

class C_OP_WorldTraceConstraint : public CParticleOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_OP_WorldTraceConstraint );

	uint32 GetWrittenAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_XYZ_MASK | PARTICLE_ATTRIBUTE_LIFE_DURATION_MASK;
	}

	uint32 GetReadAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_XYZ_MASK | PARTICLE_ATTRIBUTE_RADIUS_MASK;
	}

	virtual uint64 GetReadControlPointMask() const
	{
		return 1ULL << 0;
	}

	bool EnforceConstraint( int nStartBlock,
							int nNumBlocks,
							CParticleCollection *pParticles,
							void *pContext,
							int nNumValidParticlesInLastChunk ) const;

	template<bool bKillonContact, bool bCached>
	bool EnforceConstraintInternal( int nStartBlock,
									int nNumBlocks,
									CParticleCollection *pParticles,
									void *pContext,
									int nNumValidParticlesInLastChunk ) const;

	virtual bool IsFinalConstaint( void ) const
	{
		return ( m_flBounceAmount != 0. ) || ( m_flSlideAmount != 0. );
	}

	virtual void InitializeContextData( CParticleCollection *pParticles, void *pContext ) const
	{
	}

	virtual void InitParams( CParticleSystemDefinition *pDef )
	{
		m_nCollisionGroupNumber = g_pParticleSystemMgr->Query()->GetCollisionGroupFromName( m_CollisionGroupName );
	}

	Vector m_vecCpOffset;
	int m_nCollisionMode;
	float m_flBounceAmount;
	float m_flSlideAmount;
	float m_flRadiusScale;
	float m_flCpMovementTolerance;
	float m_flTraceTolerance;
	bool m_bKillonContact;
	char m_CollisionGroupName[128];
	int m_nCollisionGroupNumber;
	bool m_bBrushOnly;
};

DEFINE_PARTICLE_OPERATOR( C_OP_WorldTraceConstraint, "Collision via traces", OPERATOR_GENERIC );

BEGIN_PARTICLE_OPERATOR_UNPACK( C_OP_WorldTraceConstraint )
	DMXELEMENT_UNPACK_FIELD( "collision mode", "0", int, m_nCollisionMode )
	DMXELEMENT_UNPACK_FIELD( "amount of bounce", "0", float, m_flBounceAmount )
	DMXELEMENT_UNPACK_FIELD( "amount of slide", "0", float, m_flSlideAmount )
	DMXELEMENT_UNPACK_FIELD( "radius scale", "1", float, m_flRadiusScale )
	DMXELEMENT_UNPACK_FIELD( "brush only", "0", bool, m_bBrushOnly )
	DMXELEMENT_UNPACK_FIELD_STRING( "collision group", "NONE", m_CollisionGroupName )
	DMXELEMENT_UNPACK_FIELD( "control point offset for fast collisions", "0 0 0", Vector, m_vecCpOffset )
	DMXELEMENT_UNPACK_FIELD( "control point movement distance tolerance", "5", float, m_flCpMovementTolerance )
	DMXELEMENT_UNPACK_FIELD( "kill particle on collision", "0", bool, m_bKillonContact )
	DMXELEMENT_UNPACK_FIELD( "trace accuracy tolerance", "24", float, m_flTraceTolerance )
END_PARTICLE_OPERATOR_UNPACK( C_OP_WorldTraceConstraint )

// Intersects 4 segments against the cached planes, retracing the ones that are too far from
// every cached trace.
static void WorldIntersectTNew( FourVectors const *pStartPnt, FourVectors const *pEndPnt,
								int nCollisionGroup, int nMask, ISectData_t *pISectData,
								int nCollisionMode, CWorldCollideContextData *pCtx,
								fltx4 const &fl4ParticleValidMask, float flTolerance = 0.0 )
{
	pISectData->m_ISectT = Four_Zeros;
	pISectData->m_LeftOverT = Four_Zeros;
	pISectData->m_ISectNormal.x = Four_Zeros;
	pISectData->m_ISectNormal.y = Four_Zeros;
	pISectData->m_ISectNormal.z = Four_Zeros;

	if ( pCtx )
	{
		pISectData->m_ISectT = Four_Twos;
		if ( nCollisionMode == 3 )
		{
			// find the closest cached trace for each particle
			pISectData->m_ISectT = Four_Twos;
			FourVectors v4PointOnPlane;
			FourVectors v4PlaneNormal;
			fltx4 fl4ClosestDist = Four_FLT_MAX;
			for( int i = 0; i < pCtx->m_nActivePlanes; i++ )
			{
				if ( pCtx->m_bPlaneActive[i] )
				{
					fltx4 fl4TrialDistance = MaxSIMD(
						pStartPnt->DistSqrToLineSegment( pCtx->m_TraceStartPnt[i], pCtx->m_TraceEndPnt[i] ),
						pEndPnt->DistSqrToLineSegment( pCtx->m_TraceStartPnt[i], pCtx->m_TraceEndPnt[i] ) );
					fltx4 fl4Nearestmask = CmpLeSIMD( fl4TrialDistance, fl4ClosestDist );
					// NOTE: the argument order here matches the shipped library (not a min)
					fl4ClosestDist = MaskedAssign( fl4ClosestDist, fl4TrialDistance, fl4Nearestmask );
					v4PointOnPlane.x = MaskedAssign( fl4Nearestmask, pCtx->m_PointOnPlane[i].x, v4PointOnPlane.x );
					v4PointOnPlane.y = MaskedAssign( fl4Nearestmask, pCtx->m_PointOnPlane[i].y, v4PointOnPlane.y );
					v4PointOnPlane.z = MaskedAssign( fl4Nearestmask, pCtx->m_PointOnPlane[i].z, v4PointOnPlane.z );
					v4PlaneNormal.x = MaskedAssign( fl4Nearestmask, pCtx->m_PlaneNormal[i].x, v4PlaneNormal.x );
					v4PlaneNormal.y = MaskedAssign( fl4Nearestmask, pCtx->m_PlaneNormal[i].y, v4PlaneNormal.y );
					v4PlaneNormal.z = MaskedAssign( fl4Nearestmask, pCtx->m_PlaneNormal[i].z, v4PlaneNormal.z );
				}
			}

			// trace the particles that aren't close enough to a cached trace
			fltx4 fl4OutOfRange = AndSIMD( CmpGtSIMD( fl4ClosestDist, ReplicateX4( flTolerance ) ), fl4ParticleValidMask );
			{
				// NOTE: this shadows the nMask parameter, so the bitmask of particles to trace is
				// what gets passed to TraceLine as the content mask. Kept to match the shipped library.
				int nMask = TestSignSIMD( fl4OutOfRange );
				if ( nMask )
				{
					for( int i = 0; i < 4; i++ )
					{
						if ( nMask & ( 1 << i ) )
						{
							Vector start( pStartPnt->X( i ), pStartPnt->Y( i ), pStartPnt->Z( i ) );
							Vector delta = Vector( pEndPnt->X( i ), pEndPnt->Y( i ), pEndPnt->Z( i ) ) - start;
							float flLen = delta.Length();
							float flScale = MAX( 5.0, 300.0 / ( flLen + 0.01 ) );
							Vector end = start + delta * flScale;
							CBaseTrace tr;
							g_pParticleSystemMgr->Query()->TraceLine( start, end, nMask, NULL, nCollisionGroup, &tr );
							if ( tr.fraction < 1.0 )
							{
								SubFloat( v4PointOnPlane.x, i ) = start.x + ( end.x - start.x ) * tr.fraction;
								SubFloat( v4PointOnPlane.y, i ) = start.y + ( end.y - start.y ) * tr.fraction;
								SubFloat( v4PointOnPlane.z, i ) = start.z + ( end.z - start.z ) * tr.fraction;
								SubFloat( v4PlaneNormal.x, i ) = tr.plane.normal.x;
								SubFloat( v4PlaneNormal.y, i ) = tr.plane.normal.y;
								SubFloat( v4PlaneNormal.z, i ) = tr.plane.normal.z;
							}
							else
							{
								// no hit: a zero normal never intersects
								SubFloat( v4PlaneNormal.x, i ) = 0;
								SubFloat( v4PlaneNormal.y, i ) = 0;
								SubFloat( v4PlaneNormal.z, i ) = 0;
							}
						}
					}
				}
			}

			// now, intersect against the chosen planes
			//fltx4 fl4StartDist = ( *pStartPnt - v4PointOnPlane ) * v4PlaneNormal;
			FourVectors v4StartToPlane = *pStartPnt;
			v4StartToPlane -= v4PointOnPlane;
			fltx4 fl4StartDist = v4StartToPlane * v4PlaneNormal;

			//fltx4 fl4EndDist = ( *pEndPnt - v4PointOnPlane ) * v4PlaneNormal;
			FourVectors v4EndToPlane = *pEndPnt;
			v4EndToPlane -= v4PointOnPlane;
			fltx4 fl4EndDist = v4EndToPlane * v4PlaneNormal;

			fltx4 fl4CrossMask = AndSIMD( AndSIMD( CmpLtSIMD( fl4EndDist, Four_Zeros ), CmpGeSIMD( fl4StartDist, Four_Zeros ) ),
										  fl4ParticleValidMask );
			if ( IsAnyNegative( fl4CrossMask ) )
			{
				fltx4 fl4T = DivSIMD( fl4StartDist, SubSIMD( fl4StartDist, fl4EndDist ) );
				fl4CrossMask = AndSIMD( fl4CrossMask, CmpLtSIMD( fl4T, pISectData->m_ISectT ) );
				if ( IsAnyNegative( fl4CrossMask ) )
				{
					pISectData->m_ISectT = MaskedAssign( fl4CrossMask, fl4T, pISectData->m_ISectT );
					pISectData->m_ISectNormal.x = MaskedAssign( fl4CrossMask, v4PlaneNormal.x, pISectData->m_ISectNormal.x );
					pISectData->m_ISectNormal.y = MaskedAssign( fl4CrossMask, v4PlaneNormal.y, pISectData->m_ISectNormal.y );
					pISectData->m_ISectNormal.z = MaskedAssign( fl4CrossMask, v4PlaneNormal.z, pISectData->m_ISectNormal.z );
				}
			}
		}
		pISectData->m_LeftOverT = MaxSIMD( Four_Zeros, SubSIMD( Four_Ones, pISectData->m_ISectT ) );
	}
}

// Intersects 4 segments against the cached planes, or traces each one when there is no cache.
static void WorldIntersectT( FourVectors const *pStartPnt, FourVectors const *pEndPnt,
							 int nCollisionGroup, int nMask, ISectData_t *pISectData,
							 CWorldCollideContextData *pCtx )
{
	pISectData->m_ISectT = Four_Zeros;
	pISectData->m_LeftOverT = Four_Zeros;
	pISectData->m_ISectNormal.x = Four_Zeros;
	pISectData->m_ISectNormal.y = Four_Zeros;
	pISectData->m_ISectNormal.z = Four_Zeros;

	if ( pCtx )
	{
		pISectData->m_ISectT = Four_Twos;
		for( int i = 0; i < pCtx->m_nActivePlanes; i++ )
		{
			//fltx4 fl4StartDist = ( *pStartPnt - pCtx->m_PointOnPlane[i] ) * pCtx->m_PlaneNormal[i];
			FourVectors v4StartToPlane = *pStartPnt;
			v4StartToPlane -= pCtx->m_PointOnPlane[i];
			fltx4 fl4StartDist = v4StartToPlane * pCtx->m_PlaneNormal[i];

			//fltx4 fl4EndDist = ( *pEndPnt - pCtx->m_PointOnPlane[i] ) * pCtx->m_PlaneNormal[i];
			FourVectors v4EndToPlane = *pEndPnt;
			v4EndToPlane -= pCtx->m_PointOnPlane[i];
			fltx4 fl4EndDist = v4EndToPlane * pCtx->m_PlaneNormal[i];

			fltx4 fl4CrossMask = AndSIMD( CmpLtSIMD( fl4EndDist, Four_Zeros ), CmpGeSIMD( fl4StartDist, Four_Zeros ) );
			if ( IsAnyNegative( fl4CrossMask ) )
			{
				fltx4 fl4T = DivSIMD( fl4StartDist, SubSIMD( fl4StartDist, fl4EndDist ) );
				fl4CrossMask = AndSIMD( fl4CrossMask, CmpLtSIMD( fl4T, pISectData->m_ISectT ) );
				if ( IsAnyNegative( fl4CrossMask ) )
				{
					pISectData->m_ISectT = MaskedAssign( fl4CrossMask, fl4T, pISectData->m_ISectT );
					pISectData->m_ISectNormal.x = MaskedAssign( fl4CrossMask, pCtx->m_PlaneNormal[i].x, pISectData->m_ISectNormal.x );
					pISectData->m_ISectNormal.y = MaskedAssign( fl4CrossMask, pCtx->m_PlaneNormal[i].y, pISectData->m_ISectNormal.y );
					pISectData->m_ISectNormal.z = MaskedAssign( fl4CrossMask, pCtx->m_PlaneNormal[i].z, pISectData->m_ISectNormal.z );
				}
			}
		}
		pISectData->m_LeftOverT = MaxSIMD( Four_Zeros, SubSIMD( Four_Ones, pISectData->m_ISectT ) );
	}
	else
	{
		// assumes they don't start solid
		for( int i = 0; i < 4; i++ )
		{
			Vector start( pStartPnt->X( i ), pStartPnt->Y( i ), pStartPnt->Z( i ) );
			Vector end( pEndPnt->X( i ), pEndPnt->Y( i ), pEndPnt->Z( i ) );
			CBaseTrace tr;
			g_pParticleSystemMgr->Query()->TraceLine( start, end, nMask, NULL, nCollisionGroup, &tr );
			SubFloat( pISectData->m_ISectT, i ) = tr.fraction;
			if ( tr.startsolid )
			{
				SubFloat( pISectData->m_LeftOverT, i ) = 0;
			}
			else
			{
				SubFloat( pISectData->m_LeftOverT, i ) = 1.0f - tr.fraction;
			}
			SubFloat( pISectData->m_ISectNormal.x, i ) = tr.plane.normal.x;
			SubFloat( pISectData->m_ISectNormal.y, i ) = tr.plane.normal.y;
			SubFloat( pISectData->m_ISectNormal.z, i ) = tr.plane.normal.z;
		}
	}
}

bool C_OP_WorldTraceConstraint::EnforceConstraint( int nStartBlock,
												   int nNumBlocks,
												   CParticleCollection *pParticles,
												   void *pContext,
												   int nNumValidParticlesInLastChunk ) const
{
	if ( m_nCollisionMode == 3 )
	{
		if ( m_bKillonContact )
			return EnforceConstraintInternal<true, true>( nStartBlock, nNumBlocks, pParticles, pContext, nNumValidParticlesInLastChunk );
		else
			return EnforceConstraintInternal<false, true>( nStartBlock, nNumBlocks, pParticles, pContext, nNumValidParticlesInLastChunk );
	}
	else
	{
		if ( m_bKillonContact )
			return EnforceConstraintInternal<true, false>( nStartBlock, nNumBlocks, pParticles, pContext, nNumValidParticlesInLastChunk );
		else
			return EnforceConstraintInternal<false, false>( nStartBlock, nNumBlocks, pParticles, pContext, nNumValidParticlesInLastChunk );
	}
}

template<bool bKillonContact, bool bCached>
bool C_OP_WorldTraceConstraint::EnforceConstraintInternal( int nStartBlock,
														   int nNumBlocks,
														   CParticleCollection *pParticles,
														   void *pContext,
														   int nNumValidParticlesInLastChunk ) const
{
	C4VAttributeWriteIterator pPrevXYZ( PARTICLE_ATTRIBUTE_PREV_XYZ, pParticles );
	pPrevXYZ += nStartBlock;

	C4VAttributeWriteIterator pXYZ( PARTICLE_ATTRIBUTE_XYZ, pParticles );
	pXYZ += nStartBlock;

	CM128AttributeIterator pRadius( PARTICLE_ATTRIBUTE_RADIUS, pParticles );
	pRadius += nStartBlock;

	CM128AttributeWriteIterator pLifetime( PARTICLE_ATTRIBUTE_LIFE_DURATION, pParticles );
	pLifetime += nStartBlock;

	fltx4 bounceScale = ReplicateX4( m_flBounceAmount );

	fltx4 slideScale = ReplicateX4( m_flSlideAmount );

	bool bBouncingOrSliding = ( m_flBounceAmount != 0.0 ) || ( m_flSlideAmount != 0.0 );

	fltx4 radAdjustScale = ReplicateX4( m_flRadiusScale );

	bool bChangedSomething = false;

	int nMask = MASK_SOLID;
	if ( m_bBrushOnly )
	{
		nMask = MASK_SOLID_BRUSHONLY;
	}

	// children share the parent's collision cache
	CWorldCollideContextData **ppCtx;
	if ( pParticles->m_pParent )
		ppCtx = &( pParticles->m_pParent->m_pCollisionCacheData[m_nCollisionMode] );
	else
		ppCtx = &( pParticles->m_pCollisionCacheData[m_nCollisionMode] );

	CWorldCollideContextData *pCtx = NULL;
	if ( ( m_nCollisionMode == 1 ) || ( m_nCollisionMode == 3 ) || ( m_nCollisionMode == 2 ) )
	{
		if ( !*ppCtx )
		{
			*ppCtx = new CWorldCollideContextData;
			( *ppCtx )->m_nActivePlanes = 0;
			( *ppCtx )->m_flLastUpdateTime = -1.0;
		}
		pCtx = *ppCtx;
		if ( pCtx->m_flLastUpdateTime != pParticles->m_flCurTime )
		{
			pCtx->CalculatePlanes( pParticles, m_nCollisionMode, m_nCollisionGroupNumber, &m_vecCpOffset, m_flCpMovementTolerance );
			pCtx->m_flLastUpdateTime = pParticles->m_flCurTime;
		}
	}

	float flTol = m_flTraceTolerance * m_flTraceTolerance;

	do
	{
		// compute the end point of the trace: the particle's motion, extended by its radius
		FourVectors delta = *pXYZ;
		delta -= *pPrevXYZ;
		fltx4 fl4Radius = MulSIMD( *pRadius, radAdjustScale );
		fltx4 fl4Len2 = delta * delta;
		fltx4 bBadDeltas = CmpLeSIMD( fl4Len2, Four_Zeros );
		FourVectors deltanormalized = delta;
		deltanormalized *= AndNotSIMD( bBadDeltas, ReciprocalSqrtEstSIMD( fl4Len2 ) );
		FourVectors endPnt = deltanormalized;
		endPnt *= fl4Radius;
		endPnt += *pXYZ;

		ISectData_t iData;
		if ( bCached )
		{
			fltx4 fl4TailMask;
			if ( nNumBlocks > 1 )
			{
				fl4TailMask = LoadAlignedSIMD( g_SIMD_AllOnesMask );
			}
			else
			{
				fl4TailMask = LoadAlignedSIMD( g_SIMD_SkipTailMask[nNumValidParticlesInLastChunk] );
			}
			WorldIntersectTNew( pPrevXYZ, &endPnt, m_nCollisionGroupNumber, nMask, &iData, m_nCollisionMode, pCtx, fl4TailMask, flTol );
		}
		else
		{
			WorldIntersectT( pPrevXYZ, &endPnt, m_nCollisionGroupNumber, nMask, &iData, pCtx );
		}

		fltx4 fl4HitMask = CmpLtSIMD( iData.m_ISectT, Four_Ones );

		fl4HitMask = AndNotSIMD( bBadDeltas, fl4HitMask );

		if ( IsAnyNegative( fl4HitMask ) )
		{
			if ( bKillonContact )
			{
				// kill the particles that hit by zeroing their lifetime
				*pLifetime = MaskedAssign( fl4HitMask, Four_Zeros, *pLifetime );
				bChangedSomething = true;
			}
			else
			{
				// move to the intersection point
				FourVectors newPnt = delta;
				newPnt *= iData.m_ISectT;
				newPnt += *pPrevXYZ;
				bChangedSomething = true;
				if ( bBouncingOrSliding )
				{
					// reflect the velocity off the surface and apply the leftover time
					FourVectors newVel = VectorReflect( deltanormalized, iData.m_ISectNormal );
					newVel *= bounceScale;
					FourVectors bounceStep = newVel;
					bounceStep *= iData.m_LeftOverT;
					newPnt += bounceStep;

					// add in the component of the motion that slides along the surface
					FourVectors slideVel = delta;
					FourVectors normalPart = iData.m_ISectNormal;
					normalPart *= ( iData.m_ISectNormal * delta );
					slideVel -= normalPart;
					slideVel *= slideScale;
					newVel += slideVel;
					slideVel *= iData.m_LeftOverT;
					newPnt += slideVel;

					// set the previous position so that verlet integration gives the new velocity
					FourVectors newPrev = newPnt;
					newPrev -= newVel;
					pPrevXYZ->x = MaskedAssign( fl4HitMask, newPrev.x, pPrevXYZ->x );
					pPrevXYZ->y = MaskedAssign( fl4HitMask, newPrev.y, pPrevXYZ->y );
					pPrevXYZ->z = MaskedAssign( fl4HitMask, newPrev.z, pPrevXYZ->z );
				}
				pXYZ->x = MaskedAssign( fl4HitMask, newPnt.x, pXYZ->x );
				pXYZ->y = MaskedAssign( fl4HitMask, newPnt.y, pXYZ->y );
				pXYZ->z = MaskedAssign( fl4HitMask, newPnt.z, pXYZ->z );
			}
		}
		++pXYZ;
		++pPrevXYZ;
		++pRadius;
		++pLifetime;
	} while ( --nNumBlocks );
	return bChangedSomething;
}


//-----------------------------------------------------------------------------
// Constrains particles to an axis-aligned box
//-----------------------------------------------------------------------------
class C_OP_BoxConstraint : public CParticleOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_OP_BoxConstraint );

	uint32 GetWrittenAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_XYZ_MASK;
	}

	uint32 GetReadAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_XYZ_MASK;
	}

	bool EnforceConstraint( int nStartBlock,
							int nNumBlocks,
							CParticleCollection *pParticles,
							void *pContext,
							int nNumValidParticlesInLastChunk ) const;

	Vector m_vecMin;
	Vector m_vecMax;
};

DEFINE_PARTICLE_OPERATOR( C_OP_BoxConstraint, "Constrain particles to a box", OPERATOR_GENERIC );

BEGIN_PARTICLE_OPERATOR_UNPACK( C_OP_BoxConstraint )
	DMXELEMENT_UNPACK_FIELD( "min coords", "0 0 0", Vector, m_vecMin )
	DMXELEMENT_UNPACK_FIELD( "max coords", "0 0 0", Vector, m_vecMax )
END_PARTICLE_OPERATOR_UNPACK( C_OP_BoxConstraint )

bool C_OP_BoxConstraint::EnforceConstraint( int nStartBlock,
											int nNumBlocks,
											CParticleCollection *pParticles,
											void *pContext,
											int nNumValidParticlesInLastChunk ) const
{
	size_t nStride;
	FourVectors *pXYZ = pParticles->Get4VAttributePtrForWrite( PARTICLE_ATTRIBUTE_XYZ, &nStride );
	pXYZ += nStride * nStartBlock;

	FourVectors v4Min;
	v4Min.DuplicateVector( m_vecMin );
	FourVectors v4Max;
	v4Max.DuplicateVector( m_vecMax );

	fltx4 fl4AllSame = Four_NegativeOnes;					// all bits set
	do
	{
		FourVectors fvCurPos = *pXYZ;
		FourVectors fvClamped;
		fvClamped.x = MinSIMD( MaxSIMD( fvCurPos.x, v4Min.x ), v4Max.x );
		fvClamped.y = MinSIMD( MaxSIMD( fvCurPos.y, v4Min.y ), v4Max.y );
		fvClamped.z = MinSIMD( MaxSIMD( fvCurPos.z, v4Min.z ), v4Max.z );
		fl4AllSame = AndSIMD( fl4AllSame, CmpEqSIMD( fvCurPos.x, fvClamped.x ) );
		fl4AllSame = AndSIMD( fl4AllSame, CmpEqSIMD( fvCurPos.y, fvClamped.y ) );
		fl4AllSame = AndSIMD( fl4AllSame, CmpEqSIMD( fvCurPos.z, fvClamped.z ) );
		*pXYZ = fvClamped;
		pXYZ += nStride;
	} while ( --nNumBlocks );
	return ( TestSignSIMD( fl4AllSame ) != 0xf );
}


//-----------------------------------------------------------------------------
// Registration
//-----------------------------------------------------------------------------
void AddBuiltInParticleConstraints( void )
{
	REGISTER_PARTICLE_OPERATOR( FUNCTION_CONSTRAINT, C_OP_ConstrainDistance );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_CONSTRAINT, C_OP_PlanarConstraint );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_CONSTRAINT, C_OP_WorldCollideConstraint );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_CONSTRAINT, C_OP_WorldTraceConstraint );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_CONSTRAINT, C_OP_ConstrainDistanceToPath );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_CONSTRAINT, C_OP_BoxConstraint );
}
