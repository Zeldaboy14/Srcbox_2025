#include "cbase.h"
#include "luamanager.h"
#include "luasrclib.h"
#include "lbaseentity_shared.h"
#include "lbaseplayer_shared.h"
#include "lgametrace.h"
#include "mathlib/lvector.h"
#include "collisionutils.h"
#include "util_shared.h"
#include "datacache/imdlcache.h"
#include "tier3/tier3.h"
#include "base64.h"
#include "decals.h"
#include "itempents.h"

#include "particle_parse.h"
#include "ammodef.h"
#ifdef CLIENT_DLL
#include "particles_new.h"
#include "particle_property.h"
#include "c_baseanimatingoverlay.h"
#include "cdll_util.h"
#include "c_recipientfilter.h"
#include "iefx.h"
#include "dlight.h"
#else
#include "recipientfilter.h"
#include "BaseAnimatingOverlay.h"
#include "spritetrail.h"
#include "gamerules.h"
#ifdef HL2MP
#include "hl2mp_gamerules.h"
#endif
#endif

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

LUA_REGISTRATION_INIT( Utilities )

LUA_BINDING_BEGIN( Utilities, AngleDifference, "library", "Compute the difference between two angles" )
{
    float srcAngle = LUA_BINDING_ARGUMENT( luaL_checknumber, 1, "srcAngle" );
    float dstAngle = LUA_BINDING_ARGUMENT( luaL_checknumber, 2, "dstAngle" );
    lua_pushnumber( L, UTIL_AngleDiff( srcAngle, dstAngle ) );
    return 1;
}
LUA_BINDING_END( "number", "Difference between the angles." )

LUA_BINDING_BEGIN( Utilities, CreateBubbles, "library", "Create bubbles at the specified position and range" )
{
    Vector minPos = LUA_BINDING_ARGUMENT( luaL_checkvector, 1, "minPos" );
    Vector maxPos = LUA_BINDING_ARGUMENT( luaL_checkvector, 2, "maxPos" );
    int count = LUA_BINDING_ARGUMENT( luaL_checknumber, 3, "count" );
    UTIL_Bubbles( minPos, maxPos, count );
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( Utilities, ShakeScreen, "library", "Apply screen shake effect" )
{
    Vector center = LUA_BINDING_ARGUMENT( luaL_checkvector, 1, "center" );
    float amplitude = LUA_BINDING_ARGUMENT( luaL_checknumber, 2, "amplitude" );
    float frequency = LUA_BINDING_ARGUMENT( luaL_checknumber, 3, "frequency" );
    float duration = LUA_BINDING_ARGUMENT( luaL_checknumber, 4, "duration" );
    float radius = LUA_BINDING_ARGUMENT( luaL_checknumber, 5, "radius" );
    ShakeCommand_t command = LUA_BINDING_ARGUMENT_ENUM( ShakeCommand_t, 6, "command" );
    bool localAmplitude = LUA_BINDING_ARGUMENT_WITH_DEFAULT( luaL_optboolean, 7, false, "localAmplitude" );
    UTIL_ScreenShake( center, amplitude, frequency, duration, radius, command, localAmplitude );
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( Utilities, CreateTracer, "library", "Create a tracer from the specified start to end position" )
{
    Vector startPos = LUA_BINDING_ARGUMENT( luaL_checkvector, 1, "startPos" );
    Vector endPos = LUA_BINDING_ARGUMENT( luaL_checkvector, 2, "endPos" );
    int life = LUA_BINDING_ARGUMENT( luaL_checknumber, 3, "life" );
    int width = LUA_BINDING_ARGUMENT( luaL_checknumber, 4, "width" );
    int brightness = LUA_BINDING_ARGUMENT( luaL_checknumber, 5, "brightness" );
    bool noise = LUA_BINDING_ARGUMENT( luaL_checkboolean, 6, "noise" );
    const char *modelname = LUA_BINDING_ARGUMENT( luaL_checkstring, 7, "modelname" );
    UTIL_Tracer( startPos, endPos, life, width, brightness, noise, modelname );
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( Utilities, CreateSmoke, "library", "Create smoke effect at a position" )
{
    Vector position = LUA_BINDING_ARGUMENT( luaL_checkvector, 1, "position" );
    int modelIndex = LUA_BINDING_ARGUMENT( luaL_checknumber, 2, "modelIndex" );
    float scale = LUA_BINDING_ARGUMENT( luaL_checknumber, 3, "scale" );
    UTIL_Smoke( position, modelIndex, scale );
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( Utilities, ImpactTrace, "library", "Generate an impact trace" )
{
    trace_t *trace = &LUA_BINDING_ARGUMENT( luaL_checktrace, 1, "trace" );
    int damageType = LUA_BINDING_ARGUMENT( luaL_checknumber, 2, "damageType" );
    char *customImpactName = ( char * )LUA_BINDING_ARGUMENT_WITH_DEFAULT( luaL_optstring, 3, 0, "customImpactName" );
    UTIL_ImpactTrace( trace, damageType, customImpactName );
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( Utilities, SetEntityOrigin, "library", "Set the origin for an entity" )
{
    CBaseEntity *entity = LUA_BINDING_ARGUMENT( luaL_checkentity, 1, "entity" );
    Vector origin = LUA_BINDING_ARGUMENT( luaL_checkvector, 2, "origin" );
    UTIL_SetOrigin( entity, origin );
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( Utilities, PrecacheOther, "library", "Precache an entity by class name" )
{
    const char *className = LUA_BINDING_ARGUMENT( luaL_checkstring, 1, "className" );
    UTIL_PrecacheOther( className );
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( Utilities, BoundToWorldSize, "library", "Bound the vector to the world size" )
{
    Vector *vec = &LUA_BINDING_ARGUMENT( luaL_checkvector, 1, "vec" );
    UTIL_BoundToWorldSize( vec );
    return 0;
}
LUA_BINDING_END()

// TODO: Test if this function works as expected
LUA_BINDING_BEGIN( Utilities, IntersectRayWithObb, "library", "Intersect a ray with an oriented bounding box" )
{
    Vector vecRayStart = LUA_BINDING_ARGUMENT( luaL_checkvector, 1, "vecRayStart" );
    Vector vecRayDelta = LUA_BINDING_ARGUMENT( luaL_checkvector, 2, "vecRayDelta" );
    Vector vecBoxOrigin = LUA_BINDING_ARGUMENT( luaL_checkvector, 3, "vecBoxOrigin" );
    QAngle angBoxAngles = LUA_BINDING_ARGUMENT( luaL_checkangle, 4, "angBoxAngles" );
    Vector vecBoxMins = LUA_BINDING_ARGUMENT( luaL_checkvector, 5, "vecBoxMins" );
    Vector vecBoxMaxs = LUA_BINDING_ARGUMENT( luaL_checkvector, 6, "vecBoxMaxs" );

    matrix3x4_t matObbToWorld;
    AngleMatrix( angBoxAngles, vecBoxOrigin, matObbToWorld );

    CBaseTrace tr;
    if ( IntersectRayWithOBB( vecRayStart, vecRayDelta, matObbToWorld, vecBoxMins, vecBoxMaxs, 0.0f, &tr ) )
    {
        lua_pushvector( L, tr.endpos );
        lua_pushvector( L, tr.plane.normal );
        lua_pushnumber( L, tr.fraction );
    }
    else
    {
        lua_pushnil( L );
        lua_pushnil( L );
        lua_pushnil( L );
    }

    return 3;
}
LUA_BINDING_END( "Vector", "End position of the intersection", "Vector", "Normal of the intersection", "number", "Fraction of the intersection" )

LUA_BINDING_BEGIN( Utilities, IntersectRayWithPlane, "library", "Intersect a ray with a plane" )
{
    Vector vecRayOrigin = LUA_BINDING_ARGUMENT( luaL_checkvector, 1, "vecRayOrigin" );
    Vector vecRayDirection = LUA_BINDING_ARGUMENT( luaL_checkvector, 2, "vecRayDirection" );
    Vector vecPlaneOrigin = LUA_BINDING_ARGUMENT( luaL_checkvector, 3, "vecPlaneOrigin" );
    Vector vecPlaneNormal = LUA_BINDING_ARGUMENT( luaL_checkvector, 4, "vecPlaneNormal" );

    float flDot = DotProduct( vecPlaneNormal, vecRayDirection );

    // If the ray is parallel to the plane, there is no intersection.
    if ( flDot == 0.0f )
    {
        lua_pushnil( L );
        return 1;
    }

    float flDistance = DotProduct( vecPlaneNormal, vecPlaneOrigin ) - DotProduct( vecRayOrigin, vecPlaneNormal );

    Vector vecHitPos = vecRayOrigin + vecRayDirection * ( flDistance / flDot );

    lua_pushvector( L, vecHitPos );

    return 1;
}
LUA_BINDING_END( "Vector", "Intersection position" )

LUA_BINDING_BEGIN( Utilities, IntersectRayWithSphere, "library", "Intersect a ray with a sphere" )
{
    Vector vecRayOrigin = LUA_BINDING_ARGUMENT( luaL_checkvector, 1, "vecRayOrigin" );
    Vector vecRayDelta = LUA_BINDING_ARGUMENT( luaL_checkvector, 2, "vecRayDelta" );
    Vector vecSphereCenter = LUA_BINDING_ARGUMENT( luaL_checkvector, 3, "vecSphereCenter" );
    float flRadius = LUA_BINDING_ARGUMENT( luaL_checknumber, 4, "flRadius" );

    float t1, t2;
    if ( IntersectRayWithSphere( vecRayOrigin, vecRayDelta, vecSphereCenter, flRadius, &t1, &t2 ) )
    {
        lua_pushnumber( L, t1 );
        lua_pushnumber( L, t2 );
        return 2;
    }

    lua_pushnil( L );
    lua_pushnil( L );
    return 2;
}
LUA_BINDING_END( "number", "First intersection point", "number", "Second intersection point" )

LUA_BINDING_BEGIN( Utilities, IsBoxIntersectingSphere, "library", "Check if a box is intersecting a sphere" )
{
    Vector boxMin = LUA_BINDING_ARGUMENT( luaL_checkvector, 1, "boxMin" );
    Vector boxMax = LUA_BINDING_ARGUMENT( luaL_checkvector, 2, "boxMax" );
    Vector spherePosition = LUA_BINDING_ARGUMENT( luaL_checkvector, 3, "spherePosition" );
    float sphereRadius = LUA_BINDING_ARGUMENT( luaL_checknumber, 4, "sphereRadius" );

    lua_pushboolean( L, IsBoxIntersectingSphere( boxMin, boxMax, spherePosition, sphereRadius ) );
    return 1;
}
LUA_BINDING_END( "boolean", "True if the box is intersecting the sphere" )

LUA_BINDING_BEGIN( Utilities, IsObbIntersectingObb, "library", "Check if an oriented bounding box is intersecting another oriented bounding box" )
{
    Vector vecOrigin1 = LUA_BINDING_ARGUMENT( luaL_checkvector, 1, "vecOrigin1" );
    QAngle vecAngles1 = LUA_BINDING_ARGUMENT( luaL_checkangle, 2, "vecAngles1" );
    Vector boxMin1 = LUA_BINDING_ARGUMENT( luaL_checkvector, 3, "boxMin1" );
    Vector boxMax1 = LUA_BINDING_ARGUMENT( luaL_checkvector, 4, "boxMax1" );

    Vector vecOrigin2 = LUA_BINDING_ARGUMENT( luaL_checkvector, 5, "vecOrigin2" );
    QAngle vecAngles2 = LUA_BINDING_ARGUMENT( luaL_checkangle, 6, "vecAngles2" );
    Vector boxMin2 = LUA_BINDING_ARGUMENT( luaL_checkvector, 7, "boxMin2" );
    Vector boxMax2 = LUA_BINDING_ARGUMENT( luaL_checkvector, 8, "boxMax2" );

    float flTolerance = LUA_BINDING_ARGUMENT_WITH_DEFAULT( luaL_optnumber, 9, 0.0f, "flTolerance" );

    lua_pushboolean( L, IsOBBIntersectingOBB( vecOrigin1, vecAngles1, boxMin1, boxMax1, vecOrigin2, vecAngles2, boxMin2, boxMax2, flTolerance ) );
    return 1;
}
LUA_BINDING_END( "boolean", "True if the oriented bounding boxes are intersecting" )

LUA_BINDING_BEGIN( Utilities, IsSphereIntersectingCone, "library", "Check if a sphere is intersecting a cone" )
{
    Vector sphereCenter = LUA_BINDING_ARGUMENT( luaL_checkvector, 1, "sphereCenter" );
    float sphereRadius = LUA_BINDING_ARGUMENT( luaL_checknumber, 2, "sphereRadius" );
    Vector coneOrigin = LUA_BINDING_ARGUMENT( luaL_checkvector, 3, "coneOrigin" );
    Vector coneAxis = LUA_BINDING_ARGUMENT( luaL_checkvector, 4, "coneAxis" );
    float coneSine = LUA_BINDING_ARGUMENT( luaL_checknumber, 5, "coneSine" );
    float coneCosine = LUA_BINDING_ARGUMENT( luaL_checknumber, 6, "coneCosine" );

    lua_pushboolean( L, IsSphereIntersectingCone( sphereCenter, sphereRadius, coneOrigin, coneAxis, coneSine, coneCosine ) );
    return 1;
}
LUA_BINDING_END( "boolean", "True if the sphere is intersecting the cone" )

LUA_BINDING_BEGIN( Utilities, IsSphereIntersectingSphere, "library", "Check if a sphere is intersecting another sphere" )
{
    Vector sphereCenter1 = LUA_BINDING_ARGUMENT( luaL_checkvector, 1, "sphereCenter1" );
    float sphereRadius1 = LUA_BINDING_ARGUMENT( luaL_checknumber, 2, "sphereRadius1" );
    Vector sphereCenter2 = LUA_BINDING_ARGUMENT( luaL_checkvector, 3, "sphereCenter2" );
    float sphereRadius2 = LUA_BINDING_ARGUMENT( luaL_checknumber, 4, "sphereRadius2" );

    lua_pushboolean( L, IsSphereIntersectingSphere( sphereCenter1, sphereRadius1, sphereCenter2, sphereRadius2 ) );
    return 1;
}
LUA_BINDING_END( "boolean", "True if the spheres are intersecting" )

LUA_BINDING_BEGIN( Utilities, PointOnLineNearestPoint, "library", "Returns the nearest point on a line to a given point." )
{
    Vector point = LUA_BINDING_ARGUMENT( luaL_checkvector, 1, "point" );
    Vector lineStart = LUA_BINDING_ARGUMENT( luaL_checkvector, 2, "lineStart" );
    Vector lineEnd = LUA_BINDING_ARGUMENT( luaL_checkvector, 3, "lineEnd" );
    bool clamp = LUA_BINDING_ARGUMENT_WITH_DEFAULT( luaL_optboolean, 4, false, "clamp" );

    lua_pushvector( L, UTIL_PointOnLineNearestPoint( point, lineStart, lineEnd, clamp ) );
    return 1;
}
LUA_BINDING_END( "Vector", "The nearest point on the line." )

LUA_BINDING_BEGIN( Utilities, IsSpaceEmpty, "library", "Check if the space is empty" )
{
    CBaseEntity *entity = LUA_BINDING_ARGUMENT( luaL_checkentity, 1, "entity" );
    Vector minPos = LUA_BINDING_ARGUMENT( luaL_checkvector, 2, "minPos" );
    Vector maxPos = LUA_BINDING_ARGUMENT( luaL_checkvector, 3, "maxPos" );

    lua_pushboolean( L, UTIL_IsSpaceEmpty( entity, minPos, maxPos ) );
    return 1;
}
LUA_BINDING_END( "boolean", "True if the space is empty" )

LUA_BINDING_BEGIN( Utilities, GetPointWaterLevel, "library", "Get the water level at the specified position" )
{
    Vector position = LUA_BINDING_ARGUMENT( luaL_checkvector, 1, "point" );
    float minz = LUA_BINDING_ARGUMENT( luaL_checknumber, 2, "minz" );
    float maxz = LUA_BINDING_ARGUMENT( luaL_checknumber, 3, "maxz" );

    lua_pushnumber( L, UTIL_WaterLevel( position, minz, maxz ) );
    return 1;
}
LUA_BINDING_END( "number", "Water level at the specified position" )

LUA_BINDING_BEGIN( Utilities, IsLowViolence, "library", "Check if the game is in low violence mode" )
{
    lua_pushboolean( L, UTIL_IsLowViolence() );
    return 1;
}
LUA_BINDING_END( "boolean", "True if the game is in low violence mode" )

LUA_BINDING_BEGIN( Utilities, IsValidModel, "library", "Check if the given string is a valid model" )
{
    const char *pszModel = LUA_BINDING_ARGUMENT( luaL_checkstring, 1, "model" );

    // Invalid if the string starts with space or maps
    if ( pszModel[0] == ' ' || !Q_strncmp( pszModel, "maps", 4 ) )
    {
        lua_pushboolean( L, false );
        return 1;
    }

    // Invalid if the string doesn't start with models
    if ( Q_strncmp( pszModel, "models", 6 ) )
    {
        lua_pushboolean( L, false );
        return 1;
    }

    // Invalid if the string contains any of the following
    if ( Q_strstr( pszModel, "_gestures" ) || Q_strstr( pszModel, "_animations" ) || Q_strstr( pszModel, "_postures" ) || Q_strstr( pszModel, "_gst" ) || Q_strstr( pszModel, "_pst" ) || Q_strstr( pszModel, "_shd" ) || Q_strstr( pszModel, "_ss" ) || Q_strstr( pszModel, "_anm" ) || Q_strstr( pszModel, ".bsp" ) || Q_strstr( pszModel, "cs_fix" ) )
    {
        lua_pushboolean( L, false );
        return 1;
    }

    int iModelIndex = CBaseEntity::PrecacheModel( pszModel );

    if ( iModelIndex == MDLHANDLE_INVALID )
    {
        lua_pushboolean( L, false );
        return 1;
    }

    const model_t *pModel = modelinfo->GetModel( iModelIndex );

    if ( !pModel )
    {
        lua_pushboolean( L, false );
        return 1;
    }

    studiohdr_t *pStudioModel = modelinfo->GetStudiomodel( pModel );

    if ( !pStudioModel || Q_strcmp( pStudioModel->name, "error.mdl" ) == 0 )
    {
        lua_pushboolean( L, false );
        return 1;
    }

    lua_pushboolean( L, true );
    return 1;
}
LUA_BINDING_END( "boolean", "True if the model is valid" )

LUA_BINDING_BEGIN( Utilities, IsValidPhysicsProp, "library", "Check if the given string is a valid physics props" )
{
    const char *pszModel = LUA_BINDING_ARGUMENT( luaL_checkstring, 1, "model" );
    const MDLHandle_t modelHandle = g_pMDLCache->FindMDL( pszModel );

    if ( modelHandle == MDLHANDLE_INVALID )
    {
        lua_pushboolean( L, false );
        return 1;
    }

    studiohdr_t *pStudioModel = g_pMDLCache->GetStudioHdr( modelHandle );

    if ( !pStudioModel )
    {
        lua_pushboolean( L, false );
        return 1;
    }

    if ( !mdlcache->GetVCollide( modelHandle ) )
    {
        lua_pushboolean( L, false );
        return 1;
    }

    lua_pushboolean( L, true );
    return 1;
}
LUA_BINDING_END( "boolean", "True if the model is a valid physics prop" )

LUA_BINDING_BEGIN( Utilities, IsValidRagdoll, "library", "Check if the given string is a valid ragdoll model ( its collision model has more than one solid )." )
{
    const char *pszModel = LUA_BINDING_ARGUMENT( luaL_checkstring, 1, "model" );
    const MDLHandle_t modelHandle = g_pMDLCache->FindMDL( pszModel );

    if ( modelHandle == MDLHANDLE_INVALID )
    {
        lua_pushboolean( L, false );
        return 1;
    }

    studiohdr_t *pStudioModel = g_pMDLCache->GetStudioHdr( modelHandle );

    if ( !pStudioModel || Q_strcmp( pStudioModel->name, "error.mdl" ) == 0 )
    {
        lua_pushboolean( L, false );
        return 1;
    }

    // Srcbox: a ragdoll is a model whose collision model has several solids ( one per
    // physics bone ), like GMod checks. Counting bones made every animated prop a
    // "ragdoll", and props with ragdoll physics count as props too unless this is right.
    vcollide_t *pCollide = mdlcache->GetVCollide( modelHandle );
    lua_pushboolean( L, pCollide && pCollide->solidCount > 1 );
    return 1;
}
LUA_BINDING_END( "boolean", "True if the model is a valid ragdoll" )

LUA_BINDING_BEGIN( Utilities, ShouldShowBlood, "library", "Check if blood of the given color should be shown" )
{
    int bloodColor = LUA_BINDING_ARGUMENT( luaL_checknumber, 1, "bloodColor" );

    lua_pushboolean( L, UTIL_ShouldShowBlood( bloodColor ) );
    return 1;
}
LUA_BINDING_END( "boolean", "True if blood should be shown" )

// Srcbox: decals (GMod's util.Decal / util.DecalEx). The trace starts at 'start'
// and goes to 'end'; the optional 4th argument is an entity to ignore.
static bool LuaDecal_Trace( lua_State *L, trace_t &tr )
{
    Vector vecStart = luaL_checkvector( L, 2 );
    Vector vecEnd = luaL_checkvector( L, 3 );
    CBaseEntity *pIgnore = lua_isnoneornil( L, 4 ) ? NULL : lua_toentity( L, 4 );

    UTIL_TraceLine( vecStart, vecEnd, MASK_SOLID, pIgnore, COLLISION_GROUP_NONE, &tr );

    return tr.fraction < 1.0f && tr.m_pEnt && !tr.allsolid;
}

static void LuaDecal_Apply( trace_t &tr, int decalIndex )
{
    if ( decalIndex < 0 )
        return;

    CBroadcastRecipientFilter filter;
    te->Decal( filter, 0.0f, &tr.endpos, &tr.startpos, tr.GetEntityIndex(), tr.hitbox, decalIndex );
}

LUA_BINDING_BEGIN( Utilities, Decal, "library", "Traces from start to end and places a decal from decals.txt by name (name, start, end, ignoreEntity)." )
{
    const char *pszName = LUA_BINDING_ARGUMENT( luaL_checkstring, 1, "decalName" );
    trace_t tr;

    if ( LuaDecal_Trace( L, tr ) )
        LuaDecal_Apply( tr, decalsystem->GetDecalIndexForName( pszName ) );

    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( Utilities, DecalMaterial, "library", "Traces from start to end and places a decal using a material path (material, start, end, ignoreEntity)." )
{
    const char *pszMaterial = LUA_BINDING_ARGUMENT( luaL_checkstring, 1, "material" );
    trace_t tr;

    if ( !LuaDecal_Trace( L, tr ) )
        return 0;

#ifdef CLIENT_DLL
    char szMaterial[MAX_PATH];
    V_strncpy( szMaterial, pszMaterial, sizeof( szMaterial ) );
    int decalIndex = effects->Draw_DecalIndexFromName( szMaterial );
#else
    int decalIndex = engine->PrecacheDecal( pszMaterial, true );
#endif

    LuaDecal_Apply( tr, decalIndex );
    return 0;
}
LUA_BINDING_END()

//=============================================================================
// Srcbox: GMod functions Sandbox and its tools use ( util.BlastDamage, util.SpriteTrail,
// ParticleEffect, game.CleanUpMap, DynamicLight ); the compat layer puts them under GMod's names.
//=============================================================================

// GMod colours are Lua tables ( r, g, b, a ); this takes either those or a Color object
static void SrcboxReadColor( lua_State *L, int index, int &r, int &g, int &b, int &a )
{
    r = g = b = a = 255;

    if ( lua_istable( L, index ) )
    {
        lua_getfield( L, index, "r" ); r = ( int )luaL_optnumber( L, -1, 255 ); lua_pop( L, 1 );
        lua_getfield( L, index, "g" ); g = ( int )luaL_optnumber( L, -1, 255 ); lua_pop( L, 1 );
        lua_getfield( L, index, "b" ); b = ( int )luaL_optnumber( L, -1, 255 ); lua_pop( L, 1 );
        lua_getfield( L, index, "a" ); a = ( int )luaL_optnumber( L, -1, 255 ); lua_pop( L, 1 );
    }
}

// ParticleEffect( name, pos, angles, parent )
LUA_BINDING_BEGIN( Utilities, ParticleEffect, "library", "Starts a particle system ( from a .pcf ) at a position, optionally attached to an entity ( GMod's ParticleEffect )." )
{
    const char *pszName = LUA_BINDING_ARGUMENT( luaL_checkstring, 1, "name" );
    Vector vecOrigin = LUA_BINDING_ARGUMENT( luaL_checkvector, 2, "position" );
    QAngle angAngles = LUA_BINDING_ARGUMENT( luaL_checkangle, 3, "angles" );
    CBaseEntity *pParent = lua_isnoneornil( L, 4 ) ? NULL : lua_toentity( L, 4 );

    DispatchParticleEffect( pszName, vecOrigin, angAngles, pParent );
    return 0;
}
LUA_BINDING_END()

#ifdef GAME_DLL
// util.BlastDamage( inflictor, attacker, origin, radius, damage )
LUA_BINDING_BEGIN( Utilities, BlastDamage, "library", "Explosion damage to everything within a radius ( GMod's util.BlastDamage ).", "server" )
{
    CBaseEntity *pInflictor = lua_isnoneornil( L, 1 ) ? NULL : lua_toentity( L, 1 );
    CBaseEntity *pAttacker = lua_isnoneornil( L, 2 ) ? NULL : lua_toentity( L, 2 );
    Vector vecOrigin = LUA_BINDING_ARGUMENT( luaL_checkvector, 3, "origin" );
    float flRadius = ( float )LUA_BINDING_ARGUMENT( luaL_checknumber, 4, "radius" );
    float flDamage = ( float )LUA_BINDING_ARGUMENT( luaL_checknumber, 5, "damage" );

    if ( !pAttacker )
        pAttacker = pInflictor;

    CTakeDamageInfo info( pInflictor, pAttacker, flDamage, DMG_BLAST );
    info.SetDamagePosition( vecOrigin );

    if ( g_pGameRules )
        g_pGameRules->RadiusDamage( info, vecOrigin, flRadius, CLASS_NONE, NULL );

    return 0;
}
LUA_BINDING_END()

// util.SpriteTrail( ent, attachment, color, additive, startWidth, endWidth, lifetime, textureRes, texture )
LUA_BINDING_BEGIN( Utilities, SpriteTrail, "library", "Attaches a sprite trail to an entity and returns it ( GMod's util.SpriteTrail ).", "server" )
{
    CBaseEntity *pEntity = LUA_BINDING_ARGUMENT( luaL_checkentity, 1, "entity" );
    int nAttachment = ( int )luaL_optinteger( L, 2, 0 );
    int r, g, b, a;
    SrcboxReadColor( L, 3, r, g, b, a );
    bool bAdditive = lua_toboolean( L, 4 ) != 0;
    float flStartWidth = ( float )luaL_optnumber( L, 5, 8 );
    float flEndWidth = ( float )luaL_optnumber( L, 6, 0 );
    float flLifeTime = ( float )luaL_optnumber( L, 7, 1 );
    float flTextureRes = ( float )luaL_optnumber( L, 8, 1.0 / ( ( flStartWidth + flEndWidth ) * 0.5 + 0.001 ) );
    const char *pszTexture = luaL_optstring( L, 9, "trails/laser.vmt" );

    if ( !pEntity )
    {
        lua_pushnil( L );
        return 1;
    }

    CSpriteTrail *pTrail = CSpriteTrail::SpriteTrailCreate( pszTexture, pEntity->GetAbsOrigin(), false );

    if ( !pTrail )
    {
        lua_pushnil( L );
        return 1;
    }

    pTrail->SetTransparency( bAdditive ? kRenderTransAdd : kRenderTransAlpha, r, g, b, a, kRenderFxNone );
    pTrail->SetAttachment( pEntity, nAttachment );
    pTrail->SetStartWidth( flStartWidth );
    pTrail->SetEndWidth( flEndWidth );
    pTrail->SetLifeTime( flLifeTime );
    pTrail->SetTextureResolution( flTextureRes );

    CBaseEntity::PushLuaInstanceSafe( L, pTrail );
    return 1;
}
LUA_BINDING_END( "Entity", "The trail." )

// game.CleanUpMap(): removes everything players made and puts the map's own entities back
LUA_BINDING_BEGIN( Utilities, CleanUpMap, "library", "Resets the map's entities ( GMod's game.CleanUpMap ).", "server" )
{
#ifdef HL2MP
    CHL2MPRules *pRules = dynamic_cast< CHL2MPRules * >( g_pGameRules );
    if ( pRules )
        pRules->CleanUpMap();
#endif
    return 0;
}
LUA_BINDING_END()
#endif // GAME_DLL

#ifdef CLIENT_DLL
// DynamicLight( key, elight ): a light Lua fills in field by field ( pos, r, g, b, brightness,
// size, decay, dietime, style, dir, innerangle, outerangle, nomodel, noworld ). It lasts until
// its dietime, and has to be renewed each frame, as in GMod.
struct SrcboxDynamicLight_t
{
    int key;
    bool elight;
};

#define SRCBOX_DLIGHT_META "SrcboxDynamicLight"

static dlight_t *SrcboxFindDynamicLight( SrcboxDynamicLight_t *pLight )
{
    // CL_AllocDlight hands back the light with this key while it lives, cleared; so keep
    // what's set here in a table instead and push it all on every write ( see __newindex )
    return pLight->elight ? effects->CL_AllocElight( pLight->key ) : effects->CL_AllocDlight( pLight->key );
}

static int SrcboxDynamicLight_NewIndex( lua_State *L )
{
    SrcboxDynamicLight_t *pLight = ( SrcboxDynamicLight_t * )luaL_checkudata( L, 1, SRCBOX_DLIGHT_META );

    // Remember the value in the userdata's own table
    lua_getiuservalue( L, 1, 1 );
    lua_pushvalue( L, 2 );
    lua_pushvalue( L, 3 );
    lua_rawset( L, -3 );
    const int fields = lua_gettop( L );

    dlight_t *dl = SrcboxFindDynamicLight( pLight );
    if ( !dl )
        return 0;

    dl->key = pLight->key;
    dl->die = gpGlobals->curtime + 1.0f;
    dl->radius = 256.0f;
    dl->color.r = dl->color.g = dl->color.b = 255;
    dl->color.exponent = 0;

    lua_pushnil( L );
    while ( lua_next( L, fields ) != 0 )
    {
        if ( lua_type( L, -2 ) == LUA_TSTRING )
        {
            char key[32];
            Q_strncpy( key, lua_tostring( L, -2 ), sizeof( key ) );
            Q_strlower( key );

            if ( !Q_strcmp( key, "pos" ) )
                dl->origin = luaL_checkvector( L, -1 );
            else if ( !Q_strcmp( key, "dir" ) )
                dl->m_Direction = luaL_checkvector( L, -1 );
            else if ( !Q_strcmp( key, "r" ) )
                dl->color.r = ( byte )clamp( ( int )lua_tonumber( L, -1 ), 0, 255 );
            else if ( !Q_strcmp( key, "g" ) )
                dl->color.g = ( byte )clamp( ( int )lua_tonumber( L, -1 ), 0, 255 );
            else if ( !Q_strcmp( key, "b" ) )
                dl->color.b = ( byte )clamp( ( int )lua_tonumber( L, -1 ), 0, 255 );
            else if ( !Q_strcmp( key, "brightness" ) )
                dl->color.exponent = ( signed char )clamp( ( int )lua_tonumber( L, -1 ), -128, 127 );
            else if ( !Q_strcmp( key, "size" ) )
                dl->radius = ( float )lua_tonumber( L, -1 );
            else if ( !Q_strcmp( key, "decay" ) )
                dl->decay = ( float )lua_tonumber( L, -1 );
            else if ( !Q_strcmp( key, "dietime" ) )
                dl->die = ( float )lua_tonumber( L, -1 );
            else if ( !Q_strcmp( key, "style" ) )
                dl->style = ( int )lua_tonumber( L, -1 );
            else if ( !Q_strcmp( key, "minlight" ) )
                dl->minlight = ( float )lua_tonumber( L, -1 );
            else if ( !Q_strcmp( key, "innerangle" ) )
                dl->m_InnerAngle = ( float )lua_tonumber( L, -1 );
            else if ( !Q_strcmp( key, "outerangle" ) )
                dl->m_OuterAngle = ( float )lua_tonumber( L, -1 );
            else if ( !Q_strcmp( key, "nomodel" ) && lua_toboolean( L, -1 ) )
                dl->flags |= DLIGHT_NO_MODEL_ILLUMINATION;
            else if ( !Q_strcmp( key, "noworld" ) && lua_toboolean( L, -1 ) )
                dl->flags |= DLIGHT_NO_WORLD_ILLUMINATION;
        }

        lua_pop( L, 1 );
    }

    return 0;
}

static int SrcboxDynamicLight_Index( lua_State *L )
{
    luaL_checkudata( L, 1, SRCBOX_DLIGHT_META );
    lua_getiuservalue( L, 1, 1 );
    lua_pushvalue( L, 2 );
    lua_rawget( L, -2 );
    return 1;
}

LUA_BINDING_BEGIN( Utilities, DynamicLight, "library", "A dynamic light Lua sets up field by field ( GMod's DynamicLight ).", "client" )
{
    const int key = ( int )LUA_BINDING_ARGUMENT( luaL_checknumber, 1, "key" );
    const bool bElight = lua_toboolean( L, 2 ) != 0;

    SrcboxDynamicLight_t *pLight = ( SrcboxDynamicLight_t * )lua_newuserdatauv( L, sizeof( SrcboxDynamicLight_t ), 1 );
    pLight->key = key;
    pLight->elight = bElight;

    lua_newtable( L );
    lua_setiuservalue( L, -2, 1 );

    if ( luaL_newmetatable( L, SRCBOX_DLIGHT_META ) )
    {
        lua_pushcfunction( L, SrcboxDynamicLight_NewIndex );
        lua_setfield( L, -2, "__newindex" );
        lua_pushcfunction( L, SrcboxDynamicLight_Index );
        lua_setfield( L, -2, "__index" );
    }
    lua_setmetatable( L, -2 );

    return 1;
}
LUA_BINDING_END( "table", "The light." )
#endif // CLIENT_DLL

//-----------------------------------------------------------------------------
// Srcbox: ammo types ( for GMod's game.GetAmmo* ). Ids start at 1; 0 or -1 is none.
//-----------------------------------------------------------------------------
LUA_BINDING_BEGIN( Utilities, GetAmmoTypeCount, "library", "How many ammo types there are ( ids 1 to this )." )
{
    lua_pushinteger( L, GetAmmoDef()->m_nAmmoIndex - 1 );
    return 1;
}
LUA_BINDING_END( "number", "The ammo type count." )

LUA_BINDING_BEGIN( Utilities, GetAmmoTypeIndex, "library", "The id of the ammo type with this name, or -1." )
{
    const char *pszName = LUA_BINDING_ARGUMENT( luaL_checkstring, 1, "name" );
    lua_pushinteger( L, GetAmmoDef()->Index( pszName ) );
    return 1;
}
LUA_BINDING_END( "number", "The ammo id." )

// Every number GMod's game.GetAmmo* and game.GetAmmoData ask for, in one table ( nil for an unknown id )
LUA_BINDING_BEGIN( Utilities, GetAmmoTypeInfo, "library", "Name, damage type, damages, max carry, force, tracer and flags of an ammo type." )
{
    const int nIndex = ( int )LUA_BINDING_ARGUMENT( luaL_checknumber, 1, "id" );
    CAmmoDef *pDef = GetAmmoDef();
    Ammo_t *pAmmo = pDef->GetAmmoOfIndex( nIndex );

    if ( !pAmmo || nIndex < 1 )
    {
        lua_pushnil( L );
        return 1;
    }

    lua_newtable( L );
    lua_pushstring( L, pAmmo->pName ? pAmmo->pName : "" );
    lua_setfield( L, -2, "name" );
    lua_pushinteger( L, pDef->DamageType( nIndex ) );
    lua_setfield( L, -2, "dmgtype" );
    lua_pushinteger( L, pDef->PlrDamage( nIndex ) );
    lua_setfield( L, -2, "plydmg" );
    lua_pushinteger( L, pDef->NPCDamage( nIndex ) );
    lua_setfield( L, -2, "npcdmg" );
    lua_pushinteger( L, pDef->MaxCarry( nIndex ) );
    lua_setfield( L, -2, "maxcarry" );
    lua_pushnumber( L, pDef->DamageForce( nIndex ) );
    lua_setfield( L, -2, "force" );
    lua_pushinteger( L, pDef->TracerType( nIndex ) );
    lua_setfield( L, -2, "tracer" );
    lua_pushinteger( L, pDef->Flags( nIndex ) );
    lua_setfield( L, -2, "flags" );
    lua_pushinteger( L, pAmmo->nMinSplashSize );
    lua_setfield( L, -2, "minsplash" );
    lua_pushinteger( L, pAmmo->nMaxSplashSize );
    lua_setfield( L, -2, "maxsplash" );
    return 1;
}
LUA_BINDING_END( "table", "The ammo type's info." )

//-----------------------------------------------------------------------------
// Srcbox: particles attached to entities ( GMod's ParticleEffectAttach and the
// Entity:StopParticles family ). The server sends them; the client runs them.
//-----------------------------------------------------------------------------
LUA_BINDING_BEGIN( Utilities, ParticleEffectAttach, "library", "Starts a particle system on an entity ( GMod's ParticleEffectAttach )." )
{
    const char *pszName = LUA_BINDING_ARGUMENT( luaL_checkstring, 1, "name" );
    int nAttachType = ( int )LUA_BINDING_ARGUMENT( luaL_checknumber, 2, "attachType" );
    CBaseEntity *pEntity = LUA_BINDING_ARGUMENT( luaL_checkentity, 3, "entity" );
    int nAttachment = ( int )luaL_optinteger( L, 4, -1 );

    // GMod counts attachments from 1, and 0 means none
    DispatchParticleEffect( pszName, ( ParticleAttachment_t )nAttachType, pEntity, nAttachment > 0 ? nAttachment : -1 );
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( Utilities, StopEntityParticles, "library", "Stops the particle systems on an entity. 0: all ( stop emitting ), 1: all, at once, 2: by name, 3: by name and attachment." )
{
    CBaseEntity *pEntity = LUA_BINDING_ARGUMENT( luaL_checkentity, 1, "entity" );
    int nMode = ( int )luaL_optinteger( L, 2, 0 );
    const char *pszName = luaL_optstring( L, 3, NULL );
    int nAttachment = ( int )luaL_optinteger( L, 4, -1 );
    bool bInstantly = lua_toboolean( L, 5 ) != 0;

#ifdef CLIENT_DLL
    CParticleProperty *pProp = pEntity->ParticleProp();
    if ( !pProp )
        return 0;

    if ( nMode == 1 )
        pProp->StopEmissionAndDestroyImmediately();
    else if ( nMode == 2 && pszName )
        pProp->StopParticlesNamed( pszName, bInstantly );
    else if ( nMode == 3 && pszName )
        pProp->StopParticlesWithNameAndAttachment( pszName, nAttachment, bInstantly );
    else
        pProp->StopEmission();
#else
    // The server can only tell the clients to stop everything on it
    StopParticleEffects( pEntity );
#endif
    return 0;
}
LUA_BINDING_END()

#ifdef CLIENT_DLL
//-----------------------------------------------------------------------------
// Srcbox: CNewParticleEffect for Lua ( GMod's CreateParticleSystem and friends ).
// Lua holds a number; the effect is kept alive here until Lua lets go of it
// ( ParticleEffectRelease, from the Lua object's __gc ).
//-----------------------------------------------------------------------------
static CUtlMap< int, CSmartPtr< CNewParticleEffect > > s_LuaParticleEffects( DefLessFunc( int ) );
static int s_nNextLuaParticleEffect = 1;

static void SrcboxPushParticleEffect( lua_State *L, CNewParticleEffect *pEffect )
{
    if ( !pEffect )
    {
        lua_pushnil( L );
        return;
    }

    const int nId = s_nNextLuaParticleEffect++;
    s_LuaParticleEffects.Insert( nId, CSmartPtr< CNewParticleEffect >( pEffect ) );
    lua_pushinteger( L, nId );
}

static CNewParticleEffect *SrcboxGetParticleEffect( lua_State *L, int nArg )
{
    const int nId = ( int )luaL_checkinteger( L, nArg );
    const unsigned short i = s_LuaParticleEffects.Find( nId );
    if ( i == s_LuaParticleEffects.InvalidIndex() )
        return NULL;
    return s_LuaParticleEffects[i].GetObject();
}

LUA_BINDING_BEGIN( Utilities, ParticleEffectCreate, "library", "Creates a particle system, on an entity or on its own. Returns its id.", "client" )
{
    CBaseEntity *pEntity = lua_isnoneornil( L, 1 ) ? NULL : lua_toentity( L, 1 );
    const char *pszName = LUA_BINDING_ARGUMENT( luaL_checkstring, 2, "name" );
    int nAttachType = ( int )luaL_optinteger( L, 3, PATTACH_ABSORIGIN_FOLLOW );
    int nAttachment = ( int )luaL_optinteger( L, 4, 0 );
    Vector vecOffset = lua_isnoneornil( L, 5 ) ? vec3_origin : luaL_checkvector( L, 5 );

    CNewParticleEffect *pEffect = NULL;
    if ( pEntity && pEntity->ParticleProp() )
    {
        pEffect = pEntity->ParticleProp()->Create( pszName, ( ParticleAttachment_t )nAttachType, nAttachment > 0 ? nAttachment : INVALID_PARTICLE_ATTACHMENT, vecOffset );
    }
    else
    {
        CSmartPtr< CNewParticleEffect > pNew = CNewParticleEffect::Create( NULL, pszName );
        pEffect = pNew.GetObject();
        if ( pEffect )
            pEffect->SetControlPoint( 0, vecOffset );
    }

    SrcboxPushParticleEffect( L, pEffect );
    return 1;
}
LUA_BINDING_END( "number", "The effect's id, or nil." )

LUA_BINDING_BEGIN( Utilities, ParticleEffectRelease, "library", "Lets go of a particle system Lua no longer holds.", "client" )
{
    const int nId = ( int )luaL_checkinteger( L, 1 );
    s_LuaParticleEffects.Remove( nId );
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( Utilities, ParticleEffectCall, "library", "Runs one CNewParticleEffect method ( by GMod's name ) on a particle system id.", "client" )
{
    CNewParticleEffect *pEffect = SrcboxGetParticleEffect( L, 1 );
    const char *pszMethod = LUA_BINDING_ARGUMENT( luaL_checkstring, 2, "method" );

    if ( !V_stricmp( pszMethod, "IsValid" ) )
    {
        lua_pushboolean( L, pEffect != NULL );
        return 1;
    }

    if ( !pEffect )
        return 0;

    if ( !V_stricmp( pszMethod, "StopEmission" ) )
        pEffect->StopEmission( lua_toboolean( L, 3 ) != 0, lua_toboolean( L, 4 ) != 0, lua_toboolean( L, 5 ) != 0 );
    else if ( !V_stricmp( pszMethod, "StopEmissionAndDestroyImmediately" ) )
        pEffect->StopEmission( false, true, false );
    else if ( !V_stricmp( pszMethod, "StartEmission" ) )
        pEffect->StartEmission( lua_toboolean( L, 3 ) != 0 );
    else if ( !V_stricmp( pszMethod, "Restart" ) )
        pEffect->Restart();
    else if ( !V_stricmp( pszMethod, "SetControlPoint" ) )
        pEffect->SetControlPoint( ( int )luaL_checkinteger( L, 3 ), luaL_checkvector( L, 4 ) );
    else if ( !V_stricmp( pszMethod, "SetControlPointEntity" ) )
        pEffect->SetControlPointEntity( ( int )luaL_checkinteger( L, 3 ), lua_isnoneornil( L, 4 ) ? NULL : lua_toentity( L, 4 ) );
    else if ( !V_stricmp( pszMethod, "SetControlPointParent" ) )
        pEffect->SetControlPointParent( ( int )luaL_checkinteger( L, 3 ), ( int )luaL_checkinteger( L, 4 ) );
    else if ( !V_stricmp( pszMethod, "SetControlPointOrientation" ) )
        pEffect->SetControlPointOrientation( ( int )luaL_checkinteger( L, 3 ), luaL_checkvector( L, 4 ), luaL_checkvector( L, 5 ), luaL_checkvector( L, 6 ) );
    else if ( !V_stricmp( pszMethod, "SetControlPointForwardVector" ) )
        pEffect->SetControlPointForwardVector( ( int )luaL_checkinteger( L, 3 ), luaL_checkvector( L, 4 ) );
    else if ( !V_stricmp( pszMethod, "SetControlPointRightVector" ) )
        pEffect->SetControlPointRightVector( ( int )luaL_checkinteger( L, 3 ), luaL_checkvector( L, 4 ) );
    else if ( !V_stricmp( pszMethod, "SetControlPointUpVector" ) )
        pEffect->SetControlPointUpVector( ( int )luaL_checkinteger( L, 3 ), luaL_checkvector( L, 4 ) );
    else if ( !V_stricmp( pszMethod, "SetSortOrigin" ) )
        pEffect->SetSortOrigin( luaL_checkvector( L, 3 ) );
    else if ( !V_stricmp( pszMethod, "SetShouldDraw" ) )
        pEffect->SetDrawn( lua_toboolean( L, 3 ) != 0 );
    else if ( !V_stricmp( pszMethod, "SetIsViewModelEffect" ) )
        pEffect->SetIsViewModelEffect( lua_toboolean( L, 3 ) != 0 );
    else if ( !V_stricmp( pszMethod, "IsViewModelEffect" ) )
    {
        lua_pushboolean( L, pEffect->GetIsViewModelEffect() );
        return 1;
    }
    else if ( !V_stricmp( pszMethod, "GetEffectName" ) )
    {
        lua_pushstring( L, pEffect->GetEffectName() );
        return 1;
    }
    else if ( !V_stricmp( pszMethod, "GetOwner" ) )
    {
        CBaseEntity::PushLuaInstanceSafe( L, pEffect->GetOwner() );
        return 1;
    }
    else if ( !V_stricmp( pszMethod, "GetAutoUpdateBBox" ) )
    {
        lua_pushboolean( L, pEffect->GetAutoUpdateBBox() );
        return 1;
    }
    else if ( !V_stricmp( pszMethod, "GetHighestControlPoint" ) )
    {
        lua_pushinteger( L, pEffect->GetHighestControlPoint() );
        return 1;
    }
    else if ( !V_stricmp( pszMethod, "IsFinished" ) )
    {
        lua_pushboolean( L, pEffect->IsFinished() );
        return 1;
    }
    else if ( !V_stricmp( pszMethod, "GetRenderBounds" ) )
    {
        Vector mins, maxs;
        pEffect->GetRenderBounds( mins, maxs );
        lua_pushvector( L, mins );
        lua_pushvector( L, maxs );
        return 2;
    }

    return 0;
}
LUA_BINDING_END()
#endif // CLIENT_DLL

//-----------------------------------------------------------------------------
// Srcbox: animation layers and gestures ( GMod's Entity:AddGesture, Get/SetLayer* ... ).
// Adding and removing gestures is the server's; the client reads and sets what it
// was sent ( GMod's client-side layer setters only last until the next update ).
//-----------------------------------------------------------------------------
LUA_BINDING_BEGIN( Utilities, EntityLayerCall, "library", "Runs one GMod layer / gesture method ( by its name ) on an entity." )
{
    CBaseEntity *pEntity = LUA_BINDING_ARGUMENT( luaL_checkentity, 1, "entity" );
    const char *pszMethod = LUA_BINDING_ARGUMENT( luaL_checkstring, 2, "method" );

#ifdef CLIENT_DLL
    C_BaseAnimatingOverlay *pOverlay = dynamic_cast< C_BaseAnimatingOverlay * >( pEntity );
#else
    CBaseAnimatingOverlay *pOverlay = dynamic_cast< CBaseAnimatingOverlay * >( pEntity );
#endif
    if ( !pOverlay )
        return 0;

    const int nLayer = ( int )luaL_optinteger( L, 3, 0 );

    if ( !V_stricmp( pszMethod, "IsValidLayer" ) )
    {
        lua_pushboolean( L, nLayer >= 0 && nLayer < pOverlay->GetNumAnimOverlays() );
        return 1;
    }

#ifdef CLIENT_DLL
    if ( nLayer < 0 || nLayer >= pOverlay->GetNumAnimOverlays() )
        return 0;

    C_AnimationLayer *pLayer = pOverlay->GetAnimOverlay( nLayer );
    if ( !pLayer )
        return 0;

    if ( !V_stricmp( pszMethod, "GetLayerCycle" ) )
        lua_pushnumber( L, pLayer->m_flCycle );
    else if ( !V_stricmp( pszMethod, "GetLayerWeight" ) )
        lua_pushnumber( L, pLayer->m_flWeight );
    else if ( !V_stricmp( pszMethod, "GetLayerPlaybackRate" ) )
        lua_pushnumber( L, pLayer->m_flPlaybackRate );
    else if ( !V_stricmp( pszMethod, "GetLayerSequence" ) )
        lua_pushinteger( L, pLayer->m_nSequence );
    else if ( !V_stricmp( pszMethod, "GetLayerDuration" ) )
    {
        const float flRate = pLayer->m_flPlaybackRate;
        const float flLength = pOverlay->SequenceDuration( pLayer->m_nSequence );
        lua_pushnumber( L, flRate != 0.0f ? flLength / flRate : flLength );
    }
    else
    {
        if ( !V_stricmp( pszMethod, "SetLayerCycle" ) )
            pLayer->m_flCycle = ( float )luaL_checknumber( L, 4 );
        else if ( !V_stricmp( pszMethod, "SetLayerWeight" ) )
            pLayer->m_flWeight = ( float )luaL_checknumber( L, 4 );
        else if ( !V_stricmp( pszMethod, "SetLayerPlaybackRate" ) )
            pLayer->m_flPlaybackRate = ( float )luaL_checknumber( L, 4 );
        else if ( !V_stricmp( pszMethod, "SetLayerSequence" ) )
            pLayer->m_nSequence = ( int )luaL_checkinteger( L, 4 );
        return 0;
    }
    return 1;
#else
    const Activity act = ( Activity )nLayer;	// the gesture methods take an activity where the layer ones take a layer

    if ( !V_stricmp( pszMethod, "AddGesture" ) )
        lua_pushinteger( L, pOverlay->AddGesture( act, lua_isnoneornil( L, 4 ) ? true : lua_toboolean( L, 4 ) != 0 ) );
    else if ( !V_stricmp( pszMethod, "AddGestureSequence" ) )
        lua_pushinteger( L, pOverlay->AddGestureSequence( nLayer, lua_isnoneornil( L, 4 ) ? true : lua_toboolean( L, 4 ) != 0 ) );
    else if ( !V_stricmp( pszMethod, "AddLayeredSequence" ) )
        lua_pushinteger( L, pOverlay->AddLayeredSequence( nLayer, ( int )luaL_optinteger( L, 4, 0 ) ) );
    else if ( !V_stricmp( pszMethod, "IsPlayingGesture" ) )
        lua_pushboolean( L, pOverlay->IsPlayingGesture( act ) );
    else if ( !V_stricmp( pszMethod, "FindGestureLayer" ) )
        lua_pushinteger( L, pOverlay->FindGestureLayer( act ) );
    else if ( !V_stricmp( pszMethod, "GetLayerCycle" ) )
        lua_pushnumber( L, pOverlay->IsValidLayer( nLayer ) ? pOverlay->GetLayerCycle( nLayer ) : 0.0f );
    else if ( !V_stricmp( pszMethod, "GetLayerWeight" ) )
        lua_pushnumber( L, pOverlay->IsValidLayer( nLayer ) ? pOverlay->GetLayerWeight( nLayer ) : 0.0f );
    else if ( !V_stricmp( pszMethod, "GetLayerDuration" ) )
        lua_pushnumber( L, pOverlay->IsValidLayer( nLayer ) ? pOverlay->GetLayerDuration( nLayer ) : 0.0f );
    else if ( !V_stricmp( pszMethod, "GetLayerSequence" ) )
        lua_pushinteger( L, pOverlay->IsValidLayer( nLayer ) ? pOverlay->GetLayerSequence( nLayer ) : -1 );
    else if ( !V_stricmp( pszMethod, "GetLayerPlaybackRate" ) )
    {
        CAnimationLayer *pLayer = pOverlay->IsValidLayer( nLayer ) ? pOverlay->GetAnimOverlay( nLayer ) : NULL;
        lua_pushnumber( L, pLayer ? pLayer->m_flPlaybackRate : 0.0f );
    }
    else
    {
        if ( !V_stricmp( pszMethod, "RestartGesture" ) )
            pOverlay->RestartGesture( act, lua_isnoneornil( L, 4 ) ? true : lua_toboolean( L, 4 ) != 0, lua_isnoneornil( L, 5 ) ? true : lua_toboolean( L, 5 ) != 0 );
        else if ( !V_stricmp( pszMethod, "RemoveGesture" ) )
            pOverlay->RemoveGesture( act );
        else if ( !V_stricmp( pszMethod, "RemoveAllGestures" ) )
            pOverlay->RemoveAllGestures();
        else if ( !pOverlay->IsValidLayer( nLayer ) )
            return 0;
        else if ( !V_stricmp( pszMethod, "RemoveLayer" ) )
            pOverlay->RemoveLayer( nLayer, ( float )luaL_optnumber( L, 4, 0.2 ), ( float )luaL_optnumber( L, 5, 0.0 ) );
        else if ( !V_stricmp( pszMethod, "SetLayerCycle" ) )
            pOverlay->SetLayerCycle( nLayer, ( float )luaL_checknumber( L, 4 ) );
        else if ( !V_stricmp( pszMethod, "SetLayerWeight" ) )
            pOverlay->SetLayerWeight( nLayer, ( float )luaL_checknumber( L, 4 ) );
        else if ( !V_stricmp( pszMethod, "SetLayerDuration" ) )
            pOverlay->SetLayerDuration( nLayer, ( float )luaL_checknumber( L, 4 ) );
        else if ( !V_stricmp( pszMethod, "SetLayerPlaybackRate" ) )
            pOverlay->SetLayerPlaybackRate( nLayer, ( float )luaL_checknumber( L, 4 ) );
        else if ( !V_stricmp( pszMethod, "SetLayerPriority" ) )
            pOverlay->SetLayerPriority( nLayer, ( int )luaL_checkinteger( L, 4 ) );
        else if ( !V_stricmp( pszMethod, "SetLayerBlendIn" ) )
            pOverlay->SetLayerBlendIn( nLayer, ( float )luaL_checknumber( L, 4 ) );
        else if ( !V_stricmp( pszMethod, "SetLayerBlendOut" ) )
            pOverlay->SetLayerBlendOut( nLayer, ( float )luaL_checknumber( L, 4 ) );
        else if ( !V_stricmp( pszMethod, "SetLayerAutokill" ) )
            pOverlay->SetLayerAutokill( nLayer, lua_toboolean( L, 4 ) != 0 );
        else if ( !V_stricmp( pszMethod, "SetLayerLooping" ) )
            pOverlay->SetLayerLooping( nLayer, lua_toboolean( L, 4 ) != 0 );
        else if ( !V_stricmp( pszMethod, "SetLayerSequence" ) )
        {
            CAnimationLayer *pLayer = pOverlay->GetAnimOverlay( nLayer );
            if ( pLayer )
                pLayer->m_nSequence = ( int )luaL_checkinteger( L, 4 );
        }
        return 0;
    }
    return 1;
#endif
}
LUA_BINDING_END()

LUALIB_API int luaopen_Util_shared( lua_State *L )
{
    LUA_REGISTRATION_COMMIT_LIBRARY( Utilities )

    return 1;
}
