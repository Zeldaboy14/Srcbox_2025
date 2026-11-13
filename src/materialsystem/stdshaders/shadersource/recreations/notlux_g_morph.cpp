//===================== File of the Srcbox Shader Project (Under LUX) =====================//
//
//	Initial D.	:	29.10.2026 DMY
//	Last Change :	29.10.2026 DMY
//
//	Recreation of Garry's Mod's g_morph ( from an older Garry's Mod: the current
//	game_shader_generic_garrysmod.dll no longer has it, so there was no C++ to follow ).
//	Pixel Shader from g_morph_ps20.vcs. Parameter Names come from its Constants ( xAmt, yAmt,
//	pressure ) and the Render States are plain ( no Blending ): both are guesses.
//
//	Builds into game_shader_generic_garrysmod.dll ( game_shader_generic_garrysmod_hl2mp.vpc ).
//
//=========================================================================================//

#include "../../cpp_lux_shared.h"

#include "notlux_gmod_screenspace_vs30.inc"
#include "notlux_g_morph_ps30.inc"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

// Sets one Pixel Shader Constant to ( flValue, 0, 0, 0 )
#define SetPSConst( nReg, flValue ) { float f4Const[4] = { ( flValue ), 0.0f, 0.0f, 0.0f }; pShaderAPI->SetPixelShaderConstant( ( nReg ), f4Const, 1 ); }

BEGIN_VS_SHADER(g_morph, "Garry's Mod Morph ( Displacement Brush ).")
SHADER_INFO_GEOMETRY	("Screen Quads.")
SHADER_INFO_USAGE		("Garry's Mod Post Processing Materials.")
SHADER_INFO_LIMITATIONS	("None known.")
SHADER_INFO_PERFORMANCE	("Cheap to render.")
SHADER_INFO_FALLBACK	("A DXLevel below 90 will cause a Fallback to the Wireframe Shader.")
SHADER_INFO_WEBLINKS	(WEBLINK_VDC)
SHADER_INFO_D3D(LUX_SHADERINFO_SM30)

BEGIN_SHADER_PARAMS
	SHADER_PARAM(XAMT, SHADER_PARAM_TYPE_FLOAT, "0", "X Amount")
	SHADER_PARAM(YAMT, SHADER_PARAM_TYPE_FLOAT, "0", "Y Amount")
	SHADER_PARAM(PRESSURE, SHADER_PARAM_TYPE_FLOAT, "1", "Pressure")
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
		EnableSampler(SHADER_SAMPLER0, false);
		pShaderShadow->EnableDepthWrites(false);
		pShaderShadow->VertexShaderVertexFormat(VERTEX_POSITION, 1, 0, 0);
		DefaultFog();

		DECLARE_STATIC_VERTEX_SHADER(notlux_gmod_screenspace_vs30);
		SET_STATIC_VERTEX_SHADER_COMBO(VERTEXCOLOR, 0);
		SET_STATIC_VERTEX_SHADER_COMBO(VIEWPROJ, 0);
		SET_STATIC_VERTEX_SHADER(notlux_gmod_screenspace_vs30);

		DECLARE_STATIC_PIXEL_SHADER(notlux_g_morph_ps30);
		SET_STATIC_PIXEL_SHADER(notlux_g_morph_ps30);
	}

	if (IsDynamicState())
	{
		SetPSConst(0, params[XAMT]->GetFloatValue());
		SetPSConst(1, params[YAMT]->GetFloatValue());
		SetPSConst(2, params[PRESSURE]->GetFloatValue());

		BindTexture(SHADER_SAMPLER0, BaseTexture, -1);

		DECLARE_DYNAMIC_VERTEX_SHADER(notlux_gmod_screenspace_vs30);
		SET_DYNAMIC_VERTEX_SHADER(notlux_gmod_screenspace_vs30);

		DECLARE_DYNAMIC_PIXEL_SHADER(notlux_g_morph_ps30);
		SET_DYNAMIC_PIXEL_SHADER(notlux_g_morph_ps30);
	}
	Draw();
}
END_SHADER
