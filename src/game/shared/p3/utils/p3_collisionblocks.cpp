#include "cbase.h"
#include "p3_collisionblocks.h"
#include "p3_util_strings.h"
#include "debugoverlay_shared.h"
#include "engine/SndInfo.h"
#include "engine/IEngineSound.h"
#include "vphysics_interface.h"
#include "vphysics/collision_set.h"

CP3_CollisionBlocks g_CollisionBlocks;

BEGIN_SIMPLE_DATADESC( collision_description_t )
	DEFINE_FIELD( m_vecCollisionSize, FIELD_VECTOR ),
	DEFINE_FIELD( m_nCollisionAttachment, FIELD_INTEGER ),
	DEFINE_FIELD( m_nCollisionBlockHandle, FIELD_INTEGER ),
	DEFINE_FIELD( m_vecCollisionPrevPos, FIELD_VECTOR ),
	DEFINE_FIELD( m_vecOrigin, FIELD_VECTOR ),
	DEFINE_FIELD( m_hOwner, FIELD_EHANDLE ),
END_DATADESC()

collision_description_t::collision_description_t():
m_nCollisionBlockHandle(-1),
m_vecCollisionSize(vec3_origin),
m_nCollisionAttachment(-1),
m_vecCollisionPrevPos(vec3_origin)
{
}

void collision_description_t::Reset( CBaseAnimating *pOwner )
{
	Remove();

	Assert(pOwner);
	m_hOwner = pOwner;

	Vector vecOrigin;
	pOwner->GetAttachment( m_nCollisionAttachment, vecOrigin );
	m_nCollisionBlockHandle = g_CollisionBlocks.Add( pOwner, vecOrigin-m_vecCollisionSize, vecOrigin+m_vecCollisionSize );

	Assert( m_nCollisionBlockHandle>=0 );

	m_vecOrigin = pOwner->GetAbsOrigin();
	m_vecCollisionPrevPos = m_vecOrigin;
}

void collision_description_t::ReadCollisionInfo( char *pszSource )
{
	Assert(pszSource);

	if ( NULL==pszSource )
		return;

	const int nPartsNumber = 3;
	char sPart[nPartsNumber][70];
	int nParts = SplitStr(pszSource,",",sPart);

	Assert(nPartsNumber==nParts);

	if (nPartsNumber==nParts)
	{
		m_vecCollisionSize.x = atof(sPart[0]);					
		m_vecCollisionSize.y = atof(sPart[1]);
		m_vecCollisionSize.z = atof(sPart[2]);
	}
}

void collision_description_t::ProcessCollision( IPartitionEnumerator *enumerator )
{	
	Assert( m_nCollisionAttachment>0 && m_hOwner.Get() );

	if ( NULL==m_hOwner )
		return;

	m_hOwner->GetAttachment( m_nCollisionAttachment, m_vecOrigin );

	Vector mins(-m_vecCollisionSize), maxs(m_vecCollisionSize);

	g_CollisionBlocks.Update( m_nCollisionBlockHandle, m_vecOrigin+mins, m_vecOrigin+maxs );	
#ifndef CLIENT_DLL
	if ( m_hOwner->m_debugOverlays & OVERLAY_NPC_SELECTED_BIT )
		NDebugOverlay::Box( m_vecOrigin, mins, maxs, 255,0,0, 0, 0.1 );
#endif

	Vector vecDynamic( m_vecOrigin-m_vecCollisionPrevPos );	

	if ( enumerator && !vecDynamic.IsZero() )
	{		
		partition->EnumerateElementsInBox( PARTITION_ENGINE_NON_STATIC_EDICTS, m_vecOrigin+mins, m_vecOrigin+maxs, true, enumerator );
	}

	m_vecCollisionPrevPos = m_vecOrigin;
}

void collision_description_t::Remove()
{
	if ( m_nCollisionBlockHandle != -1 )
	{
		g_CollisionBlocks.Remove( m_nCollisionBlockHandle );
		m_nCollisionBlockHandle = -1;
	}

	m_hOwner = NULL;
}

SF void RhinoCollisionShit::FSpawn(CBaseAnimating* owner)
{
	FReadCollisionDescription(owner);

	m_FrontCollision.Reset(owner);
	m_CenterCollision.Reset(owner);
	m_BackCollision.Reset(owner);

	g_CollisionBlocks.AddClient( owner );
}

SF void RhinoCollisionShit::FRemove(CBaseAnimating* owner)
{
	g_CollisionBlocks.RemoveClient( owner );

	m_FrontCollision.Remove();
	m_CenterCollision.Remove();
	m_BackCollision.Remove();
}

bool RhinoCollisionShit::FReadCollisionDescription(CBaseAnimating *pAnimating) 
{ 
	const int iCollisionBoxNumber = 3;	

	if ( NULL==pAnimating )
		return false;

	CStudioHdr *pStudioHdr = pAnimating->GetModelPtr( );
	if (!pStudioHdr)
	{
		Assert(!"CP3_NPC_Rhino::ReadCollisionDescription: model missing");
		return false;
	}

	if ( pStudioHdr->SequencesAvailable() )
	{
		int nCount = 0;
		// Extract the bone index from the name
		for (int i = 0; i < pStudioHdr->GetNumAttachments(); i++)
		{
			const char *pszFrontPrefix = "P3CollisionFront:";
			int iPrefixLength = strlen(pszFrontPrefix);

			char *pszName = pStudioHdr->pAttachment(i).pszName( );
			if (!strncmp(pszFrontPrefix,pszName,iPrefixLength)) 
			{
				m_FrontCollision.ReadCollisionInfo( pszName + iPrefixLength );
				m_FrontCollision.m_nCollisionAttachment = i+1;

				++nCount;
			}

			const char *pszCenterPrefix = "P3CollisionCenter:";
			iPrefixLength = strlen(pszCenterPrefix);

			pszName = pStudioHdr->pAttachment(i).pszName( );
			if (!strncmp(pszCenterPrefix,pszName,iPrefixLength)) 
			{
				m_CenterCollision.ReadCollisionInfo( pszName + iPrefixLength );
				m_CenterCollision.m_nCollisionAttachment = i+1;

				++nCount;
			}

			const char *pszBackPrefix = "P3CollisionBack:";
			iPrefixLength = strlen(pszBackPrefix);

			pszName = pStudioHdr->pAttachment(i).pszName( );
			if (!strncmp(pszBackPrefix,pszName,iPrefixLength)) 
			{
				m_BackCollision.ReadCollisionInfo( pszName + iPrefixLength );
				m_BackCollision.m_nCollisionAttachment = i+1;

				++nCount;
			}

			if ( iCollisionBoxNumber <= nCount )
				break;
		}
	}

	return true;
}

bool P3IsIntersectsWithCollisionBlocks( CBaseEntity *pOwner, const Vector &vecOrigin, const Vector &vecMins, const Vector &vecMaxs, CBaseEntity *&pOutBlockOwner )
{		
	return g_CollisionBlocks.IsIntersects( pOwner, vecOrigin+vecMins, vecOrigin+vecMaxs, pOutBlockOwner );
}

void P3InitCollisionBlocks()
{
	g_CollisionBlocks.Reset();
}

CP3_CollisionBlocks::CP3_CollisionBlocks()
{
	Reset();
}

void CP3_CollisionBlocks::Reset()
{
	for ( int i=0; i<MAX_BLOCKS_NUMBER; ++i )
		Remove( i );

	for ( int i=0; i<MAX_CLIENTS_NUMBER; ++i )
	{
		m_Clients[i] = NULL;		
	}
}

int CP3_CollisionBlocks::Add( CBaseEntity *pOwner, const Vector &vecMins, const Vector &vecMaxs )
{
	Assert( pOwner );

	if ( NULL==pOwner )
		return -1;

	for ( int i=0; i<MAX_BLOCKS_NUMBER; ++i )
	{
		if ( !m_Blocks[i].bUsed )
		{			
			m_Blocks[i].vecMins = vecMins;
			m_Blocks[i].vecMaxs = vecMaxs;
			m_Blocks[i].bUsed	= true;
			m_Blocks[i].hOwner	= pOwner;
			return i;
		}
	}

	Assert(!"CP3_CollisionBlocks::AddBlock failed\n");
	return -1;
}

void CP3_CollisionBlocks::Remove( int nBlock )
{
	Assert( nBlock>=0 && nBlock<MAX_BLOCKS_NUMBER);

	if ( nBlock<MAX_BLOCKS_NUMBER )
	{
		m_Blocks[nBlock].vecMins=vec3_origin;
		m_Blocks[nBlock].vecMaxs=vec3_origin;
		m_Blocks[nBlock].bUsed=false;
	}
}

void CP3_CollisionBlocks::Update( int nBlock, const Vector &vecMins, const Vector &vecMaxs )
{
	Assert( nBlock>=0 && nBlock<MAX_BLOCKS_NUMBER);

	if ( nBlock<MAX_BLOCKS_NUMBER )
	{
		Assert(m_Blocks[nBlock].bUsed);

		m_Blocks[nBlock].vecMins=vecMins;
		m_Blocks[nBlock].vecMaxs=vecMaxs;
	}
}

bool CP3_CollisionBlocks::IsIntersects( CBaseEntity *pOwner, const Vector &vecMins, const Vector &vecMaxs, CBaseEntity *&pOutBlockOwner ) const
{
	Assert( pOwner );

	for ( int i=0; i<MAX_BLOCKS_NUMBER; ++i )
	{
		if ( m_Blocks[i].hOwner	== pOwner )
			continue;

		if ( !m_Blocks[i].bUsed )		
			continue;
		
		
		if ( m_Blocks[i].vecMins.x <= vecMaxs.x && m_Blocks[i].vecMins.y <= vecMaxs.y && m_Blocks[i].vecMins.z <= vecMaxs.z &&
			 m_Blocks[i].vecMaxs.x >= vecMins.x && m_Blocks[i].vecMaxs.y >= vecMins.y && m_Blocks[i].vecMaxs.z >= vecMins.z )
		{
			pOutBlockOwner = m_Blocks[i].hOwner;
			return true;
		}
	}

	return false;	
}

Vector CP3_CollisionBlocks::GetRetreatDirectoin( CBaseEntity *pOwner, const Vector &vecMins, const Vector &vecMaxs ) const
{
	Assert( pOwner );

	Vector vecDirection = vec3_origin;

	for ( int i=0; i<MAX_BLOCKS_NUMBER; ++i )
	{
		if ( m_Blocks[i].hOwner	== pOwner )
			continue;

		if ( !m_Blocks[i].bUsed )		
			continue;		
		
		if ( m_Blocks[i].vecMins.x <= vecMaxs.x && m_Blocks[i].vecMins.y <= vecMaxs.y && m_Blocks[i].vecMins.z <= vecMaxs.z &&
			 m_Blocks[i].vecMaxs.x >= vecMins.x && m_Blocks[i].vecMaxs.y >= vecMins.y && m_Blocks[i].vecMaxs.z >= vecMins.z )
		{
			//NDebugOverlay::HorzArrow(  (m_Blocks[i].vecMins + (m_Blocks[i].vecMaxs-m_Blocks[i].vecMins)*0.5f),  (vecMins + (vecMaxs-vecMins)*0.5f), 1, 255,0,0,255, true, 5.f );

			Vector vecToEntity = (vecMins + (vecMaxs-vecMins)*0.5f) - (m_Blocks[i].vecMins + (m_Blocks[i].vecMaxs-m_Blocks[i].vecMins)*0.5f);
			vecToEntity.NormalizeInPlace();
			vecDirection += vecToEntity;
		}
	}

	vecDirection.NormalizeInPlace();
	return vecDirection;	
}

bool CP3_CollisionBlocks::AddClient( CBaseEntity *pClient )
{
	bool bAdded=false;
	
	for ( int i=0; i<MAX_CLIENTS_NUMBER; ++i )
	{
		if (pClient==m_Clients[i])
		{
			Assert(false);
			return false;
		}
	}

	for ( int i=0; i<MAX_CLIENTS_NUMBER; ++i )
	{
		if (NULL==m_Clients[i])
		{
			m_Clients[i] = pClient;
			bAdded = true;
			break;
		}
	}

	Assert(bAdded);

	return bAdded;
}

void CP3_CollisionBlocks::RemoveClient( CBaseEntity *pClient )
{
	for ( int i=0; i<MAX_CLIENTS_NUMBER; ++i )
	{
		if (pClient==m_Clients[i])
		{
			m_Clients[i] = NULL;			
		}
	}
}

void CP3_CollisionBlocks::ClipTraceToBlockOwners( const Vector& vecAbsStart, const Vector& vecAbsEnd, unsigned int mask, ITraceFilter *filter, trace_t *tr )
{
	trace_t entityTrace;
	Ray_t ray;
	float smallestFraction = 1; //tr->fraction;
	const float maxRange = 60.0f;

	ray.Init( vecAbsStart, vecAbsEnd );

	for ( int i = 0; i <= MAX_CLIENTS_NUMBER; ++i )
	{
		CBaseEntity *pEntity = m_Clients[i];

		if ( !pEntity || !pEntity->IsAlive() )
			continue;

		if ( filter && filter->ShouldHitEntity( pEntity, mask ) == false )
			continue;

		float range = DistanceToRay( pEntity->WorldSpaceCenter(), vecAbsStart, vecAbsEnd );
		if ( range < 0.0f || range > maxRange )
			continue;

		enginetrace->ClipRayToEntity( ray, mask|CONTENTS_HITBOX, pEntity, &entityTrace );
		if ( entityTrace.fraction < smallestFraction )
		{
			// we shortened the ray - save off the trace
			*tr = entityTrace;
			smallestFraction = entityTrace.fraction;
		}
	}
}

void CP3_CollisionBlocks::FuckingFuck( const Vector& vecAbsStart, const Vector& vecAbsEnd, unsigned int mask, ITraceFilter *filter, trace_t *tr )
{
	trace_t entityTrace;
	Ray_t ray;
	float smallestFraction = 1; //tr->fraction;	

	ray.Init( vecAbsStart, vecAbsEnd );

	for ( int i = 0; i <= MAX_CLIENTS_NUMBER; ++i )
	{
		CBaseEntity *pEntity = m_Clients[i];

		if ( !pEntity || !pEntity->IsAlive() )
			continue;

		if ( filter && filter->ShouldHitEntity( pEntity, mask ) == false )
			continue;	

		enginetrace->ClipRayToEntity( ray, mask|CONTENTS_HITBOX, pEntity, &entityTrace );
		if ( entityTrace.fraction < smallestFraction )
		{
			// we shortened the ray - save off the trace
			*tr = entityTrace;
			smallestFraction = entityTrace.fraction;
		}
	}
}