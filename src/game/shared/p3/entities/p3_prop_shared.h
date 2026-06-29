//========= Copyright © 2010, TM Studios, All rights reserved. ================//
// Author: Igor Karatayev
//=============================================================================//

#ifndef P3_PORP_SHARED
#define P3_PORP_SHARED
#ifdef _MSC_VER
#pragma once
#endif

#define STUFF_ITEM_STRING_LENGTH	32

//-----------------------------------------------------------------------------
// StuffItemData
//-----------------------------------------------------------------------------

struct StuffItemData
{
	char	szShotDamageEffectName[STUFF_ITEM_STRING_LENGTH];
	char	szShotDestroyEffectName[STUFF_ITEM_STRING_LENGTH];
	float	flShotDamageEffectDuration;
	float	flShotDestroyEffectDuration;

	char	szDamageSplashesDecalName[STUFF_ITEM_STRING_LENGTH];
	char	szDestroySplashesDecalName[STUFF_ITEM_STRING_LENGTH];
	char	szDestroyDecalName[STUFF_ITEM_STRING_LENGTH];

	float	flDestroyExplosionRadius;
	float	flDestroyExplosionDamage;
	char	szDestroyExplosionSound[STUFF_ITEM_STRING_LENGTH];

	float	flHealth;

	bool	bMotionDisabled;
};


//-----------------------------------------------------------------------------
// CP3_StuffItemsRegistry
//-----------------------------------------------------------------------------

class CP3_StuffItemsRegistry : public CAutoGameSystem
{
public:
	CP3_StuffItemsRegistry();

	virtual void LevelInitPreEntity();
	virtual void LevelShutdownPostEntity();

	const StuffItemData& FindStuffItemData( const char* szModel );

private:
	void ParseStuffItemsData();
	void ParseString( char* data, int dataSize, const char* key, KeyValues* pKV );
	void ParseStuffItemsDataKV( StuffItemData& data, KeyValues* pKV, KeyValues* pAllKV );

	void ClearStuffItemsData();

private:
	CUtlDict< StuffItemData, int > m_Data;
};

extern CP3_StuffItemsRegistry g_CompoundObjectsRegistry;

#endif // P3_PORP_SHARED
