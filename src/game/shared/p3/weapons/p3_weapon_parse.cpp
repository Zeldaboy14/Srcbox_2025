//========= Copyright © 1996-2005, Valve Corporation, All rights reserved. ============//
//
// Purpose: Weapon data file parsing, shared by game & client dlls.
//
// $NoKeywords: $
//=============================================================================//
#include "cbase.h"
#include "p3_weapon_parse.h"
#include <KeyValues.h>
#include <tier0/mem.h>
#include "filesystem.h"
#include "utldict.h"
#include "ammodef.h"
//#include "hud.h"
#include "p3_weapon_shared.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"



P3_FileWeaponInfo_t::P3_FileWeaponInfo_t()
	: FileWeaponInfo_t()
	, iconJointlyUseability( NULL )
	, iconJointlyUseabilityActive( NULL )
	, iWeaponType( 0 )
{
	szJointlyUseabilityWeaponName[0] = 0;
}

void P3_FileWeaponInfo_t::Parse( KeyValues *pKeyValuesData, const char *szWeaponName )
{
	if ( pKeyValuesData )
	{
		KeyValues *pJointlyUseabilitySection = pKeyValuesData->FindKey( "JointlyUseabilityData" );
		if ( pJointlyUseabilitySection  )
		{
			const char *name = pJointlyUseabilitySection->GetString( "jointly_weapon" );
			if ( name )
			{
				Q_strncpy( szJointlyUseabilityWeaponName, name, sizeof( szJointlyUseabilityWeaponName ) );
			}
		}

		const char *pWeaponType = pKeyValuesData->GetString( "weapon_type" );
		if ( pWeaponType )
		{
			iWeaponType = ParseWeaponType( pWeaponType );
		}

		//Читаем идентификатор оружия
		const char *sWeaponID = pKeyValuesData->GetString( "ID" );
		if ( sWeaponID )
		{
			iWeaponID = ParseWeaponID( sWeaponID );
		}
		else
			iWeaponID = 0;//pKeyValuesData->GetInt( "ID", 0);

		//Читаем идентификатор оружия
		iRecoil = pKeyValuesData->GetInt( "recoil", 0);

		flPrimaryMinDamage = pKeyValuesData->GetFloat( "PrimaryMinDamage", 0 );
		flPrimaryMaxDamage = pKeyValuesData->GetFloat( "PrimaryMaxDamage", 0 );

		flSecondaryMinDamage = pKeyValuesData->GetFloat( "SecondaryMinDamage", 0 );
		flSecondaryMaxDamage = pKeyValuesData->GetFloat( "SecondaryMaxDamage", 0 );

		flNPCMinDamage = pKeyValuesData->GetFloat( "NPCMinDamage", 0 );
		flNPCMaxDamage = pKeyValuesData->GetFloat( "NPCMaxDamage", 0 );


		Assert(iWeaponID != 0);
	}

	FileWeaponInfo_t::Parse( pKeyValuesData, szWeaponName );
}

FileWeaponInfo_t* CreateWeaponInfo()
{
	return new P3_FileWeaponInfo_t;
}

