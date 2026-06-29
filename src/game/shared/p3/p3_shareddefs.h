//========= Copyright © 1996-2005, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

#ifndef P3_SHAREDDEFS_H
#define P3_SHAREDDEFS_H

#ifdef _WIN32
#pragma once
#endif

#include "const.h"
#include "hl2_shareddefs.h"


//--------------------------------------------------------------------------
// Collision groups
//--------------------------------------------------------------------------

enum
{
	P3COLLISION_GROUP_TEST_PROHIBITED_AREA = LAST_HL2COLLISION_GROUP,
	P3COLLISION_GROUP_PROHIBITED_AREA,
	P3COLLISION_GROUP_PIGEON,
	P3COLLISION_GROUP_COPTER,
};


//--------------------------------------------------------------------------
// Weapon mask
//--------------------------------------------------------------------------

#define WEAPON_UNDEFINED		0x0000
#define WEAPON_DEAGLE			(1 << 0)
#define WEAPON_EMPTYHANDS		(1 << 1)
#define WEAPON_FAKE_ONEHANDLED	(1 << 2)
#define WEAPON_FAKE_BULKY		(1 << 3)
#define WEAPON_FAKE_TWOHANDLED	(1 << 4)

#define P3_USESOLID_NONE 0x0000
#define P3_USESOLID_BBOX 0x0001
#define P3_USESOLID_VPHYSICS 0x0002

#define VEC_BULKY_HULL_MIN	Vector(-32, -32, 0 )	
#define VEC_BULKY_HULL_MAX	Vector( 32,  32,  72 )

#define ABILITY_UNDEFINED	0x0000
#define ABILITY_JUMP		0x0001
#define ABILITY_COVER		0x0002
#define ABILITY_BURST		0x0004
#define ABILITY_KICK		0x0008
#define ABILITY_RUN			0x0010
#define ABILITY_DUCK		0x0020

//--------------------------------------------------------------------------
// Damage -- чтобы рисовать разные декальки. См. GetImpactDecal()
//--------------------------------------------------------------------------

#define DMG_KNUCKLE			(DMG_DIRECT | DMG_CLUB)
#define DMG_BATON			(DMG_DIRECT | DMG_FALL | DMG_CLUB)
#define DMG_SHOVEL			(DMG_DIRECT | DMG_SLASH)
#define DMG_SHOVEL2			(DMG_DIRECT | DMG_SLASH | DMG_CLUB)
#define DMG_HAMMER			(DMG_DIRECT | DMG_VEHICLE | DMG_CLUB)
#define DMG_NAILBAT			(DMG_DIRECT | DMG_VEHICLE | DMG_CLUB | DMG_BULLET)

//--------------------------------------------------------------------------------------------------------
// Hit announcement types
enum hit_announcement_t
{
	HITANN_UNKNOWN,
	HITANN_DEATHMSG,

#if 0

	HITANN_INCAP,
	HITANN_SURVIVOR_DEATHMSG,
	HITANN_DEFIBRILLATOR_USED,
	HITANN_VS_REACHED_MARKER,
	HITANN_SCAVENGE_DESTROY_GASCAN,		// "%s1 destroyed a gas can!"

	// Prioritized from least to most important

	// Infected 
	L4D_HIT_ASSIST,					//			"%s1 assisted against %s2"
	L4D_HIT_ASSIST_ATTACK,			// 			"%s1 hit %s2, assisted by %s3"
	L4D_HIT_ATTACK,					//			"%s1 hit %s2"
	L4D_HIT_PUSH_ASSIST_ATTACK,		//			"%s1 pushed %s2, assisted by %s3"
	L4D_HIT_PUSH_ATTACK,			//			"%s1 pushed %s2"
	L4D_HIT_POUNCE_ATTACK,			//			"%s1 pounced %s2 for %s3 damage"
	L4D_INCAPACITATE_ASSIST,		//			"%s1 assisted incapacitating %s2"
	L4D_INCAPACITATE,				//			"%s1 incapacitated %s2"

	// Survivor
	L4D_AWARD_SAVED,				//	"L4D_OnAwardSaved"							"%s1 saved %s2"
	L4D_AWARD_SHARED,				//	"L4D_OnAwardSharing"						"%s1 gave health to %s2"
	L4D_AWARD_SHARED_ADRENALINE,	//  "L4D_OnAwardSharingAdrenaline"				"%s1 gave adrenaline to %s2"
	L4D_AWARD_PROTECTED,			//	"L4D_OnAwardProtector"						"%s1 protected %s2"
	L4D_AWARD_RESCUED,				//	"L4D_OnAwardRescuer"						"%s1 rescued %s2"
	L4D_AWARD_MEDIC,				//	"L4D_OnAwardMedic"							"%s1 healed %s2"

	// Survival
	L4D_SURVIVAL_BRONZE,			//	"L4D_BronzeMedalEarned"
	L4D_SURVIVAL_SILVER,			//	"L4D_BronzeMedalEarned"
	L4D_SURVIVAL_GOLD,				//	"L4D_BronzeMedalEarned"

#endif

	HITANN_COUNT,
};

#define DMG_KROTCHY			(DMG_LASTGENERICFLAG<<1)	// This is krotchy

#endif // P3_SHAREDDEFS_H
