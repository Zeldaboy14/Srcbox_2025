//===== Reconstructed from particles.lib (ImpTiler.obj), src/blobulator/implicit/imptiler.cpp =====//

#include "blobulator/implicit/imptiler.h"
#include "blobulator/implicit/sweeprenderer.h"
#include "blobulator/implicit/imprenderer.h"
#include "blobulator/util/fasttrunc.h"
#include "../util/rand.h"

SweepRenderer **renderers;
Point3D tile_debug_color;

ImpTiler::ImpTiler( SweepRenderer *sweepRenderer )
{
	m_sweepRenderer = sweepRenderer;
	maxNoTilesToDraw = -1;
}

ImpTiler::~ImpTiler()
{
}

void ImpTiler::beginFrame( Point3D &_offset, void *pRenderContext, bool tile_mode )
{
	offset = _offset;
	render_dim = SweepRenderer::getInnerDimensions();
	render_margin = SweepRenderer::getMarginWidth();
	corner_offset = offset - render_dim * 0.5f;

	m_sweepRenderer->beginFrame( tile_mode, pRenderContext );
}

void ImpTiler::insertParticle( ImpParticle *p )
{
	Point3D pos = p->center;
	int x = blob_trunc( ( pos[0] - corner_offset[0] ) / render_dim[0] );
	int y = blob_trunc( ( pos[1] - corner_offset[1] ) / render_dim[1] );
	int z = blob_trunc( ( pos[2] - corner_offset[2] ) / render_dim[2] );

	Point3D corner = calcTileCorner( x, y, z );

	// which of the three neighbouring tiles along each axis the particle's margin reaches
	bool xs[3], ys[3], zs[3];
	xs[0] = corner[0] + render_margin >= p->center[0];
	xs[1] = true;
	xs[2] = p->center[0] >= corner[0] + render_dim[0] - render_margin;
	ys[0] = corner[1] + render_margin >= p->center[1];
	ys[1] = true;
	ys[2] = p->center[1] >= corner[1] + render_dim[1] - render_margin;
	zs[0] = corner[2] + render_margin >= p->center[2];
	zs[1] = true;
	zs[2] = p->center[2] >= corner[2] + render_dim[2] - render_margin;

	for ( int i = x - 1; i <= x + 1; i++ )
	{
		if ( !xs[i - x + 1] )
			continue;
		for ( int j = y - 1; j <= y + 1; j++ )
		{
			if ( !ys[j - y + 1] )
				continue;
			for ( int k = z - 1; k <= z + 1; k++ )
			{
				if ( !zs[k - z + 1] )
					continue;
				addParticleToTile( p, i, j, k );
			}
		}
	}
}

void ImpTiler::addParticleToTile( ImpParticle *p, int x, int y, int z )
{
	ImpTile *tile = createTile( x, y, z );
	tile->particles.pushAutoSize( p );
}

Point3D ImpTiler::calcTileCorner( int x, int y, int z )
{
	Point3D r = corner_offset;
	r[0] += x * render_dim[0];
	r[1] += y * render_dim[1];
	r[2] += z * render_dim[2];
	return r;
}

Point3D ImpTiler::calcTileOffset( int x, int y, int z )
{
	Point3D r = offset;
	r[0] += x * render_dim[0];
	r[1] += y * render_dim[1];
	r[2] += z * render_dim[2];
	return r;
}

Point3D ImpTiler::getTileOffset( int i )
{
	return tiles[i]->center;
}

void drawTile( ImpTile *tile, SweepRenderer *sweepRenderer )
{
	tile_debug_color = tile->debug_color;
	sweepRenderer->setOffset( tile->center );
	sweepRenderer->beginTile( tile );

	for ( int i = 0; i < tile->particles.size; i++ )
	{
		sweepRenderer->addParticle( tile->particles[i], false );
	}

	sweepRenderer->endTile();
}

void ImpTiler::drawTile( ImpTile *tile )
{
	::drawTile( tile, m_sweepRenderer );
	last_tiles_offset = tile->center;
}

void ImpTiler::drawTile( int x, int y, int z )
{
	ImpTile *tile = findTile( x, y, z );
	if ( tile )
	{
		drawTile( tile );
	}
}

void ImpTiler::drawSurface()
{
	for ( int i = 0; i < tiles.size; i++ )
	{
		if ( i == maxNoTilesToDraw )
			break;
		drawTile( tiles[i] );
	}
}

class C
{
public:
	static bool gt( ImpTile *a, ImpTile *b )
	{
		return b->dist > a->dist;
	}
};

void ImpTiler::drawSurfaceSorted( Point3D &center )
{
	for ( int i = 0; i < tiles.size; i++ )
	{
		ImpTile *tile = tiles[i];
		float dx = tile->center[0] - center[0];
		float dy = tile->center[1] - center[1];
		float dz = tile->center[2] - center[2];
		tile->dist = sqrtf( ( dz * dz + dy * dy ) + dx * dx );
	}

	tiles.sort< C >();

	for ( int i = 0; i < tiles.size; i++ )
	{
		if ( i == maxNoTilesToDraw )
			break;
		drawTile( tiles[i] );
	}
}

int ImpTiler::getNoTiles()
{
	return tiles.size;
}

ImpTile *ImpTiler::getTile( int i )
{
	return tiles[i];
}

void ImpTiler::endFrame()
{
	// keep the tiles that had particles this frame for reuse, delete the rest
	SmartArray< ImpTile * > new_tiles;
	for ( int i = 0; i < tiles.size; i++ )
	{
		if ( tiles[i]->particles.size > 0 )
		{
			tiles[i]->old_tile = true;
			tiles[i]->particles.size = 0;
			new_tiles.pushAutoSize( tiles[i] );
		}
		else
		{
			delete tiles[i];
		}
	}
	tiles.swapData( new_tiles );

	m_sweepRenderer->endFrame();
}

ImpTile *ImpTiler::findTile( int x, int y, int z )
{
	for ( int i = 0; i < tiles.size; i++ )
	{
		ImpTile *tile = tiles[i];
		if ( tile->x == x && tile->y == y && tile->z == z )
		{
			if ( tile->old_tile == true )
			{
				tile->old_tile = false;
				tile->center = calcTileOffset( x, y, z );
			}
			return tile;
		}
	}
	return NULL;
}

ImpTile *ImpTiler::createTile( int x, int y, int z )
{
	ImpTile *tile = findTile( x, y, z );
	if ( !tile )
	{
		tile = new ImpTile( x, y, z );
		tile->center = calcTileOffset( x, y, z );
		tile->dist = 0.0f;
		tile->old_tile = false;

		// the arguments of set() were evaluated right to left
		float b = frand();
		float g = frand();
		float r = frand();
		tile->debug_color.set( r, g, b );

		tiles.pushAutoSize( tile );
	}
	return tile;
}

//-----------------------------------------------------------------------------
// ImpTilerFactory
//-----------------------------------------------------------------------------
ImpTilerFactory *ImpTilerFactory::factory = new ImpTilerFactory;

ImpTilerFactory::ImpTilerFactory()
{
}

ImpTiler *ImpTilerFactory::getTiler()
{
	if ( m_tilers.size > 0 )
	{
		return m_tilers.pop();
	}
	return new ImpTiler( ImpRendererFactory::factory->getRenderer() );
}

void ImpTilerFactory::returnTiler( ImpTiler *tiler )
{
	m_tilers.pushAutoSize( tiler );
}
