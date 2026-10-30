//===== Copyright (c) Valve Corporation, All rights reserved. ======//
//
// Purpose: registers the built-in particle operators with the manager
//
// Reconstructed from the Alien Swarm SDK particles.lib (addbuiltin_ops.obj).
//
//===========================================================================//

#include "tier0/platform.h"
#include "particles/particles.h"
#include "particles_internal.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"


//-----------------------------------------------------------------------------
// Adds the operators needed to simulate particle systems
//-----------------------------------------------------------------------------
void CParticleSystemMgr::AddBuiltinSimulationOperators( void )
{
	static bool s_DidAddSim = false;
	if ( !s_DidAddSim )
	{
		s_DidAddSim = true;
		AddBuiltInParticleOperators();
		AddBuiltInParticleInitializers();
		AddBuiltInParticleEmitters();
		AddBuiltInParticleForceGenerators();
		AddBuiltInParticleConstraints();
	}
}


//-----------------------------------------------------------------------------
// Adds the operators needed to render particle systems
//-----------------------------------------------------------------------------
void CParticleSystemMgr::AddBuiltinRenderingOperators( void )
{
	static bool s_DidAddRenderers = false;
	if ( !s_DidAddRenderers )
	{
		s_DidAddRenderers = true;
		AddBuiltInParticleRenderers();
	}
}
