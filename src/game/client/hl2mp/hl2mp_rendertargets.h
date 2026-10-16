//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
//
// $Workfile:     $
// $Date:         $
// $NoKeywords: $
//===========================================================================//
#ifndef TF_RENDERTARGETS_H
#define TF_RENDERTARGETS_H
#ifdef _WIN32
#pragma once
#endif

#include "baseclientrendertargets.h" // Base class, with interfaces called by engine and inherited members to init common render targets

// externs
class IMaterialSystem;
class IMaterialSystemHardwareConfig;

class CHL2MPRenderTargets : public CBaseClientRenderTargets
{
	// no networked vars
	DECLARE_CLASS_GAMEROOT( CHL2MPRenderTargets, CBaseClientRenderTargets );
public:
	virtual void InitClientRenderTargets( IMaterialSystem* pMaterialSystem, IMaterialSystemHardwareConfig* pHardwareConfig );
	virtual void ShutdownClientRenderTargets();

private:
	ITexture *CreateItemModelPanelTexture( const char *pszName, IMaterialSystem* pMaterialSystem, int iSize );
	ITexture *CreatePortalViewTexture( const char *pszName, IMaterialSystem* pMaterialSystem, int iWidth, int iHeight );

private:
	// Used for rendering item model panels.
	CUtlVector< CTextureReference >		m_hl2mpRenderTargets;
};

extern CHL2MPRenderTargets* g_pHL2MPRenderTargets;


#endif // TF_RENDERTARGETS_H
