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
// A 7.5 file has the same header as 7.4. The only format change is that
// cubemaps no longer store the spheremap face, so they hold 6 faces instead of 7.
// The TF2 SDK's materialsystem.dll (x64 and x86) needs three changes in its vtf code:
//
//  1. CVTFTexture::ReadHeader rejects any minor version above 4
//     ("*** Encountered VTF file with an invalid minor version!").
//     -> accept 5, reading the same 80-byte header as 7.2-7.4.
//  2. CVTFTexture::LoadImageData reads only 6 cubemap faces for 7.0 files
//     (which had no spheremap either). -> do the same for 7.5.
//  3. CVTFTexture::ImageFileInfo (file offsets used for mip-skipped / partial
//     loads) has the same 7.0-only check. -> do the same for 7.5.
//
// The major version is always 7 at these points (ReadHeader rejects anything else),
// so the patched code only tests the minor version: (minor - 1) > 3 unsigned means
// 0 or 5, the two versions whose cubemaps have 6 faces.
//
// Each patch checks every original byte before writing, so a different
// materialsystem.dll build is left untouched. Each patch also stops matching
// once applied, so calling this twice is harmless.
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
#ifdef PLATFORM_64BITS
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
#else
	// x86 materialsystem.dll (bin/materialsystem.dll of the same SDK build)

	// CVTFTexture::ReadHeader: header size by minor version
	//   mov eax,[ebp-78h]; cmp eax,4; jne; mov eax,40h; jmp read; ... cmp eax,1; je v70; test eax,eax; je v70
	// ->
	//   mov eax,[ebp-78h]; cmp eax,5; ja bad_version; cmp eax,1; jbe v70; mov eax,40h; jmp read; int3 x28
	{
		"ReadHeader minor version",
		"\x8B\x45\x88\x83\xF8\x04\x75\x07\xB8\x40\x00\x00\x00\xEB\x3E\x83\xF8\x03\x75\x07\xB8\x40\x00\x00\x00\xEB\x32"
		"\x83\xF8\x02\x75\x07\xB8\x40\x00\x00\x00\xEB\x26\x83\xF8\x01\x74\x1C\x85\xC0\x74\x18",
		"\x8B\x45\x88\x83\xF8\x05\x77\x28\x83\xF8\x01\x76\x3B\xB8\x40\x00\x00\x00\xEB\x39"
		"\xCC\xCC\xCC\xCC\xCC\xCC\xCC\xCC\xCC\xCC\xCC\xCC\xCC\xCC\xCC\xCC\xCC\xCC\xCC\xCC\xCC\xCC\xCC\xCC\xCC\xCC\xCC\xCC",
		48
	},
	// CVTFTexture::LoadImageData: number of cubemap faces stored in the file
	//   mov eax,[ebp+0Ch]; cmp dword [eax+4],7; jne; cmp dword [eax+8],1; mov ecx,6; cmovl ebx,ecx
	// ->
	//   mov eax,[ebp+0Ch]; mov eax,[eax+8]; dec eax; cmp eax,3; jbe skip; mov ebx,6; nop4
	{
		"LoadImageData cubemap face count",
		"\x8B\x45\x0C\x83\x78\x04\x07\x75\x0F\x83\x78\x08\x01\xB9\x06\x00\x00\x00\x0F\x4C\xD9",
		"\x8B\x45\x0C\x8B\x40\x08\x48\x83\xF8\x03\x76\x0C\xBB\x06\x00\x00\x00\x0F\x1F\x40\x00",
		21
	},
	// CVTFTexture::ImageFileInfo: same check for file offsets
	//   cmp dword [esi+4],7; jne skip; cmp dword [esi+8],1; jge skip
	// ->
	//   mov eax,[esi+8]; dec eax; cmp eax,3; jbe skip; nop3
	{
		"ImageFileInfo cubemap face count",
		"\x83\x7E\x04\x07\x75\x1A\x83\x7E\x08\x01\x7D\x14",
		"\x8B\x46\x08\x48\x83\xF8\x03\x76\x17\x0F\x1F\x00",
		12
	},
#endif
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

//-----------------------------------------------------------------------------
// Raised map (BSP) limits, like live TF2's 2025 update, as far as this engine.dll
// allows without rewriting its loaders. Only load-time count checks whose arrays
// are allocated from the map's own lump sizes are raised:
//
//  - brushes  8192 -> 32768 (CollisionBSPData_LoadBrushes, "Map has too many brushes").
//    cbrush_t is allocated per map; leaf brush lists hold 16-bit indices, so 32768 fits.
//  - texinfo 12288 -> 32768 (CollisionBSPData_LoadTexinfo, "Map has too many surfaces").
//    Allocated per map; faces/brush sides store texinfo as a 16-bit signed index.
//
// Not raised, because this build stores them in fixed or 16-bit storage:
//  - brush sides (65536): cbrush_t keeps firstbrushside/numsides as 16-bit values.
//  - displacements (2048): DispInfo_LoadDisplacements reads them into a fixed
//    2048-entry array on its stack.
// Overlays, internal entities/static props and t-junctions have no engine check
// here; those limits live in vbsp.
//
// Each patch checks the original bytes (unique in the module) before writing, so a
// different engine.dll build is left alone, and applying twice is harmless.
//-----------------------------------------------------------------------------
static const VTFBytePatch_t s_BSPLimitPatches[] =
{
#ifdef PLATFORM_64BITS
	// shr rbx,3; cmp ebx,2000h; jle  ->  cmp ebx,8000h
	{
		"brushes 8192 -> 32768",
		"\x48\xC1\xEB\x03\x81\xFB\x00\x20\x00\x00\x7E\x0C",
		"\x48\xC1\xEB\x03\x81\xFB\x00\x80\x00\x00\x7E\x0C",
		12
	},
	// jmp; cmp ebp,3000h; jle  ->  cmp ebp,8000h
	{
		"texinfo 12288 -> 32768",
		"\xEB\x0F\x81\xFD\x00\x30\x00\x00\x7E\x0C",
		"\xEB\x0F\x81\xFD\x00\x80\x00\x00\x7E\x0C",
		10
	},
#else
	// x86 engine.dll: shr ebx,3; cmp ebx,2000h; jle  ->  cmp ebx,8000h
	{
		"brushes 8192 -> 32768",
		"\xC1\xEB\x03\x81\xFB\x00\x20\x00\x00\x7E\x0D",
		"\xC1\xEB\x03\x81\xFB\x00\x80\x00\x00\x7E\x0D",
		11
	},
	// jmp; cmp ebx,3000h; jle  ->  cmp ebx,8000h
	{
		"texinfo 12288 -> 32768",
		"\xEB\x0D\x81\xFB\x00\x30\x00\x00\x7E\x0D",
		"\xEB\x0D\x81\xFB\x00\x80\x00\x00\x7E\x0D",
		10
	},
#endif
};

void ApplyBSPLimitsPatch()
{
	static bool s_bApplied = false;
	if ( s_bApplied )
		return;

	HMODULE hEngine = GetModuleHandleA( "engine.dll" );
	if ( !hEngine )
		return;
	s_bApplied = true;

	// Each limit is independent; apply whichever ones match this build
	for ( int i = 0; i < ARRAYSIZE( s_BSPLimitPatches ); i++ )
	{
		const VTFBytePatch_t &patch = s_BSPLimitPatches[i];
		BYTE *pSite = FindUniqueBytes( hEngine, patch.m_pOriginal, patch.m_nLength );
		if ( !pSite )
		{
			// Already patched (client and server both apply it), or a different engine.dll build
			if ( !FindUniqueBytes( hEngine, patch.m_pReplacement, patch.m_nLength ) )
				DevWarning( "BSP limits patch: \"%s\" not found in engine.dll, left unchanged\n", patch.m_pszName );
			continue;
		}

		DWORD oldProtect;
		if ( !VirtualProtect( pSite, patch.m_nLength, PAGE_EXECUTE_READWRITE, &oldProtect ) )
			continue;
		memcpy( pSite, patch.m_pReplacement, patch.m_nLength );
		VirtualProtect( pSite, patch.m_nLength, oldProtect, &oldProtect );
		FlushInstructionCache( GetCurrentProcess(), pSite, patch.m_nLength );
		DevMsg( "BSP limits patch: %s\n", patch.m_pszName );
	}
}

void ApplyDetoursOnDllStartup()
{
    // Both DLLs (a dedicated server has no client.dll); a no-op the second time. Before
    // MinHook, which fails with "already initialized" in the second DLL of a listen server.
    ApplyBSPLimitsPatch();

    if ( MH_Initialize() != MH_OK )
    {
        // DevWarning( "Failed to initialize MinHook.\n" );
        return;
    }

#if defined( CLIENT_DLL )
    ApplyVTFPatch();

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
