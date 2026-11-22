//========= Srcbox ============================================================//
//
// Purpose: helpers for calling Garry's Mod's gamemode hooks ( hook.Call( name,
//          GAMEMODE, ... ) ) from engine code and reading what they return.
//
//          SRCBOX_HOOK_BEGIN( "PlayerUse" )
//              CBasePlayer::PushLuaInstanceSafe( L, pPlayer );
//              CBaseEntity::PushLuaInstanceSafe( L, pEntity );
//          SRCBOX_HOOK_CALL( 2 )
//              bBlocked = SRCBOX_HOOK_RETURNED_FALSE();
//          SRCBOX_HOOK_END()
//
//          Between SRCBOX_HOOK_CALL and SRCBOX_HOOK_END the hook's first return
//          value ( if any ) is on top of the stack; SRCBOX_HOOK_END puts the
//          stack back how it was, whatever the hook did or didn't return.
//
//=============================================================================//

#ifndef SRCBOX_GMODHOOKS_H
#define SRCBOX_GMODHOOKS_H
#ifdef _WIN32
#pragma once
#endif

#ifdef LUA_SDK

#include "luamanager.h"

#define SRCBOX_HOOK_BEGIN( name )                                \
    {                                                            \
        lua_State *srcboxHookState = L;                          \
        const int srcboxHookTop = srcboxHookState ? lua_gettop( srcboxHookState ) : 0; \
        if ( srcboxHookState )                                   \
        {                                                        \
            LUA_CALL_HOOK_BEGIN( name );

#define SRCBOX_HOOK_CALL( nArgs )                                \
            LUA_CALL_HOOK_END( nArgs, 1 );                       \
        }

// The hook returned something other than nil
#define SRCBOX_HOOK_RETURNED() \
    ( srcboxHookState && lua_gettop( srcboxHookState ) > srcboxHookTop && !lua_isnil( srcboxHookState, -1 ) )

#define SRCBOX_HOOK_RETURNED_FALSE() \
    ( SRCBOX_HOOK_RETURNED() && lua_isboolean( srcboxHookState, -1 ) && !lua_toboolean( srcboxHookState, -1 ) )

#define SRCBOX_HOOK_RETURNED_TRUE() \
    ( SRCBOX_HOOK_RETURNED() && lua_isboolean( srcboxHookState, -1 ) && lua_toboolean( srcboxHookState, -1 ) )

#define SRCBOX_HOOK_END()                                        \
        if ( srcboxHookState )                                   \
            lua_settop( srcboxHookState, srcboxHookTop );        \
    }

// Whether the running gamemode ( GAMEMODE ) has a function by this name
inline bool SrcboxGamemodeHasFunction( const char *pszName )
{
    if ( !L )
        return false;

    const int top = lua_gettop( L );
    bool bHas = false;

    lua_getglobal( L, "GAMEMODE" );
    if ( lua_istable( L, -1 ) )
    {
        lua_getfield( L, -1, pszName );
        bHas = lua_isfunction( L, -1 );
    }

    lua_settop( L, top );
    return bHas;
}

#endif // LUA_SDK

#endif // SRCBOX_GMODHOOKS_H
