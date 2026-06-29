#include "cbase.h"

#include "p3_player_angles.h"
#include "p3_cvars_shared.h"
#include "p3/Weapons/p3_weapon_shared.h"

#ifdef CLIENT_DLL
#include "p3/p3_c_player.h"
#include "p3/simulators/p3_c_simulator.h"
#include "p3/Weapons/p3_c_base_weapon.h"
#else
#include "p3/p3_player.h"
#include "p3/simulators/p3_simulator.h"
#include "p3/Weapons/p3_base_weapon.h"
#endif
#include "p3/input/p3_input_mode_shared.h"
#include "debugoverlay_shared.h"
#include "tier0/valve_minmax_on.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"


ConVar p3_player_anglesdebug("p3_player_anglesdebug", "0", FCVAR_CHEAT | FCVAR_REPLICATED );


float CPlayerAngles::PLAYER_YAW_MAX;
float CPlayerAngles::PLAYER_HEADYAW_LIMIT;
float CPlayerAngles::PLAYER_HEADYAW_MAX;
float CPlayerAngles::PLAYER_HEADPITCH_MIN;
float CPlayerAngles::PLAYER_HEADPITCH_MAX;
float CPlayerAngles::PLAYER_BODYYAW_MAX;
float CPlayerAngles::PLAYER_AIMYAW_MAX;

float CPlayerAngles::PLAYER_YAW_SPEED;
float CPlayerAngles::PLAYER_HEADYAW_SPEED;
float CPlayerAngles::PLAYER_HEADPITCH_SPEED;
float CPlayerAngles::PLAYER_BODYYAW_SPEED;
float CPlayerAngles::PLAYER_AIM_SPEED;
float CPlayerAngles::PLAYER_MOVEYAW_SPEED;

void CPlayerAngles::InitVars()
{
	PLAYER_YAW_MAX = p3_player_yaw_max.GetFloat();
	PLAYER_HEADYAW_LIMIT = p3_player_head_yaw_limit.GetFloat();
	PLAYER_HEADYAW_MAX = p3_player_head_yaw_max.GetFloat();
	PLAYER_HEADPITCH_MIN = p3_player_head_pitch_min.GetFloat();
	PLAYER_HEADPITCH_MAX = p3_player_head_pitch_max.GetFloat();
	PLAYER_BODYYAW_MAX = p3_player_body_yaw_max.GetFloat();
	PLAYER_AIMYAW_MAX = p3_player_aim_yaw_max.GetFloat();

	PLAYER_YAW_SPEED = p3_player_yaw_speed.GetFloat();
	PLAYER_HEADYAW_SPEED = p3_player_head_yaw_speed.GetFloat();
	PLAYER_HEADPITCH_SPEED = p3_player_head_pitch_speed.GetFloat();
	PLAYER_BODYYAW_SPEED = p3_player_body_yaw_speed.GetFloat();
	PLAYER_AIM_SPEED = p3_player_aim_speed.GetFloat();
	PLAYER_MOVEYAW_SPEED = p3_player_move_yaw_speed.GetFloat();
}


#ifndef CLIENT_DLL

//-----------------------------------------------------------------------------
// Save/Load
//-----------------------------------------------------------------------------
BEGIN_DATADESC_NO_BASE( CPlayerAngles )

	DEFINE_FIELD( m_vecCameraOrigin, FIELD_VECTOR ),
	DEFINE_FIELD( m_angCameraAngles, FIELD_VECTOR ),

END_DATADESC()

#else

//-----------------------------------------------------------------------------
// Prediction
//-----------------------------------------------------------------------------
BEGIN_PREDICTION_DATA_NO_BASE( CPlayerAngles )

	DEFINE_PRED_FIELD( m_vecCameraOrigin, FIELD_VECTOR, FTYPEDESC_INSENDTABLE ),
	DEFINE_PRED_FIELD( m_angCameraAngles, FIELD_VECTOR, FTYPEDESC_INSENDTABLE ),

END_PREDICTION_DATA()

#endif

BEGIN_NETWORK_TABLE_NOBASE( CPlayerAngles, DT_PlayerAngles )

#ifdef CLIENT_DLL
	RecvPropVector( RECVINFO( m_vecCameraOrigin ), 0 ),
	RecvPropQAngles( RECVINFO( m_angCameraAngles ), 0 ),
	RecvPropInt( RECVINFO( m_coverAimSector ), 0 ),
	RecvPropInt( RECVINFO( m_coverPosition ), 0 ),
#else
	SendPropVector( SENDINFO( m_vecCameraOrigin ), 0, SPROP_COORD | SPROP_CHANGES_OFTEN ),
	SendPropQAngles( SENDINFO( m_angCameraAngles ), 0, SPROP_COORD | SPROP_CHANGES_OFTEN ),
	SendPropInt( SENDINFO( m_coverAimSector ), 0 ),
	SendPropInt( SENDINFO( m_coverPosition ), 0 ),
#endif

END_NETWORK_TABLE()

CPlayerAngles::CPlayerAngles()
	: m_pPlayer( NULL )
{
	m_vecCameraOrigin.Init();
	m_angCameraAngles.Init();

	InitVars();
}

CPlayerAngles::CPlayerAngles( CP3_Player *pPlayer )
	: m_pPlayer( pPlayer )
	, m_vecCameraOrigin()
	, m_angCameraAngles()
	, m_flYaw( 0 )
	, m_flAimYaw( 0 )
	, m_flAimPitch( 0 )
	, m_flBodyYaw( 0 )
	, m_flHeadYaw( 0 )
	, m_flHeadPitch( 0 )
	, m_flMoveYaw( 0 )
	, m_flGoalYaw( 0 )
	, m_flGoalAimYaw( 0 )
	, m_flGoalAimPitch( 0 )
	, m_flGoalBodyYaw( 0 )
	, m_flGoalHeadYaw( 0 )
	, m_flGoalHeadPitch( 0 )
	, m_flGoalMoveYaw( 0 )
	, m_coverAimSector( COVER_AIM_SECTOR_INVALID )
	, m_coverPosition( COVER_POSITION_INVALID )
{
	m_vecCameraOrigin.Init();
	m_angCameraAngles.Init();

	InitVars();
}

CPlayerAngles::~CPlayerAngles()
{
}

void
CPlayerAngles::SetPlayer( CP3_Player *pPlayer )
{
	m_pPlayer = pPlayer;
}

void
CPlayerAngles::Reset()
{
#ifdef ANIMATE_HERE
	InitPoseParameters();
	ReadPoseParameters();
#endif

	m_flGoalYaw = 0;
	m_flGoalHeadYaw = 0;
	m_flGoalHeadPitch = 0;
	m_flGoalBodyYaw = 0;
	m_flGoalAimYaw = 0;
	m_flGoalAimPitch = 0;
	m_flGoalMoveYaw = 0;

	m_coverAimSector = COVER_AIM_SECTOR_INVALID;
	m_coverPosition = COVER_POSITION_INVALID;
}

bool
CPlayerAngles::IsEmpty()
{
	return m_coverAimSector == COVER_AIM_SECTOR_INVALID
		|| m_coverPosition == COVER_POSITION_INVALID;
}

#ifdef ANIMATE_HERE
void
CPlayerAngles::SetComplexYaw( float yaw )
{
	float yawSign = Sign( yaw );
	float yawAbs = fabsf( yaw );

	if ( m_pPlayer->IsDriving() )
	{
		// просто yaw
		m_flGoalHeadYaw = m_flGoalBodyYaw = 0;
		m_flGoalAimYaw = yaw;
	}
	else if ( m_pPlayer->IsCovering() )
	{
		// просто yaw
		m_flGoalHeadYaw = m_flGoalBodyYaw = 0;
		m_flGoalAimYaw = yaw;
	}
	else if ( m_pPlayer->IsAiming() )
	{
		m_flGoalHeadYaw = 0;
		m_flGoalAimYaw = yawSign * min( yawAbs, PLAYER_AIMYAW_MAX );

		if ( yawAbs > PLAYER_AIMYAW_MAX )
		{
			float bodyYaw = yawAbs - PLAYER_AIMYAW_MAX;
			m_flGoalBodyYaw = yawSign * min( bodyYaw, PLAYER_BODYYAW_MAX );
		}
		else
		{
			m_flGoalBodyYaw = 0;
		}
	}
	else if ( m_pPlayer->m_bPlayingSpecialAnimation )
	{
		m_flGoalHeadYaw = m_flGoalBodyYaw = 0;
	}
	else
	{
		m_flGoalAimYaw = m_flGoalAimPitch = 0;
		m_flGoalHeadYaw = yawSign * min( yawAbs, PLAYER_HEADYAW_MAX );

		if ( yawAbs > PLAYER_HEADYAW_LIMIT )
		{
			float bodyYaw = yawAbs - PLAYER_HEADYAW_LIMIT;
			m_flGoalBodyYaw = yawSign * min( bodyYaw, PLAYER_BODYYAW_MAX );
		}
		else
		{
			m_flGoalBodyYaw = 0;
		}
	}
}
#endif

void CPlayerAngles::SelectCoverAimSector( int flags, float yaw, CoverAimSector_t prevCoverAimSector,
												CoverAimSector_t& newCoverAimSector, CoverFacing_t& newCoverFacing )
{
	newCoverAimSector = prevCoverAimSector;

	if ( prevCoverAimSector == COVER_AIM_SECTOR_INVALID )
	{
		// выбираем начальное состояние
		if ( yaw > 90 && yaw < 180 )
		{
			newCoverAimSector = COVER_AIM_SECTOR_BACK_LEFT;
		}
		else if ( yaw >= 180 && yaw < 270 )
		{
			newCoverAimSector = COVER_AIM_SECTOR_BACK_RIGHT;
		}
		else
		{
			newCoverAimSector = COVER_AIM_SECTOR_FRONT;
		}
	}
	else
	{
		float backLeftYawLimit;
		float backRightYawLimit;

		if ( P3_CHECK_FLAGS( flags, P3_PLAYERSTATE_AIMING ) )
		{
			backLeftYawLimit = 70;
			backRightYawLimit = 290;
		}
		else
		{
			backLeftYawLimit = 90;
			backRightYawLimit = 270;
		}

		if ( prevCoverAimSector == COVER_AIM_SECTOR_BACK_LEFT )
		{
			if ( yaw <= backLeftYawLimit )
			{
				newCoverAimSector = COVER_AIM_SECTOR_FRONT;
			}
			else if ( (yaw >= 225) || (flags & P3_PLAYERSTATE_FACERIGHT) )
			{
				if ( !P3_CHECK_FLAGS( flags, P3_PLAYERSTATE_MOVING ) )
				{
					newCoverAimSector = COVER_AIM_SECTOR_BACK_RIGHT;
					newCoverFacing = COVER_FACING_RIGHT;
				}
			}
			else
			{
				newCoverAimSector = COVER_AIM_SECTOR_BACK_LEFT;
			}
		}
		else if ( prevCoverAimSector == COVER_AIM_SECTOR_BACK_RIGHT )
		{
			if ( yaw >= backRightYawLimit )
			{
				newCoverAimSector = COVER_AIM_SECTOR_FRONT;
			}
			else if ( (yaw <= 135) || (flags & P3_PLAYERSTATE_FACELEFT) )
			{
				if ( !P3_CHECK_FLAGS( flags, P3_PLAYERSTATE_MOVING ) )
				{
					newCoverAimSector = COVER_AIM_SECTOR_BACK_LEFT;
					newCoverFacing = COVER_FACING_LEFT;
				}
			}
			else
			{
				newCoverAimSector = COVER_AIM_SECTOR_BACK_RIGHT;
			}
		}
		else // COVER_AIM_SECTOR_FRONT
		{
			if ( yaw > 90 && yaw < 180 )
			{
				newCoverAimSector = COVER_AIM_SECTOR_BACK_LEFT;
				newCoverFacing = COVER_FACING_LEFT;
			}
			else if ( yaw >= 180 && yaw < 270 )
			{
				newCoverAimSector = COVER_AIM_SECTOR_BACK_RIGHT;
				newCoverFacing = COVER_FACING_RIGHT;
			}
		}
	}
}

void CPlayerAngles::SelectCoverPosition( int flags, CoverAimSector_t curAimSector, CoverPosition_t& newCoverPosition )
{
	newCoverPosition = COVER_POSITION_INVALID;

	if ( flags & P3_PLAYERSTATE_DUCK )
	{
		if ( curAimSector == COVER_AIM_SECTOR_BACK_LEFT )
		{
			newCoverPosition = COVER_POSITION_BACK_LEFT_LOW;
		}
		else if ( curAimSector == COVER_AIM_SECTOR_BACK_RIGHT )
		{
			newCoverPosition = COVER_POSITION_BACK_RIGHT_LOW;
		}
		else
		{
			if ( P3_CHECK_FLAGS( flags, P3_PLAYERSTATE_CORNERLEFT | P3_PLAYERSTATE_CORNERRIGHT) ) // за колонной?
			{
				if ( P3_CHECK_FLAGS( flags, P3_PLAYERSTATE_FACELEFT ) )
				{
					newCoverPosition = COVER_POSITION_LEFT_LOW;
				}
				else
				{
					newCoverPosition = COVER_POSITION_RIGHT_LOW;
				}
			}
			else if ( P3_CHECK_FLAGS( flags, P3_PLAYERSTATE_CORNERLEFT ) )
			{
				newCoverPosition = COVER_POSITION_LEFT_LOW;
			}
			else if ( P3_CHECK_FLAGS( flags, P3_PLAYERSTATE_CORNERRIGHT ) )
			{
				newCoverPosition = COVER_POSITION_RIGHT_LOW;
			}
			else if ( P3_CHECK_MIDDLE( flags ) && P3_CHECK_FLAGS( flags, P3_PLAYERSTATE_FACELEFT ) )
			{
				newCoverPosition = COVER_POSITION_FRONT_LEFT_LOW;
			}
			else if ( P3_CHECK_MIDDLE( flags ) && P3_CHECK_FLAGS( flags, P3_PLAYERSTATE_FACERIGHT ) )
			{
				newCoverPosition = COVER_POSITION_FRONT_RIGHT_LOW;
			}
		}
	}
	else
	{
		if ( curAimSector == COVER_AIM_SECTOR_BACK_LEFT )
		{
			newCoverPosition = COVER_POSITION_BACK_LEFT_HIGH;
		}
		else if ( curAimSector == COVER_AIM_SECTOR_BACK_RIGHT )
		{
			newCoverPosition = COVER_POSITION_BACK_RIGHT_HIGH;
		}
		else
		{
			if ( P3_CHECK_FLAGS( flags, P3_PLAYERSTATE_CORNERLEFT ) )
			{
				newCoverPosition = COVER_POSITION_LEFT_HIGH;
			}
			else if ( P3_CHECK_FLAGS( flags, P3_PLAYERSTATE_CORNERRIGHT ) )
			{
				newCoverPosition = COVER_POSITION_RIGHT_HIGH;
			}
		}
	}
}

Activity CPlayerAngles::SelectAttackActivity( Activity attackAct )
{
	//uncoment when need (blind fire )
	/*if (m_pPlayer->IsAimMode())
	{
		switch ( GetCoverPosition() )
		{
		case COVER_POSITION_LEFT_HIGH:
		case COVER_POSITION_LEFT_LOW:
			return ACT_COVER_ATTACK_LEFT_ZOOM;

		case COVER_POSITION_RIGHT_HIGH:
		case COVER_POSITION_RIGHT_LOW:
			return ACT_COVER_ATTACK_RIGHT_ZOOM;

		case COVER_POSITION_FRONT_LEFT_LOW:
			return ACT_COVER_ATTACK_FRONT_LEFT_ZOOM;

		case COVER_POSITION_FRONT_RIGHT_LOW:
			return ACT_COVER_ATTACK_FRONT_RIGHT_ZOOM;

		case COVER_POSITION_BACK_LEFT_HIGH:
		case COVER_POSITION_BACK_LEFT_LOW:
			return ACT_COVER_ATTACK_BACK_LEFT_ZOOM;

		case COVER_POSITION_BACK_RIGHT_HIGH:
		case COVER_POSITION_BACK_RIGHT_LOW:
			return  ACT_COVER_ATTACK_BACK_RIGHT_ZOOM;
		}
	}
	else*/
	{
		switch ( GetCoverPosition() )
		{
		case COVER_POSITION_LEFT_HIGH:
		case COVER_POSITION_LEFT_LOW:
			return ACT_COVER_ATTACK_LEFT;

		case COVER_POSITION_RIGHT_HIGH:
		case COVER_POSITION_RIGHT_LOW:
			return ACT_COVER_ATTACK_RIGHT;

		case COVER_POSITION_FRONT_LEFT_LOW:
			return ACT_COVER_ATTACK_FRONT_LEFT;

		case COVER_POSITION_FRONT_RIGHT_LOW:
			return ACT_COVER_ATTACK_FRONT_RIGHT;

		case COVER_POSITION_BACK_LEFT_HIGH:
		case COVER_POSITION_BACK_LEFT_LOW:
			return ACT_COVER_ATTACK_BACK_LEFT;

		case COVER_POSITION_BACK_RIGHT_HIGH:
		case COVER_POSITION_BACK_RIGHT_LOW:
			return  ACT_COVER_ATTACK_BACK_RIGHT;
		}
	}

	return attackAct;
}

Activity CPlayerAngles::SelectCoverActivity( Activity baseAct, bool is_aiming )
{
	if ( baseAct == ACT_COVER_IDLE_LEFT_HIGH || baseAct == ACT_COVER_IDLE_RIGHT_HIGH ||
	     baseAct == ACT_COVER_IDLE_LEFT_LOW  || baseAct == ACT_COVER_IDLE_RIGHT_LOW ||
		 baseAct == ACT_COVER_LOOK_LEFT_HIGH || baseAct == ACT_COVER_LOOK_RIGHT_HIGH ||
		 baseAct == ACT_COVER_LOOK_LEFT_LOW || baseAct == ACT_COVER_LOOK_RIGHT_LOW)
	{
		//uncoment when need (blind fire )
		/*if (m_pPlayer->IsAimMode())
		{
			switch ( GetCoverPosition() )
			{
			case COVER_POSITION_LEFT_HIGH:
				baseAct = ACT_COVER_AIM_LEFT_HIGH_ZOOM;
				break;

			case COVER_POSITION_LEFT_LOW:
				baseAct = ACT_COVER_AIM_LEFT_LOW_ZOOM;
				break;

			case COVER_POSITION_RIGHT_HIGH:
				baseAct = ACT_COVER_AIM_RIGHT_HIGH_ZOOM;
				break;

			case COVER_POSITION_RIGHT_LOW:
				baseAct = ACT_COVER_AIM_RIGHT_LOW_ZOOM;
				break;

			case COVER_POSITION_FRONT_LEFT_LOW:
				baseAct = ACT_COVER_AIM_FRONT_LEFT_LOW_ZOOM;
				break;

			case COVER_POSITION_FRONT_RIGHT_LOW:
				baseAct = ACT_COVER_AIM_FRONT_RIGHT_LOW_ZOOM;
				break;

			case COVER_POSITION_BACK_LEFT_HIGH:
				baseAct = ACT_COVER_AIM_BACK_LEFT_HIGH_ZOOM;
				break;

			case COVER_POSITION_BACK_LEFT_LOW:
				baseAct = ACT_COVER_AIM_BACK_LEFT_LOW_ZOOM;
				break;

			case COVER_POSITION_BACK_RIGHT_HIGH:
				baseAct = ACT_COVER_AIM_BACK_RIGHT_HIGH_ZOOM;
				break;

			case COVER_POSITION_BACK_RIGHT_LOW:
				baseAct = ACT_COVER_AIM_BACK_RIGHT_LOW_ZOOM;
				break;
			}
		}
		else */if ( is_aiming )
		{
			// TODO: загнать свитч в табличку?
			switch ( GetCoverPosition() )
			{
			case COVER_POSITION_LEFT_HIGH:
				baseAct = ACT_COVER_AIM_LEFT_HIGH;
				break;

			case COVER_POSITION_LEFT_LOW:
				baseAct = ACT_COVER_AIM_LEFT_LOW;
				break;

			case COVER_POSITION_RIGHT_HIGH:
				baseAct = ACT_COVER_AIM_RIGHT_HIGH;
				break;

			case COVER_POSITION_RIGHT_LOW:
				baseAct = ACT_COVER_AIM_RIGHT_LOW;
				break;

			case COVER_POSITION_FRONT_LEFT_LOW:
				baseAct = ACT_COVER_AIM_FRONT_LEFT_LOW;
				break;

			case COVER_POSITION_FRONT_RIGHT_LOW:
				baseAct = ACT_COVER_AIM_FRONT_RIGHT_LOW;
				break;

			case COVER_POSITION_BACK_LEFT_HIGH:
				baseAct = ACT_COVER_AIM_BACK_LEFT_HIGH;
				break;

			case COVER_POSITION_BACK_LEFT_LOW:
				baseAct = ACT_COVER_AIM_BACK_LEFT_LOW;
				break;

			case COVER_POSITION_BACK_RIGHT_HIGH:
				baseAct = ACT_COVER_AIM_BACK_RIGHT_HIGH;
				break;

			case COVER_POSITION_BACK_RIGHT_LOW:
				baseAct = ACT_COVER_AIM_BACK_RIGHT_LOW;
				break;
			}
		}
		else	// looking back
		{
			switch ( GetCoverPosition() )
			{
			case COVER_POSITION_BACK_LEFT_HIGH:
				baseAct = ACT_COVER_IDLE_BACK_LEFT_HIGH;
				break;

			case COVER_POSITION_BACK_LEFT_LOW:
				baseAct = ACT_COVER_IDLE_BACK_LEFT_LOW;
				break;

			case COVER_POSITION_BACK_RIGHT_HIGH:
				baseAct = ACT_COVER_IDLE_BACK_RIGHT_HIGH;
				break;

			case COVER_POSITION_BACK_RIGHT_LOW:
				baseAct = ACT_COVER_IDLE_BACK_RIGHT_LOW;
				break;
			}
		}
	}
	// shooting
	else if ( baseAct == ACT_RANGE_ATTACK1 )
	{
		baseAct = SelectAttackActivity( ACT_RANGE_ATTACK1 );
	}

	return baseAct;
}

float
CPlayerAngles::CalcNextAngle( float curAngle, float goalAngle, float dt, float speed, float da )
{
// Kostya: Извини, Игорь, но мой мозг не смог понять что тут происходило и зачем
#if 0
	speed *= 10;
	da = da != 0 ? da : speed / 100.f;

	float diffAngle = AngleDiff( goalAngle, curAngle );
	bool cv = diffAngle > 0;
	if ( !cv ) diffAngle = -diffAngle;

	if ( diffAngle < da )
	{
		return goalAngle;
	}

	float deltaAngle =  max( da, ( speed * dt ) );

	if ( deltaAngle > diffAngle )
	{
		return goalAngle;
	}

	return ( cv ) ? curAngle + deltaAngle : curAngle - deltaAngle;
#else
	Assert( goalAngle >= -180 && goalAngle <= 180 );
	Assert( da == 0 );

	if ( curAngle != goalAngle )
	{
		float diffAngle = AngleDiff( goalAngle, curAngle );
		float newAngle;

		Assert( diffAngle >= -180 && diffAngle <= 180 );

		newAngle = curAngle + diffAngle*(speed * min(dt, 1));
		newAngle = AngleNormalize( newAngle );

		return newAngle;
	}
	else
	{
		return curAngle;
	}
#endif
}

float
CPlayerAngles::FindNearestArcDir( float goalAngle, float curAngle )
{
	float goalAngle360 = AngleNormalizePositive( goalAngle );
	float curAngle360 = AngleNormalizePositive( curAngle );
	float diffAngle360 = goalAngle360 - curAngle360;

	if ( diffAngle360 > 180 )
	{
		return -1;
	}
	else if ( diffAngle360 < -180 )
	{
		return 1;
	}
	else
	{
		return Sign( diffAngle360 );
	}
}

void CPlayerAngles::CalcPlayerAngles()
{
	if ( m_pPlayer->IsCovering() )
	{
		m_flGoalYaw = m_pPlayer->GetCoverYaw();
	}
	else if ( m_pPlayer->IsDriving() )
	{
		//Assert( m_pPlayer->GetSimulator() );

		if(m_pPlayer->GetSimulator())
			m_flGoalYaw = AngleNormalize( m_pPlayer->GetSimulator()->GetAbsAngles()[ YAW ] );

		if(m_pPlayer->GetVehicleEntity())
			m_flGoalYaw =AngleNormalize( m_pPlayer->GetVehicleEntity()->GetAbsAngles()[ YAW ] );
	}
	else if ( m_pPlayer->GetPlayerState() & P3_PLAYERSTATE_MOVING
		|| IsAimingMode( m_pPlayer->GetInputMode() ) )
	{
		// разворачиваем в направлении камеры и сбрасываем все остальные
		m_flGoalYaw = AngleNormalize( m_angCameraAngles[ YAW ] );
	}
	else
	{
		// если угол больше чем надо, то поворачиваем на 90 градусов
		float cameraYaw = AngleNormalize( m_angCameraAngles[ YAW ] );
		float playerYaw = m_flGoalYaw;
		float diffYaw = AngleDiff( cameraYaw, playerYaw );

		if ( fabsf( diffYaw ) > PLAYER_YAW_MAX )
		{
			float newAngle = 0;
			if ( FindNearestArcDir( cameraYaw, m_flGoalYaw ) > 0 )
			{
				newAngle = AngleNormalize( m_flGoalYaw + 90 );

#ifndef CLIENT_DLL
				if (!m_pPlayer->HasHostage())
				{
					m_pPlayer->RemoveGesture( ACT_GESTURE_TURN_RIGHT90 );
					m_pPlayer->AddGesture( ACT_GESTURE_TURN_LEFT90 );
				}
#endif
			}
			else
			{
				newAngle = AngleNormalize( m_flGoalYaw - 90 );

#ifndef CLIENT_DLL
				if (!m_pPlayer->HasHostage())
				{
					m_pPlayer->RemoveGesture( ACT_GESTURE_TURN_LEFT90 );
					m_pPlayer->AddGesture( ACT_GESTURE_TURN_RIGHT90 );
				}
#endif
			}

			if ( p3_player_anglesdebug.GetBool() )
			{
				DevMsg( "Changing player angle from %f to %f\n", m_flGoalYaw, newAngle );
			}

			m_flGoalYaw = newAngle;
			//m_pPlayer->SetAbsAngles( QAngle(0,m_flGoalYaw,0) ); // дрыг, дрыг
		}
	}
}

#ifdef ANIMATE_HERE

void CPlayerAngles::CalcPlayerAimYawAndPitch()
{
	Vector aimStart, aimEnd, aimVector;
	m_pPlayer->GetAimDir(aimStart, aimEnd, aimVector);	

	float aimYaw = AngleNormalize( UTIL_VecToYaw( aimVector ) );
	float aimPitch = AngleNormalize( -UTIL_VecToPitch( aimVector ) );

	float aimLocalYaw = AngleNormalize( aimYaw - m_flYaw );
	if ( m_pPlayer->IsCovering() )
	{
		extern float P3_ClampCoverAimYaw( float flAimYaw, int flags );
		aimLocalYaw = P3_ClampCoverAimYaw( aimLocalYaw, m_pPlayer->GetPlayerState() );
	}

	SetComplexYaw( aimLocalYaw );
#ifdef CLIENT_DLL
	C_P3_BaseWeapon* weapon = (C_P3_BaseWeapon*)m_pPlayer->GetActiveWeapon();
#else
	CP3_BaseWeapon* weapon = (CP3_BaseWeapon*)m_pPlayer->GetActiveWeapon();
#endif

	if (weapon && weapon->IsWeaponBlocked ())
	{
		m_flGoalAimPitch = 0;
	}
	else
	{
		m_flGoalAimPitch = aimPitch;
	}

	float flHoldPos = m_pPlayer->GetPoseParameter( m_pPlayer->LookupPoseParameter( "hold_pos" ) );

	// ракетница возле стены
	if ( flHoldPos < -0.1f && weapon && FClassnameIs( weapon, "p3_weapon_m136" ) )
	{
		m_flGoalAimYaw = 0;
		m_flGoalAimPitch = 0;
	}

	// krotchy + shopvac
	if ( m_pPlayer->GetSkin() == PS_KROTCHY && m_pPlayer->GetActiveWeapon() )
	{
		WeaponID_t wID = (WeaponID_t)m_pPlayer->GetActiveWeapon()->MyP3BaseWeapon()->GetWpnData().iWeaponID;
		if ( wID == P3_WEAPON_SHOPVAC || wID == P3_WEAPON_GAMAMET)
		{
			float flMinPitch = Lerp( fabsf(flHoldPos), -30, 0);
			m_flGoalAimPitch = max( flMinPitch, m_flGoalAimPitch );
		}
	}

	// поворот головы?
	if ( m_pPlayer->IsAiming() || m_pPlayer->IsCovering() )
	{
		m_flGoalHeadPitch = 0;
	}
	else
	{
		Vector headPosition;
		QAngle headAngles;

		m_pPlayer->GetBonePosition( m_pPlayer->LookupBone( "bip_head" ), headPosition, headAngles );		
		m_flGoalHeadPitch = AngleNormalize( UTIL_VecToPitch( m_pPlayer->GetCameraAimTrace().endpos - headPosition ) ); 
	}
}

void CPlayerAngles::CalcPlayerMoveYaw()
{
	Vector velocity = m_pPlayer->GetAbsVelocity();
	float cameraYaw = m_angCameraAngles[ YAW ];

	if ( velocity.Length2DSqr() > 0.1f )
	{
		float velocityYaw = AngleNormalize( UTIL_VecToYaw( velocity ) );
		m_flGoalMoveYaw = UTIL_AngleDiff( velocityYaw, cameraYaw );
	}
	else
	{
		m_flGoalMoveYaw = 0;
	}
}

#endif

#ifndef CLIENT_DLL

void CPlayerAngles::UpdateCoverAiming()
{
	if ( m_pPlayer->IsCovering() && m_pPlayer->IsAiming() )
	{
		const float cornerCameraYawLimitLow  = 25;
		const float cornerCameraYawLimitHigh = 75;

		int flags = m_pPlayer->GetPlayerState();
		float yaw = m_flAimYaw < 0 ? m_flAimYaw + 360.f : m_flAimYaw;
		float yawCameraRel = AngleNormalize( m_angCameraAngles[YAW] - m_flYaw );

		CoverAimSector_t newCoverAimSector = COVER_AIM_SECTOR_INVALID;
		CoverPosition_t newCoverPosition = COVER_POSITION_INVALID;
		CoverFacing_t newCoverFacing = COVER_FACING_INVALID;

		SelectCoverAimSector( flags, yaw, m_coverAimSector, newCoverAimSector, newCoverFacing );
		if ( newCoverAimSector != COVER_AIM_SECTOR_INVALID )
		{
			SelectCoverPosition( flags, newCoverAimSector, newCoverPosition );

			if      ( newCoverPosition == COVER_POSITION_LEFT_LOW && yawCameraRel < -cornerCameraYawLimitLow )
			{
				newCoverPosition = COVER_POSITION_FRONT_LEFT_LOW;
				newCoverFacing = COVER_FACING_LEFT;
			}
			else if ( newCoverPosition == COVER_POSITION_RIGHT_LOW && yawCameraRel > cornerCameraYawLimitLow )
			{
				newCoverPosition = COVER_POSITION_FRONT_RIGHT_LOW;
				newCoverFacing = COVER_FACING_RIGHT;
			}
			else if ( newCoverPosition == COVER_POSITION_LEFT_HIGH )
			{
				if ( yawCameraRel < -cornerCameraYawLimitHigh )
				{
					newCoverPosition = COVER_POSITION_BACK_RIGHT_HIGH;
					newCoverFacing = COVER_FACING_RIGHT;
				}
				else
				{
					newCoverFacing = COVER_FACING_LEFT;
				}
			}
			else if ( newCoverPosition == COVER_POSITION_RIGHT_HIGH )
			{
				if ( yawCameraRel > cornerCameraYawLimitHigh )
				{
					newCoverPosition = COVER_POSITION_BACK_LEFT_HIGH;
					newCoverFacing = COVER_FACING_LEFT;
				}
				else
				{
					newCoverFacing = COVER_FACING_RIGHT;
				}
			}

			if ( newCoverFacing == COVER_FACING_LEFT )
			{
				m_pPlayer->ChangeFacingDir( false );
			}
			else if ( newCoverFacing == COVER_FACING_RIGHT )
			{
				m_pPlayer->ChangeFacingDir( true );
			}

			m_coverAimSector = newCoverAimSector;
			m_coverPosition = newCoverPosition;

		}
		else
		{
			m_coverAimSector = COVER_AIM_SECTOR_INVALID;
			m_coverPosition = COVER_POSITION_INVALID;
		}
	}
	else
	{
		m_coverAimSector = COVER_AIM_SECTOR_INVALID;
		m_coverPosition = COVER_POSITION_INVALID;
	}
}

#endif

void
CPlayerAngles::UpdateAngles( float dt )
{
	CalcPlayerAngles();
#ifdef ANIMATE_HERE
	CalcPlayerAimYawAndPitch();
	CalcPlayerMoveYaw();
#endif
#ifndef CLIENT_DLL
	UpdateCoverAiming();
#endif
}

void
CPlayerAngles::UpdatePose( float dt )
{
	dt *= m_pPlayer->GetSlowmoMultiplier();

	float maxAngle = PLAYER_AIMYAW_MAX;

	if ( m_pPlayer->IsCovering() || m_pPlayer->IsDriving() )
		maxAngle = 180.f;

	m_flYaw			= AngleNormalize( CalcNextAngle( m_flYaw, m_flGoalYaw, dt, PLAYER_YAW_SPEED ) );
	m_flAimPitch	= CalcNextAngle( m_flAimPitch, m_flGoalAimPitch, dt, PLAYER_AIM_SPEED );			m_flAimPitch = clamp( m_flAimPitch, -90, 90 );
	m_flAimYaw		= CalcNextAngle( m_flAimYaw, m_flGoalAimYaw, dt, PLAYER_AIM_SPEED );				m_flAimYaw = clamp( m_flAimYaw, -maxAngle, maxAngle );
	m_flHeadPitch	= CalcNextAngle( m_flHeadPitch, m_flGoalHeadPitch, dt, PLAYER_HEADPITCH_SPEED );	m_flHeadPitch = clamp( m_flHeadPitch, PLAYER_HEADPITCH_MIN, PLAYER_HEADPITCH_MAX );
	m_flHeadYaw		= CalcNextAngle( m_flHeadYaw, m_flGoalHeadYaw, dt, PLAYER_HEADYAW_SPEED );			m_flHeadYaw = clamp( m_flHeadYaw, -PLAYER_HEADYAW_MAX, PLAYER_HEADYAW_MAX );
	m_flBodyYaw		= CalcNextAngle( m_flBodyYaw, m_flGoalBodyYaw, dt, PLAYER_BODYYAW_SPEED );			m_flBodyYaw = clamp( m_flBodyYaw, -PLAYER_BODYYAW_MAX, PLAYER_BODYYAW_MAX );
	m_flMoveYaw		= AngleNormalize( CalcNextAngle( m_flMoveYaw, m_flGoalMoveYaw, dt, PLAYER_MOVEYAW_SPEED ) );

#ifndef CLIENT_DLL
	if ( m_pPlayer->IsPlayingGesture( m_pPlayer->m_KickActivity ) )
	{
		m_flBodyYaw = 0;
	}
#endif

	if ( m_pPlayer->IsAiming() )
	{
		// чтобы быстрее ворочился
		m_flYaw		= CalcNextAngle( m_flYaw, m_flGoalYaw, dt, PLAYER_YAW_SPEED );
		m_flYaw		= clamp( m_flYaw, -180, 180 );
	}

	if ( m_pPlayer->HasHostage() )
	{
		m_flYaw		= AngleNormalize( m_angCameraAngles[YAW] );
		//newAimYaw	= 0;
		m_flBodyYaw	= 0;
	}

#ifdef ANIMATE_HERE
	UpdatePoseParameters();
#endif

	if ( m_pPlayer->IsDriving() )
	{
		if ( m_pPlayer->GetSimulator() )
		{
			m_pPlayer->SetLocalAngles( vec3_angle );

			/*
			if ( !m_pPlayer->m_bPlayerSimulatorOrientationsSynhronized )
			{
				m_pPlayer->m_bPlayerSimulatorOrientationsSynhronized = true;

				SetBodyYaw( 0 );

				Vector vSeatOrigin;
				QAngle qSeatAngles;
				m_pPlayer->m_hSimulator->GetPassengerSeatPoint( VEHICLE_ROLE_DRIVER, &vSeatOrigin, &qSeatAngles );
				//
				//// Set us to that position
				m_pPlayer->SetAbsOrigin( vSeatOrigin );
				m_pPlayer->SetAbsAngles( qSeatAngles );
			}
			*/

			//m_pPlayer->GetSimulator()->UpdateDriverPoseParams( dt );
		}

		if( m_pPlayer->GetVehicle() )
			m_pPlayer->SetLocalAngles( QAngle(0.0,90.0,0.0) );
	}
	else
	{
		m_pPlayer->SetLocalAngles( vec3_angle );
		m_pPlayer->SetAbsAngles( QAngle( 0, m_flYaw, 0 ) );
	}
}

void
CPlayerAngles::DrawDebugOverlay( bool server )
{
	if ( p3_player_anglesdebug.GetBool() )
	{
		int row = server ? 10 : 30;
		engine->Con_NPrintf( row++, "PLAYER ANGLES" );
		engine->Con_NPrintf( row++, "yaw			%7.02f", m_flYaw );
		engine->Con_NPrintf( row++, "camera_yaw		%7.02f", m_angCameraAngles[ YAW ] );
		engine->Con_NPrintf( row++, "camera_yaw_rel	%7.02f", AngleNormalize( m_angCameraAngles[ YAW ] - m_flYaw ) );
		engine->Con_NPrintf( row++, "camera_pitch	%7.02f", m_angCameraAngles[ PITCH ] );
		engine->Con_NPrintf( row++, "velocity_yaw	%7.02f", AngleNormalize( UTIL_VecToYaw( m_pPlayer->GetAbsVelocity() ) ) );
		engine->Con_NPrintf( row++, "move_yaw		%7.02f", m_flMoveYaw );
		engine->Con_NPrintf( row++, "aim_pitch		%7.02f", m_flAimPitch );
		engine->Con_NPrintf( row++, "aim_yaw		%7.02f", m_flAimYaw );
		engine->Con_NPrintf( row++, "aim_yaw_360	%7.02f", AngleNormalizePositive( m_flAimYaw ) );
		engine->Con_NPrintf( row++, "head_yaw		%7.02f", m_flHeadYaw );
		engine->Con_NPrintf( row++, "head_pitch		%7.02f", m_flHeadPitch );
		engine->Con_NPrintf( row++, "body_yaw		%7.02f", m_flBodyYaw );
//		engine->Con_NPrintf( row++, "torso_yaw		%7.02f", GetPlayerPoseParameter( "torso_yaw" ) );
		engine->Con_NPrintf( row++, "complex_yaw	%7.02f", GetComplexYaw() );


		struct X { static void DrawArrowAndText( Vector& pos, float yaw, float yawAdd, float length,
			const char* title, int r, int g, int b, int a )
		{
			Vector arrowDir = UTIL_YawToVector( yaw + yawAdd );
			Vector arrowEnd = pos + arrowDir * length;
			Vector textPos = pos + arrowDir * (length + 4);

			char text[32];
			Q_snprintf( text, sizeof(text), "(%s %.01f)", title, yaw );

			NDebugOverlay::HorzArrow( pos, arrowEnd, 1, r,g,b,a, true, 0 );
			NDebugOverlay::Text( textPos, text, false, 0 );
		}};

		Vector origin = m_pPlayer->GetAbsOrigin() + Vector(0,0,2);
		X::DrawArrowAndText( origin, m_flYaw, 0, 60, "abs_yaw", 0,0,255,255 );
		X::DrawArrowAndText( origin, m_angCameraAngles[ YAW ], 0, 55, "camera_yaw", 0,255,0,200 );
		X::DrawArrowAndText( origin, GetComplexYaw(), m_flYaw, 50, "complex_yaw", 255,255,255,200 );
		X::DrawArrowAndText( origin, m_flAimYaw, m_flYaw, 40, "aim_yaw", 255,0,0,160 );
		X::DrawArrowAndText( origin, m_flBodyYaw + m_flHeadYaw, m_flYaw, 35, "head_yaw", 255,128,0,180 );
		X::DrawArrowAndText( origin, m_flBodyYaw, m_flYaw, 30, "torso_yaw", 255,255,0,160 );
		X::DrawArrowAndText( origin, m_flMoveYaw, m_flYaw, 25, "move_yaw", 0,255,0,140 );

		if ( m_pPlayer->IsCovering() )
		{
			static const char* coverAimSectors[] = {
				"INVALID",
				"FRONT",
				"BACK_LEFT",
				"BACK_RIGHT",
			};

			static const char* coverPosition[] = {
				"INVALID",
				"LEFT_HIGH",
				"LEFT_LOW",
				"RIGHT_HIGH",
				"RIGHT_LOW",
				"FRONT_LEFT_LOW",
				"FRONT_RIGHT_LOW",
				"BACK_LEFT_HIGH",
				"BACK_LEFT_LOW",
				"BACK_RIGHT_HIGH",
				"BACK_RIGHT_LOW",
			};

			engine->Con_NPrintf( row++, "cover aim sector	%s", coverAimSectors[m_coverAimSector+1] );
			engine->Con_NPrintf( row++, "cover position		%s", coverPosition[m_coverPosition+1] );
		}
	}
}

#ifdef ANIMATE_HERE
void
CPlayerAngles::InitPoseParameters()
{
	m_aim_pitch = m_pPlayer->LookupPoseParameter( "aim_pitch" );
	m_aim_yaw = m_pPlayer->LookupPoseParameter( "aim_yaw" );
	m_aim_yaw_360 = m_pPlayer->LookupPoseParameter( "aim_yaw_360" );
	m_head_yaw = m_pPlayer->LookupPoseParameter( "head_yaw" );
	m_head_pitch = m_pPlayer->LookupPoseParameter( "head_pitch" );
	m_move_yaw = m_pPlayer->LookupPoseParameter( "move_yaw" );
	m_body_yaw = m_pPlayer->LookupPoseParameter( "body_yaw" );
	if ( m_body_yaw < 0 ) m_body_yaw = m_pPlayer->LookupPoseParameter( "torso_yaw" );
}

void
CPlayerAngles::ReadPoseParameters()
{
	m_flAimPitch = m_pPlayer->GetPoseParameter( m_aim_pitch );
	m_flAimYaw = m_pPlayer->GetPoseParameter( m_aim_yaw );
	if ( m_flAimYaw == 0 && m_aim_yaw_360 >= 0 ) m_flAimYaw = m_pPlayer->GetPoseParameter( m_aim_yaw_360 );
	m_flHeadYaw = m_pPlayer->GetPoseParameter( m_head_yaw );
	m_flHeadPitch = m_pPlayer->GetPoseParameter( m_head_pitch );
	m_flMoveYaw = m_pPlayer->GetPoseParameter( m_move_yaw );
	m_flBodyYaw = m_pPlayer->GetPoseParameter( m_body_yaw );
}

void
CPlayerAngles::UpdatePoseParameters()
{
	m_pPlayer->SetPoseParameter( m_aim_pitch, m_flAimPitch );
	m_pPlayer->SetPoseParameter( m_aim_yaw, m_flAimYaw );
	if ( m_aim_yaw_360 >= 0 ) m_pPlayer->SetPoseParameter( m_aim_yaw_360, AngleNormalizePositive( m_flAimYaw ) );
	m_pPlayer->SetPoseParameter( m_head_yaw, m_flHeadYaw );
	m_pPlayer->SetPoseParameter( m_head_pitch, m_flHeadPitch );
	m_pPlayer->SetPoseParameter( m_move_yaw, m_flMoveYaw );
	m_pPlayer->SetPoseParameter( m_body_yaw, m_flBodyYaw );

	CBaseCombatWeapon* weapon = m_pPlayer->GetActiveWeapon();
	if ( weapon )
	{
		int w_aim_pitch = weapon->LookupPoseParameter( "aim_pitch" );
		int w_aim_yaw = weapon->LookupPoseParameter( "aim_yaw" );
		int w_aim_yaw_360 = weapon->LookupPoseParameter( "aim_yaw_360" );
		int w_head_yaw = weapon->LookupPoseParameter( "head_yaw" );
		int w_head_pitch = weapon->LookupPoseParameter( "head_pitch" );
		int w_move_yaw = weapon->LookupPoseParameter( "move_yaw" );
		int w_body_yaw = weapon->LookupPoseParameter( "body_yaw" );
		if ( w_body_yaw < 0 ) w_body_yaw = weapon->LookupPoseParameter( "torso_yaw" );

		if ( w_aim_pitch >= 0 ) weapon->SetPoseParameter( w_aim_pitch, m_flAimPitch );
		if ( w_aim_yaw >= 0 ) weapon->SetPoseParameter( w_aim_yaw, m_flAimYaw );
		if ( w_aim_yaw_360 >= 0 ) weapon->SetPoseParameter( w_aim_yaw_360, AngleNormalizePositive( m_flAimYaw ) );
		if ( w_head_yaw >= 0 ) weapon->SetPoseParameter( w_head_yaw, m_flHeadYaw );
		if ( w_head_pitch >= 0 ) weapon->SetPoseParameter( w_head_pitch, m_flHeadPitch );
		if ( w_move_yaw >= 0 ) weapon->SetPoseParameter( w_move_yaw, m_flMoveYaw );
		if ( w_body_yaw >= 0 )weapon->SetPoseParameter(  w_body_yaw, m_flBodyYaw );
	}
}
#endif
