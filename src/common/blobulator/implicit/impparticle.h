//===== Reconstructed from particles.lib debug info (src/common/blobulator/implicit/impparticle.h) =====//
#ifndef BLOB_IMPPARTICLE_H
#define BLOB_IMPPARTICLE_H
#pragma once

#include "blobulator/point3d.h"

// A metaball.  fieldRScaleSq scales the squared distance in the field function.
class ImpParticle
{
public:
	ImpParticle();

	inline void setFieldScale( float s )
	{
		scale = s;
		fieldRScaleSq = 1.0f / ( s * s );
	}

	Point3D center;
	float fieldRScaleSq;
	float scale;
};

// Particles carrying per-particle data that gets blended into the surface vertices.
class ImpParticleWithOneInterpolant : public ImpParticle
{
public:
	ImpParticleWithOneInterpolant();

	Point3D interpolants1;
};

class ImpParticleWithTwoInterpolants : public ImpParticleWithOneInterpolant
{
public:
	ImpParticleWithTwoInterpolants();

	Point3D interpolants2;
};

class ImpParticleWithFourInterpolants : public ImpParticleWithTwoInterpolants
{
public:
	ImpParticleWithFourInterpolants();

	Point3D interpolants3;
	Point3D interpolants4;
};

#endif // BLOB_IMPPARTICLE_H
