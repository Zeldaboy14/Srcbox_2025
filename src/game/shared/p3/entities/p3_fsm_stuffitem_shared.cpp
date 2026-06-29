#include "cbase.h"

#include "p3_fsm_stuffitem_shared.h"


const static P3_FsmItemDesc g_FsmItemDescs[] =
{
	{ false, "generic", "generic_s", NULL, NULL, NULL, NULL, 0 },
	{ true, "pizza", "pizza_s", NULL, NULL, NULL, NULL, 0 },
	{ true, "bone", "bone_s", NULL, NULL, NULL, NULL, 0 },
	{ false, NULL, NULL, "Shit_Steam_Fx", "ShopVac_Ammo_Shit_Fly_FX", "ShopVac_Ammo_Shit_Splash_FX", "DogShit", 3 },
	{ false, NULL, NULL, NULL, "ShopVac_Ammo_Fly_Fx", "ShopVac_Ammo_Splash_Fx", "ShopVacAmmoSplash", 3 },
	{ true, "monkey", "monkey_s", NULL, NULL, NULL, NULL, 0 },
	{ false, NULL, NULL, "catnip_ground_01_fx", NULL, NULL, NULL, 0 },
	{ false, NULL, NULL, NULL, NULL, NULL, NULL, 0 },
	{ false, NULL, NULL, NULL, NULL, NULL, NULL, 0 },
	{ false, NULL, NULL, NULL, "Paint_Fly_Fx", "Paint_Splash_Fx", "PaintSplash", 3 },
	{ false, NULL, NULL, NULL, "Acid_Fly_Fx", "Acid_Splash_Fx", "AcidSplash", 3 },
};

const P3_FsmItemDesc& P3_GetFsmItemDesc( P3_FsmItemType type )
{
	Assert( SIZE_OF_ARRAY( g_FsmItemDescs ) == NUM_P3_FSMITEMTYPES );
	Assert( type >= 0 && type < NUM_P3_FSMITEMTYPES );

	if ( type >= 0 && type < NUM_P3_FSMITEMTYPES )
	{
		return g_FsmItemDescs[type];
	}
	else
	{
		return g_FsmItemDescs[0];
	}
}

void P3_PrecacheFsmItem( P3_FsmItemType type )
{
	const P3_FsmItemDesc& desc = P3_GetFsmItemDesc( type );

	if ( desc.idleEffect )
	{
		PrecacheParticleSystem( desc.idleEffect );
	}

	if ( desc.flyEffect )
	{
		PrecacheParticleSystem( desc.flyEffect );

		if ( desc.splashEffect )
		{
			PrecacheParticleSystem( desc.splashEffect );
		}
		if ( desc.splashDecal )
		{
			UTIL_PrecacheDecal( desc.splashDecal );
		}
	}
}
