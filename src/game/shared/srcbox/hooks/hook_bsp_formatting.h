//=============================================================================//
//
// Purpose: Runtime compatibility for old Source seven-face cubemap VTFs.
//
//=============================================================================//

#ifndef SRCBOX_BSP_FORMATTING
#define SRCBOX_BSP_FORMATTING
#ifdef _WIN32
#pragma once
#endif

class IFileSystem;

void Srcbox_InstallBSPHook();
void Srcbox_DeloadBSPHook();

#endif // L4DASW_OLD_CUBEMAP_VTF_H