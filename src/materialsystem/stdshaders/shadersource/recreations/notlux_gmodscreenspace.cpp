//===================== File of the Srcbox Shader Project (Under LUX) =====================//
//
//	Initial D.	:	29.10.2026 DMY
//	Last Change :	29.10.2026 DMY
//
//	Recreation of Garry's Mod's gmodscreenspace ( render.DrawScreenQuad / pp/ Materials ).
//	Render States from Garry's Mod's C++: no Depth Test or Writes, Alpha Writes, no Culling,
//	always Position + Color + one Texcoord, normal Alpha Blending ( One, One with $additive ),
//	$alpha in c0, and with $blur the 8 Blur Offsets ( $blur / Back Buffer Size ) in c1 - c8.
//	Pixel Shader from g_screenspace_ps20.vcs. $vertexcolor is declared ( Garry's Mod declares it )
//	but, as in Garry's Mod, never used: only the Vertex Alpha is applied.
//
//	Builds into game_shader_generic_garrysmod.dll ( game_shader_generic_garrysmod_hl2mp.vpc ).
//	Shader Name, Parameters, Defaults, Registers and Render States are Garry's Mod's own,
//	taken from its game_shader_generic_garrysmod.dll, so Garry's Mod Materials work as they are.
//
//=========================================================================================//

#include "../../cpp_lux_shared.h"

#include "notlux_gmod_screenspace_vs30.inc"
#include "notlux_gmod_screenspace_ps30.inc"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

// Sets one Pixel Shader Constant to ( flValue, 0, 0, 0 )
#define SetPSConst( nReg, flValue ) { float f4Const[4] = { ( flValue ), 0.0f, 0.0f, 0.0f }; pShaderAPI->SetPixelShaderConstant( ( nReg ), f4Const, 1 ); }

BEGIN_VS_SHADER(gmodscreenspace, "Garry's Mod Screenspace Shader.")
SHADER_INFO_GEOMETRY	("Screen Quads.")
SHADER_INFO_USAGE		("Garry's Mod Post Processing Materials.")
SHADER_INFO_LIMITATIONS	("None known.")
SHADER_INFO_PERFORMANCE	("Cheap to render.")
SHADER_INFO_FALLBACK	("A DXLevel below 90 will cause a Fallback to the Wireframe Shader.")
SHADER_INFO_WEBLINKS	(WEBLINK_VDC)
SHADER_INFO_D3D(LUX_SHADERINFO_SM30)

BEGIN_SHADER_PARAMS
	SHADER_PARAM(TEXTUREALPHA, SHADER_PARAM_TYPE_BOOL, "1", "Use Texture Alpha")
	SHADER_PARAM(VERTEXALPHA, SHADER_PARAM_TYPE_BOOL, "1", "Use Vertex Alpha")
	SHADER_PARAM(VERTEXCOLOR, SHADER_PARAM_TYPE_BOOL, "1", "Use Vertex Color")
	SHADER_PARAM(BLUR, SHADER_PARAM_TYPE_FLOAT, "0", "Blur this buffer")
END_SHADER_PARAMS

SHADER_INIT_PARAMS()
{
}

SHADER_FALLBACK
{
	if (g_pHardwareConfig->GetDXSupportLevel() < 90)
		return "Wireframe";
	return 0;
}

SHADER_INIT
{
	if (params[BaseTexture]->IsDefined())
		LoadTexture(BaseTexture);
}

SHADER_DRAW
{
	if (IsSnapshotting())
	{
		const bool bTextureAlpha	= params[TEXTUREALPHA]->GetIntValue() != 0;
		const bool bVertexAlpha		= IS_FLAG_SET(MATERIAL_VAR_VERTEXALPHA);
		const bool bBlur			= params[BLUR]->GetFloatValue() != 0.0f;

		pShaderShadow->EnableDepthWrites(false);
		pShaderShadow->EnableDepthTest(false);
		pShaderShadow->EnableAlphaWrites(true);
		pShaderShadow->EnableCulling(false);
		pShaderShadow->VertexShaderVertexFormat(VERTEX_POSITION | VERTEX_COLOR, 1, 0, 0);
		EnableSampler(SHADER_SAMPLER0, false);

		SetBlendingShadowState(IS_FLAG_SET(MATERIAL_VAR_ADDITIVE) ? BT_ADD : BT_BLEND);
		pShaderShadow->EnableSRGBWrite(false);
		DefaultFog();

		DECLARE_STATIC_VERTEX_SHADER(notlux_gmod_screenspace_vs30);
		SET_STATIC_VERTEX_SHADER_COMBO(VERTEXCOLOR, 1);
		SET_STATIC_VERTEX_SHADER_COMBO(VIEWPROJ, 0);
		SET_STATIC_VERTEX_SHADER(notlux_gmod_screenspace_vs30);

		DECLARE_STATIC_PIXEL_SHADER(notlux_gmod_screenspace_ps30);
		SET_STATIC_PIXEL_SHADER_COMBO(TEXTUREALPHA, bTextureAlpha);
		SET_STATIC_PIXEL_SHADER_COMBO(VERTEXALPHA, bVertexAlpha);
		SET_STATIC_PIXEL_SHADER_COMBO(BLUR, bBlur);
		SET_STATIC_PIXEL_SHADER(notlux_gmod_screenspace_ps30);
	}

	if (IsDynamicState())
	{
		BindTexture(SHADER_SAMPLER0, BaseTexture, -1);

		SetPSConst(0, params[Alpha]->GetFloatValue());

		const float flBlur = params[BLUR]->GetFloatValue();
		if (flBlur != 0.0f)
		{
			int nWidth = 1, nHeight = 1;
			pShaderAPI->GetBackBufferDimensions(nWidth, nHeight);
			const float x = flBlur / (float)MAX(nWidth, 1);
			const float y = flBlur / (float)MAX(nHeight, 1);
			const float f4Offsets[8][4] =
			{
				{ -x, -y, 0, 0 }, {  x, -y, 0, 0 }, { -x,  y, 0, 0 }, {  x,  y, 0, 0 },
				{ -x,  0, 0, 0 }, {  0, -y, 0, 0 }, {  x,  0, 0, 0 }, {  0,  y, 0, 0 },
			};
			pShaderAPI->SetPixelShaderConstant(1, &f4Offsets[0][0], 8);
		}

		DECLARE_DYNAMIC_VERTEX_SHADER(notlux_gmod_screenspace_vs30);
		SET_DYNAMIC_VERTEX_SHADER(notlux_gmod_screenspace_vs30);

		DECLARE_DYNAMIC_PIXEL_SHADER(notlux_gmod_screenspace_ps30);
		SET_DYNAMIC_PIXEL_SHADER(notlux_gmod_screenspace_ps30);
	}
	Draw();
}
END_SHADER
