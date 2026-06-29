#ifndef P3_PLAYER_SHARED_H
#define P3_PLAYER_SHARED_H

#ifdef CLIENT_DLL
#define CP3_Player C_P3_Player
#endif

class CP3_Player;
class CBaseEntity;

#define P3_CAMERA_SHAKE_MODEL			"models/camera/cam_shake.mdl"

// состо€ние игрока
#define P3_PLAYERSTATE_COVER			(1<<0)		// пр€четс€
#define P3_PLAYERSTATE_CORNERLEFT		(1<<1)		// пр€четс€ и слева угол
#define P3_PLAYERSTATE_CORNERRIGHT		(1<<2)		// пр€четс€ и справа угол
#define P3_PLAYERSTATE_DUCK				(1<<3)		// сидит на корточках
#define P3_PLAYERSTATE_FACELEFT			(1<<4)		// смотрит влево
#define P3_PLAYERSTATE_FACERIGHT		(1<<5)		// смотрит вправо
#define P3_PLAYERSTATE_AIMING			(1<<6)		// прицеливаемс€
#define	P3_PLAYERSTATE_MOVING			(1<<7)		// передвигаемс€
#define	P3_PLAYERSTATE_LOOKLEFT			(1<<8)		// выгл€дываем влево
#define	P3_PLAYERSTATE_LOOKRIGHT		(1<<9)		// выгл€дываем вправо
#define	P3_PLAYERSTATE_DRIVING			(1<<10)		// за рулем
#define	P3_PLAYERSTATE_READYTOSHOOT		(1<<11)		// можеть сейчас стрел€ть
#define	P3_PLAYERSTATE_AIM_CENTER		(1<<12)		// сидит возле угла, но целитс€ по центру
#define	P3_PLAYERSTATE_AIM_BACK			(1<<13)		// целитс€ назад
#define	P3_PLAYERSTATE_SPRINTING		(1<<14)		// бегаем
#define	P3_PLAYERSTATE_UNCOVERING		(1<<15)		// вызходит из кавера
#define	P3_PLAYERSTATE_SNATCHED			(1<<16)		// заснетчен животным
#define	P3_PLAYERSTATE_BADTARGET		(1<<17)		// не может туда стрел€ть
#define	P3_PLAYERSTATE_PEEING			(1<<18)		// дюд писает

// провер€ет несколько флагов
#define P3_CHECK_FLAGS( X, F ) (((X)&(F)) == (F))

// провер€ет есть ли справа или слева углы
#define P3_CHECK_CORNERS( X ) ((X)&(P3_PLAYERSTATE_CORNERLEFT|P3_PLAYERSTATE_CORNERRIGHT))

// справа и слева нет углов
#define P3_CHECK_MIDDLE( X ) (!P3_CHECK_CORNERS( X ))

extern ConVar cv_topcolor;
extern ConVar cv_bottomcolor;
extern ConVar cl_himodels;
extern ConVar cl_crosshairusealpha;
extern ConVar cl_crosshaircolor;
extern ConVar cl_crosshairscale;
extern ConVar cl_crosshair_red;
extern ConVar cl_crosshair_green;
extern ConVar cl_crosshair_blue;
extern ConVar cl_crosshair_scale;
extern ConVar cl_crosshair_file;

// функции
CP3_Player* P3_GetPlayer();
CP3_Player* ToP3Player( CBaseEntity* ent );

// Postal 3 Achievements
#define ACHIEVEMENT_P3_CHAMP_WISPERER						1
#define ACHIEVEMENT_P3_CAT_WRANGLER							2
#define ACHIEVEMENT_P3_ENTOMOLOGIST							3
#define ACHIEVEMENT_P3_ARSONIST								4
#define ACHIEVEMENT_P3_DANNY_TREJO							5
#define ACHIEVEMENT_P3_PSYCHO_DUNDEE						6
#define ACHIEVEMENT_P3_MEGA_SADIST							7
#define ACHIEVEMENT_P3_NFL_DRAFT_PICK						8
#define ACHIEVEMENT_P3_CULTURE_WARRIOR						9
#define ACHIEVEMENT_P3_REAL_AMERICAN						10
#define ACHIEVEMENT_P3_CAMELBACK							11
#define ACHIEVEMENT_P3_PERSONAL_JESUS						12
#define ACHIEVEMENT_P3_SUCKTASTIC							13
#define ACHIEVEMENT_P3_DONT_TAZE_ME_BRO						14
#define ACHIEVEMENT_P3_T_J_HOOKER							15
#define ACHIEVEMENT_P3_BIPOLAR								16
#define ACHIEVEMENT_P3_NEUROSURGEON							17
#define ACHIEVEMENT_P3_PETA_CHAIRMAN						18
#define ACHIEVEMENT_P3_AINT_GOT_TIME_TO_BLEED				19
#define ACHIEVEMENT_P3_SCHWARZENEGGER						20
#define ACHIEVEMENT_P3_STALLONE								21
#define ACHIEVEMENT_P3_EASTWOOD								22
#define ACHIEVEMENT_P3_I_AM_THE_LAW							23
#define ACHIEVEMENT_P3_GUMP									24
#define ACHIEVEMENT_P3_KAVORKIAN							25
#define ACHIEVEMENT_P3_JACK_THOMPSON_WAS_RIGHT				26
#define ACHIEVEMENT_P3_EMO									27
#define ACHIEVEMENT_P3_DADDY_NEVER_LOVED_ME					28
#define ACHIEVEMENT_P3_MISOGYNIST							29
#define ACHIEVEMENT_P3_WOLVERINES_R_GHEY					30
#define ACHIEVEMENT_P3_TOYOTA_RECALL						31
#define ACHIEVEMENT_P3_PROPERTY_DAMAGE						32
#define ACHIEVEMENT_P3_THERE_IS_NO_SPOON					33
#define ACHIEVEMENT_P3_FAIL_ZOMBIE							34
#define ACHIEVEMENT_P3_COPROPHILIAC							35
#define ACHIEVEMENT_P3_PDB_FINISH							36
#define ACHIEVEMENT_P3_CM_FINISH							37
#define ACHIEVEMENT_P3_GR__FINISH							38
#define ACHIEVEMENT_P3_ML__FINISH							39
#define ACHIEVEMENT_P3_AA2_FINISH							40
#define ACHIEVEMENT_P3_PWAC_FINISH							41
#define ACHIEVEMENT_P3_SRM_FINISH							42
#define ACHIEVEMENT_P3_ZHQA_FINISH							43
#define ACHIEVEMENT_P3_DLG_FINISH							44
#define ACHIEVEMENT_P3_BDK_FINISH							45
#define ACHIEVEMENT_P3_ASTRONAUT							46
#define ACHIEVEMENT_P3_BAD_COP								47
#define ACHIEVEMENT_P3_SPECIAL_OLYMPIAN						48
#define ACHIEVEMENT_P3_CURIOUS_BASTARD						49


#define PEEWEE_SPEED 0.3f

enum EBodyState
{
	BS_NORMAL,
	BS_RAGDOLL,
	BS_CORPSE,
};

enum PlayerSkin
{
	PS_DUDE,
	PS_AGENT,
	PS_COP,
	PS_SECURITY,
	PS_PRISONER,
	PS_SWAT,
	PS_KROTCHY,
	PS_JANITOR,
	PS_PATROL,
};

#endif	// P3_PLAYER_SHARED_H