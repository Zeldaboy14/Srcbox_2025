//========= Srcbox ============================================================//
//
// Purpose: Per-instance color modulation for static props (see staticprop_colormod.cpp)
//
//=============================================================================//

#ifndef STATICPROP_COLORMOD_H
#define STATICPROP_COLORMOD_H
#ifdef _WIN32
#pragma once
#endif

#include "interface.h"

// Hooks IStudioRender's static prop drawing. Call once from CHLClient::Init.
void StaticPropColorMod_Install( CreateInterfaceFn appSystemFactory );

#endif // STATICPROP_COLORMOD_H
