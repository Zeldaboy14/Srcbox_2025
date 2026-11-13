//===== Reconstructed from particles.lib debug info (src/common/blobulator/util/fasttrunc.h) =====//
//
// Fast float->int conversions.  The shipped header has two variants:
//  - the default one rounds with the FPU (fld / fistp, i.e. round-to-nearest-even in the
//    current rounding mode), offsetting the input by +-0.5 to truncate / round up;
//  - with BLOB_SSE_TRUNC defined (ProjectingParticleCache) it truncates with cvttss2si.
// The original functions were called round / trunc / round_up; they are renamed here
// because those names collide with the CRT's round()/trunc() overloads in newer headers.
//=============================================================================================//
#ifndef BLOB_FASTTRUNC_H
#define BLOB_FASTTRUNC_H
#pragma once

#include <xmmintrin.h>

// fld dword / fistp dword (round to nearest in the current rounding mode)
inline int blob_round( float a )
{
	return _mm_cvtss_si32( _mm_set_ss( a ) );
}

#ifndef BLOB_SSE_TRUNC

inline int blob_trunc( float x )
{
	x -= 0.499999881f;
	return _mm_cvtss_si32( _mm_set_ss( x ) );
}

inline int blob_round_up( float x )
{
	x += 0.500000119f;
	return _mm_cvtss_si32( _mm_set_ss( x ) );
}

#else

inline int blob_trunc( float x )
{
	return (int)x;
}

inline int blob_round_up( float x )
{
	return (int)( x + 1.0f );
}

#endif

#endif // BLOB_FASTTRUNC_H
