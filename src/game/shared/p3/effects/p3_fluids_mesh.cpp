#include "cbase.h"
#include "p3_fluids_mesh.h"

#include "p3_fluids_vars.h"
#include "debugoverlay_shared.h"

#include <tier1/utllinkedlist.h>
#include <tier1/utlpriorityqueue.h>
#include <tier0/vprof.h>

#include <tier0/memdbgon.h>


void
P3_FluidsMesh::Init( const Vector &min, const Vector &max )
{
	Vector2D a( min.x, min.y );
	Vector2D b( min.x, max.y );
	Vector2D c( max.x, max.y );
	Vector2D d( max.x, min.y );

	int ia = AddPoint( a );
	int ib = AddPoint( b );
	int ic = AddPoint( c );
	int id = AddPoint( d );

	Vector2D bd( b.x - d.x, b.y - d.y );
	Vector2D ac( a.x - c.x, a.y - c.y );

	if ( bd.LengthSqr() < ac.LengthSqr() )
	{
		AddTriangle( CircumcenteredTriangleIndexed( ia, ib, id, m_Points.Base() ) );
		AddTriangle( CircumcenteredTriangleIndexed( ib, ic, id, m_Points.Base() ) );
	}
	else
	{
		AddTriangle( CircumcenteredTriangleIndexed( ia, ib, ic, m_Points.Base() ) );
		AddTriangle( CircumcenteredTriangleIndexed( ia, ic, id, m_Points.Base() ) );
	}
}

void
P3_FluidsMesh::Add( const P3_Fluid &fluid )
{
	VPROF_BUDGET( "FireUpFlammableFluid", _T( "Fluids Mesh" ) );

	CUtlVector< int > triangles;
	FindTriangles( fluid.origin.AsVector2D(), triangles );

	Assert( triangles.Count() > 0 );

	CUtlVector< int > convex_hull;
	BuildConvexHull( triangles, convex_hull );

	FOR_EACH_VEC( triangles, it )
	{
		RemoveTriangle( triangles[ it ] );
	}

	int ip = AddFluid( &fluid );

	Assert( ip < m_Points.Count() && ip < m_Fluids.Count() );

	for ( int i = 1; i < convex_hull.Count(); i++ )
	{
		AddTriangle( CircumcenteredTriangleIndexed( convex_hull[ i - 1 ], convex_hull[ i ], ip, m_Points.Base() ) );
	}
}

struct WEarNode
{
	static CUtlLinkedList< WeightedTriangleIndexed > *triangles;
	int index;

	WEarNode()
		: index( -1 )
	{
	}

	WEarNode( int index )
		: index( index )
	{
	}

	bool operator <( const WEarNode &rhs ) const
	{
		return (*triangles)[ index ] < (*triangles)[ rhs.index ];
	}
};

CUtlLinkedList< WeightedTriangleIndexed > *WEarNode::triangles;

void
P3_FluidsMesh::Remove( const P3_Fluid &fluid )
{
	int ip = m_Points.Find( fluid.origin.AsVector2D() );

	Assert( m_Points.IsValidIndex( ip ) );

	CUtlVector< int > triangles;
	FindTriangles( ip, triangles );

	CUtlVector< int > convex_hull;
	BuildConvexHull( triangles, convex_hull, ip );

	Assert( convex_hull.Count() > 0 );

	int i1 = convex_hull[ 1 ];
	convex_hull.AddToTail( i1 );

	CUtlLinkedLoop< WeightedTriangleIndexed > ears( 0, convex_hull.Count() );
	WEarNode::triangles = &ears;
	CUtlPriorityQueue< WEarNode > wears;
	wears.SetLessFunc( DefLessFunc( WEarNode ) );
	for ( int i = 0; i < convex_hull.Count() - 2; i++ )
	{
		int iear =
		ears.AddToTail( WeightedTriangleIndexed( convex_hull[ i ], convex_hull[ i + 1 ], convex_hull[ i + 2 ]
		                                       , CalculatePower( ip, convex_hull[ i ], convex_hull[ i + 1 ], convex_hull[ i + 2 ] ) ) );
		wears.Insert( WEarNode( iear ) );
	}

	FOR_EACH_VEC( triangles, it )
	{
		RemoveTriangle( triangles[ it ] );
	}
	RemoveFluid( ip );

	while ( wears.Count() > 3 )
	{
		const WEarNode &node = wears.ElementAtHead();

		int piear = ears.Previous( node.index );
		int niear = ears.Next( node.index );
		ears[ niear ].A = ears[ node.index ].A;
		ears[ piear ].C = ears[ node.index ].C;
		AddTriangle( CircumcenteredTriangleIndexed( ears[ node.index ], m_Points.Base() ) );

		ears.Remove( node.index );
		wears.FastRemoveAtHead();
		WeightedTriangleIndexed &p = ears[ piear ]; p.W = CalculatePower( ip, p.A, p.B, p.C );
		WeightedTriangleIndexed &n = ears[ niear ]; n.W = CalculatePower( ip, n.A, n.B, n.C );
		wears.Rebalance();
	}

	const WEarNode &node = wears.ElementAtHead();
	AddTriangle( CircumcenteredTriangleIndexed( ears[ node.index ], m_Points.Base() ) );
}

void
P3_FluidsMesh::Purge()
{
	m_FluidIndices.Purge();
	m_TriangleIndices.Purge();
	m_Fluids.Purge();
	m_Points.Purge();
	m_Triangles.Purge();
	m_Graph.Purge();
}

void
P3_FluidsMesh::DrawDebugOverlay()
{
	FOR_EACH_NODE_L( m_Graph, in )
	{
		int i = m_Graph.Node( in );

		if ( m_Fluids[ i ] )
		{
			const P3_Fluid &fluid = *m_Fluids[ i ];

			QAngle angles;
			VectorAngles( fluid.normal, angles );
			angles[PITCH] += 90;
			Color c = fluid.IsBurning() ? Color(255,0,0) :
					fluid.IsFlammable() ? Color(255,0,0) :
										  Color(0,0,255);

			Vector nend = fluid.origin + 10*fluid.normal;
			NDebugOverlay::VertArrow( fluid.origin, nend, 1, c.r(), c.g(), c.b(), 150, false, 1.f );
			NDebugOverlay::VertArrow( nend, nend + 10*fluid.dir, 1, c.r(), c.g(), c.b(), 150, false, 1.f );
		}

		char buf[ 255 ];
		Q_snprintf( buf, 255, "%d", i );
		NDebugOverlay::Text( Vector( m_Points[ i ] ) + Vector( 0, 0, 5 ), buf, false, 1.f );

		DelaunayGraph_t::Nodes_t nodes = m_Graph[ i ];
		FOR_EACH_VEC( nodes, iln )
		{
			NDebugOverlay::Line( Vector( m_Points[ i ] ), Vector( m_Points[ nodes[ iln ] ] ), 255, 0, 0, true, 1.f );
		}
	}

	FOR_EACH_VEC_INDEXED( m_Triangles, m_TriangleIndices, it )
	{
		const CircumcenteredTriangleIndexed &tri = m_Triangles[ it ];

		char buf[ 255 ];
		Q_snprintf( buf, 255, "%d.%d.%d", tri.A, tri.B, tri.C );

		NDebugOverlay::Circle( Vector( tri.CC ), Vector( 1, 0, 0 ), Vector( 0, 1, 0 ) , tri.r, 0, 0, 200, 200, false, 0 );
		NDebugOverlay::Text( Vector( tri.CC ), buf, false, 0 );
	}
}

int
P3_FluidsMesh::AddFluid( const P3_Fluid *fluid )
{
	int i = m_FluidIndices.AddIndex();
	m_Fluids.EnsureCount( i + 1 );
	m_Points.EnsureCount( i + 1 );
	m_Fluids[ i ] = fluid;
	m_Points[ i ] = fluid->origin.AsVector2D();
	m_Graph.Add( i );

#ifdef LOG_P3_FLUID_MESH
	Msg( "AddFluid( %d )\n", i );
#endif

	return i;
}

int
P3_FluidsMesh::AddPoint( const Vector2D &point )
{
	int i = m_FluidIndices.AddIndex();
	m_Fluids.EnsureCount( i + 1 );
	m_Points.EnsureCount( i + 1 );
	m_Fluids[ i ] = NULL;
	m_Points[ i ] = point;
	m_Graph.Add( i );

#ifdef LOG_P3_FLUID_MESH
	Msg( "AddPoint( %d )\n", i );
#endif

	return i;
}

int
P3_FluidsMesh::AddTriangle( const CircumcenteredTriangleIndexed &triangle )
{
	int i = m_TriangleIndices.AddIndex();
	m_Triangles.EnsureCount( i + 1 );
	m_Triangles[ i ] = triangle;

	int ia = m_Graph.Find( triangle.A );
	int ib = m_Graph.Find( triangle.B );
	int ic = m_Graph.Find( triangle.C );
	m_Graph.Link( ia, ib );
	m_Graph.Link( ib, ic );
	m_Graph.Link( ic, ia );

#ifdef LOG_P3_FLUID_MESH
	Msg( "AddTriangle( %d, %d, %d )\n", triangle.A, triangle.B, triangle.C );
#endif

	return i;
}

void
P3_FluidsMesh::RemoveFluid( int i )
{
#ifdef LOG_P3_FLUID_MESH
	Msg( "RemoveFluid( %d )\n", i );
#endif

	m_FluidIndices.RemoveIndex( i );
	m_Graph.Remove( i );

#ifdef _DEBUG
	m_Fluids[ i ] = 0;
	m_Points[ i ].x = -1;
	m_Points[ i ].y = -1;
#endif
}

void
P3_FluidsMesh::RemoveTriangle( int i )
{
	const CircumcenteredTriangleIndexed &triangle = m_Triangles[ i ];

#ifdef LOG_P3_FLUID_MESH
	Msg( "RemoveTriangle( %d, %d, %d )\n", triangle.A, triangle.B, triangle.C );
#endif

	int ia = m_Graph.Find( triangle.A );
	int ib = m_Graph.Find( triangle.B );
	int ic = m_Graph.Find( triangle.C );
	m_Graph.Unlink( ia, ib );
	m_Graph.Unlink( ib, ic );
	m_Graph.Unlink( ic, ia );

	m_TriangleIndices.RemoveIndex( i );

#ifdef _DEBUG
	m_Triangles[ i ].A = -1;
	m_Triangles[ i ].B = -1;
	m_Triangles[ i ].C = -1;
#endif
}

CUtlVector< int > &
P3_FluidsMesh::FindTriangles( const Vector2D &point, CUtlVector< int > &result ) const
{
	result.RemoveAll();

	Vector2D d;
	FOR_EACH_VEC_INDEXED( m_Triangles, m_TriangleIndices, it )
	{
		const CircumcenteredTriangleIndexed &tri = m_Triangles[ it ];

		Vector2DSubtract( point, tri.CC, d );

		if ( d.LengthSqr() < tri.r2 )
		{
			result.AddToTail( it );
		}
	}

	return result;
}

CUtlVector< int > &
P3_FluidsMesh::FindTriangles( int point, CUtlVector< int > &result ) const
{
	result.RemoveAll();

	FOR_EACH_VEC_INDEXED( m_Triangles, m_TriangleIndices, it )
	{
		const CircumcenteredTriangleIndexed &tri = m_Triangles[ it ];

		if ( tri.A == point || tri.B == point || tri.C == point )
		{
			result.AddToTail( it );
		}
	}

	return result;
}

CUtlVector< int > &
P3_FluidsMesh::BuildConvexHull( const CUtlVector< int > &triangles, convex_hull_t &convex_hull ) const
{
	edges_t edges;

	edges.Init( triangles.Count() * 3 );

	FOR_EACH_VEC( triangles, i )
	{
		const CircumcenteredTriangleIndexed &tri = m_Triangles[ triangles[ i ] ];

		int ia = edges.FindOrAdd( tri.A );
		int ib = edges.FindOrAdd( tri.B );
		int ic = edges.FindOrAdd( tri.C );

		if ( !edges.AreLinked( ib, ia ) ) edges.Link( ia, ib ); else edges.Unlink( ib, ia );
		if ( !edges.AreLinked( ic, ib ) ) edges.Link( ib, ic ); else edges.Unlink( ic, ib );
		if ( !edges.AreLinked( ia, ic ) ) edges.Link( ic, ia ); else edges.Unlink( ia, ic );
	}

	return GetConvexHull( edges, convex_hull );
}

CUtlVector< int > &
P3_FluidsMesh::BuildConvexHull( const CUtlVector< int > &triangles, convex_hull_t &convex_hull, int removed_node ) const
{
	edges_t edges;

	edges.Init( triangles.Count() * 3 );

	FOR_EACH_VEC( triangles, i )
	{
		const CircumcenteredTriangleIndexed &tri = m_Triangles[ triangles[ i ] ];

		if ( tri.A == removed_node )
		{
			int i1 = edges.FindOrAdd( tri.B );
			int i2 = edges.FindOrAdd( tri.C );
			edges.Link( i1, i2 );
		}
		if ( tri.B == removed_node )
		{
			int i1 = edges.FindOrAdd( tri.C );
			int i2 = edges.FindOrAdd( tri.A );
			edges.Link( i1, i2 );
		}
		if ( tri.C == removed_node )
		{
			int i1 = edges.FindOrAdd( tri.A );
			int i2 = edges.FindOrAdd( tri.B );
			edges.Link( i1, i2 );
		}
	}

	return GetConvexHull( edges, convex_hull );
}

CUtlVector< int > &
P3_FluidsMesh::GetConvexHull( const edges_t &edges, convex_hull_t &convex_hull ) const
{
	convex_hull.RemoveAll();

	int in = 0;
	while ( !convex_hull.Count() )
	{
		int a = edges.Node( in++ );
		const edges_t::Nodes_t &nodes = edges[ a ];

		if ( nodes.Count() )
		{
			Assert( nodes.Count() == 1 );
			convex_hull.AddToTail( a );
			convex_hull.AddToTail( nodes[ 0 ] );
		}
	}

	while ( convex_hull.Head() != convex_hull.Tail() )
	{
		const edges_t::Nodes_t &nodes = edges[ convex_hull.Tail() ];

		Assert( nodes.Count() == 1 );
		Assert( convex_hull.Head() == nodes[ 0 ] || !convex_hull.HasElement( nodes[ 0 ] ) );

		convex_hull.AddToTail( nodes[ 0 ] );
	}

	return convex_hull;
}

float
P3_FluidsMesh::CalculatePower( int ip, int ia, int ib, int ic ) const
{
	const Vector2D &a = m_Points[ ia ];
	const Vector2D &b = m_Points[ ib ];
	const Vector2D &c = m_Points[ ic ];

	float d2 = ( a.x * b.y + b.x * c.y + c.x * a.y )
			 - ( a.x * c.y + b.x * a.y + c.x * b.y );

	if ( d2 >= 0 )
	{
		return BitsToFloat( 0xFF800000 );
	}

	const Vector2D &p = m_Points[ ip ];

	float d1 = ( a.x * b.y * c.LengthSqr() + b.x * c.y * p.LengthSqr() + c.x * p.y * a.LengthSqr() + p.x * a.y * b.LengthSqr() )
			 - ( a.x * p.y * c.LengthSqr() + b.x * a.y * p.LengthSqr() + c.x * b.y * a.LengthSqr() + p.x * c.y * b.LengthSqr() );

	return d1 / d2;
}
