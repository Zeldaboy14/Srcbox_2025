#include "cbase.h"
#include "p3_cvars_shared.h"

// Desert eagle
ConVar	sk_plr_dmg_p3_deserteagle			( "sk_plr_dmg_p3_deserteagle", "0", FCVAR_REPLICATED );
ConVar	sk_npc_dmg_p3_deserteagle			( "sk_npc_dmg_p3_deserteagle", "0", FCVAR_REPLICATED );
ConVar	sk_max_p3_deserteagle				( "sk_max_p3_deserteagle", "0", FCVAR_REPLICATED );
// M16 aka assault rifle
ConVar	sk_plr_dmg_p3_m16					( "sk_plr_dmg_p3_m16", "0", FCVAR_REPLICATED );
ConVar	sk_npc_dmg_p3_m16					( "sk_npc_dmg_p3_m16", "0", FCVAR_REPLICATED );
ConVar	sk_max_p3_m16						( "sk_max_p3_m16", "0", FCVAR_REPLICATED );
// M136 aka rocket launcher
ConVar	sk_plr_dmg_p3_m136					( "sk_plr_dmg_p3_m136", "0", FCVAR_REPLICATED );
ConVar	sk_npc_dmg_p3_m136					( "sk_npc_dmg_p3_m136", "0", FCVAR_REPLICATED );
ConVar	sk_max_p3_m136						( "sk_max_p3_m136", "5", FCVAR_REPLICATED );
// M60 aka machinegun
ConVar	sk_plr_dmg_p3_m60					( "sk_plr_dmg_p3_m60", "0", FCVAR_REPLICATED );
ConVar	sk_npc_dmg_p3_m60					( "sk_npc_dmg_p3_m60", "0", FCVAR_REPLICATED );
ConVar	sk_max_p3_m60						( "sk_max_p3_m60", "0", FCVAR_REPLICATED );
// Taser
ConVar	sk_plr_dmg_p3_taser					( "sk_plr_dmg_p3_taser", "0", FCVAR_REPLICATED );
ConVar	sk_npc_dmg_p3_taser					( "sk_npc_dmg_p3_taser", "0", FCVAR_REPLICATED );
ConVar	sk_max_p3_taser						( "sk_max_p3_taser", "1", FCVAR_REPLICATED );
// Molotov
ConVar	sk_plr_dmg_p3_molotov				( "sk_plr_dmg_p3_molotov", "1", FCVAR_REPLICATED );
ConVar	sk_npc_dmg_p3_molotov				( "sk_npc_dmg_p3_molotov", "1", FCVAR_REPLICATED );
ConVar	sk_max_p3_molotov					( "sk_max_p3_molotov", "50", FCVAR_REPLICATED );
// Shotgun
ConVar	sk_plr_num_p3_shotgun_pellets		( "sk_plr_num_p3_shotgun_pellets", "7", FCVAR_REPLICATED );
ConVar	sk_plr_dmg_p3_buckshot				( "sk_plr_dmg_p3_buckshot", "1", FCVAR_REPLICATED );
ConVar	sk_npc_dmg_p3_buckshot				( "sk_npc_dmg_p3_buckshot", "1", FCVAR_REPLICATED );
ConVar	sk_max_p3_buckshot					( "sk_max_p3_buckshot", "50", FCVAR_REPLICATED );
// Greande
ConVar	sk_plr_dmg_p3_grenade				( "sk_plr_dmg_p3_grenade", "0", FCVAR_REPLICATED );
ConVar	sk_npc_dmg_p3_grenade				( "sk_npc_dmg_p3_grenade", "0", FCVAR_REPLICATED );
ConVar	sk_max_p3_grenade					( "sk_max_p3_grenade", "8", FCVAR_REPLICATED );
// Crotchy grenade
ConVar	sk_plr_dmg_p3_crotchy_grenade		( "sk_plr_dmg_p3_crotchy_grenade", "0", FCVAR_REPLICATED );
ConVar	sk_npc_dmg_p3_crotchy_grenade		( "sk_npc_dmg_p3_crotchy_grenade", "0", FCVAR_REPLICATED );
ConVar	sk_max_p3_crotchy_grenade			( "sk_max_p3_crotchy_grenade", "8", FCVAR_REPLICATED );
// Laserpen
ConVar sk_max_p3_laserpen					( "sk_max_p3_laserpen", "99", FCVAR_REPLICATED );


ConVar	p3_player_yaw_max( "p3_player_yaw_max", "65", FCVAR_REPLICATED | FCVAR_CHEAT );
ConVar	p3_player_head_yaw_limit( "p3_player_head_yaw_limit", "20", FCVAR_REPLICATED | FCVAR_CHEAT );
ConVar	p3_player_head_yaw_max( "p3_player_head_yaw_max", "30", FCVAR_REPLICATED | FCVAR_CHEAT );
ConVar	p3_player_head_pitch_min( "p3_player_head_pitch_min", "-60", FCVAR_REPLICATED | FCVAR_CHEAT );
ConVar	p3_player_head_pitch_max( "p3_player_head_pitch_max", "80", FCVAR_REPLICATED | FCVAR_CHEAT );
ConVar	p3_player_body_yaw_max( "p3_player_body_yaw_max", "30", FCVAR_REPLICATED | FCVAR_CHEAT );
ConVar	p3_player_aim_yaw_max( "p3_player_aim_yaw_max", "45", FCVAR_REPLICATED | FCVAR_CHEAT );

ConVar	p3_player_yaw_speed( "p3_player_yaw_speed", "4.f", FCVAR_REPLICATED | FCVAR_CHEAT );
ConVar	p3_player_head_yaw_speed( "p3_player_head_yaw_speed", "8.f", FCVAR_REPLICATED | FCVAR_CHEAT );
ConVar	p3_player_head_pitch_speed( "p3_player_head_pitch_speed", "8.f", FCVAR_REPLICATED | FCVAR_CHEAT );
ConVar	p3_player_body_yaw_speed( "p3_player_body_yaw_speed", "8.f", FCVAR_REPLICATED | FCVAR_CHEAT );
ConVar	p3_player_aim_speed( "p3_player_aim_speed", "8.f", FCVAR_REPLICATED | FCVAR_CHEAT );
ConVar	p3_player_move_yaw_speed( "p3_player_move_yaw_speed", "4.f", FCVAR_REPLICATED | FCVAR_CHEAT );
