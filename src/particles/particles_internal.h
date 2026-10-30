//===== Copyright (c) Valve Corporation, All rights reserved. ======//
//
// Purpose: declarations shared between the particle library's source files
//
// Reconstructed from the Alien Swarm SDK particles.lib. Every type here was
// recovered from the debug information embedded in the library's objects.
//
//===========================================================================//

#ifndef PARTICLES_INTERNAL_H
#define PARTICLES_INTERNAL_H

#ifdef _WIN32
#pragma once
#endif

#include "particles/particles.h"
#include "mathlib/ssemath.h"


//-----------------------------------------------------------------------------
// Multiplier applied to each system's maximum particle count (particles.cpp)
//-----------------------------------------------------------------------------
extern int g_nParticle_Multiplier;


//-----------------------------------------------------------------------------
// Registration of the built-in operators (builtin_*.cpp)
//-----------------------------------------------------------------------------
void AddBuiltInParticleOperators( void );
void AddBuiltInParticleRenderers( void );
void AddBuiltInParticleInitializers( void );
void AddBuiltInParticleEmitters( void );
void AddBuiltInParticleForceGenerators( void );
void AddBuiltInParticleConstraints( void );


//-----------------------------------------------------------------------------
// Cached world collision planes, shared by the collision constraints.
// Layout matches the shipped library (sizeof == 0x1F10).
//-----------------------------------------------------------------------------
#define MAX_WORLD_PLANAR_CONSTRAINTS 41

struct CWorldCollideContextData
{
	FourVectors m_TraceStartPnt[MAX_WORLD_PLANAR_CONSTRAINTS];
	FourVectors m_TraceEndPnt[MAX_WORLD_PLANAR_CONSTRAINTS];
	FourVectors m_PointOnPlane[MAX_WORLD_PLANAR_CONSTRAINTS];
	FourVectors m_PlaneNormal[MAX_WORLD_PLANAR_CONSTRAINTS];
	int m_nActivePlanes;
	int m_nNumFixedPlanes;
	float m_flLastUpdateTime;
	Vector m_vecLastUpdateOrigin;
	bool m_bPlaneActive[MAX_WORLD_PLANAR_CONSTRAINTS];

	void *operator new( size_t nSize );
	void *operator new( size_t size, int nBlockUse, const char *pFileName, int nLine );
	void operator delete( void *pData );
	void operator delete( void* p, int nBlockUse, const char *pFileName, int nLine );

	void SetBaseTrace( int nIndex, Vector const &rayStart, Vector const &traceDir, int nCollisionGroup, bool bKeepMisses );
	void CalculatePlanes( CParticleCollection *pParticles, int nCollisionMode, int nCollisionGroupNumber, Vector const *pCpOffset = NULL, float flMovementTolerance = 0. );
};


#endif // PARTICLES_INTERNAL_H
