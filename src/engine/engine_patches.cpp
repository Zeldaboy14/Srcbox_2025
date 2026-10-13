#include "engine_patches.h"
#include <strtools.h>
#include "tier0/dbg.h"

// Experiment;
// These detours are experimental and give us access to engine features we
// wouldn't have access to otherwise. It's a shame that we have to be this
// hacky, but it's the only way to get access to things like CheckPassword and
// ClientConnect.

DWORD_PTR GetModuleBaseAddress( HMODULE module )
{
    return reinterpret_cast< DWORD_PTR >( module );
}

DWORD_PTR FindPattern( HMODULE module, const char *pattern, const char *mask )
{
    if ( !module || !pattern || !mask )
        return 0;

    PIMAGE_DOS_HEADER dos = ( PIMAGE_DOS_HEADER )module;
    PIMAGE_NT_HEADERS nt = ( PIMAGE_NT_HEADERS )( ( BYTE * )module + dos->e_lfanew );
    DWORD size = nt->OptionalHeader.SizeOfImage;
    BYTE *data = ( BYTE * )module;
    DWORD length = ( DWORD )strlen( mask );

    for ( DWORD i = 0; i <= size - length; i++ )
    {
        bool found = true;
        for ( DWORD j = 0; j < length; j++ )
        {
            if ( mask[j] != '?' && pattern[j] != ( char )data[i + j] )
            {
                found = false;
                break;
            }
        }
        if ( found )
            return ( DWORD_PTR )&data[i];
    }
    return 0;
}

//-----------------------------------------------------------------------------
// VTF 7.5 support (Alien Swarm / L4D2 / Portal 2 / CS:GO textures)
//
// Allows one to use VTF 7.5 support on the TF2 SDK. Does not work on 32bit builds (yet),
// or Source SDK 2013 (SP)
//-----------------------------------------------------------------------------
struct VTFBytePatch_t
{
	const char *m_pszName;
	const char *m_pOriginal;	// bytes to find (must be unique in the module)
	const char *m_pReplacement;	// same length as m_pOriginal
	int m_nLength;
};

static const VTFBytePatch_t s_VTF75Patches[] =
{
	// CVTFTexture::ReadHeader: header size by minor version
	//   mov eax,[rbp-19h]; cmp eax,4; jne; lea r8d,[rax+3Ch]; jmp read ... cmp eax,1; jbe v70
	// ->
	//   mov eax,[rbp-19h]; cmp eax,5; ja bad_version; cmp eax,1; jbe v70; mov r8d,40h; jmp read; int3 x20
	{
		"ReadHeader minor version",
		"\x8B\x45\xE7\x83\xF8\x04\x75\x06\x44\x8D\x40\x3C\xEB\x3A\x83\xF8\x03\x75\x06\x44\x8D\x40\x3D\xEB\x2F\x83\xF8\x02\x75\x06\x44\x8D\x40\x3E\xEB\x24\x83\xF8\x01\x76\x19",
		"\x8B\x45\xE7\x83\xF8\x05\x77\x21\x83\xF8\x01\x76\x35\x41\xB8\x40\x00\x00\x00\xEB\x33"
		"\xCC\xCC\xCC\xCC\xCC\xCC\xCC\xCC\xCC\xCC\xCC\xCC\xCC\xCC\xCC\xCC\xCC\xCC\xCC\xCC",
		41
	},
	// CVTFTexture::LoadImageData: number of cubemap faces stored in the file
	//   cmp dword [rbp+4],7; jne; cmp dword [rbp+8],1; mov eax,6; cmovl r15d,eax
	// ->
	//   mov eax,[rbp+8]; dec eax; cmp eax,3; jbe skip; mov r15d,6; nop3
	{
		"LoadImageData cubemap face count",
		"\x83\x7D\x04\x07\x75\x0D\x83\x7D\x08\x01\xB8\x06\x00\x00\x00\x44\x0F\x4C\xF8",
		"\x8B\x45\x08\xFF\xC8\x83\xF8\x03\x76\x09\x41\xBF\x06\x00\x00\x00\x0F\x1F\x00",
		19
	},
	// CVTFTexture::ImageFileInfo: same check for file offsets
	//   cmp dword [rbx+8],7; jne skip; cmp dword [rbx+0Ch],1; jge skip
	// ->
	//   mov eax,[rbx+0Ch]; dec eax; cmp eax,3; jbe skip; nop2
	{
		"ImageFileInfo cubemap face count",
		"\x83\x7B\x08\x07\x75\x15\x83\x7B\x0C\x01\x7D\x0F",
		"\x8B\x43\x0C\xFF\xC8\x83\xF8\x03\x76\x11\x66\x90",
		12
	},
};

static BYTE *FindUniqueBytes( HMODULE module, const char *pBytes, int nLength )
{
	PIMAGE_DOS_HEADER dos = ( PIMAGE_DOS_HEADER )module;
	PIMAGE_NT_HEADERS nt = ( PIMAGE_NT_HEADERS )( ( BYTE * )module + dos->e_lfanew );
	PIMAGE_SECTION_HEADER sec = IMAGE_FIRST_SECTION( nt );

	BYTE *pFound = NULL;
	for ( int i = 0; i < nt->FileHeader.NumberOfSections; i++, sec++ )
	{
		if ( !( sec->Characteristics & IMAGE_SCN_MEM_EXECUTE ) )
			continue;

		BYTE *pStart = ( BYTE * )module + sec->VirtualAddress;
		DWORD nSize = sec->Misc.VirtualSize;
		for ( DWORD j = 0; j + nLength <= nSize; j++ )
		{
			if ( memcmp( pStart + j, pBytes, nLength ) != 0 )
				continue;
			if ( pFound )
				return NULL;	// not unique, don't touch anything
			pFound = pStart + j;
		}
	}
	return pFound;
}

void ApplyVTFPatch()
{
	static bool s_bApplied = false;
	if ( s_bApplied )
		return;

	HMODULE hMaterialSys = GetModuleHandleA( "materialsystem.dll" );
	if ( !hMaterialSys )
		return;
	s_bApplied = true;

	// Find every site first so the patch is all-or-nothing.
	BYTE *pSites[ ARRAYSIZE( s_VTF75Patches ) ];
	for ( int i = 0; i < ARRAYSIZE( s_VTF75Patches ); i++ )
	{
		pSites[i] = FindUniqueBytes( hMaterialSys, s_VTF75Patches[i].m_pOriginal, s_VTF75Patches[i].m_nLength );
		if ( !pSites[i] )
		{
			// Already patched, or a different materialsystem.dll build
			Warning( "VTF 7.5 patch: \"%s\" not found in materialsystem.dll, VTF 7.5 support not enabled\n", s_VTF75Patches[i].m_pszName );
			return;
		}
	}

	for ( int i = 0; i < ARRAYSIZE( s_VTF75Patches ); i++ )
	{
		const VTFBytePatch_t &patch = s_VTF75Patches[i];
		DWORD oldProtect;
		if ( !VirtualProtect( pSites[i], patch.m_nLength, PAGE_EXECUTE_READWRITE, &oldProtect ) )
			continue;
		memcpy( pSites[i], patch.m_pReplacement, patch.m_nLength );
		VirtualProtect( pSites[i], patch.m_nLength, oldProtect, &oldProtect );
		FlushInstructionCache( GetCurrentProcess(), pSites[i], patch.m_nLength );
	}
}

void ApplyDetoursOnDllStartup()
{
    if ( MH_Initialize() != MH_OK )
    {
        // DevWarning( "Failed to initialize MinHook.\n" );
        return;
    }

#if defined( CLIENT_DLL )

#elif defined( GAME_DLL )
    // HandleCheckPasswordDetour();
    ApplyClientConnectDetour();
#endif
}

void NotifyDetoursOnLevelShutdown()
{
#if defined( CLIENT_DLL )

#elif defined( GAME_DLL )
    // UpdateCheckPasswordDetourOnLevelShutdown();
#endif
}
