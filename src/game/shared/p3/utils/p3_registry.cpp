#include "cbase.h"
#include "keyvalues.h"
#include "filesystem.h"

#define REGISTRY_FILE "scripts/p3_registry.txt"

//-----------------------------------------------------------------------------
// СP3_Registry -- грузит инишник при старте уровня
//-----------------------------------------------------------------------------

class СP3_Registry : public CAutoGameSystem
{
public:
	СP3_Registry() : CAutoGameSystem( "СP3_Registry" )
	{
		m_pValues = new KeyValues( REGISTRY_FILE );
	}

	~СP3_Registry()
	{
		m_pValues->deleteThis();
	}

	// CAutoGameSystem
	virtual void LevelInitPreEntity()
	{
		LoadFromFile();
	}

	void LoadFromFile()
	{
		m_pValues->Clear();

		if ( !m_pValues->LoadFromFile( filesystem, REGISTRY_FILE, "GAME" ) )
		{
			Warning( "СP3_Registry: Unable to load file '%s'\n", REGISTRY_FILE );
		}
	}

	KeyValues* m_pValues;
};

static СP3_Registry g_Registry;

//-----------------------------------------------------------------------------
// Функции
//-----------------------------------------------------------------------------

float P3_Registry_GetFloat( const char* key, float defaultValue /*= 0*/ )
{
	return g_Registry.m_pValues->GetFloat( key, defaultValue );
}

int P3_Registry_GetInt( const char* key, int defaultValue /*= 0*/ )
{
	return g_Registry.m_pValues->GetInt( key, defaultValue );
}

const char*	P3_Registry_GetString( const char* key, const char* defaultValue /*= ""*/ )
{
	return g_Registry.m_pValues->GetString( key, defaultValue );
}