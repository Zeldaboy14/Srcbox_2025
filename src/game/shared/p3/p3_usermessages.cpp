//========= Copyright © 1996-2005, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
//=============================================================================//

#include "cbase.h"
#include "usermessages.h"
#include "shake.h"
#include "voice_gamemgr.h"

// NVNT include to register in haptic user messages
#include "haptics/haptic_msgs.h" 

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

// from hl2_usermessages.cpp

void RegisterUserMessages( void )
{
	usermessages->Register( "ResetHUD", 1);		// called every respawn
	usermessages->Register( "ItemPickup", -1 );
	usermessages->Register( "ShowMenu", -1 );
	usermessages->Register( "Shake", 13 );
	usermessages->Register( "TextMsg", -1 );
	/*
	usermessages->Register( "SayText", -1 );
	usermessages->Register( "SayText2", -1 );
	usermessages->Register( "TextMsg", -1 );*/
	usermessages->Register( "HudText", -1 );
	usermessages->Register( "HudMsg", -1 );

	usermessages->Register( "Fade", 10 );
	usermessages->Register( "VGUIMenu", -1 );	// Show VGUI menu
	usermessages->Register( "Rumble", 3 );	// Send a rumble to a controller
	usermessages->Register( "Damage", 18 );		// BUG: floats are sent for coords, no variable bitfields in hud & fixed size Msg
	usermessages->Register( "VoiceMask", VOICE_MAX_PLAYERS_DW*4 * 2 + 1 );
	usermessages->Register( "CloseCaption", -1 ); // Show a caption (by string id number)(duration in 10th of a second)
	usermessages->Register( "RequestState", 0 );
	usermessages->Register( "SquadMemberDied", 0 );
	usermessages->Register( "AmmoDenied", 2 );
	usermessages->Register( "CreditsMsg", 1 );
	usermessages->Register( "LogoTimeMsg", 4 );
	usermessages->Register( "MarkAchievement", -1 );

	usermessages->Register( "AchievementEvent", -1 );
	usermessages->Register( "UpdateEvent", -1 );

	// P3 messages
	usermessages->Register( "AttributeBar", -1 );
	usermessages->Register( "UpdateKarma", -1 );
	usermessages->Register( "SteamWeaponStatData", -1 );

	usermessages->Register( "ShowMessage", -1);
	usermessages->Register( "ShowWeaponHint", -1);
	usermessages->Register( "RemoveWeaponHint", -1);
	usermessages->Register( "ShowLog", -1);
	usermessages->Register( "ToggleLog", -1);
	usermessages->Register( "ShowBrief", -1);
	usermessages->Register( "Snatching", 1);

	// NVNT register haptic user messages
	RegisterHapticMessages();
}