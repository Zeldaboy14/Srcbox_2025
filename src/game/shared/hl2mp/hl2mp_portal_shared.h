//========= Portal Gun for TF2 - original implementation =================//
//
// Purpose: Shared portal constants and the portal-to-portal transform math.
//
// Everything in this file is ordinary 3D change-of-basis linear algebra -
// the same category of math used for mirrors, periscopes, or any render-
// to-texture secondary camera. None of it is Portal-specific; it's built
// entirely from the public Source SDK math library (AngleMatrix,
// ConcatTransforms, MatrixInvert, VectorRotate, VectorTransform).
//
// THE CORE RULE (how momentum conservation actually works):
//   The transform from entry portal A to exit portal B is: go into A's
//   local frame, rotate 180 degrees about the "up" axis, then come back
//   out in B's local frame. Applying that same transform to a velocity
//   vector (rotation only, no translation) automatically preserves its
//   magnitude - that IS the "momentum is conserved" behavior. Fall speed
//   into a floor portal comes out as launch speed from a wall portal
//   because the transform just re-orients the vector, it never scales it.
//
//==========================================================================//

#ifndef TF_PORTAL_SHARED_H
#define TF_PORTAL_SHARED_H
#ifdef _WIN32
#pragma once
#endif

#include "mathlib/mathlib.h"

// Which of the two linked portals - matches Portal's own blue=primary /
// orange=secondary convention.
enum PortalColor_t
{
	PORTAL_COLOR_BLUE = 0,
	PORTAL_COLOR_ORANGE,

	PORTAL_COLOR_COUNT
};

// Portal footprint size, in Hammer units (inches). Shared by weapon
// placement validation, the entity's trigger volume, and (later) the
// visible portal surface, so all three always agree on the portal's size.
#define TF_PORTAL_HALF_WIDTH		32.0f
#define TF_PORTAL_HALF_HEIGHT		54.0f
#define TF_PORTAL_MAX_RANGE			4096.0f
#define TF_PORTAL_TRIGGER_DEPTH		16.0f

// How long after teleporting an entity out of a portal we ignore that same
// entity touching it again, to stop the exit portal from immediately
// re-triggering while the entity is still physically inside its volume.
#define TF_PORTAL_RETOUCH_GRACE_PERIOD	0.5f

//-----------------------------------------------------------------------------
// Purpose: Builds the full portal-to-portal transform: world position/
//			direction relative to portal A becomes world position/direction
//			relative to portal B, with the 180-degree "glue two portals
//			into one seamless opening" flip baked in.
//-----------------------------------------------------------------------------
void TF_Portal_ComputeTransform( const Vector &vecOriginA, const QAngle &angA,
								  const Vector &vecOriginB, const QAngle &angB,
								  matrix3x4_t &matOut );

// Position: full transform (rotation + translation).
void TF_Portal_TransformPoint( const matrix3x4_t &mat, const Vector &vecIn, Vector &vecOut );

// Velocity / direction: rotation only, no translation - this is what keeps
// speed magnitude intact (see the header comment above).
void TF_Portal_TransformDirection( const matrix3x4_t &mat, const Vector &vecIn, Vector &vecOut );

// Eye/body angles: rotates the forward+up basis and re-derives a QAngle,
// deliberately dropping roll (always 0) -- for the PLAYER's own eye angles
// after physically teleporting, so their screen never visually tilts,
// regardless of the entry/exit portals' relative orientation.
void TF_Portal_TransformAngles( const matrix3x4_t &mat, const QAngle &angIn, QAngle &angOut );

// Same rotation, but keeps full roll -- for the RENDER camera computing
// what a portal should visibly show as a "window". Unlike the player's own
// eyes, the portal's view must be geometrically correct: a portal pair
// connecting a wall to a floor genuinely should show a correspondingly
// rotated view, or the live picture would look subtly wrong. Do not use
// this for anything that moves the player themselves.
void TF_Portal_TransformAnglesFull( const matrix3x4_t &mat, const QAngle &angIn, QAngle &angOut );

#endif // TF_PORTAL_SHARED_H
