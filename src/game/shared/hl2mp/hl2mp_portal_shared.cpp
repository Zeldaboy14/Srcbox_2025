//========= Portal Gun for TF2 - original implementation =================//
//
// Purpose: See tf_portal_shared.h.
//
//==========================================================================//

#include "cbase.h"
#include "hl2mp_portal_shared.h"

//-----------------------------------------------------------------------------
void TF_Portal_ComputeTransform( const Vector &vecOriginA, const QAngle &angA,
								  const Vector &vecOriginB, const QAngle &angB,
								  matrix3x4_t &matOut )
{
	matrix3x4_t matA, matB, matAInverse;
	AngleMatrix( angA, vecOriginA, matA );
	AngleMatrix( angB, vecOriginB, matB );
	MatrixInvert( matA, matAInverse );

	// 180 degrees about yaw = 180 degrees about the "up" axis. This is the
	// "glue the two portal openings together, flipped, so walking through
	// feels like walking through one continuous doorway" rotation - see the
	// header comment for why this is what makes speed carry through intact.
	matrix3x4_t matFlip;
	AngleMatrix( QAngle( 0.0f, 180.0f, 0.0f ), vec3_origin, matFlip );

	// World -> A's local frame -> flipped -> B's world frame.
	matrix3x4_t matTemp;
	ConcatTransforms( matFlip, matAInverse, matTemp );
	ConcatTransforms( matB, matTemp, matOut );
}

//-----------------------------------------------------------------------------
void TF_Portal_TransformPoint( const matrix3x4_t &mat, const Vector &vecIn, Vector &vecOut )
{
	VectorTransform( vecIn, mat, vecOut );
}

//-----------------------------------------------------------------------------
void TF_Portal_TransformDirection( const matrix3x4_t &mat, const Vector &vecIn, Vector &vecOut )
{
	// Rotation only - no translation - so magnitude (speed) never changes,
	// only direction. This is the entire "momentum conservation" mechanic.
	VectorRotate( vecIn, mat, vecOut );
}

//-----------------------------------------------------------------------------
void TF_Portal_TransformAngles( const matrix3x4_t &mat, const QAngle &angIn, QAngle &angOut )
{
	Vector vecForward;
	AngleVectors( angIn, &vecForward );

	Vector vecNewForward;
	VectorRotate( vecForward, mat, vecNewForward );

	// Deliberately use the SINGLE-argument VectorAngles(forward, angles)
	// overload here, not the (forward, up, angles) one.
	//
	// The two-argument version derives roll from the given up vector --
	// and since the portal transform is an arbitrary 3D rotation (e.g.
	// entry on a wall, exit on a floor), the rotated up vector is
	// generally NOT world-up any more, which leaked rotation into roll
	// and made the camera appear tilted/crooked after teleporting.
	//
	// The single-argument version (see mathlib_base.cpp) always sets
	// angles[ROLL] = 0 by construction. Only pitch and yaw come from the
	// new forward direction, so the player's view stays perfectly upright
	// through every teleport, regardless of entry/exit portal orientation.
	VectorAngles( vecNewForward, angOut );
}

//-----------------------------------------------------------------------------
void TF_Portal_TransformAnglesFull( const matrix3x4_t &mat, const QAngle &angIn, QAngle &angOut )
{
	Vector vecForward, vecUp;
	AngleVectors( angIn, &vecForward, NULL, &vecUp );

	Vector vecNewForward, vecNewUp;
	VectorRotate( vecForward, mat, vecNewForward );
	VectorRotate( vecUp, mat, vecNewUp );

	// Two-argument VectorAngles derives roll from the transformed up
	// vector -- this is the geometrically complete rotation, kept separate
	// from TF_Portal_TransformAngles above specifically so the player's
	// own eyes are never given this version.
	VectorAngles( vecNewForward, vecNewUp, angOut );
}
