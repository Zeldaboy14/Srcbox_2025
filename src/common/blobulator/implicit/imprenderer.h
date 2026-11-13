//===== Reconstructed from particles.lib debug info (src/common/blobulator/implicit/imprenderer.h) =====//
#ifndef BLOB_IMPRENDERER_H
#define BLOB_IMPRENDERER_H
#pragma once

#include "blobulator/smartarray.h"

class SweepRenderer;

// pool of SweepRenderers (they are expensive to create)
class ImpRendererFactory
{
public:
	static ImpRendererFactory *factory;

	SweepRenderer *getRenderer();
	void returnRenderer( SweepRenderer *renderer );

private:
	ImpRendererFactory();

	SmartArray< SweepRenderer * > m_renderers;
};

#endif // BLOB_IMPRENDERER_H
