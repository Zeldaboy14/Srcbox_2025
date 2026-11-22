//========= Copyright © 1996-2005, Valve Corporation, All rights reserved. ============//
//
// Purpose: Client DLL VGUI2 Viewport
//
// $Workfile:     $
// $Date:         $
//
//-----------------------------------------------------------------------------
// $Log: $
//
// $NoKeywords: $
//=============================================================================//

#include "cbase.h"

// our definition
#include "scriptedhudviewport.h"

// lua hooks
#ifdef LUA_SDK
#include "luamanager.h"
#include "srcbox/srcbox_gmodhooks.h"
#endif

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

using namespace vgui;

//================================================================
CScriptedHudViewport::CScriptedHudViewport( lua_State* L )
    : vgui::EditablePanel( NULL, "CScriptedHudViewport" )
{
    m_lua_State = L;

    SetKeyBoardInputEnabled( false );
    SetMouseInputEnabled( false );

    SetProportional( true );
}

//-----------------------------------------------------------------------------
// Purpose: Sets the parent for each panel to use
//-----------------------------------------------------------------------------
void CScriptedHudViewport::SetParent( vgui::VPANEL parent )
{
    EditablePanel::SetParent( parent );
    // force ourselves to be proportional - when we set our parent above, if our new
    // parent happened to be non-proportional (such as the vgui root panel), we got
    // slammed to be nonproportional
    EditablePanel::SetProportional( true );
}

void CScriptedHudViewport::Paint()
{
#ifdef LUA_SDK
    // Srcbox: GMod's PreDrawHUD and HUDPaintBackground come before HUDPaint ( which the compat
    // layer runs from HudDraw ), PostDrawHUD after it
    SRCBOX_HOOK_BEGIN( "PreDrawHUD" )
    SRCBOX_HOOK_CALL( 0 )
    SRCBOX_HOOK_END()

    SRCBOX_HOOK_BEGIN( "HUDPaintBackground" )
    SRCBOX_HOOK_CALL( 0 )
    SRCBOX_HOOK_END()
#endif

    LUA_CALL_HOOK_BEGIN( "HudDraw" );
    LUA_CALL_HOOK_END( 0, 0 );

#ifdef LUA_SDK
    SRCBOX_HOOK_BEGIN( "PostDrawHUD" )
    SRCBOX_HOOK_CALL( 0 )
    SRCBOX_HOOK_END()
#endif
}
