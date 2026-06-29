#ifndef PLAYER_ANGLES_H
#define PLAYER_ANGLES_H

#include "p3_player_shared.h"

#if defined(CLIENT_DLL)// && defined(P3MP_DLL)
//#define ANIMATE_HERE
#else
#define ANIMATE_HERE
#endif // CLIENT_DLL

//-----------------------------------------------------------------------------
//         0
//         |            углы секторов прицеливания
//   70    |    290
// 90 -----+---- 270
//   135   |   255
//         |
//         |
//-----------------------------------------------------------------------------

enum CoverAimSector_t
{
	COVER_AIM_SECTOR_INVALID = -1,
	COVER_AIM_SECTOR_FRONT,			// -90 .. 90
	COVER_AIM_SECTOR_BACK_LEFT,		//  70 .. 225
	COVER_AIM_SECTOR_BACK_RIGHT,	// 290 .. 135
};

enum CoverPosition_t
{
	COVER_POSITION_INVALID = -1,
	COVER_POSITION_LEFT_HIGH,
	COVER_POSITION_LEFT_LOW,
	COVER_POSITION_RIGHT_HIGH,
	COVER_POSITION_RIGHT_LOW,
	COVER_POSITION_FRONT_LEFT_LOW,
	COVER_POSITION_FRONT_RIGHT_LOW,
	COVER_POSITION_BACK_LEFT_HIGH,
	COVER_POSITION_BACK_LEFT_LOW,
	COVER_POSITION_BACK_RIGHT_HIGH,
	COVER_POSITION_BACK_RIGHT_LOW,
};

enum CoverFacing_t
{
	COVER_FACING_INVALID = -1,
	COVER_FACING_LEFT,
	COVER_FACING_RIGHT
};


class CPlayerAngles
{
	friend class CP3_Player;

public:
	DECLARE_CLASS_NOBASE( CPlayerAngles );
	DECLARE_EMBEDDED_NETWORKVAR();
	DECLARE_PREDICTABLE();

#ifdef GAME_DLL
	DECLARE_DATADESC();
#endif

	static float PLAYER_YAW_MAX;
	static float PLAYER_HEADYAW_LIMIT;
	static float PLAYER_HEADYAW_MAX;
	static float PLAYER_HEADPITCH_MIN;
	static float PLAYER_HEADPITCH_MAX;
	static float PLAYER_BODYYAW_MAX;
	static float PLAYER_AIMYAW_MAX;

	static float PLAYER_YAW_SPEED;
	static float PLAYER_HEADYAW_SPEED;
	static float PLAYER_HEADPITCH_SPEED;
	static float PLAYER_BODYYAW_SPEED;
	static float PLAYER_AIM_SPEED;
	static float PLAYER_MOVEYAW_SPEED;

	static void InitVars();


public:
	CPlayerAngles();
	CPlayerAngles( CP3_Player *pPlayer );
	~CPlayerAngles();

	void				SetPlayer( CP3_Player *pPlayer );

	void				Reset();
	bool				IsEmpty();

	bool				IsPlayerAimingBack() const					{ return m_coverAimSector == COVER_AIM_SECTOR_BACK_LEFT || m_coverAimSector == COVER_AIM_SECTOR_BACK_RIGHT; }

	void				SetCameraOrigin( const Vector &origin )		{ m_vecCameraOrigin = origin; }
	void				SetCameraAngles( const QAngle &angles )		{ m_angCameraAngles = angles; }

#ifdef ANIMATE_HERE
	void				SetComplexYaw( float yaw );
#endif

	const Vector		&GetCameraOrigin() const					{ return m_vecCameraOrigin; }
	const QAngle		&GetCameraAngles() const					{ return m_angCameraAngles; }

	QAngle				GetViewAngles() const						{ return QAngle( 0, m_flYaw + m_flAimYaw, 0 ); }
	float				GetComplexYaw() const						{ return min( m_flHeadYaw, PLAYER_HEADYAW_LIMIT ) + m_flBodyYaw + m_flAimYaw; }
	float				GetBodyYaw() const							{ return m_flBodyYaw; }

	int					GetState() const
	{
		switch ( m_coverPosition )
		{
		case COVER_POSITION_FRONT_LEFT_LOW:
		case COVER_POSITION_FRONT_RIGHT_LOW:
			return P3_PLAYERSTATE_AIM_CENTER;
			break;

		case COVER_POSITION_BACK_LEFT_HIGH:
		case COVER_POSITION_BACK_LEFT_LOW:
		case COVER_POSITION_BACK_RIGHT_HIGH:
		case COVER_POSITION_BACK_RIGHT_LOW:
			return P3_PLAYERSTATE_AIM_BACK;
			break;
		}

		return 0;
	}

	void				SelectCoverAimSector( int flags, float yaw, CoverAimSector_t prevAimSector, CoverAimSector_t& newAimSector, CoverFacing_t& newFacing );
	void				SelectCoverPosition( int flags, CoverAimSector_t curAimSector, CoverPosition_t& newCover );

	Activity			SelectAttackActivity( Activity attackAct );
	Activity			SelectCoverActivity( Activity baseAct, bool is_aiming );

	CoverAimSector_t	GetCoverAimSector() const					{ return m_coverAimSector; }
	CoverPosition_t		GetCoverPosition() const					{ return m_coverPosition; }

	static float		CalcNextAngle( float curAngle, float goalAngle, float dt, float speed, float da = 0 );
	static float		FindNearestArcDir( float goalAngle, float curAngle ); // в какую сторону ближе двигаться

	void				CalcPlayerAngles();
#ifdef ANIMATE_HERE
	void				CalcPlayerAimYawAndPitch();
	void				CalcPlayerMoveYaw();
#endif
#ifndef CLIENT_DLL
	void				UpdateCoverAiming();
#endif

	void				UpdateAngles( float dt );

	void				UpdatePose( float dt );

	void				DrawDebugOverlay( bool server );


protected:
#ifdef ANIMATE_HERE
	void				InitPoseParameters();
	void				ReadPoseParameters();
	void				UpdatePoseParameters();
#endif

	int					m_aim_pitch;
	int					m_aim_yaw;
	int					m_aim_yaw_360;
	int					m_head_yaw;
	int					m_head_pitch;
	int					m_move_yaw;
	int					m_body_yaw;


private:
	CP3_Player			*m_pPlayer;

#ifdef CLIENT_DLL
	Vector				m_vecCameraOrigin;
	QAngle				m_angCameraAngles;

	// TODO: это все должно быть в каком-нибудь CoverManager/AimManager
	CoverAimSector_t	m_coverAimSector;
	CoverPosition_t		m_coverPosition;
#else
	CNetworkVector( m_vecCameraOrigin );
	CNetworkQAngle( m_angCameraAngles );

	// TODO: это все должно быть в каком-нибудь CoverManager/AimManager
	CNetworkVar( CoverAimSector_t, m_coverAimSector );
	CNetworkVar( CoverPosition_t, m_coverPosition );
#endif

	float				m_flYaw;
	float				m_flAimYaw;
	float				m_flAimPitch;
	float				m_flBodyYaw;
	float				m_flHeadYaw;
	float				m_flHeadPitch;
	float				m_flMoveYaw;

	float				m_flGoalYaw;
	float				m_flGoalAimYaw;
	float				m_flGoalAimPitch;
	float				m_flGoalBodyYaw;
	float				m_flGoalHeadYaw;
	float				m_flGoalHeadPitch;
	float				m_flGoalMoveYaw;
};

//-----------------------------------------------------------------------------
// For networking this bad boy
//-----------------------------------------------------------------------------
#ifdef CLIENT_DLL
EXTERN_RECV_TABLE( DT_PlayerAngles );
#else
EXTERN_SEND_TABLE( DT_PlayerAngles );
#endif


#endif // PALYER_ANGLES_H
