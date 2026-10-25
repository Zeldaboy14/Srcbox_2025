//=============================================================================
//
// Purpose: Lua material proxies (GMod's matproxy library), client only.
//
// GMod lets Lua register material proxies with matproxy.Add( { name = "...", init = ..., bind = ... } )
// (lua/matproxy/*.lua, e.g. sky_paint.lua drives the painted sky from g_SkyPaint). The material system
// creates proxies through the client's CreateInterface( "<name>_IMaterialProxy003" ), so registering a
// name adds an interface entry whose factory makes a CLuaMaterialProxy. That proxy forwards to the Lua
// side: matproxy.Init( name, uniqueName, material, values ) once, matproxy.Call( uniqueName, material,
// entity ) on every bind, and matproxy.Remove( uniqueName ) when it is released.
//
// A Lua proxy registered under the name of a native client proxy takes over from it (newer interface
// entries are found first), which is how GMod behaves too.
//
// Included by lrender.cpp.
//
//=============================================================================

#if defined( CLIENT_DLL ) && defined( LUA_SDK )

#include "materialsystem/imaterialproxy.h"
#include "tier1/interface.h"
#include "tier1/utlbuffer.h"
#include "filesystem.h"
#include "KeyValues.h"

class CLuaMaterialProxy : public IMaterialProxy
{
public:
    CLuaMaterialProxy()
        : m_pMaterial( NULL ), m_pKeyValues( NULL ), m_pInitState( NULL ), m_bWarnedBind( false )
    {
        m_szProxyName[0] = 0;
        m_szUniqueName[0] = 0;
    }

    virtual ~CLuaMaterialProxy()
    {
        if ( m_pKeyValues )
            m_pKeyValues->deleteThis();
    }

    virtual bool Init( IMaterial *pMaterial, KeyValues *pKeyValues )
    {
        m_pMaterial = pMaterial;
        Q_strncpy( m_szProxyName, pKeyValues ? pKeyValues->GetName() : "", sizeof( m_szProxyName ) );
        Q_snprintf( m_szUniqueName, sizeof( m_szUniqueName ), "%s:%s:%p", m_szProxyName,
                    pMaterial ? pMaterial->GetName() : "", this );
        m_pKeyValues = pKeyValues ? pKeyValues->MakeCopy() : NULL;

        CallInit();
        return true;
    }

    virtual void OnBind( void *pArg )
    {
        if ( !L || !m_pMaterial )
            return;

        // The Lua state was recreated (reload): the old ActiveList entry is gone
        if ( L != m_pInitState )
            CallInit();

        C_BaseEntity *pEntity = NULL;
        if ( pArg )
        {
            IClientRenderable *pRenderable = ( IClientRenderable * )pArg;
            IClientUnknown *pUnknown = pRenderable->GetIClientUnknown();
            pEntity = pUnknown ? pUnknown->GetBaseEntity() : NULL;
        }

        int iTop = lua_gettop( L );

        if ( PushMatProxyFunction( "Call" ) )
        {
            lua_pushstring( L, m_szUniqueName );
            lua_pushmaterial( L, m_pMaterial );
            CBaseEntity::PushLuaInstanceSafe( L, pEntity );

            if ( lua_pcall( L, 3, 0, 0 ) != 0 && !m_bWarnedBind )
            {
                // Once per proxy, a bind runs every frame
                Warning( "[matproxy] '%s' bind failed on %s: %s\n", m_szProxyName,
                         m_pMaterial->GetName(), lua_tostring( L, -1 ) );
                m_bWarnedBind = true;
            }
        }

        lua_settop( L, iTop );
    }

    virtual void Release()
    {
        if ( L && L == m_pInitState )
        {
            int iTop = lua_gettop( L );

            if ( PushMatProxyFunction( "Remove" ) )
            {
                lua_pushstring( L, m_szUniqueName );
                lua_pcall( L, 1, 0, 0 );
            }

            lua_settop( L, iTop );
        }

        delete this;
    }

    virtual IMaterial *GetMaterial()
    {
        return m_pMaterial;
    }

private:
    // Pushes matproxy[ pszName ] when it is a function
    bool PushMatProxyFunction( const char *pszName )
    {
        lua_getglobal( L, "matproxy" );
        if ( !lua_istable( L, -1 ) )
            return false;

        lua_getfield( L, -1, pszName );
        if ( !lua_isfunction( L, -1 ) )
            return false;

        lua_remove( L, -2 );
        return true;
    }

    // The proxy's keyvalues as a table with lower case keys, like GMod passes them
    void PushValues()
    {
        lua_newtable( L );

        if ( !m_pKeyValues )
            return;

        for ( KeyValues *pSub = m_pKeyValues->GetFirstSubKey(); pSub; pSub = pSub->GetNextKey() )
        {
            char szKey[128];
            Q_strncpy( szKey, pSub->GetName(), sizeof( szKey ) );
            Q_strlower( szKey );

            switch ( pSub->GetDataType() )
            {
                case KeyValues::TYPE_INT:
                    lua_pushinteger( L, pSub->GetInt() );
                    break;
                case KeyValues::TYPE_FLOAT:
                    lua_pushnumber( L, pSub->GetFloat() );
                    break;
                case KeyValues::TYPE_NONE:
                    continue;  // a sub-block
                default:
                    lua_pushstring( L, pSub->GetString() );
                    break;
            }

            lua_setfield( L, -2, szKey );
        }
    }

    void CallInit()
    {
        m_pInitState = L;
        m_bWarnedBind = false;

        if ( !L || !m_pMaterial )
            return;

        int iTop = lua_gettop( L );

        if ( PushMatProxyFunction( "Init" ) )
        {
            lua_pushstring( L, m_szProxyName );
            lua_pushstring( L, m_szUniqueName );
            lua_pushmaterial( L, m_pMaterial );
            PushValues();

            if ( lua_pcall( L, 4, 0, 0 ) != 0 )
            {
                Warning( "[matproxy] '%s' init failed on %s: %s\n", m_szProxyName,
                         m_pMaterial->GetName(), lua_tostring( L, -1 ) );
            }
        }

        lua_settop( L, iTop );
    }

    IMaterial *m_pMaterial;
    KeyValues *m_pKeyValues;
    lua_State *m_pInitState;
    bool m_bWarnedBind;
    char m_szProxyName[128];
    char m_szUniqueName[384];
};

static void *CreateLuaMaterialProxy()
{
    return static_cast< IMaterialProxy * >( new CLuaMaterialProxy );
}

// Interface names registered so far ("<name>_IMaterialProxy003"); the strings must outlive the entries
static CUtlVector< char * > s_LuaMaterialProxyInterfaces;

LUA_REGISTRATION_INIT( MaterialProxies );

LUA_BINDING_BEGIN( MaterialProxies, Register, "library", "Lets materials using the named proxy get a Lua proxy (calls matproxy.Init/Call/Remove). Returns false if it was already registered.", "client" )
{
    const char *pszName = LUA_BINDING_ARGUMENT( luaL_checkstring, 1, "proxyName" );

    char szInterface[256];
    Q_snprintf( szInterface, sizeof( szInterface ), "%s%s", pszName, IMATERIAL_PROXY_INTERFACE_VERSION );

    for ( int i = 0; i < s_LuaMaterialProxyInterfaces.Count(); i++ )
    {
        if ( Q_strcmp( s_LuaMaterialProxyInterfaces[i], szInterface ) == 0 )
        {
            lua_pushboolean( L, false );
            return 1;
        }
    }

    char *pszInterface = V_strdup( szInterface );
    s_LuaMaterialProxyInterfaces.AddToTail( pszInterface );

    // Adds itself to the head of the interface list; never freed, like the static ones
    new InterfaceReg( CreateLuaMaterialProxy, pszInterface );

    lua_pushboolean( L, true );
    return 1;
}
LUA_BINDING_END( "boolean", "True if newly registered." )

LUA_BINDING_BEGIN( MaterialProxies, RefreshMaterialsUsing, "library", "Reloads the loaded materials whose .vmt mentions any of the given proxy names, so they pick up proxies registered after they were loaded.", "client" )
{
    luaL_checktype( L, 1, LUA_TTABLE );

    CUtlVector< CUtlString > proxyNames;
    int nNames = ( int )lua_rawlen( L, 1 );
    for ( int i = 1; i <= nNames; i++ )
    {
        lua_rawgeti( L, 1, i );
        if ( lua_isstring( L, -1 ) )
            proxyNames.AddToTail( lua_tostring( L, -1 ) );
        lua_pop( L, 1 );
    }

    if ( proxyNames.Count() == 0 )
    {
        lua_pushinteger( L, 0 );
        return 1;
    }

    // Collect first, refreshing while iterating could change the material list
    CUtlVector< IMaterial * > toRefresh;
    CUtlBuffer buffer( 0, 0, CUtlBuffer::TEXT_BUFFER );

    for ( MaterialHandle_t h = materials->FirstMaterial(); h != materials->InvalidMaterial(); h = materials->NextMaterial( h ) )
    {
        IMaterial *pMaterial = materials->GetMaterial( h );
        if ( !pMaterial || pMaterial->IsErrorMaterial() )
            continue;

        char szPath[MAX_PATH];
        Q_snprintf( szPath, sizeof( szPath ), "materials/%s.vmt", pMaterial->GetName() );

        buffer.Clear();
        if ( !g_pFullFileSystem->ReadFile( szPath, "GAME", buffer ) )
            continue;

        buffer.PutChar( 0 );
        const char *pszText = ( const char * )buffer.Base();

        for ( int i = 0; i < proxyNames.Count(); i++ )
        {
            if ( Q_stristr( pszText, proxyNames[i].Get() ) )
            {
                toRefresh.AddToTail( pMaterial );
                break;
            }
        }
    }

    for ( int i = 0; i < toRefresh.Count(); i++ )
    {
        toRefresh[i]->Refresh();
    }

    lua_pushinteger( L, toRefresh.Count() );
    return 1;
}
LUA_BINDING_END( "integer", "How many materials were reloaded." )

//-----------------------------------------------------------------------------
// Registration, called from luaopen_render. Leaves the stack as it was.
//-----------------------------------------------------------------------------
static void luaopen_MaterialProxies( lua_State *L )
{
    int top = lua_gettop( L );
    luaL_register( L, "MaterialProxies", MaterialProxies_luaRegistry );
    lua_settop( L, top );
}

#endif  // CLIENT_DLL && LUA_SDK
