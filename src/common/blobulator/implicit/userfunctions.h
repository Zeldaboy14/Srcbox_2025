//===== Reconstructed from particles.lib debug info (src/common/blobulator/implicit/userfunctions.h) =====//
//
// Field evaluation and vertex generation callbacks plugged into SweepRenderer.
//   calcSign*:   is the field at a corner above the threshold (inside the surface)?
//   calcCorner*: evaluate the field (value - threshold), its gradient and blended particle data
//   calcVertex*: emit the vertex on a cube edge by interpolating its two corners
//=============================================================================================//
#ifndef BLOB_USERFUNCTIONS_H
#define BLOB_USERFUNCTIONS_H
#pragma once

#include "blobulator/implicit/sweeprenderer.h"

bool calcSign( const unsigned char ix, const unsigned char iy, const unsigned char iz,
	const float x, const float y, const float z, ProjectingParticleCache *pCache );
bool calcSignSIMD( const unsigned char ix, const unsigned char iy, const unsigned char iz,
	const float x, const float y, const float z, ProjectingParticleCache *pCache );

void calcSign2( const unsigned char ix, const unsigned char iy, const unsigned char iz,
	const float x, const float y, const float z, CornerInfo *const cornerInfo, ProjectingParticleCache *pCache );
void calcCornerNormal( const unsigned char ix, const unsigned char iy, const unsigned char iz,
	const float x, const float y, const float z, CornerInfo *const cornerInfo, ProjectingParticleCache *pCache );
void calcCornerNormalHiFreqColor( const unsigned char ix, const unsigned char iy, const unsigned char iz,
	const float x, const float y, const float z, CornerInfo *const cornerInfo, ProjectingParticleCache *pCache );
void calcCornerNormalColor( const unsigned char ix, const unsigned char iy, const unsigned char iz,
	const float x, const float y, const float z, CornerInfo *const cornerInfo, ProjectingParticleCache *pCache );
void calcCornerNormalColorTanNoUV( const unsigned char ix, const unsigned char iy, const unsigned char iz,
	const float x, const float y, const float z, CornerInfo *const cornerInfo, ProjectingParticleCache *pCache );
void calcCornerNormalColorUVTan( const unsigned char ix, const unsigned char iy, const unsigned char iz,
	const float x, const float y, const float z, CornerInfo *const cornerInfo, ProjectingParticleCache *pCache );
void calcCornerNormalColorUVTanForBlob( const unsigned char ix, const unsigned char iy, const unsigned char iz,
	const float x, const float y, const float z, CornerInfo *const cornerInfo, ProjectingParticleCache *pCache );

void calcVertexNormal( const float x, const float y, const float z, const int edge,
	const CornerInfo *corner1, const CornerInfo *corner2, IndexTriVertexBuffer *vertexBuffer );
void calcVertexNormalDebugColor( const float x, const float y, const float z, const int edge,
	const CornerInfo *corner1, const CornerInfo *corner2, IndexTriVertexBuffer *vertexBuffer );
void calcVertexNormalNColor( const float x, const float y, const float z, const int edge,
	const CornerInfo *corner1, const CornerInfo *corner2, IndexTriVertexBuffer *vertexBuffer );
void calcVertexNormalNColorTanNoUV( const float x, const float y, const float z, const int edge,
	const CornerInfo *_corner1, const CornerInfo *_corner2, IndexTriVertexBuffer *vertexBuffer );
void calcVertexNormalNColorUVTan( const float x, const float y, const float z, const int edge,
	const CornerInfo *_corner1, const CornerInfo *_corner2, IndexTriVertexBuffer *vertexBuffer );
void calcVertexNormalNColorUVTanForBlob( const float x, const float y, const float z, const int edge,
	const CornerInfo *_corner1, const CornerInfo *_corner2, IndexTriVertexBuffer *vertexBuffer );
void calcVertexNormalNTexCoord4( const float x, const float y, const float z, const int edge,
	const CornerInfo *_corner1, const CornerInfo *_corner2, IndexTriVertexBuffer *vertexBuffer );

#endif // BLOB_USERFUNCTIONS_H
