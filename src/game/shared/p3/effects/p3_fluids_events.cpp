#include "cbase.h"
#include "p3_fluids_events.h"

#include "p3_fluids_vars.h"
#include "utils/p3_registry.h"

#include "materialsystem/imaterialsystem.h"

#ifndef CLIENT_DLL
#include "particle_system.h"

#include "util.h"
#endif

// memdbgon must be the last include file in a .cpp file!!!
#include <tier0/memdbgon.h>


//-----------------------------------------------------------------------------
// P3_FluidsEventHandler
//-----------------------------------------------------------------------------

#define P3_FLUIDS_EVENT_SETENVLIGHTORIGIN	"fluids_set_envlightorigin"
#define P3_FLUIDS_EVENT_ADD					"fluids_add"
#define P3_FLUIDS_EVENT_DELETE				"fluids_delete"
#define P3_FLUIDS_EVENT_AMOUNTCHANGED		"fluids_amountchanged"
#define P3_FLUIDS_EVENT_FIREDUP				"fluids_firedup"
#define P3_FLUIDS_EVENT_EXTINGUISHED		"fluids_extinguished"

//-----------------------------------------------------------------------------
// P3_FluidsEventCreator
//-----------------------------------------------------------------------------

const char* Vec2Str( const Vector& v )
{
	static char buf[64];
	Q_snprintf( buf, sizeof buf, "%f %f %f", v.x, v.y, v.z );
	return buf;
}

void P3_FluidsEventCreator::OnSetEnvLightOrigin( const Vector& origin )
{
	IGameEvent* event = gameeventmanager->CreateEvent( P3_FLUIDS_EVENT_SETENVLIGHTORIGIN );
	if ( event )
	{
		event->SetString( "origin", Vec2Str( origin ) );

		gameeventmanager->FireEvent( event );
	}
}

void P3_FluidsEventCreator::OnFluidAdded( int id, P3_Fluid& fluid, float amount, float velocity )
{
	IGameEvent* event = gameeventmanager->CreateEvent( P3_FLUIDS_EVENT_ADD );
	if ( event )
	{
		event->SetInt( "fluidindex", id );
		event->SetInt( "type", fluid.type );
		event->SetInt( "category", fluid.category );
		event->SetInt( "flags", fluid.flags );
		event->SetFloat( "amount", fluid.GetAmount() );
		event->SetFloat( "vel", fluid.GetVelocity() );
		event->SetString( "origin", Vec2Str( fluid.origin ) );
		event->SetString( "normal", Vec2Str( fluid.normal ) );
		event->SetBool( "blackspot", fluid.bShowBlackSpot );

		gameeventmanager->FireEvent( event );
	}
}

void P3_FluidsEventCreator::OnFluidDeleted( int id )
{
	IGameEvent* event = gameeventmanager->CreateEvent( P3_FLUIDS_EVENT_DELETE );
	if ( event )
	{
		event->SetInt( "fluidindex", id );

		gameeventmanager->FireEvent( event );
	}
}

void P3_FluidsEventCreator::OnFluidFiredUp( int id, float time, float duration )
{
	IGameEvent* event = gameeventmanager->CreateEvent( P3_FLUIDS_EVENT_FIREDUP );
	if ( event )
	{
		event->SetInt( "fluidindex", id );
		event->SetFloat( "time", time );
		event->SetFloat( "duration", duration );

		gameeventmanager->FireEvent( event );
	}
}

void P3_FluidsEventCreator::OnFluidExtinguished( int id )
{
	IGameEvent* event = gameeventmanager->CreateEvent( P3_FLUIDS_EVENT_EXTINGUISHED );
	if ( event )
	{
		event->SetInt( "fluidindex", id );

		gameeventmanager->FireEvent( event );
	}
}

//-----------------------------------------------------------------------------
// P3_FluidsEventListener
//-----------------------------------------------------------------------------

P3_FluidsEventListener::P3_FluidsEventListener()
{
	
}

P3_FluidsEventListener::~P3_FluidsEventListener()
{
	StopListen();
}

void P3_FluidsEventListener::StartListen()
{
#ifdef CLIENT_DLL
	bool serverSide = false;
#else
	bool serverSide = true;
#endif

	gameeventmanager->AddListener( this, P3_FLUIDS_EVENT_SETENVLIGHTORIGIN, serverSide );
	gameeventmanager->AddListener( this, P3_FLUIDS_EVENT_ADD, serverSide );
	gameeventmanager->AddListener( this, P3_FLUIDS_EVENT_DELETE, serverSide );
	gameeventmanager->AddListener( this, P3_FLUIDS_EVENT_FIREDUP, serverSide );
	gameeventmanager->AddListener( this, P3_FLUIDS_EVENT_EXTINGUISHED, serverSide );
}

void P3_FluidsEventListener::StopListen()
{
	if ( gameeventmanager )
	{
		gameeventmanager->RemoveListener( this );
	}
}

void P3_FluidsEventListener::FireGameEvent( IGameEvent* event )
{
	const char* name = event->GetName();
	if ( Q_strncmp( name, "fluids_", 7 ) )
	{
		return;
	}

	const char* command = &name[7];
	if ( !Q_strcmp( command, "set_envlightorigin" ) )
	{
		Vector origin;
		UTIL_StringToVector( origin.Base(), event->GetString( "origin" ) );

		OnSetEnvLightOrigin( origin );
	}
	else if ( !Q_strcmp( command, "add" ) )
	{
		P3_Fluid fluid;

		int id = event->GetInt( "fluidindex" );
		int type = event->GetInt( "type" );
		int category = event->GetInt( "category" );

		fluid.type = ( P3_FluidType )type;
		fluid.category = ( P3_FluidCategory )category;
		fluid.flags = event->GetInt( "flags" );
		fluid.bShowBlackSpot = event->GetBool( "blackspot", true );
		UTIL_StringToVector( fluid.origin.Base(), event->GetString( "origin" ) );
		UTIL_StringToVector( fluid.normal.Base(), event->GetString( "normal" ) );

		OnFluidAdded( id, fluid, event->GetFloat( "amount" ), event->GetFloat( "vel" ) );
	}
	else if ( !Q_strcmp( command, "delete" ) )
	{
		int id = event->GetInt( "fluidindex" );
		
		OnFluidDeleted( id );
	}
	else if ( !Q_strcmp( command, "firedup" ) )
	{
		int id = event->GetInt( "fluidindex" );
		float time = event->GetFloat( "time" );
		float duration = event->GetFloat( "duration" );

		OnFluidFiredUp( id, time, duration );
	}
	else if ( !Q_strcmp( command, "extinguished" ) )
	{
		int id = event->GetInt( "fluidindex" );

		OnFluidExtinguished( id );
	}
	else
	{
		DevWarning( "Unknown fluids event: %s\n", name );
	}
}
