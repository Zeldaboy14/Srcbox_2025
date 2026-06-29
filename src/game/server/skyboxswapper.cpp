//=========== Copyright (c) Valve Corporation, All rights reserved. ===========
//
// Simple entity to switch the map's 2D skybox texture when triggered.
//
//=============================================================================

#include "cbase.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"


//-----------------------------------------------------------------------------
// CSkyboxSwapper
//-----------------------------------------------------------------------------
class CSkyboxSwapper : public CServerOnlyPointEntity
{
	DECLARE_CLASS( CSkyboxSwapper, CServerOnlyPointEntity );
public:
	DECLARE_DATADESC();

	virtual void Spawn( void );
	virtual void Precache( void );

	void InputTrigger( inputdata_t &inputdata );

protected:
	string_t m_iszSkyboxName;
};

LINK_ENTITY_TO_CLASS(skybox_swapper, CSkyboxSwapper);

BEGIN_DATADESC( CSkyboxSwapper )
	DEFINE_KEYFIELD( m_iszSkyboxName, FIELD_STRING, "SkyboxName" ),
	// Inputs
	DEFINE_INPUTFUNC(FIELD_VOID, "Trigger", InputTrigger),
END_DATADESC()


//-----------------------------------------------------------------------------
// Purpose: not much
//-----------------------------------------------------------------------------
void CSkyboxSwapper::Spawn( void )
{
	Precache();
}

//-----------------------------------------------------------------------------
// Purpose: Precache the skybox materials.
//-----------------------------------------------------------------------------
void CSkyboxSwapper::Precache( void )
{
	if ( Q_strlen( m_iszSkyboxName.ToCStr() ) == 0 )
	{
		Warning( "skybox_swapper (%s) has no skybox specified!\n", GetEntityName().ToCStr() );
		return;
	}

	char  name[ MAX_PATH ];
	char *skyboxsuffix[ 6 ] = { "rt", "bk", "lf", "ft", "up", "dn" };
	for ( int i = 0; i < 6; i++ )
	{
		Q_snprintf( name, sizeof( name ), "skybox/%s%s", m_iszSkyboxName.ToCStr(), skyboxsuffix[i] );
		PrecacheMaterial( name );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Input handler that triggers the skybox swap.
//-----------------------------------------------------------------------------
void CSkyboxSwapper::InputTrigger( inputdata_t &inputdata )
{
	static ConVarRef skyname( "sv_skyname", false );
	if ( !skyname.IsValid() )
	{
		Warning( "skybox_swapper (%s) trigger input failed - cannot find 'sv_skyname' convar!\n", GetEntityName().ToCStr() );
		return;
	}
	skyname.SetValue( m_iszSkyboxName.ToCStr() );
}

// -----------------------------------------------------------------------------
// Purpose: Since we can't support gmod's painted sky so easily we are just gonna apply an existing skybox 
// that way maps are more playable with less of a headache from the buggy sky - VVis :3 
// FCTODO: See if we can actually get it working as it should... probably engine level change though? 
// -----------------------------------------------------------------------------
/*
class CEnvSkyPaint : public CServerOnlyPointEntity
{
public:
	DECLARE_CLASS(CEnvSkyPaint, CServerOnlyPointEntity);

	CEnvSkyPaint();

	void Spawn() override;

private:
	void ForceSkyFallback();
};

LINK_ENTITY_TO_CLASS(env_skypaint, CEnvSkyPaint);

CEnvSkyPaint::CEnvSkyPaint()
{
}

void CEnvSkyPaint::Spawn()
{
	BaseClass::Spawn();

	ForceSkyFallback();
}

void CEnvSkyPaint::ForceSkyFallback()
{
	static ConVarRef skyname("sv_skyname");

	if (!skyname.IsValid())
		return;

	const char* current = skyname.GetString();

	if (!current)
		return;

	if (!Q_stricmp(current, "painted"))
	{
		skyname.SetValue("painted");
	}
}*/
