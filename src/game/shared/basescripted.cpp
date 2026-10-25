#include "cbase.h"
#include "basescripted.h"
#include "luamanager.h"
#ifdef CLIENT_DLL
#include "lc_baseanimating.h"
#else
#include "lbaseanimating.h"
#endif
#include "lbaseentity_shared.h"
#include "lvphysics_interface.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

;  // clang-format off

IMPLEMENT_NETWORKCLASS_ALIASED(BaseScripted, DT_BaseScripted)

BEGIN_NETWORK_TABLE(CBaseScripted, DT_BaseScripted)
#ifdef CLIENT_DLL
RecvPropString(RECVINFO(m_iScriptedClassname)),
#else
SendPropString(SENDINFO(m_iScriptedClassname)),
#endif
END_NETWORK_TABLE()

BEGIN_PREDICTION_DATA(CBaseScripted)
END_PREDICTION_DATA()

;  // clang-format on

#ifdef CLIENT_DLL
static C_BaseEntity* CCBaseScriptedFactory(void)
{
    return static_cast<C_BaseEntity*>(new CBaseScripted);
};
#endif

#ifndef CLIENT_DLL
static CUtlDict< CEntityFactory< CBaseScripted >*, unsigned short >
m_EntityFactoryDatabase;
#endif

// Experiment; Commented to instead go ask Lua when we encounter a non-C-registered entities.
// void RegisterScriptedEntity( const char *className )
//{
// #ifdef CLIENT_DLL
//    if ( GetClassMap().FindFactory( className ) )
//    {
//        return;
//    }
//
//    GetClassMap().Add( className, "CBaseScripted", sizeof( CBaseScripted ), &CCBaseScriptedFactory, true );
// #else
//    if ( EntityFactoryDictionary()->FindFactory( className ) )
//    {
//        return;
//    }
//
//    unsigned short lookup = m_EntityFactoryDatabase.Find( className );
//    if ( lookup != m_EntityFactoryDatabase.InvalidIndex() )
//    {
//        return;
//    }
//
//    CEntityFactory< CBaseScripted > *pFactory =
//        new CEntityFactory< CBaseScripted >( className );
//
//    lookup = m_EntityFactoryDatabase.Insert( className, pFactory );
//    Assert( lookup != m_EntityFactoryDatabase.InvalidIndex() );
// #endif
//}

void ResetEntityFactoryDatabase(void)
{
#ifdef CLIENT_DLL
#ifdef LUA_SDK
    GetClassMap().RemoveAllScripted();
#endif
#else
    for (int i = m_EntityFactoryDatabase.First();
        i != m_EntityFactoryDatabase.InvalidIndex();
        i = m_EntityFactoryDatabase.Next(i))
    {
        delete m_EntityFactoryDatabase[i];
    }
    m_EntityFactoryDatabase.RemoveAll();
#endif
}

CBaseScripted::CBaseScripted(void)
{
#ifdef LUA_SDK
    // UNDONE: We're done in CBaseEntity
    m_nTableReference = LUA_NOREF;
#endif
}

CBaseScripted::~CBaseScripted(void)
{
    // Andrew; This is actually done in CBaseEntity. I'm doing it here because
    // this is the class that initialized the reference.
#ifdef LUA_SDK
    if (L)
        lua_unref(L, m_nTableReference);

    // CBaseEntity's destructor unrefs it again: a second unref of the same ref corrupts
    // Lua's free list, and later entities then share one ref table (one entity's
    // fields and NetworkVar accessors showing up on another).
    m_nTableReference = LUA_NOREF;
#endif
}

void CBaseScripted::LoadScriptedEntity(void)
{
    lua_getglobal(L, LUA_SCRIPTEDENTITIESLIBNAME);
    if (lua_istable(L, -1))
    {
        lua_getfield(L, -1, "GetStored");
        if (lua_isfunction(L, -1))
        {
            lua_remove(L, -2);
            lua_pushstring(L, GetClassname());
            luasrc_pcall(L, 1, 1);

            Assert(lua_istable(L, -1));  // The entity should exist, since we just created it
        }
        else
        {
            lua_pop(L, 2);
        }
    }
    else
    {
        lua_pop(L, 1);
    }
}

void CBaseScripted::InitScriptedEntity(void)
{
#ifdef LUA_SDK
#if 0
#ifndef CLIENT_DLL
    // Let the instance reinitialize itself for the client.
    if (lua_isrefvalid(L, m_nTableReference))
        return;
#endif
#endif

    SetThink(&CBaseScripted::Think);
#ifdef CLIENT_DLL
    // GMod calls ENT:Think on the client every frame
    SetNextClientThink(CLIENT_THINK_ALWAYS);
#endif
    SetNextThink(gpGlobals->curtime);

    SetTouch(&CBaseScripted::Touch);

    char className[255];
#if defined( CLIENT_DLL )
    if (strlen(GetScriptedClassname()) > 0)
        Q_strncpy(className, GetScriptedClassname(), sizeof(className));
    else
        Q_strncpy(className, GetClassname(), sizeof(className));
#else
    Q_strncpy(m_iScriptedClassname.GetForModify(), GetClassname(), sizeof(className));
    Q_strncpy(className, GetClassname(), sizeof(className));
#endif
    Q_strlower(className);
    SetClassname(className);

    if (!lua_isrefvalid(L, m_nTableReference))
    {
        // Srcbox: give every entity its own table (set up below to fall back to the class
        // table) instead of using the class table itself. Sharing it meant two entities of
        // one class overwrote each other's fields and NetworkVar accessors.
        lua_newtable(L);
        m_nTableReference = luaL_ref(L, LUA_REGISTRYINDEX);
    }
    else
    {
        lua_getglobal(L, "table");
        if (lua_istable(L, -1))
        {
            lua_getfield(L, -1, "Merge");
            if (lua_isfunction(L, -1))
            {
                lua_remove(L, -2);
                lua_getref(L, m_nTableReference);
                LoadScriptedEntity();
                luasrc_pcall(L, 2, 0);
            }
            else
            {
                lua_pop(L, 2);
            }
        }
        else
        {
            lua_pop(L, 1);
        }
    }

    // Look up anything the entity's own table lacks in the class table, like GMod's
    // per-entity copy of ENT. The class table carries the Base-chain lookup (the GMod
    // compatibility layer sets that up), so base fields such as base_edit's Editable resolve.
    // The table can already exist before this runs (something touched the entity from Lua
    // first, e.g. a NetworkVar change arriving), so this is done for both cases above.
    {
        int iTop = lua_gettop(L);

        lua_getref(L, m_nTableReference);
        if (lua_istable(L, -1) && !lua_getmetatable(L, -1))
        {
            int iBeforeLoad = lua_gettop(L);
            LoadScriptedEntity();

            if (lua_gettop(L) == iBeforeLoad + 1 && lua_istable(L, -1))
            {
                lua_newtable(L);           // metatable
                lua_pushvalue(L, -2);      // class table
                lua_setfield(L, -2, "__index");
                lua_setmetatable(L, -3);   // instance table
            }
        }

        lua_settop(L, iTop);
    }

    // Get (or init) the ref table
    LUA_GET_REF_TABLE(L, this);

#ifdef CLIENT_DLL
    lua_getfield(L, -1, "RenderGroup");
    if (lua_isnumber(L, -1))
    {
        m_iRenderGroupOverride = (int)lua_tointeger(L, -1);
    }
    lua_pop(L, 1);  // Pop the RenderGroup field
#endif

    // Experiment; TODO: Remaining members: https://wiki.facepunch.com/gmod/Structures/ENT
    // Note that we don't have to implement ClassNameOverride as it's handled in (GMod) Lua

    // Pop the LUA_GET_REF_TABLE
    lua_pop(L, 1);

    LUA_CALL_HOOK_BEGIN("PreEntityInitialize");
    CBaseEntity::PushLuaInstanceSafe(L, this);
    LUA_CALL_HOOK_END(1, 0);

#ifndef CLIENT_DLL
    // Like GMod: keyvalues reach ENT:KeyValue after SetupDataTables and before Initialize,
    // so a map-placed Lua entity (env_skypaint) gets the values the mapper set.
    m_bLuaKeyValuesReady = true;
    for (int i = 0; i + 1 < m_PendingLuaKeyValues.Count(); i += 2)
    {
        CallLuaKeyValue(m_PendingLuaKeyValues[i].Get(), m_PendingLuaKeyValues[i + 1].Get());
    }
    m_PendingLuaKeyValues.Purge();
#endif

    LUA_CALL_ENTITY_METHOD_BEGIN("Initialize");
    LUA_CALL_ENTITY_METHOD_END(0, 0);
#endif
}

#ifdef CLIENT_DLL
RenderGroup_t CBaseScripted::GetRenderGroup()
{
    if (m_iRenderGroupOverride > -1)
    {
        return (RenderGroup_t)m_iRenderGroupOverride;
    }

    return BaseClass::GetRenderGroup();
}

int CBaseScripted::DrawModel(int flags)
{
#ifdef LUA_SDK
    LUA_CALL_ENTITY_METHOD_BEGIN("Draw");
    lua_pushinteger(L, flags);
    LUA_CALL_ENTITY_METHOD_END(1, 1);

    LUA_RETURN_INTEGER();
#endif

    return BaseClass::DrawModel(flags);
}

void CBaseScripted::OnDataChanged(DataUpdateType_t updateType)
{
    BaseClass::OnDataChanged(updateType);

    if (updateType == DATA_UPDATE_CREATED)
    {
        if (m_iScriptedClassname.Get())
        {
            SetClassname(m_iScriptedClassname.Get());
            InitScriptedEntity();
        }
    }
}

const char* CBaseScripted::GetScriptedClassname(void)
{
    if (m_iScriptedClassname.Get())
        return m_iScriptedClassname.Get();
    return BaseClass::GetClassname();
}
#endif

#ifndef CLIENT_DLL
// Calls ENT:KeyValue( key, value ); true when it returned true (GMod: suppress the keyvalue)
bool CBaseScripted::CallLuaKeyValue(const char *szKeyName, const char *szValue)
{
    if (!L || !lua_isrefvalid(L, m_nTableReference))
        return false;

    int iTop = lua_gettop(L);
    bool bSuppress = false;

    LUA_CALL_ENTITY_METHOD_BEGIN("KeyValue");
    lua_pushstring(L, szKeyName);
    lua_pushstring(L, szValue);
    LUA_CALL_ENTITY_METHOD_END(2, 1);

    if (lua_gettop(L) > iTop && lua_isboolean(L, -1))
        bSuppress = lua_toboolean(L, -1) != 0;

    lua_settop(L, iTop);
    return bSuppress;
}

bool CBaseScripted::KeyValue(const char *szKeyName, const char *szValue)
{
    if (!m_bLuaKeyValuesReady)
    {
        // Not set up yet (map spawn): the engine still gets it now, Lua gets it after SetupDataTables
        m_PendingLuaKeyValues.AddToTail(CUtlString(szKeyName));
        m_PendingLuaKeyValues.AddToTail(CUtlString(szValue));
        return BaseClass::KeyValue(szKeyName, szValue);
    }

    if (CallLuaKeyValue(szKeyName, szValue))
        return true;

    return BaseClass::KeyValue(szKeyName, szValue);
}
#endif

void CBaseScripted::Spawn(void)
{
    BaseClass::Spawn();

#ifndef CLIENT_DLL
    InitScriptedEntity();
#endif
}

void CBaseScripted::Precache(void)
{
    BaseClass::Precache();

    // InitScriptedEntity();
}

// Override the base class, so we call OnRemove on the Lua scripted weapon.
void CBaseScripted::Remove()
{
    bool fullUpdate = false;  // TODO: implement this argument https://wiki.facepunch.com/gmod/Entity:OnRemove
    LUA_CALL_ENTITY_METHOD_BEGIN("OnRemove");
    lua_pushboolean(L, fullUpdate);  // doc: fullUpdate (always false, unimplemented currently)
    LUA_CALL_ENTITY_METHOD_END(1, 0);

    BaseClass::Remove();
}

#ifdef CLIENT_DLL
void CBaseScripted::ClientThink()
{
#ifdef LUA_SDK
    // GMod: the client side of ENT:Think (env_skypaint takes over g_SkyPaint there).
    // It only ever ran once and only called ClientThink before.
    LUA_CALL_ENTITY_METHOD_BEGIN("Think");
    LUA_CALL_ENTITY_METHOD_END(0, 0);

    LUA_CALL_ENTITY_METHOD_BEGIN("ClientThink");
    LUA_CALL_ENTITY_METHOD_END(0, 0);
#endif
}
#endif

void CBaseScripted::Think()
{
#ifdef LUA_SDK
    LUA_CALL_ENTITY_METHOD_BEGIN("Think");
    LUA_CALL_ENTITY_METHOD_END(0, 0);
#endif
}

void CBaseScripted::StartTouch(CBaseEntity* pOther)
{
#ifdef LUA_SDK
    LUA_CALL_ENTITY_METHOD_BEGIN("StartTouch");
    CBaseEntity::PushLuaInstanceSafe(L, pOther);
    LUA_CALL_ENTITY_METHOD_END(1, 0);
#endif
}

void CBaseScripted::Touch(CBaseEntity* pOther)
{
#ifdef LUA_SDK
    LUA_CALL_ENTITY_METHOD_BEGIN("Touch");
    CBaseEntity::PushLuaInstanceSafe(L, pOther);
    LUA_CALL_ENTITY_METHOD_END(1, 0);
#endif
}

void CBaseScripted::EndTouch(CBaseEntity* pOther)
{
#ifdef LUA_SDK
    LUA_CALL_ENTITY_METHOD_BEGIN("EndTouch");
    CBaseEntity::PushLuaInstanceSafe(L, pOther);
    LUA_CALL_ENTITY_METHOD_END(1, 0);
#endif
}

void CBaseScripted::VPhysicsUpdate(IPhysicsObject* pPhysics)
{
    BaseClass::VPhysicsUpdate(pPhysics);

#ifdef LUA_SDK
    LUA_CALL_ENTITY_METHOD_BEGIN("PhysicsUpdate");
    lua_pushphysicsobject(L, pPhysics);
    LUA_CALL_ENTITY_METHOD_END(1, 0);
#endif
}

#ifndef CLIENT_DLL
int CBaseScripted::UpdateTransmitState()
{
#ifdef LUA_SDK
    LUA_CALL_ENTITY_METHOD_BEGIN("UpdateTransmitState");
    LUA_CALL_ENTITY_METHOD_END(0, 1);

    LUA_RETURN_INTEGER();
#endif

    return BaseClass::UpdateTransmitState();
}
#endif
