//===== Reconstructed from particles.lib debug info (src/common/blobulator/implicit/projectingparticlecache.h) =====//
//
// Spatial cache of the particles of one tile.  For every (y,z) column of the tile's voxel grid
// it keeps a doubly linked list (sorted by x) of the particles whose field reaches that column,
// together with the x range [bot_x, top_x] of voxels each particle can touch.
//=============================================================================================//
#ifndef BLOB_PROJECTINGPARTICLECACHE_H
#define BLOB_PROJECTINGPARTICLECACHE_H
#pragma once

#include "blobulator/implicit/impdefines.h"
#include "blobulator/point3d.h"
#include "blobulator/smartarray.h"
#include "blobulator/implicit/impparticle.h"

struct PCacheElem_t
{
	short center_x;
	unsigned char bot_x;
	unsigned char top_x;
	PCacheElem_t *prev;
	PCacheElem_t *next;
	ImpParticle *p;
};

struct pcache_YZ_t
{
	unsigned char y;
	unsigned char z;
};

class ProjectingParticleCache
{
public:
	ProjectingParticleCache();
	~ProjectingParticleCache();

	inline void setOffset( Point3D &offset ) {}
	void beginTile();
	void addParticle( ImpParticle *p );
	void buildCache( float cw, float fieldRadius, Point3D &mins, Point3D &maxs );
	void endTile();
	inline void beginSlice( unsigned char x ) {}
	inline void endSlice( unsigned char x ) {}

private:
	void insertIntoCache( ImpParticle *p, float cw, float fieldRadius, Point3D &mins, Point3D &maxs );

public:
	PCacheElem_t *botSentinel;
	PCacheElem_t *midSentinel;
	PCacheElem_t *topSentinel;
	SmartArray< ImpParticle * > particles;
	SmartArray< PCacheElem_t * > cacheElements;
	SmartArray< pcache_YZ_t > clearList;
	int curCacheElement;
	PCacheElem_t *( *cachePlane )[zsizeP1];
};

#endif // BLOB_PROJECTINGPARTICLECACHE_H
