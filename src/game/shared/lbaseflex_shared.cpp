#include "cbase.h"
#include "luamanager.h"
#include "luasrclib.h"
#include "lbaseflex_shared.h"
#include "lbaseentity_shared.h"
#include "lbaseplayer_shared.h"
#include "mathlib/lvector.h"
#include "lvphysics_interface.h"
#ifdef LUA_SDK
#include "srcbox/srcbox_ragdollflex.h"
#endif

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"
#include <model_types.h>

#ifdef CLIENT_DLL
static CClientSideEntityManager s_ClientSideEntityManager;
CClientSideEntityManager *g_pClientSideEntityManager = &s_ClientSideEntityManager;

CClientSideEntityManager::CClientSideEntityManager()
{
}

CClientSideEntityManager::~CClientSideEntityManager()
{
}

lua_CBaseFlex *CClientSideEntityManager::CreateClientSideEntity( const char *pszModelName, RenderGroup_t renderGroup )
{
    lua_CBaseFlex *pClientSideEntity = new lua_CBaseFlex();

    // Loaded (or found already loaded) as an ordinary client model. It used to be
    // registered as a dynamic model too, but nothing ever AddRef'd it: the engine
    // unloads unreferenced dynamic models a few seconds later (flushing the studiohdr
    // even though the entity still has it locked), and the entity's CStudioHdr then
    // pointed at freed memory -> crash in CStudioHdr::SequencesAvailable /
    // GetVirtualModel from C_BaseAnimating::Simulate. Client models loaded this way
    // stay until the level ends, which is when these entities are removed anyway.
    model_t *pModel = ( model_t * )engine->LoadModel( pszModelName, true );

    InitClientEntity( pClientSideEntity, pModel, renderGroup );

    if ( pClientSideEntity->InitializeAsClientEntity( pszModelName, renderGroup ) == false )
    {
        pClientSideEntity->Remove();
        DevWarning( "Failed to initialize clientside entity for model %s\n", pszModelName );
        return NULL;
    }

    return pClientSideEntity;
}

void CClientSideEntityManager::InitClientEntity( lua_CBaseFlex *pClientSideEntity, const model_t *pModel, RenderGroup_t renderGroup )
{
    pClientSideEntity->SetModelPointer( pModel );
    pClientSideEntity->m_RenderGroup = renderGroup;

    pClientSideEntity->m_nBody = 0;
    pClientSideEntity->m_nSkin = 0;
    pClientSideEntity->SetRenderMode( kRenderNormal );
    pClientSideEntity->m_nRenderFX = kRenderFxNone;
    pClientSideEntity->Interp_SetupMappings( pClientSideEntity->GetVarMapping() );
    pClientSideEntity->SetAbsOrigin( vec3_origin );
}
#endif

/*
** access functions (stack -> C)
*/

LUA_API lua_CBaseFlex *lua_tobaseflex( lua_State *L, int idx )
{
    CBaseHandle *hEntity =
        dynamic_cast< CBaseHandle * >( ( CBaseHandle * )lua_touserdata( L, idx ) );
    if ( hEntity == NULL )
        return NULL;
    IHandleEntity *pEntity = hEntity->Get();
    return dynamic_cast< lua_CBaseFlex * >( pEntity );
}

/*
** push functions (C -> stack)
*/
LUALIB_API lua_CBaseFlex *luaL_checkbaseflex( lua_State *L, int narg )
{
    lua_CBaseFlex *d = lua_tobaseflex( L, narg );

    if ( d == NULL ) /* avoid extra test when d is not 0 */
        luaL_argerror( L, narg, "CBaseFlex expected, got NULL entity" );

    return d;
}

LUA_REGISTRATION_INIT( CBaseFlex )

LUA_BINDING_BEGIN( CBaseFlex, GetFlexIdByName, "class", "Get flex ID by name." )
{
    lua_CBaseFlex *pEntity = LUA_BINDING_ARGUMENT( luaL_checkbaseflex, 1, "entity" );
    const char *szFlexName = LUA_BINDING_ARGUMENT( luaL_checkstring, 2, "flexName" );

    LocalFlexController_t iFlexController = pEntity->FindFlexController( szFlexName );

    lua_pushinteger( L, iFlexController );
    return 1;
}
LUA_BINDING_END( "integer", "Flex ID." )

// Srcbox: GMod's flex weights are 0..1 per flex controller ( what CBaseFlex networks ); the
// faceposer remaps its sliders to that. Ragdolls keep their own ( srcbox_ragdollflex.h ), and any
// other entity has none, so asking it isn't an error.
static mstudioflexcontroller_t *Srcbox_FlexController( lua_CBaseFlex *pEntity, LocalFlexController_t index )
{
    if ( !pEntity || index < 0 || index >= pEntity->GetNumFlexControllers() )
        return NULL;

    CStudioHdr *pStudioHdr = pEntity->GetModelPtr();
    return pStudioHdr ? pStudioHdr->pFlexcontroller( index ) : NULL;
}

LUA_BINDING_BEGIN( CBaseFlex, GetFlexWeight, "class", "Get flex weight ( 0..1 )." )
{
    int index = ( int )LUA_BINDING_ARGUMENT( luaL_checknumber, 2, "flexId" );
    lua_CBaseFlex *pFlex = lua_tobaseflex( L, 1 );

    if ( pFlex )
    {
        mstudioflexcontroller_t *pController = Srcbox_FlexController( pFlex, ( LocalFlexController_t )index );
        if ( !pController )
        {
            lua_pushnumber( L, 0 );
            return 1;
        }

        float flValue = pFlex->GetFlexWeight( ( LocalFlexController_t )index );
        if ( pController->max != pController->min )
            flValue = ( flValue - pController->min ) / ( pController->max - pController->min );

        lua_pushnumber( L, flValue );
        return 1;
    }

    float flWeight = 0.0f;
#ifdef LUA_SDK
    CBaseEntity *pEntity = lua_toentity( L, 1 );
    if ( pEntity )
        SrcboxRagdollFlex_GetWeight( pEntity, index, flWeight );
#endif

    lua_pushnumber( L, flWeight );
    return 1;
}
LUA_BINDING_END( "number", "Flex weight, 0..1." )

LUA_BINDING_BEGIN( CBaseFlex, SetFlexWeight, "class", "Set flex weight ( 0..1 )." )
{
    int index = ( int )LUA_BINDING_ARGUMENT( luaL_checknumber, 2, "flexId" );
    float flWeight = LUA_BINDING_ARGUMENT( luaL_checknumber, 3, "weight" );
    lua_CBaseFlex *pFlex = lua_tobaseflex( L, 1 );

    if ( pFlex )
    {
        mstudioflexcontroller_t *pController = Srcbox_FlexController( pFlex, ( LocalFlexController_t )index );
        if ( !pController )
            return 0;

        // CBaseFlex::SetFlexWeight takes the controller's own range
        flWeight = clamp( flWeight, 0.0f, 1.0f ) * ( pController->max - pController->min ) + pController->min;
        pFlex->SetFlexWeight( ( LocalFlexController_t )index, flWeight );
        return 0;
    }

#if defined( LUA_SDK ) && !defined( CLIENT_DLL )
    CBaseEntity *pEntity = lua_toentity( L, 1 );
    if ( pEntity )
        SrcboxRagdollFlex_SetWeight( pEntity, index, flWeight );
#endif

    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( CBaseFlex, HasFlexManipulator, "class", "Check if the entity has flex manipulator." )
{
    lua_CBaseFlex *pFlex = lua_tobaseflex( L, 1 );

    if ( !pFlex )
    {
        bool bPosed = false;
#ifdef LUA_SDK
        CBaseEntity *pEntity = lua_toentity( L, 1 );
        if ( pEntity )
            SrcboxRagdollFlex_IsPosed( pEntity, bPosed );
#endif
        lua_pushboolean( L, bPosed );
        return 1;
    }

    for ( LocalFlexController_t i = LocalFlexController_t( 0 ); i < pFlex->GetNumFlexControllers(); i++ )
    {
        if ( pFlex->GetFlexWeight( i ) != 0.0f )
        {
            lua_pushboolean( L, true );
            return 1;
        }
    }

    lua_pushboolean( L, false );
    return 1;
}
LUA_BINDING_END( "boolean", "True if the entity has flex manipulator." )

// Experiment; We only let CBaseEntity determine equality, which should be fine since they're pointers to the same entity. Disabled:
// LUA_BINDING_BEGIN( CBaseFlex, __eq, "class", "Equality operator." )
//{
//    lua_CBaseFlex *pEntityA = LUA_BINDING_ARGUMENT( luaL_checkbaseflex, 1, "entity" );
//    lua_CBaseFlex *pEntityB = LUA_BINDING_ARGUMENT( luaL_checkbaseflex, 2, "other" );
//
//    lua_pushboolean( L, pEntityA == pEntityB );
//    return 1;
//}
// LUA_BINDING_END( "boolean", "True if the entities are equal." )
//
// LUA_BINDING_BEGIN( CBaseFlex, __tostring, "class", "To string operator." )
//{
//    lua_CBaseFlex *pEntity = LUA_BINDING_ARGUMENT( lua_tobaseflex, 1, "entity" );
//
//    if ( pEntity == NULL )
//        lua_pushstring( L, "NULL" );
//    else
//        lua_pushfstring( L, "CBaseFlex: %d \"%s\"", pEntity->entindex(), pEntity->GetClassname() );
//
//    return 1;
//}
// LUA_BINDING_END( "string", "String representation of the entity." )

LUA_REGISTRATION_INIT( Entities )

#ifdef CLIENT_DLL
LUA_BINDING_BEGIN( Entities, CreateClientEntity, "library", "Create a clientside entity with the specified model.", "client" )
{
    const char *pszModelName = LUA_BINDING_ARGUMENT( luaL_checkstring, 1, "model" );
    int renderGroup = ( int )LUA_BINDING_ARGUMENT_WITH_DEFAULT( luaL_optnumber, 2, RENDER_GROUP_OTHER, "renderGroup" );

    if ( pszModelName == NULL )
    {
        luaL_error( L, "ClientsideModel: Invalid model name" );
        return 0;
    }

    lua_CBaseFlex *pEntity = g_pClientSideEntityManager->CreateClientSideEntity( pszModelName, ( RenderGroup_t )renderGroup );
    CBaseFlex::PushLuaInstanceSafe( L, pEntity );

    return 1;
}
LUA_BINDING_END( "Entity", "Clientside model entity." )
#endif
/*
** Open CBaseFlex object
*/
LUALIB_API int luaopen_CBaseFlex_shared( lua_State *L )
{
    LUA_PUSH_NEW_METATABLE( L, LUA_CBASEFLEXLIBNAME );

    LUA_REGISTRATION_COMMIT( CBaseFlex );

    LUA_REGISTRATION_COMMIT_LIBRARY( Entities );
    return 1;
}
