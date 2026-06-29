#ifndef P3_FLUIDS_EVENTS
#define P3_FLUIDS_EVENTS

#ifdef _WIN32
#pragma once
#endif

#include "p3_fluids_shared.h"
#include "igameevents.h"


//-----------------------------------------------------------------------------
// P3_FluidsEventHandler -- интерфейс для посылки или приема сообщений
//-----------------------------------------------------------------------------

abstract_class P3_FluidsEventHandler
{
public:
	virtual void	OnSetEnvLightOrigin( const Vector& origin ) = 0;
	virtual void	OnFluidAdded( int id, P3_Fluid& fluid, float amount, float velocity ) = 0;
	virtual void	OnFluidDeleted( int id ) = 0;
	virtual void	OnFluidFiredUp( int id, float time, float duration ) = 0;
	virtual void	OnFluidExtinguished( int id ) = 0;
};

//-----------------------------------------------------------------------------
// P3_FluidsEventCreator -- создает и посылает события на сервере
//-----------------------------------------------------------------------------

class P3_FluidsEventCreator : public P3_FluidsEventHandler
{
public:
	virtual void	OnSetEnvLightOrigin( const Vector& origin );

	virtual void	OnFluidAdded( int id, P3_Fluid& fluid, float amount = 0, float velocity = 0 );
	virtual void	OnFluidDeleted( int id );
	virtual void	OnFluidFiredUp( int id, float time, float duration );
	virtual void	OnFluidExtinguished( int id );
};

//-----------------------------------------------------------------------------
// P3_FluidsEventListener -- реагирует на события, происходящие на сервере
//-----------------------------------------------------------------------------

class P3_FluidsEventListener
	: public P3_FluidsEventHandler
	, public IGameEventListener2
{
public:
	P3_FluidsEventListener();
	virtual ~P3_FluidsEventListener();

	virtual void	StartListen();
	virtual void	StopListen();

	// IGameEventListener2
	virtual void	FireGameEvent( IGameEvent* event );
};


#endif	// P3_FLUIDS_EVENTS
