//========= Copyright © 1996-2005, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

#ifndef P3_ISIMULATOR_MOVEHELPER_H
#define P3_ISIMULATOR_MOVEHELPER_H

#ifdef _WIN32
#pragma once
#endif


//-----------------------------------------------------------------------------
// Forward declarations
//-----------------------------------------------------------------------------

enum PLAYER_ANIM;
class IPhysicsSurfaceProps;
class Vector;
struct model_t;
struct cmodel_t;
struct vcollide_t;
class CGameTrace;
enum soundlevel_t;

//-----------------------------------------------------------------------------
// Purpose: Identifies how submerged in water a player is.
//-----------------------------------------------------------------------------

namespace P3_Simulator {
enum
{
	WL_NotInWater=0,
	WL_Feet,
	WL_Waist,
	WL_Eyes
};
}


//-----------------------------------------------------------------------------
// An entity identifier that works in both game + client dlls
//-----------------------------------------------------------------------------

typedef CBaseHandle EntityHandle_t;


#define INVALID_ENTITY_HANDLE INVALID_EHANDLE_INDEX

//-----------------------------------------------------------------------------
// Functions the engine provides to IGameMovement to assist in its movement.
//-----------------------------------------------------------------------------

abstract_class P3_ISimulatorMoveHelper
{
public:	
	// Methods associated with a particular entity
	virtual	char const*		GetName( EntityHandle_t handle ) const = 0;

	// Adds the trace result to touch list, if contact is not already in list.
	virtual void	ResetTouchList( void ) = 0;
	virtual bool	AddToTouched( const CGameTrace& tr, const Vector& impactvelocity ) = 0;
	virtual void	ProcessImpacts( void ) = 0;
	
	// Numbered line printf
	virtual void	Con_NPrintf( int idx, char const* fmt, ... ) = 0;

	// These have separate server vs client impementations
	virtual void	StartSound( const Vector& origin, int channel, char const* sample, float volume, soundlevel_t soundlevel, int fFlags, int pitch ) = 0;
	virtual void	StartSound( const Vector& origin, const char *soundname ) = 0; 
	virtual void	PlaybackEventFull( int flags, int clientindex, unsigned short eventindex, float delay, Vector& origin, Vector& angles, float fparam1, float fparam2, int iparam1, int iparam2, int bparam1, int bparam2 ) = 0;

	// Apply falling damage to m_pHostPlayer based on m_pHostPlayer->m_flFallVelocity.
	virtual bool	PlayerFallingDamage( void ) = 0;

	// Apply falling damage to m_pHostPlayer based on m_pHostPlayer->m_flFallVelocity.
	virtual void	PlayerSetAnimation( PLAYER_ANIM playerAnim ) = 0;

	virtual IPhysicsSurfaceProps *GetSurfaceProps( void ) = 0;

	virtual bool IsWorldEntity( const CBaseHandle &handle ) = 0;

protected:	
};

#endif // P3_ISIMULATOR_MOVEHELPER_H
