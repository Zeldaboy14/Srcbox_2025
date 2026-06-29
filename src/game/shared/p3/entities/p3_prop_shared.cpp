#include "cbase.h"
#include "p3_prop_shared.h"

//-----------------------------------------------------------------------------
// CP3_StuffItemsRegistry
//-----------------------------------------------------------------------------

CP3_StuffItemsRegistry::CP3_StuffItemsRegistry()
    : CAutoGameSystem( "CP3_StuffItemsRegistry" )
{
}

void
CP3_StuffItemsRegistry::LevelInitPreEntity()
{
	ParseStuffItemsData();
}

void
CP3_StuffItemsRegistry::LevelShutdownPostEntity()
{
	ClearStuffItemsData();
}

const StuffItemData& 
CP3_StuffItemsRegistry::FindStuffItemData( const char* szModel )
{
	char name[256] = { 0 };
	Q_FileBase( szModel, name, sizeof name );

	int i = m_Data.Find( name );
	if ( i == m_Data.InvalidIndex() )
	{
		i = m_Data.Find( "default" );
		Assert( i != m_Data.InvalidIndex() );
	}

	return m_Data[i];
}

void
CP3_StuffItemsRegistry::ParseStuffItemsData()
{
	KeyValues* pKV = new KeyValues( "StuffItemsRegistry" );
	pKV->LoadFromFile( filesystem, "scripts/stuff_items.txt" );

	for ( KeyValues* pDataKV = pKV->GetFirstTrueSubKey(); pDataKV; pDataKV = pDataKV->GetNextTrueSubKey() )
	{
		StuffItemData data;
		memset( &data, 0, sizeof data );

		ParseStuffItemsDataKV( data, pDataKV, pKV );

		m_Data.Insert( pDataKV->GetName(), data );
	}

	pKV->deleteThis();
}

void
CP3_StuffItemsRegistry::ParseString( char* data, int dataSize, const char* key, KeyValues* pKV )
{
	Q_strncpy( data, pKV->GetString( key, data ), dataSize );
}

void
CP3_StuffItemsRegistry::ParseStuffItemsDataKV( StuffItemData& data, KeyValues* pKV, KeyValues* pAllKV )
{
	const char* base = pKV->GetString( "base", "default" );
	KeyValues* pBaseKV = pAllKV->FindKey( base );

	if ( pBaseKV != pKV )
	{
		if ( pBaseKV != NULL )
		{
			ParseStuffItemsDataKV( data, pBaseKV, pAllKV );
		}
		else
		{
			DevWarning( "StuffItem \"%s\" base \"%s\" not found\n", pKV->GetName(), base );
			return;
		}
	}

	ParseString( data.szShotDamageEffectName, STUFF_ITEM_STRING_LENGTH, "shot_damage_effect_name", pKV );
	ParseString( data.szShotDestroyEffectName, STUFF_ITEM_STRING_LENGTH, "shot_destroy_effect_name", pKV );
	data.flShotDamageEffectDuration = pKV->GetFloat( "shot_damage_effect_duration", data.flShotDamageEffectDuration );
	data.flShotDestroyEffectDuration = pKV->GetFloat( "shot_destroy_effect_duration", data.flShotDestroyEffectDuration );

	ParseString( data.szDamageSplashesDecalName, STUFF_ITEM_STRING_LENGTH, "damage_splashes_decal_name", pKV );
	ParseString( data.szDestroySplashesDecalName, STUFF_ITEM_STRING_LENGTH, "destroy_splashes_decal_name", pKV );
	ParseString( data.szDestroyDecalName, STUFF_ITEM_STRING_LENGTH, "destroy_decal_name", pKV );

	data.flDestroyExplosionRadius= pKV->GetFloat( "destroy_explosion_radius", 0 );
	data.flDestroyExplosionDamage = pKV->GetFloat( "destroy_explosion_damage", 0 );
	ParseString( data.szDestroyExplosionSound, STUFF_ITEM_STRING_LENGTH, "destroy_explosion_sound", pKV );

	data.flHealth = pKV->GetFloat( "health", data.flHealth );

	data.bMotionDisabled = !!pKV->GetInt( "sleep", data.bMotionDisabled );
}

void
CP3_StuffItemsRegistry::ClearStuffItemsData()
{
	m_Data.RemoveAll();
}

CP3_StuffItemsRegistry g_CompoundObjectsRegistry;
