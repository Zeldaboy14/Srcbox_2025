//========= Copyright © 1996-2005, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
//=============================================================================//

#include "cbase.h"

#if defined( CLIENT_DLL )
	#include "c_hl2mp_player.h"
#else
	#include "hl2mp_player.h"
#endif

#include "weapon_hl2mpbase_scriptedweapon.h"
#include "in_buttons.h"
#include "ammodef.h"
#include "luamanager.h"
#include "lbasecombatweapon_shared.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

IMPLEMENT_NETWORKCLASS_ALIASED( HL2MPScriptedWeapon, DT_HL2MPScriptedWeapon )

BEGIN_NETWORK_TABLE( CHL2MPScriptedWeapon, DT_HL2MPScriptedWeapon )
#ifdef CLIENT_DLL
	RecvPropString( RECVINFO( m_iScriptedClassname ) ),
#else
	SendPropString( SENDINFO( m_iScriptedClassname ) ),
#endif
END_NETWORK_TABLE()

BEGIN_PREDICTION_DATA( CHL2MPScriptedWeapon )
END_PREDICTION_DATA()

//=========================================================
//	>> CHLSelectFireScriptedWeapon
//=========================================================
BEGIN_DATADESC( CHL2MPScriptedWeapon )
END_DATADESC()



// LINK_ENTITY_TO_CLASS( weapon_hl2mpbase_scriptedweapon, CHL2MPScriptedWeapon );
// PRECACHE_WEAPON_REGISTER( weapon_hl2mpbase_scriptedweapon );

// These functions replace the macros above for runtime registration of
// scripted weapons.
#ifdef CLIENT_DLL
static C_BaseEntity *CCHL2MPScriptedWeaponFactory( void )
{
	return static_cast< C_BaseEntity * >( new CHL2MPScriptedWeapon );
};
#endif

#ifndef CLIENT_DLL
static CUtlDict< CEntityFactory<CHL2MPScriptedWeapon>*, unsigned short > m_WeaponFactoryDatabase;
#endif

void RegisterScriptedWeapon( const char *className )
{
#ifdef CLIENT_DLL
	if ( GetClassMap().FindFactory( className ) )
	{
		return;
	}

	GetClassMap().Add( className, "CHL2MPScriptedWeapon", sizeof( CHL2MPScriptedWeapon ),
		&CCHL2MPScriptedWeaponFactory, true );
#else
	if ( EntityFactoryDictionary()->FindFactory( className ) )
	{
		return;
	}

	unsigned short lookup = m_WeaponFactoryDatabase.Find( className );
	if ( lookup != m_WeaponFactoryDatabase.InvalidIndex() )
	{
		return;
	}

	// Andrew; This fixes months worth of pain and anguish.
	CEntityFactory<CHL2MPScriptedWeapon> *pFactory = new CEntityFactory<CHL2MPScriptedWeapon>( className );

	lookup = m_WeaponFactoryDatabase.Insert( className, pFactory );
	Assert( lookup != m_WeaponFactoryDatabase.InvalidIndex() );
#endif
	// BUGBUG: When attempting to precache weapons registered during runtime,
	// they don't appear as valid registered entities.
	// static CPrecacheRegister precache_weapon_(&CPrecacheRegister::PrecacheFn_Other, className);
}

void ResetWeaponFactoryDatabase( void )
{
#ifdef CLIENT_DLL
#ifdef LUA_SDK
	GetClassMap().RemoveAllScripted();
#endif
#else
	for ( int i=m_WeaponFactoryDatabase.First(); i != m_WeaponFactoryDatabase.InvalidIndex(); i=m_WeaponFactoryDatabase.Next( i ) )
	{
		delete m_WeaponFactoryDatabase[ i ];
	}
	m_WeaponFactoryDatabase.RemoveAll();
#endif
}


// acttable_t CHL2MPScriptedWeapon::m_acttable[] = 
// {
// 	{ ACT_MP_STAND_IDLE,				ACT_HL2MP_IDLE_PISTOL,					false },
// 	{ ACT_MP_CROUCH_IDLE,				ACT_HL2MP_IDLE_CROUCH_PISTOL,			false },
// 
// 	{ ACT_MP_RUN,						ACT_HL2MP_RUN_PISTOL,					false },
// 	{ ACT_MP_CROUCHWALK,				ACT_HL2MP_WALK_CROUCH_PISTOL,			false },
// 
// 	{ ACT_MP_ATTACK_STAND_PRIMARYFIRE,	ACT_HL2MP_GESTURE_RANGE_ATTACK_PISTOL,	false },
// 	{ ACT_MP_ATTACK_CROUCH_PRIMARYFIRE,	ACT_HL2MP_GESTURE_RANGE_ATTACK_PISTOL,	false },
// 
// 	{ ACT_MP_RELOAD_STAND,				ACT_HL2MP_GESTURE_RELOAD_PISTOL,		false },
// 	{ ACT_MP_RELOAD_CROUCH,				ACT_HL2MP_GESTURE_RELOAD_PISTOL,		false },
// 
// 	{ ACT_MP_JUMP,						ACT_HL2MP_JUMP_PISTOL,					false },
// };

// IMPLEMENT_ACTTABLE( CHL2MPScriptedWeapon );

// These functions serve as skeletons for the our weapons' actions to be
// implemented in Lua.
acttable_t *CHL2MPScriptedWeapon::ActivityList( void ) {
#ifdef LUA_SDK
	lua_getref( L, m_nTableReference );
	lua_getfield( L, -1, "m_acttable" );
	lua_remove( L, -2 );
	if ( lua_istable( L, -1 ) )
	{
		for( int i = 0 ; i < LUA_MAX_WEAPON_ACTIVITIES ; i++ )
		{
			lua_pushinteger( L, i );
			lua_gettable( L, -2 );
			if ( lua_istable( L, -1 ) )
			{
				m_acttable[i].baseAct = ACT_INVALID;
				lua_pushinteger( L, 1 );
				lua_gettable( L, -2 );
				if ( lua_isnumber( L, -1 ) )
					m_acttable[i].baseAct = lua_tointeger( L, -1 );
				lua_pop( L, 1 );

				m_acttable[i].weaponAct = ACT_INVALID;
				lua_pushinteger( L, 2 );
				lua_gettable( L, -2 );
				if ( lua_isnumber( L, -1 ) )
					m_acttable[i].weaponAct = lua_tointeger( L, -1 );
				lua_pop( L, 1 );

				m_acttable[i].required = false;
				lua_pushinteger( L, 3 );
				lua_gettable( L, -2 );
				if ( lua_isboolean( L, -1 ) )
					m_acttable[i].required = (bool)lua_toboolean( L, -1 );
				lua_pop( L, 1 );
			}
			lua_pop( L, 1 );
		}
	}
	lua_pop( L, 1 );
#endif
	return m_acttable;
}
int CHL2MPScriptedWeapon::ActivityListCount( void ) { return LUA_MAX_WEAPON_ACTIVITIES; }

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
CHL2MPScriptedWeapon::CHL2MPScriptedWeapon( void )
{
	m_pLuaWeaponInfo = dynamic_cast< CHL2MPSWeaponInfo* >( CreateWeaponInfo() );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
CHL2MPScriptedWeapon::~CHL2MPScriptedWeapon( void )
{
	delete m_pLuaWeaponInfo;
	// Andrew; This is actually done in CBaseEntity. I'm doing it here because
	// this is the class that initialized the reference.
#ifdef LUA_SDK
	lua_unref( L, m_nTableReference );
#endif
}

#ifdef LUA_SDK
//-----------------------------------------------------------------------------
// Purpose: Initialize any variables in the reference table
//-----------------------------------------------------------------------------
void CHL2MPScriptedWeapon::SetupRefTable(lua_State* L)
{
	lua_newtable(L);
	m_nTableReference = luaL_ref(L, LUA_REGISTRYINDEX);

	// We need the classname, so we can initialize the weapon based on the
	// values given when registering with ScriptedWeapons.Register.
	Assert(m_iScriptedClassname != nullptr);
	Assert(Q_strlen(m_iScriptedClassname) > 0);

	lua_getglobal(L, LUA_SCRIPTEDWEAPONSLIBNAME);

	if (lua_istable(L, -1))
	{
		lua_getfield(L, -1, "InitializeRefTable");
		if (lua_isfunction(L, -1))
		{
			// Initialize the ref table with copies of the registered values for this weapon.
			lua_remove(L, -2);  // Remove the library table
			lua_getref(L, m_nTableReference);
			lua_pushstring(L, m_iScriptedClassname);
			luasrc_pcall(L, 2, 0);
		}
		else
		{
			lua_pop(L, 2);  // Remove the library table and the function
			Error("ScriptedWeapons library does not have InitializeRefTable function!\n");
		}
	}
	else
	{
		lua_pop(L, 1);
		Error("ScriptedWeapons library not found!\n");
	}
}

// Override the base class, so we call OnRemove on the Lua scripted weapon.
void CHL2MPScriptedWeapon::Remove()
{
	bool fullUpdate = false;  // TODO: implement this argument https://wiki.facepunch.com/gmod/Entity:OnRemove
	LUA_CALL_WEAPON_METHOD_BEGIN("OnRemove");
	lua_pushboolean(L, fullUpdate);  // doc: fullUpdate (always false, unimplemented currently)
	LUA_CALL_WEAPON_METHOD_END(1, 0);

	BaseClass::Remove();
}
#endif

extern const char *pWeaponSoundCategories[ NUM_SHOOT_SOUND_TYPES ];

#ifdef CLIENT_DLL
extern ConVar hud_fastswitch;
#endif

void CHL2MPScriptedWeapon::InitScriptedWeapon( void )
{
#if defined ( LUA_SDK )
#ifndef CLIENT_DLL
	// Let the instance reinitialize itself for the client.
	if ( m_nTableReference != LUA_NOREF )
		return;
#endif

	char className[ MAX_WEAPON_STRING ];
#if defined ( CLIENT_DLL )
	if ( strlen( GetScriptedClassname() ) > 0 )
		Q_strncpy( className, GetScriptedClassname(), sizeof( className ) );
	else
		Q_strncpy( className, GetClassname(), sizeof( className ) );
#else
	Q_strncpy( m_iScriptedClassname.GetForModify(), GetClassname(), sizeof( className ) );
 	Q_strncpy( className, GetClassname(), sizeof( className ) );
#endif
 	Q_strlower( className );
	// Andrew; This redundancy is pretty annoying.
	// Classname
	Q_strncpy( m_pLuaWeaponInfo->szClassName, className, MAX_WEAPON_STRING );
	SetClassname( className );

	lua_getglobal( L, "weapon" );
	if ( lua_istable( L, -1 ) )
	{
		lua_getfield( L, -1, "get" );
		if ( lua_isfunction( L, -1 ) )
		{
			lua_remove( L, -2 );
			lua_pushstring( L, className );
			luasrc_pcall( L, 1, 1, 0 );
		}
		else
		{
			lua_pop( L, 2 );
		}
	}
	else
	{
		lua_pop( L, 1 );
	}

	m_nTableReference = luaL_ref( L, LUA_REGISTRYINDEX );
#ifndef CLIENT_DLL
	m_pLuaWeaponInfo->bParsedScript = true;
#endif

	// Printable name
	lua_getref( L, m_nTableReference );
	lua_getfield( L, -1, "printname" );
	lua_remove( L, -2 );
	if ( lua_isstring( L, -1 ) )
	{
		Q_strncpy( m_pLuaWeaponInfo->szPrintName, lua_tostring( L, -1 ), MAX_WEAPON_STRING );
	}
	else
	{
		Q_strncpy( m_pLuaWeaponInfo->szPrintName, WEAPON_PRINTNAME_MISSING, MAX_WEAPON_STRING );
	}
	lua_pop( L, 1 );
	// View model & world model
	lua_getref( L, m_nTableReference );
	lua_getfield( L, -1, "viewmodel" );
	lua_remove( L, -2 );
	if ( lua_isstring( L, -1 ) )
	{
		Q_strncpy( m_pLuaWeaponInfo->szViewModel, lua_tostring( L, -1 ), MAX_WEAPON_STRING );
	}
	lua_pop( L, 1 );
	lua_getref( L, m_nTableReference );
	lua_getfield( L, -1, "playermodel" );
	lua_remove( L, -2 );
	if ( lua_isstring( L, -1 ) )
	{
		Q_strncpy( m_pLuaWeaponInfo->szWorldModel, lua_tostring( L, -1 ), MAX_WEAPON_STRING );
	}
	lua_pop( L, 1 );
	lua_getref( L, m_nTableReference );
	lua_getfield( L, -1, "anim_prefix" );
	lua_remove( L, -2 );
	if ( lua_isstring( L, -1 ) )
	{
		Q_strncpy( m_pLuaWeaponInfo->szAnimationPrefix, lua_tostring( L, -1 ), MAX_WEAPON_PREFIX );
	}
	lua_pop( L, 1 );
	lua_getref( L, m_nTableReference );
	lua_getfield( L, -1, "bucket" );
	lua_remove( L, -2 );
	if ( lua_isnumber( L, -1 ) )
	{
		m_pLuaWeaponInfo->iSlot = lua_tonumber( L, -1 );
	}
	else
	{
		m_pLuaWeaponInfo->iSlot = 0;
	}
	lua_pop( L, 1 );
	lua_getref( L, m_nTableReference );
	lua_getfield( L, -1, "bucket_position" );
	lua_remove( L, -2 );
	if ( lua_isnumber( L, -1 ) )
	{
		m_pLuaWeaponInfo->iPosition = lua_tonumber( L, -1 );
	}
	else
	{
		m_pLuaWeaponInfo->iPosition = 0;
	}
	lua_pop( L, 1 );

	// Use the console (X360) buckets if hud_fastswitch is set to 2.
#ifdef CLIENT_DLL
	if ( hud_fastswitch.GetInt() == 2 )
#else
	if ( IsX360() )
#endif
	{
		lua_getref( L, m_nTableReference );
		lua_getfield( L, -1, "bucket_360" );
		lua_remove( L, -2 );
		if ( lua_isnumber( L, -1 ) )
		{
			m_pLuaWeaponInfo->iSlot = lua_tonumber( L, -1 );
		}
		lua_pop( L, 1 );
		lua_getref( L, m_nTableReference );
		lua_getfield( L, -1, "bucket_position_360" );
		lua_remove( L, -2 );
		if ( lua_isnumber( L, -1 ) )
		{
			m_pLuaWeaponInfo->iPosition = lua_tonumber( L, -1 );
		}
		lua_pop( L, 1 );
	}
	lua_getref( L, m_nTableReference );
	lua_getfield( L, -1, "clip_size" );
	lua_remove( L, -2 );
	if ( lua_isnumber( L, -1 ) )
	{
		m_pLuaWeaponInfo->iMaxClip1 = lua_tonumber( L, -1 );					// Max primary clips gun can hold (assume they don't use clips by default)
	}
	else
	{
		m_pLuaWeaponInfo->iMaxClip1 = WEAPON_NOCLIP;
	}
	lua_pop( L, 1 );
	lua_getref( L, m_nTableReference );
	lua_getfield( L, -1, "clip2_size" );
	lua_remove( L, -2 );
	if ( lua_isnumber( L, -1 ) )
	{
		m_pLuaWeaponInfo->iMaxClip2 = lua_tonumber( L, -1 );					// Max secondary clips gun can hold (assume they don't use clips by default)
	}
	else
	{
		m_pLuaWeaponInfo->iMaxClip2 = WEAPON_NOCLIP;
	}
	lua_pop( L, 1 );
	lua_getref( L, m_nTableReference );
	lua_getfield( L, -1, "default_clip" );
	lua_remove( L, -2 );
	if ( lua_isnumber( L, -1 ) )
	{
		m_pLuaWeaponInfo->iDefaultClip1 = lua_tonumber( L, -1 );		// amount of primary ammo placed in the primary clip when it's picked up
	}
	else
	{
		m_pLuaWeaponInfo->iDefaultClip1 = m_pLuaWeaponInfo->iMaxClip1;
	}
	lua_pop( L, 1 );
	lua_getref( L, m_nTableReference );
	lua_getfield( L, -1, "default_clip2" );
	lua_remove( L, -2 );
	if ( lua_isnumber( L, -1 ) )
	{
		m_pLuaWeaponInfo->iDefaultClip2 = lua_tonumber( L, -1 );		// amount of secondary ammo placed in the secondary clip when it's picked up
	}
	else
	{
		m_pLuaWeaponInfo->iDefaultClip2 = m_pLuaWeaponInfo->iMaxClip2;
	}
	lua_pop( L, 1 );
	lua_getref( L, m_nTableReference );
	lua_getfield( L, -1, "weight" );
	lua_remove( L, -2 );
	if ( lua_isnumber( L, -1 ) )
	{
		m_pLuaWeaponInfo->iWeight = lua_tonumber( L, -1 );
	}
	else
	{
		m_pLuaWeaponInfo->iWeight = 0;
	}
	lua_pop( L, 1 );

	lua_getref( L, m_nTableReference );
	lua_getfield( L, -1, "rumble" );
	lua_remove( L, -2 );
	if ( lua_isnumber( L, -1 ) )
	{
		m_pLuaWeaponInfo->iWeight = lua_tonumber( L, -1 );
	}
	else
	{
		m_pLuaWeaponInfo->iWeight = -1;
	}
	lua_pop( L, 1 );
	
	lua_getref( L, m_nTableReference );
	lua_getfield( L, -1, "showusagehint" );
	lua_remove( L, -2 );
	if ( lua_isnumber( L, -1 ) )
	{
		m_pLuaWeaponInfo->bShowUsageHint = (int)lua_tointeger( L, -1 ) != 0 ? true : false;
	}
	else
	{
		m_pLuaWeaponInfo->bShowUsageHint = false;
	}
	lua_pop( L, 1 );
	lua_getref( L, m_nTableReference );
	lua_getfield( L, -1, "autoswitchto" );
	lua_remove( L, -2 );
	if ( lua_isnumber( L, -1 ) )
	{
		m_pLuaWeaponInfo->bAutoSwitchTo = (int)lua_tointeger( L, -1 ) != 0 ? true : false;
	}
	else
	{
		m_pLuaWeaponInfo->bAutoSwitchTo = true;
	}
	lua_pop( L, 1 );
	lua_getref( L, m_nTableReference );
	lua_getfield( L, -1, "autoswitchfrom" );
	lua_remove( L, -2 );
	if ( lua_isnumber( L, -1 ) )
	{
		m_pLuaWeaponInfo->bAutoSwitchFrom = (int)lua_tointeger( L, -1 ) != 0 ? true : false;
	}
	else
	{
		m_pLuaWeaponInfo->bAutoSwitchFrom = true;
	}
	lua_pop( L, 1 );
	lua_getref( L, m_nTableReference );
	lua_getfield( L, -1, "BuiltRightHanded" );
	lua_remove( L, -2 );
	if ( lua_isnumber( L, -1 ) )
	{
		m_pLuaWeaponInfo->m_bBuiltRightHanded = (int)lua_tointeger( L, -1 ) != 0 ? true : false;
	}
	else
	{
		m_pLuaWeaponInfo->m_bBuiltRightHanded = true;
	}
	lua_pop( L, 1 );
	lua_getref( L, m_nTableReference );
	lua_getfield( L, -1, "AllowFlipping" );
	lua_remove( L, -2 );
	if ( lua_isnumber( L, -1 ) )
	{
		m_pLuaWeaponInfo->m_bAllowFlipping = (int)lua_tointeger( L, -1 ) != 0 ? true : false;
	}
	else
	{
		m_pLuaWeaponInfo->m_bAllowFlipping = true;
	}
	lua_pop( L, 1 );
	lua_getref( L, m_nTableReference );
	lua_getfield( L, -1, "MeleeWeapon" );
	lua_remove( L, -2 );
	if ( lua_isnumber( L, -1 ) )
	{
		m_pLuaWeaponInfo->m_bMeleeWeapon = (int)lua_tointeger( L, -1 ) != 0 ? true : false;
	}
	else
	{
		m_pLuaWeaponInfo->m_bMeleeWeapon = false;
	}
	lua_pop( L, 1 );

	// Primary ammo used
	lua_getref( L, m_nTableReference );
	lua_getfield( L, -1, "primary_ammo" );
	lua_remove( L, -2 );
	if ( lua_isstring( L, -1 ) )
	{
		const char *pAmmo = lua_tostring( L, -1 );
		if ( strcmp("None", pAmmo) == 0 )
			Q_strncpy( m_pLuaWeaponInfo->szAmmo1, "", sizeof( m_pLuaWeaponInfo->szAmmo1 ) );
		else
			Q_strncpy( m_pLuaWeaponInfo->szAmmo1, pAmmo, sizeof( m_pLuaWeaponInfo->szAmmo1 )  );
		m_pLuaWeaponInfo->iAmmoType = GetAmmoDef()->Index( m_pLuaWeaponInfo->szAmmo1 );
	}
	lua_pop( L, 1 );
	
	// Secondary ammo used
	lua_getref( L, m_nTableReference );
	lua_getfield( L, -1, "secondary_ammo" );
	lua_remove( L, -2 );
	if ( lua_isstring( L, -1 ) )
	{
		const char *pAmmo = lua_tostring( L, -1 );
		if ( strcmp("None", pAmmo) == 0)
			Q_strncpy( m_pLuaWeaponInfo->szAmmo2, "", sizeof( m_pLuaWeaponInfo->szAmmo2 ) );
		else
			Q_strncpy( m_pLuaWeaponInfo->szAmmo2, pAmmo, sizeof( m_pLuaWeaponInfo->szAmmo2 )  );
		m_pLuaWeaponInfo->iAmmo2Type = GetAmmoDef()->Index( m_pLuaWeaponInfo->szAmmo2 );
	}
	lua_pop( L, 1 );

	// Now read the weapon sounds
	memset( m_pLuaWeaponInfo->aShootSounds, 0, sizeof( m_pLuaWeaponInfo->aShootSounds ) );
	lua_getref( L, m_nTableReference );
	lua_getfield( L, -1, "SoundData" );
	lua_remove( L, -2 );
	if ( lua_istable( L, -1 ) )
	{
		for ( int i = EMPTY; i < NUM_SHOOT_SOUND_TYPES; i++ )
		{
			lua_getfield( L, -1, pWeaponSoundCategories[i] );
			if ( lua_isstring( L, -1 ) )
			{
				const char *soundname = lua_tostring( L, -1 );
				if ( soundname && soundname[0] )
				{
					Q_strncpy( m_pLuaWeaponInfo->aShootSounds[i], soundname, MAX_WEAPON_STRING );
				}
			}
			lua_pop( L, 1 );
		}
	}
	lua_pop( L, 1 );

	lua_getref( L, m_nTableReference );
	lua_getfield( L, -1, "damage" );
	lua_remove( L, -2 );
	if ( lua_isnumber( L, -1 ) )
	{
		m_pLuaWeaponInfo->m_iPlayerDamage = (int)lua_tointeger( L, -1 );
	}
	lua_pop( L, 1 );

	LUA_CALL_WEAPON_METHOD_BEGIN( "Initialize" );
	LUA_CALL_WEAPON_METHOD_END( 0, 0 );
#endif
}

#ifdef CLIENT_DLL
void CHL2MPScriptedWeapon::OnDataChanged( DataUpdateType_t updateType )
{
	BaseClass::OnDataChanged( updateType );

	if ( updateType == DATA_UPDATE_CREATED )
	{
		if ( m_iScriptedClassname.Get() && !m_pLuaWeaponInfo->bParsedScript )
		{
			m_pLuaWeaponInfo->bParsedScript = true;
			SetClassname( m_iScriptedClassname.Get() );
			InitScriptedWeapon();

#ifdef LUA_SDK
			BEGIN_LUA_CALL_WEAPON_METHOD( "Precache" );
			END_LUA_CALL_WEAPON_METHOD( 0, 0 );
#endif
		}
	}
}

const char *CHL2MPScriptedWeapon::GetScriptedClassname( void )
{
	if ( m_iScriptedClassname.Get() )
		return m_iScriptedClassname.Get();
	return BaseClass::GetClassname();
}
#endif

void CHL2MPScriptedWeapon::Precache( void )
{
	BaseClass::Precache();

	InitScriptedWeapon();

	// Get the ammo indexes for the ammo's specified in the data file
	if ( GetWpnData().szAmmo1[0] )
	{
		m_iPrimaryAmmoType = GetAmmoDef()->Index( GetWpnData().szAmmo1 );
		if (m_iPrimaryAmmoType == -1)
		{
			Msg("ERROR: Weapon (%s) using undefined primary ammo type (%s)\n",GetClassname(), GetWpnData().szAmmo1);
		}
	}
	if ( GetWpnData().szAmmo2[0] )
	{
		m_iSecondaryAmmoType = GetAmmoDef()->Index( GetWpnData().szAmmo2 );
		if (m_iSecondaryAmmoType == -1)
		{
			Msg("ERROR: Weapon (%s) using undefined secondary ammo type (%s)\n",GetClassname(),GetWpnData().szAmmo2);
		}

	}

	// Precache models (preload to avoid hitch)
	m_iViewModelIndex = 0;
	m_iWorldModelIndex = 0;
	if ( GetViewModel() && GetViewModel()[0] )
	{
		m_iViewModelIndex = CBaseEntity::PrecacheModel( GetViewModel() );
	}
	if ( GetWorldModel() && GetWorldModel()[0] )
	{
		m_iWorldModelIndex = CBaseEntity::PrecacheModel( GetWorldModel() );
	}

	// Precache sounds, too
	for ( int i = 0; i < NUM_SHOOT_SOUND_TYPES; ++i )
	{
		const char *shootsound = GetShootSound( i );
		if ( shootsound && shootsound[0] )
		{
			CBaseEntity::PrecacheScriptSound( shootsound );
		}
	}

#if defined ( LUA_SDK ) && !defined( CLIENT_DLL )
	LUA_CALL_WEAPON_METHOD_BEGIN( "Precache" );
	LUA_CALL_WEAPON_METHOD_END( 0, 0 );
#endif
}


//-----------------------------------------------------------------------------
// Purpose: Get my data in the file weapon info array
//-----------------------------------------------------------------------------
const FileWeaponInfo_t &CHL2MPScriptedWeapon::GetWpnData( void ) const
{
	return *m_pLuaWeaponInfo;
}

const char *CHL2MPScriptedWeapon::GetViewModel( int ) const
{
#if defined ( LUA_SDK )
    if ( lua_isrefvalid( L, m_nTableReference ) )
    {
        lua_getref( L, m_nTableReference );
        lua_getfield( L, -1, "ViewModel" );
        lua_remove( L, -2 );  // Remove the reference table

        LUA_RETURN_STRING();
    }
#endif

	return BaseClass::GetViewModel();
}

const char *CHL2MPScriptedWeapon::GetWorldModel( void ) const
{
#if defined ( LUA_SDK )
    if ( lua_isrefvalid( L, m_nTableReference ) )
    {
        lua_getref( L, m_nTableReference );
        lua_getfield( L, -1, "WorldModel" );
        lua_remove( L, -2 );  // Remove the reference table

        LUA_RETURN_STRING();
    }
#endif

	return BaseClass::GetWorldModel();
}

const char *CHL2MPScriptedWeapon::GetAnimPrefix( void ) const
{
#if defined ( LUA_SDK )
	lua_getref( L, m_nTableReference );
	lua_getfield( L, -1, "anim_prefix" );
	lua_remove( L, -2 );

	LUA_RETURN_STRING();
#endif

	return BaseClass::GetAnimPrefix();
}

const char *CHL2MPScriptedWeapon::GetPrintName( void ) const
{
#if defined ( LUA_SDK )
	if (lua_isrefvalid(L, m_nTableReference))
	{
		lua_getref(L, m_nTableReference);
		lua_getfield(L, -1, "PrintName");
		lua_remove(L, -2);  // Remove the reference table

		LUA_RETURN_STRING();
	}
#endif

	return BaseClass::GetPrintName();
}

#define UPDATE_LUA_WEAPON_AMMO_FIELD_NUMBER( ammoType, ammoField, targetVar ) \
    if ( lua_isrefvalid( L, m_nTableReference ) )                             \
    {                                                                         \
        lua_getref( L, m_nTableReference );                                   \
        lua_getfield( L, -1, ammoType );                                      \
        lua_remove( L, -2 );                                                  \
        if ( lua_istable( L, -1 ) )                                           \
        {                                                                     \
            lua_getfield( L, -1, ammoField );                                 \
            if ( lua_isnumber( L, -1 ) )                                      \
            {                                                                 \
                targetVar = lua_tonumber( L, -1 );                            \
            }                                                                 \
            else                                                              \
            {                                                                 \
                targetVar = -1;                                               \
            }                                                                 \
            lua_pop( L, 1 ); /* Pop the ammoField field */                    \
        }                                                                     \
        lua_pop( L, 1 ); /* Pop the ammoType field */                         \
    }

int CHL2MPScriptedWeapon::GetMaxClip1( void ) const
{
#if defined ( LUA_SDK )
    UPDATE_LUA_WEAPON_AMMO_FIELD_NUMBER( "Primary", "ClipSize", m_pLuaWeaponInfo->iMaxClip1 );
#endif

	return BaseClass::GetMaxClip1();
}

int CHL2MPScriptedWeapon::GetMaxClip2( void ) const
{
#if defined ( LUA_SDK )
	UPDATE_LUA_WEAPON_AMMO_FIELD_NUMBER( "Secondary", "ClipSize", m_pLuaWeaponInfo->iMaxClip1 );
#endif

	return BaseClass::GetMaxClip2();
}

int CHL2MPScriptedWeapon::GetDefaultClip1( void ) const
{
#if defined ( LUA_SDK )
    UPDATE_LUA_WEAPON_AMMO_FIELD_NUMBER( "Primary", "DefaultClip", m_pLuaWeaponInfo->iMaxClip1 );
#endif

	return BaseClass::GetDefaultClip1();
}

int CHL2MPScriptedWeapon::GetDefaultClip2( void ) const
{
#if defined ( LUA_SDK )
    UPDATE_LUA_WEAPON_AMMO_FIELD_NUMBER( "Secondary", "DefaultClip", m_pLuaWeaponInfo->iMaxClip1 );
#endif

	return BaseClass::GetDefaultClip2();
}


bool CHL2MPScriptedWeapon::IsMeleeWeapon() const
{
#if defined ( LUA_SDK )
    if ( lua_isrefvalid( L, m_nTableReference ) )
    {
        lua_getref( L, m_nTableReference );
        lua_getfield( L, -1, "MeleeWeapon" );
        lua_remove( L, -2 );  // Remove the reference table

        LUA_RETURN_BOOLEAN_FROM_INTEGER();
    }
#endif

	return BaseClass::IsMeleeWeapon();
}

int CHL2MPScriptedWeapon::GetWeight( void ) const
{
#if defined ( LUA_SDK )
    if ( lua_isrefvalid( L, m_nTableReference ) )
    {
        lua_getref( L, m_nTableReference );
        lua_getfield( L, -1, "Weight" );
        lua_remove( L, -2 );  // Remove the reference table

        LUA_RETURN_INTEGER();
    }
#endif

	return BaseClass::GetWeight();
}

bool CHL2MPScriptedWeapon::AllowsAutoSwitchTo( void ) const
{
#if defined ( LUA_SDK )
    if ( lua_isrefvalid( L, m_nTableReference ) )
    {
        lua_getref( L, m_nTableReference );
        lua_getfield( L, -1, "AutoSwitchTo" );
        lua_remove( L, -2 );  // Remove the reference table

        LUA_RETURN_BOOLEAN_FROM_INTEGER();
    }
#endif

	return BaseClass::AllowsAutoSwitchTo();
}

bool CHL2MPScriptedWeapon::AllowsAutoSwitchFrom( void ) const
{
#if defined ( LUA_SDK )
    if ( lua_isrefvalid( L, m_nTableReference ) )
    {
        lua_getref( L, m_nTableReference );
        lua_getfield( L, -1, "AutoSwitchFrom" );
        lua_remove( L, -2 );  // Remove the reference table

        LUA_RETURN_BOOLEAN_FROM_INTEGER();
    }
#endif

	return BaseClass::AllowsAutoSwitchFrom();
}

int CHL2MPScriptedWeapon::GetWeaponFlags( void ) const
{
#if defined ( LUA_SDK )
    if ( lua_isrefvalid( L, m_nTableReference ) )
    {
        lua_getref( L, m_nTableReference );
        lua_getfield( L, -1, "WeaponFlags" );
        lua_remove( L, -2 );  // Remove the reference table

        LUA_RETURN_INTEGER();
    }
#endif

	return BaseClass::GetWeaponFlags();
}

int CHL2MPScriptedWeapon::GetSlot( void ) const
{
#if defined ( LUA_SDK )
    if ( lua_isrefvalid( L, m_nTableReference ) )
    {
        lua_getref( L, m_nTableReference );
        lua_getfield( L, -1, "InventorySlot" );
        lua_remove( L, -2 );  // Remove the reference table

        LUA_RETURN_INTEGER();
    }
#endif

	return BaseClass::GetSlot();
}

int CHL2MPScriptedWeapon::GetPosition( void ) const
{
#if defined ( LUA_SDK )
    if ( lua_isrefvalid( L, m_nTableReference ) )
    {
        lua_getref( L, m_nTableReference );
        lua_getfield( L, -1, "InventorySlotPosition" );
        lua_remove( L, -2 );  // Remove the reference table

        LUA_RETURN_INTEGER();
    }
#endif

	return BaseClass::GetPosition();
}

const Vector &CHL2MPScriptedWeapon::GetBulletSpread( void )
{
	static Vector cone = VECTOR_CONE_3DEGREES;
	return cone;
}

//-----------------------------------------------------------------------------
// Purpose: 
//
//
//-----------------------------------------------------------------------------
void CHL2MPScriptedWeapon::PrimaryAttack( void )
{
#if defined ( LUA_SDK )
    LUA_CALL_WEAPON_METHOD_BEGIN( "PrimaryAttack" );
    LUA_CALL_WEAPON_METHOD_END( 0, 0 );
#endif
}

void CHL2MPScriptedWeapon::SecondaryAttack( void )
{
#if defined ( LUA_SDK )
    LUA_CALL_WEAPON_METHOD_BEGIN( "SecondaryAttack" );
    LUA_CALL_WEAPON_METHOD_END( 0, 0 );
#endif
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : &info - 
//-----------------------------------------------------------------------------
void CHL2MPScriptedWeapon::FireBullets( const FireBulletsInfo_t &info )
{
	if(CBasePlayer *pPlayer = ToBasePlayer ( GetOwner() ) )
	{
		pPlayer->FireBullets(info);
	}
}

bool CHL2MPScriptedWeapon::Reload( void )
{
#if defined ( LUA_SDK )
    LUA_CALL_WEAPON_METHOD_BEGIN( "Reload" );
    LUA_CALL_WEAPON_METHOD_END( 0, 1 );

    LUA_RETURN_BOOLEAN();
#endif
	return BaseClass::Reload();
}

//-----------------------------------------------------------------------------
// Purpose: 
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
bool CHL2MPScriptedWeapon::Deploy( void )
{
#if defined ( LUA_SDK )
    LUA_CALL_WEAPON_METHOD_BEGIN( "Deploy" );
    LUA_CALL_WEAPON_METHOD_END( 0, 1 );

    LUA_RETURN_VALUE_IF_TRUE( false );
#endif

	return BaseClass::Deploy();
}

Activity CHL2MPScriptedWeapon::GetDrawActivity( void )
{
#if defined ( LUA_SDK )
    LUA_CALL_WEAPON_METHOD_BEGIN( "GetDrawActivity" );
    LUA_CALL_WEAPON_METHOD_END( 0, 1 );

    // Kind of lame, but we're required to explicitly cast
    LUA_RETURN_ACTIVITY();
#endif

	return BaseClass::GetDrawActivity();
}

//-----------------------------------------------------------------------------
// Purpose: 
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
bool CHL2MPScriptedWeapon::Holster( CBaseCombatWeapon *pSwitchingTo )
{
#if defined ( LUA_SDK )
	LUA_CALL_WEAPON_METHOD_BEGIN( "Holster" );
	CBaseCombatWeapon::PushLuaInstanceSafe(L, pSwitchingTo);
	LUA_CALL_WEAPON_METHOD_END( 1, 1 );

	LUA_RETURN_BOOLEAN();
#endif

	return BaseClass::Holster( pSwitchingTo );
}

//====================================================================================
// Purpose: Copied from CBaseCombatWeapon::ItemPostFrame and modified to let Lua
//          determine more behaviour.
//====================================================================================
void CHL2MPScriptedWeapon::ItemPostFrame( void )
{
#if defined ( LUA_SDK )
    // Experiment; TODO: Is this really the best place to call Think? At the end of the frame?
    LUA_CALL_WEAPON_METHOD_BEGIN( "Think" );
    LUA_CALL_WEAPON_METHOD_END( 0, 0 );

    LUA_CALL_WEAPON_METHOD_BEGIN( "ItemPostFrame" );
    LUA_CALL_WEAPON_METHOD_END( 0, 1 );

    LUA_RETURN_NONE_IF_FALSE();
#endif

	//BaseClass::ItemPostFrame();

	CBasePlayer* pOwner = ToBasePlayer(GetOwner());
	if (!pOwner)
		return;

	UpdateAutoFire();

	// Track the duration of the fire
	// FIXME: Check for IN_ATTACK2 as well?
	// FIXME: What if we're calling ItemBusyFrame?
	m_fFireDuration = (pOwner->m_nButtons & IN_ATTACK)
		? (m_fFireDuration + gpGlobals->frametime)
		: 0.0f;

	if (UsesClipsForAmmo1())
	{
		CheckReload();
	}

	bool bFired = false;

	// Secondary attack has priority
	if ((pOwner->m_nButtons & IN_ATTACK2))
	{
		// FIXME: This isn't necessarily true if the weapon doesn't have a secondary fire!
		// For instance, the crossbow doesn't have a 'real' secondary fire, but it still
		// stops the crossbow from firing on the 360 if the player chooses to hold down their
		// zoom button. (sjb) Orange Box 7/25/2007
#if !defined( CLIENT_DLL )
		if (!IsX360() || !ClassMatches("weapon_crossbow"))
#endif
		{
			bFired = ShouldBlockPrimaryFire();
		}

		SecondaryAttack();
	}

	if (!bFired && (pOwner->m_nButtons & IN_ATTACK))
	{
		PrimaryAttack();

#ifdef CLIENT_DLL
		pOwner->SetFiredWeapon(true);
#endif
	}

	// -----------------------
	//  Reload pressed / Clip Empty
	//  Can only start the Reload Cycle after the firing cycle
	if ((pOwner->m_nButtons & IN_RELOAD) && !m_bInReload)
	{
		// reload when reload is pressed, or if no buttons are down and weapon is empty.
		Reload();
		m_fFireDuration = 0.0f;
	}

	// -----------------------
	//  No buttons down
	// -----------------------
	if (!((pOwner->m_nButtons & IN_ATTACK) || (pOwner->m_nButtons & IN_ATTACK2) || (CanReload() && pOwner->m_nButtons & IN_RELOAD)))
	{
		// no fire buttons down or reloading
		if (!ReloadOrSwitchWeapons() && (m_bInReload == false))
		{
			WeaponIdle();
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: Called each frame by the player PostThink, if the player's not ready to attack yet
//-----------------------------------------------------------------------------
void CHL2MPScriptedWeapon::ItemBusyFrame( void )
{
#if defined ( LUA_SDK )
    LUA_CALL_WEAPON_METHOD_BEGIN( "ItemBusyFrame" );
    LUA_CALL_WEAPON_METHOD_END( 0, 1 );

    LUA_RETURN_NONE_IF_FALSE();
#endif

	BaseClass::ItemBusyFrame();
}

#ifndef CLIENT_DLL
int CHL2MPScriptedWeapon::CapabilitiesGet( void )
{
#if defined ( LUA_SDK )
    LUA_CALL_WEAPON_METHOD_BEGIN( "CapabilitiesGet" );
    LUA_CALL_WEAPON_METHOD_END( 0, 1 );

    LUA_RETURN_INTEGER();
#endif

	return BaseClass::CapabilitiesGet();
}
#else
int CHL2MPScriptedWeapon::DrawModel( int flags )
{
#if defined ( LUA_SDK )
    LUA_CALL_WEAPON_METHOD_BEGIN( "Draw" );
    lua_pushinteger( L, flags );
    LUA_CALL_WEAPON_METHOD_END( 1, 1 );

    LUA_RETURN_INTEGER();
#endif

	return BaseClass::DrawModel( flags );
}
#endif


