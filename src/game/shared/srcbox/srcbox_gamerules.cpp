//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: The TF Game rules 
//
// $NoKeywords: $
//=============================================================================
#include "cbase.h"
#include "srcbox_gamerules.h"
#include "ammodef.h"
#include "KeyValues.h"
#include "tier0/icommandline.h"
#include "convar_serverbounded.h"

#ifndef CLIENT_DLL
#include "econ_holidays.h"
#endif

#include "tier3/tier3.h"
// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"


#define ITEM_RESPAWN_TIME	10.0f
#define MASK_RADIUS_DAMAGE  ( MASK_SHOT & ~( CONTENTS_HITBOX ) )

// Halloween 2013 VO defines for plr_hightower_event
#define HELLTOWER_TIMER_INTERVAL	( 60 + RandomInt( -30, 30 )	)
#define HELLTOWER_RARE_LINE_CHANCE	0.15	// 15%
#define HELLTOWER_MISC_CHANCE		0.50	// 50%

struct StatueInfo_t
{
	const char* pDiskName;
	Vector		vec_origin;
	QAngle		vec_angle;
};

static StatueInfo_t s_StatueMaps[] = {
	{ "ctf_2fort",				Vector(483, 613, 0),			QAngle(0, 180, 0) },
	{ "cp_dustbowl",			Vector(-596, 2650, -256),		QAngle(0, 180, 0) },
	{ "cp_granary",				Vector(-544, -510, -416),		QAngle(0, 180, 0) },
	{ "cp_well",				Vector(1255, 515, -512),		QAngle(0, 180, 0) },
	{ "cp_foundry",				Vector(-85, 912, 0),			QAngle(0, -90, 0) },
	{ "cp_gravelpit",			Vector(-4624, 660, -512),		QAngle(0, 0, 0) },
	{ "ctf_well",				Vector(1000, -240, -512),		QAngle(0, 180, 0) },
	{ "cp_badlands",			Vector(808, -1079, 64),		QAngle(0, 135, 0) },
	{ "pl_goldrush",			Vector(-2780, -650, 0),		QAngle(0, 90, 0) },
	{ "pl_badwater",			Vector(2690, -416, 131),		QAngle(0, -90, 0) },
	{ "plr_pipeline",			Vector(220, -2527, 128),		QAngle(0, 90, 0) },
	{ "cp_gorge",				Vector(-6970, 5920, -42),		QAngle(0, 0, 0) },
	{ "ctf_doublecross",		Vector(1304, -206, 8),		QAngle(0, 180, 0) },
	{ "pl_thundermountain",		Vector(-720, -1058, 128),		QAngle(0, -90, 0) },
	{ "cp_mountainlab",			Vector(-2930, 1606, -1069),	QAngle(0, 90, 0) },
	{ "cp_degrootkeep",			Vector(-1000, 4580, -255),	QAngle(0, -25, 0) },
	{ "pl_barnblitz",			Vector(3415, -2144, -54),		QAngle(0, 90, 0) },
	{ "pl_upward",				Vector(-736, -2275, 63),		QAngle(0, 0, 0) },
	{ "plr_hightower",			Vector(5632, 7747, 8),		QAngle(0, 0, 0) },
	{ "koth_viaduct",			Vector(-979, 0, 240),			QAngle(0, 180, 0) },
	{ "koth_king",				Vector(715, -395, -224),		QAngle(0, 135, 0) },
	{ "sd_doomsday",			Vector(-1025, 675, 128),		QAngle(0, 90, 0) },
	{ "cp_mercenarypark",		Vector(-2800, -775, -40),		QAngle(0, 0, 0) },
	{ "ctf_turbine",			Vector(718, 0, -256),			QAngle(0, 180, 0) },
	{ "koth_harvest_final",		Vector(-1428, 220, -15),		QAngle(0, 0, 0) },
	{ "pl_swiftwater_final1",	Vector(706, -2785, -934),		QAngle(0, 0, 0) },
	{ "pl_frontier_final",		Vector(3070, -3013, -193),	QAngle(0, -90, 0) },
	{ "cp_process_final",		Vector(650, -980, 535),		QAngle(0, 90, 0) },
	{ "cp_gullywash_final1",	Vector(200, 83, 47),			QAngle(0, -102, 0) },
	{ "cp_sunshine",			Vector(-4725, 5860, 65),		QAngle(0, 180, 0) },
};

struct MapInfo_t
{
	const char* pDiskName;
	const char* pDisplayName;
	const char* pGameType;
};

static MapInfo_t s_ValveMaps[] = {
	{ "ctf_2fort",	"2Fort",		"#Gametype_CTF" },
	{ "cp_dustbowl",	"Dustbowl",		"#TF_AttackDefend" },
	{ "cp_granary",	"Granary",		"#Gametype_CP" },
	{ "cp_well",		"Well",			"#Gametype_CP" },
	{ "cp_foundry",	"Foundry",		"#Gametype_CP" },
	{ "cp_gravelpit", "Gravel Pit",	"#TF_AttackDefend" },
	{ "tc_hydro",		"Hydro",		"#TF_TerritoryControl" },
	{ "ctf_well",		"Well",			"#Gametype_CTF" },
	{ "cp_badlands",	"Badlands",		"#Gametype_CP" },
	{ "pl_goldrush",	"Gold Rush",	"#Gametype_Escort" },
	{ "pl_badwater",	"Badwater Basin",	"#Gametype_Escort" },
	{ "plr_pipeline",	"Pipeline",		"#Gametype_EscortRace" },
	{ "cp_gorge",		"Gorge",		"#TF_AttackDefend" },
	{ "ctf_doublecross",		"Double Cross",		"#Gametype_CTF" },
	{ "pl_thundermountain",	"Thunder Mountain",	"#Gametype_Escort" },
	{ "tr_target",	"Target",		"#GameType_Training" },
	{ "tr_dustbowl",	"Dustbowl",		"#GameType_Training" },
	{ "cp_manor_event",	"Mann Manor",	"#TF_AttackDefend" },
	{ "cp_mountainlab",	"Mountain Lab",	"#TF_AttackDefend" },
	{ "cp_degrootkeep",	"DeGroot Keep",	"#TF_MedievalAttackDefend" },
	{ "pl_barnblitz",	"Barnblitz",	"#Gametype_Escort" },
	{ "pl_upward",	"Upward",	"#Gametype_Escort" },
	{ "plr_hightower",	"Hightower",	"#Gametype_EscortRace" },
	{ "koth_viaduct",	"Viaduct",	"#Gametype_Koth" },
	{ "koth_viaduct_event",	"Eyeaduct",	"#Gametype_Koth" },
	{ "koth_king",	"Kong King",	"#Gametype_Koth" },
	{ "koth_lakeside_event",	"Ghost Fort",	"#Gametype_Koth" },
	{ "plr_hightower_event",	"Helltower",	"#Gametype_EscortRace" },
	{ "rd_asteroid",	"Asteroid",	"#Gametype_RobotDestruction" },
	{ "pl_cactuscanyon",	"Cactus Canyon",	"#Gametype_Escort" },
	{ "sd_doomsday",	"Doomsday",	"#Gametype_SD" },
	{ "sd_doomsday_event",	"Carnival of Carnage",	"#Gametype_SD" },
	{ "cp_mercenarypark",	"Mercenary Park",	"#TF_AttackDefend" },
};

static MapInfo_t s_CommunityMaps[] = {
	{ "pl_borneo", "Borneo", "#Gametype_Escort" },
	{ "koth_suijin", "Suijin", "#Gametype_Koth" },
	{ "cp_snowplow", "Snowplow", "#TF_AttackDefend" },
	{ "koth_probed", "Probed", "#Gametype_Koth" },
	{ "pd_watergate", "Watergate", "#Gametype_PlayerDestruction" },
	{ "arena_byre", "Byre", "#Gametype_Arena" },
	{ "ctf_2fort_invasion", "2Fort Invasion", "#Gametype_CTF" },
	{ "cp_sunshine_event", "Sinshine", "#Gametype_CP" },
	{ "pl_millstone_event", "Hellstone", "#Gametype_Escort" },
	{ "cp_gorge_event", "Gorge Event", "#TF_AttackDefend" },
	{ "koth_moonshine_event", "Moonshine Event", "#Gametype_Koth" },
	{ "pl_snowycoast", "Snowycoast", "#Gametype_Escort" },
	{ "cp_vanguard", "Vanguard", "#Gametype_CP" },
	{ "ctf_landfall", "Landfall", "#Gametype_CTF" },
	{ "koth_highpass", "Highpass", "#Gametype_Koth" },
	{ "koth_maple_ridge_event", "Maple Ridge Event", "#Gametype_Koth" },
	{ "pl_fifthcurve_event", "Brimstone", "#Gametype_Escort" },
	{ "pd_pit_of_death_event", "Pit of Death", "#Gametype_PlayerDestruction" },
	{ "cp_mossrock", "Mossrock", "#TF_AttackDefend" },
	{ "koth_lazarus", "Lazarus", "#Gametype_Koth" },
	{ "plr_bananabay", "Banana Bay", "#Gametype_EscortRace" },
	{ "pl_enclosure_final", "Enclosure", "#Gametype_Escort" },
	{ "koth_brazil", "Brazil", "#Gametype_Koth" },
	{ "koth_bagel_event", "Cauldron", "#Gametype_Koth" },
	{ "pl_rumble_event", "Gravestone", "#Gametype_Escort" },
	{ "koth_slasher", "Slasher", "#Gametype_Koth" },
	{ "pd_cursed_cove_event", "Cursed Cove", "#Gametype_PlayerDestruction" },
	{ "pd_monster_bash", "Monster Bash", "#Gametype_PlayerDestruction" },
	{ "koth_slaughter_event", "Laughter", "#Gametype_Koth" },
	{ "pl_precipice_event_final", "Precipice", "#Gametype_Escort" },
	{ "koth_megalo", "Megalo", "#Gametype_Koth" },
	{ "pl_hasslecastle", "Hassle Castle", "#Gametype_Escort" },
	{ "pl_bloodwater", "Bloodwater", "#Gametype_Escort" },
	{ "koth_undergrove_event", "Moldergrove", "#Gametype_Koth" },
	{ "pl_pier", "Pier", "#Gametype_Escort" },
	{ "pd_snowville_event", "SnowVille", "#Gametype_PlayerDestruction" },
	{ "ctf_snowfall_final", "Snowfall", "#Gametype_CTF" },
	{ "pl_wutville_event", "Wutville", "#Gametype_Escort" },
	{ "pd_farmageddon", "Farmageddon", "#Gametype_PlayerDestruction" },
	{ "koth_los_muertos", "Los Muertos", "#Gametype_Koth" },
	{ "cp_ambush_event", "Erebus", "#TF_AttackDefend" },
	{ "pl_terror_event", "Terror", "#Gametype_Escort" },
	{ "arena_lumberyard_event", "Graveyard", "#Gametype_Arena" },
	{ "koth_synthetic_event", "Sinthetic", "#Gametype_Koth" },
	{ "pl_coal_event", "Polar", "#Gametype_Escort" },
	{ "pl_breadspace", "Bread Space", "#Gametype_Escort" },
	{ "pl_chilly", "Chilly", "#Gametype_Escort" },
	{ "koth_cascade", "Cascade", "#Gametype_Koth" },
	{ "cp_altitude", "Altitude", "#TF_AttackDefend" },
	{ "ctf_doublecross_snowy", "Doublefrost", "#Gametype_CTF" },
	{ "ctf_crasher", "Crasher!", "#Gametype_CTF" },
	{ "ctf_helltrain_event", "Helltrain", "#Gametype_CTF" },
	{ "pl_sludgepit_event", "Ghoulpit", "#Gametype_Escort" },
	{ "cp_spookeyridge", "Spookeyridge", "#TF_AttackDefend" },
	{ "koth_sawmill_event", "Soul-Mill", "#Gametype_Koth" },
	{ "plr_hacksaw_event", "Bonesaw", "#Gametype_EscortRace" },
	{ "cp_frostwatch", "Frostwatch", "#TF_AttackDefend" },
	{ "pl_frostcliff", "Frostcliff", "#Gametype_Escort" },
	{ "pl_rumford_event", "Rumford", "#Gametype_Escort" },
	{ "ctf_frosty", "Frosty", "#Gametype_CTF" },
	{ "cp_gravelpit_snowy", "Coal Pit", "#TF_AttackDefend" },
	{ "koth_sharkbay", "Sharkbay", "#Gametype_Koth" },
	{ "koth_rotunda", "Rotunda", "#Gametype_Koth" },
	{ "pl_phoenix", "Phoenix", "#Gametype_Escort" },
	{ "pl_cashworks", "Cashworks", "#Gametype_Escort" },
	{ "pl_venice", "Venice", "#Gametype_Escort" },
	{ "cp_reckoner", "Reckoner", "#Gametype_CP" },
	{ "cp_sulfur", "Sulfur", "#TF_AttackDefend" },
	{ "cp_hardwood_final", "Hardwood", "#TF_AttackDefend" },
	{ "ctf_pelican_peak", "Pelican Peak", "#Gametype_CTF" },
	{ "pd_selbyen", "Selbyen", "#Gametype_PlayerDestruction" },
	{ "vsh_tinyrock", "Tiny Rock", "#GameType_VSH" },
	{ "vsh_distillery", "Distillery", "#GameType_VSH" },
	{ "vsh_skirmish", "Skirmish", "#GameType_VSH" },
	{ "vsh_nucleus", "Nucleus VSH", "#GameType_VSH" },
	{ "arena_perks", "Perks", "#Gametype_Arena" },
	{ "koth_slime", "Slime", "#Gametype_Koth" },
	{ "cp_lavapit_final", "Lava Pit", "#TF_AttackDefend" },
	{ "pd_mannsylvania", "Mannsylvania", "#Gametype_PlayerDestruction" },
	{ "cp_degrootkeep_rats", "Sandcastle", "#TF_MedievalAttackDefend" },
	{ "pl_spineyard", "Spineyard", "#Gametype_Escort" },
	{ "pl_corruption", "Corruption", "#Gametype_Escort" },
	{ "zi_murky", "Murky", "#GameType_ZI" },
	{ "zi_atoll", "Atoll", "#GameType_ZI" },
	{ "zi_woods", "Woods", "#GameType_ZI" },
	{ "zi_sanitarium", "Sanitarium", "#GameType_ZI" },
	{ "zi_devastation_final1", "Devastation", "#GameType_ZI" },
	{ "koth_snowtower", "Snowtower", "#Gametype_Koth" },
	{ "koth_krampus", "Krampus", "#Gametype_Koth" },
	{ "ctf_haarp", "Haarp", "#TF_AttackDefend" },
	{ "cp_brew", "Brew", "#TF_AttackDefend" },
	{ "plr_hacksaw", "Hacksaw", "#Gametype_EscortRace" },
	{ "ctf_turbine_winter", "Turbine Event", "#Gametype_CTF" },
	{ "cp_carrier", "Carrier", "#TF_AttackDefend" },
	{ "pd_galleria", "Galleria", "#Gametype_PlayerDestruction" },
	{ "pl_emerge", "Emerge", "#Gametype_Escort" },
	{ "pl_camber", "Camber", "#Gametype_Escort" },
	{ "pl_embargo", "Embargo", "#Gametype_Escort" },
	{ "pl_odyssey", "Odyssey", "#Gametype_Escort" },
	{ "koth_megaton", "Megaton", "#Gametype_Koth" },
	{ "koth_cachoeira", "Cachoeira", "#Gametype_Koth" },
	{ "cp_overgrown", "Overgrown", "#TF_AttackDefend" },
	{ "cp_hadal", "Hadal", "#TF_AttackDefend" },
	{ "ctf_applejack", "Applejack", "#Gametype_CTF" },
	{ "pd_atom_smash", "Atom Smash", "#Gametype_PlayerDestruction" },
	{ "cp_canaveral_5cp", "Canaveral", "#Gametype_CP" },
	{ "cp_burghausen", "Burghausen", "#TF_MedievalAttackDefend" },
	{ "koth_toxic", "Toxic", "#Gametype_Koth" },
	{ "cp_darkmarsh", "Darkmarsh", "#TF_AttackDefend" },
	{ "cp_freaky_fair", "Freaky Fair", "#Gametype_CP" },
	{ "tow_dynamite", "Dynamite", "#GameType_TOW" },
	{ "pd_circus", "Circus", "#Gametype_PlayerDestruction" },
	{ "vsh_outburst", "Outburst", "#GameType_VSH" },
	{ "zi_blazehattan", "Blazehattan", "#GameType_ZI" },
	{ "koth_overcast_final", "Overcast", "#Gametype_Koth" },
	{ "cp_fortezza", "Fortezza", "#TF_AttackDefend" },
	{ "ctf_penguin_peak", "Penguin Peak", "#Gametype_CTF" },
	{ "pl_patagonia", "Patagonia", "#Gametype_Escort" },
	{ "plr_cutter", "Cutter", "#Gametype_EscortRace" },
	{ "vsh_maul", " Maul", "#GameType_VSH" },
};

bool IsValveMap(const char* pMapName)
{
	for (int i = 0; i < ARRAYSIZE(s_ValveMaps); ++i)
	{
		if (!Q_stricmp(s_ValveMaps[i].pDiskName, pMapName))
		{
			return true;
		}
	}
	return false;
}


bool IsCommunityMap(const char* pMapName)
{
	for (int i = 0; i < ARRAYSIZE(s_CommunityMaps); ++i)
	{
		if (!Q_stricmp(s_CommunityMaps[i].pDiskName, pMapName))
		{
			return true;
		}
	}
	return false;
}

typedef bool (*BIgnoreConvarChangeFunc)(void);

struct convar_tags_t
{
	const char* pszConVar;
	const char* pszTag;
	BIgnoreConvarChangeFunc ignoreConvarFunc;
};

// The list of convars that automatically turn on tags when they're changed.
// Convars in this list need to have the FCVAR_NOTIFY flag set on them, so the
// tags are recalculated and uploaded to the master server when the convar is changed.
convar_tags_t convars_to_check_for_tags[] =
{
	{ "mp_friendlyfire", "friendlyfire", NULL },
	{ "tf_birthday", "birthday", NULL },
	{ "mp_respawnwavetime", "respawntimes", NULL },
	{ "mp_fadetoblack", "fadetoblack", NULL },
	{ "tf_weapon_criticals", "nocrits", NULL },
	{ "mp_disable_respawn_times", "norespawntime", NULL },
	{ "tf_gamemode_arena", "arena", NULL },
	{ "tf_gamemode_cp", "cp", NULL },
	{ "tf_gamemode_ctf", "ctf", NULL },
	{ "tf_gamemode_sd", "sd", NULL },
	{ "tf_gamemode_mvm", "mvm", NULL },
	{ "tf_gamemode_payload", "payload", NULL },
	{ "tf_gamemode_rd",	"rd", NULL },
	{ "tf_gamemode_pd",	"pd", NULL },
	{ "tf_gamemode_tc",	"tc", NULL },
	{ "tf_beta_content", "beta", NULL },
	{ "tf_damage_disablespread", "dmgspread", NULL },
	{ "mp_highlander", "highlander", NULL },
	{ "tf_bot_count", "bots", NULL },
	{ "tf_pve_mode", "pve" },
	{ "sv_registration_successful", "_registered", NULL },
	{ "tf_server_identity_disable_quickplay", "noquickplay", NULL },
	{ "tf_mm_strict", "hidden", NULL },
	{ "tf_medieval", "medieval", NULL },
	{ "mp_holiday_nogifts", "nogifts" },
	{ "tf_powerup_mode", "powerup", NULL },
	{ "tf_gamemode_passtime", "passtime", NULL },
	{ "tf_gamemode_misc", "misc", NULL }, // catch-all for matchmaking to identify sd, tc, and pd servers via sv_tags
};

#ifdef _DEBUG
#define WAITING_FOR_PLAYERS_FLAGS	0
#else
#define WAITING_FOR_PLAYERS_FLAGS	FCVAR_DEVELOPMENTONLY
#endif


ConVar hide_server("hide_server", "0", FCVAR_GAMEDLL, "Whether the server should be hidden from the master server");

extern ConVar mp_waitingforplayers_time;

ConVar tf_gamemode_arena("tf_gamemode_arena", "0", FCVAR_REPLICATED | FCVAR_NOTIFY | FCVAR_DEVELOPMENTONLY);
ConVar tf_gamemode_cp("tf_gamemode_cp", "0", FCVAR_REPLICATED | FCVAR_NOTIFY | FCVAR_DEVELOPMENTONLY);
ConVar tf_gamemode_ctf("tf_gamemode_ctf", "0", FCVAR_REPLICATED | FCVAR_NOTIFY | FCVAR_DEVELOPMENTONLY);
ConVar tf_gamemode_sd("tf_gamemode_sd", "0", FCVAR_REPLICATED | FCVAR_NOTIFY | FCVAR_DEVELOPMENTONLY);
ConVar tf_gamemode_rd("tf_gamemode_rd", "0", FCVAR_REPLICATED | FCVAR_NOTIFY | FCVAR_DEVELOPMENTONLY);
ConVar tf_gamemode_pd("tf_gamemode_pd", "0", FCVAR_REPLICATED | FCVAR_NOTIFY | FCVAR_DEVELOPMENTONLY);
ConVar tf_gamemode_tc("tf_gamemode_tc", "0", FCVAR_REPLICATED | FCVAR_NOTIFY | FCVAR_DEVELOPMENTONLY);
ConVar tf_beta_content("tf_beta_content", "0", FCVAR_REPLICATED | FCVAR_NOTIFY | FCVAR_DEVELOPMENTONLY);
ConVar tf_gamemode_payload("tf_gamemode_payload", "0", FCVAR_REPLICATED | FCVAR_NOTIFY | FCVAR_DEVELOPMENTONLY);
ConVar tf_gamemode_mvm("tf_gamemode_mvm", "0", FCVAR_REPLICATED | FCVAR_NOTIFY | FCVAR_DEVELOPMENTONLY);
ConVar tf_gamemode_passtime("tf_gamemode_passtime", "0", FCVAR_REPLICATED | FCVAR_NOTIFY | FCVAR_DEVELOPMENTONLY);
ConVar tf_gamemode_misc("tf_gamemode_misc", "0", FCVAR_REPLICATED | FCVAR_NOTIFY | FCVAR_DEVELOPMENTONLY);
ConVar srcbox_gamemode_sandbox("srcbox_gamemode_sandbox", "0", FCVAR_REPLICATED | FCVAR_NOTIFY);
ConVar srcbox_gamemode_hl2("srcbox_gamemode_hl2", "0", FCVAR_REPLICATED | FCVAR_NOTIFY);

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CSrcboxGameRules::Activate()
{

	m_nGameType.Set(TF_GAMETYPE_UNDEFINED);

	tf_gamemode_arena.SetValue(0);
	tf_gamemode_cp.SetValue(0);
	tf_gamemode_ctf.SetValue(0);
	tf_gamemode_sd.SetValue(0);
	tf_gamemode_payload.SetValue(0);
	tf_gamemode_mvm.SetValue(0);
	tf_gamemode_rd.SetValue(0);
	tf_gamemode_pd.SetValue(0);
	tf_gamemode_tc.SetValue(0);
	tf_beta_content.SetValue(0);
	tf_gamemode_passtime.SetValue(0);
	tf_gamemode_misc.SetValue(0);
	srcbox_gamemode_sandbox.SetValue(0);
	srcbox_gamemode_hl2.SetValue(0);

	/*
	if (!Q_strncmp(STRING(gpGlobals->mapname), "tc_", 3))
	{
		tf_gamemode_tc.SetValue(1);
	}

	m_bIsInItemTestingMode.Set(false);

	CKothLogic* pKoth = dynamic_cast<CKothLogic*> (gEntList.FindEntityByClassname(NULL, "tf_logic_koth"));
	if (pKoth)
	{
		m_bPlayingKoth.Set(true);
	}

	CCompetitiveLogic* pCompLogic = dynamic_cast<CCompetitiveLogic*> (gEntList.FindEntityByClassname(NULL, "tf_logic_competitive"));
	if (pCompLogic)
	{
		m_hCompetitiveLogicEntity = pCompLogic;
	}

	CHybridMap_CTF_CP* pHybridMap_CTF_CP = dynamic_cast<CHybridMap_CTF_CP*> (gEntList.FindEntityByClassname(NULL, "tf_logic_hybrid_ctf_cp"));
	if (pHybridMap_CTF_CP)
	{
		m_bPlayingHybrid_CTF_CP.Set(true);
	}

	CHandle<CCPTimerLogic> hCPTimer = dynamic_cast<CCPTimerLogic*>(gEntList.FindEntityByClassname(NULL, "tf_logic_cp_timer"));
	while (hCPTimer != NULL)
	{
		m_CPTimerEnts.AddToTail(hCPTimer);
		hCPTimer = dynamic_cast<CCPTimerLogic*>(gEntList.FindEntityByClassname(hCPTimer, "tf_logic_cp_timer"));
	}


	if (tf_gamemode_tc.GetBool() || tf_gamemode_sd.GetBool() || tf_gamemode_pd.GetBool() || m_bPlayingMedieval)
	{
		tf_gamemode_misc.SetValue(1);
	}

	CBaseEntity* pStageLogic = gEntList.FindEntityByName(NULL, "competitive_stage_logic_case");
	if (pStageLogic)
	{
		m_bMapHasMatchSummaryStage.Set(true);
	}

	m_bCompetitiveMode.Set(false);*/
}
