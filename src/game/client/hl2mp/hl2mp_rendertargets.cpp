//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: L4D mod render targets are specified by and accessable through this singleton
// Modified from TF2
//
// $NoKeywords: $
//===========================================================================//
#include "cbase.h"
#include "hl2mp_rendertargets.h"
#include "materialsystem/imaterialsystem.h"
#include "rendertexture.h"
#if defined( REPLAY_ENABLED )
#include "replay/replay_screenshot.h"
#endif

ConVar tf_water_resolution( "tf_water_resolution", "1024", FCVAR_NONE, "Needs to be set at game launch time to override." );
ConVar tf_monitor_resolution( "tf_monitor_resolution", "1024", FCVAR_NONE, "Needs to be set at game launch time to override." );
ConVar tf_portal_view_resolution( "tf_portal_view_resolution", "512", FCVAR_NONE, "Resolution of the live portal see-through render targets. Needs to be set at game launch time to override." );

//-----------------------------------------------------------------------------
// Purpose: Live "see through the portal" render targets -- one per portal
// colour, since both can be showing a view simultaneously. Named distinctly
// from the PORTAL-gated engine internals ("_rt_PortalBlue" etc, guarded
// behind an unrelated #ifdef PORTAL block in viewrender.cpp that this mod
// deliberately does not touch or enable) to avoid any name collision.
// Filled in every frame by CViewRender::DrawTFPortalViews (viewrender.cpp),
// hooked in next to the existing monitor system.
//-----------------------------------------------------------------------------
ITexture *CHL2MPRenderTargets::CreatePortalViewTexture( const char *pszName, IMaterialSystem* pMaterialSystem, int iWidth, int iHeight )
{
	return pMaterialSystem->CreateNamedRenderTargetTextureEx2(
		pszName,
		iWidth, iHeight, RT_SIZE_DEFAULT,
		pMaterialSystem->GetBackBufferFormat(),
		MATERIAL_RT_DEPTH_SHARED,
		0,
		CREATERENDERTARGETFLAGS_HDR );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
ITexture * CHL2MPRenderTargets::CreateItemModelPanelTexture( const char *pszName, IMaterialSystem* pMaterialSystem, int iSize )
{
	return pMaterialSystem->CreateNamedRenderTargetTextureEx2(
		pszName,
		iSize, iSize, RT_SIZE_DEFAULT,
		pMaterialSystem->GetBackBufferFormat(),
		MATERIAL_RT_DEPTH_SHARED, 
		TEXTUREFLAGS_CLAMPS | TEXTUREFLAGS_CLAMPT,
		0 );
}

//-----------------------------------------------------------------------------
// Purpose: InitClientRenderTargets, interface called by the engine at material system init in the engine
// Input  : pMaterialSystem - the interface to the material system from the engine (our singleton hasn't been set up yet)
//			pHardwareConfig - the user's hardware config, useful for conditional render targets setup
//-----------------------------------------------------------------------------
//extern const char *g_ItemModelPanelRenderTargetNames[];
//extern const char *g_pszModelImagePanelRTName;
void CHL2MPRenderTargets::InitClientRenderTargets( IMaterialSystem* pMaterialSystem, IMaterialSystemHardwareConfig* pHardwareConfig )
{
	BaseClass::InitClientRenderTargets( pMaterialSystem, pHardwareConfig, tf_water_resolution.GetInt(), tf_monitor_resolution.GetInt() );

	// rt for item model panels
	//for ( int i = 0; i < ITEM_MODEL_IMAGE_CACHE_SIZE; i++ )
	//{
	//	int index = m_hl2mpRenderTargets.AddToTail();
	//	m_tfRenderTargets[index].Init( CreateItemModelPanelTexture( g_ItemModelPanelRenderTargetNames[i], pMaterialSystem, 256 ) );
	//}

	// rt for CModelImagePanel
	//int index = m_hl2mpRenderTargets.AddToTail();
	//m_hl2mpRenderTargets[index].Init( CreateItemModelPanelTexture( g_pszModelImagePanelRTName, pMaterialSystem, 256 ) );

	// rt for the live portal see-through views (one per colour).
	// Sized to match the portal quad's real aspect ratio (64:108, from
	// TF_PORTAL_HALF_WIDTH/HEIGHT in tf_portal_shared.h) so the rendered
	// view isn't stretched when mapped onto it. 320x540 keeps that exact
	// ratio at a moderate resolution.
	int iPortalW = tf_portal_view_resolution.GetInt();
	int iPortalH = ( iPortalW * 108 ) / 64;
	int indexBlue = m_hl2mpRenderTargets.AddToTail();
	m_hl2mpRenderTargets[indexBlue].Init( CreatePortalViewTexture( "_rt_TFPortalBlue", pMaterialSystem, iPortalW, iPortalH ) );
	int indexOrange = m_hl2mpRenderTargets.AddToTail();
	m_hl2mpRenderTargets[indexOrange].Init( CreatePortalViewTexture( "_rt_TFPortalOrange", pMaterialSystem, iPortalW, iPortalH ) );
}

//-----------------------------------------------------------------------------
// Purpose: Shutdown client render targets. This gets called during shutdown in the engine
// Input  :  - 
//-----------------------------------------------------------------------------
void CHL2MPRenderTargets::ShutdownClientRenderTargets()
{
	BaseClass::ShutdownClientRenderTargets();

	for ( int i = 0; i < m_hl2mpRenderTargets.Count(); i++ )
	{
		m_hl2mpRenderTargets[i].Shutdown();
	}
	m_hl2mpRenderTargets.Purge();
}


static CHL2MPRenderTargets g_HL2MPRenderTargets;
EXPOSE_SINGLE_INTERFACE_GLOBALVAR( CHL2MPRenderTargets, IClientRenderTargets, 
	CLIENTRENDERTARGETS_INTERFACE_VERSION, g_HL2MPRenderTargets);
CHL2MPRenderTargets* g_pHL2MPRenderTargets = &g_HL2MPRenderTargets;