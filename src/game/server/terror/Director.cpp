//========= Srcbox: Left 4 Dead's info_director ( basic recreation ) ==========//
//
// How a L4D map plays its intro, and what this file does for it:
//
//  1. info_director fires OnGameplayStart once per map load, when the first player
//     is in the game ( spawned, alive and sending commands ) plus director_gameplay_start_delay.
//     The player is the activator, so !activator and the cameras the relay enables
//     ( point_viewcontrol_survivor binds to its activator ) get the right player.
//  2. The map wires OnGameplayStart to Trigger "relay_intro_start", which runs the intro.
//  3. The relay calls these inputs:
//     StartIntro                 players can't take damage ( FL_GODMODE )
//     ForceSurvivorPositions     players go to the info_survivor_position entities
//                                ( by their Order ), frozen and with the HUD hidden
//     ReleaseSurvivorPositions   unfreezes them and shows the HUD again
//     FinishIntro                damage again
//  4. Every other L4D / L4D2 director input is accepted and ignored, so maps don't
//     spam "unhandled input" ( there are no infected to direct here ).
//
// director_gameplay_start  ( cheat )  fires OnGameplayStart again, to test an intro.
//
//=============================================================================//

#include "cbase.h"
#include "director.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

static ConVar director_gameplay_start_delay( "director_gameplay_start_delay", "0.0", FCVAR_NONE, "Seconds after the first player is in the game before info_director fires OnGameplayStart." );
static ConVar director_debug( "director_debug", "0", FCVAR_NONE, "Print info_director's inputs and outputs." );

#define DirectorMsg( ... ) { if ( director_debug.GetBool() ) Msg( "[Director] " __VA_ARGS__ ); }

//-----------------------------------------------------------------------------
// The director's state ( one per map: L4D only ever has one info_director )
//-----------------------------------------------------------------------------
class CDirector
{
public:
	CDirector() : m_bIsPlayingIntro( false ), m_bSurvivorPositionsForced( false ) {}

	void OnStartIntro( void );
	void OnFinishIntro( void );
	void ForceSurvivorPositions( void );
	void ReleaseSurvivorPositions( void );

	void Reset( void )
	{
		m_bIsPlayingIntro = false;
		m_bSurvivorPositionsForced = false;
	}

	bool m_bIsPlayingIntro;
	bool m_bSurvivorPositionsForced;
};

static CDirector s_Director;

bool Director_IsPlayingIntro()
{
	return s_Director.m_bIsPlayingIntro;
}

//-----------------------------------------------------------------------------
// info_survivor_position: where ForceSurvivorPositions puts the survivors.
// Also a spawn point ( player.cpp / hl2mp_player.cpp look them up by classname ).
//
// SetViewControl <camera>: the survivor at this spot looks through that camera. L4D intros
// send it before or after ForceSurvivorPositions ( c11m1: right after, c8m1: seconds later,
// as the hand-off from the shared point_viewcontrol_multiplayer ), so it applies either way.
//-----------------------------------------------------------------------------
class CInfoSurvivorPosition : public CPointEntity
{
public:
	DECLARE_CLASS( CInfoSurvivorPosition, CPointEntity );
	DECLARE_DATADESC();

	void InputSetViewControl( inputdata_t &inputdata );

	// Puts this spot's survivor on its camera ( if both are set )
	void ApplyViewControl( void );

	int			m_iOrder = 0;					// 1 - 4: which survivor goes here ( L4D maps often give them all 1 )
	string_t	m_iszSurvivorName = NULL_STRING;	// L4D: which survivor ( Bill, Zoey, ... )
	string_t	m_iszGameMode = NULL_STRING;		// L4D: only for this mode ( scavenge, ... ); empty = coop
	EHANDLE		m_hViewControl;					// from SetViewControl
	CHandle< CBasePlayer > m_hPlayer;			// who ForceSurvivorPositions put here
};

LINK_ENTITY_TO_CLASS( info_survivor_position, CInfoSurvivorPosition );

BEGIN_DATADESC( CInfoSurvivorPosition )
	DEFINE_KEYFIELD( m_iOrder, FIELD_INTEGER, "Order" ),
	DEFINE_KEYFIELD( m_iszSurvivorName, FIELD_STRING, "SurvivorName" ),
	DEFINE_KEYFIELD( m_iszGameMode, FIELD_STRING, "GameMode" ),
	DEFINE_FIELD( m_hViewControl, FIELD_EHANDLE ),
	DEFINE_FIELD( m_hPlayer, FIELD_EHANDLE ),
	DEFINE_INPUTFUNC( FIELD_STRING, "SetViewControl", InputSetViewControl ),
END_DATADESC()

void CInfoSurvivorPosition::InputSetViewControl( inputdata_t &inputdata )
{
	const char *pszCamera = inputdata.value.String();
	m_hViewControl = ( pszCamera && pszCamera[0] ) ? gEntList.FindEntityByName( NULL, pszCamera, this, inputdata.pActivator, inputdata.pCaller ) : NULL;

	DirectorMsg( "%s SetViewControl %s%s\n", GetDebugName(), pszCamera ? pszCamera : "", m_hViewControl ? "" : " ( not found )" );
	ApplyViewControl();
}

void CInfoSurvivorPosition::ApplyViewControl( void )
{
	CBaseEntity *pCamera = m_hViewControl.Get();
	CBasePlayer *pPlayer = m_hPlayer.Get();
	if ( !pCamera || !pPlayer || !pPlayer->IsAlive() )
		return;

	// The camera binds to its activator ( point_viewcontrol_survivor ) or takes the
	// activator as one more viewer ( point_viewcontrol_multiplayer )
	variant_t emptyVariant;
	const char *pszInput = FClassnameIs( pCamera, "point_viewcontrol_multiplayer" ) ? "AddPlayer" : "Enable";
	pCamera->AcceptInput( pszInput, pPlayer, this, emptyVariant, 0 );

	DirectorMsg( "%s: %s now looks through %s\n", GetDebugName(), pPlayer->GetPlayerName(), pCamera->GetDebugName() );
}

//-----------------------------------------------------------------------------
// Intro
//-----------------------------------------------------------------------------
void CDirector::OnStartIntro( void )
{
	m_bIsPlayingIntro = true;

	for ( int i = 1; i <= gpGlobals->maxClients; i++ )
	{
		CBasePlayer *pPlayer = UTIL_PlayerByIndex( i );
		if ( pPlayer )
			pPlayer->AddFlag( FL_GODMODE );
	}
}

void CDirector::OnFinishIntro( void )
{
	m_bIsPlayingIntro = false;

	for ( int i = 1; i <= gpGlobals->maxClients; i++ )
	{
		CBasePlayer *pPlayer = UTIL_PlayerByIndex( i );
		if ( pPlayer )
			pPlayer->RemoveFlag( FL_GODMODE );
	}
}

// Positions sort by Order ( unset last ), then by name ( survivorPos_intro_01 before _02 ).
// Positions for another game mode ( GameMode set, e.g. scavenge ) are skipped.
static int SurvivorPositionCompare( CInfoSurvivorPosition * const *ppA, CInfoSurvivorPosition * const *ppB )
{
	const int nA = (*ppA)->m_iOrder > 0 ? (*ppA)->m_iOrder : 0x7fffffff;
	const int nB = (*ppB)->m_iOrder > 0 ? (*ppB)->m_iOrder : 0x7fffffff;
	if ( nA != nB )
		return ( nA < nB ) ? -1 : 1;
	return Q_stricmp( STRING( (*ppA)->GetEntityName() ), STRING( (*ppB)->GetEntityName() ) );
}

// Players in index order go to the sorted positions; each spot's camera ( SetViewControl ) applies
void CDirector::ForceSurvivorPositions( void )
{
	CUtlVector< CInfoSurvivorPosition * > positions;
	for ( CBaseEntity *pEnt = gEntList.FindEntityByClassname( NULL, "info_survivor_position" ); pEnt; pEnt = gEntList.FindEntityByClassname( pEnt, "info_survivor_position" ) )
	{
		CInfoSurvivorPosition *pPos = static_cast< CInfoSurvivorPosition * >( pEnt );
		pPos->m_hPlayer = NULL;

		const char *pszMode = STRING( pPos->m_iszGameMode );
		if ( pszMode && pszMode[0] && Q_stricmp( pszMode, "coop" ) )
			continue;

		positions.AddToTail( pPos );
	}
	positions.Sort( SurvivorPositionCompare );

	int nNext = 0;
	for ( int i = 1; i <= gpGlobals->maxClients; i++ )
	{
		CBasePlayer *pPlayer = UTIL_PlayerByIndex( i );
		if ( !pPlayer || !pPlayer->IsAlive() )
			continue;

		CInfoSurvivorPosition *pSpot = ( nNext < positions.Count() ) ? positions[nNext++] : NULL;
		if ( pSpot )
		{
			pPlayer->Teleport( &pSpot->GetAbsOrigin(), &pSpot->GetAbsAngles(), &vec3_origin );
			pPlayer->SnapEyeAngles( pSpot->GetAbsAngles() );
			pSpot->m_hPlayer = pPlayer;
		}

		pPlayer->AddFlag( FL_FROZEN );
		pPlayer->m_Local.m_iHideHUD |= HIDEHUD_ALL;

		if ( pSpot )
		{
			DirectorMsg( "ForceSurvivorPositions: %s -> %s\n", pPlayer->GetPlayerName(), pSpot->GetDebugName() );
			pSpot->ApplyViewControl();
		}
	}

	m_bSurvivorPositionsForced = true;
}

void CDirector::ReleaseSurvivorPositions( void )
{
	for ( int i = 1; i <= gpGlobals->maxClients; i++ )
	{
		CBasePlayer *pPlayer = UTIL_PlayerByIndex( i );
		if ( !pPlayer )
			continue;

		pPlayer->RemoveFlag( FL_FROZEN );
		pPlayer->m_Local.m_iHideHUD &= ~HIDEHUD_ALL;

		// The intro cameras draw the player's own model while they have the view; if one was
		// killed rather than disabled, make sure that stops once the view is the player's again
		if ( pPlayer->GetViewEntity() == NULL || pPlayer->GetViewEntity() == pPlayer )
			pPlayer->SetForceLocalDraw( false );
	}

	m_bSurvivorPositionsForced = false;
}

//-----------------------------------------------------------------------------
// info_director
//-----------------------------------------------------------------------------
class CInfoDirector : public CPointEntity
{
public:
	DECLARE_CLASS( CInfoDirector, CPointEntity );
	DECLARE_DATADESC();

	CInfoDirector();

	virtual void Spawn( void );
	virtual void UpdateOnRemove( void );

	void WaitForGameplayStart( void );
	void FireGameplayStart( CBasePlayer *pActivator );

	// Inputs
	void InputStartIntro( inputdata_t &inputdata );
	void InputFinishIntro( inputdata_t &inputdata );
	void InputForceSurvivorPositions( inputdata_t &inputdata );
	void InputReleaseSurvivorPositions( inputdata_t &inputdata );
	void InputIgnored( inputdata_t &inputdata );

	COutputEvent m_OnGameplayStart;

private:
	bool m_bGameplayStarted;
	float m_flPlayerInTime;		// when the first player was found in the game ( 0 = not yet )
};

LINK_ENTITY_TO_CLASS( info_director, CInfoDirector );

BEGIN_DATADESC( CInfoDirector )
	DEFINE_FIELD( m_bGameplayStarted, FIELD_BOOLEAN ),
	DEFINE_FIELD( m_flPlayerInTime, FIELD_TIME ),

	DEFINE_THINKFUNC( WaitForGameplayStart ),

	// Intro ( all games )
	DEFINE_INPUTFUNC( FIELD_VOID, "StartIntro", InputStartIntro ),
	DEFINE_INPUTFUNC( FIELD_VOID, "FinishIntro", InputFinishIntro ),
	DEFINE_INPUTFUNC( FIELD_VOID, "ForceSurvivorPositions", InputForceSurvivorPositions ),
	DEFINE_INPUTFUNC( FIELD_VOID, "ReleaseSurvivorPositions", InputReleaseSurvivorPositions ),

	// L4D / L4D2 inputs with nothing to do here ( no infected to direct ), accepted quietly
	DEFINE_INPUTFUNC( FIELD_VOID, "PanicEvent", InputIgnored ),
	DEFINE_INPUTFUNC( FIELD_STRING, "PanicEventControlled", InputIgnored ),
	DEFINE_INPUTFUNC( FIELD_STRING, "FireConceptToAny", InputIgnored ),
	DEFINE_INPUTFUNC( FIELD_VOID, "EnableTankFrustration", InputIgnored ),
	DEFINE_INPUTFUNC( FIELD_VOID, "DisableTankFrustration", InputIgnored ),
	DEFINE_INPUTFUNC( FIELD_STRING, "BeginScript", InputIgnored ),
	DEFINE_INPUTFUNC( FIELD_VOID, "EndScript", InputIgnored ),
	DEFINE_INPUTFUNC( FIELD_STRING, "ScriptedPanicEvent", InputIgnored ),
	DEFINE_INPUTFUNC( FIELD_VOID, "ForcePanicEvent", InputIgnored ),
	DEFINE_INPUTFUNC( FIELD_VOID, "EndCustomScriptedStage", InputIgnored ),
	DEFINE_INPUTFUNC( FIELD_STRING, "IncrementTeamScore", InputIgnored ),
	DEFINE_INPUTFUNC( FIELD_STRING, "CreateNewJournal", InputIgnored ),
	DEFINE_INPUTFUNC( FIELD_STRING, "WriteToJournal", InputIgnored ),
	DEFINE_INPUTFUNC( FIELD_STRING, "ExecuteJournal", InputIgnored ),
	DEFINE_INPUTFUNC( FIELD_VOID, "RefreshInitialSpawnPositions", InputIgnored ),

	DEFINE_OUTPUT( m_OnGameplayStart, "OnGameplayStart" ),
END_DATADESC()

CInfoDirector::CInfoDirector()
{
	m_bGameplayStarted = false;
	m_flPlayerInTime = 0.0f;
}

void CInfoDirector::Spawn( void )
{
	BaseClass::Spawn();

	s_Director.Reset();

	SetThink( &CInfoDirector::WaitForGameplayStart );
	SetNextThink( gpGlobals->curtime + 0.1f );
}

void CInfoDirector::UpdateOnRemove( void )
{
	// Don't leave anyone frozen or unkillable if the director goes away mid intro
	if ( s_Director.m_bSurvivorPositionsForced )
		s_Director.ReleaseSurvivorPositions();
	if ( s_Director.m_bIsPlayingIntro )
		s_Director.OnFinishIntro();

	BaseClass::UpdateOnRemove();
}

#include "hl2mp_gamerules.h"
// Polls until a player is really in: spawned, alive and sending user commands ( on a listen
// server the player spawns before the client has finished loading ).
void CInfoDirector::WaitForGameplayStart( void )
{
	//CHL2MPRules* pRules = dynamic_cast<CHL2MPRules*>(GameRules());
	//const char* szGameDesc = pRules->GetGameDescription();
	//if (szGameDesc && Q_stricmp(szGameDesc, "terror"))
	//	return;

	if ( m_bGameplayStarted )
		return;

	CBasePlayer *pReady = NULL;
	for ( int i = 1; i <= gpGlobals->maxClients && !pReady; i++ )
	{
		CBasePlayer *pPlayer = UTIL_PlayerByIndex( i );
		if ( !pPlayer || !pPlayer->IsConnected() || !pPlayer->IsAlive() )
			continue;

		// Fake clients never send commands; real ones have once they're in game
		const float flSinceCommand = pPlayer->GetTimeSinceLastUserCommand();
		const bool bSentCommands = pPlayer->IsFakeClient() || pPlayer->IsBot() || ( flSinceCommand < gpGlobals->curtime );
		if ( bSentCommands )
			pReady = pPlayer;
	}

	if ( pReady )
	{
		if ( m_flPlayerInTime == 0.0f )
			m_flPlayerInTime = gpGlobals->curtime;

		if ( gpGlobals->curtime - m_flPlayerInTime >= director_gameplay_start_delay.GetFloat() )
		{
			FireGameplayStart( pReady );
			return;
		}
	}

	SetNextThink( gpGlobals->curtime + 0.1f );
}

void CInfoDirector::FireGameplayStart( CBasePlayer *pActivator )
{
	m_bGameplayStarted = true;
	SetThink( NULL );

	DirectorMsg( "OnGameplayStart ( activator %s )\n", pActivator ? pActivator->GetPlayerName() : "none" );
	m_OnGameplayStart.FireOutput( pActivator, this );
}

void CInfoDirector::InputStartIntro( inputdata_t &inputdata )
{
	DirectorMsg( "StartIntro\n" );
	s_Director.OnStartIntro();
}

void CInfoDirector::InputFinishIntro( inputdata_t &inputdata )
{
	DirectorMsg( "FinishIntro\n" );
	s_Director.OnFinishIntro();
}

void CInfoDirector::InputForceSurvivorPositions( inputdata_t &inputdata )
{
	s_Director.ForceSurvivorPositions();
}

void CInfoDirector::InputReleaseSurvivorPositions( inputdata_t &inputdata )
{
	DirectorMsg( "ReleaseSurvivorPositions\n" );
	s_Director.ReleaseSurvivorPositions();
}

void CInfoDirector::InputIgnored( inputdata_t &inputdata )
{
	DirectorMsg( "ignored input ( not supported here )\n" );
}

//-----------------------------------------------------------------------------
// director_gameplay_start: fire OnGameplayStart again ( to test a map's intro )
//-----------------------------------------------------------------------------
CON_COMMAND_F( director_gameplay_start, "Fire info_director's OnGameplayStart again.", FCVAR_CHEAT )
{
	CBasePlayer *pPlayer = UTIL_GetCommandClient();
	if ( !pPlayer )
		pPlayer = UTIL_PlayerByIndex( 1 );

	CBaseEntity *pEnt = gEntList.FindEntityByClassname( NULL, "info_director" );
	if ( !pEnt )
	{
		Msg( "No info_director in this map.\n" );
		return;
	}

	static_cast< CInfoDirector * >( pEnt )->FireGameplayStart( pPlayer );
}
