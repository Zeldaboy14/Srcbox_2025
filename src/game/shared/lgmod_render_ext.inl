//========= Srcbox ============//
//
// GMod-style client rendering helpers, included by lrender.cpp (client only):
//
//  - Meshes:           GMod's 'mesh' library (immediate mode mesh building)
//  - IMesh:            static meshes (GMod's Mesh() / IMesh)
//  - ParticleEmitters: GMod's ParticleEmitter / CLuaEmitter / CLuaParticle
//
// Kept in a separate file so no project files need regenerating.
//
//=============================//

#if defined( CLIENT_DLL ) && defined( LUA_SDK )

#include "materialsystem/imesh.h"
#include "texture_group_names.h"
#include "particles_simple.h"
#include "particle_util.h"
#include "c_tracer.h"
#include "mathlib/langle.h"
#include "c_sun.h"
#include "steam/steam_api.h"

#define LUA_IMESHMETANAME "IMesh"
#define LUA_LUAEMITTERMETANAME "CLuaEmitter"
#define LUA_LUAPARTICLEMETANAME "CLuaParticle"

//-----------------------------------------------------------------------------
// Lua state tracking: callbacks stored by particles must never run in (or be
// released into) a Lua state that has been closed. A sentinel userdata in each
// state bumps this serial when the state is closed.
//-----------------------------------------------------------------------------
static int s_nLuaRenderExtStateSerial = 1;

static int LuaRenderExt_SentinelGC( lua_State *L )
{
    s_nLuaRenderExtStateSerial++;
    return 0;
}

static void LuaRenderExt_EnsureSentinel( lua_State *L )
{
    lua_getfield( L, LUA_REGISTRYINDEX, "SrcboxRenderExtSentinel" );

    if ( lua_isnil( L, -1 ) )
    {
        lua_pop( L, 1 );
        lua_newuserdata( L, 1 );
        lua_newtable( L );
        lua_pushcfunction( L, LuaRenderExt_SentinelGC );
        lua_setfield( L, -2, "__gc" );
        lua_setmetatable( L, -2 );
        lua_setfield( L, LUA_REGISTRYINDEX, "SrcboxRenderExtSentinel" );
        return;
    }

    lua_pop( L, 1 );
}

//=============================================================================
// Mesh building (shared by the mesh library and IMesh)
//=============================================================================

#define LUA_MESH_VERTEX_FORMAT                                                 \
    ( VERTEX_POSITION | VERTEX_NORMAL | VERTEX_COLOR | VERTEX_SPECULAR |       \
      VERTEX_TANGENT_S | VERTEX_TANGENT_T | VERTEX_USERDATA_SIZE( 4 ) |        \
      VERTEX_TEXCOORD_SIZE( 0, 2 ) | VERTEX_TEXCOORD_SIZE( 1, 2 ) |            \
      VERTEX_TEXCOORD_SIZE( 2, 2 ) | VERTEX_TEXCOORD_SIZE( 3, 2 ) )

struct LuaMesh_t
{
    IMesh *pMesh;
};

static CMeshBuilder s_LuaMeshBuilder;
static IMesh *s_pLuaMeshBuilding = NULL;  // the mesh being built (dynamic or static)
static bool s_bLuaMeshIsDynamic = false;
static int s_nLuaMeshVertexCount = 0;
static int s_nLuaMeshMaxVertices = 0;
static unsigned char s_LuaMeshColor[4] = { 255, 255, 255, 255 };

static int LuaMesh_VertexCountForPrimitives( MaterialPrimitiveType_t type, int nPrimitives )
{
    switch ( type )
    {
        case MATERIAL_POINTS:
            return nPrimitives;
        case MATERIAL_LINES:
            return nPrimitives * 2;
        case MATERIAL_TRIANGLES:
            return nPrimitives * 3;
        case MATERIAL_TRIANGLE_STRIP:
            return nPrimitives + 2;
        case MATERIAL_LINE_STRIP:
            return nPrimitives + 1;
        case MATERIAL_LINE_LOOP:
        case MATERIAL_POLYGON:
            return nPrimitives;
        case MATERIAL_QUADS:
            return nPrimitives * 4;
        default:
            return nPrimitives * 3;
    }
}

// Errors when no mesh is being built. Returns false (silently) once every vertex
// that was asked for in mesh.Begin has been written, so nothing is written past
// the locked buffer.
static bool LuaMesh_IsBuilding( lua_State *L )
{
    if ( !s_pLuaMeshBuilding )
    {
        luaL_error( L, "mesh.Begin has not been called" );
        return false;
    }

    return s_nLuaMeshVertexCount < s_nLuaMeshMaxVertices;
}

// Writes the per-vertex state that isn't explicitly provided, then advances
static void LuaMesh_AdvanceVertex()
{
    if ( !s_pLuaMeshBuilding )
        return;

    if ( s_nLuaMeshVertexCount >= s_nLuaMeshMaxVertices )
        return;  // Writing past the locked range would corrupt memory

    s_LuaMeshBuilder.AdvanceVertex();
    s_nLuaMeshVertexCount++;
}

static void LuaMesh_WriteVertex( const Vector &pos, float u, float v, const Vector *pNormal = NULL )
{
    if ( !s_pLuaMeshBuilding || s_nLuaMeshVertexCount >= s_nLuaMeshMaxVertices )
        return;

    s_LuaMeshBuilder.Position3fv( pos.Base() );
    s_LuaMeshBuilder.Color4ubv( s_LuaMeshColor );
    s_LuaMeshBuilder.TexCoord2f( 0, u, v );

    if ( pNormal )
        s_LuaMeshBuilder.Normal3fv( pNormal->Base() );

    LuaMesh_AdvanceVertex();
}

static void LuaMesh_Begin( lua_State *L, IMesh *pStaticMesh, MaterialPrimitiveType_t type, int nPrimitives )
{
    if ( s_pLuaMeshBuilding )
    {
        // An earlier build never reached mesh.End (usually a Lua error in between).
        // Close it without drawing so mesh building doesn't stay broken.
        Warning( "mesh.Begin called while another mesh was still being built (missing mesh.End?)\n" );
        s_LuaMeshBuilder.End();
        s_pLuaMeshBuilding = NULL;
    }

    if ( nPrimitives <= 0 )
    {
        luaL_error( L, "mesh.Begin: primitive count must be positive" );
        return;
    }

    CMatRenderContextPtr pRenderContext( materials );
    IMesh *pMesh = pStaticMesh;
    s_bLuaMeshIsDynamic = ( pMesh == NULL );

    if ( !pMesh )
    {
        pMesh = pRenderContext->GetDynamicMesh( true );
    }

    if ( !pMesh )
    {
        luaL_error( L, "mesh.Begin: couldn't get a mesh to build" );
        return;
    }

    s_nLuaMeshMaxVertices = LuaMesh_VertexCountForPrimitives( type, nPrimitives );
    s_nLuaMeshVertexCount = 0;
    s_LuaMeshColor[0] = s_LuaMeshColor[1] = s_LuaMeshColor[2] = s_LuaMeshColor[3] = 255;

    s_LuaMeshBuilder.Begin( pMesh, type, nPrimitives );
    s_pLuaMeshBuilding = pMesh;
}

static void LuaMesh_End()
{
    if ( !s_pLuaMeshBuilding )
        return;

    // Unwritten vertices would be garbage; repeat the last one so the
    // primitive count stays valid and nothing random gets drawn.
    while ( s_nLuaMeshVertexCount < s_nLuaMeshMaxVertices )
    {
        s_LuaMeshBuilder.Color4ub( 0, 0, 0, 0 );
        LuaMesh_AdvanceVertex();
    }

    s_LuaMeshBuilder.End();

    if ( s_bLuaMeshIsDynamic )
    {
        s_pLuaMeshBuilding->Draw();
    }

    s_pLuaMeshBuilding = NULL;
}

//-----------------------------------------------------------------------------
// The mesh library
//-----------------------------------------------------------------------------
LUA_REGISTRATION_INIT( Meshes );

LUA_BINDING_BEGIN( Meshes, Begin, "library", "Begins building a mesh. Either (primitiveType, primitiveCount) to draw immediately with the current material, or (IMesh, primitiveType, primitiveCount) to build a static mesh.", "client" )
{
    if ( lua_isuserdata( L, 1 ) )
    {
        LuaMesh_t *pLuaMesh = ( LuaMesh_t * )luaL_checkudata( L, 1, LUA_IMESHMETANAME );

        if ( !pLuaMesh->pMesh )
            return luaL_error( L, "mesh.Begin: the IMesh has been destroyed" );

        LuaMesh_Begin( L, pLuaMesh->pMesh, ( MaterialPrimitiveType_t )( int )luaL_checknumber( L, 2 ), ( int )luaL_checknumber( L, 3 ) );
        return 0;
    }

    LuaMesh_Begin( L, NULL, ( MaterialPrimitiveType_t )( int )luaL_checknumber( L, 1 ), ( int )luaL_checknumber( L, 2 ) );
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( Meshes, End, "library", "Ends the mesh and draws it (immediate mode).", "client" )
{
    LuaMesh_End();
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( Meshes, Position, "library", "Sets the position of the current vertex.", "client" )
{
    if ( !LuaMesh_IsBuilding( L ) )
        return 0;

    if ( lua_isnumber( L, 1 ) )
    {
        s_LuaMeshBuilder.Position3f( luaL_checknumber( L, 1 ), luaL_checknumber( L, 2 ), luaL_checknumber( L, 3 ) );
        return 0;
    }

    Vector &pos = luaL_checkvector( L, 1 );
    s_LuaMeshBuilder.Position3fv( pos.Base() );
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( Meshes, Normal, "library", "Sets the normal of the current vertex.", "client" )
{
    if ( !LuaMesh_IsBuilding( L ) )
        return 0;

    if ( lua_isnumber( L, 1 ) )
    {
        s_LuaMeshBuilder.Normal3f( luaL_checknumber( L, 1 ), luaL_checknumber( L, 2 ), luaL_checknumber( L, 3 ) );
        return 0;
    }

    Vector &normal = luaL_checkvector( L, 1 );
    s_LuaMeshBuilder.Normal3fv( normal.Base() );
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( Meshes, TangentS, "library", "Sets the s tangent of the current vertex.", "client" )
{
    if ( !LuaMesh_IsBuilding( L ) )
        return 0;

    Vector &tangent = luaL_checkvector( L, 1 );
    s_LuaMeshBuilder.TangentS3fv( tangent.Base() );
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( Meshes, TangentT, "library", "Sets the t tangent of the current vertex.", "client" )
{
    if ( !LuaMesh_IsBuilding( L ) )
        return 0;

    Vector &tangent = luaL_checkvector( L, 1 );
    s_LuaMeshBuilder.TangentT3fv( tangent.Base() );
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( Meshes, TexCoord, "library", "Sets the texture coordinate of the current vertex for a stage.", "client" )
{
    if ( !LuaMesh_IsBuilding( L ) )
        return 0;

    int stage = clamp( ( int )luaL_checknumber( L, 1 ), 0, 3 );
    s_LuaMeshBuilder.TexCoord2f( stage, luaL_checknumber( L, 2 ), luaL_checknumber( L, 3 ) );
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( Meshes, Color, "library", "Sets the color (0-255) of the current vertex.", "client" )
{
    if ( !LuaMesh_IsBuilding( L ) )
        return 0;

    s_LuaMeshColor[0] = ( unsigned char )clamp( ( int )luaL_optnumber( L, 1, 255 ), 0, 255 );
    s_LuaMeshColor[1] = ( unsigned char )clamp( ( int )luaL_optnumber( L, 2, 255 ), 0, 255 );
    s_LuaMeshColor[2] = ( unsigned char )clamp( ( int )luaL_optnumber( L, 3, 255 ), 0, 255 );
    s_LuaMeshColor[3] = ( unsigned char )clamp( ( int )luaL_optnumber( L, 4, 255 ), 0, 255 );
    s_LuaMeshBuilder.Color4ubv( s_LuaMeshColor );
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( Meshes, Specular, "library", "Sets the specular color (0-255) of the current vertex.", "client" )
{
    if ( !LuaMesh_IsBuilding( L ) )
        return 0;

    s_LuaMeshBuilder.Specular4ub(
        ( unsigned char )clamp( ( int )luaL_optnumber( L, 1, 255 ), 0, 255 ),
        ( unsigned char )clamp( ( int )luaL_optnumber( L, 2, 255 ), 0, 255 ),
        ( unsigned char )clamp( ( int )luaL_optnumber( L, 3, 255 ), 0, 255 ),
        ( unsigned char )clamp( ( int )luaL_optnumber( L, 4, 255 ), 0, 255 ) );
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( Meshes, UserData, "library", "Sets the user data (tangent w) of the current vertex.", "client" )
{
    if ( !LuaMesh_IsBuilding( L ) )
        return 0;

    float data[4] = {
        ( float )luaL_optnumber( L, 1, 0 ),
        ( float )luaL_optnumber( L, 2, 0 ),
        ( float )luaL_optnumber( L, 3, 0 ),
        ( float )luaL_optnumber( L, 4, 0 ) };
    s_LuaMeshBuilder.UserData( data );
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( Meshes, AdvanceVertex, "library", "Moves on to the next vertex.", "client" )
{
    if ( !LuaMesh_IsBuilding( L ) )
        return 0;

    LuaMesh_AdvanceVertex();
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( Meshes, VertexCount, "library", "Returns the number of vertices written so far.", "client" )
{
    lua_pushinteger( L, s_nLuaMeshVertexCount );
    return 1;
}
LUA_BINDING_END( "integer", "The vertex count." )

LUA_BINDING_BEGIN( Meshes, Quad, "library", "Writes a quad (4 vertices) using the current color.", "client" )
{
    if ( !LuaMesh_IsBuilding( L ) )
        return 0;

    Vector v1 = luaL_checkvector( L, 1 );
    Vector v2 = luaL_checkvector( L, 2 );
    Vector v3 = luaL_checkvector( L, 3 );
    Vector v4 = luaL_checkvector( L, 4 );

    Vector normal = CrossProduct( v3 - v1, v2 - v1 );
    VectorNormalize( normal );

    LuaMesh_WriteVertex( v1, 0, 0, &normal );
    LuaMesh_WriteVertex( v2, 1, 0, &normal );
    LuaMesh_WriteVertex( v3, 1, 1, &normal );
    LuaMesh_WriteVertex( v4, 0, 1, &normal );
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( Meshes, QuadEasy, "library", "Writes a quad facing a normal (position, normal, width, height).", "client" )
{
    if ( !LuaMesh_IsBuilding( L ) )
        return 0;

    Vector pos = luaL_checkvector( L, 1 );
    Vector normal = luaL_checkvector( L, 2 );
    float halfWidth = luaL_checknumber( L, 3 ) * 0.5f;
    float halfHeight = luaL_checknumber( L, 4 ) * 0.5f;

    VectorNormalize( normal );
    Vector right, up;
    VectorVectors( normal, right, up );

    LuaMesh_WriteVertex( pos - right * halfWidth + up * halfHeight, 0, 0, &normal );
    LuaMesh_WriteVertex( pos + right * halfWidth + up * halfHeight, 1, 0, &normal );
    LuaMesh_WriteVertex( pos + right * halfWidth - up * halfHeight, 1, 1, &normal );
    LuaMesh_WriteVertex( pos - right * halfWidth - up * halfHeight, 0, 1, &normal );
    return 0;
}
LUA_BINDING_END()

//-----------------------------------------------------------------------------
// IMesh
//-----------------------------------------------------------------------------
LUA_REGISTRATION_INIT( LuaMesh );

static LuaMesh_t *LuaMesh_Check( lua_State *L, int idx )
{
    return ( LuaMesh_t * )luaL_checkudata( L, idx, LUA_IMESHMETANAME );
}

static void LuaMesh_Destroy( LuaMesh_t *pLuaMesh )
{
    if ( !pLuaMesh->pMesh )
        return;

    if ( s_pLuaMeshBuilding == pLuaMesh->pMesh )
    {
        s_LuaMeshBuilder.End();
        s_pLuaMeshBuilding = NULL;
    }

    CMatRenderContextPtr pRenderContext( materials );
    pRenderContext->DestroyStaticMesh( pLuaMesh->pMesh );
    pLuaMesh->pMesh = NULL;
}

// Reads a GMod vertex table: { pos, normal, tangent, binormal, color, u, v, u1, v1, ..., userdata }
static void LuaMesh_WriteVertexTable( lua_State *L, int idx )
{
    if ( !s_pLuaMeshBuilding || s_nLuaMeshVertexCount >= s_nLuaMeshMaxVertices )
        return;

    lua_getfield( L, idx, "pos" );
    if ( lua_isvector( L, -1 ) )
        s_LuaMeshBuilder.Position3fv( luaL_checkvector( L, -1 ).Base() );
    lua_pop( L, 1 );

    lua_getfield( L, idx, "normal" );
    if ( lua_isvector( L, -1 ) )
        s_LuaMeshBuilder.Normal3fv( luaL_checkvector( L, -1 ).Base() );
    lua_pop( L, 1 );

    lua_getfield( L, idx, "tangent" );
    if ( lua_isvector( L, -1 ) )
        s_LuaMeshBuilder.TangentS3fv( luaL_checkvector( L, -1 ).Base() );
    lua_pop( L, 1 );

    lua_getfield( L, idx, "binormal" );
    if ( lua_isvector( L, -1 ) )
        s_LuaMeshBuilder.TangentT3fv( luaL_checkvector( L, -1 ).Base() );
    lua_pop( L, 1 );

    unsigned char color[4] = { 255, 255, 255, 255 };
    lua_getfield( L, idx, "color" );
    if ( lua_iscolor( L, -1 ) )
    {
        lua_Color &c = luaL_checkcolor( L, -1 );
        color[0] = c.r();
        color[1] = c.g();
        color[2] = c.b();
        color[3] = c.a();
    }
    else if ( lua_istable( L, -1 ) )
    {
        const char *fields[4] = { "r", "g", "b", "a" };
        for ( int i = 0; i < 4; i++ )
        {
            lua_getfield( L, -1, fields[i] );
            if ( lua_isnumber( L, -1 ) )
                color[i] = ( unsigned char )clamp( ( int )lua_tonumber( L, -1 ), 0, 255 );
            lua_pop( L, 1 );
        }
    }
    lua_pop( L, 1 );
    s_LuaMeshBuilder.Color4ubv( color );

    const char *uFields[4] = { "u", "u1", "u2", "u3" };
    const char *vFields[4] = { "v", "v1", "v2", "v3" };
    for ( int stage = 0; stage < 4; stage++ )
    {
        lua_getfield( L, idx, uFields[stage] );
        lua_getfield( L, idx, vFields[stage] );
        if ( lua_isnumber( L, -2 ) || lua_isnumber( L, -1 ) )
            s_LuaMeshBuilder.TexCoord2f( stage, lua_tonumber( L, -2 ), lua_tonumber( L, -1 ) );
        lua_pop( L, 2 );
    }

    lua_getfield( L, idx, "userdata" );
    if ( lua_istable( L, -1 ) )
    {
        float data[4] = { 0, 0, 0, 0 };
        for ( int i = 0; i < 4; i++ )
        {
            lua_rawgeti( L, -1, i + 1 );
            data[i] = ( float )lua_tonumber( L, -1 );
            lua_pop( L, 1 );
        }
        s_LuaMeshBuilder.UserData( data );
    }
    lua_pop( L, 1 );

    LuaMesh_AdvanceVertex();
}

LUA_BINDING_BEGIN( LuaMesh, BuildFromTriangles, "class", "Builds the mesh from a table of vertex tables (3 per triangle).", "client" )
{
    LuaMesh_t *pLuaMesh = LuaMesh_Check( L, 1 );
    luaL_checktype( L, 2, LUA_TTABLE );

    if ( !pLuaMesh->pMesh )
        return luaL_error( L, "IMesh has been destroyed" );

    int nVertices = ( int )luaL_len( L, 2 );
    int nTriangles = nVertices / 3;

    if ( nTriangles <= 0 )
        return 0;

    LuaMesh_Begin( L, pLuaMesh->pMesh, MATERIAL_TRIANGLES, nTriangles );

    for ( int i = 1; i <= nTriangles * 3; i++ )
    {
        lua_rawgeti( L, 2, i );

        if ( lua_istable( L, -1 ) )
            LuaMesh_WriteVertexTable( L, lua_gettop( L ) );
        else
            LuaMesh_AdvanceVertex();

        lua_pop( L, 1 );
    }

    LuaMesh_End();
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( LuaMesh, Draw, "class", "Draws the mesh with the current material.", "client" )
{
    LuaMesh_t *pLuaMesh = LuaMesh_Check( L, 1 );

    if ( pLuaMesh->pMesh && s_pLuaMeshBuilding != pLuaMesh->pMesh )
        pLuaMesh->pMesh->Draw();

    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( LuaMesh, Destroy, "class", "Frees the mesh.", "client" )
{
    LuaMesh_Destroy( LuaMesh_Check( L, 1 ) );
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( LuaMesh, IsValid, "class", "Whether the mesh hasn't been destroyed.", "client" )
{
    lua_pushboolean( L, LuaMesh_Check( L, 1 )->pMesh != NULL );
    return 1;
}
LUA_BINDING_END( "boolean", "Is valid." )

LUA_BINDING_BEGIN( LuaMesh, __gc, "class", "Frees the mesh.", "client" )
{
    LuaMesh_Destroy( LuaMesh_Check( L, 1 ) );
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( LuaMesh, __tostring, "class", "String representation.", "client" )
{
    lua_pushfstring( L, "IMesh: %p", LuaMesh_Check( L, 1 )->pMesh );
    return 1;
}
LUA_BINDING_END( "string", "The string." )

LUA_BINDING_BEGIN( Meshes, Create, "library", "Creates a static mesh (GMod's Mesh()). The material is optional.", "client" )
{
    IMaterial *pMaterial = NULL;

    if ( !lua_isnoneornil( L, 1 ) )
        pMaterial = luaL_checkmaterial( L, 1 );

    CMatRenderContextPtr pRenderContext( materials );
    IMesh *pMesh = pRenderContext->CreateStaticMesh( LUA_MESH_VERTEX_FORMAT, TEXTURE_GROUP_STATIC_VERTEX_BUFFER_OTHER, pMaterial );

    LuaMesh_t *pLuaMesh = ( LuaMesh_t * )lua_newuserdata( L, sizeof( LuaMesh_t ) );
    pLuaMesh->pMesh = pMesh;
    LUA_SAFE_SET_METATABLE( L, LUA_IMESHMETANAME );
    return 1;
}
LUA_BINDING_END( "IMesh", "The created mesh." )

//=============================================================================
// Particles
//=============================================================================
class CLuaEmitter;
struct CLuaParticle;

struct LuaParticleHandle_t
{
    CLuaParticle *pParticle;
};

struct LuaEmitterHandle_t
{
    CSmartPtr< CLuaEmitter > *pEmitter;
};

// The engine caps particle memory at 96 bytes (PARTICLE_SIZE), so the engine-side
// particle only carries a pointer to the Lua particle's data.
struct LuaRawParticle : public Particle
{
    CLuaParticle *m_pLua;
};

struct CLuaParticle
{
    LuaRawParticle *m_pRaw;
    Vector m_vecVelocity;
    Vector m_vecGravity;
    Vector m_vecColor;  // 0-255
    QAngle m_angAngles;
    QAngle m_angAngleVelocity;
    float m_flLifetime;
    float m_flDieTime;
    float m_flStartAlpha;
    float m_flEndAlpha;
    float m_flStartSize;
    float m_flEndSize;
    float m_flStartLength;
    float m_flEndLength;
    float m_flRoll;
    float m_flRollDelta;
    float m_flAirResistance;
    float m_flBounce;
    float m_flNextThink;
    bool m_bCollide;
    bool m_bLighting;
    bool m_bVelocityScale;
    int m_nThinkRef;
    int m_nCollideRef;
    char m_szMaterial[64];

    LuaParticleHandle_t *m_pHandle;
    CLuaEmitter *m_pEmitter;
    CLuaParticle *m_pLuaPrev;
    CLuaParticle *m_pLuaNext;
};

class CLuaEmitter : public CSimpleEmitter
{
public:
    DECLARE_CLASS( CLuaEmitter, CSimpleEmitter );

    static CSmartPtr< CLuaEmitter > Create( lua_State *L, const Vector &pos, bool b3D )
    {
        CLuaEmitter *pRet = new CLuaEmitter( L, pos, b3D );
        pRet->SetDynamicallyAllocated( true );
        return pRet;
    }

    bool CanCallLua() const
    {
        return m_pL && m_pL == L && m_nStateSerial == s_nLuaRenderExtStateSerial;
    }

    void ReleaseRef( int &ref )
    {
        if ( ref != LUA_NOREF && ref != LUA_REFNIL && CanCallLua() )
            luaL_unref( m_pL, LUA_REGISTRYINDEX, ref );

        ref = LUA_NOREF;
    }

    void SetLuaRef( int &ref, lua_State *pState, int idx )
    {
        ReleaseRef( ref );

        if ( lua_isfunction( pState, idx ) && CanCallLua() )
        {
            lua_pushvalue( pState, idx );
            ref = luaL_ref( pState, LUA_REGISTRYINDEX );
        }
    }

    CLuaParticle *AddLuaParticle( const char *pszMaterial, const Vector &pos )
    {
        PMaterialHandle hMaterial = GetPMaterial( pszMaterial );

        if ( !hMaterial )
            return NULL;

        LuaRawParticle *pRaw = ( LuaRawParticle * )AddParticle( sizeof( LuaRawParticle ), hMaterial, pos );

        if ( !pRaw )
            return NULL;

        CLuaParticle *p = new CLuaParticle;
        p->m_pRaw = pRaw;
        pRaw->m_pLua = p;

        p->m_vecVelocity.Init();
        p->m_vecGravity.Init();
        p->m_vecColor.Init( 255, 255, 255 );
        p->m_angAngles.Init();
        p->m_angAngleVelocity.Init();
        p->m_flLifetime = 0;
        p->m_flDieTime = 1;
        p->m_flStartAlpha = 255;
        p->m_flEndAlpha = 255;
        p->m_flStartSize = 1;
        p->m_flEndSize = 1;
        p->m_flStartLength = 0;
        p->m_flEndLength = 0;
        p->m_flRoll = 0;
        p->m_flRollDelta = 0;
        p->m_flAirResistance = 0;
        p->m_flBounce = 0;
        p->m_flNextThink = 0;
        p->m_bCollide = false;
        p->m_bLighting = false;
        p->m_bVelocityScale = false;
        p->m_nThinkRef = LUA_NOREF;
        p->m_nCollideRef = LUA_NOREF;
        V_strncpy( p->m_szMaterial, pszMaterial, sizeof( p->m_szMaterial ) );
        p->m_pHandle = NULL;
        p->m_pEmitter = this;

        // Link into our own list so particles can be cleaned up
        p->m_pLuaPrev = NULL;
        p->m_pLuaNext = m_pFirstLuaParticle;
        if ( m_pFirstLuaParticle )
            m_pFirstLuaParticle->m_pLuaPrev = p;
        m_pFirstLuaParticle = p;

        return p;
    }

    // Cuts every tie between a particle and Lua (its handle and callbacks)
    void DetachParticle( CLuaParticle *p )
    {
        if ( p->m_pHandle )
        {
            p->m_pHandle->pParticle = NULL;
            p->m_pHandle = NULL;

            // The registry pins the handle while the particle lives; let it go
            if ( CanCallLua() )
            {
                lua_pushnil( m_pL );
                lua_rawsetp( m_pL, LUA_REGISTRYINDEX, p );
            }
        }

        ReleaseRef( p->m_nThinkRef );
        ReleaseRef( p->m_nCollideRef );
    }

    // When the effect is removed (e.g. on level change) its particles are freed in
    // the destructor without NotifyDestroyParticle, so detach them all here first.
    virtual void NotifyRemove()
    {
        CLuaParticle *p = m_pFirstLuaParticle;

        while ( p )
        {
            CLuaParticle *pNext = p->m_pLuaNext;
            DetachParticle( p );
            p->m_pRaw->m_pLua = NULL;
            delete p;
            p = pNext;
        }

        m_pFirstLuaParticle = NULL;

        // Removed while Lua still holds the emitter (level change): the base class
        // deletes us, so drop Lua's reference now instead of later on a dead object.
        if ( m_pLuaHandle && m_pLuaHandle->pEmitter )
        {
            CSmartPtr< CLuaEmitter > *pSmartPtr = m_pLuaHandle->pEmitter;
            m_pLuaHandle->pEmitter = NULL;
            m_pLuaHandle = NULL;
            delete pSmartPtr;
        }

        BaseClass::NotifyRemove();
    }

    virtual void NotifyDestroyParticle( Particle *pParticle )
    {
        LuaRawParticle *pRaw = ( LuaRawParticle * )pParticle;
        CLuaParticle *p = pRaw->m_pLua;

        if ( !p )
        {
            BaseClass::NotifyDestroyParticle( pParticle );
            return;
        }

        DetachParticle( p );

        if ( p->m_pLuaPrev )
            p->m_pLuaPrev->m_pLuaNext = p->m_pLuaNext;
        else if ( m_pFirstLuaParticle == p )
            m_pFirstLuaParticle = p->m_pLuaNext;

        if ( p->m_pLuaNext )
            p->m_pLuaNext->m_pLuaPrev = p->m_pLuaPrev;

        p->m_pLuaPrev = p->m_pLuaNext = NULL;

        pRaw->m_pLua = NULL;
        delete p;

        BaseClass::NotifyDestroyParticle( pParticle );
    }

    void PushParticle( lua_State *pState, CLuaParticle *p );

    virtual void SimulateParticles( CParticleSimulateIterator *pIterator )
    {
        float timeDelta = pIterator->GetTimeDelta();

        LuaRawParticle *pRaw = ( LuaRawParticle * )pIterator->GetFirst();
        while ( pRaw )
        {
            CLuaParticle *p = pRaw->m_pLua;

            if ( !p )
            {
                pIterator->RemoveParticle( pRaw );
                pRaw = ( LuaRawParticle * )pIterator->GetNext();
                continue;
            }

            // Think
            if ( p->m_nThinkRef != LUA_NOREF && gpGlobals->curtime >= p->m_flNextThink && CanCallLua() )
            {
                p->m_flNextThink = FLT_MAX;  // the callback has to call SetNextThink again
                lua_rawgeti( m_pL, LUA_REGISTRYINDEX, p->m_nThinkRef );
                PushParticle( m_pL, p );
                luasrc_pcall( m_pL, 1, 0 );
            }

            // Velocity
            p->m_vecVelocity += p->m_vecGravity * timeDelta;

            if ( p->m_flAirResistance > 0 )
            {
                p->m_vecVelocity *= expf( -p->m_flAirResistance * 0.02f * timeDelta );
            }

            Vector vecOldPos = pRaw->m_Pos;
            pRaw->m_Pos += p->m_vecVelocity * timeDelta;

            // Collision
            if ( p->m_bCollide && vecOldPos != pRaw->m_Pos )
            {
                trace_t tr;
                UTIL_TraceLine( vecOldPos, pRaw->m_Pos, MASK_SOLID_BRUSHONLY, NULL, COLLISION_GROUP_NONE, &tr );

                if ( tr.fraction < 1.0f && !tr.allsolid )
                {
                    pRaw->m_Pos = tr.endpos + tr.plane.normal * 0.5f;

                    float flDot = DotProduct( p->m_vecVelocity, tr.plane.normal );
                    p->m_vecVelocity = ( p->m_vecVelocity - tr.plane.normal * ( 2.0f * flDot ) ) * p->m_flBounce;

                    if ( p->m_nCollideRef != LUA_NOREF && CanCallLua() )
                    {
                        lua_rawgeti( m_pL, LUA_REGISTRYINDEX, p->m_nCollideRef );
                        PushParticle( m_pL, p );
                        lua_pushvector( m_pL, tr.endpos );
                        lua_pushvector( m_pL, tr.plane.normal );
                        luasrc_pcall( m_pL, 3, 0 );
                    }
                }
            }

            p->m_flRoll += p->m_flRollDelta * timeDelta;
            p->m_angAngles += p->m_angAngleVelocity * timeDelta;

            p->m_flLifetime += timeDelta;

            if ( p->m_flLifetime >= p->m_flDieTime )
                pIterator->RemoveParticle( pRaw );

            pRaw = ( LuaRawParticle * )pIterator->GetNext();
        }
    }

    virtual void RenderParticles( CParticleRenderIterator *pIterator )
    {
        const LuaRawParticle *pRaw = ( const LuaRawParticle * )pIterator->GetFirst();
        while ( pRaw )
        {
            Vector tPos;
            TransformParticle( ParticleMgr()->GetModelView(), pRaw->m_Pos, tPos );
            float sortKey = ( int )tPos.z;

            if ( !m_bNoDraw && pRaw->m_pLua )
                RenderLuaParticle( pIterator->GetParticleDraw(), pRaw->m_pLua, tPos );

            pRaw = ( const LuaRawParticle * )pIterator->GetNext( sortKey );
        }
    }

    void RenderLuaParticle( ParticleDraw *pDraw, const CLuaParticle *p, const Vector &tPos )
    {
        float flFrac = p->m_flDieTime > 0 ? clamp( p->m_flLifetime / p->m_flDieTime, 0.0f, 1.0f ) : 1.0f;
        float flAlpha = Lerp( flFrac, p->m_flStartAlpha, p->m_flEndAlpha ) / 255.0f;
        float flSize = Lerp( flFrac, p->m_flStartSize, p->m_flEndSize );

        flAlpha *= GetAlphaDistanceFade( tPos, m_flNearClipMin, m_flNearClipMax );

        if ( flAlpha <= 0.001f || flSize <= 0.0f )
            return;

        Vector color = p->m_vecColor * ( 1.0f / 255.0f );

        if ( p->m_bLighting )
        {
            Vector light = engine->GetLightForPoint( p->m_pRaw->m_Pos, true );
            color.x *= MIN( light.x, 1.0f );
            color.y *= MIN( light.y, 1.0f );
            color.z *= MIN( light.z, 1.0f );
        }

        color.x = clamp( color.x, 0.0f, 1.0f );
        color.y = clamp( color.y, 0.0f, 1.0f );
        color.z = clamp( color.z, 0.0f, 1.0f );

        CMeshBuilder *pBuilder = pDraw->GetMeshBuilder();
        if ( !pBuilder )
            return;

        // Stretched along the velocity (rain, sparks, tracers)
        float flLength = Lerp( flFrac, p->m_flStartLength, p->m_flEndLength );
        if ( flLength > 0.0f || p->m_bVelocityScale )
        {
            Vector vecDir = p->m_vecVelocity;
            float flSpeed = VectorNormalize( vecDir );

            if ( p->m_bVelocityScale )
                flLength = ( flLength > 0.0f ? flLength : 1.0f ) * flSpeed * 0.01f;

            if ( flSpeed > 0.0f && flLength > 0.0f )
            {
                Vector vecTail;
                TransformParticle( ParticleMgr()->GetModelView(), p->m_pRaw->m_Pos - vecDir * flLength, vecTail );
                Vector vecDelta = tPos - vecTail;
                float flColor[4] = { color.x, color.y, color.z, flAlpha };
                Tracer_Draw( pBuilder, vecTail, vecDelta, flSize, flColor );
                return;
            }
        }

        if ( m_b3D )
        {
            Vector vecForward, vecRight, vecUp;
            AngleVectors( p->m_angAngles, &vecForward, &vecRight, &vecUp );

            const VMatrix &modelView = ParticleMgr()->GetModelView();
            Vector corners[4];
            TransformParticle( modelView, p->m_pRaw->m_Pos + ( -vecRight + vecUp ) * flSize, corners[0] );
            TransformParticle( modelView, p->m_pRaw->m_Pos + ( -vecRight - vecUp ) * flSize, corners[1] );
            TransformParticle( modelView, p->m_pRaw->m_Pos + ( vecRight - vecUp ) * flSize, corners[2] );
            TransformParticle( modelView, p->m_pRaw->m_Pos + ( vecRight + vecUp ) * flSize, corners[3] );

            unsigned char ubColor[4];
            ubColor[0] = ( unsigned char )RoundFloatToInt( color.x * 254.9f );
            ubColor[1] = ( unsigned char )RoundFloatToInt( color.y * 254.9f );
            ubColor[2] = ( unsigned char )RoundFloatToInt( color.z * 254.9f );
            ubColor[3] = ( unsigned char )RoundFloatToInt( flAlpha * 254.9f );

            const float *mins = pDraw->m_pSubTexture->m_tCoordMins;
            const float *maxs = pDraw->m_pSubTexture->m_tCoordMaxs;
            float tc[4][2] = { { mins[0], mins[1] }, { mins[0], maxs[1] }, { maxs[0], maxs[1] }, { maxs[0], mins[1] } };

            for ( int i = 0; i < 4; i++ )
            {
                pBuilder->Position3fv( corners[i].Base() );
                pBuilder->Color4ubv( ubColor );
                pBuilder->TexCoord2f( 0, tc[i][0], tc[i][1] );
                pBuilder->AdvanceVertex();
            }

            return;
        }

        RenderParticle_ColorSizeAngle( pDraw, tPos, color, flAlpha, flSize, p->m_flRoll );
    }

    void OnLuaFinish()
    {
        m_bFinished = true;
    }

    LuaEmitterHandle_t *m_pLuaHandle;
    Vector m_vecEmitterPos;
    bool m_b3D;
    bool m_bNoDraw;
    bool m_bFinished;

protected:
    CLuaEmitter( lua_State *pState, const Vector &pos, bool b3D )
        : CSimpleEmitter( "LuaEmitter" )
    {
        m_pL = pState;
        m_nStateSerial = s_nLuaRenderExtStateSerial;
        m_vecEmitterPos = pos;
        m_b3D = b3D;
        m_bNoDraw = false;
        m_bFinished = false;
        m_pFirstLuaParticle = NULL;
        m_pLuaHandle = NULL;
        SetSortOrigin( pos );
    }

private:
    lua_State *m_pL;
    int m_nStateSerial;
    CLuaParticle *m_pFirstLuaParticle;
};

//-----------------------------------------------------------------------------
// CLuaParticle bindings
//-----------------------------------------------------------------------------
LUA_REGISTRATION_INIT( LuaParticle );

void CLuaEmitter::PushParticle( lua_State *pState, CLuaParticle *p )
{
    if ( p->m_pHandle )
    {
        lua_rawgetp( pState, LUA_REGISTRYINDEX, p );

        if ( lua_isuserdata( pState, -1 ) )
            return;

        lua_pop( pState, 1 );
        p->m_pHandle = NULL;
    }

    LuaParticleHandle_t *pHandle = ( LuaParticleHandle_t * )lua_newuserdata( pState, sizeof( LuaParticleHandle_t ) );
    pHandle->pParticle = p;
    LUA_SAFE_SET_METATABLE( pState, LUA_LUAPARTICLEMETANAME );

    // The registry keeps the handle (and so the same Lua object) alive while the
    // particle lives; DetachParticle removes the entry when the particle dies.
    p->m_pHandle = pHandle;
    lua_pushvalue( pState, -1 );
    lua_rawsetp( pState, LUA_REGISTRYINDEX, p );
}

// Returns NULL (without erroring) for a particle that has died; GMod is lenient here too
static CLuaParticle *LuaParticle_Get( lua_State *L, int idx )
{
    LuaParticleHandle_t *pHandle = ( LuaParticleHandle_t * )luaL_checkudata( L, idx, LUA_LUAPARTICLEMETANAME );
    return pHandle->pParticle;
}

#define LUA_PARTICLE_SET_FLOAT( Name, Field )                                                  \
    LUA_BINDING_BEGIN( LuaParticle, Set##Name, "class", "Sets " #Name ".", "client" )          \
    {                                                                                          \
        CLuaParticle *p = LuaParticle_Get( L, 1 );                                             \
        if ( p )                                                                               \
            p->Field = ( float )luaL_checknumber( L, 2 );                                      \
        return 0;                                                                              \
    }                                                                                          \
    LUA_BINDING_END()                                                                          \
    LUA_BINDING_BEGIN( LuaParticle, Get##Name, "class", "Gets " #Name ".", "client" )          \
    {                                                                                          \
        CLuaParticle *p = LuaParticle_Get( L, 1 );                                             \
        lua_pushnumber( L, p ? p->Field : 0 );                                                 \
        return 1;                                                                              \
    }                                                                                          \
    LUA_BINDING_END( "number", #Name )

#define LUA_PARTICLE_SET_VECTOR( Name, Field )                                                 \
    LUA_BINDING_BEGIN( LuaParticle, Set##Name, "class", "Sets " #Name ".", "client" )          \
    {                                                                                          \
        CLuaParticle *p = LuaParticle_Get( L, 1 );                                             \
        if ( p )                                                                               \
            p->Field = luaL_checkvector( L, 2 );                                               \
        return 0;                                                                              \
    }                                                                                          \
    LUA_BINDING_END()                                                                          \
    LUA_BINDING_BEGIN( LuaParticle, Get##Name, "class", "Gets " #Name ".", "client" )          \
    {                                                                                          \
        CLuaParticle *p = LuaParticle_Get( L, 1 );                                             \
        lua_pushvector( L, p ? p->Field : vec3_origin );                                       \
        return 1;                                                                              \
    }                                                                                          \
    LUA_BINDING_END( "Vector", #Name )

#define LUA_PARTICLE_SET_ANGLE( Name, Field )                                                  \
    LUA_BINDING_BEGIN( LuaParticle, Set##Name, "class", "Sets " #Name ".", "client" )          \
    {                                                                                          \
        CLuaParticle *p = LuaParticle_Get( L, 1 );                                             \
        if ( p )                                                                               \
            p->Field = luaL_checkangle( L, 2 );                                                \
        return 0;                                                                              \
    }                                                                                          \
    LUA_BINDING_END()                                                                          \
    LUA_BINDING_BEGIN( LuaParticle, Get##Name, "class", "Gets " #Name ".", "client" )          \
    {                                                                                          \
        CLuaParticle *p = LuaParticle_Get( L, 1 );                                             \
        lua_pushangle( L, p ? p->Field : vec3_angle );                                         \
        return 1;                                                                              \
    }                                                                                          \
    LUA_BINDING_END( "Angle", #Name )

#define LUA_PARTICLE_SET_BOOL( Name, Field )                                                   \
    LUA_BINDING_BEGIN( LuaParticle, Set##Name, "class", "Sets " #Name ".", "client" )          \
    {                                                                                          \
        CLuaParticle *p = LuaParticle_Get( L, 1 );                                             \
        if ( p )                                                                               \
            p->Field = lua_toboolean( L, 2 ) != 0;                                             \
        return 0;                                                                              \
    }                                                                                          \
    LUA_BINDING_END()                                                                          \
    LUA_BINDING_BEGIN( LuaParticle, Get##Name, "class", "Gets " #Name ".", "client" )          \
    {                                                                                          \
        CLuaParticle *p = LuaParticle_Get( L, 1 );                                             \
        lua_pushboolean( L, p ? p->Field : false );                                            \
        return 1;                                                                              \
    }                                                                                          \
    LUA_BINDING_END( "boolean", #Name )

LUA_PARTICLE_SET_FLOAT( AirResistance, m_flAirResistance )
LUA_PARTICLE_SET_FLOAT( Bounce, m_flBounce )
LUA_PARTICLE_SET_FLOAT( DieTime, m_flDieTime )
LUA_PARTICLE_SET_FLOAT( LifeTime, m_flLifetime )
LUA_PARTICLE_SET_FLOAT( StartAlpha, m_flStartAlpha )
LUA_PARTICLE_SET_FLOAT( EndAlpha, m_flEndAlpha )
LUA_PARTICLE_SET_FLOAT( StartSize, m_flStartSize )
LUA_PARTICLE_SET_FLOAT( EndSize, m_flEndSize )
LUA_PARTICLE_SET_FLOAT( StartLength, m_flStartLength )
LUA_PARTICLE_SET_FLOAT( EndLength, m_flEndLength )
LUA_PARTICLE_SET_FLOAT( Roll, m_flRoll )
LUA_PARTICLE_SET_FLOAT( RollDelta, m_flRollDelta )
LUA_PARTICLE_SET_FLOAT( NextThink, m_flNextThink )
LUA_PARTICLE_SET_VECTOR( Velocity, m_vecVelocity )
LUA_PARTICLE_SET_VECTOR( Gravity, m_vecGravity )
LUA_PARTICLE_SET_VECTOR( Pos, m_pRaw->m_Pos )
LUA_PARTICLE_SET_ANGLE( Angles, m_angAngles )
LUA_PARTICLE_SET_ANGLE( AngleVelocity, m_angAngleVelocity )
LUA_PARTICLE_SET_BOOL( Collide, m_bCollide )
LUA_PARTICLE_SET_BOOL( Lighting, m_bLighting )
LUA_PARTICLE_SET_BOOL( VelocityScale, m_bVelocityScale )

LUA_BINDING_BEGIN( LuaParticle, SetColor, "class", "Sets the color (r, g, b 0-255).", "client" )
{
    CLuaParticle *p = LuaParticle_Get( L, 1 );

    if ( !p )
        return 0;

    if ( lua_iscolor( L, 2 ) )
    {
        lua_Color &c = luaL_checkcolor( L, 2 );
        p->m_vecColor.Init( c.r(), c.g(), c.b() );
        return 0;
    }

    p->m_vecColor.Init( luaL_optnumber( L, 2, 255 ), luaL_optnumber( L, 3, 255 ), luaL_optnumber( L, 4, 255 ) );
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( LuaParticle, GetColor, "class", "Gets the color (r, g, b).", "client" )
{
    CLuaParticle *p = LuaParticle_Get( L, 1 );
    Vector color = p ? p->m_vecColor : Vector( 255, 255, 255 );
    lua_pushnumber( L, color.x );
    lua_pushnumber( L, color.y );
    lua_pushnumber( L, color.z );
    return 3;
}
LUA_BINDING_END( "number", "r, g, b" )

LUA_BINDING_BEGIN( LuaParticle, SetThinkFunction, "class", "Sets a function called when the particle thinks (see SetNextThink).", "client" )
{
    CLuaParticle *p = LuaParticle_Get( L, 1 );

    if ( p && p->m_pEmitter )
        p->m_pEmitter->SetLuaRef( p->m_nThinkRef, L, 2 );

    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( LuaParticle, SetCollideCallback, "class", "Sets a function called when the particle collides (particle, hitPos, hitNormal).", "client" )
{
    CLuaParticle *p = LuaParticle_Get( L, 1 );

    if ( p && p->m_pEmitter )
        p->m_pEmitter->SetLuaRef( p->m_nCollideRef, L, 2 );

    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( LuaParticle, SetMaterial, "class", "Not supported after creation; kept for compatibility.", "client" )
{
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( LuaParticle, GetMaterial, "class", "Gets the material.", "client" )
{
    CLuaParticle *p = LuaParticle_Get( L, 1 );

    if ( !p )
    {
        lua_pushnil( L );
        return 1;
    }

    lua_pushmaterial( L, materials->FindMaterial( p->m_szMaterial, TEXTURE_GROUP_CLIENT_EFFECTS ) );
    return 1;
}
LUA_BINDING_END( "Material", "The material." )

LUA_BINDING_BEGIN( LuaParticle, IsValid, "class", "Whether the particle is still alive.", "client" )
{
    lua_pushboolean( L, LuaParticle_Get( L, 1 ) != NULL );
    return 1;
}
LUA_BINDING_END( "boolean", "Is valid." )

LUA_BINDING_BEGIN( LuaParticle, __gc, "class", "Releases the handle.", "client" )
{
    LuaParticleHandle_t *pHandle = ( LuaParticleHandle_t * )luaL_checkudata( L, 1, LUA_LUAPARTICLEMETANAME );

    if ( pHandle->pParticle )
    {
        pHandle->pParticle->m_pHandle = NULL;
        pHandle->pParticle = NULL;
    }

    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( LuaParticle, __tostring, "class", "String representation.", "client" )
{
    lua_pushfstring( L, "CLuaParticle: %p", LuaParticle_Get( L, 1 ) );
    return 1;
}
LUA_BINDING_END( "string", "The string." )

//-----------------------------------------------------------------------------
// CLuaEmitter bindings
//-----------------------------------------------------------------------------
LUA_REGISTRATION_INIT( LuaEmitter );

static CLuaEmitter *LuaEmitter_Get( lua_State *L, int idx, bool bError = true )
{
    LuaEmitterHandle_t *pHandle = ( LuaEmitterHandle_t * )luaL_checkudata( L, idx, LUA_LUAEMITTERMETANAME );

    if ( !pHandle->pEmitter || !pHandle->pEmitter->IsValid() )
    {
        if ( bError )
            luaL_error( L, "Tried to use a finished ParticleEmitter" );

        return NULL;
    }

    return pHandle->pEmitter->GetObject();
}

static void LuaEmitter_Release( LuaEmitterHandle_t *pHandle )
{
    if ( pHandle->pEmitter )
    {
        CSmartPtr< CLuaEmitter > *pSmartPtr = pHandle->pEmitter;
        pHandle->pEmitter = NULL;

        if ( pSmartPtr->IsValid() )
        {
            pSmartPtr->GetObject()->OnLuaFinish();
            pSmartPtr->GetObject()->m_pLuaHandle = NULL;
        }

        delete pSmartPtr;  // drops our reference; particles keep living
    }
}

LUA_BINDING_BEGIN( LuaEmitter, Add, "class", "Adds a particle (material, position). Returns nil when the particle limit is reached.", "client" )
{
    CLuaEmitter *pEmitter = LuaEmitter_Get( L, 1 );
    const char *pszMaterial = NULL;

    if ( lua_isstring( L, 2 ) )
        pszMaterial = lua_tostring( L, 2 );
    else
        pszMaterial = luaL_checkmaterial( L, 2 )->GetName();

    Vector pos = luaL_checkvector( L, 3 );
    CLuaParticle *p = pEmitter->AddLuaParticle( pszMaterial, pos );

    if ( !p )
    {
        lua_pushnil( L );
        return 1;
    }

    pEmitter->PushParticle( L, p );
    return 1;
}
LUA_BINDING_END( "CLuaParticle", "The particle, or nil." )

LUA_BINDING_BEGIN( LuaEmitter, Finish, "class", "Stops the emitter from being used; existing particles live on.", "client" )
{
    LuaEmitter_Release( ( LuaEmitterHandle_t * )luaL_checkudata( L, 1, LUA_LUAEMITTERMETANAME ) );
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( LuaEmitter, Draw, "class", "Particles are drawn by the engine; kept for compatibility.", "client" )
{
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( LuaEmitter, GetNumActiveParticles, "class", "Number of active particles.", "client" )
{
    CLuaEmitter *pEmitter = LuaEmitter_Get( L, 1, false );
    lua_pushinteger( L, pEmitter ? pEmitter->GetBinding().GetNumActiveParticles() : 0 );
    return 1;
}
LUA_BINDING_END( "integer", "The count." )

LUA_BINDING_BEGIN( LuaEmitter, GetPos, "class", "Gets the emitter position.", "client" )
{
    CLuaEmitter *pEmitter = LuaEmitter_Get( L, 1 );
    lua_pushvector( L, pEmitter->m_vecEmitterPos );
    return 1;
}
LUA_BINDING_END( "Vector", "The position." )

LUA_BINDING_BEGIN( LuaEmitter, SetPos, "class", "Sets the emitter position.", "client" )
{
    CLuaEmitter *pEmitter = LuaEmitter_Get( L, 1 );
    pEmitter->m_vecEmitterPos = luaL_checkvector( L, 2 );
    pEmitter->SetSortOrigin( pEmitter->m_vecEmitterPos );
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( LuaEmitter, Is3D, "class", "Whether the emitter makes 3D (oriented) particles.", "client" )
{
    CLuaEmitter *pEmitter = LuaEmitter_Get( L, 1 );
    lua_pushboolean( L, pEmitter->m_b3D );
    return 1;
}
LUA_BINDING_END( "boolean", "Is 3D." )

LUA_BINDING_BEGIN( LuaEmitter, IsValid, "class", "Whether the emitter can still be used.", "client" )
{
    lua_pushboolean( L, LuaEmitter_Get( L, 1, false ) != NULL );
    return 1;
}
LUA_BINDING_END( "boolean", "Is valid." )

LUA_BINDING_BEGIN( LuaEmitter, SetBBox, "class", "Sets the bounding box (mins, maxs).", "client" )
{
    CLuaEmitter *pEmitter = LuaEmitter_Get( L, 1 );
    pEmitter->GetBinding().SetBBox( luaL_checkvector( L, 2 ), luaL_checkvector( L, 3 ) );
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( LuaEmitter, SetNearClip, "class", "Fades particles out close to the camera (min, max distance).", "client" )
{
    CLuaEmitter *pEmitter = LuaEmitter_Get( L, 1 );
    pEmitter->SetNearClip( luaL_checknumber( L, 2 ), luaL_checknumber( L, 3 ) );
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( LuaEmitter, SetNoDraw, "class", "Stops drawing the particles.", "client" )
{
    CLuaEmitter *pEmitter = LuaEmitter_Get( L, 1 );
    pEmitter->m_bNoDraw = lua_toboolean( L, 2 ) != 0;
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( LuaEmitter, SetParticleCullRadius, "class", "Sets the particle cull radius.", "client" )
{
    CLuaEmitter *pEmitter = LuaEmitter_Get( L, 1 );
    pEmitter->SetParticleCullRadius( luaL_checknumber( L, 2 ) );
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( LuaEmitter, __gc, "class", "Releases the emitter.", "client" )
{
    LuaEmitter_Release( ( LuaEmitterHandle_t * )luaL_checkudata( L, 1, LUA_LUAEMITTERMETANAME ) );
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( LuaEmitter, __tostring, "class", "String representation.", "client" )
{
    lua_pushfstring( L, "CLuaEmitter: %p", LuaEmitter_Get( L, 1, false ) );
    return 1;
}
LUA_BINDING_END( "string", "The string." )

LUA_REGISTRATION_INIT( ParticleEmitters );

LUA_BINDING_BEGIN( ParticleEmitters, Create, "library", "Creates a particle emitter (GMod's ParticleEmitter(position, use3D)).", "client" )
{
    Vector pos = luaL_checkvector( L, 1 );
    bool b3D = lua_toboolean( L, 2 ) != 0;

    LuaRenderExt_EnsureSentinel( L );

    LuaEmitterHandle_t *pHandle = ( LuaEmitterHandle_t * )lua_newuserdata( L, sizeof( LuaEmitterHandle_t ) );
    pHandle->pEmitter = new CSmartPtr< CLuaEmitter >( CLuaEmitter::Create( L, pos, b3D ) );
    pHandle->pEmitter->GetObject()->m_pLuaHandle = pHandle;
    LUA_SAFE_SET_METATABLE( L, LUA_LUAEMITTERMETANAME );
    return 1;
}
LUA_BINDING_END( "CLuaEmitter", "The emitter." )

//=============================================================================
// Sun information (GMod's render.GetSunInfo)
//=============================================================================
LUA_REGISTRATION_INIT( RenderExtensions );

LUA_BINDING_BEGIN( RenderExtensions, GetSunInfo, "library", "Returns { direction, obstruction } for the map's env_sun, or nil.", "client" )
{
    for ( C_BaseEntity *pEntity = ClientEntityList().FirstBaseEntity(); pEntity; pEntity = ClientEntityList().NextBaseEntity( pEntity ) )
    {
        ClientClass *pClass = pEntity->GetClientClass();

        if ( !pClass || V_strcmp( pClass->GetName(), "CSun" ) != 0 )
            continue;

        C_Sun *pSun = static_cast< C_Sun * >( pEntity );

        if ( !pSun->m_bOn )
            continue;

        Vector direction = pSun->m_vDirection;
        VectorNormalize( direction );

        // Obstruction: 1 when the sky is visible in the sun's direction, 0 when blocked
        float obstruction = 0.0f;
        trace_t tr;
        Vector start = MainViewOrigin();
        UTIL_TraceLine( start, start + direction * MAX_TRACE_LENGTH, MASK_OPAQUE, NULL, COLLISION_GROUP_NONE, &tr );

        if ( tr.fraction < 1.0f && ( tr.surface.flags & SURF_SKY ) )
            obstruction = 1.0f;

        lua_newtable( L );
        lua_pushvector( L, direction );
        lua_setfield( L, -2, "direction" );
        lua_pushnumber( L, obstruction );
        lua_setfield( L, -2, "obstruction" );
        return 1;
    }

    lua_pushnil( L );
    return 1;
}
LUA_BINDING_END( "table", "The sun info, or nil." )

//=============================================================================
// Render state overrides GMod exposes on 'render'
//=============================================================================
LUA_BINDING_BEGIN( RenderExtensions, MaterialOverride, "library", "Forces all models to draw with a material (nil to reset).", "client" )
{
    IMaterial *pMaterial = lua_isnoneornil( L, 1 ) ? NULL : luaL_checkmaterial( L, 1 );
    modelrender->ForcedMaterialOverride( pMaterial );
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( RenderExtensions, OverrideDepthEnable, "library", "Overrides depth writing (enable override, write depth).", "client" )
{
    CMatRenderContextPtr pRenderContext( materials );
    pRenderContext->OverrideDepthEnable( lua_toboolean( L, 1 ) != 0, lua_toboolean( L, 2 ) != 0 );
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( RenderExtensions, OverrideAlphaWriteEnable, "library", "Overrides alpha writing (enable override, write alpha).", "client" )
{
    CMatRenderContextPtr pRenderContext( materials );
    pRenderContext->OverrideAlphaWriteEnable( lua_toboolean( L, 1 ) != 0, lua_toboolean( L, 2 ) != 0 );
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( RenderExtensions, OverrideColorWriteEnable, "library", "Overrides color writing (enable override, write color).", "client" )
{
    CMatRenderContextPtr pRenderContext( materials );
    pRenderContext->OverrideColorWriteEnable( lua_toboolean( L, 1 ) != 0, lua_toboolean( L, 2 ) != 0 );
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( RenderExtensions, FogMode, "library", "Sets the fog mode (MATERIAL_FOG_*).", "client" )
{
    CMatRenderContextPtr pRenderContext( materials );
    pRenderContext->FogMode( ( MaterialFogMode_t )( int )luaL_checknumber( L, 1 ) );
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( RenderExtensions, GetFogMode, "library", "Gets the fog mode.", "client" )
{
    CMatRenderContextPtr pRenderContext( materials );
    lua_pushinteger( L, pRenderContext->GetFogMode() );
    return 1;
}
LUA_BINDING_END( "integer", "The fog mode." )

LUA_BINDING_BEGIN( RenderExtensions, FogStart, "library", "Sets the fog start distance.", "client" )
{
    CMatRenderContextPtr pRenderContext( materials );
    pRenderContext->FogStart( luaL_checknumber( L, 1 ) );
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( RenderExtensions, FogEnd, "library", "Sets the fog end distance.", "client" )
{
    CMatRenderContextPtr pRenderContext( materials );
    pRenderContext->FogEnd( luaL_checknumber( L, 1 ) );
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( RenderExtensions, FogColor, "library", "Sets the fog color (0-255).", "client" )
{
    CMatRenderContextPtr pRenderContext( materials );
    pRenderContext->FogColor3ub(
        ( unsigned char )clamp( ( int )luaL_checknumber( L, 1 ), 0, 255 ),
        ( unsigned char )clamp( ( int )luaL_checknumber( L, 2 ), 0, 255 ),
        ( unsigned char )clamp( ( int )luaL_checknumber( L, 3 ), 0, 255 ) );
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( RenderExtensions, FogMaxDensity, "library", "Sets the fog's maximum density (0-1).", "client" )
{
    CMatRenderContextPtr pRenderContext( materials );
    pRenderContext->FogMaxDensity( luaL_checknumber( L, 1 ) );
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( RenderExtensions, SetViewPort, "library", "Sets the viewport (x, y, width, height).", "client" )
{
    CMatRenderContextPtr pRenderContext( materials );
    pRenderContext->Viewport( ( int )luaL_checknumber( L, 1 ), ( int )luaL_checknumber( L, 2 ), ( int )luaL_checknumber( L, 3 ), ( int )luaL_checknumber( L, 4 ) );
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( RenderExtensions, GetDXLevel, "library", "Gets the DirectX support level (e.g. 95).", "client" )
{
    lua_pushinteger( L, engine->GetDXSupportLevel() );
    return 1;
}
LUA_BINDING_END( "integer", "The DX level." )

LUA_BINDING_BEGIN( RenderExtensions, GetBlend, "library", "Gets the current blend.", "client" )
{
    lua_pushnumber( L, render->GetBlend() );
    return 1;
}
LUA_BINDING_END( "number", "The blend." )

LUA_BINDING_BEGIN( RenderExtensions, GetColorModulation, "library", "Gets the current color modulation (r, g, b).", "client" )
{
    float color[3];
    render->GetColorModulation( color );
    lua_pushnumber( L, color[0] );
    lua_pushnumber( L, color[1] );
    lua_pushnumber( L, color[2] );
    return 3;
}
LUA_BINDING_END( "number", "r, g, b" )

LUA_BINDING_BEGIN( RenderExtensions, OpenURL, "library", "Opens a web page in the Steam overlay (GMod's gui.OpenURL).", "client" )
{
    const char *pszUrl = luaL_checkstring( L, 1 );

    if ( steamapicontext && steamapicontext->SteamFriends() )
        steamapicontext->SteamFriends()->ActivateGameOverlayToWebPage( pszUrl );
    else
        ConMsg( "OpenURL: %s\n", pszUrl );

    return 0;
}
LUA_BINDING_END()

//-----------------------------------------------------------------------------
// Registration, called from luaopen_render. Leaves the stack as it was.
//-----------------------------------------------------------------------------
static void luaopen_RenderExtensions( lua_State *L )
{
    int top = lua_gettop( L );

    luaL_register( L, "Meshes", Meshes_luaRegistry );
    lua_settop( L, top );

    luaL_register( L, "ParticleEmitters", ParticleEmitters_luaRegistry );
    lua_settop( L, top );

    luaL_register( L, "RenderExtensions", RenderExtensions_luaRegistry );
    lua_settop( L, top );

    const struct
    {
        const char *name;
        CUtlVector< LuaRegEntry > *pRegistry;
    } metatables[] = {
        { LUA_IMESHMETANAME, &LuaMesh_luaRegistry },
        { LUA_LUAEMITTERMETANAME, &LuaEmitter_luaRegistry },
        { LUA_LUAPARTICLEMETANAME, &LuaParticle_luaRegistry },
    };

    for ( int i = 0; i < ARRAYSIZE( metatables ); i++ )
    {
        luaL_newmetatable( L, metatables[i].name );
        luaL_register( L, NULL, *metatables[i].pRegistry );
        lua_pushvalue( L, -1 );
        lua_setfield( L, -2, "__index" );
        lua_pushstring( L, metatables[i].name );
        lua_setfield( L, -2, "__type" );
        lua_settop( L, top );
    }
}

#endif  // CLIENT_DLL && LUA_SDK
