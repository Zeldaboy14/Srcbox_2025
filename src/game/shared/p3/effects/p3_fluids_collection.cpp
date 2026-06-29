#include "cbase.h"
#include "p3_fluids_collection.h"
#include "p3_fluids_vars.h"

#include "debugoverlay_shared.h"

#ifndef CLIENT_DLL
#include "particle_system.h"

#include "util.h"
#endif

#include <tier0/vprof.h>

// memdbgon must be the last include file in a .cpp file!!!
#include <tier0/memdbgon.h>


//-----------------------------------------------------------------------------
// P3_FluidCollection
//-----------------------------------------------------------------------------

void
P3_FluidsCollection::Init( Vector wmins, Vector wmaxs )
{
	m_FluidGraph.Init( wmins.AsVector2D(), wmaxs.AsVector2D() );
}

P3_Fluid
P3_FluidsCollection::ProbeFluid( const Vector& position, float radius ) const
{
	CUtlVector< int > fluids;

	m_FluidGrid.Find( position.AsVector2D(), radius, fluids );

	P3_Fluid result;

	for ( int i = 0; i < fluids.Count(); i++ )
	{
		result.Merge( m_Fluids[i] );
	}

	return result;
}

void
P3_FluidsCollection::FindIntersectingFluids( const Vector& position, CUtlVector< int > &result ) const
{
	VPROF_BUDGET( "P3_FluidsCollection::FindIntersectingFluids", VPROF_BUDGETGROUP_FLUIDS_COLLECTION );

	m_FluidGrid.Find( position.AsVector2D(), FluidGrid_t::Size(), result );
}

void
P3_FluidsCollection::FindIntersectingFluids( const Vector& position, float radius, CUtlVector< int > &result ) const
{
	VPROF_BUDGET( "P3_FluidsCollection::FindIntersectingFluids", VPROF_BUDGETGROUP_FLUIDS_COLLECTION );

	m_FluidGrid.Find( position.AsVector2D(), radius, result );
}

void
P3_FluidsCollection::FindNeighbours( int index, CUtlVector< int > &result ) const
{
	VPROF_BUDGET( "P3_FluidsCollection::FindNeighbours", VPROF_BUDGETGROUP_FLUIDS_COLLECTION );

	P3_ASSERT_FLUID_INDEX( index );
	Assert( m_Fluids[ index ].IsActive() );

	const P3_Fluid &fluid = m_Fluids[ index ];

	Assert( fluid.graph_index >= 0 );
	if ( fluid.graph_index < 0 )
		return;

	const CUtlDelaunayGraph::IndexGraph_t &graph = m_FluidGraph.IndexGraph();
	const CUtlDelaunayGraph::Nodes_t &neighbours = graph[ graph.Find( fluid.graph_index ) ];

	FOR_EACH_VEC( neighbours, i )
	{
		int ni = neighbours[ i ];

		if ( ni < 4 ) continue; // skip corner points.
		Assert( m_GraphToFluid[ ni - 4 ] != -1 );

		const P3_Fluid &neighbour = m_Fluids[ m_GraphToFluid[ ni - 4 ] ];

		Assert( neighbour.graph_index == ni );

		float d = fluid.GetRadius() + neighbour.GetRadius();
		if ( (fluid.origin - neighbour.origin).LengthSqr() < d*d )
		{
			result.AddToTail( neighbour.index );
		}
	}
}
