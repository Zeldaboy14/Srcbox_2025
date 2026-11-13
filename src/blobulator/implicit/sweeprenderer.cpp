//===== Reconstructed from particles.lib (SweepRenderer.obj), src/blobulator/implicit/sweeprenderer.cpp =====//

#include "blobulator/implicit/sweeprenderer.h"
#include "blobulator/util/fasttrunc.h"
#include "../vertexbuffers/vertexbuffer_inl.h"
#include "../util/constants.h"
#include "tier0/dbg.h"
#include "tier0/vprof.h"

#include "impconsttable_winding_ccw2_tris.h"

float SweepRenderer::cubewidth = 0.8f;
float SweepRenderer::oneOverCubewidth = 1.25f;
int SweepRenderer::cornerInfoSize = sizeof( CornerInfo );
float SweepRenderer::cutoffR = 3.3f;
float SweepRenderer::renderR = 1.3f;
float SweepRenderer::cutoffRSq = cutoffR * cutoffR;
float SweepRenderer::scaler = 1.0f / ( sqrtf( 2.0f ) * cutoffR );
float SweepRenderer::scalerSq = scaler * scaler;
float SweepRenderer::scaledRenderRSq = scalerSq * renderR * renderR;
float SweepRenderer::threshold = scaledRenderRSq * scaledRenderRSq + 0.25f - scaledRenderRSq;
float SweepRenderer::oneOverThreshold = 1.0f / threshold;
float SweepRenderer::addAmt = 0.25f;
short SweepRenderer::fieldCalcSteps = (short)( oneOverCubewidth * cutoffR );

bool SweepRenderer::tile_mode;
int SweepRenderer::marginNCubes;
float SweepRenderer::marginWidth;
Point3D SweepRenderer::outerDimensions;
Point3D SweepRenderer::innerDimensions;

static CalcSignFunc_t calc_sign;
static CalcCornerFunc_t calc_sign2;
static CalcCornerFunc_t calc_corner;
static CalcVertexFunc_t calc_vertex;

//-----------------------------------------------------------------------------
// user function hooks
//-----------------------------------------------------------------------------
void SweepRenderer::setCalcSignFunc( CalcSignFunc_t calc_sign_ptr )
{
	calc_sign = calc_sign_ptr;
}

void SweepRenderer::setCalcSign2Func( CalcCornerFunc_t calc_sign2_ptr )
{
	calc_sign2 = calc_sign2_ptr;
}

void SweepRenderer::setCalcCornerFunc( int _cornerInfoSize, CalcCornerFunc_t calc_corner_ptr )
{
	calc_corner = calc_corner_ptr;

	// keep corner infos 16 byte aligned
	cornerInfoSize = ( ( _cornerInfoSize + 15 ) / 16 ) * 16;
}

void SweepRenderer::setCalcVertexFunc( CalcVertexFunc_t calc_vertex_ptr )
{
	calc_vertex = calc_vertex_ptr;
}

//-----------------------------------------------------------------------------
// global settings
//-----------------------------------------------------------------------------
void SweepRenderer::setCubeWidth( float _cubewidth )
{
	if ( _cubewidth != cubewidth )
	{
		changeCubeWidth( _cubewidth );
		recalculateDimensions();
	}
}

void SweepRenderer::setCutoffR( float _cutoffR )
{
	if ( _cutoffR != cutoffR )
	{
		changeRadii( _cutoffR, renderR );
		recalculateDimensions();
	}
}

void SweepRenderer::setRenderR( float _renderR )
{
	if ( _renderR != renderR )
	{
		changeRadii( cutoffR, _renderR );
		recalculateDimensions();
	}
}

float SweepRenderer::getCubeWidth()
{
	return cubewidth;
}

float SweepRenderer::getCutoffR()
{
	return cutoffR;
}

float SweepRenderer::getRenderR()
{
	return renderR;
}

int SweepRenderer::getMarginNCubes()
{
	return marginNCubes;
}

float SweepRenderer::getMarginWidth()
{
	return marginWidth;
}

Point3D &SweepRenderer::getInnerDimensions()
{
	return innerDimensions;
}

void SweepRenderer::changeCubeWidth( float _cubewidth )
{
	cubewidth = _cubewidth;
	oneOverCubewidth = 1.0f / _cubewidth;
}

void SweepRenderer::changeRadii( float _cutoffR, float _renderR )
{
	if ( _renderR >= _cutoffR )
		_renderR = _cutoffR;

	cutoffR = _cutoffR;
	renderR = _renderR;

	cutoffRSq = _cutoffR * _cutoffR;
	scaler = 1.0f / ( sqrtf( 2.0f ) * _cutoffR );
	scalerSq = scaler * scaler;
	scaledRenderRSq = scalerSq * _renderR * _renderR;
	threshold = scaledRenderRSq * scaledRenderRSq + 0.25f - scaledRenderRSq;
	oneOverThreshold = 1.0f / threshold;
	addAmt = 0.25f;
}

void SweepRenderer::recalculateDimensions()
{
	fieldCalcSteps = (short)( cutoffR * oneOverCubewidth );

	marginNCubes = blob_round_up( cutoffR / cubewidth + 0.001f );
	marginWidth = marginNCubes * cubewidth;

	outerDimensions.set( xsize * cubewidth, ysize * cubewidth, zsize * cubewidth );
	innerDimensions.set( ( xsize - 2 * marginNCubes ) * cubewidth, ( ysize - 2 * marginNCubes ) * cubewidth,
						 ( zsize - 2 * marginNCubes ) * cubewidth );
}

//-----------------------------------------------------------------------------
// construction
//-----------------------------------------------------------------------------
SweepRenderer::SweepRenderer()
{
	maxNoSlicesToDraw = -1;
	n_alloced_slice_corners = 0;
	n_alloced_slice_corner_infos = 0;
	n_alloced_slice_todo_lists = 0;

	pCache = new ProjectingParticleCache;
	vertexBuffer = new IndexTriVertexBuffer;

	static bool firstTime = true;
	if ( firstTime )
	{
		recalculateDimensions();
		firstTime = false;
	}

	for ( int i = 0; i < xsizeP2; i++ )
	{
		slices[i].corners = NULL;
	}
}

void SweepRenderer::recalculateBB()
{
	outerBBMins = offset - outerDimensions * 0.5f;
	outerBBMaxs = offset + outerDimensions * 0.5f;

	innerBBMins = offset - innerDimensions * 0.5f;
	innerBBMaxs = offset + innerDimensions * 0.5f;
}

void SweepRenderer::setOffset( Point3D &_offset )
{
	offset = _offset;
	recalculateBB();
}

void SweepRenderer::beginFrame( bool _tile_mode, void *pRenderContext )
{
	VPROF_BUDGET( "IsoSurface", "Blobulator" );

	tile_mode = _tile_mode;
	vertexBuffer->beginFrame( pRenderContext );
}

void SweepRenderer::beginTile( ImpTile *tile )
{
	VPROF_BUDGET( "IsoSurface", "Blobulator" );

	pCache->beginTile();
}

bool SweepRenderer::isParticleWithinBounds( ImpParticle *p )
{
	return p->center[0] > outerBBMins[0] && p->center[0] < outerBBMaxs[0] &&
		   p->center[1] > outerBBMins[1] && p->center[1] < outerBBMaxs[1] &&
		   p->center[2] > outerBBMins[2] && p->center[2] < outerBBMaxs[2];
}

void SweepRenderer::addParticle( ImpParticle *p, bool skipBoundsCheck )
{
	if ( skipBoundsCheck || isParticleWithinBounds( p ) )
	{
		particles.pushAutoSize( p );
		pCache->addParticle( p );
	}
}

void SweepRenderer::endTile()
{
	VPROF_BUDGET( "IsoSurface", "Blobulator" );

	pCache->buildCache( cubewidth, cutoffR, outerBBMins, outerBBMaxs );

	// find a surface cube next to every particle
	for ( int i = 0; i < particles.size; i++ )
	{
		seed_surface( particles[i]->center );
	}

	render_slices();

	particles.size = 0;
	pCache->endTile();
}

void SweepRenderer::endFrame()
{
	VPROF_BUDGET( "IsoSurface", "Blobulator" );

	vertexBuffer->endFrame();
}

//-----------------------------------------------------------------------------
// Walks from the cube containing the particle towards -x until it finds a cube
// that is outside the surface and adds it as a seed for the slice sweep.
//-----------------------------------------------------------------------------
void SweepRenderer::seed_surface( Point3D &_center )
{
	float fx = ( _center[0] - outerBBMins[0] ) * oneOverCubewidth;
	float fy = ( _center[1] - outerBBMins[1] ) * oneOverCubewidth;
	float fz = ( _center[2] - outerBBMins[2] ) * oneOverCubewidth;

	int start_x = blob_round( fx );
	if ( start_x < 0 )
		start_x = 0;
	else if ( start_x >= xsize )
		start_x = xsize - 1;

	int start_y = blob_round( fy );
	if ( start_y < 0 )
		start_y = 0;
	else if ( start_y >= ysize )
		start_y = ysize - 1;

	int start_z = blob_round( fz );
	if ( start_z < 0 )
		start_z = 0;
	else if ( start_z >= zsize )
		start_z = zsize - 1;

	float start_fy = (float)start_y;
	float start_fz = (float)start_z;

	int x = start_x;
	for ( int iter = 0; iter < fieldCalcSteps + 2; iter++ )
	{
		// x87 in the shipped code: one rounding of x*cubewidth + mins to float
		float px = (float)( (double)x * cubewidth + outerBBMins[0] );
		float py = (float)( (double)start_fy * cubewidth + outerBBMins[1] );
		float pz = (float)( (double)start_fz * cubewidth + outerBBMins[2] );

		if ( !calc_sign( x, start_y, start_z, px, py, pz, pCache ) )
		{
			YZ todo;
			todo.y = start_y;
			todo.z = start_z;
			slices[x + 1].seed_list.pushAutoSize( todo );
			return;
		}
		x--;
	}
}

//-----------------------------------------------------------------------------
// slice memory management
//-----------------------------------------------------------------------------
void SweepRenderer::allocSliceCorners( Slice_t *slice )
{
	if ( slice->corners == NULL )
	{
		if ( unused_slice_corners.size > 0 )
		{
			slice->corners = unused_slice_corners.pop();
		}
		else
		{
			// NOTE: shipped code sizes the elements as sizeof(CornerInfo), not sizeof(CubeInfo)
			slice->corners = ( CubeInfo( * )[zsizeP1] )calloc( ysizeP1 * zsizeP1, sizeof( CornerInfo ) );
			n_alloced_slice_corners++;
			ConMsg( "Allocated %d slice corner arrays\n", n_alloced_slice_corners );
		}
	}

	if ( slice->corner_info.capacity == 0 )
	{
		if ( unused_corner_info.size > 0 )
		{
			slice->corner_info = unused_corner_info.pop();
		}
		else
		{
			slice->corner_info.resize( cornerInfoSize * 16 );
			slice->corner_info.size = 1;	// corner info number 0 means "not computed"
			n_alloced_slice_corner_infos++;
			ConMsg( "Allocated %d slice corner info arrays\n", n_alloced_slice_corner_infos );
		}
	}
}

void SweepRenderer::deallocSliceCorners( Slice_t *slice )
{
	unsigned char *corner_info = slice->corner_info.a;

	// clear the cubes that reference corner infos
	for ( int i = 0; i < slice->corner_info.size; i++ )
	{
		CornerInfo *c = (CornerInfo *)( corner_info + cornerInfoSize * i );
		slice->corners[c->y][c->z].everything = 0;
	}

	slice->corner_info.size = 1;

	unused_slice_corners.pushAutoSize( slice->corners );
	slice->corners = NULL;
	unused_corner_info.pushAutoSize( slice->corner_info );
	slice->corner_info.size = 0;
	slice->corner_info.capacity = 0;
	slice->corner_info.a = NULL;
}

void SweepRenderer::allocSliceTodoList( Slice_t *slice )
{
	if ( slice->todo_list.capacity == 0 )
	{
		if ( unused_todo_lists.size > 0 )
		{
			slice->todo_list = unused_todo_lists.pop();
		}
		else
		{
			slice->todo_list.resize( 16 );
			n_alloced_slice_todo_lists++;
			ConMsg( "Allocated %d todo lists\n", n_alloced_slice_todo_lists );
		}
	}
}

void SweepRenderer::deallocSliceTodoList( Slice_t *slice )
{
	unused_todo_lists.pushAutoSize( slice->todo_list );
	slice->todo_list.size = 0;
	slice->todo_list.capacity = 0;
	slice->todo_list.a = NULL;
}

//-----------------------------------------------------------------------------
// Sweeps the slices along x.  A slice is polygonized while it (or its todo list)
// has work; finishing a slice can create work in the previous one, in which case
// the sweep steps back.
//-----------------------------------------------------------------------------
void SweepRenderer::render_slices()
{
	int noSlicesDrawn = 0;
	polygonizationEnabled = true;

	int numIterations = 0;
	for ( int x = 0; x < xsize; x++ )
	{
		if ( numIterations >= 400 )
		{
			Warning( "ERROR: SweepRenderer::render_slices got into an infinite loop!\n" );
			return;
		}

		Slice_t *prev_slice = &slices[x];
		Slice_t *cur_slice = &slices[x + 1];
		Slice_t *next_slice = &slices[x + 2];

		if ( cur_slice->todo_list.size > 0 || cur_slice->seed_list.size > 0 )
		{
			if ( noSlicesDrawn == maxNoSlicesToDraw )
			{
				polygonizationEnabled = false;
			}
			else if ( polygonizationEnabled )
			{
				last_slice_drawn = x;
			}
			noSlicesDrawn++;

			allocSliceTodoList( prev_slice );
			allocSliceTodoList( cur_slice );
			allocSliceTodoList( next_slice );
			allocSliceCorners( cur_slice );
			allocSliceCorners( next_slice );

			render_slice( x, prev_slice, cur_slice, next_slice );

			bool prev_slice_done = prev_slice->todo_list.size == 0 && prev_slice->seed_list.size == 0;
			bool next_slice_done = next_slice->todo_list.size == 0 && next_slice->seed_list.size == 0;

			deallocSliceTodoList( cur_slice );

			if ( prev_slice_done )
			{
				deallocSliceTodoList( prev_slice );
				deallocSliceCorners( cur_slice );
			}

			if ( next_slice_done )
			{
				deallocSliceTodoList( next_slice );
				deallocSliceCorners( next_slice );
			}

			// the previous slice got new work: go back to it
			if ( !prev_slice_done && x > 0 )
			{
				x -= 2;
			}
		}

		numIterations++;
	}
}

//-----------------------------------------------------------------------------
// Polygonizes the cubes between cur_slice (x) and next_slice (x+1), starting from
// the seeds / todo entries of cur_slice and crawling along the surface in y and z.
//-----------------------------------------------------------------------------
void SweepRenderer::render_slice( const unsigned char x, Slice_t *prev_slice, Slice_t *cur_slice, Slice_t *next_slice )
{
	float fx[2];
	fx[0] = (float)x * cubewidth + outerBBMins[0];
	fx[1] = fx[0] + cubewidth;

	CubeInfo( *const cur_cubes )[zsizeP1] = cur_slice->corners;
	int cur_corner_info_no = cur_slice->corner_info.size;
	CubeInfo( *const next_cubes )[zsizeP1] = next_slice->corners;
	int next_corner_info_no = next_slice->corner_info.size;

	for ( ;; )
	{
		YZ yz;
		if ( cur_slice->todo_list.size > 0 )
		{
			yz = cur_slice->todo_list.pop();
		}
		else if ( cur_slice->seed_list.size > 0 )
		{
			yz = cur_slice->seed_list.pop();
		}
		else
		{
			break;
		}

		if ( cur_slice->corners[yz.y][yz.z].doneAbove )
			continue;

		cur_slice->corners[yz.y][yz.z].doneAbove = true;
		next_slice->corners[yz.y][yz.z].doneBelow = true;
		recursion_stack_src.push2AutoSize( yz.y, yz.z );

		do
		{
			const unsigned char z = recursion_stack_src.pop();
			const unsigned char y = recursion_stack_src.pop();

			bool polygonize_cube = polygonizationEnabled;
			CalcCornerFunc_t calc_corner_func = calc_corner;

			if ( tile_mode )
			{
				// cubes in the margin are only evaluated for crawling, the neighbouring tile draws them
				if ( x < marginNCubes || x >= xsize - marginNCubes ||
					 y < marginNCubes || y >= ysize - marginNCubes ||
					 z < marginNCubes || z >= zsize - marginNCubes )
				{
					polygonize_cube = false;
					if ( x < marginNCubes - 1 || x > xsize - marginNCubes ||
						 y < marginNCubes - 1 || y > ysize - marginNCubes ||
						 z < marginNCubes - 1 || z > zsize - marginNCubes )
					{
						calc_corner_func = calc_sign2;
					}
				}
			}

			float fy[2];
			fy[0] = (float)y * cubewidth + outerBBMins[1];
			fy[1] = fy[0] + cubewidth;
			float fz[2];
			fz[0] = (float)z * cubewidth + outerBBMins[2];
			fz[1] = fz[0] + cubewidth;

			// at most 4 neighbours get pushed below
			recursion_stack_src.ensureAdditionalCapacity( 8 );

			// at most 4 new corners per slice (corner info arrays are sized in bytes while filling)
			cur_slice->corner_info.size = cornerInfoSize * cur_corner_info_no;
			cur_slice->corner_info.ensureAdditionalCapacity( cornerInfoSize * 4 );
			next_slice->corner_info.size = cornerInfoSize * next_corner_info_no;
			next_slice->corner_info.ensureAdditionalCapacity( cornerInfoSize * 4 );

			unsigned char *cur_corner_info = cur_slice->corner_info.a;
			unsigned char *next_corner_info = next_slice->corner_info.a;

			CornerInfo *cubeCorners[8];
			int index;

			if ( cur_cubes[y][z].cornerInfoNo == 0 )
			{
				cur_cubes[y][z].cornerInfoNo = cur_corner_info_no;
				calc_corner_func( x, y, z, fx[0], fy[0], fz[0],
					(CornerInfo *)( cur_corner_info + cornerInfoSize * cur_corner_info_no++ ), pCache );
			}
			cubeCorners[0] = (CornerInfo *)( cur_corner_info + cur_cubes[y][z].cornerInfoNo * cornerInfoSize );
			index = ( *(unsigned int *)&cubeCorners[0]->value ) >> 31;

			if ( cur_cubes[y][z + 1].cornerInfoNo == 0 )
			{
				cur_cubes[y][z + 1].cornerInfoNo = cur_corner_info_no;
				calc_corner_func( x, y, z + 1, fx[0], fy[0], fz[1],
					(CornerInfo *)( cur_corner_info + cornerInfoSize * cur_corner_info_no++ ), pCache );
			}
			cubeCorners[1] = (CornerInfo *)( cur_corner_info + cur_cubes[y][z + 1].cornerInfoNo * cornerInfoSize );
			index |= ( ( *(unsigned int *)&cubeCorners[1]->value ) >> 30 ) & 2;

			if ( cur_cubes[y + 1][z].cornerInfoNo == 0 )
			{
				cur_cubes[y + 1][z].cornerInfoNo = cur_corner_info_no;
				calc_corner_func( x, y + 1, z, fx[0], fy[1], fz[0],
					(CornerInfo *)( cur_corner_info + cornerInfoSize * cur_corner_info_no++ ), pCache );
			}
			cubeCorners[2] = (CornerInfo *)( cur_corner_info + cur_cubes[y + 1][z].cornerInfoNo * cornerInfoSize );
			index |= ( ( *(unsigned int *)&cubeCorners[2]->value ) >> 29 ) & 4;

			if ( cur_cubes[y + 1][z + 1].cornerInfoNo == 0 )
			{
				cur_cubes[y + 1][z + 1].cornerInfoNo = cur_corner_info_no;
				calc_corner_func( x, y + 1, z + 1, fx[0], fy[1], fz[1],
					(CornerInfo *)( cur_corner_info + cornerInfoSize * cur_corner_info_no++ ), pCache );
			}
			cubeCorners[3] = (CornerInfo *)( cur_corner_info + cur_cubes[y + 1][z + 1].cornerInfoNo * cornerInfoSize );
			index |= ( ( *(unsigned int *)&cubeCorners[3]->value ) >> 28 ) & 8;

			if ( next_cubes[y][z].cornerInfoNo == 0 )
			{
				next_cubes[y][z].cornerInfoNo = next_corner_info_no;
				calc_corner_func( x + 1, y, z, fx[1], fy[0], fz[0],
					(CornerInfo *)( next_corner_info + cornerInfoSize * next_corner_info_no++ ), pCache );
			}
			cubeCorners[4] = (CornerInfo *)( next_corner_info + next_cubes[y][z].cornerInfoNo * cornerInfoSize );
			index |= ( ( *(unsigned int *)&cubeCorners[4]->value ) >> 27 ) & 0x10;

			if ( next_cubes[y][z + 1].cornerInfoNo == 0 )
			{
				next_cubes[y][z + 1].cornerInfoNo = next_corner_info_no;
				calc_corner_func( x + 1, y, z + 1, fx[1], fy[0], fz[1],
					(CornerInfo *)( next_corner_info + cornerInfoSize * next_corner_info_no++ ), pCache );
			}
			cubeCorners[5] = (CornerInfo *)( next_corner_info + next_cubes[y][z + 1].cornerInfoNo * cornerInfoSize );
			index |= ( ( *(unsigned int *)&cubeCorners[5]->value ) >> 26 ) & 0x20;

			if ( next_cubes[y + 1][z].cornerInfoNo == 0 )
			{
				next_cubes[y + 1][z].cornerInfoNo = next_corner_info_no;
				calc_corner_func( x + 1, y + 1, z, fx[1], fy[1], fz[0],
					(CornerInfo *)( next_corner_info + cornerInfoSize * next_corner_info_no++ ), pCache );
			}
			cubeCorners[6] = (CornerInfo *)( next_corner_info + next_cubes[y + 1][z].cornerInfoNo * cornerInfoSize );
			index |= ( ( *(unsigned int *)&cubeCorners[6]->value ) >> 25 ) & 0x40;

			if ( next_cubes[y + 1][z + 1].cornerInfoNo == 0 )
			{
				next_cubes[y + 1][z + 1].cornerInfoNo = next_corner_info_no;
				calc_corner_func( x + 1, y + 1, z + 1, fx[1], fy[1], fz[1],
					(CornerInfo *)( next_corner_info + cornerInfoSize * next_corner_info_no++ ), pCache );
			}
			cubeCorners[7] = (CornerInfo *)( next_corner_info + next_cubes[y + 1][z + 1].cornerInfoNo * cornerInfoSize );
			index |= ( ( *(unsigned int *)&cubeCorners[7]->value ) >> 24 ) & 0x80;

			// crawl to the neighbouring cubes the surface passes into
			char adjacentVoxels = crawlTable[index];

			if ( ( adjacentVoxels & 1 ) && !cur_cubes[y][z].doneBelow )
			{
				YZ todo;
				todo.y = y;
				todo.z = z;
				prev_slice->todo_list.pushAutoSize( todo );
			}

			if ( ( adjacentVoxels & 2 ) && !next_cubes[y][z].doneAbove )
			{
				YZ todo;
				todo.y = y;
				todo.z = z;
				next_slice->todo_list.pushAutoSize( todo );
			}

			if ( ( adjacentVoxels & 4 ) && !cur_cubes[y - 1][z].doneAbove )
			{
				cur_cubes[y - 1][z].doneAbove = true;
				next_cubes[y - 1][z].doneBelow = true;
				recursion_stack_src.push2( y - 1, z );
			}

			if ( ( adjacentVoxels & 0x10 ) && !cur_cubes[y][z - 1].doneAbove )
			{
				cur_cubes[y][z - 1].doneAbove = true;
				next_cubes[y][z - 1].doneBelow = true;
				recursion_stack_src.push2( y, z - 1 );
			}

			if ( ( adjacentVoxels & 8 ) && !cur_cubes[y + 1][z].doneAbove )
			{
				cur_cubes[y + 1][z].doneAbove = true;
				next_cubes[y + 1][z].doneBelow = true;
				recursion_stack_src.push2( y + 1, z );
			}

			if ( ( adjacentVoxels & 0x20 ) && !cur_cubes[y][z + 1].doneAbove )
			{
				cur_cubes[y][z + 1].doneAbove = true;
				next_cubes[y][z + 1].doneBelow = true;
				recursion_stack_src.push2( y, z + 1 );
			}

			if ( polygonize_cube )
			{
				vertexBuffer->beginCube( x );

				const char *drawList = drawTable[index];
				int edge;
				while ( ( edge = *drawList ) != -1 )
				{
					// per edge: x/y/z offset of its first corner, its direction (0=x,1=y,2=z),
					// and the indices of its two corners in cubeCorners
					static const int edgeOffsets[12][6] =
					{
						{ 0, 0, 0, 2, 0, 1 },
						{ 0, 0, 0, 1, 0, 2 },
						{ 0, 0, 1, 1, 1, 3 },
						{ 0, 1, 0, 2, 2, 3 },
						{ 0, 0, 0, 0, 0, 4 },
						{ 0, 0, 1, 0, 1, 5 },
						{ 0, 1, 0, 0, 2, 6 },
						{ 0, 1, 1, 0, 3, 7 },
						{ 1, 0, 0, 2, 4, 5 },
						{ 1, 0, 0, 1, 4, 6 },
						{ 1, 0, 1, 1, 5, 7 },
						{ 1, 1, 0, 2, 6, 7 }
					};

					const int *eo = edgeOffsets[edge];
					CornerInfo *corner = cubeCorners[eo[4]];
					vbId_t *vertexId = &corner->edges[eo[3]];

					if ( vertexBuffer->isIdValid( vertexId ) )
					{
						vertexBuffer->drawVertex( vertexId );
					}
					else
					{
						vertexBuffer->beginVertex( vertexId );
						calc_vertex( fx[eo[0]], fy[eo[1]], fz[eo[2]], eo[3], corner, cubeCorners[eo[5]], vertexBuffer );
						vertexBuffer->endVertex( vertexId );
					}

					drawList++;
				}
			}
		} while ( recursion_stack_src.size > 0 );
	}

	cur_slice->corner_info.size = cur_corner_info_no;
	next_slice->corner_info.size = next_corner_info_no;
}
