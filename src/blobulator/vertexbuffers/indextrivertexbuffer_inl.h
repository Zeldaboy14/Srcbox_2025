//===== Reconstructed from particles.lib debug info (src/blobulator/vertexbuffers/indextrivertexbuffer_inl.h) =====//
#ifndef BLOB_INDEXTRIVERTEXBUFFER_INL_H
#define BLOB_INDEXTRIVERTEXBUFFER_INL_H
#pragma once

#include "blobulator/vertexbuffers/indextrivertexbuffer.h"

// a mesh is flushed when fewer than 24 vertices / indices (one cube's worth) are left
#define BLOB_MAX_VERTICES	16000
#define BLOB_MAX_INDICES	32000
#define BLOB_CUBE_RESERVE	24

inline void IndexTriVertexBuffer::init()
{
}

// pRenderContext points to a CMatRenderContextPtr (or anything starting with an IMatRenderContext*)
inline void IndexTriVertexBuffer::beginFrame( void *pRenderContext )
{
	CMatRenderContextPtr &renderContext = *(CMatRenderContextPtr *)pRenderContext;
	m_pMesh = renderContext->GetDynamicMesh( true );
	m_pMeshBuilder = new CMeshBuilder;

	m_curTime = 1;
	beginMesh();
}

inline void IndexTriVertexBuffer::endFrame()
{
	endMesh();

	if ( m_pMeshBuilder )
	{
		delete m_pMeshBuilder;
		m_pMeshBuilder = NULL;
	}
}

inline void IndexTriVertexBuffer::beginTile( ImpTile *tile )
{
}

inline void IndexTriVertexBuffer::endTile()
{
}

inline void IndexTriVertexBuffer::beginMesh()
{
	m_pMeshBuilder->Begin( m_pMesh, MATERIAL_TRIANGLES, BLOB_MAX_VERTICES, BLOB_MAX_INDICES );
	m_nVerticesOutput = 0;
}

inline void IndexTriVertexBuffer::endMesh()
{
	m_pMeshBuilder->End();
	m_pMesh->Draw();
}

inline void IndexTriVertexBuffer::beginCube( unsigned char slice )
{
	// flush the mesh if it can't hold another cube; vertex ids of the old mesh become invalid
	if ( m_pMeshBuilder->VertexCount() > BLOB_MAX_VERTICES - BLOB_CUBE_RESERVE ||
		 m_pMeshBuilder->IndexCount() > BLOB_MAX_INDICES - BLOB_CUBE_RESERVE )
	{
		endMesh();
		beginMesh();
		m_curTime++;
	}
}

inline void IndexTriVertexBuffer::endCube()
{
}

inline void IndexTriVertexBuffer::clearId( vbId_t *vertexId )
{
	vertexId->time = 0;
	vertexId->id = 0;
}

inline bool IndexTriVertexBuffer::isIdValid( const vbId_t *vertexId )
{
	return vertexId->time == m_curTime;
}

inline void IndexTriVertexBuffer::drawVertex( const vbId_t *vertexId )
{
	m_pMeshBuilder->FastIndex( vertexId->id );
}

inline void IndexTriVertexBuffer::beginVertex( vbId_t *out_vertexId )
{
	out_vertexId->time = m_curTime;
	out_vertexId->id = m_nVerticesOutput;
	m_nVerticesOutput++;
}

inline void IndexTriVertexBuffer::setVertexPosition( Point3D &pos )
{
	m_pMeshBuilder->Position3fv( pos.p );
}

inline void IndexTriVertexBuffer::setVertexNormal( Point3D &norm )
{
	m_pMeshBuilder->Normal3fv( norm.p );
}

// the tangent (with handedness in w) goes to the user data stream
inline void IndexTriVertexBuffer::setVertexTangent( Point3D &tang )
{
	m_pMeshBuilder->UserData( tang.p );
}

inline void IndexTriVertexBuffer::setVertexColor( Point3D &color )
{
	m_pMeshBuilder->Color3fv( color.p );
}

inline void IndexTriVertexBuffer::setVertexUV( int nStage, float u, float v )
{
	m_pMeshBuilder->TexCoord2f( nStage, u, v );
}

inline void IndexTriVertexBuffer::setVertexUV( int nStage, float s, float t, float u )
{
	m_pMeshBuilder->TexCoord3f( nStage, s, t, u );
}

inline void IndexTriVertexBuffer::setVertexUV( int nStage, float s, float t, float u, float w )
{
	m_pMeshBuilder->TexCoord4f( nStage, s, t, u, w );
}

inline void IndexTriVertexBuffer::endVertex( vbId_t *vertexId )
{
	m_pMeshBuilder->AdvanceVertex();
	m_pMeshBuilder->FastIndex( vertexId->id );
}

#endif // BLOB_INDEXTRIVERTEXBUFFER_INL_H
