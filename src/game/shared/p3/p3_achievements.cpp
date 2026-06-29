//====== Copyright � 1996-2008, Valve Corporation, All rights reserved. =======
//
// Purpose: 
//
//=============================================================================

#include "cbase.h"
#include "achievementmgr.h"
#include "baseachievement.h"

#ifdef CLIENT_DLL
#include "p3/p3_c_player.h"
#include "p3/p3_c_gamestats.h"
#else
#include "p3/p3_player.h"
#include "p3/weapons/p3_base_weapon.h"
#endif

//#include "p3/weapons/terrormeleeweapon.h"
#include "p3/p3_ai_shared.h"
#include "p3/p3_gamerules.h"
#include "p3/weapons/p3_weapon_shared.h"

#include "filesystem.h"

#if defined( NO_STEAM ) && defined( CLIENT_DLL )
#include "cegclient.h"
#endif



// NOTE: This has to be the last file included!
#include "tier0/memdbgon.h"


#ifdef CLIENT_DLL
CAchievementMgr g_AchievementMgrP3;	// global achievement mgr for Left4Dead

//-----------------------------------------------------------------------------
// Purpose: Indicates the game mode the achievement is for.
//-----------------------------------------------------------------------------
enum eAchGameMode
{
	ACH_GAME_MODE_SINGLE,
};

//-----------------------------------------------------------------------------
// Purpose: Indicates the game mode the achievement is for.
//-----------------------------------------------------------------------------
enum eAchDLCRequired
{
	ACH_DLC_REQUIRED_BASE = 0, // Shipped with the base game
	//ACH_DLC_REQUIRED_DLC1 = 1,
	//ACH_DLC_REQUIRED_DLC2 = 2,
	//....
};

//-----------------------------------------------------------------------------
// Purpose: Enum of Gamerscore totals to make the achievement declarations more readable
//-----------------------------------------------------------------------------
enum eGamerScore
{
	GAMERSCORE_0  = 0,
	GAMERSCORE_10 = 10,
	GAMERSCORE_15 = 15,
	GAMERSCORE_20 = 20,
	GAMERSCORE_25 = 25,
	GAMERSCORE_30 = 30,
	GAMERSCORE_35 = 35,
	GAMERSCORE_50 = 50
};

//-----------------------------------------------------------------------------
// Purpose: Base class that handles messaging and logic related to detecting
//			the start and end of levels and campagins. Subclasses 
//			Subclasses need only
//			listen to game events salient to the achievement they are interested
//			and overrride OnEvent(), OnLocalPlayerEvent(), OnLevelBegin(), 
//			OnLevelEnd(), OnCampaignBegin(), and OnCampaignEnd().
//-----------------------------------------------------------------------------
class CPostal3Achievement : public CBaseAchievement
{
	DECLARE_CLASS( CPostal3Achievement, CBaseAchievement );

public:

	CPostal3Achievement() : m_pszEventNames( NULL ), m_iNumEvents( 0 ), m_iTeam( -1 ), m_iGameMode( -1 ), m_bFailed( false ), m_eDLCRequired( ACH_DLC_REQUIRED_BASE ) {}

	//-----------------------------------------------------------------------------
	// Purpose: Initializes the base state variables.
	//-----------------------------------------------------------------------------
	virtual void Init()
	{
	}

	//-----------------------------------------------------------------------------
	// Listens for events specified in Init as well as the basics for all achievements.
	//-----------------------------------------------------------------------------
	virtual void ListenForEvents()
	{
		if ( m_pszEventNames )
		{
			for ( int i = 0; i < m_iNumEvents; i++ )
			{
				ListenForGameEvent( m_pszEventNames[i] );
			}
		}
		else
		{
			DevWarning( "Achievement with no custom events defined: %s", GetName() );
		}

		// Also listen for events that signal duration
		ListenForGameEvent( "end_mission" );
		ListenForGameEvent( "finale_win" );
		//ListenForGameEvent( "vote_passed" );

		// ListenForEvents() is called each time the level is initialized
		OnLevelBegin();
		CheckCampaignBegin();
	}

	virtual bool IsAvailable()
	{
#ifndef _X360
		return true;
#else
		// Check to see if we require a DLC and we're not available if its not present
		if ( m_eDLCRequired == ACH_DLC_REQUIRED_BASE )
		{
			return true;
		}

		return filesystem->IsSpecificDLCPresent( (unsigned int)m_eDLCRequired );
#endif
	}

	// We're only active if we're available
	virtual bool IsActive()
	{
		if ( !IsAvailable() )
		{
			return false;
		}

		return BaseClass::IsActive();
	}


	int GetGameMode() const { return m_iGameMode; }
	void SetGameMode( int iGameMode ) { m_iGameMode = iGameMode; }

	eAchDLCRequired GetDLCRequired() const { return m_eDLCRequired; }
	void SetDLCRequired( eAchDLCRequired eDLCRequired ) { m_eDLCRequired = eDLCRequired; }

	//-----------------------------------------------------------------------------
	// Purpose: Used by sub-classes to get the team associated with an
	//          achievement.
	//-----------------------------------------------------------------------------
	int GetTeam() const { return m_iTeam; }

	//-----------------------------------------------------------------------------
	// Purpose: Used by sub-classes to specify the team associated with an
	//          achievement.
	//-----------------------------------------------------------------------------
	void SetTeam( int iTeam ) { m_iTeam = iTeam; }

#if 0

	//-----------------------------------------------------------------------------
	// Purpose: Clears achievement data
	//-----------------------------------------------------------------------------
	void ClearAchievementData()
	{
		BaseClass::ClearAchievementData();
		SetFailed( false );
	}

#endif

	//-----------------------------------------------------------------------------
	// Purpose: Writes achievement-specific data for debugging
	//-----------------------------------------------------------------------------
	void PrintAdditionalStatus()
	{
		if ( IsFailed() )
		{
			Msg( "%-20s", "FAILED" );
		}
	}		

	//-----------------------------------------------------------------------------
	// Purpose: Writes achievement-specific data for debugging
	//-----------------------------------------------------------------------------
	const char *GetIconPath()
	{ 
		static char szIconPath[MAX_PATH];

		if ( IsAchieved() )
		{
			V_strncpy( szIconPath, "achievements/", sizeof( szIconPath ) );
			V_strncat( szIconPath, GetName(), sizeof( szIconPath ) );
		}
		else
		{
			switch ( GetGameMode() )
			{
			case ACH_GAME_MODE_SINGLE:
				V_strncpy( szIconPath, "achievements/", sizeof( szIconPath ) );
				V_strncat( szIconPath, GetName(), sizeof( szIconPath ) );
				V_strncat( szIconPath, "_LOCK", sizeof( szIconPath ) );
				break;
			}
		}

		return szIconPath;
	}

protected:

	//-----------------------------------------------------------------------------
	// Purpose: Overrrides CBaseAchievement::IncrementCount() to check ensure 
	//          player is playing on the correct team and is not a bot.
	//-----------------------------------------------------------------------------
	virtual void IncrementCount( void )
	{
#if !defined(NO_STEAM) && defined(USE_CEG)
		Steamworks_TestSecret() ; 
#endif
		C_P3_Player *pLocalPlayer = C_P3_Player::GetLocalP3Player();

		if ( MustBeAliveToEarn() && ( pLocalPlayer->IsObserver() || !pLocalPlayer->IsAlive() ) )
			return;
		
//		if ( pLocalPlayer && ( pLocalPlayer->GetTeamNumber() == GetTeam() || GetTeam() == TEAM_UNASSIGNED ) && !pLocalPlayer->IsBot() )
//		{
			BaseClass::IncrementCount();
//		}
	}

	virtual bool MustBeAliveToEarn( void )
	{
		return false;
	}

	//-----------------------------------------------------------------------------
	// Purpose: Receives raw game events from the CBaseAchievement. Handles general
	//			cases and calls appropriate overridable function.
	//-----------------------------------------------------------------------------
	virtual void FireGameEvent_Internal( IGameEvent *event )
	{
		const char *name = event->GetName();

		// Look for the events we've asked for
		if ( m_pszEventNames )
		{
			for ( int i = 0; i < m_iNumEvents; i++ )
			{
				if ( !Q_stricmp( name, m_pszEventNames[i] ) )
				{
					if ( GetFlags() & ACH_FILTER_LOCAL_PLAYER_EVENTS )
					{
						C_BasePlayer *localPlayer = C_BasePlayer::GetLocalPlayer();
						if ( localPlayer && ( event->GetInt( "userid" ) == localPlayer->GetUserID() ) )
						{
							OnLocalPlayerEvent( localPlayer, name, event );
						}
					}

					// Call the general event handler
					OnEvent( name, event );
				}
			}
		}

		// Detect and dispatch begin/end events
		if ( 0 == Q_strcmp( name, "end_mission" ) ) 
		{
			OnLevelEnd();
//			m_CompletedMaps.AddToTail( CUtlString( engine->GetLevelNameShort() ) );
		}
		else if ( 0 == Q_strcmp( name, "finale_win" ) )
		{
			OnLevelEnd();

			// Don't call the OnCampaignEnd() function if the campaign wasn't played in its entireity
//			if ( IsCampaignIntact() )
			{
				OnCampaignEnd( event->GetString( "map_name" ), event->GetInt( "difficulty" ) );
			}

			// Finished the campaign so we can get rid of the maps
			m_CompletedMaps.RemoveAll();
		}

#if 0

		else if ( 0 == Q_strcmp( name, "vote_passed" ) ) 
		{
			// This is to handle the case where we vote to restart a campaign which bypasses the level init code we
			// rely on for checking for the begining of a campaign when the campaign is restarted while on the first
			// map.

			// Need to get the details of the vote 
			const char *pszVoteDetails = event->GetString( "details", "" );

			// See what we just voted on
			if ( !Q_stricmp( "#L4D_vote_passed_restart_game", pszVoteDetails ) || !Q_stricmp( "#L4D_vote_passed_return_to_lobby", pszVoteDetails ) )
			{
				OnCampaignBegin();

				// Clear the list of achievements earned during this game
				g_AchievementMgrP3.ResetAchievedDuringCurrentGame( m_nUserSlot );
			}
		}

#endif

	}

	//-----------------------------------------------------------------------------
	// Purpose: Checks for whether we are at the begining of a campaign and sets up
	//			achievement appropriately.
	//-----------------------------------------------------------------------------
	SF virtual void CheckCampaignBegin()
	{
		const char *level_name = engine->GetLevelName();

		// Make sure that this message comes at the beginning of a campaign by checking the map name
		if ( Q_strstr( level_name, "pdb.bsp" ) || Q_strstr( level_name, "pw.bsp" ) )	// first chapter
		{
			OnCampaignBegin();

			// Clear the list of achievements earned during this game
//			g_AchievementMgrL4D.ResetAchievedDuringCurrentGame( m_nUserSlot );
		}
	}

	//-----------------------------------------------------------------------------
	// Purpose: Default implementation of function called when an event of occurs.
	//			that is relevant to the local player. This function only gets 
	//			called when the achievement is created with the 
	//			ACH_FILTER_LOCAL_PLAYER_EVENTS flag.
	//
	//			This implementation simplu increments the goal count to handle 
	//			the most common achievement pattern: "Player receives event x times" 
	//-----------------------------------------------------------------------------
	virtual void OnLocalPlayerEvent( C_BasePlayer *localPlayer, const char *eventName, IGameEvent *eventObj )
	{
		IncrementCount();
	}

	//-----------------------------------------------------------------------------
	// Purpose: Stub implementation of function called when a subscribed event
	//			is received. 
	//-----------------------------------------------------------------------------
	virtual void OnEvent( const char *eventName, IGameEvent *eventObj ) {}

	//-----------------------------------------------------------------------------
	// Purpose: Stub implementation of function called when a campaign begins. 
	//          Subclasses should call this implementation in their override to
	//			ensure that their OnLevelBegin() is called at the begining of a
	//			campaign.
	//-----------------------------------------------------------------------------
	virtual void OnCampaignBegin() { SetFailed( false ); m_CompletedMaps.RemoveAll(); }

	//-----------------------------------------------------------------------------
	// Purpose: Stub implementation of function called when a level begins.
	//-----------------------------------------------------------------------------
	virtual void OnLevelBegin() {}

	//-----------------------------------------------------------------------------
	// Purpose: Stub implementation of function called when a level ends.
	//-----------------------------------------------------------------------------
	virtual void OnLevelEnd() {}

	//-----------------------------------------------------------------------------
	// Purpose: Stub implementation of function called when a campaign ends.
	//-----------------------------------------------------------------------------
	virtual void OnCampaignEnd( const char *pcMapName, int nDifficulty ) {}

	//-----------------------------------------------------------------------------
	// Purpose: Stub implementation of function called when the game difficulty is
	//			changed.
	//-----------------------------------------------------------------------------
	virtual void OnDifficultyChanged( int iNewDifficulty, int oldDifficulty  ) {}

	//-----------------------------------------------------------------------------
	// Purpose: Used by sub-classes to determine if the achievement criteria
	//			have not been met.
	//-----------------------------------------------------------------------------
	bool IsFailed() const { return m_bFailed; }

	//-----------------------------------------------------------------------------
	// Purpose: Used by sub-classes to determine if the achievement criteria
	//			have not been met.+
	//-----------------------------------------------------------------------------
	void SetFailed( bool bFailed ) { m_bFailed = bFailed; }

	//-----------------------------------------------------------------------------
	// Purpose: TRUE if the player has begun play on the first map of a campaign.
	//-----------------------------------------------------------------------------
	bool IsCampaignIntact() const
	{
		return false; // TODO

		/*
		KeyValues *pGameSettings = g_pMatchFramework->GetMatchNetworkMsgController()->GetActiveServerGameDetails( NULL );
		if ( !pGameSettings )
			return false;

		KeyValues::AutoDelete autodelete_pGameSettings( pGameSettings );

		KeyValues *pMissionInfo = NULL;
		KeyValues *pChapterInfo = g_pMatchExtL4D->GetMapInfo( pGameSettings, &pMissionInfo );

		if ( !pMissionInfo || !pChapterInfo )
			return false;

		// Is the current map from one of the Valve official campaigns?
		if ( !pMissionInfo->GetBool( "BuiltIn" ) ) // check "dlcmask" flag if need to distinguish
			return false;

		// Check that there's no next map in the campaign
		int nChapter = pGameSettings->GetInt( "Game/chapter" );
		pGameSettings->SetInt( "Game/chapter", 1 + nChapter );
		KeyValues *pNextMapInfo = g_pMatchExtL4D->GetMapInfo( pGameSettings );
		if ( pNextMapInfo )
			return false;	// there's a next map in this campaign

		// Check if previous maps were completed
		if ( m_CompletedMaps.Count() < nChapter - 1 )
			return false;

		// We use this to make sure that the player has completed the maps sequentially.
		// Are the last 4 completed maps from the same campaign as the current one?
		for ( int i = m_CompletedMaps.Count() - 1, nChapterRequired = nChapter - 1;
			i >= m_CompletedMaps.Count() - nChapter + 1;
			-- i, -- nChapterRequired )
		{
			const char *pcMapName = m_CompletedMaps[i].Get();

			pGameSettings->SetInt( "Game/chapter", nChapterRequired );
			KeyValues *pRequiredChapterInfo = g_pMatchExtL4D->GetMapInfo( pGameSettings );
			if ( !pRequiredChapterInfo )
				return false;	// previous chapter doesn't exist

			if ( Q_stricmp( pRequiredChapterInfo->GetString( "map" ), pcMapName ) )
				return false;	// played a different map
		}

		// Great success!
		return true;
		*/
	}

#if 0

	//-----------------------------------------------------------------------------
	// Purpose: Store the achievement's progress.
	//-----------------------------------------------------------------------------
	bool WriteProgress( IPlayerLocal* pPlayer )
	{
		return true;
	}

	//-----------------------------------------------------------------------------
	// Purpose: Restore the achievement's progress from steam.
	//-----------------------------------------------------------------------------
	void ReadProgress( IPlayerLocal *pPlayer )
	{
		EvaluateIsAlreadyAchieved();
	}

#endif

	// Achievements must initialize m_pszEventNames in Init()!
	const char **m_pszEventNames;		// Events to listen for in ListenForEvents
	int m_iNumEvents;					// # of events to listen for
	int m_iTeam;						// Team to which this achievement is relevant (TEAM_SURVIVOR or TEAM_ZOMBIE)
	int m_iGameMode;
	eAchDLCRequired m_eDLCRequired;

	// Note: In Left4Dead2 achievements need to use TEAM_UNASSIGNED if you want them to work in Scavenger, because teams swap roles but not ids during a round.

private:

	bool m_bFailed;
	CUtlVector< CUtlString > m_CompletedMaps;
};


#define DECLARE_P3_ACHIEVEMENT_( className, achievementID, achievementName, iGoalCount, iPointValue, iDisplayOrder, assetAwardName, flags, iTeam, iGameMode, dlcRequired, bHidden ) \
	static CBaseAchievement *Create_##className( void )						\
{																			\
	CPostal3Achievement *pAchievement = new className();					\
	pAchievement->SetAchievementID( achievementID );						\
	/*pAchievement->SetDisplayOrder( iDisplayOrder );*/						\
	pAchievement->SetName( achievementName );								\
	pAchievement->SetPointValue( iPointValue );								\
	pAchievement->SetHideUntilAchieved( bHidden );							\
	pAchievement->SetGoal( iGoalCount );									\
	pAchievement->SetFlags( flags );										\
	pAchievement->SetGameDirFilter( "p3" );									\
	pAchievement->SetTeam( iTeam );											\
	pAchievement->SetGameMode( iGameMode );									\
	/*pAchievement->SetAssetAward( assetAwardName );*/						\
	pAchievement->SetDLCRequired( dlcRequired );							\
	return pAchievement;													\
};																			\
	static CBaseAchievementHelper g_##className##_Helper( Create_##className );

#define DECLARE_P3_ACHIEVEMENT( className, achievementID, achievementName, iGoalCount, iPointValue, assetAwardName, flags, iTeam, iGameMode, dlcRequired )	\
	DECLARE_P3_ACHIEVEMENT_( className, achievementID, achievementName, iGoalCount, iPointValue, achievementID, assetAwardName, flags, iTeam, iGameMode, dlcRequired, false )

//-----------------------------------------------------------------------------
// Complete a campaign using only melee weapons.
//-----------------------------------------------------------------------------
class CAchievementStallone : public CPostal3Achievement
{
	DECLARE_CLASS( CAchievementStallone, CPostal3Achievement );

protected:

	virtual void Init() 
	{
		BaseClass::Init();
		static const char *s_szEvents[] = { "non_melee_fired" };
		m_pszEventNames = s_szEvents;
		m_iNumEvents = ARRAYSIZE( s_szEvents );
		if ( !IsAchieved() )
			SetFailed( true );
	}

	virtual void OnLocalPlayerEvent( C_BasePlayer *localPlayer, const char* eventName, IGameEvent *eventObj )
	{
		SetFailed( true );
	}

	virtual void OnCampaignEnd( const char *pcMapName, int nDifficulty )
	{
		if ( !IsFailed() )
		{
			IncrementCount();
		}
	}

	virtual bool MustBeAliveToEarn( void )
	{
		return true;
	}
};
DECLARE_P3_ACHIEVEMENT( CAchievementStallone, ACHIEVEMENT_P3_STALLONE, "ACH_STALLONE"
                      , 1, GAMERSCORE_30, AWARD_ID_NONE, ACH_SAVE_WITH_GAME | ACH_FILTER_LOCAL_PLAYER_EVENTS, TEAM_ANY, ACH_GAME_MODE_SINGLE, ACH_DLC_REQUIRED_BASE );

//-----------------------------------------------------------------------------
// Complete a campaign using only pistol.
//-----------------------------------------------------------------------------
class CAchievementEastwood : public CPostal3Achievement
{
	DECLARE_CLASS( CAchievementEastwood, CPostal3Achievement );

protected:

	virtual void Init() 
	{
		BaseClass::Init();
		static const char *s_szEvents[] = { "non_pistol_fired" };
		m_pszEventNames = s_szEvents;
		m_iNumEvents = ARRAYSIZE( s_szEvents );
		if ( !IsAchieved() )
			SetFailed( true );

	}

	virtual void OnLocalPlayerEvent( C_BasePlayer *localPlayer, const char* eventName, IGameEvent *eventObj )
	{
		SetFailed( true );
	}

	virtual void OnCampaignEnd( const char *pcMapName, int nDifficulty )
	{
		if ( !IsFailed() )
		{
			IncrementCount();
		}
	}

	virtual bool MustBeAliveToEarn( void )
	{
		return true;
	}
};
DECLARE_P3_ACHIEVEMENT( CAchievementEastwood, ACHIEVEMENT_P3_EASTWOOD, "ACH_EASTWOOD"
                      , 1, GAMERSCORE_30, AWARD_ID_NONE, ACH_SAVE_WITH_GAME | ACH_FILTER_LOCAL_PLAYER_EVENTS, TEAM_ANY, ACH_GAME_MODE_SINGLE, ACH_DLC_REQUIRED_BASE );

//-----------------------------------------------------------------------------
// Complete a campaign with zero kills.
//-----------------------------------------------------------------------------
class CAchievementPersonalJesus : public CPostal3Achievement
{
	DECLARE_CLASS( CAchievementPersonalJesus, CPostal3Achievement );

protected:

	virtual void Init() 
	{
		BaseClass::Init();
		static const char *s_szEvents[] = { "npc_death" };
		m_pszEventNames = s_szEvents;
		m_iNumEvents = ARRAYSIZE( s_szEvents );
		if ( !IsAchieved() )
			SetFailed( true );
	}

	virtual void OnLocalPlayerEvent( C_BasePlayer *localPlayer, const char* eventName, IGameEvent *eventObj )
	{
		if ( eventObj->GetInt( "faction_id" ) != F_ZombieBoss ) {
			SetFailed( true );
		}
	}

	virtual void OnCampaignEnd( const char *pcMapName, int nDifficulty )
	{
		if ( !IsFailed() )
		{
			IncrementCount();
		}
	}

	virtual bool MustBeAliveToEarn( void )
	{
		return true;
	}
};
DECLARE_P3_ACHIEVEMENT( CAchievementPersonalJesus, ACHIEVEMENT_P3_PERSONAL_JESUS, "ACH_PERSONAL_JESUS"
                      , 1, GAMERSCORE_30, AWARD_ID_NONE, ACH_SAVE_WITH_GAME | ACH_FILTER_LOCAL_PLAYER_EVENTS, TEAM_ANY, ACH_GAME_MODE_SINGLE, ACH_DLC_REQUIRED_BASE );


//-----------------------------------------------------------------------------
// Complete a campaign with zero kills.
//-----------------------------------------------------------------------------
class CAchievementSpecialOlympian : public CPostal3Achievement
{
	DECLARE_CLASS( CAchievementSpecialOlympian, CPostal3Achievement );

protected:
	virtual void OnCampaignEnd( const char *pcMapName, int nDifficulty )
	{
		IncrementCount();
	}

	virtual bool MustBeAliveToEarn( void )
	{
		return true;
	}
};
DECLARE_P3_ACHIEVEMENT( CAchievementSpecialOlympian, ACHIEVEMENT_P3_SPECIAL_OLYMPIAN, "ACH_SPECIAL_OLYMPIAN"
                      , 1, GAMERSCORE_30, AWARD_ID_NONE, ACH_SAVE_WITH_GAME | ACH_FILTER_LOCAL_PLAYER_EVENTS, TEAM_ANY, ACH_GAME_MODE_SINGLE, ACH_DLC_REQUIRED_BASE );

//-----------------------------------------------------------------------------
// Complete a campaign without killing animals.
//-----------------------------------------------------------------------------
class CAchievementPETAChairman : public CPostal3Achievement
{
	DECLARE_CLASS( CAchievementPETAChairman, CPostal3Achievement );

protected:

	virtual void Init() 
	{
		BaseClass::Init();
		static const char *s_szEvents[] = { "npc_death" };
		m_pszEventNames = s_szEvents;
		m_iNumEvents = ARRAYSIZE( s_szEvents );
		if ( !IsAchieved() )
			SetFailed( true );
	}

	virtual void OnLocalPlayerEvent( C_BasePlayer *localPlayer, const char* eventName, IGameEvent *eventObj )
	{
		if ( eventObj->GetInt( "faction_id" ) == F_Animals )
		{
			SetFailed( true );
		}
	}

	virtual void OnCampaignEnd( const char *pcMapName, int nDifficulty )
	{
		if ( !IsFailed() )
		{
			IncrementCount();
		}
	}

	virtual bool MustBeAliveToEarn( void )
	{
		return true;
	}
};
DECLARE_P3_ACHIEVEMENT( CAchievementPETAChairman , ACHIEVEMENT_P3_PETA_CHAIRMAN, "ACH_PETA_CHAIRMAN"
                      , 1, GAMERSCORE_30, AWARD_ID_NONE, ACH_SAVE_WITH_GAME | ACH_FILTER_LOCAL_PLAYER_EVENTS, TEAM_ANY, ACH_GAME_MODE_SINGLE, ACH_DLC_REQUIRED_BASE );

//-----------------------------------------------------------------------------
// Complete a campaign with zero kills.
//-----------------------------------------------------------------------------
class CAchievementDaddyNeverLovedMe : public CPostal3Achievement
{
	DECLARE_CLASS( CAchievementDaddyNeverLovedMe, CPostal3Achievement );

protected:

	virtual void Init() 
	{
		BaseClass::Init();
		static const char *s_szEvents[] = { "npc_death" };
		m_pszEventNames = s_szEvents;
		m_iNumEvents = ARRAYSIZE( s_szEvents );
		if ( !IsAchieved() )
			SetFailed( true );
	}

	virtual void OnLocalPlayerEvent( C_BasePlayer *localPlayer, const char* eventName, IGameEvent *eventObj )
	{
		int mannerID = eventObj->GetInt( "faction_id" );
		if ( mannerID == M_StGranny || mannerID == M_SoccerMom || mannerID == M_CuteGirl || mannerID == M_Girl )
		{
			SetFailed( true );
		}
	}

	virtual void OnCampaignEnd( const char *pcMapName, int nDifficulty )
	{
		if ( !IsFailed() )
		{
			IncrementCount();
		}
	}

	virtual bool MustBeAliveToEarn( void )
	{
		return true;
	}
};
DECLARE_P3_ACHIEVEMENT( CAchievementDaddyNeverLovedMe, ACHIEVEMENT_P3_DADDY_NEVER_LOVED_ME, "ACH_DADDY_NEVER_LOVED_ME"
                      , 1, GAMERSCORE_30, AWARD_ID_NONE, ACH_SAVE_WITH_GAME | ACH_FILTER_LOCAL_PLAYER_EVENTS, TEAM_ANY, ACH_GAME_MODE_SINGLE, ACH_DLC_REQUIRED_BASE );


#if 0
//-----------------------------------------------------------------------------
// Complete a campaign without killing animals.
//-----------------------------------------------------------------------------
class CAchievementMisogynist : public CPostal3Achievement
{
	DECLARE_CLASS( CAchievementMisogynist, CPostal3Achievement );

protected:

	virtual void Init() 
	{
		BaseClass::Init();
		static const char *s_szEvents[] = { "npc_death" };
		m_pszEventNames = s_szEvents;
		m_iNumEvents = ARRAYSIZE( s_szEvents );
	}

	virtual void OnLocalPlayerEvent( C_BasePlayer *localPlayer, const char* eventName, IGameEvent *eventObj )
	{
		int mannerID = eventObj->GetInt( "faction_id" );
		if ( mannerID == M_StreetBro || mannerID == M_JusticeMan || mannerID == M_LameWanker || mannerID == M_Vendor
		  || mannerID == M_RedNeck || mannerID == M_SushiNinja || mannerID == M_MedicDoc || mannerID == M_JihadBeard
		  || mannerID == M_GayGuy || mannerID == M_NerdyNerd )
		{
			SetFailed( true );
		}
	}

	virtual void OnCampaignEnd( const char *pcMapName, int nDifficulty )
	{
		if ( !IsFailed() )
		{
			IncrementCount();
		}
	}

	virtual bool MustBeAliveToEarn( void )
	{
		return true;
	}
};
DECLARE_P3_ACHIEVEMENT( CAchievementMisogynist, ACHIEVEMENT_P3_MISOGYNIST, "ACH_PETA_MISOGYNIST"
                      , 1, GAMERSCORE_30, AWARD_ID_NONE, ACH_SAVE_WITH_GAME | ACH_FILTER_LOCAL_PLAYER_EVENTS, TEAM_ANY, ACH_GAME_MODE_SINGLE, ACH_DLC_REQUIRED_BASE );
#endif


//-----------------------------------------------------------------------------
// Complete a campaign without killing animals.
//-----------------------------------------------------------------------------
class CAchievementIAmTheLaw : public CPostal3Achievement
{
	DECLARE_CLASS( CAchievementIAmTheLaw, CPostal3Achievement );

protected:

	virtual void Init() 
	{
		BaseClass::Init();
		static const char *s_szEvents[] = { "join_the_dark_forces" };
		m_pszEventNames = s_szEvents;
		m_iNumEvents = ARRAYSIZE( s_szEvents );
		if ( !IsAchieved() )
			SetFailed( true );
	}

	virtual void OnLocalPlayerEvent( C_BasePlayer *localPlayer, const char* eventName, IGameEvent *eventObj )
	{
		SetFailed( true );
	}

	virtual void OnCampaignEnd( const char *pcMapName, int nDifficulty )
	{
		if ( !IsFailed() )
		{
			IncrementCount();
		}
	}

	virtual bool MustBeAliveToEarn( void )
	{
		return true;
	}
};
DECLARE_P3_ACHIEVEMENT( CAchievementIAmTheLaw, ACHIEVEMENT_P3_I_AM_THE_LAW, "ACH_I_AM_THE_LAW"
                      , 1, GAMERSCORE_30, AWARD_ID_NONE, ACH_SAVE_WITH_GAME | ACH_FILTER_LOCAL_PLAYER_EVENTS, TEAM_ANY, ACH_GAME_MODE_SINGLE, ACH_DLC_REQUIRED_BASE );

//-----------------------------------------------------------------------------
// Purpose: Suzcin 500 items
//-----------------------------------------------------------------------------
class CAchievementSucktastic : public CPostal3Achievement
{
	DECLARE_CLASS( CAchievementSucktastic, CPostal3Achievement );
public:
	virtual void Init() 
	{
		BaseClass::Init();
		static const char *s_szEvents[] = { "shopvac_suckin" };
		m_pszEventNames = s_szEvents;
		m_iNumEvents = ARRAYSIZE( s_szEvents );
	}

	virtual void OnEvent( const char* eventName, IGameEvent *eventObj )
	{
		IncrementCount();
	}

#if 0

	virtual void ReadProgress( IPlayerLocal *pPlayer )
	{
		TitleData2 const *pTitleData = ( TitleData2 const * ) pPlayer->GetPlayerTitleData( 1 );
		SetCount( pTitleData->iCountCutOffHeadsMelee );
		BaseClass::ReadProgress( pPlayer );
	}

	virtual bool WriteProgress( IPlayerLocal* pPlayer )
	{
		TitleData2 tdNew;
		tdNew.iCountCutOffHeadsMelee = GetCount();
		pPlayer->UpdatePlayerTitleData( 1, &tdNew.iCountCutOffHeadsMelee, offsetof( TitleData2, iCountCutOffHeadsMelee ), sizeof( tdNew.iCountCutOffHeadsMelee ) );
		return BaseClass::WriteProgress( pPlayer );
	}

#endif
};
DECLARE_P3_ACHIEVEMENT( CAchievementSucktastic, ACHIEVEMENT_P3_SUCKTASTIC, "ACH_SUCKTASTIC"
                      , 500, GAMERSCORE_15, AWARD_ID_NONE, ACH_SAVE_GLOBAL, TEAM_ANY, ACH_GAME_MODE_SINGLE, ACH_DLC_REQUIRED_BASE );

//-----------------------------------------------------------------------------
// Use every melee weapon to kill common infected.
//-----------------------------------------------------------------------------
class CAchievementKillWithWeaponBase
	: public CPostal3Achievement
{
	DECLARE_CLASS( CAchievementKillWithWeaponBase, CPostal3Achievement );

private:
	int m_iWeaponID;
	int m_iWeaponFlags;

public:
	void SetWeaponID( int id )
	{
		m_iWeaponID = id;
	}

	void SetWeaponFlags( int flags )
	{
		m_iWeaponFlags = flags;
	}

	//-----------------------------------------------------------------------------
	// Purpose: Class initialization
	//-----------------------------------------------------------------------------
	virtual void Init() 
	{
		BaseClass::Init();
		static const char *s_szEvents[] = { "npc_death" };
		m_pszEventNames = s_szEvents;
		m_iNumEvents = ARRAYSIZE( s_szEvents );
	}

	//-----------------------------------------------------------------------------
	// Purpose: Processes a game event to which we subscribed
	// Input  : eventName - Name of game event
	//			eventObj - Game event object.
	//-----------------------------------------------------------------------------
	virtual void OnEvent( const char* eventName, IGameEvent *eventObj )
	{
		const int nKiller = eventObj->GetInt( "attacker_id" );
		const C_BasePlayer *pLocalPlayer = C_BasePlayer::GetLocalPlayer();

		if ( pLocalPlayer && ( pLocalPlayer->entindex() == nKiller ) )
		{
			int iWeaponID = eventObj->GetInt( "weapon_id" );
			if ( iWeaponID != m_iWeaponID )
				return;

			int iWeaponFlags = eventObj->GetInt( "weapon_flags" );
			if ( iWeaponFlags != m_iWeaponFlags )
				return;

			IncrementCount();
		}
	}

#if 0

	virtual void ReadProgress( IPlayerLocal *pPlayer )
	{
		TitleData2 const *pTitleData = ( TitleData2 const * ) pPlayer->GetPlayerTitleData( 1 );
		SetCount( pTitleData->iCountKillInfectedWithChainsaw );
		BaseClass::ReadProgress( pPlayer );
	}

	virtual bool WriteProgress( IPlayerLocal* pPlayer )
	{
		TitleData2 tdNew;
		tdNew.iCountKillInfectedWithChainsaw = GetCount();
		pPlayer->UpdatePlayerTitleData( 1, &tdNew.iCountKillInfectedWithChainsaw, offsetof( TitleData2, iCountKillInfectedWithChainsaw ), sizeof( tdNew.iCountKillInfectedWithChainsaw ) );
		return BaseClass::WriteProgress( pPlayer );
	}

#endif
};

#define DECLARE_P3_WEAPON_ACHIEVEMENT_( className, achievementID, achievementName, iWeaponID, iWeaponFlags, iGoalCount, iPointValue, iDisplayOrder, assetAwardName, flags, iTeam, iGameMode, dlcRequired, bHidden ) \
	static CBaseAchievement *Create_##className##_##achievementID( void )	\
{																			\
	CAchievementKillWithWeaponBase *pAchievement = new className();			\
	pAchievement->SetAchievementID( achievementID );						\
	/*pAchievement->SetDisplayOrder( iDisplayOrder );*/						\
	pAchievement->SetName( achievementName );								\
	pAchievement->SetPointValue( iPointValue );								\
	pAchievement->SetHideUntilAchieved( bHidden );							\
	pAchievement->SetWeaponID( iWeaponID );									\
	pAchievement->SetWeaponFlags( iWeaponFlags );							\
	pAchievement->SetGoal( iGoalCount );									\
	pAchievement->SetFlags( flags );										\
	pAchievement->SetGameDirFilter( "p3" );									\
	pAchievement->SetTeam( iTeam );											\
	pAchievement->SetGameMode( iGameMode );									\
	/*pAchievement->SetAssetAward( assetAwardName );*/						\
	pAchievement->SetDLCRequired( dlcRequired );							\
	return pAchievement;													\
};																			\
	static CBaseAchievementHelper g_##className##_##achievementID##_Helper( Create_##className##_##achievementID );

#define DECLARE_P3_WEAPON_ACHIEVEMENT( className, achievementID, achievementName, iWeaponID, iWeaponFlags, iGoalCount, iPointValue, assetAwardName, flags, iTeam, iGameMode, dlcRequired )	\
	DECLARE_P3_WEAPON_ACHIEVEMENT_( className, achievementID, achievementName, iWeaponID, iWeaponFlags, iGoalCount, iPointValue, achievementID, assetAwardName, flags, iTeam, iGameMode, dlcRequired, false )


DECLARE_P3_WEAPON_ACHIEVEMENT( CAchievementKillWithWeaponBase, ACHIEVEMENT_P3_CHAMP_WISPERER, "ACH_CHAMP_WISPERER"
                             , P3_WEAPON_DOG, 0, 50, GAMERSCORE_15, AWARD_ID_NONE, ACH_SAVE_GLOBAL, TEAM_ANY, ACH_GAME_MODE_SINGLE, ACH_DLC_REQUIRED_BASE );

DECLARE_P3_WEAPON_ACHIEVEMENT( CAchievementKillWithWeaponBase, ACHIEVEMENT_P3_CAT_WRANGLER, "ACH_CAT_WRANGLER"
                             , P3_WEAPON_CAT, 0, 50, GAMERSCORE_15, AWARD_ID_NONE, ACH_SAVE_GLOBAL, TEAM_ANY, ACH_GAME_MODE_SINGLE, ACH_DLC_REQUIRED_BASE );

DECLARE_P3_WEAPON_ACHIEVEMENT( CAchievementKillWithWeaponBase, ACHIEVEMENT_P3_ENTOMOLOGIST, "ACH_ENTOMOLOGIST"
                             , P3_WEAPON_BEENEST, 0, 50, GAMERSCORE_15, AWARD_ID_NONE, ACH_SAVE_GLOBAL, TEAM_ANY, ACH_GAME_MODE_SINGLE, ACH_DLC_REQUIRED_BASE );

DECLARE_P3_WEAPON_ACHIEVEMENT( CAchievementKillWithWeaponBase, ACHIEVEMENT_P3_ARSONIST, "ACH_ARSONIST"
                             , P3_WEAPON_MOLOTOV, 0, 150, GAMERSCORE_15, AWARD_ID_NONE, ACH_SAVE_GLOBAL, TEAM_ANY, ACH_GAME_MODE_SINGLE, ACH_DLC_REQUIRED_BASE );

DECLARE_P3_WEAPON_ACHIEVEMENT( CAchievementKillWithWeaponBase, ACHIEVEMENT_P3_DANNY_TREJO, "ACH_DANNY_TREJO"
                             , P3_WEAPON_MACHETE, 0, 100, GAMERSCORE_15, AWARD_ID_NONE, ACH_SAVE_GLOBAL, TEAM_ANY, ACH_GAME_MODE_SINGLE, ACH_DLC_REQUIRED_BASE );

DECLARE_P3_WEAPON_ACHIEVEMENT( CAchievementKillWithWeaponBase, ACHIEVEMENT_P3_PSYCHO_DUNDEE, "ACH_PSYCHO_DUNDEE"
                             , P3_WEAPON_MACHETE, 1, 50, GAMERSCORE_15, AWARD_ID_NONE, ACH_SAVE_GLOBAL, TEAM_ANY, ACH_GAME_MODE_SINGLE, ACH_DLC_REQUIRED_BASE );

DECLARE_P3_WEAPON_ACHIEVEMENT( CAchievementKillWithWeaponBase, ACHIEVEMENT_P3_SCHWARZENEGGER, "ACH_SCHWARZENEGGER"
                             , P3_WEAPON_M60, 0, 200, GAMERSCORE_15, AWARD_ID_NONE, ACH_SAVE_GLOBAL, TEAM_ANY, ACH_GAME_MODE_SINGLE, ACH_DLC_REQUIRED_BASE );

DECLARE_P3_WEAPON_ACHIEVEMENT( CAchievementKillWithWeaponBase, ACHIEVEMENT_P3_WOLVERINES_R_GHEY, "ACH_WOLVERINES_R_GHEY"
                             , P3_WEAPON_WOLVERINE, 0, 50, GAMERSCORE_15, AWARD_ID_NONE, ACH_SAVE_GLOBAL, TEAM_ANY, ACH_GAME_MODE_SINGLE, ACH_DLC_REQUIRED_BASE );

#if 0
//-----------------------------------------------------------------------------
// Use every melee weapon to kill common infected.
//-----------------------------------------------------------------------------
class CAchievementCuriousBastard
	: public CPostal3Achievement
{
	DECLARE_CLASS( CAchievementCuriousBastard, CPostal3Achievement );

private:
	long WeapoIdToBitMask( int id )
	{
		static
		int weapon_ids[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14
			, -1, 15, 16, 17, 18, 19, -1, -1, -1, -1, -1, -1, -1, -1, -1, 20, -1, -1, 21, 22 };
		return ( weapon_ids[id-1] < 0 ) ? 0 : ( long(1) << weapon_ids[id-1] );
	}

	long m_iWeaponUsed;

public:
	//-----------------------------------------------------------------------------
	// Purpose: Class initialization
	//-----------------------------------------------------------------------------
	virtual void Init() 
	{
		BaseClass::Init();
		static const char *s_szEvents[] = { "npc_death" };
		m_pszEventNames = s_szEvents;
		m_iNumEvents = ARRAYSIZE( s_szEvents );
		m_iWeaponUsed = 0;
	}

	//-----------------------------------------------------------------------------
	// Purpose: Processes a game event to which we subscribed
	// Input  : eventName - Name of game event
	//			eventObj - Game event object.
	//-----------------------------------------------------------------------------
	virtual void OnEvent( const char* eventName, IGameEvent *eventObj )
	{
		const int nKiller = eventObj->GetInt( "attacker_id" );
		const C_BasePlayer *pLocalPlayer = C_BasePlayer::GetLocalPlayer();

		if ( pLocalPlayer && ( pLocalPlayer->entindex() == nKiller ) )
		{
			m_iWeaponUsed |= WeapoIdToBitMask( eventObj->GetInt( "weapon_id" ) );

			if ( m_iWeaponUsed == 8388607 )
				IncrementCount();
		}
	}

#if 0

	virtual void ReadProgress( IPlayerLocal *pPlayer )
	{
		TitleData2 const *pTitleData = ( TitleData2 const * ) pPlayer->GetPlayerTitleData( 1 );
		SetCount( pTitleData->iCountKillInfectedWithChainsaw );
		BaseClass::ReadProgress( pPlayer );
	}

	virtual bool WriteProgress( IPlayerLocal* pPlayer )
	{
		TitleData2 tdNew;
		tdNew.iCountKillInfectedWithChainsaw = GetCount();
		pPlayer->UpdatePlayerTitleData( 1, &tdNew.iCountKillInfectedWithChainsaw, offsetof( TitleData2, iCountKillInfectedWithChainsaw ), sizeof( tdNew.iCountKillInfectedWithChainsaw ) );
		return BaseClass::WriteProgress( pPlayer );
	}

#endif
};

DECLARE_P3_ACHIEVEMENT( CAchievementCuriousBastard, ACHIEVEMENT_P3_CURIOUS_BASTARD, "ACH_CURIOUS_BASTARD"
                      , 1, GAMERSCORE_15, AWARD_ID_NONE, ACH_SAVE_GLOBAL, TEAM_ANY, ACH_GAME_MODE_SINGLE, ACH_DLC_REQUIRED_BASE );

#endif

//-----------------------------------------------------------------------------
// Base time usage achievemnt. 
// Warn!! Do not use time too much!!
//-----------------------------------------------------------------------------
class CAchievementTimeUsageBase
	: public CPostal3Achievement
{
	DECLARE_CLASS( CAchievementTimeUsageBase, CPostal3Achievement );

private:
	int m_iWeaponID;
	int m_iWeaponFlags;

public:
	void SetEventName( const char *name )
	{
		static const char *s_szEvents[] = { "npc_death" };
		m_pszEventNames = s_szEvents;
		m_iNumEvents = ARRAYSIZE( s_szEvents );
	}

	//-----------------------------------------------------------------------------
	// Purpose: Class initialization
	//-----------------------------------------------------------------------------
	virtual void Init()
	{
		BaseClass::Init();
	}

	virtual void ListenForEvents()
	{
		BaseClass::ListenForEvents();
	}

	//-----------------------------------------------------------------------------
	// Purpose: Processes a game event to which we subscribed
	// Input  : eventName - Name of game event
	//			eventObj - Game event object.
	//-----------------------------------------------------------------------------
	virtual void OnEvent( const char* eventName, IGameEvent *eventObj )
	{
		const int nKiller = eventObj->GetInt( "attacker_id" );
		const C_BasePlayer *pLocalPlayer = C_BasePlayer::GetLocalPlayer();

		if ( pLocalPlayer && ( pLocalPlayer->entindex() == nKiller ) )
		{
			int iWeaponID = eventObj->GetInt( "weapon_id" );
			if ( iWeaponID != m_iWeaponID )
				return;

			int iWeaponFlags = eventObj->GetInt( "weapon_flags" );
			if ( iWeaponFlags != m_iWeaponFlags )
				return;

			IncrementCount();
		}
	}

#if 0

	virtual void ReadProgress( IPlayerLocal *pPlayer )
	{
		TitleData2 const *pTitleData = ( TitleData2 const * ) pPlayer->GetPlayerTitleData( 1 );
		SetCount( pTitleData->iCountKillInfectedWithChainsaw );
		BaseClass::ReadProgress( pPlayer );
	}

	virtual bool WriteProgress( IPlayerLocal* pPlayer )
	{
		TitleData2 tdNew;
		tdNew.iCountKillInfectedWithChainsaw = GetCount();
		pPlayer->UpdatePlayerTitleData( 1, &tdNew.iCountKillInfectedWithChainsaw, offsetof( TitleData2, iCountKillInfectedWithChainsaw ), sizeof( tdNew.iCountKillInfectedWithChainsaw ) );
		return BaseClass::WriteProgress( pPlayer );
	}

#endif
};

class CAchievementDontTazeMe
	: public CPostal3Achievement
{
	DECLARE_CLASS( CAchievementDontTazeMe, CPostal3Achievement );

public:
	//-----------------------------------------------------------------------------
	// Purpose: Class initialization
	//-----------------------------------------------------------------------------
	virtual void Init() 
	{
		BaseClass::Init();
		static const char *s_szEvents[] = { "npc_unconscious" };
		m_pszEventNames = s_szEvents;
		m_iNumEvents = ARRAYSIZE( s_szEvents );
	}

	//-----------------------------------------------------------------------------
	// Purpose: Processes a game event to which we subscribed
	// Input  : eventName - Name of game event
	//			eventObj - Game event object.
	//-----------------------------------------------------------------------------
	virtual void OnEvent( const char* eventName, IGameEvent *eventObj )
	{
		const int nKiller = eventObj->GetInt( "attacker_id" );
		const C_BasePlayer *pLocalPlayer = C_BasePlayer::GetLocalPlayer();

		if ( pLocalPlayer && ( pLocalPlayer->entindex() == nKiller ) )
		{
			if ( eventObj->GetInt( "weapon_id" ) == P3_WEAPON_TASER ) {
				IncrementCount();
			}
		}
	}

#if 0

	virtual void ReadProgress( IPlayerLocal *pPlayer )
	{
		TitleData2 const *pTitleData = ( TitleData2 const * ) pPlayer->GetPlayerTitleData( 1 );
		SetCount( pTitleData->iCountKillInfectedWithChainsaw );
		BaseClass::ReadProgress( pPlayer );
	}

	virtual bool WriteProgress( IPlayerLocal* pPlayer )
	{
		TitleData2 tdNew;
		tdNew.iCountKillInfectedWithChainsaw = GetCount();
		pPlayer->UpdatePlayerTitleData( 1, &tdNew.iCountKillInfectedWithChainsaw, offsetof( TitleData2, iCountKillInfectedWithChainsaw ), sizeof( tdNew.iCountKillInfectedWithChainsaw ) );
		return BaseClass::WriteProgress( pPlayer );
	}

#endif
};

DECLARE_P3_ACHIEVEMENT( CAchievementDontTazeMe, ACHIEVEMENT_P3_DONT_TAZE_ME_BRO, "ACH_DONT_TAZE_ME_BRO"
                      , 200, GAMERSCORE_15, AWARD_ID_NONE, ACH_SAVE_GLOBAL, TEAM_ANY, ACH_GAME_MODE_SINGLE, ACH_DLC_REQUIRED_BASE );

#if 0
//-----------------------------------------------------------------------------
// Use every melee weapon to kill common infected.
//-----------------------------------------------------------------------------
class CAchievementCultureWarrior
	: public CPostal3Achievement
{
	DECLARE_CLASS( CAchievementCultureWarrior, CPostal3Achievement );

public:
	//-----------------------------------------------------------------------------
	// Purpose: Class initialization
	//-----------------------------------------------------------------------------
	virtual void Init() 
	{
		BaseClass::Init();
		static const char *s_szEvents[] = { "npc_death", "npc_unconscious" };
		m_pszEventNames = s_szEvents;
		m_iNumEvents = ARRAYSIZE( s_szEvents );
	}

	//-----------------------------------------------------------------------------
	// Purpose: Processes a game event to which we subscribed
	// Input  : eventName - Name of game event
	//			eventObj - Game event object.
	//-----------------------------------------------------------------------------
	virtual void OnEvent( const char* eventName, IGameEvent *eventObj )
	{
		const int nKiller = eventObj->GetInt( "attacker_id" );
		const C_BasePlayer *pLocalPlayer = C_BasePlayer::GetLocalPlayer();

		if ( pLocalPlayer && ( pLocalPlayer->entindex() == nKiller ) )
		{
			int iMannerID = eventObj->GetInt( "manner_id" );
			int iFactionID = eventObj->GetInt( "faction_id" );
			if ( iMannerID == M_SoccerMom 
				|| iFactionID == F_Zealots )
			{
				IncrementCount();
			}
		}
	}

#if 0

	virtual void ReadProgress( IPlayerLocal *pPlayer )
	{
		TitleData2 const *pTitleData = ( TitleData2 const * ) pPlayer->GetPlayerTitleData( 1 );
		SetCount( pTitleData->iCountKillInfectedWithChainsaw );
		BaseClass::ReadProgress( pPlayer );
	}

	virtual bool WriteProgress( IPlayerLocal* pPlayer )
	{
		TitleData2 tdNew;
		tdNew.iCountKillInfectedWithChainsaw = GetCount();
		pPlayer->UpdatePlayerTitleData( 1, &tdNew.iCountKillInfectedWithChainsaw, offsetof( TitleData2, iCountKillInfectedWithChainsaw ), sizeof( tdNew.iCountKillInfectedWithChainsaw ) );
		return BaseClass::WriteProgress( pPlayer );
	}

#endif
};

DECLARE_P3_ACHIEVEMENT( CAchievementCultureWarrior, ACHIEVEMENT_P3_CULTURE_WARRIOR, "ACH_CULTURE_WARRIOR"
                      , 50, GAMERSCORE_15, AWARD_ID_NONE, ACH_SAVE_GLOBAL, TEAM_ANY, ACH_GAME_MODE_SINGLE, ACH_DLC_REQUIRED_BASE );
#endif


class CAchievementRealAmerican
	: public CPostal3Achievement
{
	DECLARE_CLASS( CAchievementRealAmerican, CPostal3Achievement );

public:
	//-----------------------------------------------------------------------------
	// Purpose: Class initialization
	//-----------------------------------------------------------------------------
	virtual void Init() 
	{
		BaseClass::Init();
		static const char *s_szEvents[] = { "npc_death", "npc_unconscious" };
		m_pszEventNames = s_szEvents;
		m_iNumEvents = ARRAYSIZE( s_szEvents );
	}

	//-----------------------------------------------------------------------------
	// Purpose: Processes a game event to which we subscribed
	// Input  : eventName - Name of game event
	//			eventObj - Game event object.
	//-----------------------------------------------------------------------------
	virtual void OnEvent( const char* eventName, IGameEvent *eventObj )
	{
		const int nKiller = eventObj->GetInt( "attacker_id" );
		const C_BasePlayer *pLocalPlayer = C_BasePlayer::GetLocalPlayer();

		if ( pLocalPlayer && ( pLocalPlayer->entindex() == nKiller ) )
		{
			int iMannerID = eventObj->GetInt( "manner_id" );
			if ( iMannerID == M_JihadBeard )
			{
				IncrementCount();
			}
		}
	}

#if 0

	virtual void ReadProgress( IPlayerLocal *pPlayer )
	{
		TitleData2 const *pTitleData = ( TitleData2 const * ) pPlayer->GetPlayerTitleData( 1 );
		SetCount( pTitleData->iCountKillInfectedWithChainsaw );
		BaseClass::ReadProgress( pPlayer );
	}

	virtual bool WriteProgress( IPlayerLocal* pPlayer )
	{
		TitleData2 tdNew;
		tdNew.iCountKillInfectedWithChainsaw = GetCount();
		pPlayer->UpdatePlayerTitleData( 1, &tdNew.iCountKillInfectedWithChainsaw, offsetof( TitleData2, iCountKillInfectedWithChainsaw ), sizeof( tdNew.iCountKillInfectedWithChainsaw ) );
		return BaseClass::WriteProgress( pPlayer );
	}

#endif
};

DECLARE_P3_ACHIEVEMENT( CAchievementRealAmerican, ACHIEVEMENT_P3_REAL_AMERICAN, "ACH_REAL_AMERICAN"
                      , 235, GAMERSCORE_15, AWARD_ID_NONE, ACH_SAVE_GLOBAL, TEAM_ANY, ACH_GAME_MODE_SINGLE, ACH_DLC_REQUIRED_BASE );

class CAchievementKavorikian
	: public CPostal3Achievement
{
	DECLARE_CLASS( CAchievementKavorikian, CPostal3Achievement );

public:
	//-----------------------------------------------------------------------------
	// Purpose: Class initialization
	//-----------------------------------------------------------------------------
	virtual void Init() 
	{
		BaseClass::Init();
		static const char *s_szEvents[] = { "npc_death" };
		m_pszEventNames = s_szEvents;
		m_iNumEvents = ARRAYSIZE( s_szEvents );
	}

	//-----------------------------------------------------------------------------
	// Purpose: Processes a game event to which we subscribed
	// Input  : eventName - Name of game event
	//			eventObj - Game event object.
	//-----------------------------------------------------------------------------
	virtual void OnEvent( const char* eventName, IGameEvent *eventObj )
	{
		const int nKiller = eventObj->GetInt( "attacker_id" );
		const C_BasePlayer *pLocalPlayer = C_BasePlayer::GetLocalPlayer();

		if ( pLocalPlayer && ( pLocalPlayer->entindex() == nKiller ) )
		{
			int iMannerID = eventObj->GetInt( "manner_id" );
			if ( iMannerID == M_StGranny )
			{
				IncrementCount();
			}
		}
	}

#if 0

	virtual void ReadProgress( IPlayerLocal *pPlayer )
	{
		TitleData2 const *pTitleData = ( TitleData2 const * ) pPlayer->GetPlayerTitleData( 1 );
		SetCount( pTitleData->iCountKillInfectedWithChainsaw );
		BaseClass::ReadProgress( pPlayer );
	}

	virtual bool WriteProgress( IPlayerLocal* pPlayer )
	{
		TitleData2 tdNew;
		tdNew.iCountKillInfectedWithChainsaw = GetCount();
		pPlayer->UpdatePlayerTitleData( 1, &tdNew.iCountKillInfectedWithChainsaw, offsetof( TitleData2, iCountKillInfectedWithChainsaw ), sizeof( tdNew.iCountKillInfectedWithChainsaw ) );
		return BaseClass::WriteProgress( pPlayer );
	}

#endif
};

DECLARE_P3_ACHIEVEMENT( CAchievementKavorikian, ACHIEVEMENT_P3_KAVORKIAN, "ACH_KAVORKIAN"
                      , 30, GAMERSCORE_15, AWARD_ID_NONE, ACH_SAVE_GLOBAL, TEAM_ANY, ACH_GAME_MODE_SINGLE, ACH_DLC_REQUIRED_BASE );

//-----------------------------------------------------------------------------
// Purpose: 300 headshots
//-----------------------------------------------------------------------------
class CAchievementNeurosurgeon : public CPostal3Achievement
{
	DECLARE_CLASS( CAchievementNeurosurgeon, CPostal3Achievement );
public:
	virtual void Init() 
	{
		BaseClass::Init();
		static const char *s_szEvents[] = { "npc_head_shot" };
		m_pszEventNames = s_szEvents;
		m_iNumEvents = ARRAYSIZE( s_szEvents );
	}

	virtual void OnEvent( const char* eventName, IGameEvent *eventObj )
	{
		IncrementCount();
	}

#if 0

	virtual void ReadProgress( IPlayerLocal *pPlayer )
	{
		TitleData2 const *pTitleData = ( TitleData2 const * ) pPlayer->GetPlayerTitleData( 1 );
		SetCount( pTitleData->iCountCutOffHeadsMelee );
		BaseClass::ReadProgress( pPlayer );
	}

	virtual bool WriteProgress( IPlayerLocal* pPlayer )
	{
		TitleData2 tdNew;
		tdNew.iCountCutOffHeadsMelee = GetCount();
		pPlayer->UpdatePlayerTitleData( 1, &tdNew.iCountCutOffHeadsMelee, offsetof( TitleData2, iCountCutOffHeadsMelee ), sizeof( tdNew.iCountCutOffHeadsMelee ) );
		return BaseClass::WriteProgress( pPlayer );
	}

#endif
};
DECLARE_P3_ACHIEVEMENT( CAchievementNeurosurgeon, ACHIEVEMENT_P3_NEUROSURGEON, "ACH_NEUROSURGEON"
                      , 300, GAMERSCORE_15, AWARD_ID_NONE, ACH_SAVE_GLOBAL, TEAM_ANY, ACH_GAME_MODE_SINGLE, ACH_DLC_REQUIRED_BASE );

//-----------------------------------------------------------------------------
// Purpose: 200 limbs torn apart
//-----------------------------------------------------------------------------
class CAchievementMegasadist : public CPostal3Achievement
{
	DECLARE_CLASS( CAchievementMegasadist, CPostal3Achievement );
public:
	virtual void Init() 
	{
		BaseClass::Init();
		static const char *s_szEvents[] = { "npc_butched" };
		m_pszEventNames = s_szEvents;
		m_iNumEvents = ARRAYSIZE( s_szEvents );
	}

	virtual void OnEvent( const char* eventName, IGameEvent *eventObj )
	{
		const int nKiller = eventObj->GetInt( "attacker_id" );
		const C_BasePlayer *pLocalPlayer = C_BasePlayer::GetLocalPlayer();

		if ( pLocalPlayer && ( pLocalPlayer->entindex() == nKiller ) )
		{
			IncrementCount();
		}
	}

#if 0

	virtual void ReadProgress( IPlayerLocal *pPlayer )
	{
		TitleData2 const *pTitleData = ( TitleData2 const * ) pPlayer->GetPlayerTitleData( 1 );
		SetCount( pTitleData->iCountCutOffHeadsMelee );
		BaseClass::ReadProgress( pPlayer );
	}

	virtual bool WriteProgress( IPlayerLocal* pPlayer )
	{
		TitleData2 tdNew;
		tdNew.iCountCutOffHeadsMelee = GetCount();
		pPlayer->UpdatePlayerTitleData( 1, &tdNew.iCountCutOffHeadsMelee, offsetof( TitleData2, iCountCutOffHeadsMelee ), sizeof( tdNew.iCountCutOffHeadsMelee ) );
		return BaseClass::WriteProgress( pPlayer );
	}

#endif
};
DECLARE_P3_ACHIEVEMENT( CAchievementMegasadist, ACHIEVEMENT_P3_MEGA_SADIST, "ACH_MEGA_SADIST"
                      , 200, GAMERSCORE_15, AWARD_ID_NONE, ACH_SAVE_GLOBAL, TEAM_ANY, ACH_GAME_MODE_SINGLE, ACH_DLC_REQUIRED_BASE );

//-----------------------------------------------------------------------------
// Purpose: destroy 50 cars
//-----------------------------------------------------------------------------
class CAchievementToyotaRecall : public CPostal3Achievement
{
	DECLARE_CLASS( CAchievementToyotaRecall, CPostal3Achievement );
public:
	virtual void Init() 
	{
		BaseClass::Init();
		static const char *s_szEvents[] = { "entity_destroyed" };
		m_pszEventNames = s_szEvents;
		m_iNumEvents = ARRAYSIZE( s_szEvents );
	}

	virtual void OnEvent( const char* eventName, IGameEvent *eventObj )
	{
		int type = eventObj->GetInt( "type" );

		if ( type == 1 )
		{
			IncrementCount();
		}
	}

#if 0

	virtual void ReadProgress( IPlayerLocal *pPlayer )
	{
		TitleData2 const *pTitleData = ( TitleData2 const * ) pPlayer->GetPlayerTitleData( 1 );
		SetCount( pTitleData->iCountCutOffHeadsMelee );
		BaseClass::ReadProgress( pPlayer );
	}

	virtual bool WriteProgress( IPlayerLocal* pPlayer )
	{
		TitleData2 tdNew;
		tdNew.iCountCutOffHeadsMelee = GetCount();
		pPlayer->UpdatePlayerTitleData( 1, &tdNew.iCountCutOffHeadsMelee, offsetof( TitleData2, iCountCutOffHeadsMelee ), sizeof( tdNew.iCountCutOffHeadsMelee ) );
		return BaseClass::WriteProgress( pPlayer );
	}

#endif
};
DECLARE_P3_ACHIEVEMENT( CAchievementToyotaRecall, ACHIEVEMENT_P3_TOYOTA_RECALL, "ACH_TOYOTA_RECALL"
                      , 50, GAMERSCORE_15, AWARD_ID_NONE, ACH_SAVE_GLOBAL, TEAM_ANY, ACH_GAME_MODE_SINGLE, ACH_DLC_REQUIRED_BASE );

//-----------------------------------------------------------------------------
// Purpose: smash 50 windows
//-----------------------------------------------------------------------------
class CAchievementPropertyDamage : public CPostal3Achievement
{
	DECLARE_CLASS( CAchievementPropertyDamage, CPostal3Achievement );
public:
	virtual void Init() 
	{
		BaseClass::Init();
		static const char *s_szEvents[] = { "window_smashed" };
		m_pszEventNames = s_szEvents;
		m_iNumEvents = ARRAYSIZE( s_szEvents );
	}

	virtual void OnLocalPlayerEvent( C_BasePlayer *localPlayer, const char *eventName, IGameEvent *eventObj )
	{
		IncrementCount();
	}

#if 0

	virtual void ReadProgress( IPlayerLocal *pPlayer )
	{
		TitleData2 const *pTitleData = ( TitleData2 const * ) pPlayer->GetPlayerTitleData( 1 );
		SetCount( pTitleData->iCountCutOffHeadsMelee );
		BaseClass::ReadProgress( pPlayer );
	}

	virtual bool WriteProgress( IPlayerLocal* pPlayer )
	{
		TitleData2 tdNew;
		tdNew.iCountCutOffHeadsMelee = GetCount();
		pPlayer->UpdatePlayerTitleData( 1, &tdNew.iCountCutOffHeadsMelee, offsetof( TitleData2, iCountCutOffHeadsMelee ), sizeof( tdNew.iCountCutOffHeadsMelee ) );
		return BaseClass::WriteProgress( pPlayer );
	}

#endif
};
DECLARE_P3_ACHIEVEMENT( CAchievementPropertyDamage, ACHIEVEMENT_P3_PROPERTY_DAMAGE, "ACH_PROPERTY_DAMAGE"
                      , 50, GAMERSCORE_15, AWARD_ID_NONE, ACH_SAVE_GLOBAL | ACH_FILTER_LOCAL_PLAYER_EVENTS, TEAM_ANY, ACH_GAME_MODE_SINGLE, ACH_DLC_REQUIRED_BASE );

//-----------------------------------------------------------------------------
// Purpose: respawn 15 times
//-----------------------------------------------------------------------------
class CAchievementFailZombie : public CPostal3Achievement
{
	DECLARE_CLASS( CAchievementFailZombie, CPostal3Achievement );
public:
	virtual void Init() 
	{
		BaseClass::Init();
		static const char *s_szEvents[] = { "player_respawn" };
		m_pszEventNames = s_szEvents;
		m_iNumEvents = ARRAYSIZE( s_szEvents );
	}

	virtual void OnEvent( const char* eventName, IGameEvent *eventObj )
	{
		IncrementCount();
	}

#if 0

	virtual void ReadProgress( IPlayerLocal *pPlayer )
	{
		TitleData2 const *pTitleData = ( TitleData2 const * ) pPlayer->GetPlayerTitleData( 1 );
		SetCount( pTitleData->iCountCutOffHeadsMelee );
		BaseClass::ReadProgress( pPlayer );
	}

	virtual bool WriteProgress( IPlayerLocal* pPlayer )
	{
		TitleData2 tdNew;
		tdNew.iCountCutOffHeadsMelee = GetCount();
		pPlayer->UpdatePlayerTitleData( 1, &tdNew.iCountCutOffHeadsMelee, offsetof( TitleData2, iCountCutOffHeadsMelee ), sizeof( tdNew.iCountCutOffHeadsMelee ) );
		return BaseClass::WriteProgress( pPlayer );
	}

#endif
};
DECLARE_P3_ACHIEVEMENT( CAchievementFailZombie, ACHIEVEMENT_P3_FAIL_ZOMBIE, "ACH_FAIL_ZOMBIE"
                      , 20, GAMERSCORE_15, AWARD_ID_NONE, ACH_SAVE_GLOBAL, TEAM_ANY, ACH_GAME_MODE_SINGLE, ACH_DLC_REQUIRED_BASE );


//-----------------------------------------------------------------------------
// Purpose: kill 1000 bystanders
//-----------------------------------------------------------------------------
class CAchievementJackThompsonWasRight : public CPostal3Achievement
{
	DECLARE_CLASS( CAchievementJackThompsonWasRight, CPostal3Achievement );
public:
	virtual void Init() 
	{
		BaseClass::Init();
		static const char *s_szEvents[] = { "npc_death" };
		m_pszEventNames = s_szEvents;
		m_iNumEvents = ARRAYSIZE( s_szEvents );
	}

	virtual void OnEvent( const char* eventName, IGameEvent *eventObj )
	{
		const int nKiller = eventObj->GetInt( "attacker_id" );
		const C_BasePlayer *pLocalPlayer = C_BasePlayer::GetLocalPlayer();

		if ( pLocalPlayer && ( pLocalPlayer->entindex() == nKiller ) )
		{
			int iMannerID = eventObj->GetInt( "manner_id" );
			if ( iMannerID == M_Bystander )
			{
				IncrementCount();
			}
		}
	}

#if 0

	virtual void ReadProgress( IPlayerLocal *pPlayer )
	{
		TitleData2 const *pTitleData = ( TitleData2 const * ) pPlayer->GetPlayerTitleData( 1 );
		SetCount( pTitleData->iCountCutOffHeadsMelee );
		BaseClass::ReadProgress( pPlayer );
	}

	virtual bool WriteProgress( IPlayerLocal* pPlayer )
	{
		TitleData2 tdNew;
		tdNew.iCountCutOffHeadsMelee = GetCount();
		pPlayer->UpdatePlayerTitleData( 1, &tdNew.iCountCutOffHeadsMelee, offsetof( TitleData2, iCountCutOffHeadsMelee ), sizeof( tdNew.iCountCutOffHeadsMelee ) );
		return BaseClass::WriteProgress( pPlayer );
	}

#endif
};
DECLARE_P3_ACHIEVEMENT( CAchievementJackThompsonWasRight, ACHIEVEMENT_P3_JACK_THOMPSON_WAS_RIGHT, "ACH_JACK_THOMPSON_WAS_RIGHT"
                      , 1000, GAMERSCORE_15, AWARD_ID_NONE, ACH_SAVE_GLOBAL, TEAM_ANY, ACH_GAME_MODE_SINGLE, ACH_DLC_REQUIRED_BASE );

//-----------------------------------------------------------------------------
// Purpose: launch cat on a rocket
//-----------------------------------------------------------------------------
class CAchievementAstronaut : public CPostal3Achievement
{
	DECLARE_CLASS( CAchievementAstronaut, CPostal3Achievement );
public:
	virtual void Init() 
	{
		BaseClass::Init();
		static const char *s_szEvents[] = { "cat_launched" };
		m_pszEventNames = s_szEvents;
		m_iNumEvents = ARRAYSIZE( s_szEvents );
	}

	virtual void OnEvent( const char* eventName, IGameEvent *eventObj )
	{
		IncrementCount();
	}

#if 0

	virtual void ReadProgress( IPlayerLocal *pPlayer )
	{
		TitleData2 const *pTitleData = ( TitleData2 const * ) pPlayer->GetPlayerTitleData( 1 );
		SetCount( pTitleData->iCountCutOffHeadsMelee );
		BaseClass::ReadProgress( pPlayer );
	}

	virtual bool WriteProgress( IPlayerLocal* pPlayer )
	{
		TitleData2 tdNew;
		tdNew.iCountCutOffHeadsMelee = GetCount();
		pPlayer->UpdatePlayerTitleData( 1, &tdNew.iCountCutOffHeadsMelee, offsetof( TitleData2, iCountCutOffHeadsMelee ), sizeof( tdNew.iCountCutOffHeadsMelee ) );
		return BaseClass::WriteProgress( pPlayer );
	}

#endif
};
//DECLARE_P3_ACHIEVEMENT( CAchievementAstronaut, ACHIEVEMENT_P3_ASTRONAUT, "ACH_ASTRONAUT"
//                      , 1, GAMERSCORE_15, AWARD_ID_NONE, ACH_SAVE_GLOBAL, TEAM_ANY, ACH_GAME_MODE_SINGLE, ACH_DLC_REQUIRED_BASE );

//-----------------------------------------------------------------------------
// Purpose: launch cat on a rocket
//-----------------------------------------------------------------------------
class CAchievementBadCop : public CPostal3Achievement
{
	DECLARE_CLASS( CAchievementBadCop, CPostal3Achievement );
public:
	virtual void Init() 
	{
		BaseClass::Init();
		static const char *s_szEvents[] = { "arrest_on_catnip" };
		m_pszEventNames = s_szEvents;
		m_iNumEvents = ARRAYSIZE( s_szEvents );
	}

	virtual void OnEvent( const char* eventName, IGameEvent *eventObj )
	{
		IncrementCount();
	}

#if 0

	virtual void ReadProgress( IPlayerLocal *pPlayer )
	{
		TitleData2 const *pTitleData = ( TitleData2 const * ) pPlayer->GetPlayerTitleData( 1 );
		SetCount( pTitleData->iCountCutOffHeadsMelee );
		BaseClass::ReadProgress( pPlayer );
	}

	virtual bool WriteProgress( IPlayerLocal* pPlayer )
	{
		TitleData2 tdNew;
		tdNew.iCountCutOffHeadsMelee = GetCount();
		pPlayer->UpdatePlayerTitleData( 1, &tdNew.iCountCutOffHeadsMelee, offsetof( TitleData2, iCountCutOffHeadsMelee ), sizeof( tdNew.iCountCutOffHeadsMelee ) );
		return BaseClass::WriteProgress( pPlayer );
	}

#endif
};
//DECLARE_P3_ACHIEVEMENT( CAchievementBadCop, ACHIEVEMENT_P3_BAD_COP, "ACH_BAD_COP"
//                      , 1, GAMERSCORE_15, AWARD_ID_NONE, ACH_SAVE_GLOBAL, TEAM_ANY, ACH_GAME_MODE_SINGLE, ACH_DLC_REQUIRED_BASE );

//-----------------------------------------------------------------------------
// Purpose: Arrest 200 props
//-----------------------------------------------------------------------------
class CAchievementTJHooker : public CPostal3Achievement
{
	DECLARE_CLASS( CAchievementTJHooker, CPostal3Achievement );
public:
	virtual void Init() 
	{
		BaseClass::Init();
		static const char *s_szEvents[] = { "npc_arrest" };
		m_pszEventNames = s_szEvents;
		m_iNumEvents = ARRAYSIZE( s_szEvents );
	}

	virtual void OnEvent( const char* eventName, IGameEvent *eventObj )
	{
		IncrementCount();
	}

#if 0

	virtual void ReadProgress( IPlayerLocal *pPlayer )
	{
		TitleData2 const *pTitleData = ( TitleData2 const * ) pPlayer->GetPlayerTitleData( 1 );
		SetCount( pTitleData->iCountCutOffHeadsMelee );
		BaseClass::ReadProgress( pPlayer );
	}

	virtual bool WriteProgress( IPlayerLocal* pPlayer )
	{
		TitleData2 tdNew;
		tdNew.iCountCutOffHeadsMelee = GetCount();
		pPlayer->UpdatePlayerTitleData( 1, &tdNew.iCountCutOffHeadsMelee, offsetof( TitleData2, iCountCutOffHeadsMelee ), sizeof( tdNew.iCountCutOffHeadsMelee ) );
		return BaseClass::WriteProgress( pPlayer );
	}

#endif
};
DECLARE_P3_ACHIEVEMENT( CAchievementTJHooker, ACHIEVEMENT_P3_T_J_HOOKER, "ACH_T_J_HOOKER"
                      , 200, GAMERSCORE_15, AWARD_ID_NONE, ACH_SAVE_GLOBAL, TEAM_ANY, ACH_GAME_MODE_SINGLE, ACH_DLC_REQUIRED_BASE );

//-----------------------------------------------------------------------------
// Purpose: Piss 100 galons of urine
//-----------------------------------------------------------------------------
class CAchievementCamelback : public CPostal3Achievement
{
	DECLARE_CLASS( CAchievementCamelback, CPostal3Achievement );

public:
	virtual void Init() 
	{
		BaseClass::Init();
		static const char *s_szEvents[] = { "plr_wee_start", "plr_wee_stop" };
		m_pszEventNames = s_szEvents;
		m_iNumEvents = ARRAYSIZE( s_szEvents );

		wee_start_time = -1;
		wee_amount = 0;
	}

	virtual void OnEvent( const char* eventName, IGameEvent *eventObj )
	{
		if ( !Q_strcmp( "plr_wee_start", eventName ) )
		{
			wee_start_time = gpGlobals->curtime;
		}
		else if ( wee_start_time > 0 )
		{
			float d = gpGlobals->curtime - wee_start_time;

			wee_amount += d * PEEWEE_SPEED;

			for ( int i = 0; i < (int)wee_amount; i++ )
			{
				IncrementCount();
			}
			wee_amount -= (int)wee_amount;

			wee_start_time = -1;
		}
	}

private:

	float wee_amount;
	float wee_start_time;

#if 0

	virtual void ReadProgress( IPlayerLocal *pPlayer )
	{
		TitleData2 const *pTitleData = ( TitleData2 const * ) pPlayer->GetPlayerTitleData( 1 );
		SetCount( pTitleData->iCountCutOffHeadsMelee );
		BaseClass::ReadProgress( pPlayer );
	}

	virtual bool WriteProgress( IPlayerLocal* pPlayer )
	{
		TitleData2 tdNew;
		tdNew.iCountCutOffHeadsMelee = GetCount();
		pPlayer->UpdatePlayerTitleData( 1, &tdNew.iCountCutOffHeadsMelee, offsetof( TitleData2, iCountCutOffHeadsMelee ), sizeof( tdNew.iCountCutOffHeadsMelee ) );
		return BaseClass::WriteProgress( pPlayer );
	}

#endif
};

DECLARE_P3_ACHIEVEMENT( CAchievementCamelback, ACHIEVEMENT_P3_CAMELBACK, "ACH_CAMELBACK"
                      , 100, GAMERSCORE_15, AWARD_ID_NONE, ACH_SAVE_GLOBAL, TEAM_ANY, ACH_GAME_MODE_SINGLE, ACH_DLC_REQUIRED_BASE );

//-----------------------------------------------------------------------------
// Purpose: spend 15 minutes in catnip bullet time
//-----------------------------------------------------------------------------
class CAchievementThereIsNoSpoon : public CPostal3Achievement
{
	DECLARE_CLASS( CAchievementThereIsNoSpoon, CPostal3Achievement );

public:
	virtual void Init() 
	{
		BaseClass::Init();
		static const char *s_szEvents[] = { "plr_catnip_start", "plr_catnip_stop" };
		m_pszEventNames = s_szEvents;
		m_iNumEvents = ARRAYSIZE( s_szEvents );

		catnip_start_time = -1;
	}

	virtual void OnEvent( const char* eventName, IGameEvent *eventObj )
	{
		if ( !Q_strcmp( "plr_catnip_start", eventName ) )
		{
			catnip_start_time = gpGlobals->curtime;
		}
		else if ( catnip_start_time > 0 )
		{
			float d = (gpGlobals->curtime - catnip_start_time) * 5;

			for ( int i = 0; i < (int)d; i++ )
			{
				IncrementCount();
			}

			catnip_start_time = -1;
		}
	}

private:

	float catnip_start_time;

#if 0

	virtual void ReadProgress( IPlayerLocal *pPlayer )
	{
		TitleData2 const *pTitleData = ( TitleData2 const * ) pPlayer->GetPlayerTitleData( 1 );
		SetCount( pTitleData->iCountCutOffHeadsMelee );
		BaseClass::ReadProgress( pPlayer );
	}

	virtual bool WriteProgress( IPlayerLocal* pPlayer )
	{
		TitleData2 tdNew;
		tdNew.iCountCutOffHeadsMelee = GetCount();
		pPlayer->UpdatePlayerTitleData( 1, &tdNew.iCountCutOffHeadsMelee, offsetof( TitleData2, iCountCutOffHeadsMelee ), sizeof( tdNew.iCountCutOffHeadsMelee ) );
		return BaseClass::WriteProgress( pPlayer );
	}

#endif
};

DECLARE_P3_ACHIEVEMENT( CAchievementThereIsNoSpoon, ACHIEVEMENT_P3_THERE_IS_NO_SPOON, "ACH_THERE_IS_NO_SPOON"
                      , 900, GAMERSCORE_15, AWARD_ID_NONE, ACH_SAVE_GLOBAL, TEAM_ANY, ACH_GAME_MODE_SINGLE, ACH_DLC_REQUIRED_BASE );


//-----------------------------------------------------------------------------
// Purpose: swap path 10 times
//-----------------------------------------------------------------------------
class CAchievementBipolar : public CPostal3Achievement
{
	DECLARE_CLASS( CAchievementBipolar, CPostal3Achievement );

public:
	virtual void Init() 
	{
		BaseClass::Init();
		static const char *s_szEvents[] = { "join_the_dark_forces", "join_the_alliance" };
		m_pszEventNames = s_szEvents;
		m_iNumEvents = ARRAYSIZE( s_szEvents );

		last_path = -1;
	}

	virtual void OnEvent( const char* eventName, IGameEvent *eventObj )
	{
		int path = -1;

		if ( !Q_strcmp( "join_the_dark_forces", eventName ) )
		{
			path = 1;
		}
		else if ( !Q_strcmp( "join_the_alliance", eventName ) )
		{
			path = 2;
		}

		if ( last_path < 0 ) {
			last_path = path;
		}
		else if ( path > 0 && path != last_path ) {
			IncrementCount();
			last_path = path;
		}
	}

private:

	int last_path;

#if 0

	virtual void ReadProgress( IPlayerLocal *pPlayer )
	{
		TitleData2 const *pTitleData = ( TitleData2 const * ) pPlayer->GetPlayerTitleData( 1 );
		SetCount( pTitleData->iCountCutOffHeadsMelee );
		BaseClass::ReadProgress( pPlayer );
	}

	virtual bool WriteProgress( IPlayerLocal* pPlayer )
	{
		TitleData2 tdNew;
		tdNew.iCountCutOffHeadsMelee = GetCount();
		pPlayer->UpdatePlayerTitleData( 1, &tdNew.iCountCutOffHeadsMelee, offsetof( TitleData2, iCountCutOffHeadsMelee ), sizeof( tdNew.iCountCutOffHeadsMelee ) );
		return BaseClass::WriteProgress( pPlayer );
	}

#endif
};

DECLARE_P3_ACHIEVEMENT( CAchievementBipolar, ACHIEVEMENT_P3_BIPOLAR, "ACH_BIPOLAR"
                      , 5, GAMERSCORE_15, AWARD_ID_NONE, ACH_SAVE_WITH_GAME, TEAM_ANY, ACH_GAME_MODE_SINGLE, ACH_DLC_REQUIRED_BASE );

//-----------------------------------------------------------------------------
// Purpose: swap path 10 times
//-----------------------------------------------------------------------------
class CAchievementEmo : public CPostal3Achievement
{
	DECLARE_CLASS( CAchievementEmo, CPostal3Achievement );

public:
	virtual void Init()
	{
		BaseClass::Init();
		static const char *s_szEvents[] = { "player_hurt" };
		m_pszEventNames = s_szEvents;
		m_iNumEvents = ARRAYSIZE( s_szEvents );
	}

	virtual void OnLocalPlayerEvent( C_BasePlayer *localPlayer, const char *eventName, IGameEvent *eventObj )
	{
		if ( eventObj->GetInt( "attacker" ) == localPlayer->GetUserID() ) {
			IncrementCount();
		}
	}

#if 0

	virtual void ReadProgress( IPlayerLocal *pPlayer )
	{
		TitleData2 const *pTitleData = ( TitleData2 const * ) pPlayer->GetPlayerTitleData( 1 );
		SetCount( pTitleData->iCountCutOffHeadsMelee );
		BaseClass::ReadProgress( pPlayer );
	}

	virtual bool WriteProgress( IPlayerLocal* pPlayer )
	{
		TitleData2 tdNew;
		tdNew.iCountCutOffHeadsMelee = GetCount();
		pPlayer->UpdatePlayerTitleData( 1, &tdNew.iCountCutOffHeadsMelee, offsetof( TitleData2, iCountCutOffHeadsMelee ), sizeof( tdNew.iCountCutOffHeadsMelee ) );
		return BaseClass::WriteProgress( pPlayer );
	}

#endif
};

DECLARE_P3_ACHIEVEMENT( CAchievementEmo, ACHIEVEMENT_P3_EMO, "ACH_EMO"
                      , 1, GAMERSCORE_15, AWARD_ID_NONE, ACH_SAVE_GLOBAL | ACH_FILTER_LOCAL_PLAYER_EVENTS, TEAM_ANY, ACH_GAME_MODE_SINGLE, ACH_DLC_REQUIRED_BASE );

//-----------------------------------------------------------------------------
// Complete a mission.
//-----------------------------------------------------------------------------
class CAchievementBaseEndMission : public CPostal3Achievement
{
	DECLARE_CLASS( CAchievementBaseEndMission, CPostal3Achievement );

protected:
	char mission_name[16];

public:
	void SetMissionName( const char *name )
	{
		Q_strcpy( mission_name, name );
	}

	virtual void Init()
	{
		BaseClass::Init();
		static const char *s_szEvents[] = { "end_mission" };
		m_pszEventNames = s_szEvents;
		m_iNumEvents = ARRAYSIZE( s_szEvents );
	}

	virtual void OnEvent( const char* eventName, IGameEvent *eventObj )
	{
		if ( !Q_strcmp( "end_mission", eventName ) ) {
			const char *n = eventObj->GetString( "name" );
			if ( !Q_stricmp( mission_name, n ) ) {
				IncrementCount();
			}
		}
	}
};
#define DECLARE_P3_END_MISSION_ACHIEVEMENT_( className, achievementID, achievementName, sMissionName, iGoalCount, iPointValue, iDisplayOrder, assetAwardName, flags, iTeam, iGameMode, dlcRequired, bHidden ) \
	static CBaseAchievement *Create_##className##_##achievementID( void )	\
{																			\
	CAchievementBaseEndMission *pAchievement = new className();				\
	pAchievement->SetAchievementID( achievementID );						\
	/*pAchievement->SetDisplayOrder( iDisplayOrder );*/						\
	pAchievement->SetName( achievementName );								\
	pAchievement->SetPointValue( iPointValue );								\
	pAchievement->SetHideUntilAchieved( bHidden );							\
	pAchievement->SetMissionName( sMissionName );							\
	pAchievement->SetGoal( iGoalCount );									\
	pAchievement->SetFlags( flags );										\
	pAchievement->SetGameDirFilter( "p3" );									\
	pAchievement->SetTeam( iTeam );											\
	pAchievement->SetGameMode( iGameMode );									\
	/*pAchievement->SetAssetAward( assetAwardName );*/						\
	pAchievement->SetDLCRequired( dlcRequired );							\
	return pAchievement;													\
};																			\
	static CBaseAchievementHelper g_##className##_##achievementID##_Helper( Create_##className##_##achievementID );

#define DECLARE_P3_END_MISSION_ACHIEVEMENT( className, achievementID, achievementName, sMissionName, iPointValue, assetAwardName, flags, iTeam, iGameMode, dlcRequired )	\
	DECLARE_P3_END_MISSION_ACHIEVEMENT_( className, achievementID, achievementName, sMissionName, 1, iPointValue, achievementID, assetAwardName, flags, iTeam, iGameMode, dlcRequired, false )


DECLARE_P3_END_MISSION_ACHIEVEMENT( CAchievementBaseEndMission, ACHIEVEMENT_P3_PDB_FINISH, "ACH_PDB_FINISH"
                                  , "pdb", GAMERSCORE_15, AWARD_ID_NONE, ACH_SAVE_GLOBAL, TEAM_ANY, ACH_GAME_MODE_SINGLE, ACH_DLC_REQUIRED_BASE );

DECLARE_P3_END_MISSION_ACHIEVEMENT( CAchievementBaseEndMission, ACHIEVEMENT_P3_CM_FINISH, "ACH_CM_FINISH"
                                  , "cm", GAMERSCORE_15, AWARD_ID_NONE, ACH_SAVE_GLOBAL, TEAM_ANY, ACH_GAME_MODE_SINGLE, ACH_DLC_REQUIRED_BASE );

//DECLARE_P3_WEAPON_ACHIEVEMENT2( CAchievementBaseEndMission, ACHIEVEMENT_P3_GR__FINISH, "ACH_GR__FINISH"
//                              , "gri", "grg", GAMERSCORE_15, AWARD_ID_NONE, ACH_SAVE_GLOBAL, TEAM_ANY, ACH_GAME_MODE_SINGLE, ACH_DLC_REQUIRED_BASE );

//DECLARE_P3_WEAPON_ACHIEVEMENT2( CAchievementBaseEndMission, ACHIEVEMENT_P3_ML__FINISH, "ACH_ML__FINISH"
//                              , "pdb", GAMERSCORE_15, AWARD_ID_NONE, ACH_SAVE_GLOBAL, TEAM_ANY, ACH_GAME_MODE_SINGLE, ACH_DLC_REQUIRED_BASE );

DECLARE_P3_END_MISSION_ACHIEVEMENT( CAchievementBaseEndMission, ACHIEVEMENT_P3_AA2_FINISH, "ACH_AA2_FINISH"
                                  , "aa2", GAMERSCORE_15, AWARD_ID_NONE, ACH_SAVE_GLOBAL, TEAM_ANY, ACH_GAME_MODE_SINGLE, ACH_DLC_REQUIRED_BASE );

DECLARE_P3_END_MISSION_ACHIEVEMENT( CAchievementBaseEndMission, ACHIEVEMENT_P3_PWAC_FINISH, "ACH_PWAC_FINISH"
                                  , "pwac", GAMERSCORE_15, AWARD_ID_NONE, ACH_SAVE_GLOBAL, TEAM_ANY, ACH_GAME_MODE_SINGLE, ACH_DLC_REQUIRED_BASE );

DECLARE_P3_END_MISSION_ACHIEVEMENT( CAchievementBaseEndMission, ACHIEVEMENT_P3_SRM_FINISH, "ACH_SRM_FINISH"
                                  , "srm", GAMERSCORE_15, AWARD_ID_NONE, ACH_SAVE_GLOBAL, TEAM_ANY, ACH_GAME_MODE_SINGLE, ACH_DLC_REQUIRED_BASE );

DECLARE_P3_END_MISSION_ACHIEVEMENT( CAchievementBaseEndMission, ACHIEVEMENT_P3_ZHQA_FINISH, "ACH_ZHQA_FINISH"
                                  , "zhqa", GAMERSCORE_15, AWARD_ID_NONE, ACH_SAVE_GLOBAL, TEAM_ANY, ACH_GAME_MODE_SINGLE, ACH_DLC_REQUIRED_BASE );

DECLARE_P3_END_MISSION_ACHIEVEMENT( CAchievementBaseEndMission, ACHIEVEMENT_P3_DLG_FINISH, "ACH_DLG_FINISH"
                                  , "dlg", GAMERSCORE_15, AWARD_ID_NONE, ACH_SAVE_GLOBAL, TEAM_ANY, ACH_GAME_MODE_SINGLE, ACH_DLC_REQUIRED_BASE );

DECLARE_P3_END_MISSION_ACHIEVEMENT( CAchievementBaseEndMission, ACHIEVEMENT_P3_BDK_FINISH, "ACH_BDK_FINISH"
                                  , "bdk", GAMERSCORE_15, AWARD_ID_NONE, ACH_SAVE_GLOBAL, TEAM_ANY, ACH_GAME_MODE_SINGLE, ACH_DLC_REQUIRED_BASE );


#else 

//-----------------------------------------------------------------------------
// Purpose: This server-side system listens for high-frequency game events
//			that are important to achievement logic and sends either a more
//			concise version of the message or sends a lower frequency game
//			event that the client listens for.
//-----------------------------------------------------------------------------
class CAchievementMsgHandler : public CGameEventListener, public CAutoGameSystem
{
	DECLARE_CLASS_NOBASE( CAchievementMsgHandler );

public:
	CAchievementMsgHandler() : CAutoGameSystem( "CAchievementMsgHandler" ) {}

	void PostInit() 
	{
		ListenForGameEvent( "generic_weapon_attack" );
		ListenForGameEvent( "round_start_pre_entity" );
		ListenForGameEvent( "survival_round_start" );
		ListenForGameEvent( "player_hurt" );
	}

protected:

	void FireGameEvent( IGameEvent *event )
	{
		const char *eventName = event->GetName();
		if ( !eventName )
			return;

		if( !V_strcmp(eventName, "generic_weapon_attack") )
		{
			int iWeaponID = event->GetInt( "weapon_id" );
			int iUserID = event->GetInt( "userid" );
			int iWeaponType = event->GetInt( "weapon_type" );

			if ( ( -1 == m_usersSentNonMeleeThisRound.Find( iUserID ) ) && 
				( iWeaponType != WPN_TYPE_NONLETHAL_MELEE ) &&
				( iWeaponType != WPN_TYPE_MELEE ) )
			{
				// They fired a gun other than a pistol. Send the 'non_pistol_fired' event.
				IGameEvent * newEvent = gameeventmanager->CreateEvent( "non_melee_fired" );
				if( newEvent )
				{
					newEvent->SetInt( "userid", iUserID );
					gameeventmanager->FireEvent( newEvent );

					// We've sent it this round - that's enough
					m_usersSentNonMeleeThisRound.AddToTail( iUserID );
				}
			}

			if ( ( -1 == m_usersSentNonPistolThisRound.Find( iUserID ) ) && 
				( iWeaponID != P3_WEAPON_DESERT_EAGLE ) )
			{
				// They fired a gun other than a pistol. Send the 'non_pistol_fired' event.
				IGameEvent * newEvent = gameeventmanager->CreateEvent( "non_pistol_fired" );
				if( newEvent )
				{
					newEvent->SetInt( "userid", iUserID );
					gameeventmanager->FireEvent( newEvent );

					// We've sent it this round - that's enough
					m_usersSentNonPistolThisRound.AddToTail( iUserID );
				}
			}
		}
	}

	CUtlVector<int> m_usersSentNonPistolThisRound;
	CUtlVector<int> m_usersSentNonMeleeThisRound;
};

// Make one
CAchievementMsgHandler g_AchievementMsgHandler;

#endif // CLIENT_DLL

