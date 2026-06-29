#ifndef P3_FLUIDS_COLLECTION
#define P3_FLUIDS_COLLECTION

#ifdef _WIN32
#pragma once
#endif

#include "p3_fluids_shared.h"

#include "utils/utldebugtimer.h"
#include "utils/utldelaunaygraph.h"
#include "utils/utlgrid.h"
#include "utils/utlindexbuffer.h"


class CParticleSystem;
class CP3_FluidsManager;
class C_P3_FluidsManager;
class CP3_FluidsCollectionNode;
class C_P3_FluidsCollectionNode;


//-----------------------------------------------------------------------------
// P3_FluidsCollection -- хранилище всех жидкостей
//-----------------------------------------------------------------------------

class P3_FluidsCollection
{
	typedef CUtlGrid< 5 > FluidGrid_t;

public:
	struct fluid_operation {
		enum op_e {
			add,
			remove,
		} op;
		int index;
		Vector origin;
		fluid_operation( op_e op, int i, const Vector &origin ) : op( op ), index( i ), origin( origin ) {}
	};


public:
#if !defined( CLIENT_DLL )
	P3_FluidsCollection( CP3_FluidsManager *manager )
		: m_Manager( manager )
	{
	}
#else
	P3_FluidsCollection( C_P3_FluidsManager *manager )
		: m_Manager( manager )
	{
	}
#endif


	void			Init( Vector wmins, Vector wmaxs );

#if !defined( CLIENT_DLL )
	// emulation of index collection, so FluidsCollection can be used in FOR_EACH_INDEX
	int				MaxIndex() const { return m_FluidIndices.MaxIndex(); }
	bool			IsIndexValid( int i ) const { return m_FluidIndices.IsIndexValid( i ); }

	int				Count() const { return m_FluidIndices.Count(); }

	int				AddFluid( const P3_Fluid& fluid );
#else
	int				Count() const { return P3_FLUIDS_MAX; }

	void			AddFluid( int index, const P3_Fluid& fluid );
#endif
	void			RemoveFluid( int index );

	const CUtlVector< fluid_operation >&	FluidOperations() { return m_FluidOperations; }
//	const CUtlVector< int >&	RemovedFluids() { return m_RemovedFluids; }
	void			Flush(); // adds and removes queued fluids.

	void			Purge();

	const P3_Fluid&	operator []( int index ) const;
	P3_Fluid&		operator []( int index );

	P3_Fluid		ProbeFluid( const Vector& position, float radius ) const; // returns metafluid at position with properties sum of all fluids there.
	void			FindIntersectingFluids( const Vector& position, CUtlVector< int > &result ) const;
	void			FindIntersectingFluids( const Vector& position, float radius, CUtlVector< int > &result ) const;
	void			FindNeighbours( int index, CUtlVector< int > &result ) const;

	////!!!!////
	float			GetAmount( int index );
	float			GetVelocity( int index );
	float			GetRadius( int index );
	void			SetAmount( int index, float a );
	void			SetVelocity( int index, float v );
	void			SetRadius( int index, float r );
	////!!!!////

#if defined( CLIENT_DLL )
	const CUtlDisjointSet &GetFluidGroups();
#endif

	void			DrawDebugOverlay();


protected:
#if !defined( CLIENT_DLL )
	CP3_FluidsCollectionNode	*GetNodeByIndex( int index );
#else
	C_P3_FluidsCollectionNode	*GetNodeByIndex( int index );
#endif
	int							GetNodeIndex( int index ) const { return index % P3_FLUIDS_PER_NODE; }


private:
#if !defined( CLIENT_DLL )
	CP3_FluidsManager	*m_Manager;
#else
	C_P3_FluidsManager	*m_Manager;
#endif

#if !defined( CLIENT_DLL )
	CUtlIndexBuffer		m_FluidIndices;
#endif
	int					m_GraphToFluid[ P3_FLUIDS_MAX ];
	P3_Fluid			m_Fluids[ P3_FLUIDS_MAX ];
	CUtlDelaunayGraph	m_FluidGraph;
	FluidGrid_t			m_FluidGrid;
#if defined( CLIENT_DLL )
	CUtlDisjointSet		m_FluidGroups; // used for rendering
#endif

	CUtlVector< fluid_operation >	m_FluidOperations;


#if !defined( CLIENT_DLL )
	CUtlVector< CP3_FluidsCollectionNode * > nodes;
	friend class CP3_FluidsCollectionNode;
	friend class CP3_FluidsManager;
#else
	void AddNode( C_P3_FluidsCollectionNode *node )
	{
		nodes.AddToTail( node );
	}

	void RemoveNode( C_P3_FluidsCollectionNode *node )
	{
		nodes.FindAndRemove( node );
	}

	CUtlVector< C_P3_FluidsCollectionNode * > nodes;
	friend class C_P3_FluidsCollectionNode;
	friend class C_P3_FluidsManager;
#endif

	friend struct CP3_FluidsGroupComparator;
};

inline
const P3_Fluid &
P3_FluidsCollection::operator []( int index ) const
{
	P3_ASSERT_FLUID_INDEX( index );
	return m_Fluids[ index ];
}

inline
P3_Fluid &
P3_FluidsCollection::operator []( int index )
{
	P3_ASSERT_FLUID_INDEX( index );
	return m_Fluids[ index ];
}


struct CP3_FluidsGroupComparator
{
	CP3_FluidsGroupComparator( const P3_FluidsCollection &collection )
		: m_Collection( collection )
	{
	}

	bool skip( int i ) const
	{
		if ( i < 4 ) return true;
		return false;
	}

	bool operator()( int a, int b ) const
	{
		const P3_Fluid &fa = m_Collection[ m_Collection.m_GraphToFluid[ a - 4 ] ];
		const P3_Fluid &fb = m_Collection[ m_Collection.m_GraphToFluid[ b - 4 ] ];

		float far2 = fa.GetRadius(); far2 *= far2;
		float fbr2 = fb.GetRadius(); fbr2 *= fbr2;

		return ( fa.origin - fb.origin ).AsVector2D().LengthSqr() < ( far2 + fbr2 );
	}

private:
	const P3_FluidsCollection &m_Collection;
};

/*
struct CP3_FluidsGroupIndicator
{
	CP3_FluidsGroupIndicator( const P3_FluidsCollection &collection )
		: m_Collection( collection )
	{
	}

	bool skip( int i ) const
	{
		if ( i < 4 ) return true;
		return false;
	}

	bool operator()( int node, const CUtlVector< int > &links ) const
	{
		const P3_Fluid &fa = m_Collection[ m_Collection.m_GraphToFluid[ node - 4 ] ];

		if ( links.Count() < 3 )
		{
			return false;
		}

		FOR_EACH_VEC( links, i )
		{
			const P3_Fluid &fb = m_Collection[ m_Collection.m_GraphToFluid[ links[ i ] - 4 ] ];

			float far2 = fa.GetRadius(); far2 *= far2;
			float fbr2 = fb.GetRadius(); fbr2 *= fbr2;

			if ( ( fa.origin - fb.origin ).AsVector2D().LengthSqr() > ( far2 + fbr2 ) )
			{
				return false;
			}
		}

		return true;
	}

private:
	const P3_FluidsCollection &m_Collection;
};
*/

#endif	// P3_FLUIDS_COLLECTION
