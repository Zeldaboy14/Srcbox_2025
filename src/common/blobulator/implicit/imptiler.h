//===== Reconstructed from particles.lib debug info (src/common/blobulator/implicit/imptiler.h) =====//
//
// Splits space into tiles of the renderer's inner dimensions.  Every particle is added to all
// tiles whose margin-extended bounds contain it; each tile is then polygonized separately.
//=============================================================================================//
#ifndef BLOB_IMPTILER_H
#define BLOB_IMPTILER_H
#pragma once

#include "blobulator/point3d.h"
#include "blobulator/smartarray.h"
#include "blobulator/implicit/impparticle.h"

class SweepRenderer;

class ImpTile
{
public:
	ImpTile( int _x, int _y, int _z )
	{
		x = _x;
		y = _y;
		z = _z;
	}

	bool done;
	int x;
	int y;
	int z;
	Point3D center;
	bool old_tile;			// tile was used last frame (kept around for reuse)
	Point3D debug_color;
	float dist;				// distance to the eye, for drawSurfaceSorted
	SmartArray< ImpParticle * > particles;
};

class ImpTiler
{
public:
	ImpTiler( SweepRenderer *sweepRenderer );
	~ImpTiler();

	SweepRenderer *getRenderer() { return m_sweepRenderer; }

	void beginFrame( Point3D &_offset, void *pRenderContext, bool tile_mode );
	void insertParticle( ImpParticle *p );
	void drawSurface();
	void drawSurfaceSorted( Point3D &center );
	int getNoTiles();
	ImpTile *getTile( int i );
	Point3D getTileOffset( int i );
	void setMaxNoTilesToDraw( int n ) { maxNoTilesToDraw = n; }
	int getMaxNoTilesToDraw() { return maxNoTilesToDraw; }
	Point3D getRenderDim() { return render_dim; }
	Point3D getLastTilesOffset() { return last_tiles_offset; }
	void endFrame();
	void drawTile( int x, int y, int z );
	void drawTile( ImpTile *tile );

	SweepRenderer *m_sweepRenderer;
	SmartArray< ImpTile * > tiles;
	int maxNoTilesToDraw;
	Point3D last_tiles_offset;
	Point3D offset;
	Point3D render_dim;
	float render_margin;
	Point3D corner_offset;

private:
	Point3D calcTileCorner( int x, int y, int z );
	Point3D calcTileOffset( int x, int y, int z );
	void addParticleToTile( ImpParticle *p, int x, int y, int z );
	ImpTile *findTile( int x, int y, int z );
	ImpTile *createTile( int x, int y, int z );
};

class ImpTilerFactory
{
public:
	static ImpTilerFactory *factory;

	ImpTiler *getTiler();
	void returnTiler( ImpTiler *tiler );

private:
	ImpTilerFactory();

	SmartArray< ImpTiler * > m_tilers;
};

// colour of the tile being drawn, used by calcVertexNormalDebugColor (defined in imptiler.cpp)
extern Point3D tile_debug_color;

#endif // BLOB_IMPTILER_H
