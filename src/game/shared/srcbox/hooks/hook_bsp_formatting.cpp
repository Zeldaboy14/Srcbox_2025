#include <stdlib.h>
#include <string.h>
#include <cbase.h>
#include "hook_bsp_formatting.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <Psapi.h>

#include "detours/detours.h"

//#include "winlite.h"
//extern HMODULE win32DLLHandle;

#define SIG(x) Sig({#x, sizeof(#x) - 1})
#define SIG_WILDCARD 0x2A

struct Sig { const char* sig; size_t len; };

/*class edict_t;
class CCommand;
enum EQueryCvarValueStatus;
typedef void* (*CreateInterfaceFn)(const char* pName, int* pReturnCode);
typedef int QueryCvarCookie_t;
enum PluginResult { PLUGIN_CONTINUE, PLUGIN_OVERRIDE, PLUGIN_STOP };
enum InterfaceResult { IFACE_OK, IFACE_FAILED };

typedef void* FileHandle_t;
enum FileSystemSeek_t { FILESYSTEM_SEEK_HEAD, FILESYSTEM_SEEK_CURRENT, FILESYSTEM_SEEK_TAIL };

struct Color { unsigned char r, g, b, a; };

struct Vector { float x, y, z; };
struct QAngle { float x, y, z; };

typedef void (*ConColorMsg)(const Color& clr, const char* msg, ...);
ConColorMsg Log = nullptr;

struct CPlugin
{
	virtual bool			Load(CreateInterfaceFn interfaceFactory, CreateInterfaceFn gameServerFactory);
	virtual void			Unload(void);
	virtual void			Pause(void) {}
	virtual void			UnPause(void) {}
	virtual const char*		GetPluginDescription(void) { return "BSPX"; }
	virtual void			LevelInit(char const* pMapName) {}
	virtual void			ServerActivate(edict_t* pEdictList, int edictCount, int clientMax) {}
	virtual void			GameFrame(bool simulating) {}
	virtual void			LevelShutdown(void) {}
	virtual void			ClientActive(edict_t* pEntity) {}
	virtual void			ClientDisconnect(edict_t* pEntity) {}
	virtual void			ClientPutInServer(edict_t* pEntity, char const* playername) {}
	virtual void			SetCommandClient(int index) {}
	virtual void			ClientSettingsChanged(edict_t* pEdict) {}
	virtual PluginResult	ClientConnect(bool* bAllowConnect, edict_t* pEntity, const char* pszName, const char* pszAddress, char* reject, int maxrejectlen) { return PLUGIN_CONTINUE; }
	virtual PluginResult	ClientCommand(edict_t* pEntity, const CCommand& args) { return PLUGIN_CONTINUE; }
	virtual PluginResult	NetworkIDValidated(const char* pszUserName, const char* pszNetworkID) { return PLUGIN_CONTINUE; }
	virtual void			OnQueryCvarValueFinished(QueryCvarCookie_t iCookie, edict_t* pPlayerEntity, EQueryCvarValueStatus eStatus, const char* pCvarName, const char* pCvarValue) {}
	virtual void			OnEdictAllocated(edict_t* edict) {}
	virtual void			OnEdictFreed(const edict_t* edict) {}
} plugin;

struct IBaseFileSystem
{
	virtual int				Read(void* pOutput, int size, FileHandle_t file) = 0;
	virtual int				Write(void const* pInput, int size, FileHandle_t file) = 0;
	virtual FileHandle_t	Open(const char* pFileName, const char* pOptions, const char* pathID = 0) = 0;
	virtual void			Close(FileHandle_t file) = 0;
	virtual void			Seek(FileHandle_t file, int pos, FileSystemSeek_t seekType) = 0;
	virtual unsigned int	Tell(FileHandle_t file) = 0;
	virtual unsigned int	Size(FileHandle_t file) = 0;
	virtual unsigned int	Size(const char* pFileName, const char* pPathID = 0) = 0;
	virtual void			Flush(FileHandle_t file) = 0;
	virtual bool			Precache(const char* pFileName, const char* pPathID = 0) = 0;
	virtual bool			FileExists(const char* pFileName, const char* pPathID = 0) = 0;
} *filesystem = NULL;*/

/*enum BSP_VERSION
{
	BSPVERSION_20, // Ignored

	BSPVERSION_21_ASW, // Alien Swarm
	BSPVERSION_21_TERROR, // L4D2
	BSPVERSION_21_CSTRIKE // Counter-Strike: Global Offensive
};*/

enum BSP_VERSION
{
	BSPVERSION_20, // Ignored

	BSPVERSION_21_TERROR, // Alien Swarm
	BSPVERSION_21_ASW, // L4D2
	BSPVERSION_21_CSTRIKE // Counter-Strike: Global Offensive
};

enum GAMELUMP
{
	GAMELUMP_DETAIL_PROPS = 'dprp',
	GAMELUMP_DETAIL_PROP_LIGHTING = 'dplt',
	GAMELUMP_STATIC_PROPS = 'sprp',
	GAMELUMP_DETAIL_PROP_LIGHTING_HDR = 'dplh',
};

struct lump_v20_t
{
	int		fileofs;
	int		filelen;
	int		version;
	int		uncompressedSize; // default to zero
};

struct lump_v21_terror_t
{
	int		version;
	int		fileofs;
	int		filelen;
	char	fourCC[4];
};

struct StaticPropLumpV9_terror_t
{
	Vector			m_Origin;
	QAngle			m_Angles;
	unsigned short	m_PropType;
	unsigned short	m_FirstLeaf;
	unsigned short	m_LeafCount;
	unsigned char	m_Solid;
	int				m_Skin;
	float			m_FadeMinDist;
	float			m_FadeMaxDist;
	Vector			m_LightingOrigin;
	float			m_flForcedFadeScale;
	unsigned short	m_nMinDXLevel;
	unsigned short	m_nMaxDXLevel;
	unsigned int	m_Flags;
	unsigned short  m_nLightmapResolutionX;
	unsigned short  m_nLightmapResolutionY;
};

struct StaticPropLumpV10_cstrike_t
{
	Vector			m_Origin;
	QAngle			m_Angles;
	unsigned short	m_PropType;
	unsigned short	m_FirstLeaf;
	unsigned short	m_LeafCount;
	unsigned char	m_Solid;
	unsigned char	m_Flags;
	int				m_Skin;
	float			m_FadeMinDist;
	float			m_FadeMaxDist;
	Vector			m_LightingOrigin;
	float			m_flForcedFadeScale;
	unsigned char	m_nMinCPULevel;
	unsigned char	m_nMaxCPULevel;
	unsigned char	m_nMinGPULevel;
	unsigned char	m_nMaxGPULevel;

	Color			m_DiffuseModulation;	// per instance color and alpha modulation
	bool			m_bDisableX360;
	int				m_FlagsEx;				// more flags (introduced in v10)
};

struct StaticPropLump_t // Nasty offset flags version ! most source games use this (?)
{
	Vector			m_Origin;
	QAngle			m_Angles;
	unsigned short	m_PropType;
	unsigned short	m_FirstLeaf;
	unsigned short	m_LeafCount;
	unsigned char	m_Solid;
	unsigned int	m_Flags;
	int				m_Skin;
	float			m_FadeMinDist;
	float			m_FadeMaxDist;
	Vector			m_LightingOrigin;
	float			m_flForcedFadeScale;
	unsigned short	m_nMinDXLevel;
	unsigned short	m_nMaxDXLevel;
	unsigned short  m_nLightmapResolutionX;
	unsigned short  m_nLightmapResolutionY;

	StaticPropLump_t& operator=(const StaticPropLumpV9_terror_t& _rhs)
	{
		m_Origin				= _rhs.m_Origin;
		m_Angles				= _rhs.m_Angles;
		m_PropType				= _rhs.m_PropType;
		m_FirstLeaf				= _rhs.m_FirstLeaf;
		m_LeafCount				= _rhs.m_LeafCount;
		m_Solid					= _rhs.m_Solid;
		m_Flags					= _rhs.m_Flags;
		m_Skin					= _rhs.m_Skin;
		m_FadeMinDist			= _rhs.m_FadeMinDist;
		m_FadeMaxDist			= _rhs.m_FadeMaxDist;
		m_LightingOrigin		= _rhs.m_LightingOrigin;
		m_flForcedFadeScale		= _rhs.m_flForcedFadeScale;
		m_nMinDXLevel			= _rhs.m_nMinDXLevel;
		m_nMaxDXLevel			= _rhs.m_nMaxDXLevel;
		m_nLightmapResolutionX	= _rhs.m_nLightmapResolutionX;
		m_nLightmapResolutionY	= _rhs.m_nLightmapResolutionY;

		return *this;
	}

	StaticPropLump_t& operator=(const StaticPropLumpV10_cstrike_t& _rhs)
	{
		m_Origin				= _rhs.m_Origin;
		m_Angles				= _rhs.m_Angles;
		m_PropType				= _rhs.m_PropType;
		m_FirstLeaf				= _rhs.m_FirstLeaf;
		m_LeafCount				= _rhs.m_LeafCount;
		m_Solid					= _rhs.m_Solid;
		m_Flags					= _rhs.m_Flags;
		m_Skin					= _rhs.m_Skin;
		m_FadeMinDist			= _rhs.m_FadeMinDist;
		m_FadeMaxDist			= _rhs.m_FadeMaxDist;
		m_LightingOrigin		= _rhs.m_LightingOrigin;
		m_flForcedFadeScale		= _rhs.m_flForcedFadeScale;

		m_nMinDXLevel			= 0;
		m_nMaxDXLevel			= 0;
		m_nLightmapResolutionX	= 0;
		m_nLightmapResolutionY	= 0;

		return *this;
	}
};

static void* FindSignature(const MODULEINFO& info, const Sig& sig)
{
	uintptr_t start = (uintptr_t)info.lpBaseOfDll;
	uintptr_t end = start + info.SizeOfImage - sig.len;

	for (uintptr_t i = start; i < end; i++)
	{
		bool found = true;
		for (size_t j = 0; j < sig.len; j++)
			found &= (sig.sig[j] == SIG_WILDCARD) || (sig.sig[j] == *(char*)(i + j));
		if (found)
			return (void*)i;
	}

	return NULL;
}

typedef bool (*func_modelLoader_isValid)(void*, char const *pMapFile, bool bQuiet);
func_modelLoader_isValid engine_modelLoader_isValid = NULL;
bool hook_modelLoader_isValid(void* thisptr, char const *pMapFile, bool bQuiet)
{
	return engine_modelLoader_isValid(thisptr, pMapFile, false);
}

typedef int (*func_mod_gameLumpVersion)(int);
func_mod_gameLumpVersion engine_mod_gameLumpVersion = NULL;

typedef void (*func_utlBuffer_get)(void*, void*, int size);
func_utlBuffer_get engine_utlBuffer_get = NULL;

template <typename SerializedLumpType>
void UnserializeLump(StaticPropLump_t* _output, void* thisptr)
{
	SerializedLumpType srcLump;
	engine_utlBuffer_get(thisptr, &srcLump, sizeof(SerializedLumpType));

	(*_output) = srcLump;
}

void hook_utlBuffer_get(void* thisptr, void* pMem, int size)
{
	if (size == sizeof(StaticPropLump_t))
	{
		int nLumpVersion = engine_mod_gameLumpVersion(GAMELUMP_STATIC_PROPS);

		if (nLumpVersion == 9)
		{
			UnserializeLump<StaticPropLumpV9_terror_t>((StaticPropLump_t*)pMem, thisptr);
		}
		else if (nLumpVersion == 10)
		{
			UnserializeLump<StaticPropLumpV10_cstrike_t>((StaticPropLump_t*)pMem, thisptr);
		}
		else if (nLumpVersion == 11)
		{
			float temp;

			UnserializeLump<StaticPropLumpV10_cstrike_t>((StaticPropLump_t*)pMem, thisptr);
			engine_utlBuffer_get(thisptr, &temp, sizeof(float));
		}
		else
		{
			engine_utlBuffer_get(thisptr, pMem, size);
		}
	}
	else
	{
		engine_utlBuffer_get(thisptr, pMem, size);
	}
}

typedef void (*func_modelLoader_precacheLighting)(void* thisptr);
func_modelLoader_precacheLighting engine_modelLoader_precacheLighting = NULL;
void hook_modelLoader_precacheLighting(void* thisptr)
{
	engine_modelLoader_precacheLighting(thisptr);
}

void* engine_crc_mapFile = NULL;
void* engine_md5_mapFile = NULL;

void* engine_mapLoadHelper_init = NULL;
void* engine_mapLoadHelper_initFromMemory = NULL;

void* engine_staticPropMgr_unserializeModels = NULL;
void* engine_dispInfo_loadDisplacements = NULL;

static bool FindSignatures()
{
	//Log({ 0, 255, 0, 255 }, PLUGIN_NAME "Finding signatures...\n");

	const char* client_name = "client.dll", *server_name = "server.dll", *engine_name = "engine.dll", *materialSystem_name = "materialsystem.dll";
	MODULEINFO client_info, server_info, engine_info, materialSystem_info;
	HMODULE client, server, engine, materialSystem;
	
	client = GetModuleHandle(client_name);
	if (!client)
	{
		//Log({ 255, 0, 0, 255 }, PLUGIN_NAME "Failed to get module info for %s\n", client_name);
		DevMsg("Failed to get module info for %s\n", client_name);
		return false;
	}

	server = GetModuleHandle(server_name);
	if (!server)
	{
		//Log({ 255, 0, 0, 255 }, PLUGIN_NAME "Failed to get module info for %s\n", server_name);
		DevMsg("Failed to get module info for %s\n", server_name);
		return false;
	}

	engine = GetModuleHandle(engine_name);
	if (!engine)
	{
		//Log({ 255, 0, 0, 255 }, PLUGIN_NAME "Failed to get module info for %s\n", engine_name);
		DevMsg("Failed to get module info for %s\n", engine_name);
		return false;
	}

	materialSystem = GetModuleHandle(materialSystem_name);
	if (!materialSystem)
	{
		//Log({ 255, 0, 0, 255 }, PLUGIN_NAME "Failed to get module info for %s\n", materialSystem_name);
		DevMsg("Failed to get module info for %s\n", materialSystem_name);
		return false;
	}

	GetModuleInformation(GetCurrentProcess(), client, &client_info, sizeof(client_info));
	GetModuleInformation(GetCurrentProcess(), server, &server_info, sizeof(server_info));
	GetModuleInformation(GetCurrentProcess(), engine, &engine_info, sizeof(engine_info));
	GetModuleInformation(GetCurrentProcess(), materialSystem, &materialSystem_info, sizeof(materialSystem_name));

	Sig sig_mod_gameLumpVersion = SIG(\x44\x8B\x0D\x2A\x2A\x2A\x2A\x41\x83\xE9\x01\x49\x63\xD1);

	Sig sig_utlBuffer_get = SIG(\x45\x85\xC0\x0F\x8E\x2A\x2A\x2A\x2A\x48\x89\x5C\x24\x2A\x48\x89\x74\x24);
	Sig sig_modelLoader_precacheLighting = SIG(\x48\x8B\xC4\x55\x48\x81\xEC\x90\x00\x00\x00);

	Sig sig_crc_mapFile = SIG(\x48\x89\x5C\x24\x2A\x48\x89\x6C\x24\x2A\x56\x57\x41\x56\x48\x81\xEC\x30\x08\x00\x00);
	Sig sig_md5_mapFile = SIG(\x48\x89\x5C\x24\x2A\x48\x89\x74\x24\x2A\x48\x89\x7C\x24\x2A\x55\x41\x56\x41\x57\x48\x8D\xAC\x24\x2A\x2A\x2A\x2A\x48\x81\xEC\x90\x08\x00\x00);

	Sig sig_mapLoadHelper_init = SIG(\x48\x89\x5C\x24\x2A\x57\x48\x83\xEC\x30\x8B\x05);
	Sig sig_mapLoadHelper_initFromMemory = SIG(\x48\x89\x5C\x24\x2A\x48\x89\x6C\x24\x2A\x56\x48\x83\xEC\x30\x83\x7C\x24\x2A\x00);

	Sig sig_staticPropMgr_unserializeModels = SIG(\x48\x89\x54\x24\x2A\x55\x56\x57\x41\x56\x48\x8D\xAC\x24);
	Sig sig_dispInfo_loadDisplacements = SIG(\x48\x89\x5C\x24\x2A\x88\x54\x24\x2A\x55\x56\x57\x41\x54\x41\x55\x41\x56\x41\x57\x48\x8D\xAC\x24);

	//Sig sig_readHeaderFromBufferPastBaseHeader = SIG(\x48\x89\x5C\x24\x2A\x48\x89\x74\x24\x2A\x48\x89\x7C\x24\x2A\x55\x41\x54\x41\x55\x41\x56\x41\x57\x48\x8D\x6C\x24);

	engine_mod_gameLumpVersion = (func_mod_gameLumpVersion)FindSignature(engine_info, sig_mod_gameLumpVersion);

	engine_utlBuffer_get = (func_utlBuffer_get)FindSignature(engine_info, sig_utlBuffer_get);
	engine_modelLoader_precacheLighting = (func_modelLoader_precacheLighting)FindSignature(engine_info, sig_modelLoader_precacheLighting);

	engine_crc_mapFile = FindSignature(engine_info, sig_crc_mapFile);
	engine_md5_mapFile = FindSignature(engine_info, sig_md5_mapFile);

	engine_mapLoadHelper_init = FindSignature(engine_info, sig_mapLoadHelper_init);
	engine_mapLoadHelper_initFromMemory = FindSignature(engine_info, sig_mapLoadHelper_initFromMemory);

	engine_staticPropMgr_unserializeModels = FindSignature(engine_info, sig_staticPropMgr_unserializeModels);
	engine_dispInfo_loadDisplacements = FindSignature(engine_info, sig_dispInfo_loadDisplacements);

	//materialSystem_readHeaderFromBufferPastBaseHeader = FindSignature(materialSystem_info, sig_readHeaderFromBufferPastBaseHeader);

	return true;
}

void LoadDetours()
{
	//Log({ 0, 255, 0, 255 }, PLUGIN_NAME "Loading detours...\n");

	DetourTransactionBegin();
	DetourUpdateThread(GetCurrentThread());

	DetourAttach(&engine_utlBuffer_get, hook_utlBuffer_get);
	DetourAttach(&engine_modelLoader_precacheLighting, hook_modelLoader_precacheLighting);

	DetourTransactionCommit();
}

static void UnloadDetours()
{
	DetourTransactionBegin();
	DetourUpdateThread(GetCurrentThread());

	DetourDetach(&engine_utlBuffer_get, hook_utlBuffer_get);
	DetourDetach(&engine_modelLoader_precacheLighting, hook_modelLoader_precacheLighting);

	DetourTransactionCommit();
}

static void LoadPatch(const char* name, void* hookAddress, int offset, const char* verify, const char* patch, int len)
{
	void* address = reinterpret_cast<void*>((uintptr_t)hookAddress + offset);
	auto pMemory = true ? (uintptr_t*)address : *reinterpret_cast<uintptr_t**>(address);

	if (sizeof(verify) != sizeof(patch))
	{
		//Log({ 255, 64, 64, 255 }, PLUGIN_NAME "%s has mismatched patch sizes.\n", name);
		DevMsg("%s has mismatched patch sizes.\n", name);
		return;
	}

	if (!memcmp(pMemory, verify, len))
	{
		//Log({ 0, 255, 0, 255 }, PLUGIN_NAME "%s successfully patched.\n", name);
		DevMsg("%s successfully patched.\n", name);

		DWORD protection;
		VirtualProtect(pMemory, len, PAGE_EXECUTE_READWRITE, &protection);

		memcpy(pMemory, patch, len);

		DWORD temp;
		VirtualProtect(pMemory, len, protection, &temp);
	}
	else
	{
		//Log({ 255, 64, 64, 255 }, PLUGIN_NAME "%s failed to patch.\n", name);
		DevMsg("%s failed to patch.\n", name);
	}
}

void Srcbox_InstallBSPHook()
{
	//if (!FindLog())
		//return false;

	/*filesystem = (IBaseFileSystem*)interfaceFactory(BASEFILESYSTEM_INTERFACE_VERSION, NULL);
	if (!filesystem)
	{
		Log({ 255, 0, 0, 255 }, PLUGIN_NAME "Failed to find filesystem\n");
		return false;
	}*/

	FindSignatures();
	LoadDetours();

	LoadPatch("CRC_MapFile", engine_crc_mapFile, 0xBC, "\x83\xf8\x01", "\x83\xf8\x02", 3);
	LoadPatch("MD5_MapFile", engine_md5_mapFile, 0xF6, "\x83\xf8\x01", "\x83\xf8\x02", 3);

	LoadPatch("MapLoadHelper_Init", engine_mapLoadHelper_init, 0x162, "\x83\xf8\x01", "\x83\xf8\x02", 3);
	LoadPatch("MapLoadHelper_InitFromMemory", engine_mapLoadHelper_initFromMemory, 0x17D, "\x83\xf8\x01", "\x83\xf8\x02", 3);

	LoadPatch("StaticPropMgr_UnserializeModels", engine_staticPropMgr_unserializeModels, 0xCF, "\x83\xF9\x03\x74\x28", "\x83\xF9\x05\x78\x28", 5);
	LoadPatch("DispInfo_LoadDisplacements", engine_dispInfo_loadDisplacements, 0x390, "\x41\x81\xfc\x00\x08\x00\x00", "\x41\x81\xfc\x00\x00\x08\x00", 7);

	//4585C00F8E2A2A2A2A48895C242A48897424
	//488BC4554881EC90000000

	//48895C242A48896C242A565741564881EC30080000
	//48895C242A488974242A48897C242A5541564157488DAC242A2A2A2A4881EC90080000

	//48895C242A574883EC308B05
	//48895C242A48896C242A564883EC30837C242A00

	//488954242A5556574156488DAC24
	//48895C242A8854242A5556574154415541564157488DAC24

	//48895C242A488974242A48897C242A554154415541564157488D6C24

	//LoadPatch("ReadHeaderFromBufferPastBaseHeader", materialSystem_readHeaderFromBufferPastBaseHeader, 0x13A, "\x75\x06", "\x75\x06", 2);

	//Log({ 0, 255, 0, 255 }, PLUGIN_NAME "Loaded plugin successfully\n");
	//return true;
}

void Srcbox_DeloadBSPHook()
{
	UnloadDetours();
}