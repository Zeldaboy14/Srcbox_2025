#include "cbase.h"
#include "SteamCommon.h"
#ifdef CLIENT_DLL
#include "clientsteamcontext.h"
#endif
#include "filesystem.h"
#include "fmtstr.h"

#include "content_mounter.h"

#include "ienginevgui.h"
#include <vgui/ISurface.h>
#include <vgui/IVGui.h>
#include <vgui/ILocalize.h>
#include "icommandline.h"
#include "tier3/tier3.h"

void AddHL1(const char* path)
{
	filesystem->AddSearchPath(CFmtStr("%s/hl1/hl1_pak_dir.vpk", path), "GAME", PATH_ADD_TO_HEAD);
	filesystem->AddSearchPath(CFmtStr("%s/hl1/hl1_pak_dir.vpk", path), "GAME", PATH_ADD_TO_HEAD);
	filesystem->AddSearchPath(CFmtStr("%s/hl1", path), "GAME", PATH_ADD_TO_TAIL);
	g_pVGuiLocalize->AddFile("resource/hl1_%language%.txt");
}

void AddHL2(const char* path)
{
	filesystem->AddSearchPath(CFmtStr("%s/hl2/hl2_misc_dir.vpk", path), "GAME", PATH_ADD_TO_HEAD);
	filesystem->AddSearchPath(CFmtStr("%s/hl2/hl2_sound_misc_dir.vpk", path), "GAME", PATH_ADD_TO_HEAD);
	filesystem->AddSearchPath(CFmtStr("%s/hl2/hl2_textures_dir.vpk", path), "GAME", PATH_ADD_TO_HEAD);
	filesystem->AddSearchPath(CFmtStr("%s/hl2/hl2_pak_dir.vpk", path), "GAME", PATH_ADD_TO_HEAD);
	filesystem->AddSearchPath(CFmtStr("%s/hl2/hl2_sound_vo_english_dir.vpk", path), "GAME", PATH_ADD_TO_HEAD);
	filesystem->AddSearchPath(CFmtStr("%s/hl2", path), "GAME", PATH_ADD_TO_HEAD);
	filesystem->AddSearchPath(CFmtStr("%s/hl2/maps", path), "GAME", PATH_ADD_TO_HEAD);
	filesystem->AddSearchPath(CFmtStr("%s/hl2_complete/hl2_complete_misc_dir.vpk", path), "GAME", PATH_ADD_TO_HEAD);
	filesystem->AddSearchPath(CFmtStr("%s/hl2_complete", path), "GAME", PATH_ADD_TO_HEAD);
	filesystem->AddSearchPath(CFmtStr("%s/episodic/ep1_pak_dir.vpk", path), "GAME", PATH_ADD_TO_HEAD);
	filesystem->AddSearchPath(CFmtStr("%s/episodic", path), "GAME", PATH_ADD_TO_HEAD);
	filesystem->AddSearchPath(CFmtStr("%s/ep2/ep2_pak_dir.vpk", path), "GAME", PATH_ADD_TO_HEAD);
	filesystem->AddSearchPath(CFmtStr("%s/ep2", path), "GAME", PATH_ADD_TO_TAIL);
	g_pVGuiLocalize->AddFile("resource/hl2_%language%.txt");
}

void AddCSS(const char* path)
{
	filesystem->AddSearchPath(CFmtStr("%s/cstrike/cstrike_pak_dir.vpk", path), "GAME", PATH_ADD_TO_HEAD);
	filesystem->AddSearchPath(CFmtStr("%s/cstrike", path), "GAME", PATH_ADD_TO_TAIL);
	g_pVGuiLocalize->AddFile("resource/cstrike_%language%.txt");
}

void AddHL2MP(const char* path)
{
	filesystem->AddSearchPath(CFmtStr("%s/hl2mp/hl2mp_pak.vpk", path), "GAME", PATH_ADD_TO_HEAD);
	filesystem->AddSearchPath(CFmtStr("%s/hl2mp/hl2mp_english.vpk", path), "GAME", PATH_ADD_TO_HEAD);
	filesystem->AddSearchPath(CFmtStr("%s/hl2mp", path), "GAME", PATH_ADD_TO_TAIL);
	g_pVGuiLocalize->AddFile("resource/hl2mp_%language%.txt");
}

void AddLostCoast(const char* path)
{
	filesystem->AddSearchPath(CFmtStr("%s/lostcoast/lostcoast_pak_dir.vpk", path), "GAME", PATH_ADD_TO_HEAD);
	filesystem->AddSearchPath(CFmtStr("%s/lostcoast", path), "GAME", PATH_ADD_TO_TAIL);
	g_pVGuiLocalize->AddFile("resource/lostcoast_%language%.txt");
}

void AddTF2(const char* path)
{
	filesystem->AddSearchPath(CFmtStr("%s/tf/tf2_misc_dir.vpk", path), "GAME", PATH_ADD_TO_HEAD);
	filesystem->AddSearchPath(CFmtStr("%s/tf/tf2_sound_misc_dir.vpk", path), "GAME", PATH_ADD_TO_HEAD);
	filesystem->AddSearchPath(CFmtStr("%s/tf/tf2_sound_vo_english_dir.vpk", path), "GAME", PATH_ADD_TO_HEAD);
	filesystem->AddSearchPath(CFmtStr("%s/tf/tf2_textures_dir.vpk", path), "GAME", PATH_ADD_TO_TAIL);
	filesystem->AddSearchPath(CFmtStr("%s/tf", path), "GAME", PATH_ADD_TO_TAIL);
	g_pVGuiLocalize->AddFile("resource/tf_%language%.txt");
}

void AddPortal(const char* path)
{
	filesystem->AddSearchPath(CFmtStr("%s/portal/portal_pak_dir.vpk", path), "GAME", PATH_ADD_TO_HEAD);
	filesystem->AddSearchPath(CFmtStr("%s/portal", path), "GAME", PATH_ADD_TO_TAIL);
	g_pVGuiLocalize->AddFile("resource/portal_%language%.txt");
}

void AddL4D(const char* path)
{
	filesystem->AddSearchPath(CFmtStr("%s/left4dead/pak01_dir.vpk", path), "GAME", PATH_ADD_TO_HEAD);
	filesystem->AddSearchPath(CFmtStr("%s/left4dead", path), "GAME", PATH_ADD_TO_HEAD);
	filesystem->AddSearchPath(CFmtStr("%s/left4dead_dlc3/pak01_dir.vpk", path), "GAME", PATH_ADD_TO_TAIL);
	g_pVGuiLocalize->AddFile("resource/left4dead_%language%.txt");
}

void AddL4D2(const char* path)
{
	filesystem->AddSearchPath(CFmtStr("%s/left4dead2/pak01_dir.vpk", path), "GAME", PATH_ADD_TO_HEAD);
	filesystem->AddSearchPath(CFmtStr("%s/left4dead2", path), "GAME", PATH_ADD_TO_HEAD);
	filesystem->AddSearchPath(CFmtStr("%s/left4dead2_dlc1/pak01_dir.vpk", path), "GAME", PATH_ADD_TO_HEAD);
	filesystem->AddSearchPath(CFmtStr("%s/left4dead2_dlc1", path), "GAME", PATH_ADD_TO_HEAD);
	filesystem->AddSearchPath(CFmtStr("%s/left4dead2_dlc2/pak01_dir.vpk", path), "GAME", PATH_ADD_TO_HEAD);
	filesystem->AddSearchPath(CFmtStr("%s/left4dead2_dlc2", path), "GAME", PATH_ADD_TO_HEAD);
	filesystem->AddSearchPath(CFmtStr("%s/left4dead2_dlc3/pak01_dir.vpk", path), "GAME", PATH_ADD_TO_HEAD);
	filesystem->AddSearchPath(CFmtStr("%s/left4dead2_dlc3", path), "GAME", PATH_ADD_TO_TAIL);
}

void AddPortal2(const char* path)
{
	filesystem->AddSearchPath(CFmtStr("%s/portal2/pak01_dir.vpk", path), "GAME", PATH_ADD_TO_HEAD);
	filesystem->AddSearchPath(CFmtStr("%s/portal2", path), "GAME", PATH_ADD_TO_HEAD);
	filesystem->AddSearchPath(CFmtStr("%s/portal2_dlc1/pak01_dir.vpk", path), "GAME", PATH_ADD_TO_HEAD);
	filesystem->AddSearchPath(CFmtStr("%s/portal2_dlc1", path), "GAME", PATH_ADD_TO_HEAD);
	filesystem->AddSearchPath(CFmtStr("%s/portal2_dlc2/pak01_dir.vpk", path), "GAME", PATH_ADD_TO_HEAD);
	filesystem->AddSearchPath(CFmtStr("%s/portal2_dlc2", path), "GAME", PATH_ADD_TO_TAIL);
	g_pVGuiLocalize->AddFile("resource/portal2_%language%.txt");
}

void AddCSGO(const char* path)
{
	filesystem->AddSearchPath(CFmtStr("%s/csgo/pak01_dir.vpk", path), "GAME", PATH_ADD_TO_HEAD);
	filesystem->AddSearchPath(CFmtStr("%s/csgo/pakxv_lowviolence_dir.vpk", path), "GAME", PATH_ADD_TO_HEAD);
	filesystem->AddSearchPath(CFmtStr("%s/csgo/pakxv_perfectworld_dir.vpk", path), "GAME", PATH_ADD_TO_HEAD);
	filesystem->AddSearchPath(CFmtStr("%s/csgo", path), "GAME|MOD", PATH_ADD_TO_TAIL);
}

// From here, mount the mods!

void AddFOF(const char* path)
{
	filesystem->AddSearchPath(CFmtStr("%s/fof/fof_dir.vpk", path), "GAME", PATH_ADD_TO_HEAD);
	filesystem->AddSearchPath(CFmtStr("%s/fof/fof_new_materials_dir.vpk", path), "GAME", PATH_ADD_TO_HEAD);
	filesystem->AddSearchPath(CFmtStr("%s/fof/fof2_dir.vpk", path), "GAME", PATH_ADD_TO_HEAD);
	filesystem->AddSearchPath(CFmtStr("%s/fof", path), "GAME", PATH_ADD_TO_HEAD);
}


void AddTSP(const char* path)
{
	filesystem->AddSearchPath(CFmtStr("%s/thestanleyparable", path), "GAME", PATH_ADD_TO_HEAD);
}

// Fortress Connected

/*void AddFC(const char* path)
{
	filesystem->AddSearchPath(CFmtStr("%s/fc_tfsdk/vpks/fc_tfsdk_misc_dir.vpk", path), "GAME", PATH_ADD_TO_HEAD);
	filesystem->AddSearchPath(CFmtStr("%s/fc_tfsdk/vpks/fc_tfsdk_sound_dir.vpk", path), "GAME", PATH_ADD_TO_HEAD);
	filesystem->AddSearchPath(CFmtStr("%s/fc_tfsdk/vpks/fc_tfsdk_textures_dir.vpk", path), "GAME", PATH_ADD_TO_HEAD);
}*/

// Base 2013 MP SDK

#ifdef HL2MP
void AddSDK2013MP(const char* path)
{
	filesystem->AddSearchPath(CFmtStr("%s/hl2_complete/hl2_complete_textures.vpk", path), "GAME", PATH_ADD_TO_HEAD);
	filesystem->AddSearchPath(CFmtStr("%s/hl2_complete/hl2_complete_sound_vo_english.vpk", path), "GAME", PATH_ADD_TO_HEAD);
	filesystem->AddSearchPath(CFmtStr("%s/hl2_complete/hl2_complete_sound_misc.vpk", path), "GAME", PATH_ADD_TO_HEAD);
	filesystem->AddSearchPath(CFmtStr("%s/hl2_complete/hl2_complete_misc.vpk", path), "GAME", PATH_ADD_TO_HEAD);
	filesystem->AddSearchPath(CFmtStr("%s/hl2_complete", path), "GAME", PATH_ADD_TO_HEAD);

	filesystem->AddSearchPath(CFmtStr("%s/hl2/hl2_textures.vpk", path), "GAME", PATH_ADD_TO_HEAD);
	filesystem->AddSearchPath(CFmtStr("%s/hl2/hl2_sound_vo_english.vpk", path), "GAME", PATH_ADD_TO_HEAD);
	filesystem->AddSearchPath(CFmtStr("%s/hl2/hl2_sound_misc.vpk", path), "GAME", PATH_ADD_TO_HEAD);
	filesystem->AddSearchPath(CFmtStr("%s/hl2/hl2_misc.vpk", path), "GAME", PATH_ADD_TO_HEAD);
	filesystem->AddSearchPath(CFmtStr("%s/hl2", path), "GAME", PATH_ADD_TO_HEAD);

	filesystem->AddSearchPath(CFmtStr("%s/hl2mp/hl2mp_pak_dir.vpk", path), "GAME", PATH_ADD_TO_HEAD);
	filesystem->AddSearchPath(CFmtStr("%s/hl2mp", path), "GAME", PATH_ADD_TO_HEAD);
	filesystem->AddSearchPath(CFmtStr("%s/platform/platform_misc.vpk", path), "GAME", PATH_ADD_TO_TAIL);
}
#endif

void MountExtraContent()
{
	KeyValuesAD mountdepots("MountDepots");
	mountdepots->LoadFromFile(filesystem, "cfg/mountdepots.txt");

	if (steamapicontext->SteamApps()->BIsAppInstalled(280) && (mountdepots->GetBool("hl1")))
	{
		char hl1Path[MAX_PATH];
		steamapicontext->SteamApps()->GetAppInstallDir(280, hl1Path, sizeof(hl1Path));
		AddHL1(hl1Path);
	}

	if (steamapicontext->SteamApps()->BIsAppInstalled(220) && (mountdepots->GetBool("hl2")))
	{
		char hl2Path[MAX_PATH];
		steamapicontext->SteamApps()->GetAppInstallDir(220, hl2Path, sizeof(hl2Path));
		AddHL2(hl2Path);
	}

	if (steamapicontext->SteamApps()->BIsAppInstalled(240) && mountdepots->GetBool("cstrike"))
	{
		char cssPath[MAX_PATH];
		steamapicontext->SteamApps()->GetAppInstallDir(240, cssPath, sizeof(cssPath));
		AddCSS(cssPath);
	}

#
	if (steamapicontext->SteamApps()->BIsAppInstalled(320) && mountdepots->GetBool("hl2mp"))
	{
		char hl2mpPath[MAX_PATH];
		steamapicontext->SteamApps()->GetAppInstallDir(320, hl2mpPath, sizeof(hl2mpPath));
		AddHL2(hl2mpPath);
		AddHL2MP(hl2mpPath);
	}

	if (steamapicontext->SteamApps()->BIsAppInstalled(340) && mountdepots->GetBool("lostcoast"))
	{
		char lostCoastPath[MAX_PATH];
		steamapicontext->SteamApps()->GetAppInstallDir(340, lostCoastPath, sizeof(lostCoastPath));
		AddLostCoast(lostCoastPath);
	}

	if (steamapicontext->SteamApps()->BIsAppInstalled(440) && mountdepots->GetBool("tf"))
	{
		char tf2Path[MAX_PATH];
		steamapicontext->SteamApps()->GetAppInstallDir(440, tf2Path, sizeof(tf2Path));
		AddTF2(tf2Path);
	}

	if (steamapicontext->SteamApps()->BIsAppInstalled(400) && mountdepots->GetBool("portal"))
	{
		char portalPath[MAX_PATH];
		steamapicontext->SteamApps()->GetAppInstallDir(400, portalPath, sizeof(portalPath));
		AddPortal(portalPath);
	}

	if (steamapicontext->SteamApps()->BIsAppInstalled(500) && mountdepots->GetBool("left4dead"))
	{
		char l4dPath[MAX_PATH];
		steamapicontext->SteamApps()->GetAppInstallDir(500, l4dPath, sizeof(l4dPath));
		AddL4D(l4dPath);
	}

	if (steamapicontext->SteamApps()->BIsAppInstalled(550) && mountdepots->GetBool("left4dead2"))
	{
		char l4d2Path[MAX_PATH];
		steamapicontext->SteamApps()->GetAppInstallDir(550, l4d2Path, sizeof(l4d2Path));
		AddL4D2(l4d2Path);
	}

	if (steamapicontext->SteamApps()->BIsAppInstalled(620) && mountdepots->GetBool("portal2"))
	{
		char portal2Path[MAX_PATH];
		steamapicontext->SteamApps()->GetAppInstallDir(620, portal2Path, sizeof(portal2Path));
		AddPortal2(portal2Path);
	}

	if (steamapicontext->SteamApps()->BIsAppInstalled(4465480) && mountdepots->GetBool("csgo"))
	{
		char csgoPath[MAX_PATH];
		steamapicontext->SteamApps()->GetAppInstallDir(4465480, csgoPath, sizeof(csgoPath));
		AddCSGO(csgoPath);
	}

	// Mods

	if (steamapicontext->SteamApps()->BIsAppInstalled(265630) && mountdepots->GetBool("fof"))
	{
		char fofPath[MAX_PATH];
		steamapicontext->SteamApps()->GetAppInstallDir(265630, fofPath, sizeof(fofPath));
		AddFOF(fofPath);
	}

	if (steamapicontext->SteamApps()->BIsAppInstalled(221910) && mountdepots->GetBool("tsp"))
	{
		char tspPath[MAX_PATH];
		steamapicontext->SteamApps()->GetAppInstallDir(221910, tspPath, sizeof(tspPath));
		AddTSP(tspPath);
	}

	/*if (steamapicontext->SteamApps()->BIsAppInstalled(3561600) && mountdepots->GetBool("fc"))
	{
		char fcPath[MAX_PATH];
		steamapicontext->SteamApps()->GetAppInstallDir(3561600, fcPath, sizeof(fcPath));
		AddFC(fcPath);
	}*/

#ifdef HL2MP

	// Source SDK 2013 MP
	// We have to force this when TF isn't defined, else whatever was loaded last will load.

	if (steamapicontext->SteamApps()->BIsAppInstalled(243750))
	{
		char sdk2013mpPath[MAX_PATH];
		steamapicontext->SteamApps()->GetAppInstallDir(243750, sdk2013mpPath, sizeof(sdk2013mpPath));
		AddSDK2013MP(sdk2013mpPath);
	}

#else

	// Team Fortress 2
	// We have to force this when TF is defined, else we load the above.

	if (steamapicontext->SteamApps()->BIsAppInstalled(440))
	{
		char tf2Path[MAX_PATH];
		steamapicontext->SteamApps()->GetAppInstallDir(440, tf2Path, sizeof(tf2Path));
		AddTF2(tf2Path);
	}

#endif
	filesystem->AddSearchPath(CFmtStr("%s", CommandLine()->ParmValue("-game")), "GAME", PATH_ADD_TO_HEAD);
	filesystem->AddSearchPath(CFmtStr("%s", CommandLine()->ParmValue("-game")), "MOD", PATH_ADD_TO_HEAD);
	filesystem->AddSearchPath(CFmtStr("%s", CommandLine()->ParmValue("-game")), "MOD_WRITE", PATH_ADD_TO_HEAD);
	filesystem->AddSearchPath(CFmtStr("%s", CommandLine()->ParmValue("-game")), "DEFAULT_WRITE_PATH", PATH_ADD_TO_HEAD);
	filesystem->AddSearchPath(CFmtStr("%s/bin", CommandLine()->ParmValue("-game")), "GAMEBIN", PATH_ADD_TO_HEAD);
}

void MountGamepacks()
{
	if (!filesystem)
	{
		Warning("[Mount System] Failed to access filesystem interface!\n");
		return;
	}

	// Get the full absolute path to the directory containing gameinfo.txt
	char szGameDir[MAX_PATH];
	if (!filesystem->RelativePathToFullPath(".", "GAME", szGameDir, sizeof(szGameDir)))
	{
		Warning("[Mount System] Failed to resolve base game directory!\n");
		return;
	}

	// Strip trailing slashes/backslashes
	V_StripTrailingSlash(szGameDir);

	// Go one level up from the mod folder to reach the root/parent directory
	V_StripLastDir(szGameDir, sizeof(szGameDir));

	// Append /gamepacks to form the full path
	char szGamepacksDir[MAX_PATH];
	V_snprintf(szGamepacksDir, sizeof(szGamepacksDir), "%s/gamepacks", szGameDir);
	V_FixSlashes(szGamepacksDir);

	Msg("[Mount System] Scanning directory: %s\n", szGamepacksDir);

	// Search wildcard for folders inside gamepacks
	char szSearchPath[MAX_PATH];
	V_snprintf(szSearchPath, sizeof(szSearchPath), "%s/*", szGamepacksDir);

	int nMountedFolders = 0;
	int nMountedVPKs = 0;

	FileFindHandle_t findHandle;
	const char* pFileName = filesystem->FindFirstEx(szSearchPath, NULL, &findHandle);

	while (pFileName)
	{
		// Skip current directory '.', parent '..', and non-directory files
		if (pFileName[0] != '.' && filesystem->FindIsDirectory(findHandle))
		{
			char szPackPath[MAX_PATH];
			V_snprintf(szPackPath, sizeof(szPackPath), "%s/%s", szGamepacksDir, pFileName);

			// Mount loose files in the folder directly using absolute path
			filesystem->AddSearchPath(szPackPath, "GAME", PATH_ADD_TO_TAIL);

			Msg("[Mount System] Mounted Folder: gamepacks/%s\n", pFileName);
			nMountedFolders++;

			// Scan and mount any VPKs inside this gamepack folder
			char szVpkSearch[MAX_PATH];
			V_snprintf(szVpkSearch, sizeof(szVpkSearch), "%s/*.vpk", szPackPath);

			FileFindHandle_t vpkHandle;
			const char* pVpkName = filesystem->FindFirstEx(szVpkSearch, NULL, &vpkHandle);
			while (pVpkName)
			{
				// Skip VPK chunk files (e.g. pak01_001.vpk); only mount dir or standalone VPKs
				if (!V_stristr(pVpkName, "_0") && !V_stristr(pVpkName, "_1"))
				{
					char szFullVpkPath[MAX_PATH];
					V_snprintf(szFullVpkPath, sizeof(szFullVpkPath), "%s/%s", szPackPath, pVpkName);
					filesystem->AddSearchPath(szFullVpkPath, "GAME", PATH_ADD_TO_TAIL);

					Msg("[Mount System] Mounted VPK: %s\n", pVpkName);
					nMountedVPKs++;
				}
				pVpkName = filesystem->FindNext(vpkHandle);
			}
			filesystem->FindClose(vpkHandle);
		}
		pFileName = filesystem->FindNext(findHandle);
	}
	filesystem->FindClose(findHandle);

	Msg("[Mount System] Auto-mounting complete (%d folders, %d VPKs mounted).\n", nMountedFolders, nMountedVPKs);
}