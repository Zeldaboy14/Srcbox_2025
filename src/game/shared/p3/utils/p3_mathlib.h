#ifndef P3_MATHLIB_H
#define P3_MATHLIB_H

#ifdef _WIN32
#pragma once
#endif

#include <mathlib/vector2d.h>
#include <mathlib/vector.h>
#include <tier1/utlvector.h>


//-----------------------------------------------------------------------------
// Purpose: Bound input float to 1/N boundary
// Input  : in - 
// Output : inline float
//-----------------------------------------------------------------------------
template < int N >
float Clamp( float in )
{
	return Floor2Int( in * N + 0.5f ) / (float) N;
}

inline
float Round( float in )
{
	return ( in > 0.0 ) ? floor( in + 0.5f ) : ceil( in - 0.5f );
}

inline
float D( float m00, float m01, float m02
	   , float m10, float m11, float m12
	   , float m20, float m21, float m22 )
{
	return m00 * ( m11 * m22 - m12 * m21 )
		 - m01 * ( m10 * m22 - m12 * m20 )
		 + m02 * ( m10 * m21 - m11 * m20 );
}

inline
float D( float m00, float m01, float m02, float m03
	   , float m10, float m11, float m12, float m13
	   , float m20, float m21, float m22, float m23
	   , float m30, float m31, float m32, float m33 )
{
	return - m00 * D( m11, m12, m13
					, m21, m22, m23
					, m31, m32, m33 )
		   + m01 * D( m10, m12, m13
					, m20, m22, m23
					, m30, m32, m33 )
		   - m02 * D( m10, m11, m13
					, m20, m21, m23
					, m30, m31, m33 )
		   + m03 * D( m10, m11, m12
					, m20, m21, m22
					, m30, m31, m32 );
}

inline
float D3( const float *m )
{
	return D( m[0], m[1], m[2]
			, m[3], m[4], m[5]
			, m[6], m[7], m[8] );
}

inline
float D4( const float *m )
{
	return D(  m[0],  m[1],  m[2],  m[3]
			,  m[4],  m[5],  m[6],  m[7]
			,  m[8],  m[9], m[10], m[11]
			, m[12], m[13], m[14], m[15] );
}

inline
Vector2D MedianPoint( const CUtlVector< int > &indices, const Vector2D *points )
{
	Vector2D r( 0, 0 );

	FOR_EACH_VEC( indices, ip )
	{
		r.x += points[ indices[ ip ] ].x;
		r.y += points[ indices[ ip ] ].y;
	}

	r.x /= indices.Count();
	r.y /= indices.Count();

	return r;
}

// Get the circumcenter coordinates of triangle [A,B,C].
inline void GetCircumcenterCoords2D( 
	Vector2D const &A,
	Vector2D const &B,
	Vector2D const &C,
	Vector2D &ccCoords )
{
#ifdef _DEBUG
	float m[] = { A.x, B.x, C.x
			    , A.y, B.y, C.y
			    , 1  , 1  , 1 };
	Assert( D3( m ) < 0 );
#endif

	Vector2D ab;
	Vector2D ac;

	Vector2DSubtract( B, A, ab );
	Vector2DSubtract( C, A, ac );

	vec_t d = 2 * ( ab.x*ac.y - ab.y*ac.x );

	ccCoords.x = ( ac.y*( ab.x * ab.x + ab.y * ab.y ) - ab.y*( ac.x * ac.x + ac.y * ac.y ) )/d;
	ccCoords.y = ( ab.x*( ac.x * ac.x + ac.y * ac.y ) - ac.x*( ab.x * ab.x + ab.y * ab.y ) )/d;

	Vector2DAdd( A, ccCoords, ccCoords );
}

struct Triangle
{
	Vector2D A, B, C;

	Triangle( 
		const Vector2D &A,
		const Vector2D &B,
		const Vector2D &C )
		: A( A ), B( B ), C( C )
	{
	}
};

struct TriangleIndexed
{
	int A, B, C;

	TriangleIndexed()
		: A( -1 )
		, B( -1 )
		, C( -1 )
	{
	}

	TriangleIndexed( int A, int B, int C )
		: A( A ), B( B ), C( C )
	{
	}
};

struct WeightedTriangleIndexed : public TriangleIndexed
{
	float W;

	WeightedTriangleIndexed()
		: TriangleIndexed()
	{
	}

	WeightedTriangleIndexed( int A, int B, int C, float W )
		: TriangleIndexed( A, B, C )
		, W( W )
	{
	}

	bool operator <( const WeightedTriangleIndexed &r ) const
	{
		return W < r.W;
	}
};

struct CircumcenteredTriangle : public Triangle
{
	Vector2D CC;
	float r, r2;

	CircumcenteredTriangle( 
		const Vector2D &A,
		const Vector2D &B,
		const Vector2D &C )
		: Triangle( A, B, C )
	{
		GetCircumcenterCoords2D( A, B, C, CC );
		Vector2D d;
		Vector2DSubtract( CC, A, d );
		r2 = d.LengthSqr();
		r = FastSqrt( r2 );
	}
};

struct CircumcenteredTriangleIndexed : public TriangleIndexed
{
	Vector2D CC;
	float r, r2;

	CircumcenteredTriangleIndexed()
		: TriangleIndexed()
		, r( -1 ), r2( -1 )
	{
	}

	CircumcenteredTriangleIndexed(
		int A, int B, int C,
		const Vector2D *points )
		: TriangleIndexed( A, B, C )
	{
		Init( points );
	}

	CircumcenteredTriangleIndexed(
		const TriangleIndexed &tri,
		const Vector2D *points )
		: TriangleIndexed( tri )
	{
		Init( points );
	}

	void Init( const Vector2D *points )
	{
		GetCircumcenterCoords2D( points[ A ], points[ B ], points[ C ], CC );
		Vector2D d;
		Vector2DSubtract( CC, points[ A ], d );
		r2 = d.LengthSqr();
		r = FastSqrt( r2 );
	}
};

template <class T>
T Smoothstep(T t)
{
	if(t < 0.0)
		t = 0.0;

	if(t > 1.0)
		t = 1.0;

	return t * t * (3.0 - 2.0 * t);
}

template <class T>
T Smoothstep(T a, T b, T t)
{
	t = ( t - a ) / ( b - a );	

	return Smoothstep(t);
}

/**
 * Функция вычисляет начальную скорость прыжка
 * из точки from в точку to. При этом гарантируется,
 * что модуль скорости будет минимален.
 * Возможные препятствия на пути траектории прыжка не
 * учитываются.
 * Третьим параметром получает значение ускорения свободного падения.
 */
Vector CalcJumpVelNoObstacles(const Vector& from, const Vector& to, float G);


#endif // P3_MATHLIB_H
