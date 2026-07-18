//========= Copyright Valve Corporation, All rights reserved. ============//
//
// The copyright to the contents herein is the property of Valve, L.L.C.
// The contents may be used and/or copied only with the written permission of
// Valve, L.L.C., or in accordance with the terms and conditions stipulated in
// the agreement/contract under which the contents have been supplied.
//
// Purpose: The TF Game rules object
//
// $Workfile:     $
// $Date:         $
// $NoKeywords: $
//=============================================================================

#ifndef TF_GAMERULES_H
#define TF_GAMERULES_H

#ifdef _WIN32
#pragma once
#endif


//#include "teamplayroundbased_gamerules.h"
//#include "convar.h"
//#include "gamevars_shared.h"
//#include "GameEventListener.h"

#ifdef CLIENT_DLL
//#include "c_player.h"
#else
#include "player.h"
//#include "entity_soldier_statue.h"
#endif

//-----------------------------------------------------------------------------
// For the game rules to determine which type of game we're playing
//
// NOTE: Inserting to most or all of the enums in this file will BREAK DEMOS -
// please add to the end instead.
//-----------------------------------------------------------------------------
enum ETFGameType
{
	TF_GAMETYPE_UNDEFINED = 0,
	TF_GAMETYPE_CTF,
	TF_GAMETYPE_CP,
	TF_GAMETYPE_ESCORT,
	TF_GAMETYPE_ARENA,
	TF_GAMETYPE_MVM,
	TF_GAMETYPE_RD,
	TF_GAMETYPE_PASSTIME,
	TF_GAMETYPE_PD,

	//
	// ADD NEW ITEMS HERE TO AVOID BREAKING DEMOS
	//
	TF_GAMETYPE_COUNT
};

class CSrcboxGameRules : public CGameRulesProxy
{
public:
	DECLARE_CLASS(CSrcboxGameRules, CGameRulesProxy);

	CSrcboxGameRules();

	virtual void Activate();

	CNetworkVar(ETFGameType, m_nGameType); // Type of game this map is (CTF, CP)
	CNetworkVar(int, m_nStopWatchState);
	CNetworkVar(float, m_flCapturePointEnableTime);
	CNetworkVar(int, m_iGlobalAttributeCacheVersion)
};

#ifndef CLIENT_DLL
/*
class CKothLogic : public CPointEntity
{
	DECLARE_CLASS(CKothLogic, CPointEntity);
public:
	DECLARE_DATADESC();

	CKothLogic()
	{
		m_nTimerInitialLength = 180; // seconds
		m_nTimeToUnlockPoint = 30; // seconds

		m_hRedTimer = NULL;
		m_hBlueTimer = NULL;
	}
	virtual int UpdateTransmitState()
	{
		return SetTransmitState(FL_EDICT_ALWAYS);
	}

	int GetInitialTimerLength(void) { return m_nTimerInitialLength; }
	int GetTimerToUnlockPoint(void) { return m_nTimeToUnlockPoint; }

	void InputRoundSpawn(inputdata_t& inputdata);
	void InputRoundActivate(inputdata_t& inputdata);
	void InputSetRedTimer(inputdata_t& inputdata);
	void InputSetBlueTimer(inputdata_t& inputdata);
	void InputAddRedTimer(inputdata_t& inputdata);
	void InputAddBlueTimer(inputdata_t& inputdata);

private:
	int m_nTimerInitialLength;
	int m_nTimeToUnlockPoint;

	CHandle< CTeamRoundTimer > m_hRedTimer;
	CHandle< CTeamRoundTimer > m_hBlueTimer;
};

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
class CCompetitiveLogic : public CPointEntity
{
	DECLARE_CLASS(CCompetitiveLogic, CPointEntity);
public:
	DECLARE_DATADESC();

	void OnSpawnRoomDoorsShouldLock(void);
	void OnSpawnRoomDoorsShouldUnlock(void);

	COutputEvent	m_OnSpawnRoomDoorsShouldLock;
	COutputEvent	m_OnSpawnRoomDoorsShouldUnlock;
};

class CHybridMap_CTF_CP : public CPointEntity
{
	DECLARE_CLASS(CHybridMap_CTF_CP, CPointEntity);
public:

	virtual int UpdateTransmitState()
	{
		return SetTransmitState(FL_EDICT_ALWAYS);
	}
};

#define CP_TIMER_THINK "CCPTimerLogicThink"
class CCPTimerLogic : public CPointEntity
{
	DECLARE_CLASS(CCPTimerLogic, CPointEntity);
public:
	DECLARE_DATADESC();

	CCPTimerLogic()
	{
		m_nTimerLength = 60; // seconds
		m_iszControlPointName = NULL_STRING;
		m_hControlPoint = NULL;
		m_bFire15SecRemain = m_bFire10SecRemain = m_bFire5SecRemain = true;

		SetContextThink(&CCPTimerLogic::Think, gpGlobals->curtime + 0.15, CP_TIMER_THINK);
	}
	virtual int UpdateTransmitState()
	{
		return SetTransmitState(FL_EDICT_ALWAYS);
	}

	void InputRoundSpawn(inputdata_t& inputdata);
	void Think(void);
	bool TimerMayExpire(void);

private:
	int m_nTimerLength;
	string_t m_iszControlPointName;
	CHandle<CTeamControlPoint> m_hControlPoint;
	CountdownTimer m_pointTimer;

	bool m_bFire15SecRemain;
	bool m_bFire10SecRemain;
	bool m_bFire5SecRemain;

	COutputEvent m_onCountdownStart;
	COutputEvent m_onCountdown15SecRemain;
	COutputEvent m_onCountdown10SecRemain;
	COutputEvent m_onCountdown5SecRemain;
	COutputEvent m_onCountdownEnd;

	//int m_nTimerTeam = TF_TEAM_BLUE;
};*/
#endif

#endif // TF_GAMERULES_H
