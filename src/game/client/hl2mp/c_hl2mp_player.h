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

class C_HL2MP_Player;
#include "c_basehlplayer.h"
#include "hl2mp_player_shared.h"
#include "beamdraw.h"
#include "hl2mp_playeranimstate.h"

#ifdef SRCBOX
//#include "srcbox/tf/tf_item.h"
//#include "srcbox/tf/tf_shareddefs.h"
#endif

//=============================================================================
//=============================================================================
class CSuitPowerDevice
{
    public:
    CSuitPowerDevice( int bitsID, float flDrainRate )
    {
        m_bitsDeviceID = bitsID;
        m_flDrainRate = flDrainRate;
    }

    private:
    int m_bitsDeviceID;   // tells what the device is. DEVICE_SPRINT, DEVICE_FLASHLIGHT, etc. BITMASK!!!!!
    float m_flDrainRate;  // how quickly does this device deplete suit power? ( percent per second )

    public:
    int GetDeviceID( void ) const
    {
        return m_bitsDeviceID;
    }
    float GetDeviceDrainRate( void ) const
    {
        // if ( g_pGameRules->GetSkillLevel() == SKILL_EASY && hl2_episodic.GetBool() && !( GetDeviceID() & bits_SUIT_DEVICE_SPRINT ) )
        //	return m_flDrainRate * 0.5f;
        // else
        return m_flDrainRate;
    }
};

extern ConVar hl2_sprintspeed;

#ifdef SRCBOX_EXPERIMENT
//-----------------------------------------------------------------------------
// For entity_capture_flags to use when placed in the world
// NOTE: Inserting to most or all of the enums in this file will BREAK DEMOS -
// please add to the end instead.
//-----------------------------------------------------------------------------
enum ETFFlagType
{
    TF_FLAGTYPE_CTF = 0,
    TF_FLAGTYPE_ATTACK_DEFEND,
    TF_FLAGTYPE_TERRITORY_CONTROL,
    TF_FLAGTYPE_INVADE,
    TF_FLAGTYPE_RESOURCE_CONTROL,
    TF_FLAGTYPE_ROBOT_DESTRUCTION,
    TF_FLAGTYPE_PLAYER_DESTRUCTION

    //
    // ADD NEW ITEMS HERE TO AVOID BREAKING DEMOS
    //
};

//-----------------------------------------------------------------------------
// Items.
//-----------------------------------------------------------------------------
enum
{
    TF_ITEM_UNDEFINED = 0,
    TF_ITEM_CAPTURE_FLAG = (1 << 0),
    TF_ITEM_HEALTH_KIT = (1 << 1),
    TF_ITEM_ARMOR = (1 << 2),
    TF_ITEM_AMMO_PACK = (1 << 3),
    TF_ITEM_GRENADE_PACK = (1 << 4),

    //
    // ADD NEW ITEMS HERE TO AVOID BREAKING DEMOS
    //
};
#endif

//=============================================================================
// >> HL2MP_Player
//=============================================================================
class C_HL2MP_Player : public C_BaseHLPlayer
{
    public:
    DECLARE_CLASS( C_HL2MP_Player, C_BaseHLPlayer );

    DECLARE_CLIENTCLASS();
    DECLARE_PREDICTABLE();
    DECLARE_INTERPOLATION();

    C_HL2MP_Player();
    ~C_HL2MP_Player( void );

    void ClientThink( void );

    static C_HL2MP_Player *GetLocalHL2MPPlayer();

    virtual int DrawModel( int flags );
    virtual void AddEntity( void );

    QAngle GetAnimEyeAngles( void )
    {
        return m_angEyeAngles;
    }
    Vector GetAttackSpread( CBaseCombatWeapon *pWeapon, CBaseEntity *pTarget = NULL );

    // Should this object cast shadows?
    virtual ShadowType_t ShadowCastType( void );
    virtual C_BaseAnimating *BecomeRagdollOnClient();
    virtual const QAngle &GetRenderAngles();
    virtual bool ShouldDraw( void );
    virtual void OnDataChanged( DataUpdateType_t type );
    virtual float GetFOV( void );
    virtual CStudioHdr *OnNewModel( void );
    virtual void TraceAttack( const CTakeDamageInfo &info, const Vector &vecDir, trace_t *ptr, CDmgAccumulator *pAccumulator );
    virtual void ItemPreFrame( void );
    virtual void ItemPostFrame( void );
    virtual float GetMinFOV() const
    {
        return 5.0f;
    }
    virtual Vector GetAutoaimVector( float flDelta );
    virtual void NotifyShouldTransmit( ShouldTransmitState_t state );
    virtual void CreateLightEffects( void ) {}
    virtual bool ShouldReceiveProjectedTextures( int flags );
    virtual void PostDataUpdate( DataUpdateType_t updateType );
    virtual void PlayStepSound( Vector &vecOrigin, surfacedata_t *psurface, float fvol, bool force );
    virtual void PreThink( void );
    virtual void DoImpactEffect( trace_t &tr, int nDamageType );
    IRagdoll *GetRepresentativeRagdoll() const;
    virtual void CalcView( CViewSetup &view );
    virtual const QAngle &EyeAngles( void );

    void SuitPower_Update( void );
    bool SuitPower_Drain( float flPower );   // consume some of the suit's power.
    void SuitPower_Charge( float flPower );  // add suit power.
    void SuitPower_SetCharge( float flPower )
    {
        m_HL2Local.m_flSuitPower = flPower;
    }
    void SuitPower_Initialize( void );
    bool SuitPower_IsDeviceActive( const CSuitPowerDevice &device );
    bool SuitPower_AddDevice( const CSuitPowerDevice &device );
    bool SuitPower_RemoveDevice( const CSuitPowerDevice &device );
    bool SuitPower_ShouldRecharge( void );
    float SuitPower_GetCurrentPercentage( void )
    {
        return m_HL2Local.m_flSuitPower;
    }

    bool CanSprint( void );
    void StartSprinting( void );
    void StopSprinting( void );
    virtual void HandleSpeedChanges( CMoveData *mv ) OVERRIDE;
    virtual void ReduceTimers( CMoveData *mv ) OVERRIDE;
    void UpdateLookAt( void );
    void Initialize( void );
    int GetIDTarget() const;
    void UpdateIDTarget( void );
    void PrecacheFootStepSounds( void );
    const char *GetPlayerModelSoundPrefix( void );

    HL2MPPlayerState State_Get() const;

    // Walking
    void StartWalking( void );
    void StopWalking( void );
    bool IsWalking( void )
    {
        return m_fIsWalking;
    }

#ifdef SRCBOX_EXPERIMENT

    bool			HasItem(void) const;				// Currently can have only one item at a time.
    void			SetItem(C_TFItem* pItem);
    C_TFItem*       GetItem(void) const;
    bool			HasTheFlag(ETFFlagType exceptionTypes[] = NULL, int nNumExceptions = 0) const;
    virtual bool	IsAllowedToPickUpFlag(void) const;

	CNetworkHandle( C_TFItem, m_hItem );
#endif

#ifdef LUA_SDK
    // Avoiding players
    void SetAvoidPlayers(bool shouldAvoid);
    bool GetAvoidPlayers();

    virtual void UpdateClientSideAnimation();
    void DoAnimationEvent(PlayerAnimEvent_t event, int nData = 0);
    virtual void CalculateIKLocks(float currentTime);

    bool KeyDown(int buttonCode);

    static void RecvProxy_CycleLatch(const CRecvProxyData* pData, void* pStruct, void* pOut);

    virtual float GetServerIntendedCycle()
    {
        return m_flServerCycle;
    }
    virtual void SetServerIntendedCycle(float cycle)
    {
        m_flServerCycle = cycle;
    }

    bool IsAirborne() const
    {
        return (!(GetFlags() & FL_ONGROUND));
    }

    CHL2MPPlayerAnimState* GetAnimState() const
    {
        return m_PlayerAnimState;
    }

    EHANDLE GetRagdollEntity() const
    {
        return m_hRagdoll;
    }

    bool FlashlightIsOn() const
    {
        return IsEffectActive(EF_DIMLIGHT);
    }
#endif

#ifndef LUA_SDK
    virtual void PostThink( void );
#endif

    private:
    C_HL2MP_Player( const C_HL2MP_Player & );

#ifdef LUA_SDK
    CHL2MPPlayerAnimState* m_PlayerAnimState;
#else
    CHL2MPPlayerAnimState m_PlayerAnimState;
#endif

    QAngle m_angEyeAngles;

    CInterpolatedVar< QAngle > m_iv_angEyeAngles;

    EHANDLE m_hRagdoll;

    int m_headYawPoseParam;
    int m_headPitchPoseParam;
    float m_headYawMin;
    float m_headYawMax;
    float m_headPitchMin;
    float m_headPitchMax;

    bool m_isInit;
    Vector m_vLookAtTarget;

    float m_flLastBodyYaw;
    float m_flCurrentHeadYaw;
    float m_flCurrentHeadPitch;

    int m_iIDEntIndex;

    CountdownTimer m_blinkTimer;

    int m_iSpawnInterpCounter;
    int m_iSpawnInterpCounterCache;

    int m_iPlayerSoundType;

    void ReleaseFlashlight( void );
    Beam_t *m_pFlashlightBeam;

    CNetworkVar( HL2MPPlayerState, m_iPlayerState );

    bool m_fIsWalking = false;
#ifdef LUA_SDK
    int m_cycleLatch;  // The animation cycle goes out of sync very easily. Mostly from the player entering/exiting PVS. Server will frequently update us with a new one.
    float m_flServerCycle;
    bool m_bAvoidPlayers;
#endif
};

inline C_HL2MP_Player *ToHL2MPPlayer( CBaseEntity *pEntity )
{
    if ( !pEntity || !pEntity->IsPlayer() )
        return NULL;

    return dynamic_cast< C_HL2MP_Player * >( pEntity );
}

class C_HL2MPRagdoll : public C_BaseAnimatingOverlay
{
    public:
    DECLARE_CLASS( C_HL2MPRagdoll, C_BaseAnimatingOverlay );
    DECLARE_CLIENTCLASS();

    C_HL2MPRagdoll();
    ~C_HL2MPRagdoll();

    virtual void OnDataChanged( DataUpdateType_t type );

    int GetPlayerEntIndex() const;
    IRagdoll *GetIRagdoll() const;

    void ImpactTrace( trace_t *pTrace, int iDamageType, const char *pCustomImpactName );
    void UpdateOnRemove( void );
    virtual void SetupWeights( const matrix3x4_t *pBoneToWorld, int nFlexWeightCount, float *pFlexWeights, float *pFlexDelayedWeights );

#ifdef LUA_SDK
    C_BasePlayer* GetRagdollPlayer() const
    {
        return dynamic_cast<C_BasePlayer*>(m_hPlayer.Get());
    }
#endif

    private:
#ifdef LUA_SDK
    C_HL2MPRagdoll(const C_HL2MPRagdoll&)
    {
    }
#endif

    void Interp_Copy( C_BaseAnimatingOverlay *pDestinationEntity );
    void CreateHL2MPRagdoll( void );

    private:
    EHANDLE m_hPlayer;
    CNetworkVector( m_vecRagdollVelocity );
    CNetworkVector( m_vecRagdollOrigin );
};

#endif  // HL2MP_PLAYER_H
