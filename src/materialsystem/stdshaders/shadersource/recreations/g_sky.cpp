//===================== File of the Srcbox Shader Project (Under LUX) =====================//
//
//	Initial D.	:	23.4.2026 DMY
//	Last Change :	16.10.2026 DMY
//
//	Recreation of Garry's Mod's g_sky shader (game_shader_generic_garrysmod.dll),
//	rebuilt from the original DLL and its g_sky_* shader bytecode.
//
//	What GMod's version does, for reference:
//	- Params as below; GMod never fills in defaults itself (the SkyPaint proxy
//	  or the vmt supply the values). We fill in GMod's documented defaults so
//	  a bare vmt still renders something sensible.
//	- No SetInitialShadowState: depth writes off, no blending, sRGB write on,
//	  alpha writes on, $StarTexture on s0 without sRGB read (LoadTexture flags 0).
//	- $HDRScale is only applied when HDR is enabled; otherwise 1.0.
//	- The pixel shader receives 1 / $StarScale (left untouched if $StarScale is 0).
//	- Static combo SKY_LAYERS = $StarLayers (0..3, 4 aliased to 3).
//	- ps_2_0 / ps_1_1 fallbacks: no stars / flat average of top+bottom.
//	  Not needed here, LUX is SM3 only.
//
//=========================================================================================//

#include "../cpp_lux_shared.h"

#include "notlux_g_sky_vs30.inc"
#include "notlux_g_sky_ps30.inc"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

BEGIN_VS_SHADER(g_sky, "Sky Shader native to Garry's Mod. Driven by env_skypaint through the SkyPaint proxy.")
SHADER_INFO_GEOMETRY("Skybox.")
SHADER_INFO_USAGE("Used by skybox/painted*.vmt with the SkyPaint proxy, or set the parameters directly.")
SHADER_INFO_LIMITATIONS("No fog.")
SHADER_INFO_PERFORMANCE("Cheap. Each star layer is one extra texture sample.")
SHADER_INFO_FALLBACK("A DXLevel below 90 will cause a Fallback to the Wireframe Shader.")
SHADER_INFO_WEBLINKS(WEBLINK_VDC
	"VDC env_skypaint entity Page (hand in hand with this): https://developer.valvesoftware.com/wiki/Env_skypaint")
SHADER_INFO_D3D(LUX_SHADERINFO_SM30)

// Defaults are GMod's own
BEGIN_SHADER_PARAMS
	SHADER_PARAM(TopColor,		SHADER_PARAM_TYPE_VEC3,		"[0 0 1]",		"Colour of the top of the sky.")
	SHADER_PARAM(BottomColor,	SHADER_PARAM_TYPE_VEC3,		"[0 1 1]",		"Colour of the bottom of the sky.")
	SHADER_PARAM(FadeBias,		SHADER_PARAM_TYPE_FLOAT,	"1.0",			"Bias of the fade between top/bottom. (1.0 is even)")
	SHADER_PARAM(HDRScale,		SHADER_PARAM_TYPE_FLOAT,	"1.0",			"Output scale, HDR mode only.")
	SHADER_PARAM(SunNormal,		SHADER_PARAM_TYPE_VEC3,		"[0 1 0.5]",	"Direction to the sun. Zero disables the sun and its dusk pull.")
	SHADER_PARAM(SunColor,		SHADER_PARAM_TYPE_VEC3,		"[1 1 1]",		"Colour of the sun glow. (additive)")
	SHADER_PARAM(SunSize,		SHADER_PARAM_TYPE_FLOAT,	"1",			"Size of the sun glow. 0 disables it.")
	SHADER_PARAM(DuskColor,		SHADER_PARAM_TYPE_VEC3,		"[1 0.4 0]",	"Colour of the dusk effect.")
	SHADER_PARAM(DuskScale,		SHADER_PARAM_TYPE_FLOAT,	"1.0",			"Size of the dusk band along the horizon.")
	SHADER_PARAM(DuskIntensity,	SHADER_PARAM_TYPE_FLOAT,	"1.0",			"Strength of the dusk effect.")
	SHADER_PARAM(StarTexture,	SHADER_PARAM_TYPE_TEXTURE,	"skybox/stars",	"[RGB] Star/cloud texture.")
	SHADER_PARAM(StarScale,		SHADER_PARAM_TYPE_FLOAT,	"1",			"Size of the star texture.")
	SHADER_PARAM(StarFade,		SHADER_PARAM_TYPE_FLOAT,	"1",			"How quickly stars fade towards the horizon.")
	SHADER_PARAM(StarPos,		SHADER_PARAM_TYPE_FLOAT,	"1",			"Star scroll offset. The SkyPaint proxy sets StarSpeed * RealTime.")
	SHADER_PARAM(StarLayers,	SHADER_PARAM_TYPE_INTEGER,	"0",			"0 to 3 star layers. 0 disables stars.")
END_SHADER_PARAMS

SHADER_INIT_PARAMS()
{
	DefaultFloat3(TopColor, 0.0f, 0.0f, 1.0f);
	DefaultFloat3(BottomColor, 0.0f, 1.0f, 1.0f);
	DefaultFloat(FadeBias, 1.0f);
	DefaultFloat(HDRScale, 1.0f);
	DefaultFloat3(SunNormal, 0.0f, 1.0f, 0.5f);
	DefaultFloat3(SunColor, 1.0f, 1.0f, 1.0f);
	DefaultFloat(SunSize, 1.0f);
	DefaultFloat3(DuskColor, 1.0f, 0.4f, 0.0f);
	DefaultFloat(DuskScale, 1.0f);
	DefaultFloat(DuskIntensity, 1.0f);
	DefaultFloat(StarScale, 1.0f);
	DefaultFloat(StarFade, 1.0f);
	DefaultFloat(StarPos, 1.0f);
	DefaultInt(StarLayers, 0);

	if (!IsDefined(StarTexture))
		m_ppParams[StarTexture]->SetStringValue("skybox/stars");
}

SHADER_FALLBACK
{
	if (g_pHardwareConfig->GetDXSupportLevel() < 90)
		return "Wireframe";
	return 0;
}

SHADER_INIT
{
	// GMod: LoadTexture(STARTEXTURE, 0), and sRGB read is disabled on s0
	LoadTexture(StarTexture, 0);
}

SHADER_DRAW
{
	bool bHasStarTexture = IsTextureLoaded(StarTexture);
	int nStarLayers = bHasStarTexture ? clamp(GetInt(StarLayers), 0, 3) : 0;

	if (IsSnapshotting())
	{
		// GMod sets exactly this and nothing else (no SetInitialShadowState)
		pShaderShadow->EnableDepthWrites(false);
		pShaderShadow->EnableBlending(false);
		pShaderShadow->EnableSRGBWrite(true);
		pShaderShadow->EnableAlphaWrites(true);

		pShaderShadow->VertexShaderVertexFormat(VERTEX_POSITION, 1, NULL, 0);

		EnableSampler(nStarLayers > 0, SHADER_SAMPLER0, false);

		DECLARE_STATIC_VERTEX_SHADER(notlux_g_sky_vs30);
		SET_STATIC_VERTEX_SHADER(notlux_g_sky_vs30);

		DECLARE_STATIC_PIXEL_SHADER(notlux_g_sky_ps30);
		SET_STATIC_PIXEL_SHADER_COMBO(STARLAYERS, nStarLayers);
		SET_STATIC_PIXEL_SHADER(notlux_g_sky_ps30);
	}

	if (IsDynamicState())
	{
		if (nStarLayers > 0)
			BindTexture(SHADER_SAMPLER0, StarTexture, -1);

		SetPixelShaderCameraPosition(LUX_PS_FLOAT_CAMERAPOSITION);

		float3 f3Top		= GetFloat3(TopColor);
		float3 f3Bottom		= GetFloat3(BottomColor);
		float3 f3SunNormal	= GetFloat3(SunNormal);
		float3 f3SunColor	= GetFloat3(SunColor);
		float3 f3DuskColor	= GetFloat3(DuskColor);

		// GMod only honours $HDRScale with HDR on. Ask whether HDR is actually enabled, like the
		// rest of LUX does: GetHDRType() said HDR_TYPE_NONE here even in HDR, so the scale was ignored.
		float f1HDRScale = IsHDREnabled() ? GetFloat(HDRScale) : 1.0f;

		// GMod sends 1 / $StarScale and skips the update when it is 0
		float f1StarScale = GetFloat(StarScale);
		f1StarScale = (f1StarScale != 0.0f) ? (1.0f / f1StarScale) : 1.0f;

		float c1[4] = { f3Top.x,		f3Top.y,		f3Top.z,		GetFloat(FadeBias) };
		float c2[4] = { f3Bottom.x,	f3Bottom.y,	f3Bottom.z,	f1HDRScale };
		float c3[4] = { f3SunNormal.x,	f3SunNormal.y,	f3SunNormal.z,	GetFloat(SunSize) };
		float c4[4] = { f3SunColor.x,	f3SunColor.y,	f3SunColor.z,	GetFloat(DuskScale) };
		float c5[4] = { f3DuskColor.x,	f3DuskColor.y,	f3DuskColor.z,	GetFloat(DuskIntensity) };
		float c6[4] = { f1StarScale,	GetFloat(StarFade),	GetFloat(StarPos),	0.0f };

		pShaderAPI->SetPixelShaderConstant(REGISTER_FLOAT_001, c1);
		pShaderAPI->SetPixelShaderConstant(REGISTER_FLOAT_002, c2);
		pShaderAPI->SetPixelShaderConstant(REGISTER_FLOAT_003, c3);
		pShaderAPI->SetPixelShaderConstant(REGISTER_FLOAT_004, c4);
		pShaderAPI->SetPixelShaderConstant(REGISTER_FLOAT_005, c5);
		pShaderAPI->SetPixelShaderConstant(REGISTER_FLOAT_006, c6);

		BOOL BBools[REGISTER_BOOL_MAX] = { false };
		BBools[LUX_PS_BOOL_DEPTHTODESTALPHA] = pShaderAPI->ShouldWriteDepthToDestAlpha();
		pShaderAPI->SetBooleanPixelShaderConstant(REGISTER_BOOL_START, BBools, REGISTER_BOOL_MAX);

		DECLARE_DYNAMIC_VERTEX_SHADER(notlux_g_sky_vs30);
		SET_DYNAMIC_VERTEX_SHADER(notlux_g_sky_vs30);

		DECLARE_DYNAMIC_PIXEL_SHADER(notlux_g_sky_ps30);
		SET_DYNAMIC_PIXEL_SHADER(notlux_g_sky_ps30);
	}

	Draw();
}
END_SHADER
