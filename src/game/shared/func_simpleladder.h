//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
//=============================================================================//

#ifndef FUNC_LADDER_H
#define FUNC_LADDER_H
#ifdef _WIN32
#pragma once
#endif

#if defined( CLIENT_DLL )
#define CFuncSimpleLadder C_FuncSimpleLadder
#endif

// Spawnflags
#define SF_LADDER_DONTGETON			1			// Set for ladders that are acting as automount points, but not really ladders

//-----------------------------------------------------------------------------
// Purpose: A player-climbable ladder
//-----------------------------------------------------------------------------
class CFuncSimpleLadder : public CBaseEntity
{
public:

	DECLARE_CLASS( CFuncSimpleLadder, CBaseEntity );
	DECLARE_NETWORKCLASS();
	DECLARE_DATADESC();

	CFuncSimpleLadder();
	~CFuncSimpleLadder();

	virtual void Spawn();

	virtual void DrawDebugGeometryOverlays(void);

	void	GetTopPosition( Vector& org );
	void	GetBottomPosition( Vector& org );
	void	ComputeLadderDir( Vector& bottomToTopVec );

	void	SetEndPoints( const Vector& p1, const Vector& p2 );

	void	InputEnable( inputdata_t &inputdata );
	void	InputDisable( inputdata_t &inputdata );

	bool	IsEnabled() const;

	void	PlayerGotOn( CBasePlayer *pPlayer );
	void	PlayerGotOff( CBasePlayer *pPlayer );

	virtual void Activate();

	bool	DontGetOnLadder( void ) const;

	static int GetLadderCount();
	static CFuncSimpleLadder *GetLadder( int index );
	static CUtlVector< CFuncSimpleLadder * >	s_Ladders;
public:
	const char *GetSurfacePropName();

	void	SearchForDismountPoints();
private:

	// Movement vector from "bottom" to "top" of ladder
	CNetworkVector( m_vecLadderDir );

	// Endpoints for checking for mount/dismount
	CNetworkVector( m_vecPlayerMountPositionTop );
	CNetworkVector( m_vecPlayerMountPositionBottom );

	bool		m_bDisabled;
	CNetworkVar( bool,	m_bFakeLadder );

#if defined( GAME_DLL )
	string_t	m_surfacePropName;
	//-----------------------------------------------------
	//	Outputs
	//-----------------------------------------------------
	COutputEvent	m_OnPlayerGotOnLadder;
	COutputEvent	m_OnPlayerGotOffLadder;

	virtual int UpdateTransmitState();
#endif
};

inline bool CFuncSimpleLadder::IsEnabled() const
{
	return !m_bDisabled;
}

const char *FuncLadder_GetSurfaceprops(CBaseEntity *pLadderEntity);

#endif // FUNC_LADDER_H
