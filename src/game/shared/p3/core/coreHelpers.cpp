//--------------------------------------------------------------------------------------------------
// Автор    : Пятышев Иван, 2006
// Описание : Вспомогательные функции для работы с объектами Source
//--------------------------------------------------------------------------------------------------

#include "cbase.h"
#include "coreHelpers.h"

namespace core
{

//--------------------------------------------------------------------------------------------------
// CHelpers
//--------------------------------------------------------------------------------------------------
// Найти поле по внешнему имени (например, это "Tick" в случае DEFINE_INPUTFUNC( FIELD_FLOAT, "Tick", InputTick )

typedescription_t* CHelpers::FindField(CBaseEntity* pEnt, const char* szExternalName)
{
	// loop through the data description list, restoring each data desc block
	for ( datamap_t *dmap = pEnt->GetDataDescMap(); dmap != NULL; dmap = dmap->baseMap )
	{
		// search through all the readable fields in the data description, looking for a match
		for ( int i = 0; i < dmap->dataNumFields; i++ )
		{
			typedescription_t& cur = dmap->dataDesc[i];
			//if ( dmap->dataDesc[i].flags & (FTYPEDESC_OUTPUT | FTYPEDESC_KEY) )
			{
				if (cur.externalName != 0)
				{
					if ( !stricmp(cur.externalName, szExternalName))
					{
						return &cur;
					}
				}
			}
		}
	}

	return 0;
}

//--------------------------------------------------------------------------------------------------

}