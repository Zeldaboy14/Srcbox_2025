#ifndef UTLDELAUNAYGRAPH_H
#define UTLDELAUNAYGRAPH_H

#ifdef _WIN32
#pragma once
#endif

#include "utils/p3_mathlib.h"
#include "utils/utldisjointset.h"
#include "utils/utlgraph.h"
#include "utils/utlindexbuffer.h"
#include <tier1/utlhash.h>
#include <tier1/utlpriorityqueue.h>
#include <tier1/utlrbtree.h>

//#define LOG_DELAUNAY_GRAPH 1

#if defined( CLIENT_DLL )
static const char *sign = "c";
#else
static const char *sign = "s";
#endif


//-----------------------------------------------------------------------------
// CUtlDelaunayGraph
//-----------------------------------------------------------------------------

// Delaunay indices are shifted by 4
//#define INDEX_SHIFT 4

// Delaunay triangulation.
class CUtlDelaunayGraph
{
	typedef CUtlVector< Vector2D > Points_t;
	typedef CUtlVector< CircumcenteredTriangleIndexed > Triangles_t;
	typedef CUtlGraphLinked< int > DelaunayGraph_t;


public:
	typedef int Index_t;
	typedef CUtlVector< Index_t > indices_t;
	typedef CUtlVector< int > convex_hull_t;
	typedef CUtlGraphMatrix< int > edges_t;
	typedef DelaunayGraph_t IndexGraph_t;
	typedef DelaunayGraph_t::Nodes_t Nodes_t;


public:
	CUtlDelaunayGraph()
	{
	}
	/*
	CUtlDelaunayGraph( const CUtlIndexBuffer &point_indices )
		: m_PointIndices( point_indices )
	{
	}
	*/

	void Init( const Vector2D &min, const Vector2D &max );

//	int Node( Index_t i )								{ return m_Graph.Node( m_Graph.Find( i ) ); }
//	const int Node( Index_t i ) const					{ return m_Graph.Node( m_Graph.Find( i ) ); }
//	const Nodes_t &operator[]( Index_t i ) const		{ return m_Graph[ m_Graph.Find( i ) ]; }
	const IndexGraph_t &IndexGraph() const				{ return m_Graph; }

	const Vector2D &Point( Index_t i ) const			{ return m_Points[ i ]; }

//	int Count() const									{ return m_Graph.Count(); }
//	int MaxElement() const								{ return m_Graph.MaxElement(); }
//	bool IsValidIndex( int i ) const					{ return m_Graph.IsValidIndex( i ); }

	Index_t Add( const Vector2D &point );
	void Remove( Index_t index );
	void Remove( const indices_t &indices );
	void Remove( const CUtlDisjointSet &indices );

	Index_t Find( const Vector2D &point ) const;
	Index_t Find( int node ) const;

	void Purge();


private:
	int AddPoint( const Vector2D &point );
	int AddTriangle( const CircumcenteredTriangleIndexed &triangle );
	void RemovePoint( Index_t i );
	void RemoveTriangle( Index_t i );

	CUtlVector< int > &FindTriangles( const Vector2D &point, CUtlVector< int > &result ) const;
	CUtlVector< int > &FindTriangles( Index_t point, CUtlVector< int > &result ) const;
	CUtlVector< int > &FindTriangles( const indices_t &points, CUtlVector< int > &result ) const;
	convex_hull_t &BuildConvexHull( const CUtlVector< int > &triangles, convex_hull_t &convex_hull ) const;
	convex_hull_t &BuildConvexHull( const CUtlVector< int > &triangles, convex_hull_t &convex_hull, int removed_node ) const;
	convex_hull_t &GetConvexHull( const edges_t &edges, convex_hull_t &convex_hull ) const;
	void FillConvexHull( const convex_hull_t &convex_hull, int index );

	float CalculatePower( int ip, int ia, int ib, int ic ) const;


private:
	CUtlIndexBuffer m_PointIndices;
	CUtlIndexBuffer m_TriangleIndices;
	Points_t m_Points;
	Triangles_t m_Triangles;
	DelaunayGraph_t m_Graph;
};

inline
void
CUtlDelaunayGraph::Init( const Vector2D &min, const Vector2D &max )
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

/*
inline
const CUtlDelaunayGraph::Nodes_t &
CUtlDelaunayGraph::operator[]( Index_t i ) const
{
	static Nodes_t nodes;

	nodes.RemoveAll();
	m_Graph.Nodes( i + INDEX_SHIFT, nodes );
	FOR_EACH_VEC( nodes, i )
	{
		nodes[ i ] += INDEX_SHIFT;
	}

	return nodes;
}
*/

inline
CUtlDelaunayGraph::Index_t
CUtlDelaunayGraph::Add( const Vector2D &point )
{
	CUtlVector< int > triangles;
	FindTriangles( point, triangles );

	Assert( triangles.Count() > 0 );

	CUtlVector< int > convex_hull;
	BuildConvexHull( triangles, convex_hull );

	if ( convex_hull.Count() )
	{
		FOR_EACH_VEC( triangles, it )
		{
			RemoveTriangle( triangles[ it ] );
		}

		int ip = AddPoint( point );

		Assert( ip < m_Points.Count() );

		for ( int i = 1; i < convex_hull.Count(); i++ )
		{
			AddTriangle( CircumcenteredTriangleIndexed( convex_hull[ i - 1 ], convex_hull[ i ], ip, m_Points.Base() ) );
		}

		return ip;
	}

	return -1;
}

inline
void
CUtlDelaunayGraph::Remove( Index_t index )
{
	Assert( m_Points.IsValidIndex( index ) );

	if ( !m_Points.IsValidIndex( index ) )
	{
		return;
	}

	CUtlVector< int > triangles;
	FindTriangles( index, triangles );

	convex_hull_t convex_hull;
	BuildConvexHull( triangles, convex_hull, index );

	Assert( convex_hull.Count() > 0 );

	if ( convex_hull.Count() > 0 )
	{
		int i1 = convex_hull[ 1 ];
		convex_hull.AddToTail( i1 );

		FOR_EACH_VEC( triangles, it )
		{
			RemoveTriangle( triangles[ it ] );
		}

		FillConvexHull( convex_hull, index );
	}

	RemovePoint( index );
}

inline
void
CUtlDelaunayGraph::Remove( const indices_t &indices )
{
	Assert( false && "It does not work." );
#if 0
	CUtlVector< int > triangles;

	FindTriangles( indices, triangles );

	convex_hull_t convex_hull;
	BuildConvexHull( triangles, convex_hull );

	Assert( convex_hull.Count() > 0 );

	int i1 = convex_hull[ 1 ];
	convex_hull.AddToTail( i1 );

	FOR_EACH_VEC( triangles, it )
	{
		RemoveTriangle( triangles[ it ] );
	}

	m_Points[ indices[ 0 ] ] = MedianPoint( indices, m_Points.Base() );

	FillConvexHull( convex_hull, indices[ 0 ] );

	FOR_EACH_VEC( indices, ip )
	{
		RemovePoint( indices[ ip ] );
	}
#endif
}

inline
void
CUtlDelaunayGraph::Remove( const CUtlDisjointSet &indices )
{
	Assert( false && "It does not work." );
#if 0
	FOR_EACH_VEC( indices.Sets(), is )
	{
		const CUtlDisjointSet::NodeSet_t &nodes = indices[ is ];

		Remove( nodes );
	}
#endif
}

inline
CUtlDelaunayGraph::Index_t
CUtlDelaunayGraph::Find( const Vector2D &point ) const
{
	return m_Points.Find( point );
}

inline
CUtlDelaunayGraph::Index_t
CUtlDelaunayGraph::Find( int node ) const
{
	return m_Graph.Find( node );
}

inline
void
CUtlDelaunayGraph::Purge()
{
	m_PointIndices.Purge();
	m_TriangleIndices.Purge();
	m_Points.Purge();
	m_Triangles.Purge();
	m_Graph.Purge();
}

inline
int
CUtlDelaunayGraph::AddPoint( const Vector2D &point )
{
	int i = m_PointIndices.AddIndex();
	m_Points.EnsureCount( i + 1 );
	m_Points[ i ] = point;
	m_Graph.Add( i );

#if defined( LOG_DELAUNAY_GRAPH )
	Msg( "%s AddPoint( %d )\n", sign, i );
#endif

	return i;
}

inline
int
CUtlDelaunayGraph::AddTriangle( const CircumcenteredTriangleIndexed &triangle )
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

#if defined( LOG_DELAUNAY_GRAPH )
	Msg( "%s AddTriangle( %d, %d, %d )\n", sign, triangle.A, triangle.B, triangle.C );
#endif

	return i;
}

inline
void
CUtlDelaunayGraph::RemovePoint( Index_t i )
{
	Assert( m_PointIndices.IsIndexValid( i ) );

#if defined( LOG_DELAUNAY_GRAPH )
	Msg( "%s RemovePoint( %d )\n", sign, i );
#endif

	m_PointIndices.RemoveIndex( i );
	m_Graph.Remove( i );

#ifdef _DEBUG
	m_Points[ i ].x = -1;
	m_Points[ i ].y = -1;
#endif
}

inline
void
CUtlDelaunayGraph::RemoveTriangle( Index_t i )
{
	Assert( m_TriangleIndices.IsIndexValid( i ) );

	const CircumcenteredTriangleIndexed &triangle = m_Triangles[ i ];

#if defined( LOG_DELAUNAY_GRAPH )
	Msg( "%s RemoveTriangle( %d, %d, %d )\n", sign, triangle.A, triangle.B, triangle.C );
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

inline
CUtlVector< int > &
CUtlDelaunayGraph::FindTriangles( const Vector2D &point, CUtlVector< int > &result ) const
{
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

inline
CUtlVector< int > &
CUtlDelaunayGraph::FindTriangles( Index_t point, CUtlVector< int > &result ) const
{
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

inline
CUtlVector< int > &
CUtlDelaunayGraph::FindTriangles( const indices_t &points, CUtlVector< int > &result ) const
{
	CUtlRBTree< Index_t > point_set( DefLessFunc( Index_t ) );
	point_set.Insert( points.Base(), points.Count() );

	FOR_EACH_VEC_INDEXED( m_Triangles, m_TriangleIndices, it )
	{
		const CircumcenteredTriangleIndexed &tri = m_Triangles[ it ];

		if ( point_set.IsValidIndex( point_set.Find( tri.A ) )
		  || point_set.IsValidIndex( point_set.Find( tri.B ) )
		  || point_set.IsValidIndex( point_set.Find( tri.C ) ) )
		{
			result.AddToTail( it );
		}
	}

	return result;
}

inline
CUtlDelaunayGraph::convex_hull_t &
CUtlDelaunayGraph::BuildConvexHull( const CUtlVector< int > &triangles, convex_hull_t &convex_hull ) const
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

inline
CUtlDelaunayGraph::convex_hull_t &
CUtlDelaunayGraph::BuildConvexHull( const CUtlVector< int > &triangles, convex_hull_t &convex_hull, int removed_node ) const
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

inline
CUtlDelaunayGraph::convex_hull_t &
CUtlDelaunayGraph::GetConvexHull( const edges_t &edges, convex_hull_t &convex_hull ) const
{
	convex_hull.RemoveAll();

	int in = 0;
	while ( !convex_hull.Count() && in < edges.Count() )
	{
		int a = edges.Node( in );
		const edges_t::Nodes_t &nodes = edges[ in ];

		if ( nodes.Count() )
		{
			Assert( nodes.Count() == 1 );
			convex_hull.AddToTail( a );
			convex_hull.AddToTail( nodes[ 0 ] );
		}

		in++;
	}

	if ( !convex_hull.Count() )
		return convex_hull;

	while ( convex_hull.Head() != convex_hull.Tail() )
	{
		const edges_t::Nodes_t &nodes = edges[ edges.Find( convex_hull.Tail() ) ];

		Assert( nodes.Count() == 1 );
		Assert( convex_hull.Head() == nodes[ 0 ] || !convex_hull.HasElement( nodes[ 0 ] ) );

		convex_hull.AddToTail( nodes[ 0 ] );
	}

	return convex_hull;
}

static CUtlLinkedList< WeightedTriangleIndexed > *WEarNode_triangles;

struct WEarNode
{
	int index;

	WEarNode()
		: index( -1 )
	{
	}

	WEarNode( int index )
		: index( index )
	{
	}

	static int Compare( const WEarNode *lhs, const WEarNode *rhs )
	{
		return (*WEarNode_triangles)[ lhs->index ].W < (*WEarNode_triangles)[ rhs->index ].W ? -1
			 : (*WEarNode_triangles)[ lhs->index ].W > (*WEarNode_triangles)[ rhs->index ].W ? 1
			 : 0;
	}
};

inline
void
CUtlDelaunayGraph::FillConvexHull( const convex_hull_t &convex_hull, int index )
{
	CUtlLinkedLoop< WeightedTriangleIndexed > ears( 0, convex_hull.Count() );
	WEarNode_triangles = &ears;
	CUtlVector< WEarNode > wears;
	for ( int i = 0; i < convex_hull.Count() - 2; i++ )
	{
		int iear =
		ears.AddToTail( WeightedTriangleIndexed( convex_hull[ i ], convex_hull[ i + 1 ], convex_hull[ i + 2 ]
		                                       , CalculatePower( index, convex_hull[ i ], convex_hull[ i + 1 ], convex_hull[ i + 2 ] ) ) );
		wears.AddToTail( WEarNode( iear ) );

#if defined( LOG_DELAUNAY_GRAPH )
		Msg( "%s ears.Add( %d, %d, %d, %f )\n", sign, ears[ iear ].A, ears[ iear ].B, ears[ iear ].C, ears[ iear ].W );
#endif
	}

	int last_triangle = -1;

	while ( wears.Count() > 3 )
	{
		wears.Sort( WEarNode::Compare );

		const WEarNode &node = wears[ 0 ];

		WeightedTriangleIndexed &cear = ears[ node.index ]; 
		WeightedTriangleIndexed &pear = ears[ ears.Previous( node.index ) ]; 
		WeightedTriangleIndexed &near1 = ears[ ears.Next( node.index ) ]; 

		Assert( IsFinite( cear.W ) );

		last_triangle = AddTriangle( CircumcenteredTriangleIndexed( cear, m_Points.Base() ) );

		near1.A = cear.A;
		pear.C = cear.C;
		pear.W = CalculatePower( index, pear.A, pear.B, pear.C );
		near1.W = CalculatePower( index, near1.A, near1.B, near1.C );

		ears.Remove( node.index );
		wears.FastRemove( 0 );

#if defined( LOG_DELAUNAY_GRAPH )
		Msg( "%s p( %d, %d, %d, %f )\n", sign, pear.A, pear.B, pear.C, pear.W );
		Msg( "%s n( %d, %d, %d, %f )\n", sign, near1.A, near1.B, near1.C, near1.W );
#endif
	}

	wears.Sort( WEarNode::Compare );
	const WEarNode &node = wears[ 0 ];

	Assert( IsFinite( ears[ node.index ].W ) );
	AddTriangle( CircumcenteredTriangleIndexed( ears[ node.index ], m_Points.Base() ) );
}

inline
float
CUtlDelaunayGraph::CalculatePower( int ip, int ia, int ib, int ic ) const
{
	const Vector2D &p = m_Points[ ip ];
	const Vector2D &a = m_Points[ ia ] - p;
	const Vector2D &b = m_Points[ ib ] - p;
	const Vector2D &c = m_Points[ ic ] - p;

	float m2[] = { a.x, b.x, c.x
				 , a.y, b.y, c.y
				 , 1  , 1  , 1 };
	float d2 = D3( m2 );

	if ( d2 >= 0 )
	{
		return BitsToFloat( 0x7F800000 );
	}

	float m1[] = { a.x, b.x, c.x, 0
				 , a.y, b.y, c.y, 0
				 , a.LengthSqr(), b.LengthSqr(), c.LengthSqr(), 0
				 , 1  , 1  , 1  , 1 };

	float d1 = D4( m1 );

	return fabs( d1 / d2 );
}

#endif // UTLDELAUNAYGRAPH_H
