//===================== File of the Srcbox Shader Project (Under LUX) =====================//
//
//	Initial D.	:	29.10.2026 DMY
//	Last Change :	29.10.2026 DMY
//
//	Recreation of Garry's Mod's g_colourmodify ( Color Modify Post Process, $pp_colour_* ).
//
//	Builds into game_shader_generic_garrysmod.dll ( game_shader_generic_garrysmod_hl2mp.vpc ).
//	Shader Name, Parameters, Defaults, Registers and Render States are Garry's Mod's own,
//	taken from its game_shader_generic_garrysmod.dll, so Garry's Mod Materials work as they are.
//
//=========================================================================================//

#include "../../cpp_lux_shared.h"

#include "notlux_gmod_screenspace_vs30.inc"
#include "notlux_g_colourmodify_ps30.inc"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

// Sets one Pixel Shader Constant to ( flValue, 0, 0, 0 )
#define SetPSConst( nReg, flValue ) { float f4Const[4] = { ( flValue ), 0.0f, 0.0f, 0.0f }; pShaderAPI->SetPixelShaderConstant( ( nReg ), f4Const, 1 ); }

BEGIN_VS_SHADER(g_colourmodify, "Garry's Mod Color Modify.")
SHADER_INFO_GEOMETRY	("Screen Quads.")
SHADER_INFO_USAGE		("Garry's Mod Post Processing Materials.")
SHADER_INFO_LIMITATIONS	("None known.")
SHADER_INFO_PERFORMANCE	("Cheap to render.")
SHADER_INFO_FALLBACK	("A DXLevel below 90 will cause a Fallback to the Wireframe Shader.")
SHADER_INFO_WEBLINKS	(WEBLINK_VDC)
SHADER_INFO_D3D(LUX_SHADERINFO_SM30)

BEGIN_SHADER_PARAMS
	SHADER_PARAM(FBTEXTURE, SHADER_PARAM_TYPE_TEXTURE, "_rt_FullFrameFB", "")
	SHADER_PARAM(pp_colour_addr, SHADER_PARAM_TYPE_FLOAT, "0.0", "pp_colour_addr")
	SHADER_PARAM(pp_colour_addg, SHADER_PARAM_TYPE_FLOAT, "0.0", "pp_colour_addg")
	SHADER_PARAM(pp_colour_addb, SHADER_PARAM_TYPE_FLOAT, "0.0", "pp_colour_addb")
	SHADER_PARAM(pp_colour_brightness, SHADER_PARAM_TYPE_FLOAT, "0.0", "pp_colour_brightness")
	SHADER_PARAM(pp_colour_contrast, SHADER_PARAM_TYPE_FLOAT, "1", "pp_colour_contrast")
	SHADER_PARAM(pp_colour_colour, SHADER_PARAM_TYPE_FLOAT, "1", "pp_colour_colour")
	SHADER_PARAM(pp_colour_mulr, SHADER_PARAM_TYPE_FLOAT, "0", "pp_colour_mulr")
	SHADER_PARAM(pp_colour_mulg, SHADER_PARAM_TYPE_FLOAT, "0", "pp_colour_mulg")
	SHADER_PARAM(pp_colour_mulb, SHADER_PARAM_TYPE_FLOAT, "0", "pp_colour_mulb")
	SHADER_PARAM(pp_colour_inv, SHADER_PARAM_TYPE_FLOAT, "0", "pp_colour_inv")
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
	if (params[FBTEXTURE]->IsDefined())
		LoadTexture(FBTEXTURE);
}

SHADER_DRAW
{
	if (IsSnapshotting())
	{
		pShaderShadow->EnableDepthWrites(false);
		EnableSampler(SHADER_SAMPLER0, false);
		pShaderShadow->VertexShaderVertexFormat(VERTEX_POSITION, 1, 0, 0);
		pShaderShadow->EnableSRGBWrite(false);
		DefaultFog();

		DECLARE_STATIC_VERTEX_SHADER(notlux_gmod_screenspace_vs30);
		SET_STATIC_VERTEX_SHADER_COMBO(VERTEXCOLOR, 0);
		SET_STATIC_VERTEX_SHADER_COMBO(VIEWPROJ, 0);
		SET_STATIC_VERTEX_SHADER(notlux_gmod_screenspace_vs30);

		DECLARE_STATIC_PIXEL_SHADER(notlux_g_colourmodify_ps30);
		SET_STATIC_PIXEL_SHADER(notlux_g_colourmodify_ps30);
	}

	if (IsDynamicState())
	{
		SetPSConst(0, params[pp_colour_addr]->GetFloatValue());
		SetPSConst(1, params[pp_colour_addg]->GetFloatValue());
		SetPSConst(2, params[pp_colour_addb]->GetFloatValue());
		SetPSConst(3, params[pp_colour_brightness]->GetFloatValue());
		SetPSConst(4, params[pp_colour_contrast]->GetFloatValue());
		SetPSConst(5, params[pp_colour_colour]->GetFloatValue());
		SetPSConst(6, params[pp_colour_mulr]->GetFloatValue());
		SetPSConst(7, params[pp_colour_mulg]->GetFloatValue());
		SetPSConst(8, params[pp_colour_mulb]->GetFloatValue());
		SetPSConst(9, params[pp_colour_inv]->GetFloatValue());
		BindTexture(SHADER_SAMPLER0, FBTEXTURE, -1);

		DECLARE_DYNAMIC_VERTEX_SHADER(notlux_gmod_screenspace_vs30);
		SET_DYNAMIC_VERTEX_SHADER(notlux_gmod_screenspace_vs30);

		DECLARE_DYNAMIC_PIXEL_SHADER(notlux_g_colourmodify_ps30);
		SET_DYNAMIC_PIXEL_SHADER(notlux_g_colourmodify_ps30);
	}
	Draw();
}
END_SHADER
