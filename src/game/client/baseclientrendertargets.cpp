//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Implementation for CBaseClientRenderTargets class.
//			Provides Init functions for common render textures used by the engine.
//			Mod makers can inherit from this class, and call the Create functions for
//			only the render textures the want for their mod.
//=============================================================================//

#include "cbase.h"
#include "baseclientrendertargets.h"						// header	
#include "materialsystem/imaterialsystemhardwareconfig.h"	// Hardware config checks
#include "tier0/icommandline.h"
//#ifdef LOW_LEVEL_DX9
#include "shaderapi/IShaderDevice.h"

IDirect3DDevice9* g_pDirect3DDevice9 = NULL;
//#endif

#undef interface

static bool FindDX9Device()
{
	// Try both DXVK and DirectX 9 mode
	CreateInterfaceFn interface = Sys_GetFactory("shaderapivk");
	if (!interface)
		interface = Sys_GetFactory("shaderapidx9");

	if (!interface)
		return false;

	IShaderDevice* pShaderDevice = (IShaderDevice*)interface(SHADER_DEVICE_INTERFACE_VERSION, NULL);
	if (!pShaderDevice)
		return false;

	// Dereference the virtual table and access the IsUsingGraphics method
	byte* pIShaderDevice_IsUsingGraphics = (byte*)(*(void***)pShaderDevice)[5];
	// Resolve the RIP instruction to get the absolute address
	byte* ppDirect3DDevice9 = pIShaderDevice_IsUsingGraphics + 8 + *(int32*)(pIShaderDevice_IsUsingGraphics + 3);
	g_pDirect3DDevice9 = *(IDirect3DDevice9**)ppDirect3DDevice9;

	return g_pDirect3DDevice9 != NULL;
}

ITexture* CBaseClientRenderTargets::CreateWaterReflectionTexture( IMaterialSystem* pMaterialSystem, int iSize )
{
	return pMaterialSystem->CreateNamedRenderTargetTextureEx2(
		"_rt_WaterReflection",
		iSize, iSize, RT_SIZE_PICMIP,
		pMaterialSystem->GetBackBufferFormat(), 
		MATERIAL_RT_DEPTH_SHARED, 
		TEXTUREFLAGS_CLAMPS | TEXTUREFLAGS_CLAMPT,
		CREATERENDERTARGETFLAGS_HDR );
}

ITexture* CBaseClientRenderTargets::CreateWaterRefractionTexture( IMaterialSystem* pMaterialSystem, int iSize )
{
	return pMaterialSystem->CreateNamedRenderTargetTextureEx2(
		"_rt_WaterRefraction",
		iSize, iSize, RT_SIZE_PICMIP,
		// This is different than reflection because it has to have alpha for fog factor.
		IMAGE_FORMAT_RGBA8888, 
		MATERIAL_RT_DEPTH_SHARED, 
		TEXTUREFLAGS_CLAMPS | TEXTUREFLAGS_CLAMPT,
		CREATERENDERTARGETFLAGS_HDR );
}

ITexture* CBaseClientRenderTargets::CreateCameraTexture( IMaterialSystem* pMaterialSystem, int iSize )
{
	return pMaterialSystem->CreateNamedRenderTargetTextureEx2(
		"_rt_Camera",
		iSize, iSize, RT_SIZE_DEFAULT,
		pMaterialSystem->GetBackBufferFormat(),
		MATERIAL_RT_DEPTH_SHARED, 
		0,
		CREATERENDERTARGETFLAGS_HDR );
}

//-----------------------------------------------------------------------------
// Purpose: Called by the engine in material system init and shutdown.
//			Clients should override this in their inherited version, but the base
//			is to init all standard render targets for use.
// Input  : pMaterialSystem - the engine's material system (our singleton is not yet inited at the time this is called)
//			pHardwareConfig - the user hardware config, useful for conditional render target setup
//-----------------------------------------------------------------------------
void CBaseClientRenderTargets::InitClientRenderTargets( IMaterialSystem* pMaterialSystem, IMaterialSystemHardwareConfig* pHardwareConfig, int iWaterTextureSize, int iCameraTextureSize )
{
	if (!FindDX9Device())
	{
		Error("Failed to get DirectX9 device pointer");
		return;
	}
	// Water effects
	m_WaterReflectionTexture.Init( CreateWaterReflectionTexture( pMaterialSystem, iWaterTextureSize ) );
	m_WaterRefractionTexture.Init( CreateWaterRefractionTexture( pMaterialSystem, iWaterTextureSize ) );

	// Monitors
	m_CameraTexture.Init( CreateCameraTexture( pMaterialSystem, iCameraTextureSize ) );
}

//-----------------------------------------------------------------------------
// Purpose: Shut down each CTextureReference we created in InitClientRenderTargets.
//			Called by the engine in material system shutdown.
// Input  :  - 
//-----------------------------------------------------------------------------
void CBaseClientRenderTargets::ShutdownClientRenderTargets()
{
	// Water effects
	m_WaterReflectionTexture.Shutdown();
	m_WaterRefractionTexture.Shutdown();

	// Monitors
	m_CameraTexture.Shutdown();
}

//#include "../../dx9sdk/include/d3d9.h"

// ...
/*void ExampleToFetchSwapChain()
{
	IDirect3DSwapChain9* pSwapChain = NULL;
	g_pDirect3DDevice9->GetSwapChain(0, &pSwapChain);

	//SSAO//(c) Michael Auerbach 2008
	float pos = sqrt(tex2D(depth, tex).r);
	half occlusion = 0;
	float sample;
	float pPos;
	half dist = (tex.x * 1440) % 8;
	dist += (tex.y * 900) % 8;
	dist += 1;
	half3 pNorm;
	float SSnorm;
	float3 vec;
	for (int j = 1;j < 3;j++)
	{
		for (int i = 0;i < 180;i += 45)
		{
			pPos = sqrt(tex2D(depth, tex + float2(cos(i), sin(i)) * j * dist * vecViewPort.zw).r);
			SSnorm = sqrt(tex2D(depth, tex - float2(cos(i), sin(i)) * j * dist * vecViewPort.zw).r);
			//SSnorm -= pos;
			sample = (pos / pPos) - (SSnorm / pos);
			if ((sample > 0.001) && (sample < 0.05))
				occlusion += (sample * 30);
		}
	}
}*/