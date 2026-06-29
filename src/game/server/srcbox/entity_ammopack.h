//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: CTF AmmoPack.
//
//=============================================================================//
#ifndef ENTITY_AMMOPACK_H
#define ENTITY_AMMOPACK_H

#ifdef _WIN32
#pragma once
#endif

//#include "../tf/tf_powerup.h"
//#include "../tf/tf_gamerules.h"

#define kHoliday_TFBirthday 2

#define TF_AMMOPACK_SMALL_BDAY		"models/items/ammopack_small_bday.mdl"
#define TF_AMMOPACK_MEDIUM_BDAY		"models/items/ammopack_medium_bday.mdl"
#define TF_AMMOPACK_LARGE_BDAY		"models/items/ammopack_large_bday.mdl"

//=============================================================================
//
// CTF AmmoPack class.
//

//class CAmmoPack : public CTFPowerup
class CAmmoPack : public CItem
{
public:
	DECLARE_CLASS( CAmmoPack, CItem);

	void	Spawn( void );
	virtual void Precache( void );
	bool	MyTouch( CBasePlayer *pPlayer );

	//powerupsize_t	GetPowerupSize( void ) { return POWERUP_FULL; }
	virtual const char *GetAmmoPackName( void ) { return "ammopack_large"; }
	virtual bool	ValidTouch(CBasePlayer* pPlayer);
};

#endif // ENTITY_AMMOPACK_H


