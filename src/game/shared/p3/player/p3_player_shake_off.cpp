#include "cbase.h"

#include "p3_player_shake_off.h"
#include "in_buttons.h"

#ifdef CLIENT_DLL
#include "p3/p3_c_player.h"
#else
#include "p3/p3_player.h"
#endif


#define COOLDOWN_TICK_COUNT 10


Activity GetSufferActivity( int a )
{
	switch ( a )
	{
	case 1:
		return ACT_MONKEY_FACEFUCK_DUDE;
	case 4:
		return ACT_CAT_SUFFER_DUDE_L_LEG;
	case 5:
		return ACT_CAT_SUFFER_DUDE_R_LEG;
	case 2:
		return ACT_CAT_SUFFER_DUDE_L_HAND;
	case 3:
		return ACT_CAT_SUFFER_DUDE_R_HAND;
	}

	return ACT_IDLE;
}

Activity GetSnatchAttackActivity( int a )
{
	switch ( a )
	{
	case 1:
		return ACT_MONKEY_FACEFUCK_DUDE;
	case 4:
		return ACT_CAT_SNATCH_ATTACK_DUDE_L_LEG;
	case 5:
		return ACT_CAT_SNATCH_ATTACK_DUDE_R_LEG;
	case 2:
		return ACT_CAT_SNATCH_ATTACK_DUDE_L_HAND;
	case 3:
		return ACT_CAT_SNATCH_ATTACK_DUDE_R_HAND;
	}

	return ACT_IDLE;
}

Activity GetTearOffAnimation( int a )
{
	switch ( a )
	{
	case 1:
		return ACT_MONKEY_FACEFUCK_DUDE_THROW;
	case 4:
		return ACT_DUDE_TEAR_AWAY_L_LEG;
	case 5:
		return ACT_DUDE_TEAR_AWAY_R_LEG;
	case 2:
		return ACT_DUDE_TEAR_AWAY_L_HAND;
	case 3:
		return ACT_DUDE_TEAR_AWAY_R_HAND;
	}

	return ACT_IDLE;
}

BEGIN_SIMPLE_DATADESC( CP3PlayerShakeOffController )
	DEFINE_FIELD( attachment, FIELD_INTEGER ),
	DEFINE_FIELD( stage, FIELD_INTEGER ),
	DEFINE_FIELD( tick_count, FIELD_INTEGER ),
END_DATADESC()

void CP3PlayerShakeOffController::Reset( int att )
{
	attachment = att;
	stage = 0;
	tick_count = 0;
}

Activity CP3PlayerShakeOffController::TranslateActivity( Activity idealActivity )
{
	if ( stage == 0 ) return GetSufferActivity( attachment );
	if ( stage < 9 ) return GetSnatchAttackActivity( attachment );
	return GetTearOffAnimation( attachment );
}

void CP3PlayerShakeOffController::HandleAnimationEvent( int ae )
{
	if ( stage % 2 )
	{
		stage++;
	}
	else
	{
	}

	UTIL_ScreenShake( m_pPlayer->GetAbsOrigin(), 5, 20, 100, 60, SHAKE_START );
}

void CP3PlayerShakeOffController::HandleInputEvent( CUserCmd *ucmd )
{
	if ( stage == 0 )
	{
		tick_count = ucmd->tick_count - COOLDOWN_TICK_COUNT;
	}

	if ( ucmd->tick_count - tick_count < COOLDOWN_TICK_COUNT )
		return;

//	if ( ucmd->tick_count - tick_count > 

	switch ( stage )
	{
	case 0:
		if ( ( ucmd->buttons & IN_ATTACK ) != 0 && ( ucmd->buttons & IN_ATTACK2 ) == 0 )
		{
			m_pPlayer->ResetSequence( 0 );

			stage++;
			tick_count = ucmd->tick_count;
		}
		break;
	case 2:
		if ( ( ucmd->buttons & IN_ATTACK2 ) != 0 && ( ucmd->buttons & IN_ATTACK ) == 0 )
		{
			stage++;
			tick_count = ucmd->tick_count;
		}
		break;
	case 4:
		if ( ( ucmd->buttons & IN_ATTACK ) != 0 && ( ucmd->buttons & IN_ATTACK2 ) == 0 )
		{
			stage++;
			tick_count = ucmd->tick_count;
		}
		break;
	case 6:
		if ( ( ucmd->buttons & IN_ATTACK2 ) != 0 && ( ucmd->buttons & IN_ATTACK ) == 0 )
		{
			stage++;
			tick_count = ucmd->tick_count;
		}
		break;
	case 8:
		if ( ( ucmd->buttons & IN_ATTACK ) != 0 && ( ucmd->buttons & IN_ATTACK2 ) != 0 )
		{
			m_pPlayer->ResetSequence( 0 );

			stage++;
		}
		break;
	}
}
