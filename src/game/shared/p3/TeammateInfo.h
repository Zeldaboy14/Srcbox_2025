#ifndef TEAMMATE_INFO_H
#define TEAMMATE_INFO_H

#ifdef CLIENT_DLL
#include "c_baseentity.h"
#define  VAR(T, name) T name
#else
#include "baseentity.h"
#include "networkvar.h"
#define  VAR(T, name) CNetworkVar(T, name)
#endif

class CTeammateInfo
{
public:
	DECLARE_CLASS_NOBASE( CTeammateInfo );
#ifndef CLIENT_DLL
	DECLARE_EMBEDDED_NETWORKVAR();
#endif

	VAR(EHANDLE,	 teammate);
	VAR(int,		 health);
	VAR(int,		 maxHealth);
};

#endif	// P3_PLAYER_SHARED_H