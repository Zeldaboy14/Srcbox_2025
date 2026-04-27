//===================== File of the Srcbox Shader Project (Under LUX) =====================//
//
//	Initial D.	:	23.4.2026 DMY
//	Last Change :	24.4.2026 DMY
//
//=========================================================================================//

// ----------------------------------------------------------------------------
// Includes
// ----------------------------------------------------------------------------

#include "../cpp_lux_shared.h"

// Includes for Shaderfiles...
#include "notlux_g_sky_vs30.inc"
#include "notlux_g_sky_ps30.inc"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

Vars_Detail_t Stars;

//==========================================================================//
// CommandBuffer Setup
//==========================================================================//
class g_skyGenericContext : public LUXPerMaterialContextData
{
public:
	ShrinkableCommandBuilder_t<5000> m_StaticCmds;
	CommandBuilder_t<1000> m_SemiStaticCmds;

	// Snapshot / Dynamic State
	BlendType_t m_nBlendType = BT_NONE;
	bool m_bIsFullyOpaque = false;

	// Everything related to constants

	g_skyGenericContext(CBaseShader* pShader)
		: m_SemiStaticCmds(pShader),
		m_StaticCmds(pShader)
	{
	}
};

//==========================================================================//
// Shader Start
//==========================================================================//
BEGIN_VS_SHADER(g_sky, "Sky Shader native to Garry's Mod. Allows for customization that the normal shader lacks")
SHADER_INFO_GEOMETRY("Brushes.")
SHADER_INFO_USAGE("Apply to Geometry.")
SHADER_INFO_LIMITATIONS("TBD")
SHADER_INFO_PERFORMANCE("Very Cheap.")
SHADER_INFO_FALLBACK("A DXLevel below 90 will cause a Fallback to the Wireframe Shader.")
SHADER_INFO_WEBLINKS(WEBLINK_VDC
	"VDC env_skypaint entity Page (hand in hand with this): https://developer.valvesoftware.com/wiki/Env_skypaint")
	SHADER_INFO_D3D(LUX_SHADERINFO_SM30)

// ----------------------------------------------------------------------------
// This block is where you'd define inputs that users can feed to your
// shader.
// ----------------------------------------------------------------------------
BEGIN_SHADER_PARAMS
//params[STARLAYERS]->GetIntValue()
	SHADER_PARAM(TopColor,		SHADER_PARAM_TYPE_COLOR,	"[0 1 1]", "The colour of the top of the sky.")
	SHADER_PARAM(BottomColor,	SHADER_PARAM_TYPE_COLOR,	"", "The colour of the bottom of the sky.")
	SHADER_PARAM(FadeBias,		SHADER_PARAM_TYPE_FLOAT,	"", "Controls the bias of the fade between top/bottom. (1.0 is even)")
	SHADER_PARAM(HDRScale,		SHADER_PARAM_TYPE_FLOAT,	"[0 1 0.5]", "When rendering your skybox in HDR mode, output will be scaled by this amount.")
	SHADER_PARAM(SunNormal,		SHADER_PARAM_TYPE_INTEGER,	"[1 0.4 0]", "The position of the sun, expressed as a normal from the center of the world.")
	SHADER_PARAM(DuskColor,		SHADER_PARAM_TYPE_COLOR,	"", "The color of the dusk effect.")
	SHADER_PARAM(DuskScale,		SHADER_PARAM_TYPE_FLOAT,	"", "The size of the dusk effect. (colouring of the horizon)")
	SHADER_PARAM(DuskIntensity,	SHADER_PARAM_TYPE_FLOAT,	"[1 1 1]", "How powerful the dusk effect is.")
	SHADER_PARAM(SunColor,		SHADER_PARAM_TYPE_COLOR,	"", "The color of the sun glow. (this is additive)")
	SHADER_PARAM(SunSize,		SHADER_PARAM_TYPE_FLOAT,	"", "Controls the size of the sun glow.")
	SHADER_PARAM(StarTexture,	SHADER_PARAM_TYPE_TEXTURE,	"skybox/stars", "[RGBA] Star/Clouds texture.")
	SHADER_PARAM(StarScale,		SHADER_PARAM_TYPE_FLOAT,	"", "Sets how big the star texture should be.")
	SHADER_PARAM(StarPos,		SHADER_PARAM_TYPE_INTEGER,	"", "Where the stars are") // Unused????
	SHADER_PARAM(StarLayers,	SHADER_PARAM_TYPE_INTEGER,	"0", "From 1 to 3, how many layers should the star texture be repeated over.")
END_SHADER_PARAMS

// ----------------------------------------------------------------------------
// This is the shader initialization block. This disgusting macro defines
// a bunch of ick that makes this shader work.
// ----------------------------------------------------------------------------
SHADER_INIT
{
	LoadTexture(BaseTexture, TEXTUREFLAGS_SRGB);
	LoadTexture(StarTexture, TEXTUREFLAGS_SRGB);
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

	// Always needed!
	bool bHasBaseTexture = IsTextureLoaded(BaseTexture);
	bool bHasStarTexture = IsTextureLoaded(StarTexture);

	//==========================================================================//
	// Static Snapshot of Shader Setup
	//==========================================================================//
	if (IsSnapshotting())
	{
		//==========================================================================//
		// General Rendering Setup
		//==========================================================================//

		// This handles : $IgnoreZ, $Decal, $Nocull, $Znearer, $Wireframe, $AllowAlphaToCoverage
		SetInitialShadowState();

		// Always write Alpha, used for Depth Values
		pShaderShadow->EnableAlphaWrites(true);

		// Weird name, what it actually means : We output linear values
		pShaderShadow->EnableSRGBWrite(true);

		//==========================================================================//
		// Vertex Shader - Vertex Format
		//==========================================================================//
		// Just always ask for Normal... You pretty much need it 99% of the time
		// Pretty simple, one TexCoord and vPos for ProjPos
		unsigned int nFlags = VERTEX_POSITION;
		int nTexCoords = 1;
		int nUserDataSize = 0;

		pShaderShadow->VertexShaderVertexFormat(nFlags, nTexCoords, NULL, nUserDataSize);

		//==========================================================================//
		// Sampler Setup
		//==========================================================================//

		// s0 - $BaseTexture.
		EnableSampler(SHADER_SAMPLER0, true);

		// s4 - $StarTexture.
		EnableSampler(SHADER_SAMPLER1, true);

		// We don't need to write to the depth buffer.
		pShaderShadow->EnableDepthWrites(false);

		DECLARE_STATIC_VERTEX_SHADER(notlux_g_sky_vs30);
		SET_STATIC_VERTEX_SHADER(notlux_g_sky_vs30);

		DECLARE_STATIC_PIXEL_SHADER(notlux_g_sky_ps30);
		SET_STATIC_PIXEL_SHADER(notlux_g_sky_ps30);
	}

	// ----------------------------------------------------------------------------
	// This section is called every frame.
	// ----------------------------------------------------------------------------
	if (IsDynamicState())
	{
		//==========================================================================//
		// Bind Textures
		//==========================================================================//

		// ShiroDkxtro2:
		// Some of the default Textures have an invalid ShaderAPI Texture Handle. Great!
		// This was never an Issue on Stock Shaders because they use the Command Buffer
		// The Command Buffer ensures you only ever stuff VALID Texture Handles into it
		// Since we don't use the Command Buffer here ( due to Reasons ),
		// using BindTexture will crash Hammer on some default White Engine Texture.
		// For Some it doesn't have a valid ShaderAPI Texture Handle..
		if (bHasBaseTexture)
		{
			ITexture* pTexture = GetTexture(BaseTexture);
			if (pTexture)
			{
				ShaderAPITextureHandle_t hHandle = GetShaderAPITextureBindHandle(BaseTexture, Frame);
				if (hHandle != INVALID_SHADERAPI_TEXTURE_HANDLE)
					BindTexture(SAMPLER_BASETEXTURE, BaseTexture, Frame);
			}
			else
				BindTexture(SAMPLER_BASETEXTURE, TEXTURE_BLACK);
		}

		if (bHasStarTexture)
			BindTexture(SAMPLER_DETAILTEXTURE, StarTexture, Frame);

		DECLARE_DYNAMIC_VERTEX_SHADER(notlux_g_sky_vs30);
		SET_DYNAMIC_VERTEX_SHADER(notlux_g_sky_vs30);

		DECLARE_DYNAMIC_PIXEL_SHADER(notlux_g_sky_ps30);
		SET_DYNAMIC_PIXEL_SHADER(notlux_g_sky_ps30);
	}

	Draw();
}

END_SHADER