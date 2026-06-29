#include "cbase.h"
#include "p3_fluids_vars.h"

ConVar p3_fluids_draw( "p3_fluids_draw", "1", FCVAR_CHEAT | FCVAR_REPLICATED );

ConVar p3_fireup_debug( "p3_fireup_debug", "0", FCVAR_CHEAT | FCVAR_REPLICATED );
ConVar p3_fluids_debug( "p3_fluids_debug", "0", FCVAR_CHEAT | FCVAR_REPLICATED );
ConVar p3_fluid_debug_info( "p3_fluid_debug_info", "0", FCVAR_CHEAT | FCVAR_REPLICATED );

ConVar p3_fluid_amount_threshold( "p3_fluid_amount_threshold", "500", FCVAR_REPLICATED );
ConVar p3_fluid_absorption( "p3_fluid_absorption", "100", FCVAR_REPLICATED );
ConVar p3_fluid_burn_time( "p3_fluid_burn_time", "4.0", FCVAR_REPLICATED );
ConVar p3_fluid_density( "p3_fluid_density", "0.005", FCVAR_REPLICATED );
ConVar p3_fluid_gravity_scale( "p3_fluid_gravity_scale", "1", FCVAR_REPLICATED );
ConVar p3_fluid_new_count( "p3_fluid_new_count", "6", FCVAR_REPLICATED );
ConVar p3_fluid_new_threshold( "p3_fluid_new_threshold", "5000", FCVAR_REPLICATED );
ConVar p3_fluid_remove_amount( "p3_fluid_remove_amount", "20.00", FCVAR_REPLICATED );
ConVar p3_fluid_spread_speed( "p3_fluid_spread_speed", "0.01", FCVAR_REPLICATED );
ConVar p3_fluid_step_scale( "p3_fluid_step_scale", "0.6", FCVAR_REPLICATED );
ConVar p3_fluid_stream_min_step( "p3_fluid_stream_min_step", "10", FCVAR_REPLICATED );
ConVar p3_fluid_vel_scale( "p3_fluid_vel_scale", "0.003", FCVAR_REPLICATED );
//ConVar p3_fluidity_friction( "p3_fluidity_friction", "0.001", FCVAR_REPLICATED );
ConVar p3_fluidity_curvature( "p3_fluidity_curvature", "1", FCVAR_REPLICATED );
ConVar p3_fluidity_force_scale( "p3_fluidity_force_scale", "1", FCVAR_REPLICATED );
ConVar p3_fluidity_max_speed( "p3_fluidity_max_speed", "12000", FCVAR_REPLICATED );
ConVar p3_fluidity_min_force( "p3_fluidity_min_force", "5", FCVAR_REPLICATED );
//ConVar p3_fluidity_force1_scale( "p3_fluidity_force1_scale", "0.001", FCVAR_REPLICATED );

#ifdef _WIN32
	ConVar p3_fluids_maxflames( "p3_fluids_maxflames", "128", FCVAR_REPLICATED );
#else
	ConVar p3_fluids_maxflames( "p3_fluids_maxflames", "128", FCVAR_REPLICATED );
#endif

ConVar p3_fluid_bump_scale( "p3_fluid_bump_scale", "50", FCVAR_REPLICATED );


ConVar p3_gasoline_burn_time( "p3_gasoline_burn_time", "2", FCVAR_CHEAT | FCVAR_REPLICATED );
