//========= Srcbox ============================================================//
//
// Purpose: Garry's Mod's movement and key hooks, run for every user command on
//          the server ( player_command.cpp ) and in the client's prediction
//          ( prediction.cpp ), the way GMod runs them:
//
//          KeyPress( ply, key ) / KeyRelease( ply, key )
//          SetupMove( ply, mv, cmd )          after the move data is filled in
//          PlayerTick( ply, mv )
//          Move( ply, mv )                    true = skip the engine's movement
//          FinishMove( ply, mv )              true = skip copying mv back to the player
//
//=============================================================================//

#ifndef SRCBOX_GMODMOVEHOOKS_H
#define SRCBOX_GMODMOVEHOOKS_H
#ifdef _WIN32
#pragma once
#endif

#ifdef LUA_SDK

#include "srcbox/srcbox_gmodhooks.h"
#include "igamemovement.h"
#include "usercmd.h"
#include "lmovedata.h"
#include "lusercmd.h"
#include "lbaseplayer_shared.h"

inline void SrcboxCallKeyHooks( CBasePlayer *pPlayer )
{
    if ( !L || !pPlayer )
        return;

    const int pressed = pPlayer->m_afButtonPressed;
    const int released = pPlayer->m_afButtonReleased;

    if ( !pressed && !released )
        return;

    for ( int bit = 0; bit < 32; bit++ )
    {
        const int key = 1 << bit;

        if ( pressed & key )
        {
            SRCBOX_HOOK_BEGIN( "KeyPress" )
                CBasePlayer::PushLuaInstanceSafe( L, pPlayer );
                lua_pushinteger( L, key );
            SRCBOX_HOOK_CALL( 2 )
            SRCBOX_HOOK_END()
        }

        if ( released & key )
        {
            SRCBOX_HOOK_BEGIN( "KeyRelease" )
                CBasePlayer::PushLuaInstanceSafe( L, pPlayer );
                lua_pushinteger( L, key );
            SRCBOX_HOOK_CALL( 2 )
            SRCBOX_HOOK_END()
        }
    }
}

inline void SrcboxCallSetupMove( CBasePlayer *pPlayer, CMoveData *pMove, CUserCmd *pCmd )
{
    if ( !L || !pPlayer || !pMove )
        return;

    SRCBOX_HOOK_BEGIN( "SetupMove" )
        CBasePlayer::PushLuaInstanceSafe( L, pPlayer );
        lua_pushmovedata( L, pMove );
        lua_pushusercmd( L, pCmd );
    SRCBOX_HOOK_CALL( 3 )
    SRCBOX_HOOK_END()

    SRCBOX_HOOK_BEGIN( "PlayerTick" )
        CBasePlayer::PushLuaInstanceSafe( L, pPlayer );
        lua_pushmovedata( L, pMove );
    SRCBOX_HOOK_CALL( 2 )
    SRCBOX_HOOK_END()
}

// GMod's StartCommand( ply, cmd ): before the command is used, so the hook can change it
inline void SrcboxCallStartCommand( CBasePlayer *pPlayer, CUserCmd *pCmd )
{
    if ( !L || !pPlayer || !pCmd )
        return;

    SRCBOX_HOOK_BEGIN( "StartCommand" )
        CBasePlayer::PushLuaInstanceSafe( L, pPlayer );
        lua_pushusercmd( L, pCmd );
    SRCBOX_HOOK_CALL( 2 )
    SRCBOX_HOOK_END()
}

// true: the hook did the movement itself
inline bool SrcboxCallMove( CBasePlayer *pPlayer, CMoveData *pMove )
{
    bool bOverride = false;

    if ( !L || !pPlayer || !pMove )
        return false;

    SRCBOX_HOOK_BEGIN( "Move" )
        CBasePlayer::PushLuaInstanceSafe( L, pPlayer );
        lua_pushmovedata( L, pMove );
    SRCBOX_HOOK_CALL( 2 )
        bOverride = SRCBOX_HOOK_RETURNED_TRUE();
    SRCBOX_HOOK_END()

    return bOverride;
}

// true: the hook applied the move data itself
inline bool SrcboxCallFinishMove( CBasePlayer *pPlayer, CMoveData *pMove )
{
    bool bOverride = false;

    if ( !L || !pPlayer || !pMove )
        return false;

    SRCBOX_HOOK_BEGIN( "FinishMove" )
        CBasePlayer::PushLuaInstanceSafe( L, pPlayer );
        lua_pushmovedata( L, pMove );
    SRCBOX_HOOK_CALL( 2 )
        bOverride = SRCBOX_HOOK_RETURNED_TRUE();
    SRCBOX_HOOK_END()

    return bOverride;
}

#endif // LUA_SDK

#endif // SRCBOX_GMODMOVEHOOKS_H
