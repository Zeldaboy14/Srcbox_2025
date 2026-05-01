//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: CTF AmmoPack.
//
//=============================================================================//
#include "cbase.h"
#include "items.h"
#include "hl2mp_gamerules.h"
#include "ammodef.h"
#include "mp_shareddefs.h"
#include "hl2mp_player.h"
//#include "tf_team.h"
#include "engine/IEngineSound.h"
#include "entity_ammopack.h"
//#include "tf_gamestats.h"

extern ConVar sk_healthkit;

//=============================================================================
//
// CTF AmmoPack defines.
//

#define TF_AMMOPACK_PICKUP_SOUND	"AmmoPack.Touch"

LINK_ENTITY_TO_CLASS( item_ammopack_full, CAmmoPack );
LINK_ENTITY_TO_CLASS( item_ammopack_small, CAmmoPackSmall );
LINK_ENTITY_TO_CLASS( item_ammopack_medium, CAmmoPackMedium );

//=============================================================================
//
// CTF AmmoPack functions.
//

//-----------------------------------------------------------------------------
// Purpose: Spawn function for the ammopack
//-----------------------------------------------------------------------------
void CAmmoPack::Spawn( void )
{
	BaseClass::Spawn();
}

//-----------------------------------------------------------------------------
// Purpose: Precache function for the ammopack
//-----------------------------------------------------------------------------
void CAmmoPack::Precache( void )
{
	PrecacheScriptSound( TF_AMMOPACK_PICKUP_SOUND );
	PrecacheModel( TF_AMMOPACK_LARGE_BDAY ); // always precache this for PyroVision

	BaseClass::Precache();

	UpdateModelIndexOverrides();
}

//---------------------------------------------------------
// Applies ammo quantity scale.
//---------------------------------------------------------
int ITEM_GiveAmmoTF(CBasePlayer* pPlayer, float flCount, const char* pszAmmoName, bool bSuppressSound = false)
{
	int iAmmoType = GetAmmoDef()->Index(pszAmmoName);
	if (iAmmoType == -1)
	{
		Msg("ERROR: Attempting to give unknown ammo type (%s)\n", pszAmmoName);
		return 0;
	}

	flCount *= g_pGameRules->GetAmmoQuantityScale(iAmmoType);

	// Don't give out less than 1 of anything.
	flCount = MAX(1.0f, flCount);

	return pPlayer->GiveAmmo(flCount, iAmmoType, bSuppressSound);
}

//-----------------------------------------------------------------------------
// Purpose: MyTouch function for the ammopack
//-----------------------------------------------------------------------------
bool CAmmoPack::MyTouch( CBasePlayer *pPlayer )
{
	//int iMaxPrimary = pPlayer->GetMaxAmmo(TF_AMMO_PRIMARY);
	if (ITEM_GiveAmmoTF(pPlayer, SIZE_AMMO_PISTOL, "Pistol"))
	{
		if (g_pGameRules->ItemShouldRespawn(this) == GR_ITEM_RESPAWN_NO)
		{
			UTIL_Remove(this);
		}

		return true;
	}

	if (ITEM_GiveAmmoTF(pPlayer, SIZE_AMMO_SMG1, "SMG1"))
	{
		if (g_pGameRules->ItemShouldRespawn(this) == GR_ITEM_RESPAWN_NO)
		{
			UTIL_Remove(this);
		}
		return true;
	}

	if (ITEM_GiveAmmoTF(pPlayer, SIZE_AMMO_SMG1_LARGE, "SMG1"))
	{
		if (g_pGameRules->ItemShouldRespawn(this) == GR_ITEM_RESPAWN_NO)
		{
			UTIL_Remove(this);
		}
		return true;
	}

	if (ITEM_GiveAmmoTF(pPlayer, SIZE_AMMO_AR2, "AR2"))
	{
		if (g_pGameRules->ItemShouldRespawn(this) == GR_ITEM_RESPAWN_NO)
		{
			UTIL_Remove(this);
		}
		return true;
	}

	if (ITEM_GiveAmmoTF(pPlayer, SIZE_AMMO_AR2_LARGE, "AR2"))
	{
		if (g_pGameRules->ItemShouldRespawn(this) == GR_ITEM_RESPAWN_NO)
		{
			UTIL_Remove(this);
		}
		return true;
	}

	if (ITEM_GiveAmmoTF(pPlayer, SIZE_AMMO_357, "357"))
	{
		if (g_pGameRules->ItemShouldRespawn(this) == GR_ITEM_RESPAWN_NO)
		{
			UTIL_Remove(this);
		}
		return true;
	}

	if (ITEM_GiveAmmoTF(pPlayer, SIZE_AMMO_357_LARGE, "357"))
	{
		if (g_pGameRules->ItemShouldRespawn(this) == GR_ITEM_RESPAWN_NO)
		{
			UTIL_Remove(this);
		}
		return true;
	}

	if (ITEM_GiveAmmoTF(pPlayer, SIZE_AMMO_CROSSBOW, "XBowBolt"))
	{
		if (g_pGameRules->ItemShouldRespawn(this) == GR_ITEM_RESPAWN_NO)
		{
			UTIL_Remove(this);
		}
		return true;
	}

	// BETA
	/*if (ITEM_GiveAmmoTF(pPlayer, 1, "FlareRound"))
	{
		if (g_pGameRules->ItemShouldRespawn(this) == GR_ITEM_RESPAWN_NO)
		{
			UTIL_Remove(this);
		}
		return true;
	}*/

	if (ITEM_GiveAmmoTF(pPlayer, SIZE_AMMO_RPG_ROUND, "RPG_Round"))
	{
		if (g_pGameRules->ItemShouldRespawn(this) == GR_ITEM_RESPAWN_NO)
		{
			UTIL_Remove(this);
		}
		return true;
	}

	if (ITEM_GiveAmmoTF(pPlayer, SIZE_AMMO_SMG1_GRENADE, "SMG1_Grenade"))
	{
		if (g_pGameRules->ItemShouldRespawn(this) == GR_ITEM_RESPAWN_NO)
		{
			UTIL_Remove(this);
		}
		return true;
	}

	if (ITEM_GiveAmmoTF(pPlayer, SIZE_AMMO_BUCKSHOT, "Buckshot"))
	{
		if (g_pGameRules->ItemShouldRespawn(this) == GR_ITEM_RESPAWN_NO)
		{
			UTIL_Remove(this);
		}
		return true;
	}

	if (ITEM_GiveAmmoTF(pPlayer, SIZE_AMMO_AR2_ALTFIRE, "AR2AltFire"))
	{
		if (g_pGameRules->ItemShouldRespawn(this) == GR_ITEM_RESPAWN_NO)
		{
			UTIL_Remove(this);
		}
		return true;
	}

	return false;
}
