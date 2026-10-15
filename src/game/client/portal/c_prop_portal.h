//========= Portal Gun for TF2 - original implementation =================//
//
// Purpose: C_TFPortal - client-side twin of CTFPortal.
//
// RENDERING NOTE
// ==============
// C_TFPortal has no model. The base C_BaseEntity::ShouldDraw() returns
// (model != 0) -- always false for us -- so we override ShouldDraw() to
// skip that check entirely. However UpdateVisibility() is only called from
// OnDataChanged(), and for non-model entities the leaf system doesn't
// guarantee the render handle stays valid across subsequent frames.
//
// Fix: SetNextClientThink(CLIENT_THINK_ALWAYS) in OnDataChanged() +
// AddToLeafSystem() in ClientThink() every frame. This brute-forces the
// entity into the render list so DrawModel() is reliably called.
//
//==========================================================================//

#ifndef C_TF_PORTAL_H
#define C_TF_PORTAL_H
#ifdef _WIN32
#pragma once
#endif

#include "cbase.h"
#include "hl2mp_portal_shared.h"

class IMaterial;

//-----------------------------------------------------------------------------
class C_TFPortal : public C_BaseEntity
{
public:

	DECLARE_CLASS( C_TFPortal, C_BaseEntity );
	DECLARE_NETWORKCLASS();

	C_TFPortal();
	~C_TFPortal();

	// --- Accessors ---
	C_TFPortal	*GetLinkedPortal( void ) const	{ return m_hLinkedPortal.Get(); }
	int			GetPortalColor( void ) const	{ return m_nPortalColor; }
	bool		IsActive( void ) const			{ return m_bActive; }

	// --- Entity lifetime ---
	virtual void			OnDataChanged( DataUpdateType_t type ) OVERRIDE;
	virtual void			ClientThink( void ) OVERRIDE;

	// --- Rendering ---
	virtual bool			ShouldDraw( void ) OVERRIDE;
	virtual RenderGroup_t	GetRenderGroup( void ) OVERRIDE;
	virtual void			GetRenderBounds( Vector &mins, Vector &maxs ) OVERRIDE;
	virtual int				DrawModel( int flags ) OVERRIDE;

private:

	CNetworkHandle( C_TFPortal, m_hLinkedPortal );
	CNetworkVar( int, m_nPortalColor );
	CNetworkVar( bool, m_bActive );

	// Opening-animation progress, 0 (just placed) -> 1 (fully open).
	// Client-only, not networked -- eases in over time in ClientThink()
	// the same way the real portal grows rather than popping in at full
	// size instantly. Technique confirmed against a real working Source
	// implementation's approach (an ease-toward-1 curve driven by
	// FrameTime()), not guessed.
	//
	// Reset by watching for an origin change rather than the m_bActive
	// edge, since PlaceAt() also fires (with m_bActive already true) when
	// an existing portal is relocated by refiring -- watching origin
	// covers both "just created" and "just moved" with one check.
	float	m_flOpenPercent;
	Vector	m_vecLastOrigin;

public:
	// Self-registering list, same pattern as g_PointCameraList /
	// g_ReflectiveGlassList (c_point_camera.cpp / c_func_reflective_glass.cpp)
	// -- lets CViewRender::DrawTFPortalViews() find active portals each
	// frame without a general entity-list scan. At most 2 entries ever
	// exist (one per colour), so this is a trivial walk.
	C_TFPortal *m_pNext;
};

C_TFPortal *GetTFPortalList();

#endif // C_TF_PORTAL_H
