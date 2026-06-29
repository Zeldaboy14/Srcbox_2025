//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef Experiment_PLAYERANIMSTATE_H
#define Experiment_PLAYERANIMSTATE_H
#ifdef _WIN32
#pragma once
#endif

#include "convar.h"
#include "../Multiplayer/multiplayer_animstate.h"

#if defined( CLIENT_DLL )
class C_HL2MP_Player;
#define CHL2MP_Player C_HL2MP_Player
#else
class CHL2MP_Player;
#endif

// ------------------------------------------------------------------------------------------------ //
// CPlayerAnimState declaration.
// ------------------------------------------------------------------------------------------------ //
class CHL2MPPlayerAnimState : public CMultiPlayerAnimState
{
   public:
    DECLARE_CLASS( CHL2MPPlayerAnimState, CMultiPlayerAnimState );

    CHL2MPPlayerAnimState();
    CHL2MPPlayerAnimState( CBasePlayer *pPlayer, MultiPlayerMovementData_t &movementData );
    ~CHL2MPPlayerAnimState();

    void InitExperimentAnimState( CHL2MP_Player *pPlayer );
    CHL2MP_Player *GetExperimentPlayer( void )
    {
        return m_pExperimentPlayer;
    }

    virtual void ClearAnimationState();
    virtual Activity TranslateActivity( Activity actDesired );
    virtual void Update( float eyeYaw, float eyePitch );

    void DoAnimationEvent( PlayerAnimEvent_t event, int nData = 0 );
    virtual Activity CalcMainActivity();

   private:
    CHL2MP_Player *m_pExperimentPlayer;

    bool m_bFreshJump;
    bool m_bWasJumping;
};

CHL2MPPlayerAnimState *CreateExperimentPlayerAnimState( CHL2MP_Player *pPlayer );

#endif  // Experiment_PLAYERANIMSTATE_H
