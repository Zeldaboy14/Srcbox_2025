//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: CTF AmmoPack.
//
//=============================================================================//
#include "cbase.h"
#include "items.h"
#include "hl2mp_gamerules.h"
#include "shareddefs.h"
#include "player.h"
#include "team.h"
#include "engine/IEngineSound.h"
#include "entity_ammopack.h"
#include "gamestats.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

//=============================================================================
//
// CTF AmmoPack defines.
//

#define TF_AMMOPACK_PICKUP_SOUND	"AmmoPack.Touch"

LINK_ENTITY_TO_CLASS( item_ammopack_full, CAmmoPack );
LINK_ENTITY_TO_CLASS( item_ammopack_small, CAmmoPack);
LINK_ENTITY_TO_CLASS( item_ammopack_medium, CAmmoPack);

//=============================================================================
//
// CTF AmmoPack functions.
//

//-----------------------------------------------------------------------------
// Purpose: Spawn function for the ammopack
//-----------------------------------------------------------------------------
void CAmmoPack::Spawn(void)
{
	Precache();
	/*if (gEntList.FindEntityByName(nullptr, "item_ammopack_small")) {
		SetModel("models/items/ammopack_small.mdl");
	} else if (gEntList.FindEntityByName(nullptr, "item_ammopack_medium")) {
		SetModel("models/items/ammopack_medium.mdl");
	} else if (gEntList.FindEntityByName(nullptr, "item_ammopack_full")) {
		SetModel("models/items/ammopack_full.mdl");
	}*/
	SetModel("models/items/ammopack_medium.mdl");

	BaseClass::Spawn();

	BaseClass::SetOriginalSpawnOrigin(GetAbsOrigin());
	BaseClass::SetOriginalSpawnAngles(GetAbsAngles());

	VPhysicsDestroyObject();
	SetMoveType(MOVETYPE_NONE);
	SetSolidFlags(FSOLID_NOT_SOLID | FSOLID_TRIGGER);

	ResetSequence(LookupSequence("idle"));
}

//-----------------------------------------------------------------------------
// Purpose: Precache function for the ammopack
//-----------------------------------------------------------------------------
void CAmmoPack::Precache(void)
{
	PrecacheScriptSound(TF_AMMOPACK_PICKUP_SOUND);
	PrecacheModel(TF_AMMOPACK_LARGE_BDAY); // always precache this for PyroVision
	PrecacheModel("models/items/ammopack_small.mdl");
	PrecacheModel("models/items/ammopack_medium.mdl");
	PrecacheModel("models/items/ammopack_large.mdl");

	BaseClass::Precache();
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
bool CAmmoPack::ValidTouch(CBasePlayer* pPlayer)
{

	// Only touch a live player.
	if (!pPlayer || !pPlayer->IsPlayer() || !pPlayer->IsAlive())
	{
		return false;
	}

	return true;
}

//-----------------------------------------------------------------------------
// Purpose: MyTouch function for the ammopack
//-----------------------------------------------------------------------------
bool CAmmoPack::MyTouch( CBasePlayer *pPlayer )
{
	bool bSuccess = false;

	if ( ValidTouch( pPlayer ) )
	{
		CBasePlayer *pTFPlayer = ToBasePlayer( pPlayer );
		if ( !pTFPlayer )
			return false;

		//float flPackRatio = PackRatios[GetPowerupSize()];

		// did we give them anything?
		if ( bSuccess )
		{
			CSingleUserRecipientFilter filter( pPlayer );
			EmitSound( filter, entindex(), TF_AMMOPACK_PICKUP_SOUND );

			//CTF_GameStats.Event_PlayerAmmokitPickup( pTFPlayer );

			IGameEvent * event = gameeventmanager->CreateEvent( "item_pickup" );
			if( event )
			{
				event->SetInt( "userid", pPlayer->GetUserID() );
				event->SetString( "item", GetAmmoPackName() );
				gameeventmanager->FireEvent( event );
			}
		}
	}

	return bSuccess;
}
