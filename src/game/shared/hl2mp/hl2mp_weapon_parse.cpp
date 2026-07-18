//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
//=============================================================================//

#include "cbase.h"
#include <KeyValues.h>
#include "hl2mp_weapon_parse.h"
#include "ammodef.h"

const char* nullStr = NULL;

FileWeaponInfo_t* CreateWeaponInfo()
{
	return new CHL2MPSWeaponInfo;
}



CHL2MPSWeaponInfo::CHL2MPSWeaponInfo()
{
	m_iPlayerDamage = 0;
}


void CHL2MPSWeaponInfo::Parse( KeyValues *pKeyValuesData, const char *szWeaponName )
{
	BaseClass::Parse( pKeyValuesData, szWeaponName );

	m_iPlayerDamage = pKeyValuesData->GetInt( "damage", 0 );

	// Terror Data

	// Punch
/*	m_verticalPunch = pKeyValuesData->GetFloat("VerticalPunch", m_verticalPunch);
	m_horizontalPunch = pKeyValuesData->GetFloat("HorizontalPunch", m_horizontalPunch);
	m_horizontalPunchDirChance = pKeyValuesData->GetFloat("HorizontalPunchDirChance", m_horizontalPunchDirChance);

	// Spread
	m_spreadPerShot = pKeyValuesData->GetFloat("SpreadPerShot", m_spreadPerShot);
	m_maxSpread = pKeyValuesData->GetFloat("MaxSpread", m_maxSpread);
	m_spreadDecay = pKeyValuesData->GetFloat("SpreadDecay", m_spreadDecay);
	m_minDuckingSpread = pKeyValuesData->GetFloat("MinDuckingSpread", m_minDuckingSpread);
	m_minStandingSpread = pKeyValuesData->GetFloat("MinStandingSpread", m_minStandingSpread);
	m_minInAirSpread = pKeyValuesData->GetFloat("MinInAirSpread", m_minInAirSpread);
	m_maxMovementSpread = pKeyValuesData->GetFloat("MaxMovementSpread", m_maxMovementSpread);

	// Scatter
	m_pelletScatter.x = pKeyValuesData->GetFloat("PelletScatterPitch", m_pelletScatter.x);
	m_pelletScatter.y = pKeyValuesData->GetFloat("PelletScatterYaw", m_pelletScatter.y);

	// Durations
	m_reloadDuration = pKeyValuesData->GetFloat("ReloadDuration", m_reloadDuration);
	m_dualReloadDuration = pKeyValuesData->GetFloat("DualReloadDuration", m_dualReloadDuration);
	m_deployDuration = pKeyValuesData->GetFloat("DeployDuration", m_deployDuration);
	m_dualDeployDuration = pKeyValuesData->GetFloat("DualDeployDuration", m_dualDeployDuration);

	// Wall penetration
	m_penetrationNumLayers = pKeyValuesData->GetFloat("PenetrationNumLayers", m_penetrationNumLayers);
	m_penetrationPower = pKeyValuesData->GetFloat("PenetrationPower", m_penetrationPower);
	m_penetrationMaxDistance = pKeyValuesData->GetFloat("PenetrationMaxDistance", m_penetrationMaxDistance);
	m_characterPenetrationMaxDistance = pKeyValuesData->GetFloat("CharacterPenetrationMaxDistance", m_characterPenetrationMaxDistance);

	m_flGainRange = pKeyValuesData->GetFloat("GainRange", m_flGainRange);

	// Auto aim
	m_maxAutoAimDeflection1 = pKeyValuesData->GetFloat("MaxAutoAimDeflection1", m_maxAutoAimDeflection1);
	m_maxAutoAimRange1 = pKeyValuesData->GetFloat("MaxAutoAimRange1", m_maxAutoAimRange1);
	m_weaponAutoAimScale = pKeyValuesData->GetFloat("WeaponAutoAimScale", m_weaponAutoAimScale);

	m_loadoutSlots = pKeyValuesData->GetFloat("LoadoutSlots", m_loadoutSlots);*/

	// Models
#ifdef USE_L4D_WEAPON_MODELS
	Q_strcpy(m_dualPlayerModel, pKeyValuesData->GetString("playermodel_dual", ""));
	Q_strcpy(m_dualWorldModel, pKeyValuesData->GetString("worldmodel_dual", ""));
	Q_strcpy(m_addonAttachmentName, pKeyValuesData->GetString("AddonAttachment", ""));
	KeyValues* pVMKey = pKeyValuesData->FindKey("CharacterViewmodel");
	if (pVMKey)
	{
		for (KeyValues* pKey = pVMKey->GetFirstValue(); pKey != NULL; pKey = pKey->GetNextValue())
		{
			const char* characterName = pKey->GetName();
			const char* modelName = pKey->GetString(nullStr);

			char* szCharacter = new char[strlen(characterName) + 1];
			strcpy(szCharacter, characterName);
			char* szViewModel = new char[strlen(modelName) + 1];
			strcpy(szViewModel, modelName);

			if (m_viewmodels.Find(szCharacter) == m_viewmodels.InvalidIndex())
				m_viewmodels.Insert(szCharacter, szViewModel);
		}
	}
	pVMKey = pKeyValuesData->FindKey("DualCharacterViewmodel");
	if (pVMKey)
	{
		for (KeyValues* pKey = pVMKey->GetFirstValue(); pKey != NULL; pKey = pKey->GetNextValue())
		{
			const char* characterName = pKey->GetName();
			const char* modelName = pKey->GetString(nullStr);

			char* szCharacter = new char[strlen(characterName) + 1];
			strcpy(szCharacter, characterName);
			char* szViewModel = new char[strlen(modelName) + 1];
			strcpy(szViewModel, modelName);

			if (m_dualViewmodels.Find(szCharacter) == m_dualViewmodels.InvalidIndex())
				m_dualViewmodels.Insert(szCharacter, szViewModel);
		}
	}

	pVMKey = pKeyValuesData->FindKey("CharacterViewmodelAddon");
	if (pVMKey)
	{
		for (KeyValues* pKey = pVMKey->GetFirstValue(); pKey != NULL; pKey = pKey->GetNextValue())
		{
			const char* characterName = pKey->GetName();
			const char* modelName = pKey->GetString(nullStr);

			char* szCharacter = new char[strlen(characterName) + 1];
			strcpy(szCharacter, characterName);
			char* szViewModel = new char[strlen(modelName) + 1];
			strcpy(szViewModel, modelName);

			if (m_viewmodelAddons.Find(szCharacter) == m_viewmodelAddons.InvalidIndex())
				m_viewmodelAddons.Insert(szCharacter, szViewModel);
		}
	}
#endif
}

#ifdef USE_L4D_WEAPON_MODELS
const char* CHL2MPSWeaponInfo::GetViewModelAddonName(const char* character, bool dualWielding)
{
	if (m_viewmodelAddons.Find(character) == m_viewmodelAddons.InvalidIndex())
		return "";

	return m_viewmodelAddons[m_viewmodelAddons.Find(character)];
}

CHL2MPSWeaponInfo *CBaseCombatWeapon::GetTerrorWeaponData(void) const
{
	return (CHL2MPSWeaponInfo*)&GetWpnData();
}
#endif


