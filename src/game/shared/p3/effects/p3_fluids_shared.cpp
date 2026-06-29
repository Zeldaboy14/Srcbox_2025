#include "cbase.h"
#include "p3_fluids_shared.h"

#include "p3_fluids_collection.h"
#include "p3_fluids_vars.h"

#include "utils/p3_registry.h"

#include "materialsystem/imaterialsystem.h"

// memdbgon must be the last include file in a .cpp file!!!
#include <tier0/memdbgon.h>


P3_FluidCategory P3_GetFluidCategory( const Vector &normal )
{
	float angle = acosf( DotProduct( normal, Vector( 0, 0, 1 ) ) );

	if ( angle < M_PI/4 )
	{
		return P3_FLUID_CATEGORY_GROUND;
	}
	else if ( angle < M_PI/2 + M_PI/8 )
	{
		return P3_FLUID_CATEGORY_WALL;
	}

	return P3_FLUID_CATEGORY_CEILING;
}

//-----------------------------------------------------------------------------
// P3_Fluid
//-----------------------------------------------------------------------------

P3_Fluid::P3_Fluid()
	: index( -1 )
	, owner( NULL )
	, type( P3_FLUID_TYPE_ERROR )
	, category( P3_FLUID_CATEGORY_ERROR )
	, flags( 0 )
	, maxRadius( 0 )
	, alpha( 1.f )
	, xs( 0 )
	, ys( 0 )
	, ignitionTime( 0 )
	, rndBumpAngle( 0 )
	, bDirCalculated( false )
#ifndef CLIENT_DLL
	, burnStartTime( 0 )
	, burnDuration( 0 )
#endif
	, graph_index( -1 )
{
	effects[ 0 ] = 0;
	effects[ 1 ] = 0;
}

P3_Fluid::P3_Fluid( P3_FluidType type, const Vector &origin, const Vector &normal )
	: index( -1 )
	, owner( NULL )
	, type( type )
	, category( P3_GetFluidCategory( normal ) )
	, flags( 0 )
	, origin( origin )
	, normal( normal )
	, maxRadius( 0 )
	, alpha( 1.f )
	, xs( 0 )
	, ys( 0 )
	, ignitionTime( 0 )
	, rndBumpAngle( 0 )
	, bDirCalculated( false )
#ifndef CLIENT_DLL
	, burnStartTime( 0 )
	, burnDuration( 0 )
#endif
	, graph_index( -1 )
{
	effects[ 0 ] = 0;
	effects[ 1 ] = 0;
}

P3_Fluid::~P3_Fluid()
{
}

void
P3_Fluid::Merge( const P3_Fluid &f )
{
	type = min( type, f.type );
	flags |= f.flags;
}

void
P3_Fluid::Update( float dt )
{
	const P3_FluidDesc& desc = P3_GetFluidDesc( type );
	
	maxRadius = P3_CalcFluidRadius( GetAmount() );
	maxRadius = min( maxRadius, desc.maxRadius );

	float amount = GetAmount();
	float radius = GetRadius();
	float velocity = GetVelocity();
	float t1 = dt * desc.spreadSpeed;
	if( t1 > 1 )
		t1 = 1;

	if ( radius >= maxRadius )
	{
		SetRadius( maxRadius );
	}
	else
	{
		SetRadius( radius * ( 1 - t1 ) + maxRadius * t1 );
	}


	float curVelocity = velocity * t1;
	float moveStep = curVelocity * dt;// 20 / radius;
	SetVelocity( curVelocity );
	xs += moveStep;
	ys += moveStep;


	if ( amount > 600 )
	{
		SetAmount( amount - 100 * dt );
	}


	if ( IsFlammable() )
	{
		if ( IsBurning() )
		{
			alpha -= dt / p3_gasoline_burn_time.GetFloat();
			if( alpha < 0.01f )
				alpha = 0.01f;
		}
		if ( IsTakingFire() )
		{
			if ( ignitionTime > p3_fluid_spread_speed.GetFloat() )
			{
				flags &= ~P3_FLUID_FLAG_TAKING_FIRE;
				flags |= P3_FLUID_FLAG_READY_TO_BURN;
			}
			ignitionTime += dt;
		}
	}
	else
	{
		flags &= ~P3_FLUID_FLAG_TAKING_FIRE;
	}
}

float
P3_Fluid::GetAmount() const
{
	return owner->GetAmount( index );
}

float
P3_Fluid::GetVelocity() const
{
	return owner->GetVelocity( index );
}

float
P3_Fluid::GetRadius() const
{
	return owner->GetRadius( index );
}

void
P3_Fluid::DrawDebugOveralay()
{
#ifndef CLIENT_DLL
	QAngle angles;
	VectorAngles( normal, angles );
	angles[PITCH] += 90;
	Color c = IsBurning() ? Color(0,255,0) :
			IsFlammable() ? Color(255,0,0) :
							Color(0,0,255);

	Vector nend = Vector( origin.AsVector2D() ) + 10*normal;
	NDebugOverlay::VertArrow( Vector( origin.AsVector2D() ), nend, 1, c.r(), c.g(), c.b(), 150, false, g_DebugTimer.Duration() );
	NDebugOverlay::VertArrow( nend, nend + 10*dir, 1, c.r(), c.g(), c.b(), 150, false, g_DebugTimer.Duration() );

	char buf[255];
	Q_snprintf( buf, 255, "%d", index );
	NDebugOverlay::Text( Vector( origin.AsVector2D() ), buf, true, g_DebugTimer.Duration() );
#endif
}

//-----------------------------------------------------------------------------
// P3_FluidsIndices -- массив с индексами
//-----------------------------------------------------------------------------

P3_FluidsIndices::P3_FluidsIndices()
	: m_count( 0 )
{
}

int P3_FluidsIndices::Add( int index )
{
	P3_ASSERT_FLUID_INDEX( index );

	int i = m_count;
	m_indices[i] = index;
	m_count++;

	return i;
}

void P3_FluidsIndices::Remove( int index )
{
	P3_ASSERT_FLUID_INDEX( index );

	int i = Find( index );
	if ( i != -1 )
	{
		m_count--;
		if ( i != m_count )
		{
			size_t bytes = ( m_count - i ) * sizeof( int );
			memcpy( &m_indices[i], &m_indices[i+1], bytes );
		}
	}
}

int P3_FluidsIndices::Find( int index ) const
{
	for ( int i = 0; i < m_count; i++ )
	{
		if ( m_indices[i] == index )
		{
			return i;
		}
	}

	return -1;
}

int P3_FluidsIndices::Index( int i ) const
{
	Assert( m_count );
	Assert( i >= 0 && i < m_count );

	return m_indices[i];
}

int P3_FluidsIndices::Index( int i )
{
	Assert( m_count );
	Assert( i >= 0 && i < m_count );

	return m_indices[i];
}

//-----------------------------------------------------------------------------
// Настройки
//-----------------------------------------------------------------------------

static P3_FluidDesc g_FluidDescs[P3_FLUID_NUM_TYPES];

//-----------------------------------------------------------------------------
// Функции
//-----------------------------------------------------------------------------

void P3_InitFluidDescs()
{
	g_FluidDescs[P3_FLUID_TYPE_WEE].texture			= "effects/wee_splash";
	g_FluidDescs[P3_FLUID_TYPE_WEE].decal			= "beersplash";
	g_FluidDescs[P3_FLUID_TYPE_WEE].spreadSpeed		= P3_Registry_GetFloat( "wee_spreadspeed" );
	g_FluidDescs[P3_FLUID_TYPE_WEE].maxRadius		= P3_Registry_GetFloat( "wee_maxradius" );
	g_FluidDescs[P3_FLUID_TYPE_WEE].flammable		= false;

	g_FluidDescs[P3_FLUID_TYPE_PUKE].texture		= "effects/puke_splash";
	g_FluidDescs[P3_FLUID_TYPE_PUKE].decal			= "pukesplash";
	g_FluidDescs[P3_FLUID_TYPE_PUKE].spreadSpeed	= P3_Registry_GetFloat( "puke_spreadspeed" );
	g_FluidDescs[P3_FLUID_TYPE_PUKE].maxRadius		= P3_Registry_GetFloat( "puke_maxradius" );
	g_FluidDescs[P3_FLUID_TYPE_PUKE].flammable		= false;

	g_FluidDescs[P3_FLUID_TYPE_GASOLINE].texture	= "effects/gasoline_splash";
	g_FluidDescs[P3_FLUID_TYPE_GASOLINE].decal		= "gasolinesplash";
	g_FluidDescs[P3_FLUID_TYPE_GASOLINE].spreadSpeed = P3_Registry_GetFloat( "gasoline_spreadspeed" );
	g_FluidDescs[P3_FLUID_TYPE_GASOLINE].maxRadius	= P3_Registry_GetFloat( "gasoline_maxradius" );
	g_FluidDescs[P3_FLUID_TYPE_GASOLINE].flammable	= true;

	for ( int i = 0; i < P3_FLUID_NUM_TYPES; i++ )
	{
		PrecacheMaterial( g_FluidDescs[i].texture );
		UTIL_PrecacheDecal( g_FluidDescs[i].decal );

		g_FluidDescs[i].material = materials->FindMaterial( g_FluidDescs[i].texture, NULL, false );
	}

	PrecacheMaterial( "effects/gasoline_splash_bg" );
}

const P3_FluidDesc& P3_GetFluidDesc( int type )
{
	Assert( type >= 0 && type <= P3_FLUID_NUM_TYPES );
	return g_FluidDescs[type];
}

float P3_CalcFluidRadius( float amount )
{
	// S = PI*R^2    ==>    R = SQRT(S/PI)
	return sqrtf( P3_CalcFluidRadius2( amount ) );
}

float P3_CalcFluidRadius2( float amount )
{
	// S = PI*R^2    ==>    R = SQRT(S/PI)
	return amount / M_PI;
}
