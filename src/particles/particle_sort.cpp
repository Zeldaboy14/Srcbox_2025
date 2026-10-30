//===== Copyright (c) Valve Corporation, All rights reserved. ======//
//
// Purpose: builds the per-frame render lists for particle collections
//
// Reconstructed from the Alien Swarm SDK particles.lib (particle_sort.obj).
//
// Two kinds of list are produced:
//   * ParticleRenderData_t: sort key / index / radius / alpha, 16 bytes per
//     particle, written four particles at a time and transposed into AOS.
//   * The "extended" lists: SIMD views holding everything a renderer needs
//     (position, color, radius, animation, rotation, ...), plus an array of
//     pointers to the scalar view of each particle, which is what gets sorted.
//
// Both sort ascending by squared distance to the camera. The functions that
// hand out the lists return a pointer one past the last element, so callers
// walk them backwards (far to near).
//
//===========================================================================//

#include <algorithm>
#include "tier0/platform.h"
#include "particles/particles.h"
#include "materialsystem/imaterialsystem.h"
#include "tier0/vprof.h"
#include "mathlib/ssemath.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"


//-----------------------------------------------------------------------------
// Scratch storage shared by all collections (rendering is single threaded).
// Big enough for the largest SIMD view at the maximum particle count.
//-----------------------------------------------------------------------------
static ALIGN16 uint8 s_SortBuffer[ ( 1 + MAX_PARTICLES_IN_A_SYSTEM / 4 ) * sizeof( ParticleRenderDataWithOutlineInformation_SIMD_View ) ] ALIGN16_POST;
static ParticleFullRenderData_Scalar_View *s_pParticlePtrs[MAX_PARTICLES_IN_A_SYSTEM];


//-----------------------------------------------------------------------------
// Interpolating 4-vector iterator: remembers where the same attribute lives in
// the previous frame's attribute block
//-----------------------------------------------------------------------------
void C4VInterpolatedAttributeIterator::Init( int nAttribute, CParticleCollection *pParticles )
{
	m_pData = pParticles->Get4VAttributePtr( nAttribute, &m_nStride );
	Assert( pParticles->GetPrevAttributeMemory() );
	if ( m_nStride )
	{
		m_nOldDataOffset =
			pParticles->m_PreviousFrameAttributes.ByteAddress( nAttribute ) - pParticles->m_ParticleAttributes.ByteAddress( nAttribute );
	}
	else
	{
		// constant attribute, there is no per-particle previous value
		m_nOldDataOffset = 0;
	}
}


//-----------------------------------------------------------------------------
// Fills ParticleRenderData_t entries, four particles per pass.
//
// Alpha and index are stored as integers by adding 2^23 to a float: the
// integer then sits in the low mantissa bits. When culling, particles behind
// the camera plane get an alpha of 0.
//-----------------------------------------------------------------------------
template<bool bSort, bool bCull>
void s_GenerateData( void *pOutData, Vector CameraPos, Vector *pCameraFwd,
					 CParticleVisibilityData *pVisibilityData, CParticleCollection *pParticles )
{
	fltx4 *pOutUnSorted = reinterpret_cast<fltx4 *>( pOutData );

	C4VAttributeIterator pXYZ( PARTICLE_ATTRIBUTE_XYZ, pParticles );
	CM128AttributeIterator pAlpha( PARTICLE_ATTRIBUTE_ALPHA, pParticles );
	CM128AttributeIterator pAlpha2( PARTICLE_ATTRIBUTE_ALPHA2, pParticles );
	CM128AttributeIterator pRadius( PARTICLE_ATTRIBUTE_RADIUS, pParticles );

	int nParticles = pParticles->m_nActiveParticles;

	FourVectors EyePos;
	EyePos.DuplicateVector( CameraPos );

	FourVectors v4Fwd;
	if ( bCull )
	{
		v4Fwd.DuplicateVector( *pCameraFwd );
	}

	fltx4 fl4AlphaVis = ReplicateX4( pVisibilityData->m_flAlphaVisibility );
	fltx4 fl4RadVis = ReplicateX4( pVisibilityData->m_flRadiusVisibility );

	// 2^23 + { 0, 1, 2, 3 }: integer particle indices in the low mantissa bits
	fltx4 fl4OutIdx = AddSIMD( Four_2ToThe23s, g_SIMD_0123 );

	bool bUseVis = pVisibilityData->m_bUseVisibility;

	fltx4 fl4AlphaScale = ReplicateX4( 255.0f );

	// The key is only meaningful (and only read) when sorting
	fltx4 fl4SortKey = Four_Zeros;

	do
	{
		fltx4 fl4FinalAlpha = MulSIMD( *pAlpha, *pAlpha2 );
		fltx4 fl4FinalRadius = *pRadius;
		if ( bUseVis )
		{
			fl4FinalAlpha = MaxSIMD( Four_Zeros, MinSIMD( Four_Ones, MulSIMD( fl4FinalAlpha, fl4AlphaVis ) ) );
			fl4FinalRadius = MulSIMD( fl4FinalRadius, fl4RadVis );
		}

		// 8-bit alpha in the low bits
		fl4FinalAlpha = AddSIMD( MulSIMD( fl4FinalAlpha, fl4AlphaScale ), Four_2ToThe23s );

		if ( bSort || bCull )
		{
			FourVectors v4Delta = *pXYZ;
			v4Delta -= EyePos;

			if ( bSort )
			{
				fl4SortKey = v4Delta.length2();
			}

			if ( bCull )
			{
				// hide everything behind the camera
				fltx4 fl4Dot = v4Delta * v4Fwd;
				fl4FinalAlpha = AndSIMD( fl4FinalAlpha, CmpLeSIMD( Four_Zeros, fl4Dot ) );
			}
		}

		// Write sortkey/index/radius/alpha as four rows, then transpose to one
		// ParticleRenderData_t per particle
		pOutUnSorted[0] = fl4SortKey;
		pOutUnSorted[1] = AndSIMD( fl4OutIdx, LoadAlignedSIMD( (float *) g_SIMD_Low16BitsMask ) );
		pOutUnSorted[2] = fl4FinalRadius;
		pOutUnSorted[3] = fl4FinalAlpha;
		TransposeSIMD( pOutUnSorted[0], pOutUnSorted[1], pOutUnSorted[2], pOutUnSorted[3] );

		pOutUnSorted += 4;
		fl4OutIdx = AddSIMD( fl4OutIdx, Four_Fours );

		++pXYZ;
		++pAlpha;
		++pAlpha2;
		++pRadius;
	} while( ( nParticles -= 4 ) > 0 );
}


//-----------------------------------------------------------------------------
// Sort keys are non-negative floats, so comparing their bits as ints works
//-----------------------------------------------------------------------------
bool SortLessFunc( const ParticleRenderData_t &left, const ParticleRenderData_t &right )
{
	return *( (int32 const *) &left.m_flSortKey ) < *( (int32 const *) &right.m_flSortKey );
}


int CParticleCollection::GenerateSortedIndexList( ParticleRenderData_t *pOut, Vector vecCamera, CParticleVisibilityData *pVisibilityData, bool bSorted )
{
	VPROF_BUDGET( "CParticleCollection::GenerateSortedIndexList", VPROF_BUDGETGROUP_PARTICLE_RENDERING );

	int nParticles = m_nActiveParticles;
	if ( bSorted )
	{
		s_GenerateData<true, false>( pOut, vecCamera, NULL, pVisibilityData, this );
	}
	else
	{
		s_GenerateData<false, false>( pOut, vecCamera, NULL, pVisibilityData, this );
	}

	if ( bSorted )
	{
		std::make_heap( pOut, pOut + nParticles, SortLessFunc );
		std::sort_heap( pOut, pOut + nParticles, SortLessFunc );
	}
	return nParticles;
}


int CParticleCollection::GenerateCulledSortedIndexList( ParticleRenderData_t *pOut, Vector vecCamera, Vector vecFwd, CParticleVisibilityData *pVisibilityData, bool bSorted )
{
	// (the shipped build reuses the unculled function's profiling scope name)
	VPROF_BUDGET( "CParticleCollection::GenerateSortedIndexList", VPROF_BUDGETGROUP_PARTICLE_RENDERING );

	int nParticles = m_nActiveParticles;
	if ( bSorted )
	{
		s_GenerateData<true, true>( pOut, vecCamera, &vecFwd, pVisibilityData, this );
	}
	else
	{
		s_GenerateData<false, true>( pOut, vecCamera, &vecFwd, pVisibilityData, this );
	}

	if ( bSorted )
	{
		std::make_heap( pOut, pOut + nParticles, SortLessFunc );
		std::sort_heap( pOut, pOut + nParticles, SortLessFunc );
	}
	return nParticles;
}


//-----------------------------------------------------------------------------
// Returns the end of the render list (walk it backwards for far-to-near)
//-----------------------------------------------------------------------------
const ParticleRenderData_t *CParticleCollection::GetRenderList( IMatRenderContext *pRenderContext, bool bSorted, int *pNparticles, CParticleVisibilityData *pVisibilityData )
{
	if ( bSorted )
	{
		bSorted = m_pDef->m_bShouldSort;
	}

	Vector vecCamera;
	pRenderContext->GetWorldSpaceCameraPosition( &vecCamera );

	ParticleRenderData_t *pOut = reinterpret_cast<ParticleRenderData_t *>( s_SortBuffer );
	int nParticles;

	// Particles can only be behind the camera when the camera is inside the bounds
	if ( ( vecCamera.x >= m_MinBounds.x ) && ( vecCamera.x <= m_MaxBounds.x ) &&
		 ( vecCamera.y >= m_MinBounds.y ) && ( vecCamera.y <= m_MaxBounds.y ) &&
		 ( vecCamera.z >= m_MinBounds.z ) && ( vecCamera.z <= m_MaxBounds.z ) )
	{
		Vector vecFwd, vecRight, vecUp;
		pRenderContext->GetWorldSpaceCameraVectors( &vecFwd, &vecRight, &vecUp );

		nParticles = GenerateCulledSortedIndexList( pOut, vecCamera, vecFwd, pVisibilityData, bSorted );
	}
	else
	{
		nParticles = GenerateSortedIndexList( pOut, vecCamera, pVisibilityData, bSorted );
	}

	*pNparticles = nParticles;
	return pOut + nParticles;
}


//-----------------------------------------------------------------------------
// Fills the extended SIMD views, four particles per pass.
//
//   bSort              - write the squared camera distance as the sort key
//   bCull              - zero alpha for particles behind the camera plane
//   bLerpCoords        - interpolate between the previous and current frame
//                        (also hides particles born after the previous frame)
//   OUTPUTCLASS        - which SIMD view to fill
//   bDoPerParticleGlow - fill the glow color/alpha (outline view)
//   bDoNormals         - fill the normal (normal view)
//
// pIndexBuffer receives a pointer to each particle's scalar view.
//-----------------------------------------------------------------------------
template<bool bSort, bool bCull, bool bLerpCoords, class OUTPUTCLASS, bool bDoPerParticleGlow, bool bDoNormals,
		 class C4VAttributeIteratorClass, class CM128AttributeIteratorClass>
void GenerateExtendedData( void *pOutbuf, ParticleFullRenderData_Scalar_View **pIndexBuffer,
						   Vector CameraPos, Vector *pCameraFwd,
						   CParticleVisibilityData *pVisibilityData, CParticleCollection *pParticles,
						   float flInterpT )
{
	OUTPUTCLASS *pOutUnSorted = reinterpret_cast<OUTPUTCLASS *>( pOutbuf );

	C4VAttributeIteratorClass pXYZ( PARTICLE_ATTRIBUTE_XYZ, pParticles );
	C4VAttributeIteratorClass pRGB( PARTICLE_ATTRIBUTE_TINT_RGB, pParticles );

	CM128AttributeIteratorClass pAlpha( PARTICLE_ATTRIBUTE_ALPHA, pParticles );
	CM128AttributeIteratorClass pAlpha2( PARTICLE_ATTRIBUTE_ALPHA2, pParticles );
	CM128AttributeIteratorClass pRadius( PARTICLE_ATTRIBUTE_RADIUS, pParticles );
	CM128AttributeIteratorClass pRot( PARTICLE_ATTRIBUTE_ROTATION, pParticles );
	CM128AttributeIteratorClass pYaw( PARTICLE_ATTRIBUTE_YAW, pParticles );

	// never interpolated
	CM128AttributeIterator pSeq( PARTICLE_ATTRIBUTE_SEQUENCE_NUMBER, pParticles );
	CM128AttributeIterator pSeq1( PARTICLE_ATTRIBUTE_SEQUENCE_NUMBER1, pParticles );
	CM128AttributeIterator pCreationTimeStamp( PARTICLE_ATTRIBUTE_CREATION_TIME, pParticles );

	C4VAttributeIteratorClass pRGB2;
	CM128AttributeIteratorClass pGlowAlpha;
	if ( bDoPerParticleGlow )
	{
		pRGB2.Init( PARTICLE_ATTRIBUTE_GLOW_RGB, pParticles );
		pGlowAlpha.Init( PARTICLE_ATTRIBUTE_GLOW_ALPHA, pParticles );
	}

	C4VAttributeIterator pNormal;
	if ( bDoNormals )
	{
		pNormal.Init( PARTICLE_ATTRIBUTE_NORMAL, pParticles );
	}

	fltx4 fl4T = ReplicateX4( flInterpT );

	fltx4 fl4CurTime;
	fltx4 fl4MaximumCreationTimeToDraw;
	if ( bLerpCoords )
	{
		fl4MaximumCreationTimeToDraw = ReplicateX4( pParticles->m_flPrevSimTime );
		fl4CurTime = ReplicateX4( pParticles->m_flTargetDrawTime );
	}
	else
	{
		fl4CurTime = pParticles->m_fl4CurTime;
	}

	fltx4 Four_256s = ReplicateX4( 256.0f );

	int nParticles = pParticles->m_nActiveParticles;

	FourVectors EyePos;
	FourVectors v4Fwd;
	if ( bSort || bCull )
	{
		EyePos.DuplicateVector( CameraPos );
	}
	if ( bCull )
	{
		v4Fwd.DuplicateVector( *pCameraFwd );
	}

	fltx4 fl4AlphaVis = ReplicateX4( pVisibilityData->m_flAlphaVisibility );
	fltx4 fl4RadVis = ReplicateX4( pVisibilityData->m_flRadiusVisibility );

	bool bUseVis = pVisibilityData->m_bUseVisibility;

	fltx4 fl4AlphaScale = ReplicateX4( 255.0f );

	do
	{
		// lane i of the SIMD view is the scalar view at byte offset 4*i
		pIndexBuffer[0] = reinterpret_cast<ParticleFullRenderData_Scalar_View *>( ( (uint8 *) pOutUnSorted ) + 0 );
		pIndexBuffer[1] = reinterpret_cast<ParticleFullRenderData_Scalar_View *>( ( (uint8 *) pOutUnSorted ) + 4 );
		pIndexBuffer[2] = reinterpret_cast<ParticleFullRenderData_Scalar_View *>( ( (uint8 *) pOutUnSorted ) + 8 );
		pIndexBuffer[3] = reinterpret_cast<ParticleFullRenderData_Scalar_View *>( ( (uint8 *) pOutUnSorted ) + 12 );

		fltx4 fl4FinalAlpha = MulSIMD( pAlpha( fl4T ), pAlpha2( fl4T ) );
		fltx4 fl4FinalRadius = pRadius( fl4T );
		if ( bUseVis )
		{
			fl4FinalAlpha = MaxSIMD( Four_Zeros, MinSIMD( Four_Ones, MulSIMD( fl4FinalAlpha, fl4AlphaVis ) ) );
			fl4FinalRadius = MulSIMD( fl4FinalRadius, fl4RadVis );
		}

		FourVectors v4Pos;
		v4Pos.x = pXYZ.X( fl4T );
		v4Pos.y = pXYZ.Y( fl4T );
		v4Pos.z = pXYZ.Z( fl4T );
		pOutUnSorted->m_fl4XYZ = v4Pos;

		fltx4 fl4AlphaInt = AddSIMD( MulSIMD( fl4FinalAlpha, fl4AlphaScale ), Four_2ToThe23s );
		if ( bLerpCoords )
		{
			// particles created after the previous simulation step have no
			// previous position to interpolate from; hide them
			fl4AlphaInt = AndSIMD( fl4AlphaInt, CmpLtSIMD( *pCreationTimeStamp, fl4MaximumCreationTimeToDraw ) );
		}

		if ( bSort || bCull )
		{
			FourVectors v4Delta = v4Pos;
			v4Delta -= EyePos;
			if ( bSort )
			{
				pOutUnSorted->m_fl4SortKey = v4Delta.length2();
			}
			if ( bCull )
			{
				fl4AlphaInt = AndSIMD( fl4AlphaInt, CmpLeSIMD( Four_Zeros, v4Delta * v4Fwd ) );
			}
		}
		pOutUnSorted->m_fl4Alpha = fl4AlphaInt;

		pOutUnSorted->m_fl4Red = AddSIMD( MulSIMD( pRGB.X( fl4T ), fl4AlphaScale ), Four_2ToThe23s );
		pOutUnSorted->m_fl4Green = AddSIMD( MulSIMD( pRGB.Y( fl4T ), fl4AlphaScale ), Four_2ToThe23s );
		pOutUnSorted->m_fl4Blue = AddSIMD( MulSIMD( pRGB.Z( fl4T ), fl4AlphaScale ), Four_2ToThe23s );

		pOutUnSorted->m_fl4Radius = fl4FinalRadius;
		pOutUnSorted->m_fl4AnimationTimeValue = SubSIMD( fl4CurTime, *pCreationTimeStamp );
		pOutUnSorted->m_fl4Rotation = pRot( fl4T );
		pOutUnSorted->m_fl4Yaw = pYaw( fl4T );

		// sequence numbers packed into the low two bytes
		fltx4 fl4Seq;
		if ( pSeq1.Stride() )
		{
			fl4Seq = AddSIMD( *pSeq, MulSIMD( Four_256s, *pSeq1 ) );
		}
		else
		{
			fl4Seq = *pSeq;
		}
		pOutUnSorted->m_fl4SequenceID = AddSIMD( fl4Seq, Four_2ToThe23s );

		if ( bDoPerParticleGlow )
		{
			pOutUnSorted->SetARGB2( pRGB2.X( fl4T ), pRGB2.Y( fl4T ), pRGB2.Z( fl4T ), pGlowAlpha( fl4T ) );
			++pRGB2;
			++pGlowAlpha;
		}

		if ( bDoNormals )
		{
			pOutUnSorted->SetNormal( pNormal->x, pNormal->y, pNormal->z );
			++pNormal;
		}

		nParticles -= 4;

		++pXYZ;
		++pAlpha;
		++pAlpha2;
		++pRadius;
		++pRGB;
		++pYaw;
		++pRot;
		++pSeq;
		++pSeq1;
		++pCreationTimeStamp;

		++pOutUnSorted;
		pIndexBuffer += 4;
	} while( nParticles > 0 );
}


//-----------------------------------------------------------------------------
// Picks the iterator types for interpolated vs. current-frame data
//-----------------------------------------------------------------------------
template<bool bSort, bool bCull, bool bLerpCoords, class OUTPUTCLASS, bool bDoPerParticleGlow, bool bDoNormals>
void GenerateExtendedData( void *pOutbuf, ParticleFullRenderData_Scalar_View **pIndexBuffer,
						   Vector CameraPos, Vector *pCameraFwd,
						   CParticleVisibilityData *pVisibilityData, CParticleCollection *pParticles,
						   float flInterpT )
{
	if ( bLerpCoords )
	{
		GenerateExtendedData<bSort, bCull, bLerpCoords, OUTPUTCLASS, bDoPerParticleGlow, bDoNormals,
			C4VInterpolatedAttributeIterator, CM128InterpolatedAttributeIterator>(
				pOutbuf, pIndexBuffer, CameraPos, pCameraFwd, pVisibilityData, pParticles, flInterpT );
	}
	else
	{
		GenerateExtendedData<bSort, bCull, bLerpCoords, OUTPUTCLASS, bDoPerParticleGlow, bDoNormals,
			C4VAttributeIterator, CM128AttributeIterator>(
				pOutbuf, pIndexBuffer, CameraPos, pCameraFwd, pVisibilityData, pParticles, flInterpT );
	}
}


//-----------------------------------------------------------------------------
// Turns the runtime sort flag into a template argument
//-----------------------------------------------------------------------------
template<bool bCull, bool bLerpCoords, class OUTPUTCLASS, bool bDoPerParticleGlow, bool bDoNormals>
void s_GenerateExtendedData( void *pOutbuf, ParticleFullRenderData_Scalar_View **pIndexBuffer,
							 Vector CameraPos, Vector *pCameraFwd,
							 CParticleVisibilityData *pVisibilityData, CParticleCollection *pParticles,
							 float flInterpT, bool bSort )
{
	if ( bSort )
	{
		GenerateExtendedData<true, bCull, bLerpCoords, OUTPUTCLASS, bDoPerParticleGlow, bDoNormals>(
			pOutbuf, pIndexBuffer, CameraPos, pCameraFwd, pVisibilityData, pParticles, flInterpT );
	}
	else
	{
		GenerateExtendedData<false, bCull, bLerpCoords, OUTPUTCLASS, bDoPerParticleGlow, bDoNormals>(
			pOutbuf, pIndexBuffer, CameraPos, pCameraFwd, pVisibilityData, pParticles, flInterpT );
	}
}


bool SortLessFuncExtended( ParticleFullRenderData_Scalar_View * const &left, const ParticleFullRenderData_Scalar_View * const &right )
{
	return left->m_nSortKey < right->m_nSortKey;
}


//-----------------------------------------------------------------------------
// Fraction of the way from the previous simulation step to the current one
// at which the collection is being drawn
//-----------------------------------------------------------------------------
static FORCEINLINE float InterpolationFraction( CParticleCollection *pParticles )
{
	return ( pParticles->m_flTargetDrawTime - pParticles->m_flPrevSimTime ) /
		( pParticles->m_flCurTime - pParticles->m_flPrevSimTime );
}


// Variants of GenerateExtendedSortedIndexList for the larger views
int GenerateExtendedSortedIndexListWithPerParticleGlow( Vector vecCamera, Vector *pCameraFwd, CParticleVisibilityData *pVisibilityData,
														CParticleCollection *pParticles, bool bSorted, void *pOutBuf,
														ParticleRenderDataWithOutlineInformation_Scalar_View **pParticlePtrs );
int GenerateExtendedSortedIndexListWithNormals( Vector vecCamera, Vector *pCameraFwd, CParticleVisibilityData *pVisibilityData,
												CParticleCollection *pParticles, bool bSorted, void *pOutBuf,
												ParticleRenderDataWithNormal_Scalar_View **pParticlePtrs );


//-----------------------------------------------------------------------------
// Builds an extended list; returns the particle count
//-----------------------------------------------------------------------------
int GenerateExtendedSortedIndexList( Vector vecCamera, Vector *pCameraFwd, CParticleVisibilityData *pVisibilityData,
									 CParticleCollection *pParticles, bool bSorted, void *pOutBuf,
									 ParticleFullRenderData_Scalar_View **pParticlePtrs )
{
	// (the camera direction is not passed down; extended lists are never culled)
	if ( pParticles->IsUsingInterpolatedRendering() )
	{
		s_GenerateExtendedData<false, true, ParticleFullRenderData_SIMD_View, false, false>(
			pOutBuf, pParticlePtrs, vecCamera, NULL, pVisibilityData, pParticles, InterpolationFraction( pParticles ), bSorted );
	}
	else
	{
		s_GenerateExtendedData<false, false, ParticleFullRenderData_SIMD_View, false, false>(
			pOutBuf, pParticlePtrs, vecCamera, NULL, pVisibilityData, pParticles, 0.0f, bSorted );
	}

	int nParticles = pParticles->m_nActiveParticles;
	if ( bSorted )
	{
		std::make_heap( pParticlePtrs, pParticlePtrs + nParticles, SortLessFuncExtended );
		std::sort_heap( pParticlePtrs, pParticlePtrs + nParticles, SortLessFuncExtended );
	}
	return nParticles;
}

int GenerateExtendedSortedIndexListWithPerParticleGlow( Vector vecCamera, Vector *pCameraFwd, CParticleVisibilityData *pVisibilityData,
														CParticleCollection *pParticles, bool bSorted, void *pOutBuf,
														ParticleRenderDataWithOutlineInformation_Scalar_View **pParticlePtrs )
{
	if ( pParticles->IsUsingInterpolatedRendering() )
	{
		s_GenerateExtendedData<false, true, ParticleRenderDataWithOutlineInformation_SIMD_View, true, false>(
			pOutBuf, (ParticleFullRenderData_Scalar_View **) pParticlePtrs, vecCamera, NULL, pVisibilityData, pParticles,
			InterpolationFraction( pParticles ), bSorted );
	}
	else
	{
		s_GenerateExtendedData<false, false, ParticleRenderDataWithOutlineInformation_SIMD_View, true, false>(
			pOutBuf, (ParticleFullRenderData_Scalar_View **) pParticlePtrs, vecCamera, NULL, pVisibilityData, pParticles,
			0.0f, bSorted );
	}

	int nParticles = pParticles->m_nActiveParticles;
	if ( bSorted )
	{
		std::make_heap( pParticlePtrs, pParticlePtrs + nParticles, SortLessFuncExtended );
		std::sort_heap( pParticlePtrs, pParticlePtrs + nParticles, SortLessFuncExtended );
	}
	return nParticles;
}

int GenerateExtendedSortedIndexListWithNormals( Vector vecCamera, Vector *pCameraFwd, CParticleVisibilityData *pVisibilityData,
												CParticleCollection *pParticles, bool bSorted, void *pOutBuf,
												ParticleRenderDataWithNormal_Scalar_View **pParticlePtrs )
{
	if ( pParticles->IsUsingInterpolatedRendering() )
	{
		s_GenerateExtendedData<false, true, ParticleRenderDataWithNormal_SIMD_View, false, true>(
			pOutBuf, (ParticleFullRenderData_Scalar_View **) pParticlePtrs, vecCamera, NULL, pVisibilityData, pParticles,
			InterpolationFraction( pParticles ), bSorted );
	}
	else
	{
		s_GenerateExtendedData<false, false, ParticleRenderDataWithNormal_SIMD_View, false, true>(
			pOutBuf, (ParticleFullRenderData_Scalar_View **) pParticlePtrs, vecCamera, NULL, pVisibilityData, pParticles,
			0.0f, bSorted );
	}

	int nParticles = pParticles->m_nActiveParticles;
	if ( bSorted )
	{
		std::make_heap( pParticlePtrs, pParticlePtrs + nParticles, SortLessFuncExtended );
		std::sort_heap( pParticlePtrs, pParticlePtrs + nParticles, SortLessFuncExtended );
	}
	return nParticles;
}


//-----------------------------------------------------------------------------
// Extended render lists; each returns the end of its pointer list
//-----------------------------------------------------------------------------
ParticleFullRenderData_Scalar_View **GetExtendedRenderList( CParticleCollection *pParticles,
															IMatRenderContext *pRenderContext,
															bool bSorted, int *pNparticles,
															CParticleVisibilityData *pVisibilityData )
{
	if ( bSorted )
	{
		bSorted = pParticles->m_pDef->m_bShouldSort;
	}

	Vector vecCamera;
	pRenderContext->GetWorldSpaceCameraPosition( &vecCamera );
	int nParticles = GenerateExtendedSortedIndexList( vecCamera, NULL, pVisibilityData, pParticles, bSorted, s_SortBuffer, s_pParticlePtrs );
	*pNparticles = nParticles;
	return s_pParticlePtrs + nParticles;
}

ParticleRenderDataWithOutlineInformation_Scalar_View **GetExtendedRenderListWithPerParticleGlow(
	CParticleCollection *pParticles,
	IMatRenderContext *pRenderContext,
	bool bSorted, int *pNparticles,
	CParticleVisibilityData *pVisibilityData )
{
	if ( bSorted )
	{
		bSorted = pParticles->m_pDef->m_bShouldSort;
	}

	Vector vecCamera;
	pRenderContext->GetWorldSpaceCameraPosition( &vecCamera );
	ParticleRenderDataWithOutlineInformation_Scalar_View **pPtrs = (ParticleRenderDataWithOutlineInformation_Scalar_View **) s_pParticlePtrs;
	int nParticles = GenerateExtendedSortedIndexListWithPerParticleGlow( vecCamera, NULL, pVisibilityData, pParticles, bSorted, s_SortBuffer, pPtrs );
	*pNparticles = nParticles;
	return pPtrs + nParticles;
}

ParticleRenderDataWithNormal_Scalar_View **GetExtendedRenderListWithNormals(
	CParticleCollection *pParticles,
	IMatRenderContext *pRenderContext,
	bool bSorted, int *pNparticles,
	CParticleVisibilityData *pVisibilityData )
{
	if ( bSorted )
	{
		bSorted = pParticles->m_pDef->m_bShouldSort;
	}

	Vector vecCamera;
	pRenderContext->GetWorldSpaceCameraPosition( &vecCamera );
	ParticleRenderDataWithNormal_Scalar_View **pPtrs = (ParticleRenderDataWithNormal_Scalar_View **) s_pParticlePtrs;
	int nParticles = GenerateExtendedSortedIndexListWithNormals( vecCamera, NULL, pVisibilityData, pParticles, bSorted, s_SortBuffer, pPtrs );
	*pNparticles = nParticles;
	return pPtrs + nParticles;
}
