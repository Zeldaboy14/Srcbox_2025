//========= Srcbox ============================================================//
//
// Purpose: Loading newer BSPs (version 21) on the TF2 SDK engine, in real time
//          (see bsp_compat.cpp)
//
//=============================================================================//

#ifndef BSP_COMPAT_H
#define BSP_COMPAT_H
#ifdef _WIN32
#pragma once
#endif

// A .bsp opened with this bit in CreateFile's dwFlagsAndAttributes is read as
// it is on disk, not through the version 20 view. (FILE_ATTRIBUTE_OFFLINE:
// attributes are ignored when opening an existing file.)
#define BSPCOMPAT_BYPASS_ATTRIBUTE	0x00001000

// Starts redirecting newer maps. Call from CHLClient::Init and
// CServerGameDLL::DLLInit; the first call in the process installs it.
void BSPCompat_Install();

// Removes the redirect if this DLL installed it. Call when the DLL shuts down.
void BSPCompat_Shutdown();

#endif // BSP_COMPAT_H
