#ifndef P3_FSM_STUFFITEM_SHARED
#define P3_FSM_STUFFITEM_SHARED
#pragma once

enum P3_FsmItemType
{
	P3_FSMITEMTYPE_GENERIC = 0,
	P3_FSMITEMTYPE_PIZZA,
	P3_FSMITEMTYPE_BONE,
	P3_FSMITEMTYPE_SHIT,
	P3_FSMITEMTYPE_DROCHA,
	P3_FSMITEMTYPE_MONKEY,
	P3_FSMITEMTYPE_CATNIP,
	P3_FSMITEMTYPE_SEED,
	P3_FSMITEMTYPE_PAINTBUCKET,		// ведро с краской
	P3_FSMITEMTYPE_PAINT,			// сама краска
	P3_FSMITEMTYPE_ACID,			// сама кислота

	NUM_P3_FSMITEMTYPES
};

struct P3_FsmItemDesc
{
	bool			usable;			// можно класть в инвентарь

	const char*		icon;			// имя иконки
	const char*		iconSelected;	// имя иконки когда айтем выбран

	const char*		idleEffect;		// просто эффект
	const char*		flyEffect;		// эффект в полете
	const char*		splashEffect;	// эффект убивания апстену в полете
	const char*		splashDecal;	// декаль от убивания апстену
	float			splashDuration;	// время жизни splashEffect
};

const P3_FsmItemDesc& P3_GetFsmItemDesc( P3_FsmItemType type );
void P3_PrecacheFsmItem( P3_FsmItemType type );

#endif // P3_FSM_STUFFITEM_SHARED
