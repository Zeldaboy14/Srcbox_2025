#ifndef LExperiment_PLAYER_SHARED_H
#define LExperiment_PLAYER_SHARED_H
#pragma once

#ifdef CLIENT_DLL
#include "c_hl2mp_player.h"
#else
#include "HL2MP_player.h"
#endif

/* type for CExperiment_Player functions */
#ifdef CLIENT_DLL
#define lua_CExperiment_Player C_HL2MP_Player
#else
#define lua_CExperiment_Player CHL2MP_Player
#endif

/*
** access functions (stack -> C)
*/
LUA_API lua_CExperiment_Player *( luaL_toexperimentplayer )( lua_State *L, int idx );

/*
** push functions (C -> stack)
*/
LUALIB_API lua_CExperiment_Player *( luaL_checkexperimentplayer )( lua_State *L, int narg );
LUALIB_API lua_CExperiment_Player *( luaL_optexperimentplayer )( lua_State *L, int narg, lua_CExperiment_Player *def );

#endif  // LExperiment_PLAYER_SHARED_h
