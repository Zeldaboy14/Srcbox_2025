//========= Portal Gun for TF2 - original implementation =================//
//
// Purpose: CTFPortal - one placed portal. Two of these, linked to each
//          other, are what the portal gun creates. Position/orientation
//          come from the weapon's placement trace; the teleport itself
//          (position, eye angles, velocity all transformed together, with
//          speed magnitude preserved) happens in StartTouch below, using
//          the transform in tf_portal_shared.h/.cpp.
//
// Scope note: this pass handles living TF players only. Physics props,
// projectiles, and buildings passing through portals are a deliberate
// fast-follow, not implemented here yet.
//
//==========================================================================//

#ifndef TF_PORTAL_H
#define TF_PORTAL_H
#ifdef _WIN32
#pragma once
#endif

#include "cbase.h"
#include "hl2mp_portal_shared.h"

//-----------------------------------------------------------------------------
// Purpose: One end of a linked portal pair.
//-----------------------------------------------------------------------------
class CTFPortal : public CBaseEntity
{
public:

	DECLARE_CLASS( CTFPortal, CBaseEntity );
	DECLARE_NETWORKCLASS();

	CTFPortal();

	virtual void	Precache( void );
	virtual void	Spawn( void );
	virtual void	StartTouch( CBaseEntity *pOther );

	// Called by the weapon when firing/re-firing this color of portal.
	// Activates the portal (if it was dormant) and moves/orients it.
	void			PlaceAt( const Vector &vecOrigin, const QAngle &angles, const Vector &vecPlacementNormal );

	// Mutual link - also links pOther back to us.
	void			LinkTo( CTFPortal *pOther );

	CTFPortal		*GetLinkedPortal( void ) const	{ return m_hLinkedPortal.Get(); }
	int				GetPortalColor( void ) const	{ return m_nPortalColor; }
	void			SetPortalColor( int nColor )	{ m_nPortalColor = nColor; }
	bool			IsActive( void ) const			{ return m_bActive; }

	const Vector	&GetPortalNormal( void ) const	{ return m_vecPortalNormal; }

private:

	bool			CanTeleport( CBaseEntity *pOther ) const;
	void			TeleportEntity( CBaseEntity *pOther, CTFPortal *pExitPortal );

	CNetworkHandle( CTFPortal, m_hLinkedPortal );
	CNetworkVar( int, m_nPortalColor );
	CNetworkVar( bool, m_bActive );

	// Surface normal at placement time - kept separately from GetAbsAngles()
	// forward vector purely for readability at call sites; they're the same
	// vector under the hood (angles are built FROM this normal).
	Vector			m_vecPortalNormal;

	// Re-touch guard: which entity we most recently sent OUT of this portal,
	// and when, so the moment they arrive here doesn't immediately trigger
	// again while they're still inside the trigger volume.
	EHANDLE			m_hLastTeleported;
	float			m_flLastTeleportTime;
};

#endif // TF_PORTAL_H
