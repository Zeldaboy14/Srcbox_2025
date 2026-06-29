#ifndef P3_FLUIDS_MESH
#define P3_FLUIDS_MESH

#ifdef _WIN32
#pragma once
#endif

#include "p3_fluids_shared.h"
#include "utils/p3_mathlib.h"
#include "utils/utlgraph.h"
#include "utils/utlindexbuffer.h"
#include <tier1/utlhash.h>


//-----------------------------------------------------------------------------
// P3_FluidsMesh
//-----------------------------------------------------------------------------

// Delaunay triangulation.
class P3_FluidsMesh
{
	typedef CUtlVector< Vector2D > Points_t;
	typedef CUtlVector< const P3_Fluid * > Fluids_t;
	typedef CUtlVector< CircumcenteredTriangleIndexed > Triangles_t;
	typedef CUtlGraphLinked< int > DelaunayGraph_t;


public:
	typedef CUtlVector< int > convex_hull_t;
	typedef CUtlGraphMatrix< int > edges_t;


public:
	P3_FluidsMesh()
	{
	}

	void Init( const Vector &min, const Vector &max );
	void Add( const P3_Fluid &fluid );
	void Remove( const P3_Fluid &fluid );

	void Purge();

	void DrawDebugOverlay();


private:
	int AddFluid( const P3_Fluid *fluid );
	int AddPoint( const Vector2D &point );
	int AddTriangle( const CircumcenteredTriangleIndexed &triangle );
	void RemoveFluid( int i );
	void RemoveTriangle( int i );

	CUtlVector< int > &FindTriangles( const Vector2D &point, CUtlVector< int > &result ) const;
	CUtlVector< int > &FindTriangles( int point, CUtlVector< int > &result ) const;
	CUtlVector< int > &BuildConvexHull( const CUtlVector< int > &triangles, convex_hull_t &convex_hull ) const;
	CUtlVector< int > &BuildConvexHull( const CUtlVector< int > &triangles
	                                  , convex_hull_t &convex_hull, int removed_node ) const;
	CUtlVector< int > &GetConvexHull( const edges_t &edges, convex_hull_t &convex_hull ) const;

	float CalculatePower( int ip, int ia, int ib, int ic ) const;


private:
	CUtlIndexBuffer m_FluidIndices;
	CUtlIndexBuffer m_TriangleIndices;
	Fluids_t m_Fluids;
	Points_t m_Points;
	Triangles_t m_Triangles;
	DelaunayGraph_t m_Graph;
};

#endif // P3_FLUIDS_MESH
