//===== Reconstructed from particles.lib debug info (src/common/blobulator/implicit/sweeprenderer.h) =====//
//
// Polygonizes the implicit surface of the particles of one tile (a 100^3 voxel block) by
// sweeping slices along x and crawling the surface from seed cubes (marching cubes).
//=============================================================================================//
#ifndef BLOB_SWEEPRENDERER_H
#define BLOB_SWEEPRENDERER_H
#pragma once

#include "blobulator/point3d.h"
#include "blobulator/smartarray.h"
#include "blobulator/implicit/impparticle.h"
#include "blobulator/implicit/particlecache.h"
#include "blobulator/vertexbuffers/vertexbuffer.h"

class ImpTile;

struct YZ
{
	unsigned char y;
	unsigned char z;
};

// per-cube bookkeeping of a slice
union CubeInfo
{
	struct
	{
		unsigned short cornerInfoNo;	// 1-based index into the slice's corner info array (0: not computed)
		bool doneAbove;
		bool doneBelow;
	};
	unsigned int everything;
};

// field value / gradient at a voxel corner and the ids of the vertices on its three edges
struct CornerInfo
{
	float value;
	float normal[3];
	vbId_t edges[3];
	unsigned char y;
	unsigned char z;
	unsigned char pad1;
	unsigned char pad2;
};

struct CornerInfoWithOneInterpolant : public CornerInfo
{
	Point3D interpolants1;
};

struct CornerInfoWithTwoInterpolants : public CornerInfoWithOneInterpolant
{
	Point3D interpolants2;
};

struct CornerInfoWithFourInterpolants : public CornerInfoWithTwoInterpolants
{
	Point3D interpolants3;
	Point3D interpolants4;
};

struct Slice_t
{
	CubeInfo ( *corners )[zsizeP1];
	SmartArray< unsigned char, 1, 0 > corner_info;
	SmartArray< YZ, 0, 0 > todo_list;
	SmartArray< YZ, 0, 16 > seed_list;
};

typedef bool ( *CalcSignFunc_t )( const unsigned char ix, const unsigned char iy, const unsigned char iz,
	const float x, const float y, const float z, ProjectingParticleCache *pCache );
typedef void ( *CalcCornerFunc_t )( const unsigned char ix, const unsigned char iy, const unsigned char iz,
	const float x, const float y, const float z, CornerInfo *const cornerInfo, ProjectingParticleCache *pCache );
typedef void ( *CalcVertexFunc_t )( const float x, const float y, const float z, const int edge,
	const CornerInfo *corner1, const CornerInfo *corner2, IndexTriVertexBuffer *vertexBuffer );

class SweepRenderer
{
public:
	static float cubewidth;
	static float oneOverCubewidth;
	static int cornerInfoSize;
	static float cutoffR;
	static float renderR;
	static float cutoffRSq;
	static float scaler;
	static float scalerSq;
	static float scaledRenderRSq;
	static float threshold;
	static float oneOverThreshold;
	static float addAmt;
	static short fieldCalcSteps;

	static float getCubeWidth();
	static float getCutoffR();
	static float getRenderR();
	static void setCubeWidth( float _cubewidth );
	static void setCutoffR( float _cutoffR );
	static void setRenderR( float _renderR );
	static int getMarginNCubes();
	static float getMarginWidth();
	static Point3D &getInnerDimensions();

	static void setCalcSignFunc( CalcSignFunc_t calc_sign_ptr );
	static void setCalcSign2Func( CalcCornerFunc_t calc_sign2_ptr );
	static void setCalcCornerFunc( int _cornerInfoSize, CalcCornerFunc_t calc_corner_ptr );
	static void setCalcVertexFunc( CalcVertexFunc_t calc_vertex_ptr );

private:
	static bool tile_mode;
	static int marginNCubes;
	static float marginWidth;
	static Point3D outerDimensions;
	static Point3D innerDimensions;

	static void changeRadii( float _cutoffR, float _renderR );
	static void changeCubeWidth( float _cubewidth );
	static void recalculateDimensions();

public:
	ProjectingParticleCache *pCache;
	IndexTriVertexBuffer *vertexBuffer;

	SweepRenderer();
	void rendererInit();
	void setOffset( Point3D &_offset );
	void beginFrame( bool _tile_mode, void *pRenderContext );
	void beginTile( ImpTile *tile );
	bool isParticleWithinBounds( ImpParticle *p );
	void addParticle( ImpParticle *p, bool skipBoundsCheck );
	void endTile();
	void endFrame();
	void setMaxNoSlicesToDraw( int n ) { maxNoSlicesToDraw = n; }
	int getMaxNoSlicesToDraw() { return maxNoSlicesToDraw; }
	int getLastSliceDrawn() { return last_slice_drawn; }

private:
	int maxNoSlicesToDraw;
	int last_slice_drawn;
	bool polygonizationEnabled;
	Point3D offset;
	Point3D outerBBMins;
	Point3D outerBBMaxs;
	Point3D innerBBMins;
	Point3D innerBBMaxs;
	SmartArray< ImpParticle * > particles;
	Slice_t slices[xsizeP2];
	SmartArray< CubeInfo ( * )[zsizeP1] > unused_slice_corners;
	SmartArray< SmartArray< unsigned char, 1, 0 > > unused_corner_info;
	SmartArray< SmartArray< YZ, 0, 0 > > unused_todo_lists;
	int n_alloced_slice_corners;
	int n_alloced_slice_corner_infos;
	int n_alloced_slice_todo_lists;
	SmartArray< unsigned char > recursion_stack_src;

	void recalculateBB();
	void seed_surface( Point3D &_center );
	void allocSliceCorners( Slice_t *slice );
	void deallocSliceCorners( Slice_t *slice );
	void allocSliceTodoList( Slice_t *slice );
	void deallocSliceTodoList( Slice_t *slice );
	void render_slices();
	void render_slice( const unsigned char x, Slice_t *prev_slice, Slice_t *cur_slice, Slice_t *next_slice );
};

#endif // BLOB_SWEEPRENDERER_H
