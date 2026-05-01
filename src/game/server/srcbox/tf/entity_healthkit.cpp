//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: CTF HealthKit.
//
//=============================================================================//
#include "cbase.h"
#include "items.h"
#include "hl2mp_gamerules.h"
#include "shareddefs.h"
//#include "tf_player.h"
//#include "tf_team.h"
#include "engine/IEngineSound.h"
#include "entity_healthkit.h"
//#include "tf_weapon_lunchbox.h"
//#include "tf_gamestats.h"

extern ConVar sk_healthkit;
extern ConVar sk_healthvial;

//=============================================================================
//
// CTF HealthKit defines.
//

#define TF_HEALTHKIT_MODEL			"models/items/healthkit.mdl"
#define TF_HEALTHKIT_PICKUP_SOUND	"HealthKit.Touch"

#define TF_AMMOPACK_PICKUP_SOUND	"AmmoPack.Touch"

LINK_ENTITY_TO_CLASS( item_healthkit_full, CHealthKitTF );
LINK_ENTITY_TO_CLASS( item_healthkit_small, CHealthKitSmall );
LINK_ENTITY_TO_CLASS( item_healthkit_medium, CHealthKitMedium );

LINK_ENTITY_TO_CLASS( item_healthammokit, CHealthAmmoKit );

IMPLEMENT_AUTO_LIST( IHealthKitAutoList );

//=============================================================================
//
// CTF HealthKit functions.
//

//-----------------------------------------------------------------------------
// Purpose: Spawn function for the healthkit
//-----------------------------------------------------------------------------
void CHealthKitTF::Spawn( void )
{
	BaseClass::Spawn();
}

//-----------------------------------------------------------------------------
// Purpose: Precache function for the healthkit
//-----------------------------------------------------------------------------
void CHealthKitTF::Precache( void )
{
	PrecacheScriptSound( TF_HEALTHKIT_PICKUP_SOUND );
	PrecacheModel( TF_MEDKIT_LARGE_BDAY ); // always precache this for PyroVision
	PrecacheModel( TF_MEDKIT_LARGE_HALLOWEEN ); // always precache this for Halloween

	BaseClass::Precache();

	UpdateModelIndexOverrides();
}

//-----------------------------------------------------------------------------
// Purpose: MyTouch function for the healthkit
//-----------------------------------------------------------------------------
bool CHealthKitTF::MyTouch( CBasePlayer *pPlayer )
{
	if (pPlayer->TakeHealth(sk_healthkit.GetFloat(), DMG_GENERIC))
	{
		CSingleUserRecipientFilter user(pPlayer);
		user.MakeReliable();

		UserMessageBegin(user, "ItemPickup");
		WRITE_STRING(GetClassname());
		MessageEnd();

		CPASAttenuationFilter filter(pPlayer);
		EmitSound(filter, pPlayer->entindex(), TF_HEALTHKIT_PICKUP_SOUND);

		if (g_pGameRules->ItemShouldRespawn(this))
		{
			Respawn();
		}
		else
		{
			UTIL_Remove(this);
		}

		return true;
	}

	return false;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
float CHealthKitTF::GetRespawnDelay( void )
{
	return g_pGameRules->FlItemRespawnTime( this );
}


//-----------------------------------------------------------------------------
// Purpose: MyTouch function for the health-ammo kit
//-----------------------------------------------------------------------------
bool CHealthAmmoKit::MyTouch( CBasePlayer *pPlayer )
{
	// Now do ammo-kit behavior (essentially a dupe of the logic in CAmmmoPack::MyTouch - no easy way to put in one spot
	// without larger refactoring).	Filtering out heavies picking up their own sandvich.

	if (pPlayer->TakeHealth(sk_healthvial.GetFloat(), DMG_GENERIC))
	{
		CSingleUserRecipientFilter user(pPlayer);
		user.MakeReliable();

		UserMessageBegin(user, "ItemPickup");
		WRITE_STRING(GetClassname());
		MessageEnd();

		CPASAttenuationFilter filter(pPlayer);
		EmitSound(filter, pPlayer->entindex(), TF_AMMOPACK_PICKUP_SOUND);

		if (g_pGameRules->ItemShouldRespawn(this))
		{
			Respawn();
		}
		else
		{
			UTIL_Remove(this);
		}

		return true;
	}

	return false;
}