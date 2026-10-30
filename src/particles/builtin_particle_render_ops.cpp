//===== Copyright (c) Valve Corporation, All rights reserved. ======//
//
// Purpose: particle system renderers
//
// Reconstructed from the Alien Swarm SDK particles.lib (builtin_particle_render_ops.obj).
//
// NOTE: the shipped object was built with /fp:fast; where the compiler re-associated floating point
// sums the expressions below are written out in the order the shipped code evaluates them.
//
//===========================================================================//

#include "tier0/platform.h"
#include "particles/particles.h"
#include "filesystem.h"
#include "tier2/tier2.h"
#include "tier2/fileutils.h"
#include "tier2/beamsegdraw.h"
#include "tier1/UtlStringMap.h"
#include "tier1/strtools.h"
#include "materialsystem/imesh.h"
#include "materialsystem/imaterial.h"
#include "materialsystem/imaterialvar.h"
#include "materialsystem/imaterialsystem.h"
#include "mathlib/ssemath.h"
#include "mathlib/vmatrix.h"
#include "particles_internal.h"
#include "bitmap/psheet.h"

#ifdef PORTAL
#include "blobulator/implicit/imptiler.h"
#include "blobulator/implicit/sweeprenderer.h"
#include "blobulator/implicit/userfunctions.h"
#endif

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"


//-----------------------------------------------------------------------------
// Utility method to compute the max # of particles per batch
//-----------------------------------------------------------------------------
static inline int GetMaxParticlesPerBatch( IMatRenderContext *pRenderContext, IMaterial *pMaterial, bool bWithInstancing )
{
	int nMaxVertices = pRenderContext->GetMaxVerticesToRender( pMaterial );
	int nMaxIndices = pRenderContext->GetMaxIndicesToRender();

	if ( bWithInstancing )
		return nMaxVertices;
	else
		return MIN( (nMaxVertices / 4), (nMaxIndices / 6) );
}

void SetupParticleVisibility( CParticleCollection *pParticles, CParticleVisibilityData *pVisibilityData, const CParticleVisibilityInputs *pVisibilityInputs, int *nQueryHandle, IMatRenderContext *pRenderContext )
{
	float flVisibility = 1.0f;

	if ( pVisibilityInputs->m_nCPin >= 0 )
	{
		Vector vecOrigin = pParticles->GetControlPointAtCurrentTime( pVisibilityInputs->m_nCPin );

		// Pixel visibility of the proxy
		if ( pVisibilityInputs->m_flInputMin != pVisibilityInputs->m_flInputMax )
		{
			float flScale = pVisibilityInputs->m_flProxyRadius;
			flVisibility = g_pParticleSystemMgr->Query()->GetPixelVisibility( nQueryHandle, vecOrigin, flScale );
			// NOTE: shipped code remaps the proxy radius rather than the queried visibility
			flVisibility = RemapValClamped( flScale, pVisibilityInputs->m_flInputMin, pVisibilityInputs->m_flInputMax, 0.0f, 1.0f ) * flVisibility;
		}

		// Angle between the view direction and the control point's forward vector
		if ( pVisibilityInputs->m_flDotInputMin != pVisibilityInputs->m_flDotInputMax )
		{
			CParticleSIMDTransformation pXForm1;
			pParticles->GetControlPointTransformAtTime( pVisibilityInputs->m_nCPin, pParticles->m_flCurTime, &pXForm1 );
			Vector vecInput1( SubFloat( pXForm1.m_v4Fwd.x, 0 ), SubFloat( pXForm1.m_v4Fwd.y, 0 ), SubFloat( pXForm1.m_v4Fwd.z, 0 ) );
			Vector vecInput2( SubFloat( pXForm1.m_v4Origin.x, 0 ), SubFloat( pXForm1.m_v4Origin.y, 0 ), SubFloat( pXForm1.m_v4Origin.z, 0 ) );
			Vector vecDelta = vecInput2 - g_pParticleSystemMgr->Query()->GetCurrentViewOrigin();
			VectorNormalize( vecDelta );
			float flDot = ( vecDelta.z * vecInput1.z + vecDelta.y * vecInput1.y ) + vecDelta.x * vecInput1.x;
			flVisibility = RemapValClamped( flDot, pVisibilityInputs->m_flDotInputMin, pVisibilityInputs->m_flDotInputMax, 0.0f, 1.0f ) * flVisibility;
		}

		// Distance to the camera
		if ( pVisibilityInputs->m_flDistanceInputMin != pVisibilityInputs->m_flDistanceInputMax )
		{
			Vector vecCameraPos;
			if ( pParticles->m_pDef->IsScreenSpaceEffect() )
			{
				pRenderContext->MatrixMode( MATERIAL_VIEW );
				pRenderContext->PopMatrix();
				pRenderContext->MatrixMode( MATERIAL_PROJECTION );
				pRenderContext->PopMatrix();
				pRenderContext->GetWorldSpaceCameraPosition( &vecCameraPos );
				pRenderContext->MatrixMode( MATERIAL_VIEW );
				pRenderContext->PushMatrix();
				pRenderContext->LoadIdentity();
				pRenderContext->MatrixMode( MATERIAL_PROJECTION );
				pRenderContext->PushMatrix();
				pRenderContext->LoadIdentity();
				pRenderContext->Ortho( -100, -100, 100, 100, -100, 100 );
			}
			else
			{
				pRenderContext->GetWorldSpaceCameraPosition( &vecCameraPos );
			}

			float dx = vecOrigin.x - vecCameraPos.x;
			float dy = vecOrigin.y - vecCameraPos.y;
			float dz = vecOrigin.z - vecCameraPos.z;
			float flDistance = sqrtf( ( dz * dz + dy * dy ) + dx * dx );
			flVisibility = RemapValClamped( flDistance, pVisibilityInputs->m_flDistanceInputMin, pVisibilityInputs->m_flDistanceInputMax, 0.0f, 1.0f ) * flVisibility;
		}
	}

	pVisibilityData->m_flAlphaVisibility = ( pVisibilityInputs->m_flAlphaScaleMax - pVisibilityInputs->m_flAlphaScaleMin ) * flVisibility + pVisibilityInputs->m_flAlphaScaleMin;
	pVisibilityData->m_flRadiusVisibility = ( pVisibilityInputs->m_flRadiusScaleMax - pVisibilityInputs->m_flRadiusScaleMin ) * flVisibility + pVisibilityInputs->m_flRadiusScaleMin;

	// Scale radius by the field of view so that particles keep their screen size
	if ( pVisibilityInputs->m_flRadiusScaleFOVBase != 0.0f )
	{
		matrix3x4_t projMatrix;
		pRenderContext->GetMatrix( MATERIAL_PROJECTION, &projMatrix );
		float flHalfFOV = pVisibilityInputs->m_flRadiusScaleFOVBase * 0.00872664526f;	// half the FOV, in radians
		float flTan = (float)tan( (double)flHalfFOV );
		pVisibilityData->m_flRadiusVisibility = ( ( 1.0f / flTan ) / projMatrix[0][0] ) * pVisibilityData->m_flRadiusVisibility;
	}
}


//-----------------------------------------------------------------------------
//
// Render points
//
//-----------------------------------------------------------------------------
class C_OP_RenderPoints : public CParticleRenderOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_OP_RenderPoints );

	struct C_OP_RenderPointsContext_t
	{
		CParticleVisibilityData m_VisibilityData;
		int m_nQueryHandle;
	};

	uint32 GetWrittenAttributes( void ) const
	{
		return 0;
	}

	uint32 GetReadAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_XYZ_MASK;
	}

	virtual uint64 GetReadControlPointMask() const
	{
		if ( VisibilityInputs.m_nCPin >= 0 )
			return 1ULL << VisibilityInputs.m_nCPin;
		return 0;
	}

	size_t GetRequiredContextBytes( ) const
	{
		return sizeof( C_OP_RenderPointsContext_t );
	}

	virtual void InitializeContextData( CParticleCollection *pParticles, void *pContext ) const
	{
		C_OP_RenderPointsContext_t *pCtx = reinterpret_cast<C_OP_RenderPointsContext_t *>( pContext );
		pCtx->m_VisibilityData.m_bUseVisibility = false;
	}

	virtual void Render( IMatRenderContext *pRenderContext, CParticleCollection *pParticles, const Vector4D &vecDiffuseModulation, void *pContext ) const;
};

DEFINE_PARTICLE_OPERATOR( C_OP_RenderPoints, "render_points", OPERATOR_SINGLETON );

BEGIN_PARTICLE_RENDER_OPERATOR_UNPACK( C_OP_RenderPoints )
END_PARTICLE_OPERATOR_UNPACK( C_OP_RenderPoints )

void C_OP_RenderPoints::Render( IMatRenderContext *pRenderContext, CParticleCollection *pParticles, const Vector4D &vecDiffuseModulation, void *pContext ) const
{
	C_OP_RenderPointsContext_t *pCtx = reinterpret_cast<C_OP_RenderPointsContext_t *>( pContext );
	IMaterial *pMaterial = pParticles->m_pDef->GetMaterial();

	int nParticles;
	const ParticleRenderData_t *pRenderList = pParticles->GetRenderList( pRenderContext, true, &nParticles, &pCtx->m_VisibilityData );

	size_t xyz_stride;
	const fltx4 *xyz = pParticles->GetM128AttributePtr( PARTICLE_ATTRIBUTE_XYZ, &xyz_stride );

	pRenderContext->Bind( pMaterial );

	CMeshBuilder meshBuilder;

	int nMaxVertices = pRenderContext->GetMaxVerticesToRender( pMaterial );
	while ( nParticles )
	{
		IMesh* pMesh = pRenderContext->GetDynamicMesh( true );

		int nParticlesInBatch = MIN( nMaxVertices, nParticles );
		meshBuilder.Begin( pMesh, MATERIAL_POINTS, nParticlesInBatch );
		nParticles -= nParticlesInBatch;
		for( int i = 0; i < nParticlesInBatch; i++ )
		{
			int hParticle = (--pRenderList)->m_nIndex;
			int nIndex = ( hParticle / 4 ) * xyz_stride;
			int nOffset = hParticle & 0x3;
			meshBuilder.Position3f( SubFloat( xyz[nIndex], nOffset ), SubFloat( xyz[nIndex+1], nOffset ), SubFloat( xyz[nIndex+2], nOffset ) );
			meshBuilder.Color4ub( 255, 255, 255, 255 );
			meshBuilder.AdvanceVertexF<VTX_HAVEPOS | VTX_HAVECOLOR, 0>();
		}
		meshBuilder.End();
		pMesh->DrawModulated( vecDiffuseModulation );
	}
}


//-----------------------------------------------------------------------------
//
// Sprite Rendering
//
//-----------------------------------------------------------------------------
static SheetSequenceSample_t s_DefaultSheetSequence =
{
	0.0f, 0.0f, 1.0f, 1.0f,
	0.0f, 0.0f, 1.0f, 1.0f,
	0.0f, 0.0f, 1.0f, 1.0f,
	0.0f, 0.0f, 1.0f, 1.0f,
	1.0f
};

struct SpriteRenderInfo_t
{
	size_t m_nXYZStride;
	const fltx4 *m_pXYZ;
	size_t m_nRotStride;
	const fltx4 *m_pRot;
	size_t m_nYawStride;
	const fltx4 *m_pYaw;
	size_t m_nRGBStride;
	const fltx4 *m_pRGB;
	size_t m_nCreationTimeStride;
	const fltx4 *m_pCreationTimeStamp;
	size_t m_nSequenceStride;
	const fltx4 *m_pSequenceNumber;
	size_t m_nSequence1Stride;
	const fltx4 *m_pSequence1Number;
	float m_flAgeScale;
	float m_flAgeScale2;
	CSheet *m_pSheet;
	int m_nVertexOffset;
	CParticleCollection *m_pParticles;

	void Init( CParticleCollection *pParticles, int nVertexOffset, float flAgeScale, float flAgeScale2, CSheet *pSheet )
	{
		m_pParticles = pParticles;
		m_pXYZ = pParticles->GetM128AttributePtr( PARTICLE_ATTRIBUTE_XYZ, &m_nXYZStride );
		m_pRot = pParticles->GetM128AttributePtr( PARTICLE_ATTRIBUTE_ROTATION, &m_nRotStride );
		m_pYaw = pParticles->GetM128AttributePtr( PARTICLE_ATTRIBUTE_YAW, &m_nYawStride );
		m_pRGB = pParticles->GetM128AttributePtr( PARTICLE_ATTRIBUTE_TINT_RGB, &m_nRGBStride );
		m_pCreationTimeStamp = pParticles->GetM128AttributePtr( PARTICLE_ATTRIBUTE_CREATION_TIME, &m_nCreationTimeStride );
		m_pSequenceNumber = pParticles->GetM128AttributePtr( PARTICLE_ATTRIBUTE_SEQUENCE_NUMBER, &m_nSequenceStride );
		m_pSequence1Number = pParticles->GetM128AttributePtr( PARTICLE_ATTRIBUTE_SEQUENCE_NUMBER1, &m_nSequence1Stride );
		m_flAgeScale = flAgeScale;
		m_flAgeScale2 = flAgeScale2;
		m_pSheet = pSheet;
		m_nVertexOffset = nVertexOffset;
	}
};

class C_OP_RenderSprites : public C_OP_RenderPoints
{
	DECLARE_PARTICLE_OPERATOR( C_OP_RenderSprites );

	struct C_OP_RenderSpritesContext_t
	{
		unsigned int m_nOrientationVarToken;
		unsigned int m_nOrientationMatrixVarToken;
		CParticleVisibilityData m_VisibilityData;
		int m_nQueryHandle;
		bool m_bDidPerfWarning;
		bool m_bPerParticleGlow;
	};

	size_t GetRequiredContextBytes( ) const
	{
		return sizeof( C_OP_RenderSpritesContext_t );
	}

	virtual uint64 GetReadControlPointMask() const
	{
		uint64 nMask = 0;
		if ( m_nOrientationControlPoint >= 0 )
		{
			nMask |= 1ULL << m_nOrientationControlPoint;
		}
		if ( VisibilityInputs.m_nCPin >= 0 )
		{
			nMask |= 1ULL << VisibilityInputs.m_nCPin;
		}
		return nMask;
	}

	uint32 GetReadAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_XYZ_MASK | PARTICLE_ATTRIBUTE_ROTATION_MASK | PARTICLE_ATTRIBUTE_RADIUS_MASK |
			PARTICLE_ATTRIBUTE_TINT_RGB_MASK | PARTICLE_ATTRIBUTE_ALPHA_MASK | PARTICLE_ATTRIBUTE_CREATION_TIME_MASK |
			PARTICLE_ATTRIBUTE_SEQUENCE_NUMBER_MASK | PARTICLE_ATTRIBUTE_SEQUENCE_NUMBER1_MASK | PARTICLE_ATTRIBUTE_LIFE_DURATION_MASK;
	}

	virtual int GetParticlesToRender( CParticleCollection *pParticles, void *pContext, int nFirstParticle, int nRemainingVertices, int nRemainingIndices, int *pVertsUsed, int *pIndicesUsed ) const;
	virtual void Render( IMatRenderContext *pRenderContext, CParticleCollection *pParticles, const Vector4D &vecDiffuseModulation, void *pContext ) const;
	virtual void RenderUnsorted( CParticleCollection *pParticles, void *pContext, IMatRenderContext *pRenderContext, CMeshBuilder &meshBuilder, int nVertexOffset, int nFirstParticle, int nParticleCount ) const;

	void RenderSpriteCard( CMeshBuilder &meshBuilder, SpriteRenderInfo_t& info, int hParticle, const ParticleRenderData_t *pSortList ) const;
	void RenderTwoSequenceSpriteCardNew( CMeshBuilder &meshBuilder, SpriteRenderInfo_t& info, const ParticleFullRenderData_Scalar_View *pParticle ) const;
	template<bool bPerParticleOutline, bool bDoNormals, class S> void RenderSpriteCardNew( CMeshBuilder &meshBuilder, SpriteRenderInfo_t& info, const S *pParticle ) const;
	void RenderNonSpriteCardCameraFacing( CParticleCollection *pParticles, const Vector4D &vecDiffuseModulation, void *pContext, IMatRenderContext *pRenderContext, IMaterial *pMaterial ) const;
	void RenderNonSpriteCardCameraFacingOld( CParticleCollection *pParticles, const Vector4D &vecDiffuseModulation, void *pContext, IMatRenderContext *pRenderContext, IMaterial *pMaterial ) const;
	void RenderNonSpriteCardZRotating( CMeshBuilder &meshBuilder, SpriteRenderInfo_t& info, int hParticle, const Vector& vecCameraPos, const ParticleRenderData_t *pSortList ) const;
	void RenderNonSpriteCardZRotating( CParticleCollection *pParticles, const Vector4D &vecDiffuseModulation, void *pContext, IMatRenderContext *pRenderContext, IMaterial *pMaterial ) const;
	void RenderUnsortedNonSpriteCardZRotating( CParticleCollection *pParticles, void *pContext, IMatRenderContext *pRenderContext, CMeshBuilder &meshBuilder, int nVertexOffset, int nFirstParticle, int nParticleCount ) const;
	void RenderNonSpriteCardOriented( CMeshBuilder &meshBuilder, SpriteRenderInfo_t& info, int hParticle, const Vector& vecCameraPos, const ParticleRenderData_t *pSortList ) const;
	void RenderNonSpriteCardOriented( CParticleCollection *pParticles, const Vector4D &vecDiffuseModulation, void *pContext, IMatRenderContext *pRenderContext, IMaterial *pMaterial ) const;
	void RenderUnsortedNonSpriteCardOriented( CParticleCollection *pParticles, void *pContext, IMatRenderContext *pRenderContext, CMeshBuilder &meshBuilder, int nVertexOffset, int nFirstParticle, int nParticleCount ) const;

	virtual void InitializeContextData( CParticleCollection *pParticles, void *pContext ) const;

	float m_flAnimationRate;
	float m_flAnimationRate2;
	bool m_bFitCycleToLifetime;
	bool m_bAnimateInFPS;
	int m_nOrientationType;
	int m_nOrientationControlPoint;
};

DEFINE_PARTICLE_OPERATOR( C_OP_RenderSprites, "render_animated_sprites", OPERATOR_GENERIC );

BEGIN_PARTICLE_RENDER_OPERATOR_UNPACK( C_OP_RenderSprites )
	DMXELEMENT_UNPACK_FIELD( "animation rate", ".1", float, m_flAnimationRate )
	DMXELEMENT_UNPACK_FIELD( "animation_fit_lifetime", "0", bool, m_bFitCycleToLifetime )
	DMXELEMENT_UNPACK_FIELD( "orientation_type", "0", int, m_nOrientationType )
	DMXELEMENT_UNPACK_FIELD( "orientation control point", "-1", int, m_nOrientationControlPoint )
	DMXELEMENT_UNPACK_FIELD( "second sequence animation rate", "0", float, m_flAnimationRate2 )
	DMXELEMENT_UNPACK_FIELD( "use animation rate as FPS", "0", bool, m_bAnimateInFPS )
END_PARTICLE_OPERATOR_UNPACK( C_OP_RenderSprites )

void C_OP_RenderSprites::InitializeContextData( CParticleCollection *pParticles, void *pContext ) const
{
	C_OP_RenderSpritesContext_t *pCtx = reinterpret_cast<C_OP_RenderSpritesContext_t *>( pContext );
	pCtx->m_nOrientationVarToken = 0;
	pCtx->m_nOrientationMatrixVarToken = 0;
	if ( ( VisibilityInputs.m_nCPin >= 0 ) || ( VisibilityInputs.m_flRadiusScaleFOVBase > 0 ) )
	{
		pCtx->m_VisibilityData.m_bUseVisibility = true;
	}
	else
	{
		pCtx->m_VisibilityData.m_bUseVisibility = false;
	}
	pCtx->m_bDidPerfWarning = false;
	IMaterial *pMaterial = pParticles->m_pDef->GetMaterial();
	IMaterialVar *pVar = pMaterial->FindVarFast( "$perparticleoutline", &pParticles->m_pDef->m_nPerParticleOutlineMaterialVarToken );
	pCtx->m_bPerParticleGlow = ( pVar && pVar->GetIntValue() );
}

const SheetSequenceSample_t *GetSampleForSequence( CSheet *pSheet, float flAge, float flAgeScale, int nSequence )
{
	if ( pSheet == NULL )
		return NULL;

	if ( pSheet->m_SheetInfo[nSequence].m_nNumFrames == 1 )
		return (const SheetSequenceSample_t *) &pSheet->m_SheetInfo[nSequence].m_pSamples[0];

	// NOTE: the product is converted without being rounded to float first (x87)
	unsigned int nFrame = (unsigned int)(int64)( (double)flAge * (double)flAgeScale );
	if ( pSheet->m_SheetInfo[nSequence].m_SeqFlags & SEQ_FLAG_CLAMP )
	{
		nFrame = MIN( nFrame, SEQUENCE_SAMPLE_COUNT-1 );
	}
	else
	{
		nFrame &= SEQUENCE_SAMPLE_COUNT-1;
	}

	return &pSheet->m_SheetInfo[nSequence].m_pSamples[nFrame];
}

int C_OP_RenderSprites::GetParticlesToRender( CParticleCollection *pParticles, void *pContext, int nFirstParticle, int nRemainingVertices, int nRemainingIndices, int *pVertsUsed, int *pIndicesUsed ) const
{
	int nMaxParticles = ( (nRemainingVertices / 4) > (nRemainingIndices / 6) ) ? nRemainingIndices / 6 : nRemainingVertices / 4;
	int nParticleCount = pParticles->m_nActiveParticles - nFirstParticle;
	if ( nParticleCount > nMaxParticles )
	{
		nParticleCount = nMaxParticles;
	}
	*pVertsUsed = nParticleCount * 4;
	*pIndicesUsed = nParticleCount * 6;
	return nParticleCount;
}

// Fast float -> color conversion as done by the shipped renderers
static FORCEINLINE unsigned char SpriteFToC( float c )
{
	volatile float dc = c * 255.0f + (float)(1 << 23);
	return *(volatile unsigned char *)&dc;
}

void C_OP_RenderSprites::RenderNonSpriteCardCameraFacingOld( CParticleCollection *pParticles, const Vector4D &vecDiffuseModulation, void *pContext, IMatRenderContext *pRenderContext, IMaterial *pMaterial ) const
{
	C_OP_RenderSpritesContext_t *pCtx = reinterpret_cast<C_OP_RenderSpritesContext_t *>( pContext );

	// generate the sort list before this code starts messing with the matrices
	int nParticles;
	const ParticleRenderData_t *pSortList = pParticles->GetRenderList( pRenderContext, true, &nParticles, &pCtx->m_VisibilityData );

	VMatrix tempView;

	// Store matrices off so we can restore them in RenderEnd().
	pRenderContext->GetMatrix( MATERIAL_VIEW, &tempView );

	// Force the user clip planes to use the old view matrix
	pRenderContext->EnableUserClipTransformOverride( true );
	pRenderContext->UserClipTransform( tempView );

	// The particle renderers want to do things in camera space
	pRenderContext->MatrixMode( MATERIAL_VIEW );
	pRenderContext->PushMatrix();
	pRenderContext->LoadIdentity();

	size_t xyz_stride;
	const fltx4 *xyz = pParticles->GetM128AttributePtr( PARTICLE_ATTRIBUTE_XYZ, &xyz_stride );

	size_t rot_stride;
	const fltx4 *pRot = pParticles->GetM128AttributePtr( PARTICLE_ATTRIBUTE_ROTATION, &rot_stride );

	size_t rgb_stride;
	const fltx4 *pRGB = pParticles->GetM128AttributePtr( PARTICLE_ATTRIBUTE_TINT_RGB, &rgb_stride );

	size_t ct_stride;
	const fltx4 *pCreationTimeStamp = pParticles->GetM128AttributePtr( PARTICLE_ATTRIBUTE_CREATION_TIME, &ct_stride );

	size_t seq_stride;
	const fltx4 *pSequenceNumber = pParticles->GetM128AttributePtr( PARTICLE_ATTRIBUTE_SEQUENCE_NUMBER, &seq_stride );

	size_t ld_stride;
	const fltx4 *pLifeDuration = pParticles->GetM128AttributePtr( PARTICLE_ATTRIBUTE_LIFE_DURATION, &ld_stride );

	int nMaxParticlesInBatch = GetMaxParticlesPerBatch( pRenderContext, pMaterial, false );
	float flAgeScale;
	CSheet *pSheet = pParticles->m_Sheet();
	while ( nParticles )
	{
		int nParticlesInBatch = MIN( nMaxParticlesInBatch, nParticles );
		nParticles -= nParticlesInBatch;

		IMesh* pMesh = pRenderContext->GetDynamicMesh( true );
		CMeshBuilder meshBuilder;
		meshBuilder.Begin( pMesh, MATERIAL_QUADS, nParticlesInBatch );
		for( int i = 0; i < nParticlesInBatch; i++ )
		{
			int hParticle = (--pSortList)->m_nIndex;
			int nGroup = hParticle / 4;
			int nOffset = hParticle & 0x3;

			unsigned char ac = pSortList->m_nAlpha;
			if (! ac )
				continue;

			int nColorIndex = nGroup * rgb_stride;
			float r = SubFloat( pRGB[nColorIndex], nOffset );
			float g = SubFloat( pRGB[nColorIndex+1], nOffset );
			float b = SubFloat( pRGB[nColorIndex+2], nOffset );

			unsigned char rc = SpriteFToC( r );
			unsigned char gc = SpriteFToC( g );
			unsigned char bc = SpriteFToC( b );

			float rad = pSortList->m_flRadius;
			int nXYZIndex = nGroup * xyz_stride;
			Vector vecWorldPos( SubFloat( xyz[ nXYZIndex ], nOffset ), SubFloat( xyz[ nXYZIndex+1 ], nOffset ), SubFloat( xyz[ nXYZIndex+2 ], nOffset ) );
			Vector vecViewPos;
			vecViewPos.x = ( ( tempView[0][2] * vecWorldPos.z + tempView[0][1] * vecWorldPos.y ) + tempView[0][0] * vecWorldPos.x ) + tempView[0][3];
			vecViewPos.y = ( ( tempView[1][2] * vecWorldPos.z + tempView[1][1] * vecWorldPos.y ) + tempView[1][0] * vecWorldPos.x ) + tempView[1][3];
			vecViewPos.z = ( ( tempView[2][2] * vecWorldPos.z + tempView[2][1] * vecWorldPos.y ) + tempView[2][0] * vecWorldPos.x ) + tempView[2][3];
			if (!IsFinite(vecViewPos.x))
				continue;

			float rot = SubFloat( pRot[ nGroup * rot_stride ], nOffset );
			float ca, sa;
			SinCos( rot, &sa, &ca );

			const SheetSequenceSample_t *pSample = &s_DefaultSheetSequence;
			if ( pSheet )
			{
				int nSequence = SubFloat( pSequenceNumber[ nGroup * seq_stride ], nOffset );
				if ( m_bFitCycleToLifetime )
				{
					float flLifetime = SubFloat( pLifeDuration[ nGroup * ld_stride ], nOffset );
					flAgeScale = ( flLifetime > 0.0f ) ? ( 1.0f / flLifetime ) * SEQUENCE_SAMPLE_COUNT : 0.0f;
				}
				else
				{
					flAgeScale = m_flAnimationRate * SEQUENCE_SAMPLE_COUNT;
					if ( m_bAnimateInFPS )
					{
						flAgeScale = flAgeScale / pSheet->m_SheetInfo[nSequence].m_flFrameSpan;
					}
				}
				pSample = GetSampleForSequence( pSheet,
					pParticles->m_flCurTime - SubFloat( pCreationTimeStamp[ nGroup * ct_stride ], nOffset ),
					flAgeScale, nSequence );
			}

			const SequenceSampleTextureCoords_t *pSample0 = &(pSample->m_TextureCoordData[0]);

			meshBuilder.Position3f( vecViewPos.x + (sa - ca) * rad, vecViewPos.y + (-sa - ca) * rad, vecViewPos.z );
			meshBuilder.Color4ub( rc, gc, bc, ac );
			meshBuilder.TexCoord2f( 0, pSample0->m_fLeft_U0, pSample0->m_fBottom_V0 );
			meshBuilder.AdvanceVertexF<VTX_HAVEPOS | VTX_HAVECOLOR, 1>();

			meshBuilder.Position3f( vecViewPos.x + (-ca - sa) * rad, vecViewPos.y + (ca - sa) * rad, vecViewPos.z );
			meshBuilder.Color4ub( rc, gc, bc, ac );
			meshBuilder.TexCoord2f( 0, pSample0->m_fLeft_U0, pSample0->m_fTop_V0 );
			meshBuilder.AdvanceVertexF<VTX_HAVEPOS | VTX_HAVECOLOR, 1>();

			meshBuilder.Position3f( vecViewPos.x + (ca - sa) * rad, vecViewPos.y + (sa + ca) * rad, vecViewPos.z );
			meshBuilder.Color4ub( rc, gc, bc, ac );
			meshBuilder.TexCoord2f( 0, pSample0->m_fRight_U0, pSample0->m_fTop_V0 );
			meshBuilder.AdvanceVertexF<VTX_HAVEPOS | VTX_HAVECOLOR, 1>();

			meshBuilder.Position3f( vecViewPos.x + (sa + ca) * rad, vecViewPos.y + (sa - ca) * rad, vecViewPos.z );
			meshBuilder.Color4ub( rc, gc, bc, ac );
			meshBuilder.TexCoord2f( 0, pSample0->m_fRight_U0, pSample0->m_fBottom_V0 );
			meshBuilder.AdvanceVertexF<VTX_HAVEPOS | VTX_HAVECOLOR, 1>();
		}
		meshBuilder.End();
		pMesh->DrawModulated( vecDiffuseModulation );
	}

	pRenderContext->EnableUserClipTransformOverride( false );

	pRenderContext->MatrixMode( MATERIAL_VIEW );
	pRenderContext->PopMatrix();
}

void C_OP_RenderSprites::RenderNonSpriteCardCameraFacing( CParticleCollection *pParticles, const Vector4D &vecDiffuseModulation, void *pContext, IMatRenderContext *pRenderContext, IMaterial *pMaterial ) const
{
	C_OP_RenderSpritesContext_t *pCtx = reinterpret_cast<C_OP_RenderSpritesContext_t *>( pContext );

	// generate the sort list before this code starts messing with the matrices
	int nParticles;
	ParticleFullRenderData_Scalar_View **pSortList = GetExtendedRenderList( pParticles, pRenderContext, true, &nParticles, &pCtx->m_VisibilityData );

	VMatrix tempView;

	// Store matrices off so we can restore them in RenderEnd().
	pRenderContext->GetMatrix( MATERIAL_VIEW, &tempView );

	// Force the user clip planes to use the old view matrix
	pRenderContext->EnableUserClipTransformOverride( true );
	pRenderContext->UserClipTransform( tempView );

	// The particle renderers want to do things in camera space
	pRenderContext->MatrixMode( MATERIAL_VIEW );
	pRenderContext->PushMatrix();
	pRenderContext->LoadIdentity();

	int nMaxParticlesInBatch = GetMaxParticlesPerBatch( pRenderContext, pMaterial, false );
	CSheet *pSheet = pParticles->m_Sheet();
	while ( nParticles )
	{
		int nParticlesInBatch = MIN( nMaxParticlesInBatch, nParticles );
		nParticles -= nParticlesInBatch;

		IMesh* pMesh = pRenderContext->GetDynamicMesh( true );
		CMeshBuilder meshBuilder;
		meshBuilder.Begin( pMesh, MATERIAL_QUADS, nParticlesInBatch );
		for( int i = 0; i < nParticlesInBatch; i++ )
		{
			ParticleFullRenderData_Scalar_View *pParticle = *( --pSortList );
			unsigned char ac = pParticle->m_nAlpha;
			if (! ac )
				continue;

			unsigned char rc = pParticle->m_nRed;
			unsigned char gc = pParticle->m_nGreen;
			unsigned char bc = pParticle->m_nBlue;

			Vector vecViewPos;
			vecViewPos.x = ( ( tempView[0][0] * pParticle->m_flX + tempView[0][2] * pParticle->m_flZ ) + tempView[0][1] * pParticle->m_flY ) + tempView[0][3];
			vecViewPos.y = ( ( tempView[1][0] * pParticle->m_flX + tempView[1][2] * pParticle->m_flZ ) + tempView[1][1] * pParticle->m_flY ) + tempView[1][3];
			vecViewPos.z = ( ( tempView[2][0] * pParticle->m_flX + tempView[2][2] * pParticle->m_flZ ) + tempView[2][1] * pParticle->m_flY ) + tempView[2][3];
			if (!IsFinite(vecViewPos.x))
				continue;

			float rot = pParticle->m_flRotation;
			float ca, sa;
			SinCos( rot, &sa, &ca );

			const SheetSequenceSample_t *pSample = &s_DefaultSheetSequence;
			if ( pSheet )
			{
				float flAgeScale = m_flAnimationRate * SEQUENCE_SAMPLE_COUNT;
				if ( m_bAnimateInFPS )
				{
					flAgeScale = flAgeScale / pSheet->m_SheetInfo[pParticle->m_nSequenceID].m_flFrameSpan;
				}
				pSample = GetSampleForSequence( pSheet, pParticle->m_flAnimationTimeValue, flAgeScale, pParticle->m_nSequenceID );
			}

			const SequenceSampleTextureCoords_t *pSample0 = &(pSample->m_TextureCoordData[0]);
			float rad = pParticle->m_flRadius;

			meshBuilder.Position3f( vecViewPos.x + (sa - ca) * rad, vecViewPos.y + (-sa - ca) * rad, vecViewPos.z );
			meshBuilder.Color4ub( rc, gc, bc, ac );
			meshBuilder.TexCoord2f( 0, pSample0->m_fLeft_U0, pSample0->m_fBottom_V0 );
			meshBuilder.AdvanceVertexF<VTX_HAVEPOS | VTX_HAVECOLOR, 1>();

			meshBuilder.Position3f( vecViewPos.x + (-ca - sa) * rad, vecViewPos.y + (ca - sa) * rad, vecViewPos.z );
			meshBuilder.Color4ub( rc, gc, bc, ac );
			meshBuilder.TexCoord2f( 0, pSample0->m_fLeft_U0, pSample0->m_fTop_V0 );
			meshBuilder.AdvanceVertexF<VTX_HAVEPOS | VTX_HAVECOLOR, 1>();

			meshBuilder.Position3f( vecViewPos.x + (ca - sa) * rad, vecViewPos.y + (sa + ca) * rad, vecViewPos.z );
			meshBuilder.Color4ub( rc, gc, bc, ac );
			meshBuilder.TexCoord2f( 0, pSample0->m_fRight_U0, pSample0->m_fTop_V0 );
			meshBuilder.AdvanceVertexF<VTX_HAVEPOS | VTX_HAVECOLOR, 1>();

			meshBuilder.Position3f( vecViewPos.x + (sa + ca) * rad, vecViewPos.y + (sa - ca) * rad, vecViewPos.z );
			meshBuilder.Color4ub( rc, gc, bc, ac );
			meshBuilder.TexCoord2f( 0, pSample0->m_fRight_U0, pSample0->m_fBottom_V0 );
			meshBuilder.AdvanceVertexF<VTX_HAVEPOS | VTX_HAVECOLOR, 1>();
		}
		meshBuilder.End();
		pMesh->DrawModulated( vecDiffuseModulation );
	}

	pRenderContext->EnableUserClipTransformOverride( false );

	pRenderContext->MatrixMode( MATERIAL_VIEW );
	pRenderContext->PopMatrix();
}

void C_OP_RenderSprites::RenderNonSpriteCardZRotating( CMeshBuilder &meshBuilder, SpriteRenderInfo_t& info, int hParticle, const Vector& vecCameraPos, const ParticleRenderData_t *pSortList ) const
{
	int nGroup = hParticle / 4;
	int nOffset = hParticle & 0x3;

	unsigned char ac = pSortList->m_nAlpha;
	if (! ac )
		return;

	int nColorIndex = nGroup * info.m_nRGBStride;
	float r = SubFloat( info.m_pRGB[nColorIndex], nOffset );
	float g = SubFloat( info.m_pRGB[nColorIndex+1], nOffset );
	float b = SubFloat( info.m_pRGB[nColorIndex+2], nOffset );

	unsigned char rc = SpriteFToC( r );
	unsigned char gc = SpriteFToC( g );
	unsigned char bc = SpriteFToC( b );

	float rad = pSortList->m_flRadius;
	float rot = SubFloat( info.m_pRot[ nGroup * info.m_nRotStride ], nOffset );

	float ca, sa;
	SinCos( -rot, &sa, &ca );

	int nXYZIndex = nGroup * info.m_nXYZStride;
	Vector vecWorldPos( SubFloat( info.m_pXYZ[ nXYZIndex ], nOffset ), SubFloat( info.m_pXYZ[ nXYZIndex+1 ], nOffset ), SubFloat( info.m_pXYZ[ nXYZIndex+2 ], nOffset ) );

	Vector vecViewToPos;
	VectorSubtract( vecWorldPos, vecCameraPos, vecViewToPos );
	float flLength = sqrtf( ( vecViewToPos.x * vecViewToPos.x + vecViewToPos.z * vecViewToPos.z ) + vecViewToPos.y * vecViewToPos.y );
	if ( flLength < rad * 0.5f )
		return;

	Vector vecUp( 0, 0, 1 );
	Vector vecRight;
	CrossProduct( vecUp, vecCameraPos, vecRight );
	VectorNormalize( vecRight );

	const SheetSequenceSample_t *pSample = &s_DefaultSheetSequence;
	if ( info.m_pSheet )
	{
		pSample = GetSampleForSequence( info.m_pSheet,
			info.m_pParticles->m_flCurTime - SubFloat( info.m_pCreationTimeStamp[ nGroup * info.m_nCreationTimeStride ], nOffset ),
			info.m_flAgeScale,
			SubFloat( info.m_pSequenceNumber[ nGroup * info.m_nSequenceStride ], nOffset ) );
	}

	const SequenceSampleTextureCoords_t *pSample0 = &(pSample->m_TextureCoordData[0]);

	vecRight *= rad;

	float x, y;
	Vector vecCorner;

	x = -ca - sa; y = sa - ca;
	vecCorner.x = vecRight.x * x + vecWorldPos.x;
	vecCorner.y = vecRight.y * x + vecWorldPos.y;
	vecCorner.z = y * rad + ( vecRight.z * x + vecWorldPos.z );
	meshBuilder.Position3fv( vecCorner.Base() );
	meshBuilder.Color4ub( rc, gc, bc, ac );
	meshBuilder.TexCoord2f( 0, pSample0->m_fLeft_U0, pSample0->m_fBottom_V0 );
	meshBuilder.AdvanceVertexF<VTX_HAVEPOS | VTX_HAVECOLOR, 1>();

	x = sa - ca; y = sa + ca;
	vecCorner.x = vecRight.x * x + vecWorldPos.x;
	vecCorner.y = vecRight.y * x + vecWorldPos.y;
	vecCorner.z = y * rad + ( vecRight.z * x + vecWorldPos.z );
	meshBuilder.Position3fv( vecCorner.Base() );
	meshBuilder.Color4ub( rc, gc, bc, ac );
	meshBuilder.TexCoord2f( 0, pSample0->m_fLeft_U0, pSample0->m_fTop_V0 );
	meshBuilder.AdvanceVertexF<VTX_HAVEPOS | VTX_HAVECOLOR, 1>();

	x = sa + ca; y = ca - sa;
	vecCorner.x = vecRight.x * x + vecWorldPos.x;
	vecCorner.y = vecRight.y * x + vecWorldPos.y;
	vecCorner.z = y * rad + ( vecRight.z * x + vecWorldPos.z );
	meshBuilder.Position3fv( vecCorner.Base() );
	meshBuilder.Color4ub( rc, gc, bc, ac );
	meshBuilder.TexCoord2f( 0, pSample0->m_fRight_U0, pSample0->m_fTop_V0 );
	meshBuilder.AdvanceVertexF<VTX_HAVEPOS | VTX_HAVECOLOR, 1>();

	x = ca - sa; y = -ca - sa;
	vecCorner.x = vecRight.x * x + vecWorldPos.x;
	vecCorner.y = vecRight.y * x + vecWorldPos.y;
	vecCorner.z = y * rad + ( vecRight.z * x + vecWorldPos.z );
	meshBuilder.Position3fv( vecCorner.Base() );
	meshBuilder.Color4ub( rc, gc, bc, ac );
	meshBuilder.TexCoord2f( 0, pSample0->m_fRight_U0, pSample0->m_fBottom_V0 );
	meshBuilder.AdvanceVertexF<VTX_HAVEPOS | VTX_HAVECOLOR, 1>();

	meshBuilder.FastQuad( info.m_nVertexOffset );
	info.m_nVertexOffset += 4;
}

void C_OP_RenderSprites::RenderNonSpriteCardZRotating( CParticleCollection *pParticles, const Vector4D &vecDiffuseModulation, void *pContext, IMatRenderContext *pRenderContext, IMaterial *pMaterial ) const
{
	C_OP_RenderSpritesContext_t *pCtx = reinterpret_cast<C_OP_RenderSpritesContext_t *>( pContext );

	// NOTE: This is interesting to support because at first we won't have all the various
	// pixel-shader versions of SpriteCard, like modulate, twotexture, etc. etc.
	Vector vecCameraPos;
	pRenderContext->GetWorldSpaceCameraPosition( &vecCameraPos );

	SpriteRenderInfo_t info;
	info.Init( pParticles, 0, m_flAnimationRate * SEQUENCE_SAMPLE_COUNT, 0, pParticles->m_Sheet() );

	int nParticles;
	const ParticleRenderData_t *pSortList = pParticles->GetRenderList( pRenderContext, true, &nParticles, &pCtx->m_VisibilityData );

	int nMaxParticlesInBatch = GetMaxParticlesPerBatch( pRenderContext, pMaterial, false );
	while ( nParticles )
	{
		int nParticlesInBatch = MIN( nMaxParticlesInBatch, nParticles );
		nParticles -= nParticlesInBatch;

		IMesh* pMesh = pRenderContext->GetDynamicMesh( true );
		CMeshBuilder meshBuilder;
		meshBuilder.Begin( pMesh, MATERIAL_TRIANGLES, nParticlesInBatch * 4, nParticlesInBatch * 6 );
		info.m_nVertexOffset = 0;
		for( int i = 0; i < nParticlesInBatch; i++ )
		{
			--pSortList;
			RenderNonSpriteCardZRotating( meshBuilder, info, pSortList->m_nIndex, vecCameraPos, pSortList );
		}
		meshBuilder.End( );
		pMesh->DrawModulated( vecDiffuseModulation );
	}
}

void C_OP_RenderSprites::RenderUnsortedNonSpriteCardZRotating( CParticleCollection *pParticles, void *pContext, IMatRenderContext *pRenderContext, CMeshBuilder &meshBuilder, int nVertexOffset, int nFirstParticle, int nParticleCount ) const
{
	C_OP_RenderSpritesContext_t *pCtx = reinterpret_cast<C_OP_RenderSpritesContext_t *>( pContext );

	Vector vecCameraPos;
	pRenderContext->GetWorldSpaceCameraPosition( &vecCameraPos );

	SpriteRenderInfo_t info;
	info.Init( pParticles, nVertexOffset, m_flAnimationRate * SEQUENCE_SAMPLE_COUNT, 0, pParticles->m_Sheet() );

	int nParticles;
	const ParticleRenderData_t *pSortList = pParticles->GetRenderList( pRenderContext, false, &nParticles, &pCtx->m_VisibilityData );

	// NOTE: shipped code passes the end of the render list for every particle
	int hParticle = nFirstParticle;
	for ( int i = 0; i < nParticleCount; i++, hParticle++ )
	{
		RenderNonSpriteCardZRotating( meshBuilder, info, hParticle, vecCameraPos, pSortList );
	}
}

void C_OP_RenderSprites::RenderNonSpriteCardOriented( CMeshBuilder &meshBuilder, SpriteRenderInfo_t& info, int hParticle, const Vector& vecCameraPos, const ParticleRenderData_t *pSortList ) const
{
	int nGroup = hParticle / 4;
	int nOffset = hParticle & 0x3;

	unsigned char ac = pSortList->m_nAlpha;
	if (! ac )
		return;

	int nColorIndex = nGroup * info.m_nRGBStride;
	float r = SubFloat( info.m_pRGB[nColorIndex], nOffset );
	float g = SubFloat( info.m_pRGB[nColorIndex+1], nOffset );
	float b = SubFloat( info.m_pRGB[nColorIndex+2], nOffset );

	unsigned char rc = SpriteFToC( r );
	unsigned char gc = SpriteFToC( g );
	unsigned char bc = SpriteFToC( b );

	float rad = pSortList->m_flRadius;
	float rot = SubFloat( info.m_pRot[ nGroup * info.m_nRotStride ], nOffset );

	float ca, sa;
	SinCos( -rot, &sa, &ca );

	int nXYZIndex = nGroup * info.m_nXYZStride;
	Vector vecWorldPos( SubFloat( info.m_pXYZ[ nXYZIndex ], nOffset ), SubFloat( info.m_pXYZ[ nXYZIndex+1 ], nOffset ), SubFloat( info.m_pXYZ[ nXYZIndex+2 ], nOffset ) );

	Vector vecViewToPos;
	VectorSubtract( vecWorldPos, vecCameraPos, vecViewToPos );
	float flLength = sqrtf( ( vecViewToPos.z * vecViewToPos.z + vecViewToPos.x * vecViewToPos.x ) + vecViewToPos.y * vecViewToPos.y );
	if ( flLength < rad * 0.5f )
		return;

	Vector vecRight, vecUp;
	if ( m_nOrientationControlPoint < 0 )
	{
		vecRight.Init( 1, 0, 0 );
		vecUp.Init( 0, -1, 0 );
	}
	else
	{
		vecRight = info.m_pParticles->ControlPoint( m_nOrientationControlPoint ).m_ForwardVector;
		vecUp = info.m_pParticles->ControlPoint( m_nOrientationControlPoint ).m_RightVector;
	}

	const SheetSequenceSample_t *pSample = &s_DefaultSheetSequence;
	if ( info.m_pSheet )
	{
		pSample = GetSampleForSequence( info.m_pSheet,
			info.m_pParticles->m_flCurTime - SubFloat( info.m_pCreationTimeStamp[ nGroup * info.m_nCreationTimeStride ], nOffset ),
			info.m_flAgeScale,
			SubFloat( info.m_pSequenceNumber[ nGroup * info.m_nSequenceStride ], nOffset ) );
	}

	const SequenceSampleTextureCoords_t *pSample0 = &(pSample->m_TextureCoordData[0]);

	vecRight *= rad;
	vecUp *= rad;

	float x, y;
	Vector vecCorner;

	x = ca - sa; y = -ca - sa;
	VectorMA( vecWorldPos, x, vecRight, vecCorner );
	VectorMA( vecCorner, y, vecUp, vecCorner );
	meshBuilder.Position3fv( vecCorner.Base() );
	meshBuilder.Color4ub( rc, gc, bc, ac );
	meshBuilder.TexCoord2f( 0, pSample0->m_fRight_U0, pSample0->m_fBottom_V0 );
	meshBuilder.AdvanceVertexF<VTX_HAVEPOS | VTX_HAVECOLOR, 1>();

	x = sa + ca; y = ca - sa;
	VectorMA( vecWorldPos, x, vecRight, vecCorner );
	VectorMA( vecCorner, y, vecUp, vecCorner );
	meshBuilder.Position3fv( vecCorner.Base() );
	meshBuilder.Color4ub( rc, gc, bc, ac );
	meshBuilder.TexCoord2f( 0, pSample0->m_fRight_U0, pSample0->m_fTop_V0 );
	meshBuilder.AdvanceVertexF<VTX_HAVEPOS | VTX_HAVECOLOR, 1>();

	x = sa - ca; y = sa + ca;
	VectorMA( vecWorldPos, x, vecRight, vecCorner );
	VectorMA( vecCorner, y, vecUp, vecCorner );
	meshBuilder.Position3fv( vecCorner.Base() );
	meshBuilder.Color4ub( rc, gc, bc, ac );
	meshBuilder.TexCoord2f( 0, pSample0->m_fLeft_U0, pSample0->m_fTop_V0 );
	meshBuilder.AdvanceVertexF<VTX_HAVEPOS | VTX_HAVECOLOR, 1>();

	x = -ca - sa; y = sa - ca;
	VectorMA( vecWorldPos, x, vecRight, vecCorner );
	VectorMA( vecCorner, y, vecUp, vecCorner );
	meshBuilder.Position3fv( vecCorner.Base() );
	meshBuilder.Color4ub( rc, gc, bc, ac );
	meshBuilder.TexCoord2f( 0, pSample0->m_fLeft_U0, pSample0->m_fBottom_V0 );
	meshBuilder.AdvanceVertexF<VTX_HAVEPOS | VTX_HAVECOLOR, 1>();

	meshBuilder.FastQuad( info.m_nVertexOffset );
	info.m_nVertexOffset += 4;
}

void C_OP_RenderSprites::RenderNonSpriteCardOriented( CParticleCollection *pParticles, const Vector4D &vecDiffuseModulation, void *pContext, IMatRenderContext *pRenderContext, IMaterial *pMaterial ) const
{
	C_OP_RenderSpritesContext_t *pCtx = reinterpret_cast<C_OP_RenderSpritesContext_t *>( pContext );

	// NOTE: This is interesting to support because at first we won't have all the various
	// pixel-shader versions of SpriteCard, like modulate, twotexture, etc. etc.
	Vector vecCameraPos;
	pRenderContext->GetWorldSpaceCameraPosition( &vecCameraPos );

	SpriteRenderInfo_t info;
	info.Init( pParticles, 0, m_flAnimationRate * SEQUENCE_SAMPLE_COUNT, 0, pParticles->m_Sheet() );

	int nParticles;
	const ParticleRenderData_t *pSortList = pParticles->GetRenderList( pRenderContext, true, &nParticles, &pCtx->m_VisibilityData );

	int nMaxParticlesInBatch = GetMaxParticlesPerBatch( pRenderContext, pMaterial, false );
	while ( nParticles )
	{
		int nParticlesInBatch = MIN( nMaxParticlesInBatch, nParticles );
		nParticles -= nParticlesInBatch;

		IMesh* pMesh = pRenderContext->GetDynamicMesh( true );
		CMeshBuilder meshBuilder;
		meshBuilder.Begin( pMesh, MATERIAL_TRIANGLES, nParticlesInBatch * 4, nParticlesInBatch * 6 );
		info.m_nVertexOffset = 0;
		for( int i = 0; i < nParticlesInBatch; i++ )
		{
			--pSortList;
			RenderNonSpriteCardOriented( meshBuilder, info, pSortList->m_nIndex, vecCameraPos, pSortList );
		}
		meshBuilder.End( );
		pMesh->DrawModulated( vecDiffuseModulation );
	}
}

void C_OP_RenderSprites::RenderUnsortedNonSpriteCardOriented( CParticleCollection *pParticles, void *pContext, IMatRenderContext *pRenderContext, CMeshBuilder &meshBuilder, int nVertexOffset, int nFirstParticle, int nParticleCount ) const
{
	C_OP_RenderSpritesContext_t *pCtx = reinterpret_cast<C_OP_RenderSpritesContext_t *>( pContext );

	Vector vecCameraPos;
	pRenderContext->GetWorldSpaceCameraPosition( &vecCameraPos );

	SpriteRenderInfo_t info;
	info.Init( pParticles, nVertexOffset, m_flAnimationRate * SEQUENCE_SAMPLE_COUNT, 0, pParticles->m_Sheet() );

	int nParticles;
	const ParticleRenderData_t *pSortList = pParticles->GetRenderList( pRenderContext, false, &nParticles, &pCtx->m_VisibilityData );

	// NOTE: shipped code passes the end of the render list for every particle
	int hParticle = nFirstParticle;
	for ( int i = 0; i < nParticleCount; i++, hParticle++ )
	{
		RenderNonSpriteCardOriented( meshBuilder, info, hParticle, vecCameraPos, pSortList );
	}
}

template<bool bPerParticleOutline, bool bDoNormals, class S> void C_OP_RenderSprites::RenderSpriteCardNew( CMeshBuilder &meshBuilder, SpriteRenderInfo_t& info, const S *pSortList ) const
{
	unsigned char ac = pSortList->m_nAlpha;
	if (! ac )
		return;

	unsigned char rc = pSortList->m_nRed;
	unsigned char gc = pSortList->m_nGreen;
	unsigned char bc = pSortList->m_nBlue;

	float rad = pSortList->m_flRadius;
	float rot = pSortList->m_flRotation;
	float yaw = pSortList->m_flYaw;

	float x = pSortList->m_flX;
	float y = pSortList->m_flY;
	float z = pSortList->m_flZ;

	const SheetSequenceSample_t *pSample = &s_DefaultSheetSequence;
	if ( info.m_pSheet )
	{
		float flAgeScale = info.m_flAgeScale;
		if ( m_bAnimateInFPS )
		{
			flAgeScale = flAgeScale / info.m_pParticles->m_Sheet()->m_SheetInfo[pSortList->m_nSequenceID].m_flFrameSpan;
		}
		pSample = GetSampleForSequence( info.m_pSheet, pSortList->m_flAnimationTimeValue, flAgeScale, pSortList->m_nSequenceID );
	}

	const SequenceSampleTextureCoords_t *pSample0 = &(pSample->m_TextureCoordData[0]);
	const SequenceSampleTextureCoords_t *pSecondTexture0 = &(pSample->m_TextureCoordData[1]);

	static float s_flCornerIds[] = { 0, 0, 1, 0, 1, 1, 0, 1 };
	for ( int i = 0; i < 4; ++i )
	{
		meshBuilder.Position3f( x, y, z );
		meshBuilder.Color4ub( rc, gc, bc, ac );
		meshBuilder.TexCoord4f( 0, pSample0->m_fLeft_U0, pSample0->m_fTop_V0, pSample0->m_fRight_U0, pSample0->m_fBottom_V0 );
		meshBuilder.TexCoord4f( 1, pSample0->m_fLeft_U1, pSample0->m_fTop_V1, pSample0->m_fRight_U1, pSample0->m_fBottom_V1 );
		meshBuilder.TexCoord4f( 2, pSample->m_fBlendFactor, rot, rad, yaw );
		meshBuilder.TexCoord2fv( 3, s_flCornerIds + 2 * i );
		meshBuilder.TexCoord4f( 4, pSecondTexture0->m_fLeft_U0, pSecondTexture0->m_fTop_V0, pSecondTexture0->m_fRight_U0, pSecondTexture0->m_fBottom_V0 );
		if ( bPerParticleOutline )
		{
			const ParticleRenderDataWithOutlineInformation_Scalar_View *pOutline = (const ParticleRenderDataWithOutlineInformation_Scalar_View *)pSortList;
			meshBuilder.TexCoord4f( 5, pOutline->m_flRed2, pOutline->m_flGreen2, pOutline->m_flBlue2, pOutline->m_flAlpha2 );
			meshBuilder.AdvanceVertexF<VTX_HAVEPOS | VTX_HAVECOLOR, 6>();
		}
		else if ( bDoNormals )
		{
			const ParticleRenderDataWithNormal_Scalar_View *pNormal = (const ParticleRenderDataWithNormal_Scalar_View *)pSortList;
			meshBuilder.TexCoord3f( 5, pNormal->m_flNormalX, pNormal->m_flNormalY, pNormal->m_flNormalZ );
			meshBuilder.AdvanceVertexF<VTX_HAVEPOS | VTX_HAVECOLOR, 6>();
		}
		else
		{
			meshBuilder.AdvanceVertexF<VTX_HAVEPOS | VTX_HAVECOLOR, 5>();
		}
	}
	meshBuilder.FastQuad( info.m_nVertexOffset );
	info.m_nVertexOffset += 4;
}

void C_OP_RenderSprites::RenderSpriteCard( CMeshBuilder &meshBuilder, SpriteRenderInfo_t& info, int hParticle, const ParticleRenderData_t *pSortList ) const
{
	unsigned char ac = pSortList->m_nAlpha;
	if (! ac )
		return;

	int nGroup = hParticle / 4;
	int nOffset = hParticle & 0x3;

	int nColorIndex = nGroup * info.m_nRGBStride;
	float r = SubFloat( info.m_pRGB[nColorIndex], nOffset );
	float g = SubFloat( info.m_pRGB[nColorIndex+1], nOffset );
	float b = SubFloat( info.m_pRGB[nColorIndex+2], nOffset );

	unsigned char rc = SpriteFToC( r );
	unsigned char gc = SpriteFToC( g );
	unsigned char bc = SpriteFToC( b );

	float rot = SubFloat( info.m_pRot[ nGroup * info.m_nRotStride ], nOffset );
	float rad = pSortList->m_flRadius;
	float yaw = SubFloat( info.m_pYaw[ nGroup * info.m_nYawStride ], nOffset );

	int nXYZIndex = nGroup * info.m_nXYZStride;
	float x = SubFloat( info.m_pXYZ[ nXYZIndex ], nOffset );
	float y = SubFloat( info.m_pXYZ[ nXYZIndex+1 ], nOffset );
	float z = SubFloat( info.m_pXYZ[ nXYZIndex+2 ], nOffset );

	const SheetSequenceSample_t *pSample = &s_DefaultSheetSequence;
	if ( info.m_pSheet )
	{
		int nSequence = SubFloat( info.m_pSequenceNumber[ nGroup * info.m_nSequenceStride ], nOffset );
		float flAgeScale = info.m_flAgeScale;
		if ( m_bAnimateInFPS )
		{
			flAgeScale = flAgeScale / info.m_pParticles->m_Sheet()->m_SheetInfo[nSequence].m_flFrameSpan;
		}
		pSample = GetSampleForSequence( info.m_pSheet,
			info.m_pParticles->m_flCurTime - SubFloat( info.m_pCreationTimeStamp[ nGroup * info.m_nCreationTimeStride ], nOffset ),
			flAgeScale, nSequence );
	}

	const SequenceSampleTextureCoords_t *pSample0 = &(pSample->m_TextureCoordData[0]);
	const SequenceSampleTextureCoords_t *pSecondTexture0 = &(pSample->m_TextureCoordData[1]);

	meshBuilder.Position3f( x, y, z );
	meshBuilder.Color4ub( rc, gc, bc, ac );
	meshBuilder.TexCoord4f( 0, pSample0->m_fLeft_U0, pSample0->m_fTop_V0, pSample0->m_fRight_U0, pSample0->m_fBottom_V0 );
	meshBuilder.TexCoord4f( 1, pSample0->m_fLeft_U1, pSample0->m_fTop_V1, pSample0->m_fRight_U1, pSample0->m_fBottom_V1 );
	meshBuilder.TexCoord4f( 2, pSample->m_fBlendFactor, rot, rad, yaw );
	meshBuilder.TexCoord2f( 3, 0, 0 );
	meshBuilder.TexCoord4f( 4, pSecondTexture0->m_fLeft_U0, pSecondTexture0->m_fTop_V0, pSecondTexture0->m_fRight_U0, pSecondTexture0->m_fBottom_V0 );
	meshBuilder.AdvanceVertexF<VTX_HAVEPOS | VTX_HAVECOLOR, 5>();

	meshBuilder.Position3f( x, y, z );
	meshBuilder.Color4ub( rc, gc, bc, ac );
	meshBuilder.TexCoord4f( 0, pSample0->m_fLeft_U0, pSample0->m_fTop_V0, pSample0->m_fRight_U0, pSample0->m_fBottom_V0 );
	meshBuilder.TexCoord4f( 1, pSample0->m_fLeft_U1, pSample0->m_fTop_V1, pSample0->m_fRight_U1, pSample0->m_fBottom_V1 );
	meshBuilder.TexCoord4f( 2, pSample->m_fBlendFactor, rot, rad, yaw );
	meshBuilder.TexCoord2f( 3, 1, 0 );
	meshBuilder.TexCoord4f( 4, pSecondTexture0->m_fLeft_U0, pSecondTexture0->m_fTop_V0, pSecondTexture0->m_fRight_U0, pSecondTexture0->m_fBottom_V0 );
	meshBuilder.AdvanceVertexF<VTX_HAVEPOS | VTX_HAVECOLOR, 5>();

	meshBuilder.Position3f( x, y, z );
	meshBuilder.Color4ub( rc, gc, bc, ac );
	meshBuilder.TexCoord4f( 0, pSample0->m_fLeft_U0, pSample0->m_fTop_V0, pSample0->m_fRight_U0, pSample0->m_fBottom_V0 );
	meshBuilder.TexCoord4f( 1, pSample0->m_fLeft_U1, pSample0->m_fTop_V1, pSample0->m_fRight_U1, pSample0->m_fBottom_V1 );
	meshBuilder.TexCoord4f( 2, pSample->m_fBlendFactor, rot, rad, yaw );
	meshBuilder.TexCoord2f( 3, 1, 1 );
	meshBuilder.TexCoord4f( 4, pSecondTexture0->m_fLeft_U0, pSecondTexture0->m_fTop_V0, pSecondTexture0->m_fRight_U0, pSecondTexture0->m_fBottom_V0 );
	meshBuilder.AdvanceVertexF<VTX_HAVEPOS | VTX_HAVECOLOR, 5>();

	meshBuilder.Position3f( x, y, z );
	meshBuilder.Color4ub( rc, gc, bc, ac );
	meshBuilder.TexCoord4f( 0, pSample0->m_fLeft_U0, pSample0->m_fTop_V0, pSample0->m_fRight_U0, pSample0->m_fBottom_V0 );
	meshBuilder.TexCoord4f( 1, pSample0->m_fLeft_U1, pSample0->m_fTop_V1, pSample0->m_fRight_U1, pSample0->m_fBottom_V1 );
	meshBuilder.TexCoord4f( 2, pSample->m_fBlendFactor, rot, rad, yaw );
	meshBuilder.TexCoord2f( 3, 0, 1 );
	meshBuilder.TexCoord4f( 4, pSecondTexture0->m_fLeft_U0, pSecondTexture0->m_fTop_V0, pSecondTexture0->m_fRight_U0, pSecondTexture0->m_fBottom_V0 );
	meshBuilder.AdvanceVertexF<VTX_HAVEPOS | VTX_HAVECOLOR, 5>();

	meshBuilder.FastQuad( info.m_nVertexOffset );
	info.m_nVertexOffset += 4;
}

void C_OP_RenderSprites::RenderTwoSequenceSpriteCardNew( CMeshBuilder &meshBuilder, SpriteRenderInfo_t& info, const ParticleFullRenderData_Scalar_View *pParticle ) const
{
	unsigned char rc = pParticle->m_nRed;
	unsigned char gc = pParticle->m_nGreen;
	unsigned char bc = pParticle->m_nBlue;
	unsigned char ac = pParticle->m_nAlpha;

	if (! ac )
		return;

	float rad = pParticle->m_flRadius;
	float rot = pParticle->m_flRotation;
	float yaw = pParticle->m_flYaw;

	float x = pParticle->m_flX;
	float y = pParticle->m_flY;
	float z = pParticle->m_flZ;

	const SheetSequenceSample_t *pSample = &s_DefaultSheetSequence;
	const SheetSequenceSample_t *pSample1 = &s_DefaultSheetSequence;
	if ( info.m_pSheet )
	{
		float flAgeScale = info.m_flAgeScale;
		float flAgeScale2 = info.m_flAgeScale2;
		float flAge = pParticle->m_flAnimationTimeValue;
		if ( m_bAnimateInFPS )
		{
			flAgeScale = flAgeScale / info.m_pParticles->m_Sheet()->m_SheetInfo[pParticle->m_nSequenceID].m_flFrameSpan;
			flAgeScale2 = flAgeScale2 / info.m_pParticles->m_Sheet()->m_SheetInfo[pParticle->m_nSequenceID1].m_flFrameSpan;
		}
		pSample = GetSampleForSequence( info.m_pSheet, flAge, flAgeScale, pParticle->m_nSequenceID );
		pSample1 = GetSampleForSequence( info.m_pSheet, flAge, flAgeScale2, pParticle->m_nSequenceID1 );
	}

	const SequenceSampleTextureCoords_t *pSample0 = &(pSample->m_TextureCoordData[0]);
	const SequenceSampleTextureCoords_t *pSecondTexture0 = &(pSample->m_TextureCoordData[1]);
	const SequenceSampleTextureCoords_t *pSample1Frame = &(pSample1->m_TextureCoordData[0]);

	static const float s_flCorners[4][2] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
	for ( int i = 0; i < 4; i++ )
	{
		meshBuilder.Position3f( x, y, z );
		meshBuilder.Color4ub( rc, gc, bc, ac );
		meshBuilder.TexCoord4f( 0, pSample0->m_fLeft_U0, pSample0->m_fTop_V0, pSample0->m_fRight_U0, pSample0->m_fBottom_V0 );
		meshBuilder.TexCoord4f( 1, pSample0->m_fLeft_U1, pSample0->m_fTop_V1, pSample0->m_fRight_U1, pSample0->m_fBottom_V1 );
		meshBuilder.TexCoord4f( 2, pSample->m_fBlendFactor, rot, rad, yaw );
		meshBuilder.TexCoord2f( 3, s_flCorners[i][0], s_flCorners[i][1] );
		meshBuilder.TexCoord4f( 4, pSecondTexture0->m_fLeft_U0, pSecondTexture0->m_fTop_V0, pSecondTexture0->m_fRight_U0, pSecondTexture0->m_fBottom_V0 );
		meshBuilder.TexCoord4f( 5, pSample1Frame->m_fLeft_U0, pSample1Frame->m_fTop_V0, pSample1Frame->m_fRight_U0, pSample1Frame->m_fBottom_V0 );
		meshBuilder.TexCoord4f( 6, pSample1Frame->m_fLeft_U1, pSample1Frame->m_fTop_V1, pSample1Frame->m_fRight_U1, pSample1Frame->m_fBottom_V1 );
		meshBuilder.TexCoord3f( 7, pSample1->m_fBlendFactor, 0, 0 );
		meshBuilder.AdvanceVertexF<VTX_HAVEPOS | VTX_HAVECOLOR, 8>();
	}

	meshBuilder.FastQuad( info.m_nVertexOffset );
	info.m_nVertexOffset += 4;
}

void C_OP_RenderSprites::Render( IMatRenderContext *pRenderContext, CParticleCollection *pParticles, const Vector4D &vecDiffuseModulation, void *pContext ) const
{
	IMaterial *pMaterial = pParticles->m_pDef->GetMaterial();

	C_OP_RenderSpritesContext_t *pCtx = reinterpret_cast<C_OP_RenderSpritesContext_t *>( pContext );
	if ( pCtx->m_VisibilityData.m_bUseVisibility )
	{
		SetupParticleVisibility( pParticles, &pCtx->m_VisibilityData, &VisibilityInputs, &pCtx->m_nQueryHandle, pRenderContext );
	}

	IMaterialVar* pVar = pMaterial->FindVarFast( "$orientation", &pCtx->m_nOrientationVarToken );
	if ( pVar )
	{
		pVar->SetIntValue( clamp( m_nOrientationType, 0, 3 ) );
	}

	pRenderContext->Bind( pMaterial );

	if ( !pMaterial->IsSpriteCard() )
	{
		if ( !pCtx->m_bDidPerfWarning )
		{
			pCtx->m_bDidPerfWarning = true;
		}

		switch( m_nOrientationType )
		{
		case 0:
			if ( m_bFitCycleToLifetime )
			{
				RenderNonSpriteCardCameraFacingOld( pParticles, vecDiffuseModulation, pContext, pRenderContext, pMaterial );
			}
			else
			{
				RenderNonSpriteCardCameraFacing( pParticles, vecDiffuseModulation, pContext, pRenderContext, pMaterial );
			}
			break;

		case 1:
			RenderNonSpriteCardZRotating( pParticles, vecDiffuseModulation, pContext, pRenderContext, pMaterial );
			break;

		case 2:
			RenderNonSpriteCardOriented( pParticles, vecDiffuseModulation, pContext, pRenderContext, pMaterial );
			break;
		}
		return;
	}

	if ( m_nOrientationType == 2 )
	{
		pVar = pMaterial->FindVarFast( "$orientationMatrix", &pCtx->m_nOrientationMatrixVarToken );
		if ( pVar )
		{
			VMatrix mat;
			if ( m_nOrientationControlPoint < 0 )
			{
				MatrixSetIdentity( mat );
			}
			else
			{
				pParticles->GetControlPointTransformAtCurrentTime( m_nOrientationControlPoint, &mat );
			}
			pVar->SetMatrixValue( mat );
		}
	}

	SpriteRenderInfo_t info;
	info.Init( pParticles, 0, SEQUENCE_SAMPLE_COUNT * m_flAnimationRate, SEQUENCE_SAMPLE_COUNT * m_flAnimationRate2, pParticles->m_Sheet() );

	int nMaxParticlesInBatch = GetMaxParticlesPerBatch( pRenderContext, pMaterial, false );
	if ( pCtx->m_bPerParticleGlow )
	{
		int nParticles;
		ParticleRenderDataWithOutlineInformation_Scalar_View **pSortList = GetExtendedRenderListWithPerParticleGlow( pParticles, pRenderContext, true, &nParticles, &pCtx->m_VisibilityData );
		while ( nParticles )
		{
			int nParticlesInBatch = MIN( nMaxParticlesInBatch, nParticles );
			nParticles -= nParticlesInBatch;

			IMesh* pMesh = pRenderContext->GetDynamicMesh( true );
			CMeshBuilder meshBuilder;
			meshBuilder.Begin( pMesh, MATERIAL_TRIANGLES, nParticlesInBatch * 4, nParticlesInBatch * 6 );
			info.m_nVertexOffset = 0;
			for( int i = 0; i < nParticlesInBatch; i++ )
			{
				RenderSpriteCardNew<true, false>( meshBuilder, info, *(--pSortList) );
			}
			meshBuilder.End( );
			pMesh->DrawModulated( vecDiffuseModulation );
		}
	}
	else
	{
		int nParticles;
		ParticleFullRenderData_Scalar_View **pSortList = NULL;
		ParticleRenderDataWithNormal_Scalar_View **pSortListWithNormal = NULL;
		if ( m_nOrientationType == 3 )
		{
			pSortListWithNormal = GetExtendedRenderListWithNormals( pParticles, pRenderContext, true, &nParticles, &pCtx->m_VisibilityData );
		}
		else
		{
			pSortList = GetExtendedRenderList( pParticles, pRenderContext, true, &nParticles, &pCtx->m_VisibilityData );
		}

		while ( nParticles )
		{
			int nParticlesInBatch = MIN( nMaxParticlesInBatch, nParticles );
			nParticles -= nParticlesInBatch;

			IMesh* pMesh = pRenderContext->GetDynamicMesh( true );
			CMeshBuilder meshBuilder;
			meshBuilder.Begin( pMesh, MATERIAL_TRIANGLES, nParticlesInBatch * 4, nParticlesInBatch * 6 );
			info.m_nVertexOffset = 0;
			if ( pSortListWithNormal )
			{
				for( int i = 0; i < nParticlesInBatch; i++ )
				{
					RenderSpriteCardNew<false, true>( meshBuilder, info, *(--pSortListWithNormal) );
				}
			}
			else if ( meshBuilder.TextureCoordinateSize( 5 ) )
			{
				// The material wants the second sequence
				for( int i = 0; i < nParticlesInBatch; i++ )
				{
					--pSortList;
					RenderTwoSequenceSpriteCardNew( meshBuilder, info, *pSortList );
				}
			}
			else
			{
				for( int i = 0; i < nParticlesInBatch; i++ )
				{
					--pSortList;
					RenderSpriteCardNew<false, false>( meshBuilder, info, *pSortList );
				}
			}
			meshBuilder.End( );
			pMesh->DrawModulated( vecDiffuseModulation );
		}
	}
}

void C_OP_RenderSprites::RenderUnsorted( CParticleCollection *pParticles, void *pContext, IMatRenderContext *pRenderContext, CMeshBuilder &meshBuilder, int nVertexOffset, int nFirstParticle, int nParticleCount ) const
{
	if ( !pParticles->m_pDef->GetMaterial()->IsSpriteCard() )
	{
		switch( m_nOrientationType )
		{
		case 0:
			Warning( "C_OP_RenderSprites::RenderUnsorted: Attempting to use an unimplemented sprite renderer for system \"%s\"!\n",
				pParticles->m_pDef->GetName() );
			break;

		case 1:
			RenderUnsortedNonSpriteCardZRotating( pParticles, pContext, pRenderContext, meshBuilder, nVertexOffset, nFirstParticle, nParticleCount );
			break;

		case 2:
			RenderUnsortedNonSpriteCardOriented( pParticles, pContext, pRenderContext, meshBuilder, nVertexOffset, nFirstParticle, nParticleCount );
			break;
		}
		return;
	}

	C_OP_RenderSpritesContext_t *pCtx = reinterpret_cast<C_OP_RenderSpritesContext_t *>( pContext );

	// NOTE: shipped code ignores nVertexOffset here
	SpriteRenderInfo_t info;
	info.Init( pParticles, 0, SEQUENCE_SAMPLE_COUNT * m_flAnimationRate, SEQUENCE_SAMPLE_COUNT * m_flAnimationRate2, pParticles->m_Sheet() );

	int nParticles;
	const ParticleRenderData_t *pSortList = pParticles->GetRenderList( pRenderContext, false, &nParticles, &pCtx->m_VisibilityData );

	int hParticle = nFirstParticle;
	for ( int i = 0; i < nParticleCount; i++, hParticle++ )
	{
		RenderSpriteCard( meshBuilder, info, hParticle, pSortList );
	}
}

//-----------------------------------------------------------------------------
//
// Sprite Trail Rendering
//
//-----------------------------------------------------------------------------
struct SpriteTrailRenderInfo_t : public SpriteRenderInfo_t
{
	size_t m_nPrevXYZStride;
	const fltx4 *m_pPrevXYZ;
	size_t length_stride;
	const fltx4 *m_pLength;
	const fltx4 *m_pCreationTime;
	size_t m_nCreationTimeStride;

	void Init( CParticleCollection *pParticles, int nVertexOffset, float flAgeScale, CSheet *pSheet )
	{
		SpriteRenderInfo_t::Init( pParticles, nVertexOffset, flAgeScale, 0, pSheet );

		m_pParticles = pParticles;
		m_pPrevXYZ = pParticles->GetM128AttributePtr( PARTICLE_ATTRIBUTE_PREV_XYZ, &m_nPrevXYZStride );
		m_pLength = pParticles->GetM128AttributePtr( PARTICLE_ATTRIBUTE_TRAIL_LENGTH, &length_stride );
		m_pCreationTime = pParticles->GetM128AttributePtr( PARTICLE_ATTRIBUTE_CREATION_TIME, &m_nCreationTimeStride );
	}
};


class C_OP_RenderSpritesTrail : public CParticleRenderOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_OP_RenderSpritesTrail );

	struct C_OP_RenderSpriteTrailContext_t
	{
		CParticleVisibilityData m_VisibilityData;
		int m_nQueryHandle;
	};

	virtual uint64 GetReadControlPointMask() const
	{
		if ( VisibilityInputs.m_nCPin >= 0 )
			return 1ULL << VisibilityInputs.m_nCPin;
		return 0;
	}

	size_t GetRequiredContextBytes( ) const
	{
		return sizeof( C_OP_RenderSpriteTrailContext_t );
	}

	virtual void InitializeContextData( CParticleCollection *pParticles, void *pContext ) const
	{
		C_OP_RenderSpriteTrailContext_t *pCtx = reinterpret_cast<C_OP_RenderSpriteTrailContext_t *>( pContext );
		if ( ( VisibilityInputs.m_nCPin >= 0 ) || ( VisibilityInputs.m_flRadiusScaleFOVBase > 0 ) )
			pCtx->m_VisibilityData.m_bUseVisibility = true;
		else
			pCtx->m_VisibilityData.m_bUseVisibility = false;
	}

	uint32 GetWrittenAttributes( void ) const
	{
		return 0;
	}

	virtual void InitParams( CParticleSystemDefinition *pDef )
	{
		pDef->SetMaxTailLength( m_flMaxLength );
	}

	uint32 GetReadAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_XYZ_MASK | PARTICLE_ATTRIBUTE_PREV_XYZ_MASK | PARTICLE_ATTRIBUTE_RADIUS_MASK |
			PARTICLE_ATTRIBUTE_TINT_RGB_MASK | PARTICLE_ATTRIBUTE_ALPHA_MASK | PARTICLE_ATTRIBUTE_CREATION_TIME_MASK |
			PARTICLE_ATTRIBUTE_SEQUENCE_NUMBER_MASK | PARTICLE_ATTRIBUTE_TRAIL_LENGTH_MASK;
	}

	virtual int GetParticlesToRender( CParticleCollection *pParticles, void *pContext, int nFirstParticle, int nRemainingVertices, int nRemainingIndices, int *pVertsUsed, int *pIndicesUsed ) const;
	virtual void Render( IMatRenderContext *pRenderContext, CParticleCollection *pParticles, const Vector4D &vecDiffuseModulation, void *pContext ) const;
	virtual void RenderUnsorted( CParticleCollection *pParticles, void *pContext, IMatRenderContext *pRenderContext, CMeshBuilder &meshBuilder, int nVertexOffset, int nFirstParticle, int nParticleCount ) const;

private:
	void RenderSpriteTrail( CMeshBuilder &meshBuilder, SpriteTrailRenderInfo_t& info, int hParticle, const Vector &vecCameraPos, float flOODt, const ParticleRenderData_t *pSortList ) const;
	void RenderSpriteTrailSpriteCard( CMeshBuilder &meshBuilder, SpriteTrailRenderInfo_t& info, int hParticle, const Vector &vecCameraPos, float flOODt, const ParticleRenderData_t *pSortList ) const;

	Vector4D m_FadeColor;
	float m_flAnimationRate;
	float m_flLengthFadeInTime;
	float m_flMaxLength;
	float m_flMinLength;
	bool m_bConstrainRadius;
	bool m_bIgnoreDT;
};

DEFINE_PARTICLE_OPERATOR( C_OP_RenderSpritesTrail, "render_sprite_trail", OPERATOR_SINGLETON );

BEGIN_PARTICLE_RENDER_OPERATOR_UNPACK( C_OP_RenderSpritesTrail )
	DMXELEMENT_UNPACK_FIELD( "animation rate", ".1", float, m_flAnimationRate )
	DMXELEMENT_UNPACK_FIELD( "length fade in time", "0", float, m_flLengthFadeInTime )
	DMXELEMENT_UNPACK_FIELD( "max length", "2000", float, m_flMaxLength )
	DMXELEMENT_UNPACK_FIELD( "min length", "0", float, m_flMinLength )
	DMXELEMENT_UNPACK_FIELD( "constrain radius to length", "1", bool, m_bConstrainRadius )
	DMXELEMENT_UNPACK_FIELD( "ignore delta time", "0", bool, m_bIgnoreDT )
	DMXELEMENT_UNPACK_FIELD( "tail color and alpha scale factor", "1 1 1 1", Vector4D, m_FadeColor )
END_PARTICLE_OPERATOR_UNPACK( C_OP_RenderSpritesTrail )

int C_OP_RenderSpritesTrail::GetParticlesToRender( CParticleCollection *pParticles, void *pContext, int nFirstParticle, int nRemainingVertices, int nRemainingIndices, int *pVertsUsed, int *pIndicesUsed ) const
{
	int nMaxParticles = ( (nRemainingVertices / 4) > (nRemainingIndices / 6) ) ? nRemainingIndices / 6 : nRemainingVertices / 4;
	int nParticleCount = pParticles->m_nActiveParticles - nFirstParticle;
	if ( nParticleCount > nMaxParticles )
	{
		nParticleCount = nMaxParticles;
	}
	*pVertsUsed = nParticleCount * 4;
	*pIndicesUsed = nParticleCount * 6;
	return nParticleCount;
}

void C_OP_RenderSpritesTrail::RenderSpriteTrailSpriteCard( CMeshBuilder &meshBuilder, SpriteTrailRenderInfo_t& info, int hParticle, const Vector &vecCameraPos, float flOODt, const ParticleRenderData_t *pSortList ) const
{
	unsigned char ac = pSortList->m_nAlpha;
	if (! ac )
		return;

	int nGroup = hParticle / 4;
	int nOffset = hParticle & 0x3;

	int nColorIndex = nGroup * info.m_nRGBStride;
	float r = SubFloat( info.m_pRGB[nColorIndex], nOffset );
	float g = SubFloat( info.m_pRGB[nColorIndex+1], nOffset );
	float b = SubFloat( info.m_pRGB[nColorIndex+2], nOffset );

	unsigned char rc = SpriteFToC( r );
	unsigned char gc = SpriteFToC( g );
	unsigned char bc = SpriteFToC( b );

	float rad = pSortList->m_flRadius;

	const SheetSequenceSample_t *pSample = &s_DefaultSheetSequence;
	if ( info.m_pSheet )
	{
		pSample = GetSampleForSequence( info.m_pSheet,
			info.m_pParticles->m_flCurTime - SubFloat( info.m_pCreationTime[ nGroup * info.m_nCreationTimeStride ], nOffset ),
			info.m_flAgeScale,
			SubFloat( info.m_pSequenceNumber[ nGroup * info.m_nSequenceStride ], nOffset ) );
	}
	const SequenceSampleTextureCoords_t *pSample0 = &(pSample->m_TextureCoordData[0]);

	// Fade the trail length in
	float flAge = info.m_pParticles->m_flCurTime - SubFloat( info.m_pCreationTime[ nGroup * info.m_nCreationTimeStride ], nOffset );
	float flLengthScale = ( flAge >= m_flLengthFadeInTime ) ? 1.0 : ( flAge / m_flLengthFadeInTime );

	int nXYZIndex = nGroup * info.m_nXYZStride;
	Vector vecWorldPos( SubFloat( info.m_pXYZ[ nXYZIndex ], nOffset ), SubFloat( info.m_pXYZ[ nXYZIndex+1 ], nOffset ), SubFloat( info.m_pXYZ[ nXYZIndex+2 ], nOffset ) );

	int nPrevXYZIndex = nGroup * info.m_nPrevXYZStride;
	Vector vecPrevWorldPos( SubFloat( info.m_pPrevXYZ[ nPrevXYZIndex ], nOffset ), SubFloat( info.m_pPrevXYZ[ nPrevXYZIndex+1 ], nOffset ), SubFloat( info.m_pPrevXYZ[ nPrevXYZIndex+2 ], nOffset ) );

	// Compute the trail direction and length
	Vector vecDelta;
	VectorSubtract( vecPrevWorldPos, vecWorldPos, vecDelta );
	float flMag = VectorNormalize( vecDelta );
	float flLength = ( ( flLengthScale * SubFloat( info.m_pLength[ nGroup * info.length_stride ], nOffset ) ) * flMag ) * flOODt;
	if ( flLength <= 0.0f )
		return;

	float flClamped = ( flLength > m_flMaxLength ) ? m_flMaxLength : flLength;
	flLength = ( m_flMinLength > flClamped ) ? m_flMinLength : flClamped;

	vecDelta *= flLength;

	if ( ( rad > flLength ) && m_bConstrainRadius )
	{
		rad = flLength;
	}

	Vector p0 = vecWorldPos - vecDelta;
	Vector p2 = vecWorldPos + vecDelta;
	Vector p3;
	p3.x = vecDelta.x * 2.0f + vecWorldPos.x;
	p3.y = vecDelta.y * 2.0f + vecWorldPos.y;
	p3.z = vecDelta.z * 2.0f + vecWorldPos.z;

	Vector4D vFadeColor( ( (float)rc * m_FadeColor.x ) * ( 1.0f / 255.0f ), ( m_FadeColor.y * (float)gc ) * ( 1.0f / 255.0f ),
		( m_FadeColor.z * (float)bc ) * ( 1.0f / 255.0f ), ( m_FadeColor.w * (float)ac ) * ( 1.0f / 255.0f ) );

	for( int i = 0; i < 4; i++ )
	{
		meshBuilder.Color4ub( rc, gc, bc, ac );
		meshBuilder.Position3f( (float)( i > 1 ), (float)( i < 2 ), (float)( ( i == 1 ) || ( i == 2 ) ) );
		meshBuilder.TexCoord4f( 0, p0.x, p0.y, p0.z, rad );
		meshBuilder.TexCoord4f( 1, vecWorldPos.x, vecWorldPos.y, vecWorldPos.z, rad );
		meshBuilder.TexCoord4f( 2, p2.x, p2.y, p2.z, rad );
		meshBuilder.TexCoord4f( 3, p3.x, p3.y, p3.z, rad );
		meshBuilder.TexCoord4f( 4, pSample0->m_fLeft_U0, pSample0->m_fTop_V0, pSample0->m_fRight_U0, pSample0->m_fBottom_V0 );
		meshBuilder.TexCoord4fv( 5, vFadeColor.Base() );
		meshBuilder.AdvanceVertexF<VTX_HAVEPOS | VTX_HAVECOLOR, 6>();
	}
	meshBuilder.FastQuad( info.m_nVertexOffset );
	info.m_nVertexOffset += 4;
}

void C_OP_RenderSpritesTrail::RenderSpriteTrail( CMeshBuilder &meshBuilder, SpriteTrailRenderInfo_t& info, int hParticle, const Vector &vecCameraPos, float flOODt, const ParticleRenderData_t *pSortList ) const
{
	unsigned char ac = pSortList->m_nAlpha;
	if (! ac )
		return;

	int nGroup = hParticle / 4;
	int nOffset = hParticle & 0x3;

	int nColorIndex = nGroup * info.m_nRGBStride;
	float r = SubFloat( info.m_pRGB[nColorIndex], nOffset );
	float g = SubFloat( info.m_pRGB[nColorIndex+1], nOffset );
	float b = SubFloat( info.m_pRGB[nColorIndex+2], nOffset );

	unsigned char rc = SpriteFToC( r );
	unsigned char gc = SpriteFToC( g );
	unsigned char bc = SpriteFToC( b );

	float rad = pSortList->m_flRadius;

	const SheetSequenceSample_t *pSample = &s_DefaultSheetSequence;
	if ( info.m_pSheet )
	{
		pSample = GetSampleForSequence( info.m_pSheet,
			info.m_pParticles->m_flCurTime - SubFloat( info.m_pCreationTime[ nGroup * info.m_nCreationTimeStride ], nOffset ),
			info.m_flAgeScale,
			SubFloat( info.m_pSequenceNumber[ nGroup * info.m_nSequenceStride ], nOffset ) );
	}
	const SequenceSampleTextureCoords_t *pSample0 = &(pSample->m_TextureCoordData[0]);

	// Fade the trail length in
	float flAge = info.m_pParticles->m_flCurTime - SubFloat( info.m_pCreationTime[ nGroup * info.m_nCreationTimeStride ], nOffset );
	float flLengthScale = ( flAge >= m_flLengthFadeInTime ) ? 1.0 : ( flAge / m_flLengthFadeInTime );

	int nXYZIndex = nGroup * info.m_nXYZStride;
	Vector vecWorldPos( SubFloat( info.m_pXYZ[ nXYZIndex ], nOffset ), SubFloat( info.m_pXYZ[ nXYZIndex+1 ], nOffset ), SubFloat( info.m_pXYZ[ nXYZIndex+2 ], nOffset ) );

	int nPrevXYZIndex = nGroup * info.m_nPrevXYZStride;
	Vector vecPrevWorldPos( SubFloat( info.m_pPrevXYZ[ nPrevXYZIndex ], nOffset ), SubFloat( info.m_pPrevXYZ[ nPrevXYZIndex+1 ], nOffset ), SubFloat( info.m_pPrevXYZ[ nPrevXYZIndex+2 ], nOffset ) );

	// Compute the trail direction and length
	Vector vecDelta;
	VectorSubtract( vecPrevWorldPos, vecWorldPos, vecDelta );
	float flMag = VectorNormalize( vecDelta );
	float flLength = ( ( flLengthScale * SubFloat( info.m_pLength[ nGroup * info.length_stride ], nOffset ) ) * flMag ) * flOODt;
	if ( flLength <= 0.0f )
		return;

	float flClamped = ( flLength > m_flMaxLength ) ? m_flMaxLength : flLength;
	flLength = ( m_flMinLength > flClamped ) ? m_flMinLength : flClamped;

	vecDelta *= flLength;

	if ( ( rad > flLength ) && m_bConstrainRadius )
	{
		rad = flLength;
	}

	Vector vecViewToPos;
	VectorSubtract( vecWorldPos, vecCameraPos, vecViewToPos );

	Vector vecTangentY;
	CrossProduct( vecViewToPos, vecDelta, vecTangentY );
	float ool = FastRSqrt( ( ( vecTangentY.z * vecTangentY.z + vecTangentY.y * vecTangentY.y ) + vecTangentY.x * vecTangentY.x ) + FLT_EPSILON );
	vecTangentY.x *= ool;
	vecTangentY.y *= ool;
	vecTangentY.z *= ool;

	Vector verts[4];
	VectorMA( vecWorldPos, rad * 0.5f, vecTangentY, verts[0] );
	VectorMA( vecWorldPos, rad * -0.5f, vecTangentY, verts[1] );
	verts[2] = verts[1] + vecDelta;
	verts[3] = verts[0] + vecDelta;

	meshBuilder.Position3fv( verts[0].Base() );
	meshBuilder.Color4ub( rc, gc, bc, ac );
	meshBuilder.TexCoord2f( 0, pSample0->m_fLeft_U0, pSample0->m_fBottom_V0 );
	meshBuilder.AdvanceVertexF<VTX_HAVEPOS | VTX_HAVECOLOR, 1>();

	meshBuilder.Position3fv( verts[1].Base() );
	meshBuilder.Color4ub( rc, gc, bc, ac );
	meshBuilder.TexCoord2f( 0, pSample0->m_fRight_U0, pSample0->m_fBottom_V0 );
	meshBuilder.AdvanceVertexF<VTX_HAVEPOS | VTX_HAVECOLOR, 1>();

	meshBuilder.Position3fv( verts[2].Base() );
	meshBuilder.Color4ub( rc, gc, bc, ac );
	meshBuilder.TexCoord2f( 0, pSample0->m_fRight_U0, pSample0->m_fTop_V0 );
	meshBuilder.AdvanceVertexF<VTX_HAVEPOS | VTX_HAVECOLOR, 1>();

	meshBuilder.Position3fv( verts[3].Base() );
	meshBuilder.Color4ub( rc, gc, bc, ac );
	meshBuilder.TexCoord2f( 0, pSample0->m_fLeft_U0, pSample0->m_fTop_V0 );
	meshBuilder.AdvanceVertexF<VTX_HAVEPOS | VTX_HAVECOLOR, 1>();

	meshBuilder.FastQuad( info.m_nVertexOffset );
	info.m_nVertexOffset += 4;
}

void C_OP_RenderSpritesTrail::Render( IMatRenderContext *pRenderContext, CParticleCollection *pParticles, const Vector4D &vecDiffuseModulation, void *pContext ) const
{
	IMaterial *pMaterial = pParticles->m_pDef->GetMaterial();

	C_OP_RenderSpriteTrailContext_t *pCtx = reinterpret_cast<C_OP_RenderSpriteTrailContext_t *>( pContext );
	if ( pCtx->m_VisibilityData.m_bUseVisibility )
	{
		SetupParticleVisibility( pParticles, &pCtx->m_VisibilityData, &VisibilityInputs, &pCtx->m_nQueryHandle, pRenderContext );
	}

	bool bSpriteCard = pMaterial->IsSpriteCard();

	pRenderContext->Bind( pMaterial );

	float flAgeScale = m_flAnimationRate * SEQUENCE_SAMPLE_COUNT;

	// Right now we only have a meshbuilder version!
	Vector vecCameraPos;
	pRenderContext->GetWorldSpaceCameraPosition( &vecCameraPos );

	SpriteTrailRenderInfo_t info;
	info.Init( pParticles, 0, flAgeScale, pParticles->m_Sheet() );

	int nParticles;
	const ParticleRenderData_t *pSortList = pParticles->GetRenderList( pRenderContext, true, &nParticles, &pCtx->m_VisibilityData );

	int nMaxParticlesInBatch = GetMaxParticlesPerBatch( pRenderContext, pMaterial, false );
	float flOODt = m_bIgnoreDT ? 1.0 : ( ( pParticles->m_flDt != 0.0f ) ? ( 1.0f / pParticles->m_flDt ) : 1.0f );
	while ( nParticles )
	{
		int nParticlesInBatch = MIN( nMaxParticlesInBatch, nParticles );
		nParticles -= nParticlesInBatch;

		IMesh* pMesh = pRenderContext->GetDynamicMesh( true );
		CMeshBuilder meshBuilder;
		meshBuilder.Begin( pMesh, MATERIAL_TRIANGLES, nParticlesInBatch * 4, nParticlesInBatch * 6 );
		info.m_nVertexOffset = 0;
		if ( bSpriteCard )
		{
			for( int i = 0; i < nParticlesInBatch; i++ )
			{
				--pSortList;
				RenderSpriteTrailSpriteCard( meshBuilder, info, pSortList->m_nIndex, vecCameraPos, flOODt, pSortList );
			}
		}
		else
		{
			for( int i = 0; i < nParticlesInBatch; i++ )
			{
				--pSortList;
				RenderSpriteTrail( meshBuilder, info, pSortList->m_nIndex, vecCameraPos, flOODt, pSortList );
			}
		}
		meshBuilder.End( );
		pMesh->DrawModulated( vecDiffuseModulation );
	}
}

void C_OP_RenderSpritesTrail::RenderUnsorted( CParticleCollection *pParticles, void *pContext, IMatRenderContext *pRenderContext, CMeshBuilder &meshBuilder, int nVertexOffset, int nFirstParticle, int nParticleCount ) const
{
	C_OP_RenderSpriteTrailContext_t *pCtx = reinterpret_cast<C_OP_RenderSpriteTrailContext_t *>( pContext );

	// Right now we only have a meshbuilder version!
	Vector vecCameraPos;
	pRenderContext->GetWorldSpaceCameraPosition( &vecCameraPos );

	SpriteTrailRenderInfo_t info;
	info.Init( pParticles, nVertexOffset, m_flAnimationRate * SEQUENCE_SAMPLE_COUNT, pParticles->m_Sheet() );

	int nParticles;
	const ParticleRenderData_t *pSortList = pParticles->GetRenderList( pRenderContext, false, &nParticles, &pCtx->m_VisibilityData );

	float flOODt = m_bIgnoreDT ? 1.0 : ( ( pParticles->m_flDt != 0.0f ) ? ( 1.0f / pParticles->m_flDt ) : 1.0f );

	// NOTE: shipped code passes the end of the render list for every particle
	int hParticle = nFirstParticle;
	for ( int i = 0; i < nParticleCount; i++, hParticle++ )
	{
		RenderSpriteTrail( meshBuilder, info, hParticle, vecCameraPos, flOODt, pSortList );
	}
}

//-----------------------------------------------------------------------------
// Rope renderer
//-----------------------------------------------------------------------------
struct RopeRenderInfo_t
{
	size_t m_nXYZStride;
	const fltx4 *m_pXYZ;
	size_t m_nRadStride;
	const fltx4 *m_pRadius;
	size_t m_nRGBStride;
	const fltx4 *m_pRGB;
	size_t m_nAlphaStride;
	const fltx4 *m_pAlpha;
	size_t m_nAlpha2Stride;
	const fltx4 *m_pAlpha2;
	CParticleCollection *m_pParticles;

	void Init( CParticleCollection *pParticles )
	{
		m_pParticles = pParticles;
		m_pXYZ = pParticles->GetM128AttributePtr( PARTICLE_ATTRIBUTE_XYZ, &m_nXYZStride );
		m_pRadius = pParticles->GetM128AttributePtr( PARTICLE_ATTRIBUTE_RADIUS, &m_nRadStride );
		m_pRGB = pParticles->GetM128AttributePtr( PARTICLE_ATTRIBUTE_TINT_RGB, &m_nRGBStride );
		m_pAlpha = pParticles->GetM128AttributePtr( PARTICLE_ATTRIBUTE_ALPHA, &m_nAlphaStride );
		m_pAlpha2 = pParticles->GetM128AttributePtr( PARTICLE_ATTRIBUTE_ALPHA2, &m_nAlpha2Stride );
	}

	void GenerateSeg( int hParticle, BeamSeg_t& seg )
	{
		int nIndex = hParticle / 4;
		int nOffset = hParticle & 0x3;

		size_t xyz = nIndex * m_nXYZStride;
		size_t rgb = nIndex * m_nRGBStride;
		size_t rad = nIndex * m_nRadStride;
		size_t alpha = nIndex * m_nAlphaStride;
		size_t alpha2 = nIndex * m_nAlpha2Stride;

		seg.m_vPos.Init( SubFloat( m_pXYZ[xyz], nOffset ), SubFloat( m_pXYZ[xyz+1], nOffset ), SubFloat( m_pXYZ[xyz+2], nOffset ) );
		fltx4 fl4Alpha = MulSIMD( m_pAlpha2[alpha2], m_pAlpha[alpha] );
		seg.SetColor( SubFloat( m_pRGB[rgb], nOffset ), SubFloat( m_pRGB[rgb+1], nOffset ), SubFloat( m_pRGB[rgb+2], nOffset ), SubFloat( fl4Alpha, nOffset ) );
		seg.m_flWidth = SubFloat( m_pRadius[rad], nOffset );
	}
};

class C_OP_RenderRope : public CParticleOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_OP_RenderRope );

	struct C_OP_RenderRopeContext_t
	{
		float m_flRenderedRopeLength;
		float m_flSubdivList[1];	// actually m_nSubdivCount entries
	};

	uint32 GetWrittenAttributes( void ) const
	{
		return 0;
	}

	uint32 GetReadAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_XYZ_MASK | PARTICLE_ATTRIBUTE_RADIUS_MASK | PARTICLE_ATTRIBUTE_TINT_RGB_MASK | 
			PARTICLE_ATTRIBUTE_ALPHA_MASK | PARTICLE_ATTRIBUTE_ALPHA2_MASK;
	}

	virtual void InitializeContextData( CParticleCollection *pParticles, void *pContext ) const
	{
		C_OP_RenderRopeContext_t *pCtx = reinterpret_cast<C_OP_RenderRopeContext_t *>( pContext );
		pCtx->m_flRenderedRopeLength = 0.0f;
		for ( int i = 0; i < m_nSubdivCount; ++i )
		{
			pCtx->m_flSubdivList[i] = (float)i / (float)m_nSubdivCount;
		}

		// Compute the texture scale (NOTE: shipped code writes this member from a const method)
		IMaterial *pMaterial = pParticles->m_pDef->GetMaterial();
		float flTextureScale = 1.0f;
		if ( pMaterial )
		{
			flTextureScale = 1.0f / ( pMaterial->GetMappingHeight() * m_flTexelSizeInUnits );
		}
		const_cast<C_OP_RenderRope *>( this )->m_flTextureScale = flTextureScale;
	}

	size_t GetRequiredContextBytes( void ) const
	{
		return sizeof( float ) * ( m_nSubdivCount + 1 );
	}

	virtual void InitParams( CParticleSystemDefinition *pDef )
	{
		if ( m_nSubdivCount <= 0 )
		{
			m_nSubdivCount = 1;
		}
		if ( m_flTexelSizeInUnits <= 0 )
		{
			m_flTexelSizeInUnits = 1.0f;
		}
		m_flTStep = 1.0 / m_nSubdivCount;
		if ( ( m_bScaleByControlPointDistance || m_bScaleScrollByControlPointDistance || m_bScaleOffsetByControlPointDistance ) && m_nScaleCP1 > -1 && m_nScaleCP2 > -1 )
		{
			m_bUsesCPScaling = true;
		}
		else
		{
			m_bUsesCPScaling = false;
		}
	}

	virtual bool IsOrderImportant() const
	{
		return true;
	}

	virtual int GetParticlesToRender( CParticleCollection *pParticles, void *pContext, int nFirstParticle, int nRemainingVertices, int nRemainingIndices, int *pVertsUsed, int *pIndicesUsed ) const;
	virtual void RenderSpriteCard( CParticleCollection *pParticles, const Vector4D &vecDiffuseModulation, void *pContext, IMaterial *pMaterial ) const;
	virtual void Render( IMatRenderContext *pRenderContext, CParticleCollection *pParticles, const Vector4D &vecDiffuseModulation, void *pContext ) const;
	virtual void RenderUnsorted( CParticleCollection *pParticles, void *pContext, IMatRenderContext *pRenderContext, CMeshBuilder &meshBuilder, int nVertexOffset, int nFirstParticle, int nParticleCount ) const;

	int m_nSubdivCount;
	int m_nScaleCP1;
	int m_nScaleCP2;
	float m_flTexelSizeInUnits;
	float m_flTextureScale;
	float m_flTextureScrollRate;
	float m_flTextureOffset;
	float m_flTStep;
	bool m_bScaleByControlPointDistance;
	bool m_bScaleScrollByControlPointDistance;
	bool m_bScaleOffsetByControlPointDistance;
	bool m_bUsesCPScaling;
};

DEFINE_PARTICLE_OPERATOR( C_OP_RenderRope, "render_rope", OPERATOR_SINGLETON );

BEGIN_PARTICLE_OPERATOR_UNPACK( C_OP_RenderRope )
	DMXELEMENT_UNPACK_FIELD( "subdivision_count", "3", int, m_nSubdivCount )
	DMXELEMENT_UNPACK_FIELD( "texel_size", "4.0f", float, m_flTexelSizeInUnits )
	DMXELEMENT_UNPACK_FIELD( "texture_scroll_rate", "0.0f", float, m_flTextureScrollRate )
	DMXELEMENT_UNPACK_FIELD( "texture_offset", "0.0f", float, m_flTextureOffset )
	DMXELEMENT_UNPACK_FIELD( "scale CP start", "-1", int, m_nScaleCP1 )
	DMXELEMENT_UNPACK_FIELD( "scale CP end", "-1", int, m_nScaleCP2 )
	DMXELEMENT_UNPACK_FIELD( "scale texture by CP distance", "0", bool, m_bScaleByControlPointDistance )
	DMXELEMENT_UNPACK_FIELD( "scale scroll by CP distance", "0", bool, m_bScaleScrollByControlPointDistance )
	DMXELEMENT_UNPACK_FIELD( "scale offset by CP distance", "0", bool, m_bScaleOffsetByControlPointDistance )
END_PARTICLE_OPERATOR_UNPACK( C_OP_RenderRope )


//-----------------------------------------------------------------------------
// Returns the number of particles to render
//-----------------------------------------------------------------------------
int C_OP_RenderRope::GetParticlesToRender( CParticleCollection *pParticles, void *pContext, int nFirstParticle, int nRemainingVertices, int nRemainingIndices, int *pVertsUsed, int *pIndicesUsed ) const
{
	if ( ( nFirstParticle >= pParticles->m_nActiveParticles - 1 ) || ( pParticles->m_nActiveParticles <= 1 ) )
	{
		*pVertsUsed = 0;
		*pIndicesUsed = 0;
		return 0;
	}

	// NOTE: This is only true for particles *after* the first particle.
	// First particle takes 2 verts, no indices.
	int nVertsPerParticle = 2 * m_nSubdivCount;
	int nIndicesPerParticle = 6 * m_nSubdivCount;

	// Subtract 2 is because the first particle uses 2 verts and no indices
	int nMaxParticleCount = ( nRemainingVertices - 2 ) / nVertsPerParticle + 1;
	int nMaxParticleCountByIndices = nRemainingIndices / nIndicesPerParticle;
	if ( nMaxParticleCount > nMaxParticleCountByIndices )
	{
		nMaxParticleCount = nMaxParticleCountByIndices;
	}

	int nParticleCount = pParticles->m_nActiveParticles - nFirstParticle;

	// Never leave a single particle by itself for the next batch
	if ( nMaxParticleCount == nParticleCount - 1 )
	{
		--nMaxParticleCount;
	}

	if ( nParticleCount > nMaxParticleCount )
	{
		nParticleCount = nMaxParticleCount;
	}

	*pVertsUsed = ( nParticleCount - 1 ) * m_nSubdivCount * 2 + 2;
	*pIndicesUsed = nParticleCount * m_nSubdivCount * 6;
	return nParticleCount;
}


//-----------------------------------------------------------------------------
// Renders the rope with a spritecard material: the curve is evaluated in the vertex shader
//-----------------------------------------------------------------------------
void C_OP_RenderRope::RenderSpriteCard( CParticleCollection *pParticles, const Vector4D &vecDiffuseModulation, void *pContext, IMaterial *pMaterial ) const
{
	int nParticles = pParticles->m_nActiveParticles;

	int nSegmentsToRender = nParticles - 1;
	if ( ! nSegmentsToRender )
		return;

	CMatRenderContextPtr pRenderContext( g_pMaterialSystem );
	pRenderContext->Bind( pMaterial );

	int nMaxVertices = pRenderContext->GetMaxVerticesToRender( pMaterial );
	int nMaxIndices = pRenderContext->GetMaxIndicesToRender();

	int nNumVerticesPerSegment = 2 * m_nSubdivCount;
	int nNumIndicesPerSegment = 6 * m_nSubdivCount;

	int nNumSegmentsPerBatch = MIN( ( nMaxVertices - 2 ) / nNumVerticesPerSegment, nMaxIndices / nNumIndicesPerSegment );

	const float *pXYZ = pParticles->GetFloatAttributePtr( PARTICLE_ATTRIBUTE_XYZ, 0 );
	const float *pRadius = pParticles->GetFloatAttributePtr( PARTICLE_ATTRIBUTE_RADIUS, 0 );
	const float *pColor = pParticles->GetFloatAttributePtr( PARTICLE_ATTRIBUTE_TINT_RGB, 0 );
	const float *pAlpha = pParticles->GetFloatAttributePtr( PARTICLE_ATTRIBUTE_ALPHA, 0 );
	const float *pAlpha2 = pParticles->GetFloatAttributePtr( PARTICLE_ATTRIBUTE_ALPHA2, 0 );
	const float *pNorm = pParticles->GetFloatAttributePtr( PARTICLE_ATTRIBUTE_NORMAL, 0 );

	IMesh* pMesh = pRenderContext->GetDynamicMesh( true );
	CMeshBuilder meshBuilder;

	int nNumSegmentsIWillRenderPerBatch = MIN( nNumSegmentsPerBatch, nSegmentsToRender );

	bool bFirstPoint = true;

	float flTextureScrollRate = m_flTextureScrollRate;
	float flTexOffset = m_flTextureOffset;
	float flOffsetScaled = m_flTextureOffset / pMaterial->GetMappingHeight();
	float flTextureScale = m_flTexelSizeInUnits;

	if ( m_bUsesCPScaling )
	{
		Vector vecDelta = pParticles->GetControlPointAtCurrentTime( m_nScaleCP1 ) - pParticles->GetControlPointAtCurrentTime( m_nScaleCP2 );
		float flDist = sqrtf( ( vecDelta.z * vecDelta.z + vecDelta.x * vecDelta.x ) + vecDelta.y * vecDelta.y );
		if ( m_bScaleByControlPointDistance )
		{
			flTextureScale = 1.0f / ( flTextureScale * flDist + FLT_EPSILON );
		}
		if ( m_bScaleScrollByControlPointDistance )
		{
			flTextureScrollRate = ( ( flDist / pMaterial->GetMappingHeight() ) * flTextureScale ) * flTextureScrollRate;
		}
		if ( m_bScaleOffsetByControlPointDistance )
		{
			flOffsetScaled = ( flDist / pMaterial->GetMappingHeight() + 1.0f ) * flOffsetScaled;
		}
	}

	float flU = ( pParticles->m_flCurTime * flTextureScrollRate + flOffsetScaled ) + flTexOffset;

	// first segment
	Vector4D vecP1( pXYZ[0], pXYZ[4], pXYZ[8], pRadius[0] );
	Vector4D vecP2( pXYZ[1], pXYZ[5], pXYZ[9], pRadius[1] );
	Vector vecNorm0( pNorm[0], pNorm[4], pNorm[8] );
	Vector vecNorm1( pNorm[1], pNorm[5], pNorm[9] );

	unsigned char nRed = FastFToC( pColor[0] );
	unsigned char nGreen = FastFToC( pColor[4] );
	unsigned char nBlue = FastFToC( pColor[8] );
	unsigned char nAlpha = FastFToC( pAlpha2[0] * pAlpha[0] );

	Vector4D vecDelta01( vecP2.x - vecP1.x, vecP2.y - vecP1.y, vecP2.z - vecP1.z, vecP2.w - vecP1.w );
	Vector4D vecP0( vecP1.x - vecDelta01.x, vecP1.y - vecDelta01.y, vecP1.z - vecDelta01.z, vecP1.w - vecDelta01.w );

	Vector4D vecEndPointColor( pColor[1], pColor[5], pColor[9], pAlpha2[1] * pAlpha[1] );

	Vector4D vecP3;
	if ( nParticles < 3 )
	{
		vecP3.Init( vecDelta01.x + vecP2.x, vecDelta01.y + vecP2.y, vecDelta01.z + vecP2.z, vecDelta01.w + vecP2.w );
	}
	else
	{
		vecP3.Init( pXYZ[2], pXYZ[6], pXYZ[10], pRadius[2] );
	}

	int nPnt = 3;
	int nSegmentsAvailableInBuffer = nNumSegmentsIWillRenderPerBatch;

	int nVertexCount = nNumVerticesPerSegment * nNumSegmentsIWillRenderPerBatch + 2;
	int nIndexCount = nNumIndicesPerSegment * nNumSegmentsIWillRenderPerBatch;
	meshBuilder.Begin( pMesh, MATERIAL_TRIANGLES, nVertexCount, nIndexCount );

	int nCurrentVertex = 0;
	float flDu = m_flTStep * flTextureScale;
	float flT = 0.0f;

	do
	{
		if ( nSegmentsAvailableInBuffer == 0 )
		{
			meshBuilder.End();
			pMesh->DrawModulated( vecDiffuseModulation );
			meshBuilder.Begin( pMesh, MATERIAL_TRIANGLES, nVertexCount, nIndexCount );

			// re-emit the last two vertices
			for ( int k = 0; k < 2; k++ )
			{
				meshBuilder.Color4ub( nRed, nGreen, nBlue, nAlpha );
				meshBuilder.Position3f( flT, flU, k ? 1.0f : 0.0f );
				meshBuilder.TexCoord4f( 0, vecP0.x, vecP0.y, vecP0.z, vecP0.w );
				meshBuilder.TexCoord4f( 1, vecP1.x, vecP1.y, vecP1.z, vecP1.w );
				meshBuilder.TexCoord4f( 2, vecP2.x, vecP2.y, vecP2.z, vecP2.w );
				meshBuilder.TexCoord4f( 3, vecP3.x, vecP3.y, vecP3.z, vecP3.w );
				meshBuilder.TexCoord4f( 4, 0.0f, 0.0f, 1.0f, 1.0f );
				meshBuilder.TexCoord4f( 5, vecEndPointColor.x, vecEndPointColor.y, vecEndPointColor.z, vecEndPointColor.w );
				meshBuilder.TexCoord3f( 6, vecNorm0.x, vecNorm0.y, vecNorm0.z );
				meshBuilder.TexCoord3f( 7, vecNorm1.x, vecNorm1.y, vecNorm1.z );
				meshBuilder.AdvanceVertexF<VTX_HAVEPOS | VTX_HAVECOLOR, 8>();
			}
			nSegmentsAvailableInBuffer = nNumSegmentsIWillRenderPerBatch;
			nCurrentVertex = 0;
		}

		float dx = vecP2.x - vecP1.x;
		float dy = vecP2.y - vecP1.y;
		float dz = vecP2.z - vecP1.z;
		float flSegDu = sqrtf( ( dz * dz + dy * dy ) + dx * dx ) * flDu;

		nSegmentsAvailableInBuffer--;
		flT = 0.0f;
		for ( int nSlice = 0; nSlice < m_nSubdivCount; nSlice++ )
		{
			for ( int k = 0; k < 2; k++ )
			{
				meshBuilder.Color4ub( nRed, nGreen, nBlue, nAlpha );
				meshBuilder.Position3f( flT, flU, k ? 1.0f : 0.0f );
				meshBuilder.TexCoord4f( 0, vecP0.x, vecP0.y, vecP0.z, vecP0.w );
				meshBuilder.TexCoord4f( 1, vecP1.x, vecP1.y, vecP1.z, vecP1.w );
				meshBuilder.TexCoord4f( 2, vecP2.x, vecP2.y, vecP2.z, vecP2.w );
				meshBuilder.TexCoord4f( 3, vecP3.x, vecP3.y, vecP3.z, vecP3.w );
				meshBuilder.TexCoord4f( 4, 0.0f, 0.0f, 1.0f, 1.0f );
				meshBuilder.TexCoord4f( 5, vecEndPointColor.x, vecEndPointColor.y, vecEndPointColor.z, vecEndPointColor.w );
				meshBuilder.TexCoord3f( 6, vecNorm0.x, vecNorm0.y, vecNorm0.z );
				meshBuilder.TexCoord3f( 7, vecNorm1.x, vecNorm1.y, vecNorm1.z );
				meshBuilder.AdvanceVertexF<VTX_HAVEPOS | VTX_HAVECOLOR, 8>();
			}
			flT += m_flTStep;
			flU += flSegDu;

			if ( !bFirstPoint )
			{
				meshBuilder.FastIndex( nCurrentVertex );
				meshBuilder.FastIndex( nCurrentVertex + 1 );
				meshBuilder.FastIndex( nCurrentVertex + 2 );
				meshBuilder.FastIndex( nCurrentVertex + 1 );
				meshBuilder.FastIndex( nCurrentVertex + 3 );
				meshBuilder.FastIndex( nCurrentVertex + 2 );
				nCurrentVertex += 2;
			}
			bFirstPoint = false;
		}

		if ( nSegmentsToRender > 1 )
		{
			// advance to the next segment
			vecP0 = vecP1;
			vecP1 = vecP2;
			vecP2 = vecP3;

			nRed = FastFToC( vecEndPointColor.x );
			nGreen = FastFToC( vecEndPointColor.y );
			nBlue = FastFToC( vecEndPointColor.z );
			nAlpha = FastFToC( vecEndPointColor.w );

			vecNorm0 = vecNorm1;
			const float *pEndColor = pParticles->GetFloatAttributePtr( PARTICLE_ATTRIBUTE_TINT_RGB, nPnt - 1 );
			vecEndPointColor.Init( pEndColor[0], pEndColor[4], pEndColor[8],
				*pParticles->GetFloatAttributePtr( PARTICLE_ATTRIBUTE_ALPHA2, nPnt - 1 ) * *pParticles->GetFloatAttributePtr( PARTICLE_ATTRIBUTE_ALPHA, nPnt - 1 ) );

			if ( nPnt < nParticles )
			{
				const float *pNextXYZ = pParticles->GetFloatAttributePtr( PARTICLE_ATTRIBUTE_XYZ, nPnt );
				vecP3.Init( pNextXYZ[0], pNextXYZ[4], pNextXYZ[8], *pParticles->GetFloatAttributePtr( PARTICLE_ATTRIBUTE_RADIUS, nPnt ) );
				// NOTE: shipped code reads the normal of particle nPnt here (one ahead of the end point)
				const float *pNextNorm = pParticles->GetFloatAttributePtr( PARTICLE_ATTRIBUTE_NORMAL, nPnt );
				vecNorm1.Init( pNextNorm[0], pNextNorm[4], pNextNorm[8] );
				nPnt++;
			}
			else
			{
				// fake last point by extrapolation
				vecP3.Init( 2.0f * vecP3.x - vecP1.x, 2.0f * vecP3.y - vecP1.y, 2.0f * vecP3.z - vecP1.z, 2.0f * vecP3.w - vecP1.w );
			}
		}
	} while ( --nSegmentsToRender );

	// output last piece
	for ( int k = 0; k < 2; k++ )
	{
		meshBuilder.Color4ub( nRed, nGreen, nBlue, nAlpha );
		meshBuilder.Position3f( 1.0f, flU, k ? 1.0f : 0.0f );
		meshBuilder.TexCoord4f( 0, vecP0.x, vecP0.y, vecP0.z, vecP0.w );
		meshBuilder.TexCoord4f( 1, vecP1.x, vecP1.y, vecP1.z, vecP1.w );
		meshBuilder.TexCoord4f( 2, vecP2.x, vecP2.y, vecP2.z, vecP2.w );
		meshBuilder.TexCoord4f( 3, vecP3.x, vecP3.y, vecP3.z, vecP3.w );
		meshBuilder.TexCoord4f( 4, 0.0f, 0.0f, 1.0f, 1.0f );
		meshBuilder.TexCoord4f( 5, vecEndPointColor.x, vecEndPointColor.y, vecEndPointColor.z, vecEndPointColor.w );
		meshBuilder.TexCoord3f( 6, vecNorm0.x, vecNorm0.y, vecNorm0.z );
		meshBuilder.TexCoord3f( 7, vecNorm1.x, vecNorm1.y, vecNorm1.z );
		meshBuilder.AdvanceVertexF<VTX_HAVEPOS | VTX_HAVECOLOR, 8>();
	}
	meshBuilder.FastIndex( nCurrentVertex );
	meshBuilder.FastIndex( nCurrentVertex + 1 );
	meshBuilder.FastIndex( nCurrentVertex + 2 );
	meshBuilder.FastIndex( nCurrentVertex + 1 );
	meshBuilder.FastIndex( nCurrentVertex + 3 );
	meshBuilder.FastIndex( nCurrentVertex + 2 );

	meshBuilder.End();
	pMesh->DrawModulated( vecDiffuseModulation );
}


void C_OP_RenderRope::Render( IMatRenderContext *pRenderContext, CParticleCollection *pParticles, const Vector4D &vecDiffuseModulation, void *pContext ) const
{
	IMaterial *pMaterial = pParticles->m_pDef->GetMaterial();
	if ( pMaterial->IsSpriteCard() )
	{
		RenderSpriteCard( pParticles, vecDiffuseModulation, pContext, pMaterial );
		return;
	}

	pRenderContext->Bind( pMaterial );

	int nMaxVertices = pRenderContext->GetMaxVerticesToRender( pMaterial );
	int nMaxIndices = pRenderContext->GetMaxIndicesToRender();

	int nParticles = pParticles->m_nActiveParticles;
	int nFirstParticle = 0;
	while ( nParticles )
	{
		int nVertCount, nIndexCount;
		int nParticlesInBatch = GetParticlesToRender( pParticles, pContext, nFirstParticle, nMaxVertices, nMaxIndices, &nVertCount, &nIndexCount );
		if ( nParticlesInBatch == 0 )
			break;

		nParticles -= nParticlesInBatch;

		IMesh* pMesh = pRenderContext->GetDynamicMesh( true );
		CMeshBuilder meshBuilder;
		meshBuilder.Begin( pMesh, MATERIAL_TRIANGLES, nVertCount, nIndexCount );
		RenderUnsorted( pParticles, pContext, pRenderContext, meshBuilder, 0, nFirstParticle, nParticlesInBatch );
		meshBuilder.End();
		pMesh->DrawModulated( vecDiffuseModulation );

		nFirstParticle += nParticlesInBatch;
	}
}


void C_OP_RenderRope::RenderUnsorted( CParticleCollection *pParticles, void *pContext, IMatRenderContext *pRenderContext, CMeshBuilder &meshBuilder, int nVertexOffset, int nFirstParticle, int nParticleCount ) const
{
	IMaterial *pMaterial = pParticles->m_pDef->GetMaterial();

	// Spritecard ropes are rendered by RenderSpriteCard
	if ( pMaterial->IsSpriteCard() )
		return;

	C_OP_RenderRopeContext_t *pCtx = reinterpret_cast<C_OP_RenderRopeContext_t *>( pContext );
	if ( nFirstParticle == 0 )
	{
		pCtx->m_flRenderedRopeLength = 0.0f;
	}

	float flTexOffset = m_flTextureScrollRate;
	float flTextureScale = m_flTextureScale;

	RopeRenderInfo_t info;
	info.Init( pParticles );

	flTexOffset = pParticles->m_flCurTime * flTexOffset;
	float flOffsetScaled = m_flTextureOffset;

	if ( m_bUsesCPScaling )
	{
		Vector vecDelta = pParticles->GetControlPointAtCurrentTime( m_nScaleCP1 ) - pParticles->GetControlPointAtCurrentTime( m_nScaleCP2 );
		float flDist = sqrtf( ( vecDelta.z * vecDelta.z + vecDelta.y * vecDelta.y ) + vecDelta.x * vecDelta.x );
		if ( m_bScaleByControlPointDistance )
		{
			flTextureScale = 1.0f / ( m_flTexelSizeInUnits * flDist + FLT_EPSILON );
		}
		if ( m_bScaleScrollByControlPointDistance )
		{
			// NOTE: shipped code scales the offset here, not the scroll
			flOffsetScaled = ( flDist / pMaterial->GetMappingHeight() ) * flOffsetScaled;
		}
		if ( m_bScaleOffsetByControlPointDistance )
		{
			flOffsetScaled = ( flDist / pMaterial->GetMappingHeight() ) * m_flTextureOffset + flOffsetScaled;
		}
	}

	flTexOffset = flOffsetScaled + flTexOffset;

	CBeamSegDraw beamSegment;
	beamSegment.Start( pRenderContext, ( nParticleCount - 1 ) * m_nSubdivCount + 1, pMaterial, &meshBuilder, nVertexOffset );

	BeamSeg_t seg[2];
	info.GenerateSeg( nFirstParticle, seg[0] );
	seg[0].m_flTexCoord = ( pCtx->m_flRenderedRopeLength + flTexOffset ) * flTextureScale;
	beamSegment.NextSeg( &seg[0] );

	Vector vecCatmullRom[4];
	vecCatmullRom[1] = seg[0].m_vPos;
	if ( nFirstParticle == 0 )
	{
		vecCatmullRom[0] = vecCatmullRom[1];
	}
	else
	{
		int nGroup = ( nFirstParticle - 1 ) / 4;
		int nOffset = ( nFirstParticle - 1 ) & 0x3;
		int nXYZIndex = nGroup * info.m_nXYZStride;
		vecCatmullRom[0].Init( SubFloat( info.m_pXYZ[nXYZIndex], nOffset ), SubFloat( info.m_pXYZ[nXYZIndex+1], nOffset ), SubFloat( info.m_pXYZ[nXYZIndex+2], nOffset ) );
	}

	float flOOSubDivCount = 1.0f / m_nSubdivCount;
	int hParticle = nFirstParticle + 1;
	for ( int i = 1; i < nParticleCount; ++i, ++hParticle )
	{
		int nCurr = i & 1;
		int nPrev = 1 - nCurr;
		info.GenerateSeg( hParticle, seg[nCurr] );
		float dx = seg[nCurr].m_vPos.x - seg[nPrev].m_vPos.x;
		float dy = seg[nCurr].m_vPos.y - seg[nPrev].m_vPos.y;
		float dz = seg[nCurr].m_vPos.z - seg[nPrev].m_vPos.z;
		pCtx->m_flRenderedRopeLength += sqrtf( ( dz * dz + dy * dy ) + dx * dx );
		seg[nCurr].m_flTexCoord = ( pCtx->m_flRenderedRopeLength + flTexOffset ) * flTextureScale;

		if ( m_nSubdivCount > 1 )
		{
			vecCatmullRom[ (i+1) & 0x3 ] = seg[nCurr].m_vPos;
			if ( hParticle != info.m_pParticles->m_nActiveParticles - 1 )
			{
				int nGroup = ( hParticle + 1 ) / 4;
				int nOffset = ( hParticle + 1 ) & 0x3;
				int nXYZIndex = nGroup * info.m_nXYZStride;
				vecCatmullRom[ (i+2) & 0x3 ].Init( SubFloat( info.m_pXYZ[nXYZIndex], nOffset ), SubFloat( info.m_pXYZ[nXYZIndex+1], nOffset ), SubFloat( info.m_pXYZ[nXYZIndex+2], nOffset ) );
			}
			else
			{
				vecCatmullRom[ (i+2) & 0x3 ] = vecCatmullRom[ (i+1) & 0x3 ];
			}

			Vector4D vecColor( seg[nPrev].m_color.r * ( 1.0f / 255.0f ), seg[nPrev].m_color.g * ( 1.0f / 255.0f ), seg[nPrev].m_color.b * ( 1.0f / 255.0f ), seg[nPrev].m_color.a * ( 1.0f / 255.0f ) );
			Vector4D vecNextColor( seg[nCurr].m_color.r * ( 1.0f / 255.0f ), seg[nCurr].m_color.g * ( 1.0f / 255.0f ), seg[nCurr].m_color.b * ( 1.0f / 255.0f ), seg[nCurr].m_color.a * ( 1.0f / 255.0f ) );
			Vector4D vecColorInc( ( vecNextColor.x - vecColor.x ) * flOOSubDivCount, ( vecNextColor.y - vecColor.y ) * flOOSubDivCount,
				( vecNextColor.z - vecColor.z ) * flOOSubDivCount, ( vecNextColor.w - vecColor.w ) * flOOSubDivCount );
			float flTexcoordInc = ( seg[nCurr].m_flTexCoord - seg[nPrev].m_flTexCoord ) * flOOSubDivCount;
			float flWidthInc = ( seg[nCurr].m_flWidth - seg[nPrev].m_flWidth ) * flOOSubDivCount;

			for ( int iSubdiv = 1; iSubdiv < m_nSubdivCount; ++iSubdiv )
			{
				vecColor.x += vecColorInc.x;
				vecColor.y += vecColorInc.y;
				vecColor.z += vecColorInc.z;
				vecColor.w += vecColorInc.w;
				seg[nPrev].SetColor( vecColor );
				seg[nPrev].m_flTexCoord += flTexcoordInc;
				seg[nPrev].m_flWidth += flWidthInc;
				Catmull_Rom_Spline( vecCatmullRom[ (i-1) & 0x3 ], vecCatmullRom[ i & 0x3 ], vecCatmullRom[ (i+1) & 0x3 ], vecCatmullRom[ (i+2) & 0x3 ],
					pCtx->m_flSubdivList[iSubdiv], seg[nPrev].m_vPos );
				beamSegment.NextSeg( &seg[nPrev] );
			}
		}

		beamSegment.NextSeg( &seg[nCurr] );
	}

	beamSegment.End();
}

//-----------------------------------------------------------------------------
// render_blobs: polygonizes the particles as an implicit surface (metaballs)
// using the blobulator (src/blobulator, headers in src/common/blobulator).
//
// The blobulator headers must be included in the include block at the top of
// builtin_particle_render_ops.cpp, BEFORE tier0/memdbgon.h (SmartArray calls the CRT
// malloc/realloc/free; the shipped Render calls _malloc/_realloc directly):
//
//	#include "blobulator/implicit/imptiler.h"
//	#include "blobulator/implicit/sweeprenderer.h"
//	#include "blobulator/implicit/userfunctions.h"
//
// (needs src/common on the include path)
//-----------------------------------------------------------------------------
#ifdef PORTAL
class C_OP_RenderBlobs : public CParticleRenderOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_OP_RenderBlobs );

	struct C_OP_RenderBlobsContext_t
	{
		CParticleVisibilityData m_VisibilityData;
		int m_nQueryHandle;
	};

	virtual unsigned __int64 GetReadControlPointMask( void ) const;

	virtual unsigned int GetRequiredContextBytes( void ) const
	{
		return sizeof( C_OP_RenderBlobsContext_t );
	}

	virtual void InitializeContextData( CParticleCollection *pParticles, void *pContext ) const;

	virtual unsigned int GetWrittenAttributes( void ) const
	{
		return 0;
	}

	virtual unsigned int GetReadAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_XYZ_MASK;
	}

	virtual bool IsBatchable( void ) const
	{
		return false;
	}

	virtual void Render( IMatRenderContext *pRenderContext, CParticleCollection *pParticles, const Vector4D &vecDiffuseModulation, void *pContext ) const;

	float m_cubeWidth;		// +0x8C
	float m_cutoffRadius;	// +0x90
	float m_renderRadius;	// +0x94
};

DEFINE_PARTICLE_OPERATOR( C_OP_RenderBlobs, "render_blobs", OPERATOR_SINGLETON );

BEGIN_PARTICLE_RENDER_OPERATOR_UNPACK( C_OP_RenderBlobs )
	DMXELEMENT_UNPACK_FIELD( "cube_width", "1.0f", float, m_cubeWidth )
	DMXELEMENT_UNPACK_FIELD( "cutoff_radius", "3.3f", float, m_cutoffRadius )
	DMXELEMENT_UNPACK_FIELD( "render_radius", "1.3f", float, m_renderRadius )
END_PARTICLE_OPERATOR_UNPACK( C_OP_RenderBlobs )

unsigned __int64 C_OP_RenderBlobs::GetReadControlPointMask( void ) const
{
	unsigned __int64 nMask = 0;
	if ( VisibilityInputs.m_nCPin >= 0 )
		nMask |= 1ULL << VisibilityInputs.m_nCPin;
	return nMask;
}

void C_OP_RenderBlobs::InitializeContextData( CParticleCollection *pParticles, void *pContext ) const
{
	C_OP_RenderBlobsContext_t *pCtx = reinterpret_cast< C_OP_RenderBlobsContext_t * >( pContext );
	if ( ( VisibilityInputs.m_nCPin >= 0 ) || ( VisibilityInputs.m_flRadiusScaleFOVBase > 0 ) )
		pCtx->m_VisibilityData.m_bUseVisibility = true;
	else
		pCtx->m_VisibilityData.m_bUseVisibility = false;
}

void C_OP_RenderBlobs::Render( IMatRenderContext *pRenderContext, CParticleCollection *pParticles, const Vector4D &vecDiffuseModulation, void *pContext ) const
{
#ifdef PORTAL
	ImpTiler *tiler = ImpTilerFactory::factory->getTiler();

	C_OP_RenderBlobsContext_t *pCtx = reinterpret_cast< C_OP_RenderBlobsContext_t * >( pContext );
	if ( pCtx->m_VisibilityData.m_bUseVisibility )
	{
		SetupParticleVisibility( pParticles, &pCtx->m_VisibilityData, &VisibilityInputs, &pCtx->m_nQueryHandle, pRenderContext );
	}

	// particle storage handed to the tiler, kept between frames
	static SmartArray< ImpParticle > imp_particles_sa;

	SweepRenderer::setCubeWidth( m_cubeWidth );
	SweepRenderer::setRenderR( m_renderRadius );
	SweepRenderer::setCutoffR( m_cutoffRadius );

	SweepRenderer::setCalcSignFunc( calcSign );
	SweepRenderer::setCalcSign2Func( calcSign2 );
	SweepRenderer::setCalcCornerFunc( sizeof( CornerInfo ), calcCornerNormal );
	SweepRenderer::setCalcVertexFunc( calcVertexNormalDebugColor );

	IMaterial *pMaterial = pParticles->m_pDef->GetMaterial();

	int nParticles;
	const ParticleRenderData_t *pSortList = pParticles->GetRenderList( pRenderContext, false, &nParticles, &pCtx->m_VisibilityData );

	size_t xyz_stride;
	const fltx4 *xyz = pParticles->GetM128AttributePtr( PARTICLE_ATTRIBUTE_XYZ, &xyz_stride );

	Vector bbMin, bbMax;
	pParticles->GetBounds( &bbMin, &bbMax );
	Vector bbCenter = 0.5f * ( bbMin + bbMax );

	// set up the lighting and bind the material
	g_pParticleSystemMgr->Query()->SetUpLightingEnvironment( bbCenter );
	pRenderContext->Bind( pMaterial, &bbCenter );

	Point3D offset( 0.0f, 0.0f, 0.0f );
	tiler->beginFrame( offset, (void *)&pRenderContext, true );

	while ( imp_particles_sa.size < nParticles )
	{
		imp_particles_sa.pushAutoSize( ImpParticle() );
	}

	for ( int i = 0; i < nParticles; i++ )
	{
		int hParticle = ( --pSortList )->m_nIndex;
		int nIndex = ( hParticle / 4 ) * xyz_stride;
		int nOffset = hParticle & 0x3;

		ImpParticle *imp_particle = &imp_particles_sa[i];
		imp_particle->center[0] = SubFloat( xyz[nIndex], nOffset );
		imp_particle->center[1] = SubFloat( xyz[nIndex + 1], nOffset );
		imp_particle->center[2] = SubFloat( xyz[nIndex + 2], nOffset );
		imp_particle->setFieldScale( 1.0f );

		tiler->insertParticle( imp_particle );
	}

	tiler->drawSurface();
	tiler->endFrame();

	ImpTilerFactory::factory->returnTiler( tiler );
#endif
}
#endif

//-----------------------------------------------------------------------------
// render_screen_velocity_rotate: sets each particle's roll to face its
// screen-space direction of motion
//-----------------------------------------------------------------------------
class C_OP_RenderScreenVelocityRotate : public CParticleRenderOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_OP_RenderScreenVelocityRotate );

	struct C_OP_RenderScreenVelocityRotateContext_t
	{
		CParticleVisibilityData m_VisibilityData;
		int m_nQueryHandle;
	};

	size_t GetRequiredContextBytes( ) const
	{
		return sizeof( C_OP_RenderScreenVelocityRotateContext_t );
	}

	virtual uint64 GetReadControlPointMask() const
	{
		if ( VisibilityInputs.m_nCPin >= 0 )
			return 1ULL << VisibilityInputs.m_nCPin;
		return 0;
	}

	virtual void InitializeContextData( CParticleCollection *pParticles, void *pContext ) const
	{
		C_OP_RenderScreenVelocityRotateContext_t *pCtx = reinterpret_cast<C_OP_RenderScreenVelocityRotateContext_t *>( pContext );
		if ( ( VisibilityInputs.m_nCPin >= 0 ) || ( VisibilityInputs.m_flRadiusScaleFOVBase > 0 ) )
			pCtx->m_VisibilityData.m_bUseVisibility = true;
		else
			pCtx->m_VisibilityData.m_bUseVisibility = false;
	}

	uint32 GetWrittenAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_ROTATION_MASK;
	}

	uint32 GetReadAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_XYZ_MASK | PARTICLE_ATTRIBUTE_PREV_XYZ_MASK | PARTICLE_ATTRIBUTE_ROTATION_MASK;
	}

	virtual void Render( IMatRenderContext *pRenderContext, CParticleCollection *pParticles, const Vector4D &vecDiffuseModulation, void *pContext ) const;

	float m_flRotateRateDegrees;
	float m_flForwardDegrees;
};

DEFINE_PARTICLE_OPERATOR( C_OP_RenderScreenVelocityRotate, "render_screen_velocity_rotate", OPERATOR_SINGLETON );

BEGIN_PARTICLE_RENDER_OPERATOR_UNPACK( C_OP_RenderScreenVelocityRotate )
	DMXELEMENT_UNPACK_FIELD( "rotate_rate(dps)", "0.0f", float, m_flRotateRateDegrees )
	DMXELEMENT_UNPACK_FIELD( "forward_angle", "-90.0f", float, m_flForwardDegrees )
END_PARTICLE_OPERATOR_UNPACK( C_OP_RenderScreenVelocityRotate )

void C_OP_RenderScreenVelocityRotate::Render( IMatRenderContext *pRenderContext, CParticleCollection *pParticles, const Vector4D &vecDiffuseModulation, void *pContext ) const
{
	C_OP_RenderScreenVelocityRotateContext_t *pCtx = reinterpret_cast<C_OP_RenderScreenVelocityRotateContext_t *>( pContext );
	if ( pCtx->m_VisibilityData.m_bUseVisibility )
	{
		SetupParticleVisibility( pParticles, &pCtx->m_VisibilityData, &VisibilityInputs, &pCtx->m_nQueryHandle, pRenderContext );
	}

	VMatrix tempView;
	pRenderContext->GetMatrix( MATERIAL_VIEW, &tempView );

	int nParticles;
	const ParticleRenderData_t *pRenderList = pParticles->GetRenderList( pRenderContext, false, &nParticles, &pCtx->m_VisibilityData );

	size_t xyz_stride;
	const fltx4 *xyz = pParticles->GetM128AttributePtr( PARTICLE_ATTRIBUTE_XYZ, &xyz_stride );
	size_t prev_xyz_stride;
	const fltx4 *prev_xyz = pParticles->GetM128AttributePtr( PARTICLE_ATTRIBUTE_PREV_XYZ, &prev_xyz_stride );
	size_t rot_stride;
	fltx4 *pRot = pParticles->GetM128AttributePtrForWrite( PARTICLE_ATTRIBUTE_ROTATION, &rot_stride );

	float flForwardRadians = (float)( (double)m_flForwardDegrees * ( M_PI / 180.0 ) );

	for ( int i = 0; i < nParticles; i++ )
	{
		int hParticle = (--pRenderList)->m_nIndex;
		int nGroup = hParticle / 4;
		int nOffset = hParticle & 0x3;

		int nXYZIndex = nGroup * xyz_stride;
		float x = SubFloat( xyz[nXYZIndex], nOffset );
		float y = SubFloat( xyz[nXYZIndex+1], nOffset );
		float z = SubFloat( xyz[nXYZIndex+2], nOffset );
		float flScreenX = ( ( tempView.m[0][2] * z + tempView.m[0][1] * y ) + tempView.m[0][0] * x ) + tempView.m[0][3];
		float flScreenY = ( ( tempView.m[1][2] * z + tempView.m[1][1] * y ) + tempView.m[1][0] * x ) + tempView.m[1][3];
		if ( !IsFinite( flScreenX ) )
			continue;

		int nPrevXYZIndex = nGroup * prev_xyz_stride;
		float px = SubFloat( prev_xyz[nPrevXYZIndex], nOffset );
		float py = SubFloat( prev_xyz[nPrevXYZIndex+1], nOffset );
		float pz = SubFloat( prev_xyz[nPrevXYZIndex+2], nOffset );
		float flPrevScreenX = ( ( tempView.m[0][1] * py + tempView.m[0][2] * pz ) + tempView.m[0][0] * px ) + tempView.m[0][3];
		float flPrevScreenY = ( ( tempView.m[1][1] * py + tempView.m[1][2] * pz ) + tempView.m[1][0] * px ) + tempView.m[1][3];

		float flDX = flScreenX - flPrevScreenX;
		float flDY = flScreenY - flPrevScreenY;
		SubFloat( pRot[nGroup * rot_stride], nOffset ) = (float)atan2( (double)flDY, (double)flDX ) + flForwardRadians;
	}
}


//-----------------------------------------------------------------------------
// Render models
//-----------------------------------------------------------------------------
class C_OP_RenderModels : public CParticleRenderOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_OP_RenderModels );

	uint32 GetReadAttributes( void ) const
	{
		return ( 1 << m_nAnimationScaleField ) | PARTICLE_ATTRIBUTE_XYZ_MASK | PARTICLE_ATTRIBUTE_RADIUS_MASK |
			PARTICLE_ATTRIBUTE_ROTATION_MASK | PARTICLE_ATTRIBUTE_TINT_RGB_MASK | PARTICLE_ATTRIBUTE_ALPHA_MASK |
			PARTICLE_ATTRIBUTE_SEQUENCE_NUMBER_MASK | PARTICLE_ATTRIBUTE_YAW_MASK |
			PARTICLE_ATTRIBUTE_ALPHA2_MASK | PARTICLE_ATTRIBUTE_NORMAL_MASK;
	}

	uint32 GetWrittenAttributes( void ) const
	{
		return 0;
	}

	virtual void Precache( void );
	virtual void Render( IMatRenderContext *pRenderContext, CParticleCollection *pParticles, const Vector4D &vecDiffuseModulation, void *pContext ) const;

	char m_pszModelNames[1][256];
	void *m_pModels[1];
	bool m_bOrientZ;
	bool m_bScaleAnimationRate;
	int m_nAnimationScaleField;
	float m_flAnimationRate;
};

DEFINE_PARTICLE_OPERATOR( C_OP_RenderModels, "Render models", OPERATOR_SINGLETON );

BEGIN_PARTICLE_RENDER_OPERATOR_UNPACK( C_OP_RenderModels )
	DMXELEMENT_UNPACK_FIELD_STRING( "sequence 0 model", "NONE", m_pszModelNames[0] )
	DMXELEMENT_UNPACK_FIELD( "animation rate", "30.0", float, m_flAnimationRate )
	DMXELEMENT_UNPACK_FIELD( "scale animation rate", "0", bool, m_bScaleAnimationRate )
	DMXELEMENT_UNPACK_FIELD_USERDATA( "animation rate scale field", "10", int, m_nAnimationScaleField, "intchoice particlefield_scalar" )
	DMXELEMENT_UNPACK_FIELD( "orient model z to normal", "0", bool, m_bOrientZ )
END_PARTICLE_OPERATOR_UNPACK( C_OP_RenderModels )

void C_OP_RenderModels::Precache( void )
{
	m_pModels[0] = g_pParticleSystemMgr->Query()->GetModel( m_pszModelNames[0] );
}

void AVectorPerpendicularToVector( const Vector &in, Vector *pvecOut )
{
	pvecOut->x = Lerp( in.y * in.y, in.z, 1.0f );
	pvecOut->y = 0.0f;
	pvecOut->z = -in.x;
	VectorNormalize( *pvecOut );
	float flDot = DotProduct( *pvecOut, in );
	*pvecOut -= in * flDot;
	VectorNormalize( *pvecOut );
}

void C_OP_RenderModels::Render( IMatRenderContext *pRenderContext, CParticleCollection *pParticles, const Vector4D &vecDiffuseModulation, void *pContext ) const
{
	CParticleVisibilityData visibilityData;
	visibilityData.m_flAlphaVisibility = 1.0f;
	visibilityData.m_flRadiusVisibility = 1.0f;
	visibilityData.m_bUseVisibility = false;

	int nNumParticles;
	const ParticleRenderData_t *pRenderList = pParticles->GetRenderList( pRenderContext, false, &nNumParticles, &visibilityData );

	g_pParticleSystemMgr->Query()->BeginDrawModels( nNumParticles, pParticles->m_Center, pParticles );

	size_t xyz_stride;
	const fltx4 *xyz = pParticles->GetM128AttributePtr( PARTICLE_ATTRIBUTE_XYZ, &xyz_stride );
	size_t seq1_stride;
	const fltx4 *pSequence1Number = pParticles->GetM128AttributePtr( PARTICLE_ATTRIBUTE_SEQUENCE_NUMBER1, &seq1_stride );
	size_t rgb_stride;
	const fltx4 *pRGB = pParticles->GetM128AttributePtr( PARTICLE_ATTRIBUTE_TINT_RGB, &rgb_stride );
	size_t nAlphaStride;
	const fltx4 *pAlpha = pParticles->GetM128AttributePtr( PARTICLE_ATTRIBUTE_ALPHA, &nAlphaStride );
	size_t nAlpha2Stride;
	const fltx4 *pAlpha2 = pParticles->GetM128AttributePtr( PARTICLE_ATTRIBUTE_ALPHA2, &nAlpha2Stride );
	size_t seq_stride;
	const fltx4 *pSequenceNumber = pParticles->GetM128AttributePtr( PARTICLE_ATTRIBUTE_SEQUENCE_NUMBER, &seq_stride );
	size_t nRadStride;
	const fltx4 *pRadius = pParticles->GetM128AttributePtr( PARTICLE_ATTRIBUTE_RADIUS, &nRadStride );
	size_t nRotStride;
	const fltx4 *pRot = pParticles->GetM128AttributePtr( PARTICLE_ATTRIBUTE_ROTATION, &nRotStride );
	const fltx4 *pYaw = pParticles->GetM128AttributePtr( PARTICLE_ATTRIBUTE_YAW, &nRotStride );	// NOTE: shipped - shares the rotation stride
	size_t nScalerStride;
	const fltx4 *pAnimationScale = pParticles->GetM128AttributePtr( m_nAnimationScaleField, &nScalerStride );

	for ( int i = 0; i < nNumParticles; i++ )
	{
		int hParticle = (--pRenderList)->m_nIndex;
		int nGroup = hParticle / 4;
		int nOffset = hParticle & 0x3;

		int nSequence = (int)SubFloat( pSequenceNumber[ nGroup * seq_stride ], nOffset );
		int nAnimationSequence = (int)SubFloat( pSequence1Number[ nGroup * seq1_stride ], nOffset );

		float flAnimationRate = m_flAnimationRate;
		if ( m_bScaleAnimationRate )
		{
			flAnimationRate = SubFloat( pAnimationScale[ nGroup * nScalerStride ], nOffset ) * flAnimationRate;
		}

		int nXYZIndex = nGroup * xyz_stride;
		Vector vecWorldPos( SubFloat( xyz[ nXYZIndex ], nOffset ), SubFloat( xyz[ nXYZIndex+1 ], nOffset ), SubFloat( xyz[ nXYZIndex+2 ], nOffset ) );

		const float *pNormal = pParticles->GetFloatAttributePtr( PARTICLE_ATTRIBUTE_NORMAL, hParticle );
		Vector vecFwd( pNormal[0], pNormal[4], pNormal[8] );
		VectorNormalize( vecFwd );

		Vector vecRight;
		AVectorPerpendicularToVector( vecFwd, &vecRight );
		if ( fabs( DotProduct( vecRight, vecFwd ) ) >= 0.1f )
		{
			AVectorPerpendicularToVector( vecFwd, &vecRight );
		}
		Vector vecUp = CrossProduct( vecFwd, vecRight );

		int nRGBIndex = nGroup * rgb_stride;
		float r = SubFloat( pRGB[ nRGBIndex ], nOffset );
		float g = SubFloat( pRGB[ nRGBIndex+1 ], nOffset );
		float b = SubFloat( pRGB[ nRGBIndex+2 ], nOffset );
		float a = SubFloat( MulSIMD( pAlpha2[ nGroup * nAlpha2Stride ], pAlpha[ nGroup * nAlphaStride ] ), nOffset );

		float flScale = SubFloat( pRadius[ nGroup * nRadStride ], nOffset );

		// RAD2DEG, with the constant spelled out: the shipped compiler folded 180/pi to 57.2957802f (0x42652EE1),
		// newer compilers fold the macro's float expression to 57.2957764f
		const float flRadToDeg = 57.2957802f;
		QAngle angRotate( 0.0f, SubFloat( pYaw[ nGroup * nRotStride ], nOffset ) * flRadToDeg, SubFloat( pRot[ nGroup * nRotStride ], nOffset ) * flRadToDeg );
		ALIGN16 matrix3x4_t matRotate ALIGN16_POST;
		AngleMatrix( angRotate, matRotate );

		ALIGN16 matrix3x4_t matDir ALIGN16_POST;
		if ( m_bOrientZ )
		{
			matDir = matrix3x4_t( vecUp * flScale, -vecRight * flScale, vecFwd * flScale, vec3_origin );
		}
		else
		{
			matDir = matrix3x4_t( vecFwd * flScale, vecRight * flScale, vecUp * flScale, vec3_origin );
		}

		// the shipped build keeps these matrices 16 byte aligned, which selects mathlib's SSE ConcatTransforms path
		ALIGN16 matrix3x4_t matFinal ALIGN16_POST;
		ConcatTransforms( matDir, matRotate, matFinal );
		MatrixSetColumn( vecWorldPos, 3, matFinal );

		// NOTE: shipped code passes the sequence as the body part and always uses sub model 1
		g_pParticleSystemMgr->Query()->DrawModel( m_pModels[0], matFinal, pParticles, hParticle, nSequence, 1, nAnimationSequence, flAnimationRate, r, g, b, a );
	}

	g_pParticleSystemMgr->Query()->FinishDrawModels( pParticles );
}

//-----------------------------------------------------------------------------
// Render projected
//-----------------------------------------------------------------------------
class C_OP_RenderProjected : public CParticleRenderOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_OP_RenderProjected );

	uint32 GetReadAttributes( void ) const
	{
		return PARTICLE_ATTRIBUTE_XYZ_MASK | PARTICLE_ATTRIBUTE_RADIUS_MASK | PARTICLE_ATTRIBUTE_ROTATION_MASK |
			PARTICLE_ATTRIBUTE_TINT_RGB_MASK | PARTICLE_ATTRIBUTE_ALPHA_MASK | PARTICLE_ATTRIBUTE_PARTICLE_ID_MASK |
			PARTICLE_ATTRIBUTE_ALPHA2_MASK;
	}

	uint32 GetWrittenAttributes( void ) const
	{
		return 0;
	}

	virtual void InitializeContextData( CParticleCollection *pParticles, void *pContext ) const
	{
		*reinterpret_cast<void **>( pContext ) = NULL;
	}

	size_t GetRequiredContextBytes( ) const
	{
		return sizeof( void * );
	}

	virtual void PostSimulate( CParticleCollection *pParticles, void *pContext ) const;
	virtual void Render( IMatRenderContext *pRenderContext, CParticleCollection *pParticles, const Vector4D &vecDiffuseModulation, void *pContext ) const;
};

DEFINE_PARTICLE_OPERATOR( C_OP_RenderProjected, "Render projected", OPERATOR_SINGLETON );

BEGIN_PARTICLE_RENDER_OPERATOR_UNPACK( C_OP_RenderProjected )
END_PARTICLE_OPERATOR_UNPACK( C_OP_RenderProjected )

void C_OP_RenderProjected::PostSimulate( CParticleCollection *pParticles, void *pContext ) const
{
	IMaterial *pMaterial = pParticles->m_pDef->GetMaterial();

	if ( pParticles->m_nActiveParticles >= 1 )
	{
		// only the first particle drives the projected texture
		for ( int i = 0; i < pParticles->m_nActiveParticles; i++ )
		{
			const float *pXYZ = pParticles->GetFloatAttributePtr( PARTICLE_ATTRIBUTE_XYZ, i );
			Vector vPosition( pXYZ[0], pXYZ[4], pXYZ[8] );
			const float *pColor = pParticles->GetFloatAttributePtr( PARTICLE_ATTRIBUTE_TINT_RGB, i );
			const float *pAlpha = pParticles->GetFloatAttributePtr( PARTICLE_ATTRIBUTE_ALPHA, i );
			const float *pAlpha2 = pParticles->GetFloatAttributePtr( PARTICLE_ATTRIBUTE_ALPHA2, i );
			const float *pRadius = pParticles->GetFloatAttributePtr( PARTICLE_ATTRIBUTE_RADIUS, i );
			const float *pRotation = pParticles->GetFloatAttributePtr( PARTICLE_ATTRIBUTE_ROTATION, i );
			const int *pID = pParticles->GetIntAttributePtr( PARTICLE_ATTRIBUTE_PARTICLE_ID, i );

			g_pParticleSystemMgr->Query()->UpdateProjectedTexture( *pID, pMaterial, vPosition, *pRadius, *pRotation,
				pColor[0], pColor[4], pColor[8], *pAlpha2 * *pAlpha, *reinterpret_cast<void **>( pContext ) );
			break;
		}
	}
	else
	{
		*reinterpret_cast<void **>( pContext ) = NULL;
	}
}

void C_OP_RenderProjected::Render( IMatRenderContext *pRenderContext, CParticleCollection *pParticles, const Vector4D &vecDiffuseModulation, void *pContext ) const
{
	if ( !g_pParticleSystemMgr->Query()->IsEditor() )
		return;

	IMaterial *pMaterial = pParticles->m_pDef->GetMaterial();
	pRenderContext->Bind( pMaterial );

	if ( pParticles->m_nActiveParticles <= 0 )
		return;

	const float *pXYZ = pParticles->GetFloatAttributePtr( PARTICLE_ATTRIBUTE_XYZ, 0 );
	const float *pColor = pParticles->GetFloatAttributePtr( PARTICLE_ATTRIBUTE_TINT_RGB, 0 );
	const float *pAlpha = pParticles->GetFloatAttributePtr( PARTICLE_ATTRIBUTE_ALPHA, 0 );
	const float *pAlpha2 = pParticles->GetFloatAttributePtr( PARTICLE_ATTRIBUTE_ALPHA2, 0 );
	const float *pRadius = pParticles->GetFloatAttributePtr( PARTICLE_ATTRIBUTE_RADIUS, 0 );
	const float *pRotation = pParticles->GetFloatAttributePtr( PARTICLE_ATTRIBUTE_ROTATION, 0 );

	FlashlightState_t state;
	// NOTE: shipped code reads the position as three consecutive floats (x of particles 0..2)
	state.m_vecLightOrigin.Init( pXYZ[0], pXYZ[1], pXYZ[2] );
	float flAlpha = *pAlpha2 * *pAlpha;
	state.m_Color[0] = pColor[0] * flAlpha;
	state.m_Color[1] = pColor[4] * flAlpha;
	state.m_Color[2] = pColor[8] * flAlpha;
	state.m_Color[3] = 0.0f;
	state.m_flProjectionSize = *pRadius;
	state.m_flProjectionRotation = *pRotation;

	// NOTE: shipped code never fills in the world to texture matrix
	VMatrix WorldToTexture;
	pRenderContext->SetFlashlightState( state, WorldToTexture );

	CMeshBuilder meshBuilder;
	IMesh *pMesh = pRenderContext->GetDynamicMesh( true );
	meshBuilder.Begin( pMesh, MATERIAL_QUADS, 1 );

	meshBuilder.Position3f( -1000.0f, -1000.0f, 0.0f );
	meshBuilder.AdvanceVertexF<VTX_HAVEPOS, 0>();
	meshBuilder.Position3f( 1000.0f, -1000.0f, 0.0f );
	meshBuilder.AdvanceVertexF<VTX_HAVEPOS, 0>();
	meshBuilder.Position3f( 1000.0f, 1000.0f, 0.0f );
	meshBuilder.AdvanceVertexF<VTX_HAVEPOS, 0>();
	meshBuilder.Position3f( -1000.0f, 1000.0f, 0.0f );
	meshBuilder.AdvanceVertexF<VTX_HAVEPOS, 0>();

	meshBuilder.End();
	pMesh->DrawModulated( vecDiffuseModulation );
}


void AddBuiltInParticleRenderers( void )
{
	REGISTER_PARTICLE_OPERATOR( FUNCTION_RENDERER, C_OP_RenderSprites );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_RENDERER, C_OP_RenderSpritesTrail );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_RENDERER, C_OP_RenderRope );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_RENDERER, C_OP_RenderScreenVelocityRotate );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_RENDERER, C_OP_RenderModels );
#ifdef PORTAL
	REGISTER_PARTICLE_OPERATOR( FUNCTION_RENDERER, C_OP_RenderBlobs );
#endif
	REGISTER_PARTICLE_OPERATOR( FUNCTION_RENDERER, C_OP_RenderProjected );
}
