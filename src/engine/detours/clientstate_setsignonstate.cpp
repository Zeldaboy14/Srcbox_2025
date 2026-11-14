#include "cbase.h"
#include "../engine_patches.h"
#include "inetchannel.h"
#include <netadr.h>
#include <psapi.h>
#include <util/networkmanager.h>

// CClientState::SetSignonState (__thiscall on x86, see EP_EDX)
using Function_ClientState_SetSignonState_t = bool( __fastcall* )( void* pThis EP_EDX, int state, int count );

#ifdef PLATFORM_64BITS
/*
    ```
    48 89 5C 24 18
    55
    56
    57
    48 8D 6C 24 90
    48 81 EC 70 01 00 00
    41 8B F0
    8B FA
    48 8B D9
    E8 ? ? ? ?
    84 C0
    75 0C
    E8 ? ? ? ?
    32 C0
    E9 ? ? ? ?
    8B D7
    ```
*/
const char* CLIENTSTATE_SETSIGNONSTATE_SIG =
    "\x48\x89\x5C\x24\x18"
    "\x55"
    "\x56"
    "\x57"
    "\x48\x8D\x6C\x24\x90"
    "\x48\x81\xEC\x70\x01\x00\x00"
    "\x41\x8B\xF0"
    "\x8B\xFA"
    "\x48\x8B\xD9"
    "\xE8\x00\x00\x00\x00"
    "\x84\xC0"
    "\x75\x0C"
    "\xE8\x00\x00\x00\x00"
    "\x32\xC0"
    "\xE9\x00\x00\x00\x00"
    "\x8B\xD7";

const char* CLIENTSTATE_SETSIGNONSTATE_MASK =
    "xxxxx"
    "x"
    "x"
    "x"
    "xxxxx"
    "xxxxxxx"
    "xxx"
    "xx"
    "xxx"
    "x????"
    "xx"
    "xx"
    "x????"
    "xx"
    "x????"
    "xx";

#else

// x86 engine.dll (bin/engine.dll), CClientState::SetSignonState at 0x100B7DF0 in that build:
//   push ebp; mov ebp,esp; sub esp,128h; push esi; push edi; push [ebp+0Ch]; mov edi,[ebp+8]
//   mov esi,ecx; push edi; call CBaseClientState::SetSignonState; test al,al; jnz; call CL_Retry
//   pop edi; xor al,al
const char* CLIENTSTATE_SETSIGNONSTATE_SIG =
    "\x55\x8B\xEC\x81\xEC\x28\x01\x00\x00\x56\x57\xFF\x75\x0C\x8B\x7D"
    "\x08\x8B\xF1\x57\xE8\x00\x00\x00\x00\x84\xC0\x75\x0F\xE8\x00\x00"
    "\x00\x00\x5F\x32\xC0";

const char* CLIENTSTATE_SETSIGNONSTATE_MASK =
    "xxxxxxxxxxxxxxxx"
    "xxxxx????xxxxx??"
    "??xxx";

#endif

static Function_ClientState_SetSignonState_t OriginalClientStateSetSignOnState = nullptr;

#pragma warning( disable : 4189 )  // Disable warning about unused variable

bool __fastcall DetourClientStateSetSignOnState(
    void* thisClientState EP_EDX,
    int state,
    int count )
{
    bool result = OriginalClientStateSetSignOnState( thisClientState EP_EDX_ARG, state, count );

    // CBaseClientState starts with three vtable pointers; the fields below follow them on
    // both builds (x64: netchannel 0x20, signon state 0x14C; x86: 0x10, 0x130 - checked
    // against CBaseClientState::SetSignonState in each engine.dll)
    BEGIN_MEMORY_LAYOUT( ClientState, thisClientState );

    SKIP_UNKNOWN_TO( ClientState, 3 * sizeof( void* ) );

    DEFINE_INT( ClientState, socket );
    DEFINE_PTR( ClientState, INetChannel*, netChannel );
    DEFINE_INT( ClientState, challengeNumber );
    DEFINE_DOUBLE( ClientState, connectTime );
    DEFINE_INT( ClientState, retryNumber );
    DEFINE_STRING( ClientState, retryAddress, MAX_PATH );
    DEFINE_TYPE( ClientState, CUtlString, retrySourceTag );
    DEFINE_INT( ClientState, retryChallenge );  // Matches retryChallenge in DetourClientConnect
    DEFINE_INT( ClientState, signOnState );

    END_MEMORY_LAYOUT( ClientState );

    // TODO: This is only for validating we have correctly deduced the memory layout. Remove this later.
    if ( netChannel )
    {
        // Ensure we've got a pointer to the real net channel
        Assert( netChannel->GetProtocolVersion() == 24 );

        // Make sure we really have the correct setup
        Assert( strcmp( retrySourceTag->Get(), "listenserver" ) == 0 );

        if ( signOnState == SIGN_ON_STATE_CONNECTED )
        {
#ifdef LUA_SDK
            // Registers this connection with the Lua network manager. Without it, net/Networks messages
            // (e.g. GMod's properties: Remove, Ignite, ...) were never sent in either direction.
            g_pNetworkManager->BindClient( netChannel );
#else
            //g_pNetworkManager->BindClient( netChannel );
#endif
        }
    }

    return result;
}

#pragma warning( default : 4189 )  // Enable warning about unused variable

void ApplyClientStateDetours()
{
    HMODULE hModule = GetModuleHandle( "engine.dll" );
    DWORD_PTR functionAddress = FindPattern(
        hModule,
        CLIENTSTATE_SETSIGNONSTATE_SIG,
        CLIENTSTATE_SETSIGNONSTATE_MASK );
    DWORD_PTR baseAddress = GetModuleBaseAddress( hModule );

    // DWORD_PTR expectedAsInIda = baseAddress + 0xA3DF0;
    // bool same = ( functionAddress ) == ( expectedAsInIda );

    if ( !functionAddress )
    {
        DevWarning( "Failed to find ClientState::SetSignonState function address!\n" );
        return;
    }

    if ( MH_CreateHook(
             ( LPVOID )functionAddress,
             &DetourClientStateSetSignOnState,
             reinterpret_cast< LPVOID* >( &OriginalClientStateSetSignOnState ) ) != MH_OK )
    {
        DevWarning( "Failed to hook ClientConnect.\n" );
        //Assert( 0 );
    }
    else
    {
        MH_EnableHook( ( LPVOID )functionAddress );
    }
}
