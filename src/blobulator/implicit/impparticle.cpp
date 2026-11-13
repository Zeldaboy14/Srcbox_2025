//===== Reconstructed from particles.lib (ImpParticle.obj), src/blobulator/implicit/impparticle.cpp =====//

#include "blobulator/implicit/impparticle.h"
#include "../util/rand.h"

ImpParticle::ImpParticle()
{
	fieldRScaleSq = 1.0f;
}

ImpParticleWithOneInterpolant::ImpParticleWithOneInterpolant()
{
	// the arguments are evaluated right to left: z gets the first random number
	float z = frand();
	float y = frand();
	float x = frand();
	interpolants1 = Point3D( x, y, z );

	// NOTE: shipped bug - writes one float past interpolants1 (offset 0x30, beyond the object
	// for this class; it is the first float of interpolants2 in the derived classes).
	interpolants1[4] = 0.0f;
}

ImpParticleWithTwoInterpolants::ImpParticleWithTwoInterpolants()
{
	interpolants2 = Point3D( 1.0f, 0.0f, 0.0f );
}

ImpParticleWithFourInterpolants::ImpParticleWithFourInterpolants()
{
	interpolants3 = Point3D( 0.0f, 1.0f, 0.0f );
	interpolants4 = Point3D( 0.0f, 0.0f, 1.0f );
}
