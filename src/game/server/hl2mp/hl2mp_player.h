//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//
//=============================================================================//
#ifndef HL2MP_PLAYER_H
#define HL2MP_PLAYER_H
#pragma once

class CHL2MP_Player;

#include "basemultiplayerplayer.h"
#include "hl2_playerlocaldata.h"
#include "hl2_player.h"
#include "simtimer.h"
#include "soundenvelope.h"
#include "hl2mp_player_shared.h"
#include "hl2mp_gamerules.h"
#include "utldict.h"
#include "Multiplayer\multiplayer_animstate.h"
#ifdef LUA_SDK
#include "lsingleluainstance.h"
#include "hl2mp_playeranimstate.h"
#endif

//=============================================================================
// >> HL2MP_Player
//=============================================================================
class CHL2MPPlayerStateInfo
{
public:
	HL2MPPlayerState m_iPlayerState;
	const char* m_pStateName;

	void (CHL2MP_Player::* pfnEnterState)();	// Init and deinit the state.
	void (CHL2MP_Player::* pfnLeaveState)();

	void (CHL2MP_Player::* pfnPreThink)();	// Do a PreThink() in this state.
};

class CHL2MP_Player : public CHL2_Player
{
#ifdef LUA_SDK
	LUA_OVERRIDE_SINGLE_LUA_INSTANCE_METATABLE(CHL2MP_Player, LUA_EXPERIMENTPLAYERLIBNAME);
#endif

public:
	DECLARE_CLASS( CHL2MP_Player, CHL2_Player );

	CHL2MP_Player();
	~CHL2MP_Player( void );
	
	static CHL2MP_Player *CreatePlayer( const char *className, edict_t *ed )
	{
		CHL2MP_Player::s_PlayerEdict = ed;
		return (CHL2MP_Player*)CreateEntityByName( className );
	}

	DECLARE_SERVERCLASS();
	DECLARE_DATADESC();
	DECLARE_ENT_SCRIPTDESC();

#ifdef LUA_SDK
	// This passes the event to the client's and server's CHL2MPPlayerAnimState.
	void			DoAnimationEvent(PlayerAnimEvent_t event, int nData = 0);
	void SetupBones(matrix3x4_t* pBoneToWorld, int boneMask);
	bool KeyDown(int buttonCode);

	// Avoiding players
	void SetAvoidPlayers(bool shouldAvoid);
	bool GetAvoidPlayers();
#endif

	virtual void Precache( void );
	virtual void Spawn( void );
	virtual void PostThink( void );
	virtual void PreThink( void );
	virtual void PlayerDeathThink( void );
	virtual void SetAnimation( PLAYER_ANIM playerAnim );
	virtual bool HandleCommand_JoinTeam( int team );
	virtual bool ClientCommand( const CCommand &args );
	virtual void CreateViewModel( int viewmodelindex = 0 );
	virtual bool BecomeRagdollOnClient( const Vector &force );
	virtual void Event_Killed( const CTakeDamageInfo &info );
	virtual int OnTakeDamage( const CTakeDamageInfo &inputInfo );
	virtual bool WantsLagCompensationOnEntity( const CBasePlayer *pPlayer, const CUserCmd *pCmd, const CBitVec<MAX_EDICTS> *pEntityTransmitBits ) const;
	virtual void FireBullets ( const FireBulletsInfo_t &info );
	virtual void OnMyWeaponFired( CBaseCombatWeapon* weapon );
	virtual bool Weapon_Switch( CBaseCombatWeapon *pWeapon, int viewmodelindex = 0);
	virtual bool BumpWeapon( CBaseCombatWeapon *pWeapon );
	virtual void ChangeTeam( int iTeam ) OVERRIDE;
	virtual void PickupObject ( CBaseEntity *pObject, bool bLimitMassAndSize );
	virtual void PlayStepSound( Vector &vecOrigin, surfacedata_t *psurface, float fvol, bool force );
	virtual void Weapon_Drop( CBaseCombatWeapon *pWeapon, const Vector *pvecTarget = NULL, const Vector *pVelocity = NULL );
	virtual void UpdateOnRemove( void );
	virtual void DeathSound( const CTakeDamageInfo &info );
	virtual CBaseEntity* EntSelectSpawnPoint( void );
		
	int FlashlightIsOn( void );
	void FlashlightTurnOn( void );
	void FlashlightTurnOff( void );
	void	PrecacheFootStepSounds( void );
	bool	ValidatePlayerModel( const char *pModel );

	QAngle GetAnimEyeAngles( void ) { return m_angEyeAngles.Get(); }

	Vector GetAttackSpread( CBaseCombatWeapon *pWeapon, CBaseEntity *pTarget = NULL );

	void CheatImpulseCommands( int iImpulse );
	void CreateRagdollEntity( void );
	void GiveAllItems( void );
	void GiveDefaultItems( void );

	void NoteWeaponFired( void );

	void ResetAnimation( void );
	void SetPlayerModel( void );
	void SetPlayerTeamModel( void );
	Activity TranslateTeamActivity( Activity ActToTranslate );
	
	float GetNextModelChangeTime( void ) { return m_flNextModelChangeTime; }
	float GetNextTeamChangeTime( void ) { return m_flNextTeamChangeTime; }
	void  PickDefaultSpawnTeam( void );
	void  SetupPlayerSoundsByModel( const char *pModelName );
	const char *GetPlayerModelSoundPrefix( void );
	int	  GetPlayerModelType( void ) { return m_iPlayerSoundType;	}

	int	GetMaxAmmo( int iAmmoIndex ) const;
	
	void  DetonateTripmines( void );

	void Reset();

	bool IsReady();
	void SetReady( bool bReady );

	void CheckChatText( char *p, int bufsize );

	void State_Transition( HL2MPPlayerState newState );
	void State_Enter( HL2MPPlayerState newState );
	void State_Leave();
	void State_PreThink();
	CHL2MPPlayerStateInfo *State_LookupInfo( HL2MPPlayerState state );

	void State_Enter_ACTIVE();
	void State_PreThink_ACTIVE();
	void State_Enter_OBSERVER_MODE();
	void State_PreThink_OBSERVER_MODE();


	virtual bool StartObserverMode( int mode );
	virtual void StopObserverMode( void );


	Vector m_vecTotalBulletForce;	//Accumulator for bullet force in a single frame

	// Tracks our ragdoll entity.
	CNetworkHandle( CBaseEntity, m_hRagdoll );	// networked entity handle 

	virtual bool	CanHearAndReadChatFrom( CBasePlayer *pPlayer );

#ifdef LUA_SDK
	
    bool IsAirborne() const
    {
        return ( !( GetFlags() & FL_ONGROUND ) );
    }

    CHL2MPPlayerAnimState *GetAnimState() const
    {
        return m_PlayerAnimState;
    }

    CBaseEntity *GetRagdollEntity() const
    {
        return m_hRagdoll;
    }

    int GetMaxArmor() const
    {
        return m_MaxArmorValue;
    }
    void SetMaxArmor( int maxArmor )
    {
        m_MaxArmorValue.GetForModify() = maxArmor;
    }
#endif

	bool IsThreatAimingTowardMe( CBaseEntity* threat, float cosTolerance = 0.8f ) const;
	bool IsThreatFiringAtMe( CBaseEntity* threat ) const;
private:

	CNetworkQAngle( m_angEyeAngles );
#ifdef LUA_SDK
	CHL2MPPlayerAnimState* m_PlayerAnimState;
#else
	CPlayerAnimState   m_PlayerAnimState;
#endif

	int m_iLastWeaponFireUsercmd;
	int m_iModelType;
	CNetworkVar( int, m_iSpawnInterpCounter );
	CNetworkVar( int, m_iPlayerSoundType );

	float m_flNextModelChangeTime;
	float m_flNextTeamChangeTime;

	float m_flSlamProtectTime;	

	HL2MPPlayerState m_iPlayerState;
	CHL2MPPlayerStateInfo *m_pCurStateInfo;

	bool ShouldRunRateLimitedCommand( const CCommand &args );

	// This lets us rate limit the commands the players can execute so they don't overflow things like reliable buffers.
	CUtlDict<float,int>	m_RateLimitLastCommandTimes;

    bool m_bEnterObserver;
	bool m_bReady;

	CNetworkVar(bool, m_bAvoidPlayers);

	CNetworkVar(int, m_MaxArmorValue);
};

inline CHL2MP_Player *ToHL2MPPlayer( CBaseEntity *pEntity )
{
	if ( !pEntity || !pEntity->IsPlayer() )
		return NULL;

	return dynamic_cast<CHL2MP_Player*>( pEntity );
}

#ifdef LUA_SDK
class CHL2MPRagdoll : public CBaseAnimatingOverlay
{
public:
	DECLARE_CLASS(CHL2MPRagdoll, CBaseAnimatingOverlay);
	DECLARE_SERVERCLASS();

	// Transmit ragdolls to everyone.
	virtual int UpdateTransmitState()
	{
		return SetTransmitState(FL_EDICT_ALWAYS);
	}

public:
	// In case the client has the player entity, we transmit the player index.
	// In case the client doesn't have it, we transmit the player's model index, origin, and angles
	// so they can create a ragdoll in the right place.
	CNetworkHandle(CBaseEntity, m_hPlayer);	// networked entity handle 
	CNetworkVector(m_vecRagdollVelocity);
	CNetworkVector(m_vecRagdollOrigin);

	CBasePlayer* GetRagdollPlayer() const
	{
		return dynamic_cast<CBasePlayer*>(m_hPlayer.Get());
	}
};
#endif

#endif //HL2MP_PLAYER_H
