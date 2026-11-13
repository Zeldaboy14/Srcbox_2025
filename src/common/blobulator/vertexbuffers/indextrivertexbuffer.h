//===== Reconstructed from particles.lib debug info (src/common/blobulator/vertexbuffers/indextrivertexbuffer.h) =====//
//
// Collects the triangles produced by the polygonizer into a dynamic indexed triangle mesh.
// Vertices shared by neighbouring cubes are referenced through vbId_t handles: a handle is
// valid while its time stamp matches the current mesh (m_curTime), i.e. until the mesh is
// flushed because it ran out of room.
//=============================================================================================//
#ifndef BLOB_INDEXTRIVERTEXBUFFER_H
#define BLOB_INDEXTRIVERTEXBUFFER_H
#pragma once

#include "blobulator/point3d.h"
#include "materialsystem/imaterialsystem.h"
#include "materialsystem/imesh.h"

class IMesh;
class CMeshBuilder;
class ImpTile;

struct vbId_t
{
	unsigned short time;
	unsigned short id;
};

class IndexTriVertexBuffer
{
public:
	IndexTriVertexBuffer();
	~IndexTriVertexBuffer();

	inline void init();
	inline void beginFrame( void *pRenderContext );
	inline void endFrame();
	inline void beginTile( ImpTile *tile );
	inline void endTile();
	inline void beginCube( unsigned char slice );
	inline void endCube();
	void beginTriStrip();
	void endTriStrip();

	static inline void clearId( vbId_t *vertexId );
	inline bool isIdValid( const vbId_t *vertexId );
	inline void drawVertex( const vbId_t *vertexId );

	inline void beginVertex( vbId_t *out_vertexId );
	inline void setVertexPosition( Point3D &pos );
	inline void setVertexNormal( Point3D &norm );
	inline void setVertexTangent( Point3D &tang );
	inline void setVertexColor( Point3D &color );
	inline void setVertexUV( int nStage, float u, float v );
	inline void setVertexUV( int nStage, float s, float t, float u );
	inline void setVertexUV( int nStage, float s, float t, float u, float w );
	inline void endVertex( vbId_t *vertexId );

private:
	inline void beginMesh();
	inline void endMesh();

public:
	unsigned short m_curTime;
	unsigned short m_nVerticesOutput;
	IMesh *m_pMesh;
	CMeshBuilder *m_pMeshBuilder;
	unsigned short m_stat_no_flushes;
};

#endif // BLOB_INDEXTRIVERTEXBUFFER_H
