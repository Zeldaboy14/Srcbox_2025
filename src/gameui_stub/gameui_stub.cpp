//========= Resonance Team ============//
//
// Make the engine think we're loading 
// GameUI.dll, when we're really just 
// grabbing from the client.
//
//=====================================//

#include "IGameUI.h"
#include "IGameConsole.h"
#include "IVGuiModule.h"
#include "IVGuiModuleLoader.h"

//#include <windows.h> // For LoadLibrary

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

IGameUI *g_pGameUI = NULL;

// Add this near the top, after your includes
//static CSysModule* m = nullptr;

// Function to load the legacy DLL
void LoadLegacyGameUIDLL()
{
	static CSysModule* m = Sys_LoadModule("GameUI_Legacy.dll");
}

static CreateInterfaceFn ClientFactory()
{
    // Ensure legacy DLL is loaded for its exports
    LoadLegacyGameUIDLL();

	static CSysModule* m = Sys_LoadModule("client", SYS_NOLOAD);
	static CSysModule* n = Sys_LoadModule("GameUI_Legacy");
	static CreateInterfaceFn fn = Sys_GetFactory(m);
	static CreateInterfaceFn fm = Sys_GetFactory(n);
	return fn;
}

#define INTERFACE_REDIRECT_TO_CLIENT(inter) \
static void* __##inter##_interface() { return ClientFactory()(inter, nullptr); } \
static InterfaceReg __g_Creat##inter##_reg(__##inter##_interface, inter);

INTERFACE_REDIRECT_TO_CLIENT(GAMEUI_INTERFACE_VERSION)
INTERFACE_REDIRECT_TO_CLIENT(GAMECONSOLE_INTERFACE_VERSION)
INTERFACE_REDIRECT_TO_CLIENT(VGUIMODULE_INTERFACE_VERSION)
INTERFACE_REDIRECT_TO_CLIENT(VGUIMODULELOADER_INTERFACE_VERSION)