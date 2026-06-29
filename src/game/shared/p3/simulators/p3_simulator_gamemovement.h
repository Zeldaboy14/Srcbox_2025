//========= Copyright © 1996-2005, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $Workfile:     $
// $Date:         $
// $NoKeywords: $
//=============================================================================//
#if !defined( P3_SIMULATOR_GAMEMOVEMENT_H )
#define P3_SIMULATOR_GAMEMOVEMENT_H
#ifdef _WIN32
#pragma once
#endif

#include "igamemovement.h"
#include "cmodel.h"
#include "tier0/vprof.h"
//#include "p3/simulators/p3_simulator.h"
#include "p3/simulators/p3_simulator_movehelper_server.h"
#include "ai_debug.h"


#define CTEXTURESMAX		512			// max number of textures loaded
#define CBTEXTURENAMEMAX	13			// only load first n chars of name

#define GAMEMOVEMENT_DUCK_TIME				1000.0f		// ms
#define GAMEMOVEMENT_JUMP_TIME				1000.0f		// ms approx - based on the 21 unit height jump
#define GAMEMOVEMENT_JUMP_HEIGHT			21.0f		// units
#define GAMEMOVEMENT_TIME_TO_UNDUCK			( TIME_TO_UNDUCK * 1000.0f )		// ms
#define GAMEMOVEMENT_TIME_TO_UNDUCK_INV		( GAMEMOVEMENT_DUCK_TIME - GAMEMOVEMENT_TIME_TO_UNDUCK )

struct surfacedata_t;


class CP3_SimulatorMoveData : public CMoveData
{
public:
	Vector m_vecWishDirection;
	QAngle	m_vecBodyAngles;	// Body view angles (local space)
};

enum ESimulatorMovementState
{
	StateInvalid,
	StateMoving,
	StateObstacle,
	StateStuck
};

class CP3_SimulatorGameMovement // : public IGameMovement
{
public:
	DECLARE_CLASS_NOBASE( CP3_SimulatorGameMovement );
	
	CP3_SimulatorGameMovement( void );
	virtual			~CP3_SimulatorGameMovement( void );

	virtual void	ProcessMovement( CP3_Simulator *pPlayer, CP3_SimulatorMoveData *pMove );

	virtual void	StartTrackPredictionErrors( CP3_Simulator *pPlayer );
	virtual void	FinishTrackPredictionErrors( CP3_Simulator *pPlayer );
	virtual void	DiffPrint( char const *fmt, ... );
	virtual const Vector&	GetPlayerMins( bool ducked ) const;
	virtual const Vector&	GetPlayerMaxs( bool ducked ) const;
	virtual const Vector&	GetPlayerViewOffset( bool ducked ) const;

// For sanity checking getting stuck on CMoveData::SetAbsOrigin
	virtual void			TracePlayerPrimitive( const Vector& start, const Vector& end, unsigned int fMask, int collisionGroup, trace_t& pm, int type = 1 ) const;	
#define BRUSH_ONLY true
	virtual unsigned int PlayerSolidMask( bool brushOnly = false ) const;	///< returns the solid mask for the given player, so bots can have a more-restrictive set
	CP3_Simulator		*player;
	CP3_SimulatorMoveData *GetMoveData() { return mv; }
	CP3_SimulatorMoveHelperServer *GetMoveHelper() { return &m_MoveHelper; }
	inline	int	GetMovementState() const { return m_nMovementState; }
	inline  trace_t	*GetLastTrace() { return &m_LastTrace; }
	inline  void	SetMovementCollisionGroup( int group ) { m_nMovementCollisionGroup = group; }
	virtual ESimulatorMovementState	MoveProbe( Vector &vecDestination, trace_t &trace ) const;
	void CategorizePosition( trace_t &pm );

	//void SetHull( const Vector &vecHullMins, const Vector &vecHullMaxs );

	virtual const Vector&	GetPlayerMins( void ) const; // uses local player
	virtual const Vector&	GetPlayerMaxs( void ) const; // uses local player

protected:

	Vector m_vecHullMins, m_vecHullMaxs;

	// Input/Output for this movement
	CP3_SimulatorMoveData		*mv;
	
	int				m_nOldWaterLevel;
	float			m_flWaterEntryTime;
	int				m_nOnLadder;

	Vector			m_vecForward;
	Vector			m_vecRight;
	Vector			m_vecUp;

	// Does most of the player movement logic.
	// Returns with origin, angles, and velocity modified in place.
	// were contacted during the move.
	virtual void	PlayerMove(	void );

	// Set ground data, etc.
	void			FinishMove( void );

	virtual float	CalcRoll( const QAngle &angles, const Vector &velocity, float rollangle, float rollspeed );

	virtual	void	DecayPunchAngle( void );

	virtual void	CheckWaterJump(void );

	virtual void	WaterMove( void );

	void			WaterJump( void );

	// Handles both ground friction and water friction
	void			Friction( void );

	virtual void	AirAccelerate( Vector& wishdir, float wishspeed, float accel );

	virtual void	AirMove( void );
	
	virtual bool	CanAccelerate();
	virtual void	Accelerate( Vector& wishdir, float wishspeed, float accel);

	// Only used by players.  Moves along the ground when player is a MOVETYPE_WALK.
	virtual void	WalkMove( void );
	
	// Handle MOVETYPE_WALK.
	virtual void	FullWalkMove();

	// Implement this if you want to know when the player collides during OnPlayerMove
	virtual void	OnTryPlayerMoveCollision( trace_t &tr ) {}
	
	void			StayOnGround( void );

	typedef enum
	{
		GROUND = 0,
		STUCK,
		LADDER
	} IntervalType_t;

	virtual int		GetCheckInterval( IntervalType_t type );

	// Useful for things that happen periodically. This lets things happen on the specified interval, but
	// spaces the events onto different frames for different players so they don't all hit their spikes
	// simultaneously.
	bool			CheckInterval( IntervalType_t type );


	// Decompoosed gravity
	void			StartGravity( void );
	void			FinishGravity( void );

	// Apply normal ( undecomposed ) gravity
	void			AddGravity( void );

	// Handle movement in noclip mode.
	void			FullNoClipMove( float factor, float maxacceleration );

	// Returns true if he started a jump (ie: should he play the jump animation)?
	virtual bool	CheckJumpButton( void );	// Overridden by each game.

	// Dead player flying through air., e.g.
	virtual void    FullTossMove( void );
	
	// Player is a Observer chasing another player
	void			FullObserverMove( void );

	// Handle movement when in MOVETYPE_LADDER mode.
	virtual void	FullLadderMove();

	// The basic solid body movement clip that slides along multiple planes
	virtual int		TryPlayerMove( Vector *pFirstDest=NULL, trace_t *pFirstTrace=NULL, trace_t *pOutStuck=NULL );
	virtual int		ProbePlayerMove( Vector *pFirstDest=NULL, trace_t *pFirstTrace=NULL, trace_t *pOutStuck=NULL ) const;
	
	virtual bool	LadderMove( void );
	virtual bool	OnLadder( trace_t &trace );
	virtual float	LadderDistance( void ) const { return 2.0f; }	///< Returns the distance a player can be from a ladder and still attach to it
	virtual unsigned int LadderMask( void ) const { return MASK_PLAYERSOLID; }
	virtual float	ClimbSpeed( void ) const { return MAX_CLIMB_SPEED; }
	virtual float	LadderLateralMultiplier( void ) const { return 1.0f; }

	// See if the player has a bogus velocity value.
	void			CheckVelocity( void );

	// Does not change the entities velocity at all
	void			PushEntity( Vector& push, trace_t *pTrace );

	// Slide off of the impacting object
	// returns the blocked flags:
	// 0x01 == floor
	// 0x02 == step / wall
	int				ClipVelocity( Vector& in, Vector& normal, Vector& out, float overbounce ) const;

	// If pmove.origin is in a solid position,
	// try nudging slightly on all axis to
	// allow for the cut precision of the net coordinates
#ifdef PORTAL
	virtual 
#endif
	int				CheckStuck( void );	
	
	// Check if the point is in water.
	// Sets refWaterLevel and refWaterType appropriately.
	// If in water, applies current to baseVelocity, and returns true.
	virtual bool			CheckWater( void );
	
	// Determine if player is in water, on ground, etc.
	virtual void CategorizePosition( void );

	virtual void	CheckParameters( void );

	virtual	void	ReduceTimers( void );

	virtual void	CheckFalling( void );

	virtual void	PlayerRoughLandingEffects( float fvol );

	void			PlayerWaterSounds( void );

	void ResetGetPointContentsCache();
	int GetPointContentsCached( const Vector &point, int slot );

	// Ducking
	virtual void	Duck( void );
	virtual void	HandleDuckingSpeedCrop();
	virtual void	FinishUnDuck( void );
	virtual void	FinishDuck( void );
	virtual bool	CanUnduck();
	void			UpdateDuckJumpEyeOffset( void );
	bool			CanUnDuckJump( trace_t &trace );
	void			StartUnDuckJump( void );
	void			FinishUnDuckJump( trace_t &trace );
	void			SetDuckedEyeOffset( float duckFraction );
	void			FixPlayerCrouchStuck( bool moveup );

	float			SplineFraction( float value, float scale );

	void			CategorizeGroundSurface( trace_t &pm );

	bool			InWater( void );

	// Commander view movement
	void			IsometricMove( void );

	// Traces the player bbox as it is swept from start to end
	virtual CBaseHandle		TestPlayerPosition( const Vector& pos, int collisionGroup, trace_t& pm );

	// Checks to see if we should actually jump 
	void			PlaySwimSound();

	bool			IsDead( void ) const;

	// Figures out how the constraint should slow us down
	float			ComputeConstraintSpeedFactor( void );

	virtual void	SetGroundEntity( trace_t *pm );

	virtual void	StepMove( Vector &vecDestination, trace_t &trace );
	
	// Performs the collision resolution for fliers.
	void			PerformFlyCollisionResolution( trace_t &pm, Vector &move );

	virtual bool	GameHasLadders() const;

	enum
	{
		// eyes, waist, feet points (since they are all deterministic
		MAX_PC_CACHE_SLOTS = 3,
	};

	// Cache used to remove redundant calls to GetPointContents().
	int m_CachedGetPointContents[ MAX_PC_CACHE_SLOTS ];
	Vector m_CachedGetPointContentsPoint[ MAX_PC_CACHE_SLOTS ];	

	Vector			m_vecProximityMins;		// Used to be globals in sv_user.cpp.
	Vector			m_vecProximityMaxs;

	float			m_fFrameTime;

//private:
	bool			m_bSpeedCropped;
	//bool			m_bStuck;
	ESimulatorMovementState m_nMovementState;
	trace_t			m_LastTrace;

	float			m_flStuckCheckTime[2]; // Last time we did a full test	

	// special function for teleport-with-duck for episodic
#ifdef HL2_EPISODIC
public:
	void			ForceDuck( void );

#endif
	CP3_SimulatorMoveHelperServer m_MoveHelper;
	int				m_nMovementCollisionGroup;	
	Vector			m_vecLastImpactNotificationPosition;

	void			AddDebugValue( int val );
};

extern ConVar p3_simulator_use_sphere;

//-----------------------------------------------------------------------------
// Traces player movement + position
// type 0 - box trace, 1 - sphere trace
//-----------------------------------------------------------------------------
//inline void CP3_SimulatorGameMovement::TracePlayerPrimitive( const Vector& start, const Vector& end, unsigned int fMask, int collisionGroup, trace_t& pm, int type )
//{
//	VPROF( "CP3_SimulatorGameMovement::TracePlayerPrimitive" );
//
//	Ray_t ray;
//	ray.Init( start, end, GetPlayerMins(), GetPlayerMaxs(), p3_simulator_use_sphere.GetBool() );
//	UTIL_TraceRay( ray, fMask, mv->m_nPlayerHandle.Get(), collisionGroup, &pm );
//}

class CTraceFilterIgnoreNPC : public CTraceFilterSimple
{
public:
	DECLARE_CLASS( CTraceFilterIgnoreNPC, CTraceFilterSimple );

	CTraceFilterIgnoreNPC( CBaseEntity *pMe, int collisionGroup )
		: CTraceFilterSimple( NULL, collisionGroup )
	{
		m_pMe = pMe;
	}

	virtual bool ShouldHitEntity( IHandleEntity *pHandleEntity, int contentsMask );

protected:
	CBaseEntity *m_pMe; 
};

inline void CP3_SimulatorGameMovement::TracePlayerPrimitive( const Vector& start, const Vector& end, unsigned int fMask, int collisionGroup, trace_t& pm, int type ) const
{
	AI_PROFILE_SCOPE( "CP3_SimulatorGameMovement::TracePlayerPrimitive" );

	Ray_t ray;
	ray.Init( start, end, GetPlayerMins(), GetPlayerMaxs(), p3_simulator_use_sphere.GetBool() );	
	
	CTraceFilterIgnoreNPC filter( EntityFromEntityHandle( mv->m_nPlayerHandle.Get() ), collisionGroup );
	
	enginetrace->TraceRay( ray, fMask, &filter, &pm );
	
	if( r_visualizetraces.GetBool() )
	{
		DebugDrawLine( start, end, 255, 255, 255, true, -1.0f );
	}
}

#endif // P3_SIMULATOR_GAMEMOVEMENT_H
