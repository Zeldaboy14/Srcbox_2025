//===== Reconstructed from particles.lib (ProjectingParticleCache.obj), src/blobulator/implicit/projectingparticlecache.cpp =====//

// this file uses the truncating (cvttss2si) variants of the fast float->int helpers
#define BLOB_SSE_TRUNC
#include "blobulator/util/fasttrunc.h"
#include "blobulator/implicit/projectingparticlecache.h"
#include "tier0/dbg.h"

ProjectingParticleCache::ProjectingParticleCache()
{
	curCacheElement = 0;
	cachePlane = ( PCacheElem_t *( * )[zsizeP1] )malloc( ysizeP1 * zsizeP1 * sizeof( PCacheElem_t * ) );

	botSentinel = new PCacheElem_t();
	midSentinel = new PCacheElem_t();
	topSentinel = new PCacheElem_t();

	botSentinel->center_x = -32767;
	botSentinel->bot_x = 0;
	botSentinel->top_x = 0;
	botSentinel->prev = NULL;
	botSentinel->next = midSentinel;
	botSentinel->p = NULL;

	midSentinel->center_x = 32767;
	midSentinel->bot_x = 0;
	midSentinel->top_x = 0;
	midSentinel->prev = botSentinel;
	midSentinel->next = topSentinel;
	midSentinel->p = NULL;

	topSentinel->center_x = 32767;
	topSentinel->bot_x = 0;
	topSentinel->top_x = 0;
	topSentinel->prev = midSentinel;
	topSentinel->next = NULL;
	topSentinel->p = NULL;

	for ( int y = 0; y < ysizeP1; y++ )
	{
		for ( int z = 0; z < zsizeP1; z++ )
		{
			cachePlane[y][z] = midSentinel;
		}
	}
}

ProjectingParticleCache::~ProjectingParticleCache()
{
	free( cachePlane );
}

void ProjectingParticleCache::beginTile()
{
}

void ProjectingParticleCache::addParticle( ImpParticle *p )
{
	particles.pushAutoSize( p );
}

class C
{
public:
	static bool gt( ImpParticle *a, ImpParticle *b )
	{
		return b->center[0] > a->center[0];
	}
};

void ProjectingParticleCache::buildCache( float cw, float fieldRadius, Point3D &mins, Point3D &maxs )
{
	particles.sort< C >();
	if ( !particles.testSort< C >() )
	{
		ConMsg( "Error: Sort failed\n" );
	}

	for ( int i = 0; i < particles.size; i++ )
	{
		insertIntoCache( particles[i], cw, fieldRadius, mins, maxs );
	}
}

void ProjectingParticleCache::endTile()
{
	particles.size = 0;
	curCacheElement = 0;

	for ( int i = 0; i < clearList.size; i++ )
	{
		pcache_YZ_t e = clearList[i];
		cachePlane[e.y][e.z] = midSentinel;
	}
	clearList.size = 0;
}

void ProjectingParticleCache::insertIntoCache( ImpParticle *p, float cw, float fieldRadius, Point3D &mins, Point3D &maxs )
{
	float oneOverCw = 1.0f / cw;
	Point3D c = ( p->center - mins ) / cw;			// c = particle center in voxel units
	float fr = ( p->scale * fieldRadius ) * oneOverCw;	// field radius in voxel units
	float frSq = fr * fr;

	// make sure there are enough free cache elements for this particle
	int maxElements = (int)( fr * 2.0f + 2.0f );
	maxElements *= maxElements;
	int available = cacheElements.size - curCacheElement;
	if ( available < maxElements )
	{
		int nNew = maxElements - available;
		for ( int i = 0; i < nNew; i++ )
		{
			cacheElements.pushAutoSize( new PCacheElem_t() );
		}
	}

	int min_y = blob_round_up( c[1] - fr );
	if ( min_y < 1 )
		min_y = 1;
	int max_y = blob_trunc( c[1] + fr );
	if ( max_y >= ysize - 1 )
		max_y = ysize - 1;

	int min_z = blob_round_up( c[2] - fr );
	if ( min_z < 1 )
		min_z = 1;
	int max_z = blob_trunc( c[2] + fr );
	if ( max_z >= zsize - 1 )
		max_z = zsize - 1;

	for ( int i = min_y; i <= max_y; i++ )
	{
		float dy = c[1] - (float)i;
		float dySq = dy * dy;

		for ( int j = min_z; j <= max_z; j++ )
		{
			float dz = c[2] - (float)j;
			float distSq = dz * dz + dySq;
			float dxSq = frSq - distSq;

			if ( dxSq > 0.0f )
			{
				float dx = sqrtf( dxSq );

				int bot_x = blob_round_up( c[0] - dx );
				int top_x = blob_trunc( c[0] + dx );
				if ( bot_x < 1 )
					bot_x = 1;
				if ( top_x >= xsize - 1 )
					top_x = xsize - 1;

				if ( bot_x <= top_x )
				{
					PCacheElem_t **pCell = &cachePlane[i][j];

					PCacheElem_t *e = cacheElements[curCacheElement++];
					e->bot_x = bot_x;
					e->top_x = top_x;
					e->center_x = (short)blob_trunc( c[0] );

					// NOTE: shipped code links every element back to the bottom sentinel
					e->prev = botSentinel;
					e->next = *pCell;
					e->p = p;

					if ( *pCell != midSentinel )
					{
						( *pCell )->prev = e;
					}
					else
					{
						pcache_YZ_t yz;
						yz.y = i;
						yz.z = j;
						clearList.pushAutoSize( yz );
					}

					*pCell = e;
				}
			}
		}
	}
}
