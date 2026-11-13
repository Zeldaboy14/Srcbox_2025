#ifndef _FUNC_SIMPLELADDER_H_
#define _FUNC_SIMPLELADDER_H_
#ifdef _WIN32
#pragma once
#endif

#include "func_ladder.h"

//-----------------------------------------------------------------------------
// L4D-style ladder brush.
//
// In Left 4 Dead this entity is only a nav/AI hint: L4D's VBSP bakes
// CONTENTS_LADDER into the brush and its movement code knows how to climb it.
// Source SDK 2013 has neither of those, and its HL2 movement only climbs
// CFuncLadder (func_useableladder) volumes.  So this class keeps the L4D brush
// around (visuals + AI/nav marker) and additionally builds a matching
// CFuncLadder volume at runtime from the brush bounds.  That makes maps that
// use either system climbable with the same binaries.
//-----------------------------------------------------------------------------
class CFuncSimpleLadder : public CBaseEntity
{
public:
	DECLARE_CLASS( CFuncSimpleLadder, CBaseEntity );
	DECLARE_DATADESC();
	DECLARE_SERVERCLASS();

	CFuncSimpleLadder();

	virtual void	Spawn( void );
	virtual void	Activate( void );
	virtual bool	KeyValue( const char *szKeyName, const char *szValue );

	// L4D "team" key: 0 = anyone, 1 = survivors only, 2 = infected only
	enum { LADDER_TEAM_ANY = 0, LADDER_TEAM_SURVIVOR = 1, LADDER_TEAM_INFECTED = 2 };
	int				GetL4DLadderTeam( void ) const { return m_iLadderTeam; }

	virtual int		DrawDebugTextOverlays( void );
	virtual void	DrawDebugGeometryOverlays( void );

protected:
	void			AdapterThink( void );
	void			CreatePlayerLadder( void );

	// Works out which way the climbable face points. Uses the L4D normal.*
	// keys first and the brush geometry second.
	void			ComputeClimbableNormal( const Vector &mins, const Vector &maxs, Vector &outNormal );

	// Drops the player hull from probe.z + flUp to probe.z - flDown and returns
	// the first spot where the hull can stand. Used to place dismount nodes on
	// real floors instead of guessing heights.
	bool			FindStandingSpot( const Vector &probe, float flUp, float flDown, Vector &out );

	CNetworkVector( m_climbableNormal );

	// The CFuncLadder volume only needs to be built once per map spawn.
	bool			m_bAdapterSpawned;

	// L4D stores a team restriction on the ladder (see GetL4DLadderTeam). hl2mp has no
	// survivor / infected teams, so the entity's own team is left unassigned; nav and
	// future zombie code should read GetL4DLadderTeam() instead.
	int				m_iLadderTeam;
};

#endif // _FUNC_SIMPLELADDER_H_
