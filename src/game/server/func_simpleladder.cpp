#include "cbase.h"
#include "func_simpleladder.h"
#include "team.h"
#include "hl2mp_gamerules.h"

extern ConVar sv_showladders;

IMPLEMENT_SERVERCLASS_ST( CFuncSimpleLadder, DT_FuncSimpleLadder )
	SendPropVector( SENDINFO( m_climbableNormal ) )
END_SEND_TABLE()

LINK_ENTITY_TO_CLASS( func_simpleladder, CFuncSimpleLadder );
LINK_ENTITY_TO_CLASS( info_ladder, CFuncSimpleLadder );

void CFuncSimpleLadder::Spawn( void )
{
	SetClassname( "func_simpleladder" );
	Precache();
	SetSolid( SOLID_BSP );
	SetModel( GetModelName().ToCStr() );
}

void CFuncSimpleLadder::Activate( void )
{
	BaseClass::Activate();
	if ( sv_showladders.GetBool() )
	{
		m_debugOverlays |= OVERLAY_TEXT_BIT;
	}
}

bool CFuncSimpleLadder::KeyValue( const char *szKeyName, const char *szValue )
{
	if ( FStrEq( szValue, "normal.x" ) )
	{
		m_climbableNormal.SetX( strtod( szValue, NULL ) );
		return true;
	}

	if ( FStrEq( szValue, "normal.y" ) )
	{
		m_climbableNormal.SetY( strtod( szValue, NULL ) );
		return true;
	}

	if ( FStrEq( szValue, "normal.z" ) )
	{
		m_climbableNormal.SetZ( strtod( szValue, NULL ) );
		return true;
	}

	if ( FStrEq( szValue, "team" ) )
	{
		int team = strtol( szValue, 0, 10 );
		if ( team == 1 )
		{
			//ChangeTeam( TEAM_SURVIVOR );
			ChangeTeam( TEAM_REBELS );
		}
		if ( team == 2 )
		{
			//ChangeTeam( TEAM_ZOMBIE );
			ChangeTeam( TEAM_COMBINE );
		}
		else
		{
			ChangeTeam( TEAM_UNASSIGNED );
		}
		return true;
	}

	return BaseClass::KeyValue( szKeyName, szValue );
}

int CFuncSimpleLadder::DrawDebugTextOverlays( void )
{
	int text_offset = BaseClass::DrawDebugTextOverlays();

	char tempstr[512];
	V_snprintf( tempstr, sizeof( tempstr ), "Team: %s", GetGlobalTeam( GetTeamNumber() )->GetName() );
	EntityText( text_offset, tempstr, 0.0 );

	return ++text_offset;
}

void CFuncSimpleLadder::DrawDebugGeometryOverlays( void )
{
	BaseClass::DrawDebugGeometryOverlays();
}