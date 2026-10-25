#include "cbase.h"
#include "materialsystem/itexture.h"
#include "luamanager.h"
#include "luasrclib.h"
#include "lrender.h"
#ifdef CLIENT_DLL
#include "rendertexture.h"
#include "view_scene.h"
#include <materialsystem/imaterialsystem.h>
#include <vgui/ISurface.h>
#include <vgui_controls/Controls.h>
#include "mathlib/lvector.h"
#include <lColor.h>
#include <renderparm.h>
#include <view.h>
#include <litexture.h>
#include <shaderapi/ishaderapi.h>
#include <utlstack.h>
#include "beamdraw.h"
#include "materialsystem/limaterial.h"
#include "iviewrender_beams.h"
#include <mathlib/lvmatrix.h>
#ifdef LUA_SDK
#include <KeyValues.h>
#include "materialsystem/imaterialvar.h"
#endif
#endif

#ifdef CLIENT_DLL
#include "c_pixel_visibility.h"
#endif

// Srcbox: GMod mesh library, IMesh, ParticleEmitter and render.GetSunInfo (client)
#include "lgmod_render_ext.inl"

// Srcbox: GMod matproxy library (Lua material proxies, client)
#include "lgmod_matproxy.inl"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

LUA_REGISTRATION_INIT( Renders );

#ifdef CLIENT_DLL

// Minifaction and magnification filter stacks
static CUtlStack< ShaderAPITextureHandle_t > filterTextureHandlesMinification;
static CUtlStack< ShaderAPITextureHandle_t > filterTextureHandlesMagnification;
extern IShaderAPI *g_pShaderApi;

#ifdef GAME_DLL

LUA_BINDING_BEGIN( Renders, SpawnBeam, "library", "Spawns a temporary beam entity to render a beam. The alpha of the color servers as the brightness of it.", "server" )
{
    Vector start = LUA_BINDING_ARGUMENT( luaL_checkvector, 1, "start" );
    Vector end = LUA_BINDING_ARGUMENT( luaL_checkvector, 2, "end" );
    int modelIndex = LUA_BINDING_ARGUMENT( luaL_checknumber, 3, "modelIndex" );
    int haloIndex = LUA_BINDING_ARGUMENT( luaL_checknumber, 4, "haloIndex" );
    unsigned char frameStart = LUA_BINDING_ARGUMENT( luaL_checknumber, 5, "frameStart" );
    unsigned char frameRate = LUA_BINDING_ARGUMENT( luaL_checknumber, 6, "frameRate" );
    float life = LUA_BINDING_ARGUMENT( luaL_checknumber, 7, "life" );
    unsigned char width = LUA_BINDING_ARGUMENT( luaL_checknumber, 8, "width" );
    unsigned char endWidth = LUA_BINDING_ARGUMENT( luaL_checknumber, 9, "endWidth" );
    unsigned char fadeLength = LUA_BINDING_ARGUMENT( luaL_checknumber, 10, "fadeLength" );
    unsigned char noise = LUA_BINDING_ARGUMENT( luaL_checknumber, 11, "noise" );
    lua_Color colorAndBrightness = LUA_BINDING_ARGUMENT( luaL_checkcolor, 12, "colorAndBrightness" );
    unsigned char speed = LUA_BINDING_ARGUMENT( luaL_checknumber, 13, "speed" );

    UTIL_Beam(
        start,
        end,
        modelIndex,
        haloIndex,
        frameStart,
        frameRate,
        life,
        width,
        endWidth,
        fadeLength,
        noise,
        colorAndBrightness.r(),
        colorAndBrightness.g(),
        colorAndBrightness.b(),
        colorAndBrightness.a(),
        speed );

    return 0;
}
LUA_BINDING_END()

#endif

#ifdef CLIENT_DLL
//-----------------------------------------------------------------------------
// Srcbox: GMod's render.GetLightColor and util.GetPixelVisibleHandle /
// util.PixelVisible (mapped to these in the GMod compatibility layer).
//-----------------------------------------------------------------------------
LUA_BINDING_BEGIN( Renders, GetLightColor, "library", "Get the lighting at a point in the world (world lights and lightmap).", "client" )
{
    Vector vecPosition = LUA_BINDING_ARGUMENT( luaL_checkvector, 1, "position" );
    lua_pushvector( L, engine->GetLightForPoint( vecPosition, true ) );
    return 1;
}
LUA_BINDING_END( "Vector", "The light colour at that point." )

// pixelvis handles are plain ints the engine fills in on first use; Lua gets an
// index into this list (never shrunk, they are tiny and reused per addon)
static CUtlVector< pixelvis_handle_t > s_LuaPixelVisHandles;

LUA_BINDING_BEGIN( Renders, GetPixelVisibleHandle, "library", "Create a handle for Renders.PixelVisible.", "client" )
{
    int nIndex = s_LuaPixelVisHandles.AddToTail( 0 );
    lua_pushinteger( L, nIndex + 1 );
    return 1;
}
LUA_BINDING_END( "integer", "The handle." )

LUA_BINDING_BEGIN( Renders, PixelVisible, "library", "How much of a sphere at a position is visible on screen (0-1), using an occlusion query.", "client" )
{
    Vector vecPosition = LUA_BINDING_ARGUMENT( luaL_checkvector, 1, "position" );
    float flRadius = LUA_BINDING_ARGUMENT( luaL_checknumber, 2, "radius" );
    int nHandle = ( int )LUA_BINDING_ARGUMENT( luaL_checkinteger, 3, "handle" ) - 1;

    if ( !s_LuaPixelVisHandles.IsValidIndex( nHandle ) )
    {
        lua_pushnumber( L, 0 );
        return 1;
    }

    pixelvis_queryparams_t params;
    params.Init( vecPosition, flRadius );
    lua_pushnumber( L, PixelVisibility_FractionVisible( params, &s_LuaPixelVisHandles[nHandle] ) );
    return 1;
}
LUA_BINDING_END( "number", "Visible fraction, 0 to 1." )
#endif

LUA_BINDING_BEGIN( Renders, GetRenderTarget, "library", "Get the currently active render target.", "client" )
{
    CMatRenderContextPtr pRenderContext( materials );
    ITexture *pTexture = pRenderContext->GetRenderTarget();
    lua_pushitexture( L, pTexture );

    return 1;
}
LUA_BINDING_END( "Texture", "The currently active render target texture." )

LUA_BINDING_BEGIN( Renders, CopyRenderTargetToTexture, "library", "Copies the currently active Render Target to the specified texture.", "client" )
{
    CMatRenderContextPtr pRenderContext( materials );
    ITexture *pTexture = LUA_BINDING_ARGUMENT( luaL_checkitexture, 1, "texture" );
    pRenderContext->CopyRenderTargetToTexture( pTexture );

    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( Renders, CreateRenderTargetTextureEx, "library", "Create a new render target texture.", "client" )
{
    const char *name = LUA_BINDING_ARGUMENT( luaL_checkstring, 1, "name" );
    int width = LUA_BINDING_ARGUMENT( luaL_checknumber, 2, "width" );
    int height = LUA_BINDING_ARGUMENT( luaL_checknumber, 3, "height" );
    int sizeMode = LUA_BINDING_ARGUMENT_WITH_DEFAULT( luaL_optnumber, 4, RT_SIZE_DEFAULT, "sizeMode" );
    int depthMode = LUA_BINDING_ARGUMENT_WITH_DEFAULT( luaL_optnumber, 5, MATERIAL_RT_DEPTH_SHARED, "depthMode" );
    int textureFlags = LUA_BINDING_ARGUMENT_WITH_DEFAULT( luaL_optnumber, 6, TEXTUREFLAGS_CLAMPS | TEXTUREFLAGS_CLAMPT, "textureFlags" );
    int renderTargetFlags = LUA_BINDING_ARGUMENT_WITH_DEFAULT( luaL_optnumber, 7, 0, "renderTargetFlags" );
    int imageFormat = LUA_BINDING_ARGUMENT_WITH_DEFAULT( luaL_optnumber, 8, IMAGE_FORMAT_RGBA8888, "imageFormat" );

    g_pMaterialSystem->OverrideRenderTargetAllocation( true );
    ITexture *pTexture = materials->CreateNamedRenderTargetTextureEx(
        name,
        width,
        height,
        ( RenderTargetSizeMode_t )sizeMode,
        ( ImageFormat )imageFormat,
        ( MaterialRenderTargetDepth_t )depthMode,
        textureFlags,
        renderTargetFlags );
    g_pMaterialSystem->OverrideRenderTargetAllocation( false );

    lua_pushitexture( L, pTexture );

    return 1;
}
LUA_BINDING_END( "Texture", "The created render target texture." )

LUA_BINDING_BEGIN( Renders, CullMode, "library", "Sets the cull mode that decides how back faces are culled when rendering geometry.", "client" )
{
    CMatRenderContextPtr pRenderContext( materials );
    MaterialCullMode_t cullMode = LUA_BINDING_ARGUMENT_ENUM( MaterialCullMode_t, 1, "cullMode" );
    pRenderContext->CullMode( cullMode );

    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( Renders, FindTexture, "library", "Finds an existing texture (e.g. a named render target). Returns nil when it doesn't exist.", "client" )
{
    const char *name = LUA_BINDING_ARGUMENT( luaL_checkstring, 1, "name" );
    const char *group = LUA_BINDING_ARGUMENT_WITH_DEFAULT( luaL_optstring, 2, TEXTURE_GROUP_RENDER_TARGET, "textureGroup" );
    ITexture *pTexture = materials->FindTexture( name, group, false );

    if ( !pTexture || pTexture->IsError() )
    {
        lua_pushnil( L );
        return 1;
    }

    lua_pushitexture( L, pTexture );
    return 1;
}
LUA_BINDING_END( "Texture", "The texture, or nil." )

LUA_BINDING_BEGIN( Renders, GetScreenEffectTexture, "library", "Get the screen effect texture.", "client" )
{
    lua_ITexture *pTexture = GetFullFrameFrameBufferTexture( LUA_BINDING_ARGUMENT_WITH_DEFAULT( luaL_optnumber, 1, 0, "textureIndex" ) );
    lua_pushitexture( L, pTexture );
    return 1;
}
LUA_BINDING_END( "Texture", "The screen effect texture." )

LUA_BINDING_BEGIN( Renders, SetClippingEnabled, "library", "Set the clipping enabled.", "client" )
{
    CMatRenderContextPtr pRenderContext( materials );
    pRenderContext->EnableClipping( LUA_BINDING_ARGUMENT( lua_toboolean, 1, "isEnabled" ) );
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( Renders, UpdateScreenEffectTexture, "library", "Update the screen effect texture.", "client" )
{
    const CViewSetup *pViewSetup = view->GetViewSetup();
    UpdateScreenEffectTexture(
        LUA_BINDING_ARGUMENT_WITH_DEFAULT(
            luaL_optnumber, 1, 0, "textureIndex" ),
        pViewSetup->x,
        pViewSetup->y,
        pViewSetup->width,
        pViewSetup->height );
    return 0;
}
LUA_BINDING_END()

// Srcbox: the render target Lua pushed with render.PushRenderTarget (NULL when none is active).
// cam.Start2D/Start3D inside one must keep drawing into it, sized to it (the tool gun screen).
static ITexture *LuaActiveRenderTarget();

LUA_BINDING_BEGIN( Renders, PushView3D, "library", "Push a 3D view.", "client" )
{
    CViewSetup playerView = *view->GetPlayerViewSetup();
    ITexture *pLuaRenderTarget = LuaActiveRenderTarget();

    if ( pLuaRenderTarget )
    {
        playerView.x = playerView.m_nUnscaledX = 0;
        playerView.y = playerView.m_nUnscaledY = 0;
        playerView.width = playerView.m_nUnscaledWidth = pLuaRenderTarget->GetActualWidth();
        playerView.height = playerView.m_nUnscaledHeight = pLuaRenderTarget->GetActualHeight();
        playerView.m_flAspectRatio = ( float )playerView.width / ( float )MAX( playerView.height, 1 );
    }

    Vector origin = LUA_BINDING_ARGUMENT_WITH_DEFAULT( luaL_optvector, 1, &playerView.origin, "origin" );
    QAngle angles = LUA_BINDING_ARGUMENT_WITH_DEFAULT( luaL_optangle, 2, &playerView.angles, "angles" );
    float fov = LUA_BINDING_ARGUMENT_WITH_DEFAULT( luaL_optnumber, 3, playerView.fov, "fov" );
    int x = LUA_BINDING_ARGUMENT_WITH_DEFAULT( luaL_optnumber, 4, playerView.x, "x" );
    int y = LUA_BINDING_ARGUMENT_WITH_DEFAULT( luaL_optnumber, 5, playerView.y, "y" );
    int width = LUA_BINDING_ARGUMENT_WITH_DEFAULT( luaL_optnumber, 6, playerView.width, "width" );
    int height = LUA_BINDING_ARGUMENT_WITH_DEFAULT( luaL_optnumber, 7, playerView.height, "height" );
    float zNear = LUA_BINDING_ARGUMENT_WITH_DEFAULT( luaL_optnumber, 8, playerView.zNear, "zNear" );
    float zFar = LUA_BINDING_ARGUMENT_WITH_DEFAULT( luaL_optnumber, 9, playerView.zFar, "zFar" );

    CViewSetup viewSetup( playerView );
    viewSetup.origin = origin;
    viewSetup.angles = angles;
    viewSetup.fov = fov;
    viewSetup.x = x;
    viewSetup.y = y;
    viewSetup.width = width;
    viewSetup.height = height;
    viewSetup.zNear = zNear;
    viewSetup.zFar = zFar;

    render->Push3DView( viewSetup, 0, pLuaRenderTarget, view->GetFrustum() );

    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( Renders, PopView3D, "library", "Pop a 3D view.", "client" )
{
    render->PopView( view->GetFrustum() );
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( Renders, PushView2D, "library", "Push a 2D view.", "client" )
{
    CViewSetup viewSetup = *view->GetPlayerViewSetup();
    ITexture *pLuaRenderTarget = LuaActiveRenderTarget();

    if ( pLuaRenderTarget )
    {
        viewSetup.x = viewSetup.m_nUnscaledX = 0;
        viewSetup.y = viewSetup.m_nUnscaledY = 0;
        viewSetup.width = viewSetup.m_nUnscaledWidth = pLuaRenderTarget->GetActualWidth();
        viewSetup.height = viewSetup.m_nUnscaledHeight = pLuaRenderTarget->GetActualHeight();
    }

    render->Push2DView( viewSetup, 0, pLuaRenderTarget, view->GetFrustum() );
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( Renders, PopView2D, "library", "Pop a 2D view.", "client" )
{
    render->PopView( view->GetFrustum() );
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( Renders, PushCustomClipPlane, "library", "Push a custom clip plane.", "client" )
{
    Vector normal = LUA_BINDING_ARGUMENT( luaL_checkvector, 1, "normal" );
    float distance = LUA_BINDING_ARGUMENT( luaL_checknumber, 2, "distance" );

    Vector4D plane;
    VectorCopy( normal, plane.AsVector3D() );
    plane.w = distance + 0.1f;

    CMatRenderContextPtr pRenderContext( materials );
    pRenderContext->PushCustomClipPlane( plane.Base() );

    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( Renders, PopCustomClipPlane, "library", "Pop a custom clip plane.", "client" )
{
    CMatRenderContextPtr pRenderContext( materials );
    pRenderContext->PopCustomClipPlane();

    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( Renders, GetModelMatrix, "library", "Get the model matrix.", "client" )
{
    VMatrix matrix;
    CMatRenderContextPtr pRenderContext( materials );
    pRenderContext->GetMatrix( MATERIAL_MODEL, &matrix );
    lua_pushvmatrix( L, matrix );

    return 1;
}
LUA_BINDING_END( "VMatrix", "The model matrix." )

LUA_BINDING_BEGIN( Renders, PushModelMatrix, "library", "Push a model matrix.", "client" )
{
    VMatrix matrix = LUA_BINDING_ARGUMENT( luaL_checkvmatrix, 1, "matrix" );
    bool shouldMultiply = LUA_BINDING_ARGUMENT_WITH_DEFAULT( luaL_optboolean, 2, false, "shouldMultiply" );

    CMatRenderContextPtr pRenderContext( materials );
    pRenderContext->MatrixMode( MATERIAL_MODEL );
    pRenderContext->PushMatrix();

    // TODO: Is this the correct implementation, the same as how GMod does it?
    if ( shouldMultiply )
    {
        pRenderContext->LoadMatrix( matrix );
    }
    else
    {
        pRenderContext->LoadIdentity();
        pRenderContext->LoadMatrix( matrix );
    }

    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( Renders, PopModelMatrix, "library", "Pop a model matrix.", "client" )
{
    CMatRenderContextPtr pRenderContext( materials );
    pRenderContext->MatrixMode( MATERIAL_MODEL );
    pRenderContext->PopMatrix();

    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( Renders, SuppressEngineLighting, "library", "Suppress engine lighting.", "client" )
{
    modelrender->SuppressEngineLighting( LUA_BINDING_ARGUMENT( lua_toboolean, 1, "suppress" ) );
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( Renders, SetLightingOrigin, "library", "Set the lighting origin.", "client" )
{
    CMatRenderContextPtr pRenderContext( materials );
    pRenderContext->SetLightingOrigin( LUA_BINDING_ARGUMENT( luaL_checkvector, 1, "lightingOrigin" ) );
    return 0;
}
LUA_BINDING_END()

/*
directionFace:
    TODO: Check that these match the values in the engine
    BOX_FRONT	0	Place the light from the front
    BOX_BACK	1	Place the light behind
    BOX_RIGHT	2	Place the light to the right
    BOX_LEFT	3	Place the light to the left
    BOX_TOP	4	Place the light to the top
    BOX_BOTTOM	5	Place the light to the bottom
*/
LUA_BINDING_BEGIN( Renders, SetAmbientLightCube, "library", "Set the ambient light cube.", "client" )
{
    int directionFace = LUA_BINDING_ARGUMENT( luaL_checknumber, 1, "directionFace" );
    float r = LUA_BINDING_ARGUMENT( luaL_checknumber, 2, "r" );
    float g = LUA_BINDING_ARGUMENT( luaL_checknumber, 3, "g" );
    float b = LUA_BINDING_ARGUMENT( luaL_checknumber, 4, "b" );
    static Vector4D cubeFaces[6];
    int i;

    for ( i = 0; i < 6; i++ )
    {
        if ( i == directionFace )
        {
            cubeFaces[i].Init( r, g, b, 1.0f );
        }
        else
        {
            cubeFaces[i].Init( 0.0f, 0.0f, 0.0f, 0.0f );
        }
    }

    CMatRenderContextPtr pRenderContext( materials );
    pRenderContext->SetAmbientLightCube( cubeFaces );

    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( Renders, ResetAmbientLightCube, "library", "Reset the ambient light cube.", "client" )
{
    float r = LUA_BINDING_ARGUMENT( luaL_checknumber, 1, "r" );
    float g = LUA_BINDING_ARGUMENT( luaL_checknumber, 2, "g" );
    float b = LUA_BINDING_ARGUMENT( luaL_checknumber, 3, "b" );

    static Vector4D cubeFaces[6];
    int i;

    for ( i = 0; i < 6; i++ )
    {
        cubeFaces[i].Init( r, g, b, 1.0f );
    }

    CMatRenderContextPtr pRenderContext( materials );
    pRenderContext->SetAmbientLightCube( cubeFaces );

    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( Renders, RenderFlashlights, "library", "Render flashlights.", "client" )
{
    // Check that a function is provided
    luaL_checktype( L, 1, LUA_TFUNCTION );

    CMatRenderContextPtr pRenderContext( materials );
    pRenderContext->SetFlashlightMode( true );

    // Execute the function
    luasrc_pcall( L, 1, 0 );

    pRenderContext->SetFlashlightMode( false );

    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( Renders, SetLight, "library", "Set a light.", "client" )
{
    LightDesc_t desc;
    memset( &desc, 0, sizeof( desc ) );

    desc.m_Type = MATERIAL_LIGHT_DIRECTIONAL;

    desc.m_Color[0] = LUA_BINDING_ARGUMENT( luaL_checknumber, 1, "r" );
    desc.m_Color[1] = LUA_BINDING_ARGUMENT( luaL_checknumber, 2, "g" );
    desc.m_Color[2] = LUA_BINDING_ARGUMENT( luaL_checknumber, 3, "b" );
    desc.m_Color *= LUA_BINDING_ARGUMENT( luaL_checknumber, 4, "intensity" ) / 255.0f;

    desc.m_Attenuation0 = 1.0f;
    desc.m_Attenuation1 = 0.0f;
    desc.m_Attenuation2 = 0.0f;
    desc.m_Flags = LIGHTTYPE_OPTIMIZATIONFLAGS_HAS_ATTENUATION0;

    desc.m_Direction = LUA_BINDING_ARGUMENT( luaL_checkvector, 5, "direction" );
    VectorNormalize( desc.m_Direction );

    desc.m_Theta = 0.0f;
    desc.m_Phi = 0.0f;
    desc.m_Falloff = 1.0f;

    CMatRenderContextPtr pRenderContext( materials );
    pRenderContext->SetLight( LUA_BINDING_ARGUMENT( luaL_checknumber, 6, "lightIndex" ), desc );

    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( Renders, SetColorModulation, "library", "Set the color modulation.", "client" )
{
    float color[3];
    color[0] = LUA_BINDING_ARGUMENT( luaL_checknumber, 1, "r" );
    color[1] = LUA_BINDING_ARGUMENT( luaL_checknumber, 2, "g" );
    color[2] = LUA_BINDING_ARGUMENT( luaL_checknumber, 3, "b" );

    render->SetColorModulation( color );

    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( Renders, SetBlend, "library", "Set the blend.", "client" )
{
    render->SetBlend( LUA_BINDING_ARGUMENT( luaL_checknumber, 1, "blend" ) );
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( Renders, ClearBuffers, "library", "Clear the buffers.", "client" )
{
    bool bClearColor = LUA_BINDING_ARGUMENT( lua_toboolean, 1, "clearColor" );
    bool bClearDepth = LUA_BINDING_ARGUMENT( lua_toboolean, 2, "clearDepth" );
    bool bClearStencil = LUA_BINDING_ARGUMENT( lua_toboolean, 3, "clearStencil" );

    CMatRenderContextPtr pRenderContext( materials );
    pRenderContext->ClearBuffers( bClearColor, bClearDepth, bClearStencil );

    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( Renders, ClearColor, "library", "Clear the color.", "client" )
{
    lua_Color clr = LUA_BINDING_ARGUMENT( luaL_checkcolor, 1, "color" );

    CMatRenderContextPtr pRenderContext( materials );
    pRenderContext->ClearColor4ub( clr.r(), clr.g(), clr.b(), clr.a() );
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( Renders, SetScissorRectangle, "library", "Set the scissor rectangle.", "client" )
{
    int nLeft = LUA_BINDING_ARGUMENT( luaL_checknumber, 1, "left" );
    int nTop = LUA_BINDING_ARGUMENT( luaL_checknumber, 2, "top" );
    int nRight = LUA_BINDING_ARGUMENT( luaL_checknumber, 3, "right" );
    int nBottom = LUA_BINDING_ARGUMENT( luaL_checknumber, 4, "bottom" );
    bool bEnableScissor = LUA_BINDING_ARGUMENT( lua_toboolean, 5, "enableScissor" );

    CMatRenderContextPtr pRenderContext( materials );
    pRenderContext->SetScissorRect( nLeft, nTop, nRight, nBottom, bEnableScissor );

    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( Renders, SetWriteDepthToDestinationAlpha, "library", "Set the write depth to destination alpha.", "client" )
{
    bool bEnable = LUA_BINDING_ARGUMENT( lua_toboolean, 1, "enable" );

    CMatRenderContextPtr pRenderContext( materials );
    pRenderContext->SetIntRenderingParameter( INT_RENDERPARM_WRITE_DEPTH_TO_DESTALPHA, bEnable );
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( Renders, MainViewOrigin, "library", "Get the main view origin.", "client" )
{
    lua_pushvector( L, MainViewOrigin() );
    return 1;
}
LUA_BINDING_END( "Vector", "The main view origin." )

LUA_BINDING_BEGIN( Renders, MainViewAngles, "library", "Get the main view angles.", "client" )
{
    lua_pushangle( L, MainViewAngles() );
    return 1;
}
LUA_BINDING_END( "Angle", "The main view angles." )

LUA_BINDING_BEGIN( Renders, MainViewForward, "library", "Get the main view forward.", "client" )
{
    lua_pushvector( L, MainViewForward() );
    return 1;
}
LUA_BINDING_END( "Vector", "The main view forward." )

static ShaderTexFilterMode_t FilterModeFromEnumeration( int enumeration )
{
    switch ( enumeration )
    {
        case 0:
            return SHADER_TEXFILTERMODE_NEAREST;
        case 1:
            return SHADER_TEXFILTERMODE_NEAREST;
        case 2:
            return SHADER_TEXFILTERMODE_LINEAR;
        case 3:
            return SHADER_TEXFILTERMODE_ANISOTROPIC;
        default:
            return SHADER_TEXFILTERMODE_NEAREST;
            break;
    }
}

LUA_BINDING_BEGIN( Renders, PushFilterMinification, "library", "Push a minification filter. (Non-functional; help wanted)", "client" )
{
    int filterMode = LUA_BINDING_ARGUMENT( luaL_checknumber, 1, "filter" );

    // TODO: This doesn't work
    CMatRenderContextPtr pRenderContext( materials );
    ITexture *screenTexture = pRenderContext->GetFrameBufferCopyTexture( 0 );
#ifdef LUA_SDK
    if ( !g_pShaderApi )
        return 0;
#endif
    ShaderAPITextureHandle_t hTexture = g_pShaderApi->CreateTexture(
        screenTexture->GetActualWidth(),
        screenTexture->GetActualHeight(),
        1,
        screenTexture->GetImageFormat(),
        1,
        1,
        TEXTUREFLAGS_CLAMPS | TEXTUREFLAGS_CLAMPT,
        "filterTextureMinification",
        "RenderTarget" );

    filterTextureHandlesMinification.Push( hTexture );
    g_pShaderApi->ModifyTexture( hTexture );
    g_pShaderApi->TexMinFilter( FilterModeFromEnumeration( filterMode ) );

    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( Renders, PopFilterMinification, "library", "Pop a minification filter.", "client" )
{
#ifdef LUA_SDK
    if ( !g_pShaderApi || filterTextureHandlesMinification.Count() == 0 )
        return 0;
#endif
    g_pShaderApi->DeleteTexture( filterTextureHandlesMinification.Top() );
    filterTextureHandlesMinification.Pop();

    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( Renders, PushFilterMagnification, "library", "Push a magnification filter.", "client" )
{
    int filterMode = LUA_BINDING_ARGUMENT( luaL_checknumber, 1, "filter" );

    // TODO: This doesn't work
    CMatRenderContextPtr pRenderContext( materials );
    ITexture *screenTexture = pRenderContext->GetFrameBufferCopyTexture( 0 );
    if ( !g_pShaderApi )
        return 0;
    ShaderAPITextureHandle_t hTexture = g_pShaderApi->CreateTexture(
        screenTexture->GetActualWidth(),
        screenTexture->GetActualHeight(),
        1,
        screenTexture->GetImageFormat(),
        1,
        1,
        TEXTUREFLAGS_CLAMPS | TEXTUREFLAGS_CLAMPT,
        "filterTextureMagnification",
        "RenderTarget" );

    filterTextureHandlesMagnification.Push( hTexture );
    g_pShaderApi->ModifyTexture( hTexture );
    g_pShaderApi->TexMagFilter( FilterModeFromEnumeration( filterMode ) );

    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( Renders, PopFilterMagnification, "library", "Pop a magnification filter. (Non-functional; help wanted)", "client" )
{
#ifdef LUA_SDK
    if ( !g_pShaderApi || filterTextureHandlesMagnification.Count() == 0 )
        return 0;
#endif
    g_pShaderApi->DeleteTexture( filterTextureHandlesMagnification.Top() );
    filterTextureHandlesMagnification.Pop();

    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( Renders, DepthRange, "library", "Set's the depth range of the upcoming render.", "client" )
{
    float depthMin = LUA_BINDING_ARGUMENT( luaL_checknumber, 1, "depthMin" );
    float depthMax = LUA_BINDING_ARGUMENT( luaL_checknumber, 2, "depthMax" );

    CMatRenderContextPtr pRenderContext( materials );
    pRenderContext->DepthRange( depthMin, depthMax );

    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( Renders, GetViewEntity, "library", "Returns the entity the client is using to see (mostly the player itself)", "client" )
{
    int viewEntityId = render->GetViewEntity();
    C_BaseEntity *pCameraObject = cl_entitylist->GetEnt( viewEntityId );
    CBaseEntity::PushLuaInstanceSafe( L, pCameraObject );

    return 1;
}
LUA_BINDING_END( "Entity", "The view entity." )

#ifdef LUA_SDK
static IMaterial *g_pLuaBoundMaterial = NULL;
#endif

LUA_BINDING_BEGIN( Renders, SetMaterial, "library", "Binds a material for use in the next render operations", "client" )
{
    IMaterial *pMaterial = LUA_BINDING_ARGUMENT( luaL_checkmaterial, 1, "material" );

    CMatRenderContextPtr pRenderContext( materials );
    pRenderContext->Bind( pMaterial );
#ifdef LUA_SDK
    // Remembered for render.DrawScreenQuad; GetCurrentMaterial() is unreliable on the queued render context
    g_pLuaBoundMaterial = pMaterial;
#endif

    return 0;
}
LUA_BINDING_END()

#ifdef LUA_SDK
// GMod render.* functions used by halo.lua, render.lua and the post-process scripts

// A NULL render target means "the back buffer" (render.GetRenderTarget() returns NULL while
// drawing straight to the screen), so these have to accept it instead of raising an error.
static ITexture *LuaOptRenderTarget( lua_State *L, int narg )
{
    if ( lua_isnoneornil( L, narg ) )
        return NULL;

    lua_ITexture **ppTexture = ( lua_ITexture ** )luaL_checkudata( L, narg, LUA_ITEXTUREMETANAME );
    return ppTexture ? *ppTexture : NULL;
}

LUA_BINDING_BEGIN( Renders, SetRenderTarget, "library", "Sets the active render target.", "client" )
{
    ITexture *pTexture = LUA_BINDING_ARGUMENT( LuaOptRenderTarget, 1, "texture" );
    CMatRenderContextPtr pRenderContext( materials );
    pRenderContext->SetRenderTarget( pTexture );
    return 0;
}
LUA_BINDING_END()

//-----------------------------------------------------------------------------
// Srcbox: render targets pushed from Lua that were never popped (an addon that
// errors between PushRenderTarget and PopRenderTarget). Left alone, every later
// frame draws into that texture: a black screen while the game keeps running.
// LuaRender_RecoverLeakedRenderTargets runs at the start of each frame, outside
// any Lua code, and pops whatever is still on the stack.
//-----------------------------------------------------------------------------
static int s_nLuaRenderTargetDepth = 0;

static ITexture *LuaActiveRenderTarget()
{
    if ( s_nLuaRenderTargetDepth <= 0 )
        return NULL;

    CMatRenderContextPtr pRenderContext( materials );
    return pRenderContext->GetRenderTarget();
}

void LuaRender_RecoverLeakedRenderTargets()
{
    if ( s_nLuaRenderTargetDepth <= 0 )
        return;

    static int s_nReports = 0;
    if ( s_nReports < 5 )
    {
        Warning( "[Lua] %d render target(s) pushed by Lua were never popped (a script errored mid-render?); popping them\n", s_nLuaRenderTargetDepth );
        if ( ++s_nReports == 5 )
            Warning( "[Lua] Further leaked render target reports are muted\n" );
    }

    CMatRenderContextPtr pRenderContext( materials );
    while ( s_nLuaRenderTargetDepth > 0 )
    {
        pRenderContext->PopRenderTargetAndViewport();
        s_nLuaRenderTargetDepth--;
    }
}

LUA_BINDING_BEGIN( Renders, PushRenderTarget, "library", "Pushes a render target (and viewport) onto the stack.", "client" )
{
    ITexture *pTexture = LUA_BINDING_ARGUMENT( LuaOptRenderTarget, 1, "texture" );
    CMatRenderContextPtr pRenderContext( materials );

    if ( lua_gettop( L ) >= 5 )
    {
        int x = LUA_BINDING_ARGUMENT( luaL_checknumber, 2, "x" );
        int y = LUA_BINDING_ARGUMENT( luaL_checknumber, 3, "y" );
        int w = LUA_BINDING_ARGUMENT( luaL_checknumber, 4, "width" );
        int h = LUA_BINDING_ARGUMENT( luaL_checknumber, 5, "height" );
        pRenderContext->PushRenderTargetAndViewport( pTexture, x, y, w, h );
    }
    else
    {
        pRenderContext->PushRenderTargetAndViewport( pTexture );
    }
    s_nLuaRenderTargetDepth++;
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( Renders, PopRenderTarget, "library", "Pops the last render target (and viewport) from the stack.", "client" )
{
    CMatRenderContextPtr pRenderContext( materials );
    pRenderContext->PopRenderTargetAndViewport();
    if ( s_nLuaRenderTargetDepth > 0 )
        s_nLuaRenderTargetDepth--;
    return 0;
}
LUA_BINDING_END()

//-----------------------------------------------------------------------------
// Srcbox: GMod's render.CapturePixels / render.ReadPixel. CapturePixels copies
// the current render target to memory; ReadPixel reads one pixel of that copy.
//-----------------------------------------------------------------------------
static CUtlVector< unsigned char > s_CapturedPixels;
static int s_nCapturedWidth = 0;
static int s_nCapturedHeight = 0;

LUA_BINDING_BEGIN( Renders, CapturePixels, "library", "Copy the current render target's pixels to memory, for Renders.ReadPixel.", "client" )
{
    CMatRenderContextPtr pRenderContext( materials );

    int nWidth = 0, nHeight = 0;
    pRenderContext->GetRenderTargetDimensions( nWidth, nHeight );

    if ( nWidth <= 0 || nHeight <= 0 )
    {
        s_nCapturedWidth = s_nCapturedHeight = 0;
        return 0;
    }

    s_CapturedPixels.SetCount( nWidth * nHeight * 4 );
    pRenderContext->ReadPixels( 0, 0, nWidth, nHeight, s_CapturedPixels.Base(), IMAGE_FORMAT_RGBA8888 );
    s_nCapturedWidth = nWidth;
    s_nCapturedHeight = nHeight;
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( Renders, ReadPixel, "library", "Read a pixel captured by Renders.CapturePixels.", "client" )
{
    int x = ( int )LUA_BINDING_ARGUMENT( luaL_checknumber, 1, "x" );
    int y = ( int )LUA_BINDING_ARGUMENT( luaL_checknumber, 2, "y" );

    if ( x < 0 || y < 0 || x >= s_nCapturedWidth || y >= s_nCapturedHeight )
    {
        lua_pushinteger( L, 0 );
        lua_pushinteger( L, 0 );
        lua_pushinteger( L, 0 );
        lua_pushinteger( L, 0 );
        return 4;
    }

    const unsigned char *pPixel = s_CapturedPixels.Base() + ( y * s_nCapturedWidth + x ) * 4;
    lua_pushinteger( L, pPixel[0] );
    lua_pushinteger( L, pPixel[1] );
    lua_pushinteger( L, pPixel[2] );
    lua_pushinteger( L, pPixel[3] );
    return 4;
}
LUA_BINDING_END( "integer", "Red", "integer", "Green", "integer", "Blue", "integer", "Alpha" )

LUA_BINDING_BEGIN( Renders, UpdateRefractTexture, "library", "Copies the current frame into the refraction texture.", "client" )
{
    UpdateRefractTexture( true );
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( Renders, DrawScreenQuad, "library", "Draws the bound material over the whole viewport.", "client" )
{
    CMatRenderContextPtr pRenderContext( materials );
    IMaterial *pMaterial = g_pLuaBoundMaterial ? g_pLuaBoundMaterial : pRenderContext->GetCurrentMaterial();

    if ( pMaterial )
    {
        // DrawScreenSpaceQuad draws nothing here (verified in-game); DrawScreenSpaceRectangle works,
        // so cover the current viewport with it and map the whole texture across it.
        int x, y, w, h;
        pRenderContext->GetViewport( x, y, w, h );

        if ( w > 0 && h > 0 )
            pRenderContext->DrawScreenSpaceRectangle( pMaterial, 0, 0, w, h, 0, 0, w - 1, h - 1, w, h );
    }

    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( Renders, DrawScreenQuadEx, "library", "Draws the bound material over a screen rectangle.", "client" )
{
    int x = LUA_BINDING_ARGUMENT( luaL_checknumber, 1, "x" );
    int y = LUA_BINDING_ARGUMENT( luaL_checknumber, 2, "y" );
    int w = LUA_BINDING_ARGUMENT( luaL_checknumber, 3, "width" );
    int h = LUA_BINDING_ARGUMENT( luaL_checknumber, 4, "height" );

    CMatRenderContextPtr pRenderContext( materials );
    IMaterial *pMaterial = g_pLuaBoundMaterial ? g_pLuaBoundMaterial : pRenderContext->GetCurrentMaterial();

    if ( pMaterial && w > 0 && h > 0 )
        pRenderContext->DrawScreenSpaceRectangle( pMaterial, x, y, w, h, 0, 0, w - 1, h - 1, w, h );

    return 0;
}
LUA_BINDING_END()

static IMaterial *GetLuaBlurMaterial( bool bX )
{
    static IMaterial *s_pBlur[2] = { NULL, NULL };
    IMaterial *&pMaterial = s_pBlur[bX ? 0 : 1];

    if ( !pMaterial )
    {
        KeyValues *pKeyValues = new KeyValues( bX ? "BlurFilterX" : "BlurFilterY" );
        pKeyValues->SetString( "$basetexture", "_rt_FullFrameFB1" );
        pKeyValues->SetInt( "$ignorez", 1 );
        pKeyValues->SetInt( "$nofog", 1 );
        pKeyValues->SetFloat( "$bloomamount", 1.0f );
        pMaterial = materials->CreateMaterial( bX ? "__lua_blurx" : "__lua_blury", pKeyValues );
        pMaterial->IncrementReferenceCount();
    }

    return pMaterial;
}

static ITexture *GetLuaBlurScratch()
{
    static ITexture *s_pScratch = NULL;

    if ( !s_pScratch )
    {
        g_pMaterialSystem->OverrideRenderTargetAllocation( true );
        s_pScratch = materials->CreateNamedRenderTargetTextureEx(
            "_rt_LuaBlurScratch", 1, 1, RT_SIZE_FULL_FRAME_BUFFER,
            materials->GetBackBufferFormat(), MATERIAL_RT_DEPTH_NONE,
            TEXTUREFLAGS_CLAMPS | TEXTUREFLAGS_CLAMPT, 0 );
        g_pMaterialSystem->OverrideRenderTargetAllocation( false );

        if ( s_pScratch )
            s_pScratch->IncrementReferenceCount();
    }

    return s_pScratch;
}

LUA_BINDING_BEGIN( Renders, BlurRenderTarget, "library", "Blurs a render target texture.", "client" )
{
    ITexture *pTexture = LUA_BINDING_ARGUMENT( luaL_checkitexture, 1, "texture" );
    float blurX = LUA_BINDING_ARGUMENT_WITH_DEFAULT( luaL_optnumber, 2, 1, "blurX" );
    float blurY = LUA_BINDING_ARGUMENT_WITH_DEFAULT( luaL_optnumber, 3, 1, "blurY" );
    int passes = LUA_BINDING_ARGUMENT_WITH_DEFAULT( luaL_optnumber, 4, 1, "passes" );

    ITexture *pScratch = GetLuaBlurScratch();

    if ( !pTexture || !pScratch )
        return 0;

    IMaterial *pBlurX = GetLuaBlurMaterial( true );
    IMaterial *pBlurY = GetLuaBlurMaterial( false );

    int w = pTexture->GetActualWidth();
    int h = pTexture->GetActualHeight();

    // The blur shaders use a fixed kernel, so approximate GMod's pixel radius with extra iterations
    int iterations = clamp( passes, 1, 8 ) * clamp( ( int )( MAX( blurX, blurY ) * 0.5f + 0.5f ), 1, 4 );

    CMatRenderContextPtr pRenderContext( materials );

    for ( int i = 0; i < iterations; i++ )
    {
        // texture -> scratch (horizontal)
        pRenderContext->PushRenderTargetAndViewport( pScratch, 0, 0, w, h );
        bool bFound = false;
        IMaterialVar *pVar = pBlurX->FindVar( "$basetexture", &bFound, false );
        if ( bFound ) pVar->SetTextureValue( pTexture );
        pRenderContext->DrawScreenSpaceRectangle( pBlurX, 0, 0, w, h, 0, 0, w - 1, h - 1, w, h );
        pRenderContext->PopRenderTargetAndViewport();

        // scratch -> texture (vertical)
        pRenderContext->PushRenderTargetAndViewport( pTexture, 0, 0, w, h );
        pVar = pBlurY->FindVar( "$basetexture", &bFound, false );
        if ( bFound ) pVar->SetTextureValue( pScratch );
        pRenderContext->DrawScreenSpaceRectangle( pBlurY, 0, 0, w, h, 0, 0, w - 1, h - 1, w, h );
        pRenderContext->PopRenderTargetAndViewport();
    }

    return 0;
}
LUA_BINDING_END()
#endif

LUA_BINDING_BEGIN( Renders, DrawSprite, "library", "Draws a sprite", "client" )
{
    Vector position = LUA_BINDING_ARGUMENT( luaL_checkvector, 1, "position" );
    float width = LUA_BINDING_ARGUMENT( luaL_checknumber, 2, "width" );
    float height = LUA_BINDING_ARGUMENT( luaL_checknumber, 3, "height" );
    lua_Color color = LUA_BINDING_ARGUMENT_WITH_DEFAULT( luaL_optcolor, 4, lua_Color( 255, 255, 255, 255 ), "color" );

    color32 rawColor = { color.r(), color.g(), color.b(), color.a() };
    DrawSprite( position, width, height, rawColor );

    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( Renders, DrawBeam, "library", "Draws a beam", "client" )
{
    Vector &start = LUA_BINDING_ARGUMENT( luaL_checkvector, 1, "start" );
    Vector &end = LUA_BINDING_ARGUMENT( luaL_checkvector, 2, "end" );
    float width = LUA_BINDING_ARGUMENT( luaL_checknumber, 3, "width" );
    float textureStart = LUA_BINDING_ARGUMENT( luaL_checknumber, 4, "textureStart" );
    float textureEnd = LUA_BINDING_ARGUMENT( luaL_checknumber, 5, "textureEnd" );
    lua_Color color = LUA_BINDING_ARGUMENT_WITH_DEFAULT( luaL_optcolor, 6, lua_Color( 255, 255, 255, 255 ), "color" );

    CMatRenderContextPtr pRenderContext( materials );
    CBeamSegDraw beamDraw;
    beamDraw.Start( pRenderContext, 2, NULL );

    BeamSeg_t seg;
    seg.m_flAlpha = 1.0;
    seg.m_flWidth = width;

    seg.m_vPos = start;
    seg.m_flTexCoord = textureStart;
    seg.m_vColor = color.ToVector();
    beamDraw.NextSeg( &seg );

    seg.m_vPos = end;
    seg.m_flTexCoord = textureEnd;
    seg.m_vColor = color.ToVector();
    beamDraw.NextSeg( &seg );

    beamDraw.End();

    return 0;
}
LUA_BINDING_END()

#define STENCIL_COMPARISON_FUNCTION StencilComparisonFunction_t
#define STENCIL_OPERATION StencilOperation_t

LUA_BINDING_BEGIN( Renders, SetStencilCompareFunction, "library", "Set the stencil compare function.", "client" )
{
    CMatRenderContextPtr pRenderContext( materials );
    pRenderContext->SetStencilCompareFunction( LUA_BINDING_ARGUMENT_ENUM( STENCIL_COMPARISON_FUNCTION, 1, "compareFunction" ) );
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( Renders, SetStencilEnable, "library", "Set the stencil enable.", "client" )
{
    CMatRenderContextPtr pRenderContext( materials );
    pRenderContext->SetStencilEnable( LUA_BINDING_ARGUMENT( lua_toboolean, 1, "enable" ) );
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( Renders, SetStencilFailOperation, "library", "Set the stencil fail operation.", "client" )
{
    CMatRenderContextPtr pRenderContext( materials );
    pRenderContext->SetStencilFailOperation( LUA_BINDING_ARGUMENT_ENUM( STENCIL_OPERATION, 1, "failOperation" ) );
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( Renders, SetStencilPassOperation, "library", "Set the stencil pass operation.", "client" )
{
    CMatRenderContextPtr pRenderContext( materials );
    pRenderContext->SetStencilPassOperation( LUA_BINDING_ARGUMENT_ENUM( STENCIL_OPERATION, 1, "passOperation" ) );
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( Renders, SetStencilReferenceValue, "library", "Set the stencil reference value.", "client" )
{
    CMatRenderContextPtr pRenderContext( materials );
    pRenderContext->SetStencilReferenceValue( LUA_BINDING_ARGUMENT( luaL_checknumber, 1, "referenceValue" ) );
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( Renders, SetStencilTestMask, "library", "Set the stencil test mask.", "client" )
{
    CMatRenderContextPtr pRenderContext( materials );
    pRenderContext->SetStencilTestMask( LUA_BINDING_ARGUMENT( luaL_checknumber, 1, "testMask" ) );
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( Renders, SetStencilWriteMask, "library", "Set the stencil write mask.", "client" )
{
    CMatRenderContextPtr pRenderContext( materials );
    pRenderContext->SetStencilWriteMask( LUA_BINDING_ARGUMENT( luaL_checknumber, 1, "writeMask" ) );
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( Renders, SetStencilZFailOperation, "library", "Set the stencil z fail operation.", "client" )
{
    CMatRenderContextPtr pRenderContext( materials );
    pRenderContext->SetStencilZFailOperation( LUA_BINDING_ARGUMENT_ENUM( STENCIL_OPERATION, 1, "zFailOperation" ) );
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( Renders, ClearStencilBufferRectangle, "library", "Clear the stencil buffer rectangle.", "client" )
{
    int startX = LUA_BINDING_ARGUMENT( luaL_checknumber, 1, "startX" );
    int startY = LUA_BINDING_ARGUMENT( luaL_checknumber, 2, "startY" );
    int endX = LUA_BINDING_ARGUMENT( luaL_checknumber, 3, "endX" );
    int endY = LUA_BINDING_ARGUMENT( luaL_checknumber, 4, "endY" );
    int value = LUA_BINDING_ARGUMENT( luaL_checknumber, 5, "value" );

    CMatRenderContextPtr pRenderContext( materials );
    pRenderContext->ClearStencilBufferRectangle( startX, startY, endX, endY, value );

    return 0;
}
LUA_BINDING_END()

#endif  // CLIENT_DLL

/*
** Open render library
*/
LUALIB_API int luaopen_render( lua_State *L )
{
    LUA_REGISTRATION_COMMIT_LIBRARY( Renders );

#if defined( CLIENT_DLL ) && defined( LUA_SDK )
    luaopen_RenderExtensions( L );
    luaopen_MaterialProxies( L );
#endif

#ifdef CLIENT_DLL
    LUA_SET_ENUM_LIB_BEGIN( L, "CULL_MODE" );
    lua_pushenum( L, MaterialCullMode_t::MATERIAL_CULLMODE_CCW, "COUNTER_CLOCKWISE" );
    lua_pushenum( L, MaterialCullMode_t::MATERIAL_CULLMODE_CW, "CLOCKWISE" );
    LUA_SET_ENUM_LIB_END( L );

    LUA_SET_ENUM_LIB_BEGIN( L, "STENCIL_COMPARISON_FUNCTION" );
    lua_pushenum( L, StencilComparisonFunction_t::STENCILCOMPARISONFUNCTION_NEVER, "NEVER" );
    lua_pushenum( L, StencilComparisonFunction_t::STENCILCOMPARISONFUNCTION_LESS, "LESS" );
    lua_pushenum( L, StencilComparisonFunction_t::STENCILCOMPARISONFUNCTION_EQUAL, "EQUAL" );
    lua_pushenum( L, StencilComparisonFunction_t::STENCILCOMPARISONFUNCTION_LESSEQUAL, "LESS_OR_EQUAL" );
    lua_pushenum( L, StencilComparisonFunction_t::STENCILCOMPARISONFUNCTION_GREATER, "GREATER" );
    lua_pushenum( L, StencilComparisonFunction_t::STENCILCOMPARISONFUNCTION_NOTEQUAL, "NOT_EQUAL" );
    lua_pushenum( L, StencilComparisonFunction_t::STENCILCOMPARISONFUNCTION_GREATEREQUAL, "GREATER_OR_EQUAL" );
    lua_pushenum( L, StencilComparisonFunction_t::STENCILCOMPARISONFUNCTION_ALWAYS, "ALWAYS" );
    LUA_SET_ENUM_LIB_END( L );

    LUA_SET_ENUM_LIB_BEGIN( L, "STENCIL_OPERATION" );
    lua_pushenum( L, StencilOperation_t::STENCILOPERATION_KEEP, "KEEP" );
    lua_pushenum( L, StencilOperation_t::STENCILOPERATION_ZERO, "ZERO" );
    lua_pushenum( L, StencilOperation_t::STENCILOPERATION_REPLACE, "REPLACE" );
    lua_pushenum( L, StencilOperation_t::STENCILOPERATION_INCRSAT, "INCREMENT_CLAMP" );
    lua_pushenum( L, StencilOperation_t::STENCILOPERATION_DECRSAT, "DECREMENT_CLAMP" );
    lua_pushenum( L, StencilOperation_t::STENCILOPERATION_INVERT, "INVERT" );
    lua_pushenum( L, StencilOperation_t::STENCILOPERATION_INCR, "INCREMENT_WRAP" );
    lua_pushenum( L, StencilOperation_t::STENCILOPERATION_DECR, "DECREMENT_WRAP" );
    LUA_SET_ENUM_LIB_END( L );
#endif

    return 1;
}
