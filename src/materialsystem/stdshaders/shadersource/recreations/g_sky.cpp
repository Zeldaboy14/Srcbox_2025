// ----------------------------------------------------------------------------
// MYSHADER.CPP
//
// This file defines the C++ component of the example shader.
// ----------------------------------------------------------------------------

//#define SCRNSPACE 1

// ----------------------------------------------------------------------------
// Includes
// ----------------------------------------------------------------------------

// Must include this. Contains a bunch of macro definitions along with the
// declaration of CBaseShader.
//#include "BaseVSShader.h"
#include "../cpp_lux_shared.h"

// We're going to be making a screenspace effect. Therefore, we need the
// screenspace vertex shader.
#ifdef SCRNSPACE
#include "SDK_screenspaceeffect_vs20.inc"
#endif

// We also need to include the pixel shader for our own shader.
// Note that the shader compiler generates both 2.0 and 2.0b versions.
// Need to include both.
#include "notlux_g_sky_vs30.inc"
#include "notlux_g_sky_ps30.inc"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

#ifndef VERTEX_TEXCOORD0
#define VERTEX_TEXCOORD0 (1 << 2)
#endif

// ----------------------------------------------------------------------------
// This macro defines the start of the shader. Effectively, every shader is
// 
// ----------------------------------------------------------------------------
BEGIN_SHADER(g_sky, "")

// ----------------------------------------------------------------------------
// This block is where you'd define inputs that users can feed to your
// shader.
// ----------------------------------------------------------------------------
BEGIN_SHADER_PARAMS
END_SHADER_PARAMS

// ----------------------------------------------------------------------------
// This is the shader initialization block. This disgusting macro defines
// a bunch of ick that makes this shader work.
// ----------------------------------------------------------------------------
SHADER_INIT
{

}

// ----------------------------------------------------------------------------
// We want this shader to operate on the frame buffer itself. Therefore,
// we need to set this to true.
// ----------------------------------------------------------------------------
bool NeedsFullFrameBufferTexture(IMaterialVar **params, bool bCheckSpecificToThisFrame /* = true */) const
{
#ifdef SCRNSPACE
	return true;
#else
	return false;
#endif
}

// ----------------------------------------------------------------------------
// This block should return the name of the shader to fall back to if
// we fail to bind this shader for any reason.
// ----------------------------------------------------------------------------
SHADER_FALLBACK
{
	// Requires DX9 + above
	if (g_pHardwareConfig->GetDXSupportLevel() < 90)
	{
		Assert(0);
		return "Wireframe";
	}
	return 0;
}

// ----------------------------------------------------------------------------
// This implements the guts of the shader drawing code.
// ----------------------------------------------------------------------------
SHADER_DRAW
{
	// ----------------------------------------------------------------------------
	// This section is called when the shader is bound for the first time.
	// You should setup any static state variables here.
	// ----------------------------------------------------------------------------
	SHADOW_STATE
	{
		// Setup the vertex format.
		#ifdef SCRNSPACE
		int fmt = VERTEX_POSITION;
		pShaderShadow->VertexShaderVertexFormat(fmt, 1, 0, 0);
		#else

		int flags = VERTEX_FORMAT_COMPRESSED | VERTEX_POSITION;

		pShaderShadow->VertexShaderVertexFormat(flags, 1, 0, 0);

		#endif

		// We don't need to write to the depth buffer.
		pShaderShadow->EnableDepthWrites(false);

		// Precache and set the screenspace shader.
		#ifdef SCRNSPACE
		DECLARE_STATIC_VERTEX_SHADER(sdk_screenspaceeffect_vs20);
		SET_STATIC_VERTEX_SHADER(sdk_screenspaceeffect_vs20);
		#endif

		DECLARE_STATIC_VERTEX_SHADER(notlux_g_sky_vs30);
		SET_STATIC_VERTEX_SHADER(notlux_g_sky_vs30);

		// Precache and set the example shader.
		if (g_pHardwareConfig->SupportsPixelShaders_2_b())
		{
			DECLARE_STATIC_PIXEL_SHADER(notlux_g_sky_ps30);
			SET_STATIC_PIXEL_SHADER(notlux_g_sky_ps30);
		}
		else
		{
			DECLARE_STATIC_PIXEL_SHADER(notlux_g_sky_ps30);
			SET_STATIC_PIXEL_SHADER(notlux_g_sky_ps30);
		}
	}

		// ----------------------------------------------------------------------------
		// This section is called every frame.
		// ----------------------------------------------------------------------------
		DYNAMIC_STATE
	{
		// Use the sdk_screenspaceeffect_vs20 vertex shader.
		#ifdef SCRNSPACE
		DECLARE_DYNAMIC_VERTEX_SHADER(sdk_screenspaceeffect_vs20);
		SET_DYNAMIC_VERTEX_SHADER(sdk_screenspaceeffect_vs20);
		#endif

		DECLARE_DYNAMIC_VERTEX_SHADER(notlux_g_sky_vs30);
		SET_DYNAMIC_VERTEX_SHADER(notlux_g_sky_vs30);

		// Use our custom pixel shader.
		if (g_pHardwareConfig->SupportsPixelShaders_2_b())
		{
			DECLARE_DYNAMIC_PIXEL_SHADER(notlux_g_sky_ps30);
			SET_DYNAMIC_PIXEL_SHADER(notlux_g_sky_ps30);
		}
		else
		{
			DECLARE_DYNAMIC_PIXEL_SHADER(notlux_g_sky_ps30);
			SET_DYNAMIC_PIXEL_SHADER(notlux_g_sky_ps30);
		}
	}

		// NEVER FORGET THIS CALL! This is what actually
		// draws your shader!
		// ALWAYS make this last, or else things will happen.
	Draw();
}

END_SHADER