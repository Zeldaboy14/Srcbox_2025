#ifndef P3_CVARS_H
#define P3_CVARS_H

extern ConVar	sk_plr_dmg_p3_deserteagle;
extern ConVar	sk_npc_dmg_p3_deserteagle;
extern ConVar	sk_max_p3_deserteagle;
// M16 aka assault rifle
extern ConVar	sk_plr_dmg_p3_m16;
extern ConVar	sk_npc_dmg_p3_m16;
extern ConVar	sk_max_p3_m16;
// M60 aka machinegun
extern ConVar	sk_plr_dmg_p3_m60;
extern ConVar	sk_npc_dmg_p3_m60;
extern ConVar	sk_max_p3_m60;
// Taser
extern ConVar	sk_plr_dmg_p3_taser;
extern ConVar	sk_npc_dmg_p3_taser;
extern ConVar	sk_max_p3_taser;
// Molotov
extern ConVar	sk_plr_dmg_p3_molotov; 
extern ConVar	sk_npc_dmg_p3_molotov;
extern ConVar	sk_max_p3_molotov;
// Shotgun
extern ConVar	sk_plr_num_p3_shotgun_pellets;
extern ConVar	sk_plr_dmg_p3_buckshot;
extern ConVar	sk_npc_dmg_p3_buckshot;
extern ConVar	sk_max_p3_buckshot;
// Greande
extern ConVar	sk_plr_dmg_p3_grenade;
extern ConVar	sk_npc_dmg_p3_grenade;
extern ConVar	sk_max_p3_grenade;

extern ConVar	sk_plr_dmg_p3_crotchy_grenade;
extern ConVar	sk_npc_dmg_p3_crotchy_grenade;
extern ConVar	sk_max_p3_crotchy_grenade;

// Laserpen
extern ConVar sk_max_p3_laserpen;

extern ConVar	p3_player_yaw_max;						// после этого угла поворачивает ВСЕ тело на 90 градусов
extern ConVar	p3_player_head_yaw_limit;				// после поворота головы на этот угол начинает поворачиваться корпус
extern ConVar	p3_player_head_yaw_max;					// после поворота головы на этот угол начинает поворачиваться корпус
extern ConVar	p3_player_head_pitch_min;				// поворот головы
extern ConVar	p3_player_head_pitch_max;				// поворот головы
extern ConVar	p3_player_body_yaw_max;					// максимальный поворот корпуса
extern ConVar	p3_player_aim_yaw_max;					// максимальный поворот корпуса при прицеливании

extern ConVar	p3_player_yaw_speed;					// скорость всего тела
extern ConVar	p3_player_head_yaw_speed;				// скорость поворота головы
extern ConVar	p3_player_head_pitch_speed;				// скорость поворота головы
extern ConVar	p3_player_body_yaw_speed;				// скорость поворота корпуса
extern ConVar	p3_player_aim_speed;					// скорость прицеливания (yaw и pitch)
extern ConVar	p3_player_move_yaw_speed;				// скорость смены направления движения

#endif // P3_CVARS_H
