#include "cbase.h"
#include "p3_input_mode_shared.h"

#ifdef CLIENT_DLL
	#include "p3/p3_c_player.h"
#else
	#include "p3/p3_player.h"
#endif

InputMode_t	GetInputMode()
{
	if ( CP3_Player* pPlayer = P3_GetPlayer() )
	{
		int mode = clamp( pPlayer->GetInputMode(), 0, NUM_INPUT_MODES-1 );
		return (InputMode_t)mode;
	}
	else
	{
		return (InputMode_t)0;
	}
}

#ifdef CLIENT_DLL

void SetInputMode( InputMode_t mode )
{
	// меняем переменную только на сервере
	AssertMsg( 0, "SetInputMode() is forbidden on client" );
} 

#else

CON_COMMAND_F( p3_input_mode, "Select camera and input mode", FCVAR_CHEAT )
{
	if ( args.ArgC() != 2 )
		return;

	if ( CP3_Player* player = P3_GetPlayer() )
	{
		player->SetInputMode( Q_atoi( args[1] ) );
	}
}

CON_COMMAND_F( p3_input_mode_03, "Select next camera and input mode", FCVAR_CHEAT )
{
	if ( CP3_Player* player = P3_GetPlayer() )
	{
		player->SetInputMode( ( player->GetInputMode() + 1 ) % 3 );
	}
}


#if 0
void SetInputMode( InputMode_t mode )
{
	if ( CP3_Player* pPlayer = P3_GetPlayer() )
	{
		pPlayer->SetInputMode( (int)mode );
	}
}

static void ModeChangeCallback( IConVar *var, const char *pOldValue, float flOldValue )
{
	SetInputMode( (InputMode_t)((ConVar*)var)->GetInt() );
}

static ConVar p3_input_mode( "p3_input_mode", "0", FCVAR_CHEAT,
			"Select camera and input mode:\n"
			" 0=thirdperson, 1=firstperson, 2=freecamera,\n"
			" 3=thirdperson_aim, 4=thirdperson_cover, 5=thirdperson_cover_aim,\n"
			" 6=cutscene, 7=killingmode, 8=thirdperson_alt1, 9=thirdperson_alt2\n"
			" 10=thirdperson_burst, 11=thirdperson_hostage",
			&ModeChangeCallback );
#endif

#endif
