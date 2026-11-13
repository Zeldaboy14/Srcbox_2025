//===== Reconstructed from particles.lib (UserFunctions.obj), src/blobulator/implicit/userfunctions.cpp =====//
//
// Field function: for a particle at squared (scaled) distance d, f(d) = d^2 - d + 1/4 = (d - 1/2)^2,
// summed over all particles whose cached x range covers the voxel column.
//
// NOTE on floating point: the shipped code was compiled with /fp:fast, so sums of squares were
// re-associated differently at the various (inlined) call sites.  The order of every sum below
// follows the shipped instruction sequence.
//=============================================================================================//

#include "blobulator/implicit/userfunctions.h"
#include "blobulator/implicit/imptiler.h"
#include "blobulator/implicit/imprenderer.h"
#include "../vertexbuffers/vertexbuffer_inl.h"
#include "../util/constants.h"
#include "mathlib/ssemath.h"

// Moves the cached list pointer of column (iy,iz) to the first particle whose center can be
// within fieldCalcSteps voxels of ix, and returns it.
static inline PCacheElem_t *FindFirstCacheElem( const unsigned char ix, const unsigned char iy, const unsigned char iz,
	ProjectingParticleCache *pCache )
{
	short min_x = ix - SweepRenderer::fieldCalcSteps - 1;

	PCacheElem_t **pCell = &pCache->cachePlane[iy][iz];
	PCacheElem_t *e = *pCell;
	while ( e->prev->center_x >= min_x )
		e = e->prev;
	while ( e->next->center_x < min_x )
		e = e->next;
	*pCell = e;
	return e;
}

static inline float FieldFunction( float distSq, ImpParticle *p )
{
	distSq *= p->fieldRScaleSq;
	distSq *= SweepRenderer::scalerSq;
	return distSq * distSq + 0.25f - distSq;
}

//-----------------------------------------------------------------------------
// sign only
//-----------------------------------------------------------------------------
bool calcSign( const unsigned char ix, const unsigned char iy, const unsigned char iz,
	const float x, const float y, const float z, ProjectingParticleCache *pCache )
{
	float totalf = 0.0f;

	PCacheElem_t *e = FindFirstCacheElem( ix, iy, iz, pCache );
	short max_x = ix + SweepRenderer::fieldCalcSteps;
	for ( ; e->center_x <= max_x; e = e->next )
	{
		if ( e->bot_x > ix || e->top_x < ix )
			continue;

		ImpParticle *p = e->p;
		float dy = y - p->center[1];
		float dx = x - p->center[0];
		float dz = z - p->center[2];
		float distSq = dx * dx + dy * dy + dz * dz;

		totalf += FieldFunction( distSq, p );
		if ( totalf > SweepRenderer::threshold )
			return true;
	}
	return false;
}

bool calcSignSIMD( const unsigned char ix, const unsigned char iy, const unsigned char iz,
	const float x, const float y, const float z, ProjectingParticleCache *pCache )
{
	float totalf = 0.0f;

	PCacheElem_t *e = FindFirstCacheElem( ix, iy, iz, pCache );
	fltx4 position;
	SubFloat( position, 0 ) = x;
	SubFloat( position, 1 ) = y;
	SubFloat( position, 2 ) = z;
	SubFloat( position, 3 ) = 0.0f;

	short max_x = ix + SweepRenderer::fieldCalcSteps;
	for ( ; e->center_x <= max_x; e = e->next )
	{
		if ( e->bot_x > ix || e->top_x < ix )
			continue;

		ImpParticle *p = e->p;
		fltx4 diff = SubSIMD( position, p->center.sse_vec );
		fltx4 diffSq = MulSIMD( diff, diff );
		float distSq = SubFloat( diffSq, 2 ) + SubFloat( diffSq, 1 ) + SubFloat( diffSq, 0 );

		totalf += FieldFunction( distSq, p );
		if ( totalf > SweepRenderer::threshold )
			return true;
	}
	return false;
}

//-----------------------------------------------------------------------------
// value only
//-----------------------------------------------------------------------------
void calcSign2( const unsigned char ix, const unsigned char iy, const unsigned char iz,
	const float x, const float y, const float z, CornerInfo *const cornerInfo, ProjectingParticleCache *pCache )
{
	float totalf = 0.0f;

	PCacheElem_t *e = FindFirstCacheElem( ix, iy, iz, pCache );
	short max_x = ix + SweepRenderer::fieldCalcSteps;
	for ( ; e->center_x <= max_x; e = e->next )
	{
		if ( e->bot_x > ix || e->top_x < ix )
			continue;

		ImpParticle *p = e->p;
		float dy = y - p->center[1];
		float dx = x - p->center[0];
		float dz = z - p->center[2];
		float distSq = dx * dx + dy * dy + dz * dz;

		totalf += FieldFunction( distSq, p );
		if ( totalf > SweepRenderer::threshold )
			break;
	}

	cornerInfo->value = totalf - SweepRenderer::threshold;
	cornerInfo->y = iy;
	cornerInfo->z = iz;
}

//-----------------------------------------------------------------------------
// value + gradient
//-----------------------------------------------------------------------------
void calcCornerNormal( const unsigned char ix, const unsigned char iy, const unsigned char iz,
	const float x, const float y, const float z, CornerInfo *const cornerInfo, ProjectingParticleCache *pCache )
{
	float totalf = 0.0f;
	float nx = 0.0f, ny = 0.0f, nz = 0.0f;

	PCacheElem_t *e = FindFirstCacheElem( ix, iy, iz, pCache );
	short max_x = ix + SweepRenderer::fieldCalcSteps;
	for ( ; e->center_x <= max_x; e = e->next )
	{
		if ( e->bot_x > ix || e->top_x < ix )
			continue;

		ImpParticle *p = e->p;
		float dx = x - p->center[0];
		float dy = y - p->center[1];
		float dz = z - p->center[2];
		float distSq = dx * dx + dy * dy + dz * dz;

		float f = FieldFunction( distSq, p );
		nx += f * dx;
		ny += f * dy;
		nz += f * dz;
		totalf += f;
	}

	cornerInfo->value = totalf - SweepRenderer::threshold;
	cornerInfo->normal[0] = nx;
	cornerInfo->normal[1] = ny;
	cornerInfo->normal[2] = nz;
	IndexTriVertexBuffer::clearId( &cornerInfo->edges[0] );
	IndexTriVertexBuffer::clearId( &cornerInfo->edges[1] );
	IndexTriVertexBuffer::clearId( &cornerInfo->edges[2] );
	cornerInfo->y = iy;
	cornerInfo->z = iz;
}

//-----------------------------------------------------------------------------
// value + gradient; interpolants1 = 0.75 * the largest per-particle gradient
// contribution along each axis
//-----------------------------------------------------------------------------
void calcCornerNormalHiFreqColor( const unsigned char ix, const unsigned char iy, const unsigned char iz,
	const float x, const float y, const float z, CornerInfo *const cornerInfo, ProjectingParticleCache *pCache )
{
	float totalf = 0.0f;
	float nx = 0.0f, ny = 0.0f, nz = 0.0f;
	float max_dx = 0.0f, max_dy = 0.0f, max_dz = 0.0f;

	PCacheElem_t *e = FindFirstCacheElem( ix, iy, iz, pCache );
	short max_x = ix + SweepRenderer::fieldCalcSteps;
	for ( ; e->center_x <= max_x; e = e->next )
	{
		if ( e->bot_x > ix || e->top_x < ix )
			continue;

		ImpParticle *p = e->p;
		float dx = x - p->center[0];
		float dy = y - p->center[1];
		float dz = z - p->center[2];
		float distSq = dx * dx + dy * dy + dz * dz;

		float f = FieldFunction( distSq, p );

		float gx = f * dx;
		if ( fabsf( gx ) > max_dx )
			max_dx = fabsf( gx );
		float gy = f * dy;
		if ( fabsf( gy ) > max_dy )
			max_dy = fabsf( gy );
		float gz = f * dz;
		if ( fabsf( gz ) > max_dz )
			max_dz = fabsf( gz );

		totalf += f;
		nx += gx;
		ny += gy;
		nz += gz;
	}

	cornerInfo->value = totalf - SweepRenderer::threshold;
	cornerInfo->normal[0] = nx;
	cornerInfo->normal[1] = ny;
	cornerInfo->normal[2] = nz;
	IndexTriVertexBuffer::clearId( &cornerInfo->edges[0] );
	IndexTriVertexBuffer::clearId( &cornerInfo->edges[1] );
	IndexTriVertexBuffer::clearId( &cornerInfo->edges[2] );
	cornerInfo->y = iy;
	cornerInfo->z = iz;

	CornerInfoWithOneInterpolant *ci = (CornerInfoWithOneInterpolant *)cornerInfo;
	ci->interpolants1[0] = max_dx * 0.75f;
	ci->interpolants1[1] = max_dy * 0.75f;
	ci->interpolants1[2] = max_dz * 0.75f;
}

//-----------------------------------------------------------------------------
// value + gradient + blended interpolants1 (colour)
//-----------------------------------------------------------------------------
void calcCornerNormalColor( const unsigned char ix, const unsigned char iy, const unsigned char iz,
	const float x, const float y, const float z, CornerInfo *const cornerInfo, ProjectingParticleCache *pCache )
{
	float totalf = 0.0f;
	float nx = 0.0f, ny = 0.0f, nz = 0.0f;
	float interpolants1[3] = { 0.0f, 0.0f, 0.0f };

	PCacheElem_t *e = FindFirstCacheElem( ix, iy, iz, pCache );
	short max_x = ix + SweepRenderer::fieldCalcSteps;
	for ( ; e->center_x <= max_x; e = e->next )
	{
		if ( e->bot_x > ix || e->top_x < ix )
			continue;

		ImpParticleWithOneInterpolant *p = (ImpParticleWithOneInterpolant *)e->p;
		float dx = x - p->center[0];
		float dy = y - p->center[1];
		float dz = z - p->center[2];
		float distSq = dx * dx + dy * dy + dz * dz;

		float f = FieldFunction( distSq, p );
		nx += f * dx;
		ny += f * dy;
		nz += f * dz;
		interpolants1[0] += p->interpolants1[0] * f;
		interpolants1[1] += p->interpolants1[1] * f;
		interpolants1[2] += p->interpolants1[2] * f;
		totalf += f;
	}

	cornerInfo->value = totalf - SweepRenderer::threshold;
	cornerInfo->normal[0] = nx;
	cornerInfo->normal[1] = ny;
	cornerInfo->normal[2] = nz;
	IndexTriVertexBuffer::clearId( &cornerInfo->edges[0] );
	IndexTriVertexBuffer::clearId( &cornerInfo->edges[1] );
	IndexTriVertexBuffer::clearId( &cornerInfo->edges[2] );
	cornerInfo->y = iy;
	cornerInfo->z = iz;

	CornerInfoWithOneInterpolant *ci = (CornerInfoWithOneInterpolant *)cornerInfo;
	ci->interpolants1[0] = interpolants1[0];
	ci->interpolants1[1] = interpolants1[1];
	ci->interpolants1[2] = interpolants1[2];
}

//-----------------------------------------------------------------------------
// SIMD: value + gradient + blended interpolants1 and interpolants2 (all four lanes)
//-----------------------------------------------------------------------------
void calcCornerNormalColorTanNoUV( const unsigned char ix, const unsigned char iy, const unsigned char iz,
	const float x, const float y, const float z, CornerInfo *const cornerInfo, ProjectingParticleCache *pCache )
{
	float totalf = 0.0f;
	fltx4 normal = Four_Zeros;
	fltx4 interpolants1 = Four_Zeros;
	fltx4 interpolants2 = Four_Zeros;

	PCacheElem_t *e = FindFirstCacheElem( ix, iy, iz, pCache );
	fltx4 position;
	SubFloat( position, 0 ) = x;
	SubFloat( position, 1 ) = y;
	SubFloat( position, 2 ) = z;
	SubFloat( position, 3 ) = 0.0f;

	short max_x = ix + SweepRenderer::fieldCalcSteps;
	for ( ; e->center_x <= max_x; e = e->next )
	{
		if ( e->bot_x > ix || e->top_x < ix )
			continue;

		ImpParticleWithTwoInterpolants *p = (ImpParticleWithTwoInterpolants *)e->p;
		fltx4 diff = SubSIMD( position, p->center.sse_vec );
		fltx4 diffSq = MulSIMD( diff, diff );
		float distSq = SubFloat( diffSq, 2 ) + SubFloat( diffSq, 1 ) + SubFloat( diffSq, 0 );

		float f = FieldFunction( distSq, p );
		totalf += f;

		fltx4 f4 = ReplicateX4( f );
		normal = AddSIMD( MulSIMD( f4, diff ), normal );
		interpolants1 = AddSIMD( MulSIMD( p->interpolants1.sse_vec, f4 ), interpolants1 );
		interpolants2 = AddSIMD( MulSIMD( p->interpolants2.sse_vec, f4 ), interpolants2 );
	}

	cornerInfo->value = totalf - SweepRenderer::threshold;
	cornerInfo->normal[0] = SubFloat( normal, 0 );
	cornerInfo->normal[1] = SubFloat( normal, 1 );
	cornerInfo->normal[2] = SubFloat( normal, 2 );
	IndexTriVertexBuffer::clearId( &cornerInfo->edges[0] );
	IndexTriVertexBuffer::clearId( &cornerInfo->edges[1] );
	IndexTriVertexBuffer::clearId( &cornerInfo->edges[2] );
	cornerInfo->y = iy;
	cornerInfo->z = iz;

	CornerInfoWithTwoInterpolants *ci = (CornerInfoWithTwoInterpolants *)cornerInfo;
	ci->interpolants1.sse_vec = interpolants1;
	ci->interpolants2.sse_vec = interpolants2;
}

//-----------------------------------------------------------------------------
// SIMD: value + gradient + blended interpolants1..4.  The position has w = 1, so with
// particle centers having w = 0 the w lane of the gradient sum accumulates the field value.
//-----------------------------------------------------------------------------
void calcCornerNormalColorUVTan( const unsigned char ix, const unsigned char iy, const unsigned char iz,
	const float x, const float y, const float z, CornerInfo *const cornerInfo, ProjectingParticleCache *pCache )
{
	fltx4 normal = Four_Zeros;
	fltx4 interpolants1 = Four_Zeros;
	fltx4 interpolants2 = Four_Zeros;
	fltx4 interpolants3 = Four_Zeros;
	fltx4 interpolants4 = Four_Zeros;

	PCacheElem_t *e = FindFirstCacheElem( ix, iy, iz, pCache );
	fltx4 position;
	SubFloat( position, 0 ) = x;
	SubFloat( position, 1 ) = y;
	SubFloat( position, 2 ) = z;
	SubFloat( position, 3 ) = 1.0f;

	short max_x = ix + SweepRenderer::fieldCalcSteps;
	for ( ; e->center_x <= max_x; e = e->next )
	{
		if ( e->bot_x > ix || e->top_x < ix )
			continue;

		ImpParticleWithFourInterpolants *p = (ImpParticleWithFourInterpolants *)e->p;
		fltx4 diff = SubSIMD( position, p->center.sse_vec );
		fltx4 diffSq = MulSIMD( diff, diff );
		float distSq = SubFloat( diffSq, 2 ) + SubFloat( diffSq, 1 ) + SubFloat( diffSq, 0 );

		float f = FieldFunction( distSq, p );

		fltx4 f4 = ReplicateX4( f );
		normal = AddSIMD( MulSIMD( f4, diff ), normal );
		interpolants1 = AddSIMD( MulSIMD( p->interpolants1.sse_vec, f4 ), interpolants1 );
		interpolants2 = AddSIMD( MulSIMD( p->interpolants2.sse_vec, f4 ), interpolants2 );
		interpolants3 = AddSIMD( MulSIMD( p->interpolants3.sse_vec, f4 ), interpolants3 );
		interpolants4 = AddSIMD( MulSIMD( p->interpolants4.sse_vec, f4 ), interpolants4 );
	}

	cornerInfo->value = SubFloat( normal, 3 ) - SweepRenderer::threshold;
	cornerInfo->normal[0] = SubFloat( normal, 0 );
	cornerInfo->normal[1] = SubFloat( normal, 1 );
	cornerInfo->normal[2] = SubFloat( normal, 2 );
	IndexTriVertexBuffer::clearId( &cornerInfo->edges[0] );
	IndexTriVertexBuffer::clearId( &cornerInfo->edges[1] );
	IndexTriVertexBuffer::clearId( &cornerInfo->edges[2] );
	cornerInfo->y = iy;
	cornerInfo->z = iz;

	CornerInfoWithFourInterpolants *ci = (CornerInfoWithFourInterpolants *)cornerInfo;
	ci->interpolants1.sse_vec = interpolants1;
	ci->interpolants2.sse_vec = interpolants2;
	ci->interpolants3.sse_vec = interpolants3;
	ci->interpolants4.sse_vec = interpolants4;
}

// identical to calcCornerNormalColorUVTan in the shipped code
void calcCornerNormalColorUVTanForBlob( const unsigned char ix, const unsigned char iy, const unsigned char iz,
	const float x, const float y, const float z, CornerInfo *const cornerInfo, ProjectingParticleCache *pCache )
{
	fltx4 normal = Four_Zeros;
	fltx4 interpolants1 = Four_Zeros;
	fltx4 interpolants2 = Four_Zeros;
	fltx4 interpolants3 = Four_Zeros;
	fltx4 interpolants4 = Four_Zeros;

	PCacheElem_t *e = FindFirstCacheElem( ix, iy, iz, pCache );
	fltx4 position;
	SubFloat( position, 0 ) = x;
	SubFloat( position, 1 ) = y;
	SubFloat( position, 2 ) = z;
	SubFloat( position, 3 ) = 1.0f;

	short max_x = ix + SweepRenderer::fieldCalcSteps;
	for ( ; e->center_x <= max_x; e = e->next )
	{
		if ( e->bot_x > ix || e->top_x < ix )
			continue;

		ImpParticleWithFourInterpolants *p = (ImpParticleWithFourInterpolants *)e->p;
		fltx4 diff = SubSIMD( position, p->center.sse_vec );
		fltx4 diffSq = MulSIMD( diff, diff );
		float distSq = SubFloat( diffSq, 2 ) + SubFloat( diffSq, 1 ) + SubFloat( diffSq, 0 );

		float f = FieldFunction( distSq, p );

		fltx4 f4 = ReplicateX4( f );
		normal = AddSIMD( MulSIMD( f4, diff ), normal );
		interpolants1 = AddSIMD( MulSIMD( p->interpolants1.sse_vec, f4 ), interpolants1 );
		interpolants2 = AddSIMD( MulSIMD( p->interpolants2.sse_vec, f4 ), interpolants2 );
		interpolants3 = AddSIMD( MulSIMD( p->interpolants3.sse_vec, f4 ), interpolants3 );
		interpolants4 = AddSIMD( MulSIMD( p->interpolants4.sse_vec, f4 ), interpolants4 );
	}

	cornerInfo->value = SubFloat( normal, 3 ) - SweepRenderer::threshold;
	cornerInfo->normal[0] = SubFloat( normal, 0 );
	cornerInfo->normal[1] = SubFloat( normal, 1 );
	cornerInfo->normal[2] = SubFloat( normal, 2 );
	IndexTriVertexBuffer::clearId( &cornerInfo->edges[0] );
	IndexTriVertexBuffer::clearId( &cornerInfo->edges[1] );
	IndexTriVertexBuffer::clearId( &cornerInfo->edges[2] );
	cornerInfo->y = iy;
	cornerInfo->z = iz;

	CornerInfoWithFourInterpolants *ci = (CornerInfoWithFourInterpolants *)cornerInfo;
	ci->interpolants1.sse_vec = interpolants1;
	ci->interpolants2.sse_vec = interpolants2;
	ci->interpolants3.sse_vec = interpolants3;
	ci->interpolants4.sse_vec = interpolants4;
}

//-----------------------------------------------------------------------------
// vertex helpers
//-----------------------------------------------------------------------------

// position of the zero crossing on the edge starting at (x,y,z)
static inline float CalcEdgePosition( Point3D &position, const float x, const float y, const float z, const int edge,
	const CornerInfo *corner1, const CornerInfo *corner2 )
{
	float percent = corner1->value / ( corner1->value - corner2->value );
	position = Point3D( x, y, z );
	position[edge] += SweepRenderer::cubewidth * percent;
	return percent;
}

static inline void LerpNormal( Point3D &normal, float percent, float omPercent, const CornerInfo *corner1, const CornerInfo *corner2 )
{
	normal[0] = corner2->normal[0] * percent + corner1->normal[0] * omPercent;
	normal[1] = corner2->normal[1] * percent + corner1->normal[1] * omPercent;
	normal[2] = corner2->normal[2] * percent + corner1->normal[2] * omPercent;
}

// normalize with the sum ordered y, z, x
static inline void NormalizeYZX( Point3D &v )
{
	float len = sqrtf( ( v[1] * v[1] + v[2] * v[2] ) + v[0] * v[0] );
	if ( len == 0.0f )
		len = 1.0f;
	float inv = 1.0f / len;
	v[0] = inv * v[0];
	v[1] = inv * v[1];
	v[2] = inv * v[2];
}

// normalize with the sum ordered z, y, x
static inline void NormalizeZYX( Point3D &v )
{
	float len = sqrtf( ( v[2] * v[2] + v[1] * v[1] ) + v[0] * v[0] );
	if ( len == 0.0f )
		len = 1.0f;
	float inv = 1.0f / len;
	v[0] = inv * v[0];
	v[1] = inv * v[1];
	v[2] = inv * v[2];
}

//-----------------------------------------------------------------------------
// vertex: position + interpolated (unnormalized) gradient
//-----------------------------------------------------------------------------
void calcVertexNormal( const float x, const float y, const float z, const int edge,
	const CornerInfo *corner1, const CornerInfo *corner2, IndexTriVertexBuffer *vertexBuffer )
{
	Point3D position;
	float percent = CalcEdgePosition( position, x, y, z, edge, corner1, corner2 );
	vertexBuffer->setVertexPosition( position );

	float omPercent = 1.0f - percent;
	Point3D normal;
	LerpNormal( normal, percent, omPercent, corner1, corner2 );
	vertexBuffer->setVertexNormal( normal );
}

//-----------------------------------------------------------------------------
// vertex: position + normal + the tile's debug colour
//-----------------------------------------------------------------------------
void calcVertexNormalDebugColor( const float x, const float y, const float z, const int edge,
	const CornerInfo *corner1, const CornerInfo *corner2, IndexTriVertexBuffer *vertexBuffer )
{
	Point3D position;
	float percent = CalcEdgePosition( position, x, y, z, edge, corner1, corner2 );
	vertexBuffer->setVertexPosition( position );

	float omPercent = 1.0f - percent;
	Point3D normal;
	LerpNormal( normal, percent, omPercent, corner1, corner2 );
	NormalizeYZX( normal );
	vertexBuffer->setVertexNormal( normal );

	vertexBuffer->setVertexColor( tile_debug_color );
}

//-----------------------------------------------------------------------------
// vertex: position + gradient + colour from interpolants1
//-----------------------------------------------------------------------------
void calcVertexNormalNColor( const float x, const float y, const float z, const int edge,
	const CornerInfo *corner1, const CornerInfo *corner2, IndexTriVertexBuffer *vertexBuffer )
{
	Point3D position;
	float percent = CalcEdgePosition( position, x, y, z, edge, corner1, corner2 );
	vertexBuffer->setVertexPosition( position );

	float omPercent = 1.0f - percent;
	Point3D normal;
	LerpNormal( normal, percent, omPercent, corner1, corner2 );
	vertexBuffer->setVertexNormal( normal );

	// the corner sums are weighted by the field, divide by the threshold (the field at the surface)
	float w1 = SweepRenderer::oneOverThreshold * omPercent;
	float w2 = SweepRenderer::oneOverThreshold * percent;

	const CornerInfoWithOneInterpolant *c1 = (const CornerInfoWithOneInterpolant *)corner1;
	const CornerInfoWithOneInterpolant *c2 = (const CornerInfoWithOneInterpolant *)corner2;
	Point3D color;
	color[0] = c1->interpolants1[0] * w1 + c2->interpolants1[0] * w2;
	color[1] = c1->interpolants1[1] * w1 + c2->interpolants1[1] * w2;
	color[2] = c1->interpolants1[2] * w1 + c2->interpolants1[2] * w2;
	vertexBuffer->setVertexColor( color );
}

//-----------------------------------------------------------------------------
// vertex: position + gradient + colour + tangent from interpolants2, no UVs
//-----------------------------------------------------------------------------
void calcVertexNormalNColorTanNoUV( const float x, const float y, const float z, const int edge,
	const CornerInfo *_corner1, const CornerInfo *_corner2, IndexTriVertexBuffer *vertexBuffer )
{
	const CornerInfoWithTwoInterpolants *corner1 = (const CornerInfoWithTwoInterpolants *)_corner1;
	const CornerInfoWithTwoInterpolants *corner2 = (const CornerInfoWithTwoInterpolants *)_corner2;

	Point3D position;
	float percent = CalcEdgePosition( position, x, y, z, edge, corner1, corner2 );
	vertexBuffer->setVertexPosition( position );

	float omPercent = 1.0f - percent;
	Point3D normal;
	LerpNormal( normal, percent, omPercent, corner1, corner2 );
	vertexBuffer->setVertexNormal( normal );

	float w2 = SweepRenderer::oneOverThreshold * percent;
	float w1 = SweepRenderer::oneOverThreshold * omPercent;

	Point3D color;
	color[0] = corner1->interpolants1[0] * w1 + corner2->interpolants1[0] * w2;
	color[1] = corner1->interpolants1[1] * w1 + corner2->interpolants1[1] * w2;
	color[2] = corner1->interpolants1[2] * w1 + corner2->interpolants1[2] * w2;
	vertexBuffer->setVertexColor( color );

	Point3D interpolants2;
	interpolants2.sse_vec = AddSIMD( MulSIMD( ReplicateX4( w2 ), corner2->interpolants2.sse_vec ),
									 MulSIMD( ReplicateX4( w1 ), corner1->interpolants2.sse_vec ) );

	NormalizeYZX( normal );
	NormalizeYZX( interpolants2 );

	vertexBuffer->setVertexUV( 0, 0.0f, 0.0f );

	Point3D tangent = interpolants2.crossProduct( normal );
	tangent[3] = 1.0f;
	vertexBuffer->setVertexTangent( tangent );
}

//-----------------------------------------------------------------------------
// vertex: position + gradient + colour + spherical UV from interpolants3/4 + tangent
//-----------------------------------------------------------------------------
void calcVertexNormalNColorUVTan( const float x, const float y, const float z, const int edge,
	const CornerInfo *_corner1, const CornerInfo *_corner2, IndexTriVertexBuffer *vertexBuffer )
{
	const CornerInfoWithFourInterpolants *corner1 = (const CornerInfoWithFourInterpolants *)_corner1;
	const CornerInfoWithFourInterpolants *corner2 = (const CornerInfoWithFourInterpolants *)_corner2;

	Point3D position;
	float percent = CalcEdgePosition( position, x, y, z, edge, corner1, corner2 );
	vertexBuffer->setVertexPosition( position );

	float omPercent = 1.0f - percent;
	Point3D normal;
	LerpNormal( normal, percent, omPercent, corner1, corner2 );
	vertexBuffer->setVertexNormal( normal );

	float w2 = SweepRenderer::oneOverThreshold * percent;
	float w1 = SweepRenderer::oneOverThreshold * omPercent;
	fltx4 w1_4 = ReplicateX4( w1 );
	fltx4 w2_4 = ReplicateX4( w2 );

	Point3D color;
	color.sse_vec = AddSIMD( MulSIMD( corner2->interpolants1.sse_vec, w2_4 ), MulSIMD( corner1->interpolants1.sse_vec, w1_4 ) );
	vertexBuffer->setVertexColor( color );

	Point3D interpolants2, interpolants3, interpolants4;
	interpolants2.sse_vec = AddSIMD( MulSIMD( corner2->interpolants2.sse_vec, w2_4 ), MulSIMD( corner1->interpolants2.sse_vec, w1_4 ) );
	interpolants3.sse_vec = AddSIMD( MulSIMD( corner2->interpolants3.sse_vec, w2_4 ), MulSIMD( corner1->interpolants3.sse_vec, w1_4 ) );
	interpolants4.sse_vec = AddSIMD( MulSIMD( corner2->interpolants4.sse_vec, w2_4 ), MulSIMD( corner1->interpolants4.sse_vec, w1_4 ) );

	NormalizeYZX( normal );

	float u = interpolants3[1] * normal[1] + interpolants3[2] * normal[2] + interpolants3[0] * normal[0];
	float v = interpolants4[1] * normal[1] + interpolants4[2] * normal[2] + interpolants4[0] * normal[0];
	float s = (float)atan2( (double)u, (double)v ) * BLOB_ONE_OVER_TWO_PI + 0.5f;
	vertexBuffer->setVertexUV( 0, s, color[3] );

	Point3D tangent = interpolants2.crossProduct( normal );
	tangent[3] = 1.0f;
	vertexBuffer->setVertexTangent( tangent );
}

// identical to calcVertexNormalNColorUVTan in the shipped code
void calcVertexNormalNColorUVTanForBlob( const float x, const float y, const float z, const int edge,
	const CornerInfo *_corner1, const CornerInfo *_corner2, IndexTriVertexBuffer *vertexBuffer )
{
	const CornerInfoWithFourInterpolants *corner1 = (const CornerInfoWithFourInterpolants *)_corner1;
	const CornerInfoWithFourInterpolants *corner2 = (const CornerInfoWithFourInterpolants *)_corner2;

	Point3D position;
	float percent = CalcEdgePosition( position, x, y, z, edge, corner1, corner2 );
	vertexBuffer->setVertexPosition( position );

	float omPercent = 1.0f - percent;
	Point3D normal;
	LerpNormal( normal, percent, omPercent, corner1, corner2 );
	vertexBuffer->setVertexNormal( normal );

	float w2 = SweepRenderer::oneOverThreshold * percent;
	float w1 = SweepRenderer::oneOverThreshold * omPercent;
	fltx4 w1_4 = ReplicateX4( w1 );
	fltx4 w2_4 = ReplicateX4( w2 );

	Point3D color;
	color.sse_vec = AddSIMD( MulSIMD( corner2->interpolants1.sse_vec, w2_4 ), MulSIMD( corner1->interpolants1.sse_vec, w1_4 ) );
	vertexBuffer->setVertexColor( color );

	Point3D interpolants2, interpolants3, interpolants4;
	interpolants2.sse_vec = AddSIMD( MulSIMD( corner2->interpolants2.sse_vec, w2_4 ), MulSIMD( corner1->interpolants2.sse_vec, w1_4 ) );
	interpolants3.sse_vec = AddSIMD( MulSIMD( corner2->interpolants3.sse_vec, w2_4 ), MulSIMD( corner1->interpolants3.sse_vec, w1_4 ) );
	interpolants4.sse_vec = AddSIMD( MulSIMD( corner2->interpolants4.sse_vec, w2_4 ), MulSIMD( corner1->interpolants4.sse_vec, w1_4 ) );

	NormalizeYZX( normal );

	float u = interpolants3[1] * normal[1] + interpolants3[2] * normal[2] + interpolants3[0] * normal[0];
	float v = interpolants4[1] * normal[1] + interpolants4[2] * normal[2] + interpolants4[0] * normal[0];
	float s = (float)atan2( (double)u, (double)v ) * BLOB_ONE_OVER_TWO_PI + 0.5f;
	vertexBuffer->setVertexUV( 0, s, color[3] );

	Point3D tangent = interpolants2.crossProduct( normal );
	tangent[3] = 1.0f;
	vertexBuffer->setVertexTangent( tangent );
}

//-----------------------------------------------------------------------------
// vertex: position + normal + interpolants1 as a 4D texture coordinate
//-----------------------------------------------------------------------------
void calcVertexNormalNTexCoord4( const float x, const float y, const float z, const int edge,
	const CornerInfo *_corner1, const CornerInfo *_corner2, IndexTriVertexBuffer *vertexBuffer )
{
	const CornerInfoWithOneInterpolant *corner1 = (const CornerInfoWithOneInterpolant *)_corner1;
	const CornerInfoWithOneInterpolant *corner2 = (const CornerInfoWithOneInterpolant *)_corner2;

	Point3D position;
	float percent = CalcEdgePosition( position, x, y, z, edge, corner1, corner2 );
	vertexBuffer->setVertexPosition( position );

	float omPercent = 1.0f - percent;
	Point3D normal;
	LerpNormal( normal, percent, omPercent, corner1, corner2 );
	NormalizeZYX( normal );
	vertexBuffer->setVertexNormal( normal );

	float w2 = SweepRenderer::oneOverThreshold * percent;
	float w1 = SweepRenderer::oneOverThreshold * omPercent;

	Point3D interpolants1;
	interpolants1.sse_vec = AddSIMD( MulSIMD( ReplicateX4( w2 ), corner2->interpolants1.sse_vec ),
									 MulSIMD( ReplicateX4( w1 ), corner1->interpolants1.sse_vec ) );
	vertexBuffer->setVertexUV( 0, interpolants1[0], interpolants1[1], interpolants1[2], interpolants1[3] );
}
