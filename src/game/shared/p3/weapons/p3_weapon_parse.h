//========= Copyright © 1996-2005, Valve Corporation, All rights reserved. ============//
//
// Purpose: Weapon data file parsing, shared by game & client dlls.
//
// $NoKeywords: $
//=============================================================================//

#ifndef P3_WEAPON_PARSE_H
#define P3_WEAPON_PARSE_H
#ifdef _WIN32
#pragma once
#endif

#include "shareddefs.h"
#include "weapon_parse.h"


//-----------------------------------------------------------------------------
// Purpose: Contains the data read from the weapon's script file.
// It's cached so we only read each weapon's script file once.
// Each game provides a CreateWeaponInfo function so it can have game-specific
// data (like CS move speeds) in the weapon script.
//-----------------------------------------------------------------------------
class P3_FileWeaponInfo_t : public FileWeaponInfo_t
{
public:

	P3_FileWeaponInfo_t();

	// Each game can override this to get whatever values it wants from the script.
	virtual void Parse( KeyValues *pKeyValuesData, const char *szWeaponName );

	virtual CHudTexture *GetSpriteJointly() const { return iconJointlyUseability; }
	virtual CHudTexture *GetSpriteJointlyActive() const { return iconJointlyUseabilityActive; }
	virtual const char *GetJointlyWeaponName() const { return szJointlyUseabilityWeaponName; }

	char	szJointlyUseabilityWeaponName[MAX_WEAPON_STRING];

	CHudTexture						*iconJointlyUseability;
	CHudTexture						*iconJointlyUseabilityActive;

	int		iWeaponType;

	/**
	 * Идентификатор оружия
	 */
	int iWeaponID;

	int iRecoil;

public:
	int GetWeaponType() const { return iWeaponType; }
};

#endif // P3_WEAPON_PARSE_H
