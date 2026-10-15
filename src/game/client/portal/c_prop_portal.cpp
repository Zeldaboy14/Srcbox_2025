//========= Portal Gun for TF2 - original implementation =================//
//
// Purpose: See c_tf_portal.h.
//
//==========================================================================//

#include "cbase.h"
#include "c_prop_portal.h"
#include "hl2mp_portal_shared.h"
#include "materialsystem/imaterialsystem.h"
#include "materialsystem/imesh.h"
#include "materialsystem/MaterialSystemUtil.h"
#include "texture_group_names.h"
#include "KeyValues.h"
#include "iefx.h"
#include "dlight.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

//-----------------------------------------------------------------------------
IMPLEMENT_NETWORKCLASS_ALIASED( TFPortal, DT_TFPortal )

BEGIN_NETWORK_TABLE( C_TFPortal, DT_TFPortal )
	RecvPropEHandle( RECVINFO( m_hLinkedPortal ) ),
	RecvPropInt( RECVINFO( m_nPortalColor ) ),
	RecvPropBool( RECVINFO( m_bActive ) ),
END_NETWORK_TABLE()

//-----------------------------------------------------------------------------
// Lazily create one shared material per portal colour.
//
// REVERTED from Portal's real portal_1/2_particle texture. Those turned out
// to be small particle-sprite glow textures (portal_1_particle.vtf is 2952
// bytes -- consistent with a soft ~32-64px glow blob meant for sparkle/dust
// effects within an actual particle system), not a hard-edged portal-shape
// asset. Stretched across a single flat quad, that's exactly the blurry
// "flame blob" look instead of a crisp ring -- wrong asset for this job,
// not a rendering bug. Back to solid vertex-color fill on proper oval ring
// geometry instead (see DrawModel).
//-----------------------------------------------------------------------------
static IMaterial *GetPortalMaterial( int nPortalColor )
{
	static IMaterial *s_pMat[PORTAL_COLOR_COUNT] = { NULL, NULL };
	if ( s_pMat[nPortalColor] )
		return s_pMat[nPortalColor];

	const char *pName = ( nPortalColor == PORTAL_COLOR_BLUE )
		? "__tf_portal_blue" : "__tf_portal_orange";

	KeyValues *pKV = new KeyValues( "UnlitGeneric" );
	pKV->SetString( "$basetexture", "vgui/white" );
	pKV->SetInt( "$translucent", 1 );
	pKV->SetInt( "$nocull",      1 );
	pKV->SetInt( "$vertexcolor", 1 );
	pKV->SetInt( "$vertexalpha", 1 );

	s_pMat[nPortalColor] = materials->CreateMaterial( pName, pKV );
	return s_pMat[nPortalColor];
}

//-----------------------------------------------------------------------------
// Self-registering list -- see header. Template instantiation pattern
// copied exactly from c_point_camera.cpp's C_EntityClassList<C_PointCamera>.
//-----------------------------------------------------------------------------
C_EntityClassList<C_TFPortal> g_TFPortalList;
template<> C_TFPortal *C_EntityClassList<C_TFPortal>::m_pClassList = NULL;

C_TFPortal *GetTFPortalList()
{
	return g_TFPortalList.m_pClassList;
}

//-----------------------------------------------------------------------------
// Live "see through the portal" material, sourced from the render targets
// CViewRender::DrawTFPortalViews() (viewrender.cpp) fills in every frame
// before the main scene draws. Referencing a render target by its
// registered name in $basetexture is the same approach the real portalred/
// portalblue.vmt use for "_rt_PortalRed" etc (confirmed by reading those
// files directly) -- just pointed at our own distinctly-named targets.
//
// Falls back to NULL (caller keeps the old flat dark disc) rather than a
// second flat-color material if the render target isn't ready yet, same
// spirit as GetPortalMaterial()'s fallback but simpler since the ring
// material is always available as the visual backstop here.
//-----------------------------------------------------------------------------
static IMaterial *GetPortalViewMaterial( int nPortalColor )
{
	static IMaterial *s_pMat[PORTAL_COLOR_COUNT] = { NULL, NULL };
	if ( s_pMat[nPortalColor] )
		return s_pMat[nPortalColor];

	const char *pMatName = ( nPortalColor == PORTAL_COLOR_BLUE )
		? "__tf_portal_blue_view" : "__tf_portal_orange_view";
	const char *pRTName = ( nPortalColor == PORTAL_COLOR_BLUE )
		? "_rt_TFPortalBlue" : "_rt_TFPortalOrange";

	KeyValues *pKV = new KeyValues( "UnlitGeneric" );
	pKV->SetString( "$basetexture", pRTName );
	pKV->SetInt( "$nocull",   1 );
	pKV->SetInt( "$nofog",    1 );	// already a fully-lit rendered scene, don't fog it a second time
	pKV->SetInt( "$vertexalpha", 1 );

	IMaterial *pMat = materials->CreateMaterial( pMatName, pKV );
	if ( !pMat || pMat->IsErrorMaterial() )
		return NULL;

	s_pMat[nPortalColor] = pMat;
	return s_pMat[nPortalColor];
}

//-----------------------------------------------------------------------------
C_TFPortal::C_TFPortal()
{
	m_nPortalColor = PORTAL_COLOR_BLUE;
	m_bActive      = false;
	m_flOpenPercent = 0.0f;
	m_vecLastOrigin.Init();

	g_TFPortalList.Insert( this );
}

C_TFPortal::~C_TFPortal()
{
	g_TFPortalList.Remove( this );
}

//-----------------------------------------------------------------------------
// OnDataChanged fires whenever any networked var changes.
// We use DATA_UPDATE_CREATED to start the always-on think that keeps the
// entity in the leaf system.  We also call UpdateVisibility() every time so
// the entity enters / leaves the render list when m_bActive flips.
//-----------------------------------------------------------------------------
void C_TFPortal::OnDataChanged( DataUpdateType_t type )
{
	BaseClass::OnDataChanged( type );

	if ( type == DATA_UPDATE_CREATED )
	{
		// Start a permanent think to re-add to the leaf system every frame.
		// Without this, non-model entities can silently drop out of the
		// render list between OnDataChanged calls.
		SetNextClientThink( CLIENT_THINK_ALWAYS );
	}

	UpdateVisibility();
}

//-----------------------------------------------------------------------------
// Called every frame (CLIENT_THINK_ALWAYS).
//
// Three jobs:
//   1. Re-insert into the leaf system so DrawModel() keeps getting called.
//   2. Ease m_flOpenPercent toward 1 -- an opening-grow animation instead of
//      popping to full size instantly. Reset to 0 whenever the origin moves
//      (covers both first placement and being relocated by a refire).
//      Easing curve technique confirmed against a real working Source
//      implementation (ease-toward-target scaled by FrameTime and remaining
//      distance, not a guessed formula).
//   3. Maintain a dynamic light matching the portal's color so it actually
//      casts light on nearby surfaces -- confirmed real API via
//      effects->CL_AllocDlight() (see dlight.h / c_te_flare.cpp for the
//      reference usage this follows). Using GetEntityIndex() as the light's
//      key keeps it stable/updated in place across frames rather than
//      allocating a new one each time.
//-----------------------------------------------------------------------------
void C_TFPortal::ClientThink( void )
{
	if ( !m_bActive )
		return;

	AddToLeafSystem( RENDER_GROUP_TRANSLUCENT_ENTITY );

	// --- Opening animation ---
	const Vector vecOrigin = GetAbsOrigin();
	if ( vecOrigin.DistToSqr( m_vecLastOrigin ) > 1.0f )
	{
		m_flOpenPercent = 0.0f;
		m_vecLastOrigin = vecOrigin;
	}

	if ( m_flOpenPercent < 1.0f )
	{
		float flRate = gpGlobals->frametime * 3.0f * ( 1.0f - m_flOpenPercent + 0.1f );
		m_flOpenPercent = clamp( m_flOpenPercent + flRate, 0.0f, 1.0f );
	}

	// --- Dynamic light ---
	dlight_t *dl = effects->CL_AllocDlight( entindex() );

	Vector vecForward;
	AngleVectors( GetAbsAngles(), &vecForward );
	dl->origin = vecOrigin + vecForward * 8.0f;

	if ( m_nPortalColor == PORTAL_COLOR_BLUE )
	{ dl->color.r = 60;  dl->color.g = 160; dl->color.b = 255; }
	else
	{ dl->color.r = 255; dl->color.g = 130; dl->color.b = 40;  }
	dl->color.exponent = 5;

	dl->radius = m_flOpenPercent * 180.0f;
	dl->die    = gpGlobals->curtime + 0.1f;	// re-allocated every think, so this just needs to outlive one frame
	dl->decay  = 0.0f;
}

//-----------------------------------------------------------------------------
// We render via dynamic mesh -- no model file involved.
// The base C_BaseEntity::ShouldDraw() returns (model != 0) which is always
// false here, so we override it and skip that check entirely.
//-----------------------------------------------------------------------------
bool C_TFPortal::ShouldDraw( void )
{
	if ( GetRenderMode() == kRenderNone )
		return false;
	if ( IsEffectActive( EF_NODRAW ) )
		return false;
	return m_bActive;
}

//-----------------------------------------------------------------------------
RenderGroup_t C_TFPortal::GetRenderGroup( void )
{
	return RENDER_GROUP_TRANSLUCENT_ENTITY;
}

//-----------------------------------------------------------------------------
// Local-space AABB for the leaf system.
// Portal local frame: +X = outward surface normal, +Y = width, +Z = height.
//-----------------------------------------------------------------------------
void C_TFPortal::GetRenderBounds( Vector &mins, Vector &maxs )
{
	mins.Init( -TF_PORTAL_TRIGGER_DEPTH, -TF_PORTAL_HALF_WIDTH, -TF_PORTAL_HALF_HEIGHT );
	maxs.Init(  TF_PORTAL_TRIGGER_DEPTH,  TF_PORTAL_HALF_WIDTH,  TF_PORTAL_HALF_HEIGHT );
}

//-----------------------------------------------------------------------------
// Draws a procedural oval ring (matching the real Portal silhouette) instead
// of the old flat rectangle:
//   Ring band   -- bright portal color, a thin oval annulus (MATERIAL_QUADS,
//                  one quad per segment around the loop)
//   Inner fill  -- dark tinted disc filling the ring's interior
//                  (MATERIAL_POLYGON, auto-triangulated fan), standing in
//                  for "portal opening, no see-through view yet"
// 1.5-unit nudge along the surface normal prevents z-fighting with the wall.
//-----------------------------------------------------------------------------
int C_TFPortal::DrawModel( int flags )
{
	if ( !m_bActive )
		return 0;

	IMaterial *pMat = GetPortalMaterial( m_nPortalColor );
	// GetPortalMaterial() always returns a usable (non-error) material --
	// this only fires if even vgui/white fails, i.e. the materialsystem
	// itself is in a bad state, not a portal-specific problem.
	if ( !pMat || pMat->IsErrorMaterial() )
		return 0;

	Vector vecForward, vecRight, vecUp;
	AngleVectors( GetAbsAngles(), &vecForward, &vecRight, &vecUp );

	const Vector vecOrigin = GetAbsOrigin() + vecForward * 1.5f;

	unsigned char r, g, b;
	if ( m_nPortalColor == PORTAL_COLOR_BLUE )
	{ r = 60;  g = 160; b = 255; }
	else
	{ r = 255; g = 130; b = 40;  }

	const float W = TF_PORTAL_HALF_WIDTH  * m_flOpenPercent;
	const float H = TF_PORTAL_HALF_HEIGHT * m_flOpenPercent;
	const float flInnerScale = 0.78f;	// ring band thickness, as a fraction of W/H
	const int   NSEG = 24;				// segments around the loop -- smooth enough, cheap enough

	// Skip drawing entirely at the very start of the open animation --
	// a zero-size ring/fill is wasted draw calls, and W==0 would also
	// degenerate the ring band into zero-area quads.
	if ( W < 0.5f || H < 0.5f )
		return 1;

	CMatRenderContextPtr pRenderContext( materials );
	pRenderContext->Bind( pMat );

	// ---- Outer ring band ----
	{
		IMesh *pMesh = pRenderContext->GetDynamicMesh( true );
		CMeshBuilder mb;
		mb.Begin( pMesh, MATERIAL_QUADS, NSEG );

		for ( int i = 0; i < NSEG; ++i )
		{
			float theta0 = (float)i       / (float)NSEG * 2.0f * M_PI;
			float theta1 = (float)(i + 1) / (float)NSEG * 2.0f * M_PI;

			Vector outer0 = vecOrigin + vecRight * ( W * cosf( theta0 ) )               + vecUp * ( H * sinf( theta0 ) );
			Vector outer1 = vecOrigin + vecRight * ( W * cosf( theta1 ) )               + vecUp * ( H * sinf( theta1 ) );
			Vector inner1 = vecOrigin + vecRight * ( W * flInnerScale * cosf( theta1 ) ) + vecUp * ( H * flInnerScale * sinf( theta1 ) );
			Vector inner0 = vecOrigin + vecRight * ( W * flInnerScale * cosf( theta0 ) ) + vecUp * ( H * flInnerScale * sinf( theta0 ) );

			mb.Position3fv( outer0.Base() ); mb.Color4ub( r, g, b, 255 ); mb.TexCoord2f( 0, 0, 0 ); mb.AdvanceVertex();
			mb.Position3fv( outer1.Base() ); mb.Color4ub( r, g, b, 255 ); mb.TexCoord2f( 0, 1, 0 ); mb.AdvanceVertex();
			mb.Position3fv( inner1.Base() ); mb.Color4ub( r, g, b, 255 ); mb.TexCoord2f( 0, 1, 1 ); mb.AdvanceVertex();
			mb.Position3fv( inner0.Base() ); mb.Color4ub( r, g, b, 255 ); mb.TexCoord2f( 0, 0, 1 ); mb.AdvanceVertex();
		}

		mb.End();
		pMesh->Draw();
	}

	// ---- Inner fill ----
	// Tries the live "see through the portal" render target first. Falls
	// back to the original flat dark disc if that material isn't ready
	// (e.g. very first frame before render targets finish initializing).
	//
	// MATERIAL_POLYGON takes vertex count directly (not primitive count) and
	// auto-triangulates as a fan from N boundary vertices -- confirmed via
	// CMeshBuilder::ComputeNumVertsAndIndices in imesh.h. The fan's apex is
	// vertex[0] on the boundary, not a true geometric center, so per-vertex
	// UVs are computed from each vertex's actual angular position rather
	// than left at a fixed value -- otherwise the live view would warp
	// toward vertex[0] instead of mapping cleanly across the disc.
	{
		IMaterial *pViewMat = GetPortalViewMaterial( m_nPortalColor );
		bool bHasLiveView = ( pViewMat != NULL );

		pRenderContext->Bind( bHasLiveView ? pViewMat : pMat );

		IMesh *pMesh = pRenderContext->GetDynamicMesh( true );
		CMeshBuilder mb;
		mb.Begin( pMesh, MATERIAL_POLYGON, NSEG );

		for ( int i = 0; i < NSEG; ++i )
		{
			float theta = (float)i / (float)NSEG * 2.0f * M_PI;
			Vector p = vecOrigin + vecRight * ( W * flInnerScale * cosf( theta ) ) + vecUp * ( H * flInnerScale * sinf( theta ) );

			mb.Position3fv( p.Base() );

			if ( bHasLiveView )
			{
				// Full brightness, untinted -- this is an actual rendered
				// scene, the ring band already carries the color identity.
				mb.Color4ub( 255, 255, 255, 255 );
				mb.TexCoord2f( 0, 0.5f + 0.5f * cosf( theta ), 0.5f + 0.5f * sinf( theta ) );
			}
			else
			{
				mb.Color4ub( r / 5, g / 5, b / 5, 210 );
				mb.TexCoord2f( 0, 0.5f, 0.5f );
			}

			mb.AdvanceVertex();
		}

		mb.End();
		pMesh->Draw();
	}

	return 1;
}
