//===== Reconstructed from particles.lib debug info (src/common/blobulator/point3d.h) =====//
//
// A 16-byte aligned 3D point/vector with a padding 4th component.
//
// NOTE on floating point: the shipped objects inline these helpers at every call site, and
// MSVC (/fp:fast) re-associated some of the sums differently per call site.  Call sites
// that depend on a particular operation order spell the arithmetic out explicitly; the
// member functions below reproduce the out-of-line copies that were emitted.
// length() / dot() were emitted as x87 code (extended precision intermediates, one final
// rounding to float).  They are written with double intermediates, which is exact for the
// products and matches x87 under the default Windows 53-bit precision control.
//=============================================================================================//
#ifndef BLOB_POINT3D_H
#define BLOB_POINT3D_H
#pragma once

#include <xmmintrin.h>
#include <math.h>
#include "mathlib/vector.h"
#include "blobulator/implicit/impdefines.h"

class Point3D
{
public:
	union
	{
		__m128 sse_vec;
		float p[4];
	};

	inline Point3D()
	{
		p[3] = 0.0f;
	}

	inline Point3D( const float x, const float y, const float z )
	{
		p[0] = x;
		p[1] = y;
		p[2] = z;
		p[3] = 0.0f;
	}

	inline Point3D( const Vector &v )
	{
		p[0] = v.x;
		p[1] = v.y;
		p[2] = v.z;
		p[3] = 0.0f;
	}

	inline const Vector &AsVector() const
	{
		return *(const Vector *)this;
	}

	inline void clear()
	{
		p[0] = p[1] = p[2] = 0.0f;
	}

	inline void set( const float x, const float y, const float z )
	{
		p[0] = x;
		p[1] = y;
		p[2] = z;
	}

	void print() const;
	void printnl() const;

	inline float &operator[]( const int i )
	{
		return p[i];
	}

	inline const float operator[]( const int i ) const
	{
		return p[i];
	}

	// x87 in the shipped code: z*z' + y*y' + x*x'
	inline float dot( const Point3D &o ) const
	{
		return (float)( ( (double)o.p[2] * p[2] + (double)o.p[1] * p[1] ) + (double)o.p[0] * p[0] );
	}

	// x87 in the shipped code: sqrt( (x*x + y*y) + z*z )
	inline float length() const
	{
		return (float)sqrt( ( (double)p[0] * p[0] + (double)p[1] * p[1] ) + (double)p[2] * p[2] );
	}

	inline float length( const Point3D &o ) const
	{
		return ( *this - o ).length();
	}

	inline float lengthSq() const
	{
		return p[0] * p[0] + p[1] * p[1] + p[2] * p[2];
	}

	inline float lengthSq( const Point3D &o ) const
	{
		return ( *this - o ).lengthSq();
	}

	inline Point3D &normalize()
	{
		float len = sqrtf( ( p[0] * p[0] + p[1] * p[1] ) + p[2] * p[2] );
		if ( len == 0.0f )
			len = 1.0f;
		float inv = 1.0f / len;
		p[0] = inv * p[0];
		p[1] = inv * p[1];
		p[2] = inv * p[2];
		return *this;
	}

	inline Point3D unit() const
	{
		Point3D r = *this;
		r.normalize();
		return r;
	}

	inline Point3D &negate()
	{
		p[0] = -p[0];
		p[1] = -p[1];
		p[2] = -p[2];
		return *this;
	}

	inline Point3D operator-() const
	{
		return Point3D( -p[0], -p[1], -p[2] );
	}

	inline Point3D &add( const Point3D &o )
	{
		p[0] += o.p[0];
		p[1] += o.p[1];
		p[2] += o.p[2];
		return *this;
	}

	inline Point3D operator+( const Point3D &o ) const
	{
		return Point3D( o.p[0] + p[0], o.p[1] + p[1], o.p[2] + p[2] );
	}

	inline Point3D &subtract( const Point3D &o )
	{
		p[0] -= o.p[0];
		p[1] -= o.p[1];
		p[2] -= o.p[2];
		return *this;
	}

	inline Point3D operator-( const Point3D &o ) const
	{
		return Point3D( p[0] - o.p[0], p[1] - o.p[1], p[2] - o.p[2] );
	}

	inline Point3D &mult( const Point3D &o )
	{
		p[0] *= o.p[0];
		p[1] *= o.p[1];
		p[2] *= o.p[2];
		return *this;
	}

	inline Point3D &mult( const float f )
	{
		p[0] *= f;
		p[1] *= f;
		p[2] *= f;
		return *this;
	}

	inline Point3D operator*( const Point3D &o ) const
	{
		return Point3D( p[0] * o.p[0], p[1] * o.p[1], p[2] * o.p[2] );
	}

	inline Point3D operator*( const float f ) const
	{
		return Point3D( p[0] * f, p[1] * f, p[2] * f );
	}

	inline Point3D &div( const Point3D &o )
	{
		p[0] /= o.p[0];
		p[1] /= o.p[1];
		p[2] /= o.p[2];
		return *this;
	}

	inline Point3D &div( const float f )
	{
		float inv = 1.0f / f;
		p[0] *= inv;
		p[1] *= inv;
		p[2] *= inv;
		return *this;
	}

	inline Point3D operator/( const Point3D &o ) const
	{
		return Point3D( p[0] / o.p[0], p[1] / o.p[1], p[2] / o.p[2] );
	}

	// division by a scalar is a multiplication by its reciprocal
	inline Point3D operator/( const float f ) const
	{
		float inv = 1.0f / f;
		return Point3D( inv * p[0], p[1] * inv, p[2] * inv );
	}

	inline bool operator==( const Point3D &o ) const
	{
		return p[0] == o.p[0] && p[1] == o.p[1] && p[2] == o.p[2];
	}

	inline Point3D crossProduct( const Point3D &o ) const
	{
		return Point3D( p[1] * o.p[2] - p[2] * o.p[1],
						o.p[0] * p[2] - p[0] * o.p[2],
						p[0] * o.p[1] - o.p[0] * p[1] );
	}

	inline Point3D &toInt()
	{
		p[0] = (float)(int)p[0];
		p[1] = (float)(int)p[1];
		p[2] = (float)(int)p[2];
		return *this;
	}

	inline Point3D getToInt() const
	{
		Point3D r = *this;
		r.toInt();
		return r;
	}
};

#endif // BLOB_POINT3D_H
