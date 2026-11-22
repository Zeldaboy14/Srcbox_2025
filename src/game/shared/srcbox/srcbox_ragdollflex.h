#ifndef SRCBOX_RAGDOLLFLEX_H
#define SRCBOX_RAGDOLLFLEX_H
#pragma once

//-----------------------------------------------------------------------------
// Srcbox: face flex weights on ragdolls ( prop_ragdoll ), so GMod's faceposer can pose them.
// Weights are 0..1 per flex controller, the same as CBaseFlex networks them. Each function
// returns false when the entity isn't a ragdoll.
//-----------------------------------------------------------------------------
#ifdef LUA_SDK

bool SrcboxRagdollFlex_GetWeight( CBaseEntity *pEntity, int index, float &flWeight );
bool SrcboxRagdollFlex_GetScale( CBaseEntity *pEntity, float &flScale );
bool SrcboxRagdollFlex_IsPosed( CBaseEntity *pEntity, bool &bPosed );

#ifndef CLIENT_DLL
bool SrcboxRagdollFlex_SetWeight( CBaseEntity *pEntity, int index, float flWeight );
bool SrcboxRagdollFlex_SetScale( CBaseEntity *pEntity, float flScale );
#endif

#endif // LUA_SDK

#endif // SRCBOX_RAGDOLLFLEX_H
