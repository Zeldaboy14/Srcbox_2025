//===== Reconstructed from particles.lib debug info (src/blobulator/util/rand.h) =====//
#ifndef BLOB_RAND_H
#define BLOB_RAND_H
#pragma once

#include <stdlib.h>

// uniform random float in [0,1]
inline float frand()
{
	return (float)rand() * ( 1.0f / 32767.0f );
}

#endif // BLOB_RAND_H
