//=============================================================================//
//
// Purpose: Runtime compatibility for old Source seven-face cubemap VTFs.
//
//=============================================================================//

#ifndef L4DASW_OLD_CUBEMAP_VTF_H
#define L4DASW_OLD_CUBEMAP_VTF_H
#ifdef _WIN32
#pragma once
#endif

class IFileSystem;

void L4DASW_InstallOldCubemapVTFReadHook();
void L4DASW_RemoveOldCubemapVTFReadHook();

#endif // L4DASW_OLD_CUBEMAP_VTF_H