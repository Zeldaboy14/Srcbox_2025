//===== Reconstructed from particles.lib (ImpRenderer.obj), src/blobulator/implicit/imprenderer.cpp =====//

#include "blobulator/implicit/imprenderer.h"
#include "blobulator/implicit/sweeprenderer.h"

ImpRendererFactory *ImpRendererFactory::factory = new ImpRendererFactory;

ImpRendererFactory::ImpRendererFactory()
{
}

SweepRenderer *ImpRendererFactory::getRenderer()
{
	if ( m_renderers.size > 0 )
	{
		return m_renderers.pop();
	}
	return new SweepRenderer;
}

void ImpRendererFactory::returnRenderer( SweepRenderer *renderer )
{
	m_renderers.pushAutoSize( renderer );
}
