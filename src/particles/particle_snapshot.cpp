//===== Copyright (c) Valve Corporation, All rights reserved. ======//
//
// Purpose: CParticleSnapshot - a CSOAContainer whose fields are labelled
//          with particle attributes, so particle systems can read/write it
//
// Reconstructed from the Alien Swarm SDK particles.lib (particle_snapshot.obj).
//
//===========================================================================//

#include "tier0/platform.h"
#include "particles/particles.h"
#include "dmxloader/dmxloader.h"
#include "dmxloader/dmxelement.h"
#include "tier1/utlsoacontainer.h"
#include "tier1/strtools.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"


//-----------------------------------------------------------------------------
// CSOAContainer <-> DMX conversion lives in dmxloader.lib (so tier1 does not
// depend on it). The signatures must match the ones exported there.
//-----------------------------------------------------------------------------
bool SerializeCSOAContainer( const CSOAContainer *pContainer, CDmxElement *pRootElement );
bool UnserializeCSOAContainer( const CSOAContainer *pContainer, const CDmxElement *pRootElement );


//-----------------------------------------------------------------------------
// Clears the snapshot and its embedded container, and removes all mappings
//-----------------------------------------------------------------------------
void CParticleSnapshot::Purge( void )
{
	m_Container.Purge();
	m_pContainer = NULL;
	for ( int i = 0; i < MAX_PARTICLE_ATTRIBUTES; i++ ) m_ParticleAttributeToContainerAttribute[ i ] = -1;
	for ( int i = 0; i < MAX_SOA_FIELDS; i++ ) m_ContainerAttributeToParticleAttribute[ i ] = -1;
}


//-----------------------------------------------------------------------------
// Records one container field <-> particle attribute pair. Fails (and purges)
// if either side is already mapped to something else.
//-----------------------------------------------------------------------------
bool CParticleSnapshot::AddAttributeMapping( int nFieldNumber, int nParticleAttribute, const char *pFunc )
{
	if ( ( ( m_ParticleAttributeToContainerAttribute[ nParticleAttribute ] != -1 ) && ( m_ParticleAttributeToContainerAttribute[ nParticleAttribute ] != nFieldNumber ) ) ||
		 ( ( m_ContainerAttributeToParticleAttribute[ nFieldNumber ] != -1 ) && ( m_ContainerAttributeToParticleAttribute[ nFieldNumber ] != nParticleAttribute ) ) )
	{
		Warning( "CParticleSnapshot::%s - Invalid attribute mapping specified (must be one-to-one)!\n", pFunc );
		Purge();
		return false;
	}

	m_ParticleAttributeToContainerAttribute[ nParticleAttribute ] = nFieldNumber;
	m_ContainerAttributeToParticleAttribute[ nFieldNumber ] = nParticleAttribute;
	return true;
}


//-----------------------------------------------------------------------------
// Checks that a mapped container field has the particle attribute's data type
// and has memory behind it. Fails (and purges) otherwise.
//-----------------------------------------------------------------------------
bool CParticleSnapshot::ValidateAttributeMapping( int nFieldNumber, int nParticleAttribute, const char *pFunc )
{
	EAttributeDataType nDataType = g_pParticleSystemMgr->GetParticleAttributeDataType( nParticleAttribute );
	if ( ( m_pContainer->GetAttributeType( nFieldNumber ) != nDataType ) || !m_pContainer->HasAllocatedMemory( nFieldNumber ) )
	{
		Warning( "CParticleSnapshot::%s - Invalid attribute mapping specified for the provided container!\n", pFunc );
		if ( m_pContainer->GetAttributeType( nFieldNumber ) != nDataType )
		{
			Warning( "  (data type of container field %d does not match particle attribute %s)\n", nFieldNumber, g_pParticleSystemMgr->GetParticleAttributeName( nParticleAttribute ) );
		}
		if ( !m_pContainer->HasAllocatedMemory( nFieldNumber ) )
		{
			Warning( "  (container field %d has no allocated data)\n", nFieldNumber );
		}
		Purge();
		return false;
	}
	return true;
}


//-----------------------------------------------------------------------------
// Creates the embedded container with one field per mapping
//-----------------------------------------------------------------------------
bool CParticleSnapshot::Init( int nX, int nY, int nZ, const AttributeMapVector &attributeMaps )
{
	if ( attributeMaps.Count() <= 0 )
		return false;

	if ( ( nX < 1 ) || ( nY < 1 ) || ( nZ < 1 ) )
		return false;

	Purge();

	m_pContainer = &m_Container;
	for ( int i = 0; i < attributeMaps.Count(); i++ )
	{
		int nFieldNumber = attributeMaps[ i ].m_nContainerAttribute;
		int nParticleAttribute = attributeMaps[ i ].m_nParticleAttribute;
		if ( !AddAttributeMapping( nFieldNumber, nParticleAttribute, "Init" ) )
			return false;

		// Each field takes the data type of the particle attribute it holds
		EAttributeDataType nDataType = g_pParticleSystemMgr->GetParticleAttributeDataType( nParticleAttribute );
		m_Container.SetAttributeType( nFieldNumber, nDataType );
	}
	m_Container.AllocateData( nX, nY, nZ );

	return true;
}

// Varargs form: (field, attribute) pairs terminated by -1
bool CParticleSnapshot::Init( int nX, int nY, int nZ, ... )
{
	AttributeMapVector attributeMaps;
	va_list argList;
	va_start( argList, nZ );
	int nFieldNumber;
	while ( ( nFieldNumber = va_arg( argList, int ) ) != -1 )
	{
		int nParticleAttribute = va_arg( argList, int );
		attributeMaps.AddToTail( AttributeMap( nFieldNumber, nParticleAttribute ) );
	}
	va_end( argList );

	return Init( nX, nY, nZ, attributeMaps );
}


//-----------------------------------------------------------------------------
// Wraps an existing container; every mapped field must already exist with the
// right type
//-----------------------------------------------------------------------------
bool CParticleSnapshot::InitExternal( CSOAContainer *pContainer, const AttributeMapVector &attributeMaps )
{
	if ( attributeMaps.Count() <= 0 )
		return false;

	Purge();
	m_pContainer = pContainer;

	for ( int i = 0; i < attributeMaps.Count(); i++ )
	{
		int nFieldNumber = attributeMaps[ i ].m_nContainerAttribute;
		int nParticleAttribute = attributeMaps[ i ].m_nParticleAttribute;
		if ( !AddAttributeMapping( nFieldNumber, nParticleAttribute, "InitExternal" ) ||
			 !ValidateAttributeMapping( nFieldNumber, nParticleAttribute, "InitExternal" ) )
			return false;
	}

	return true;
}

// Varargs form: (field, attribute) pairs terminated by -1
bool CParticleSnapshot::InitExternal( CSOAContainer *pContainer, ... )
{
	AttributeMapVector attributeMaps;
	va_list argList;
	va_start( argList, pContainer );
	int nFieldNumber;
	while ( ( nFieldNumber = va_arg( argList, int ) ) != -1 )
	{
		int nParticleAttribute = va_arg( argList, int );
		attributeMaps.AddToTail( AttributeMap( nFieldNumber, nParticleAttribute ) );
	}
	va_end( argList );

	return InitExternal( pContainer, attributeMaps );
}

#ifdef PARTICLE_SNAPSHOT
//-----------------------------------------------------------------------------
// DMX layout of a snapshot: the two mapping tables (the container itself is
// written separately as a "container" sub-element)
//-----------------------------------------------------------------------------
BEGIN_DMXELEMENT_UNPACK( CParticleSnapshot )
	DMXELEMENT_UNPACK_FIELD_ARRAY( "particle_attribute_to_container_attribute", "-1", int, m_ParticleAttributeToContainerAttribute )
	DMXELEMENT_UNPACK_FIELD_ARRAY( "container_attribute_to_particle_attribute", "-1", int, m_ContainerAttributeToParticleAttribute )
END_DMXELEMENT_UNPACK( CParticleSnapshot, s_pParticleSnapshotUnpack )


//-----------------------------------------------------------------------------
// Compile-time guard: the .psf format stores particle attribute indices, so if
// the attribute set changes the format version must be bumped and old files
// converted
//-----------------------------------------------------------------------------
void CParticleSnapshot::CheckParticleAttributesForChanges( void )
{
	COMPILE_TIME_ASSERT( MAX_PARTICLE_ATTRIBUTES == 24 );
	COMPILE_TIME_ASSERT( PARTICLE_ATTRIBUTE_XYZ == 0 );
	COMPILE_TIME_ASSERT( PARTICLE_ATTRIBUTE_LIFE_DURATION == 1 );
	COMPILE_TIME_ASSERT( PARTICLE_ATTRIBUTE_PREV_XYZ == 2 );
	COMPILE_TIME_ASSERT( PARTICLE_ATTRIBUTE_RADIUS == 3 );
	COMPILE_TIME_ASSERT( PARTICLE_ATTRIBUTE_ROTATION == 4 );
	COMPILE_TIME_ASSERT( PARTICLE_ATTRIBUTE_ROTATION_SPEED == 5 );
	COMPILE_TIME_ASSERT( PARTICLE_ATTRIBUTE_TINT_RGB == 6 );
	COMPILE_TIME_ASSERT( PARTICLE_ATTRIBUTE_ALPHA == 7 );
	COMPILE_TIME_ASSERT( PARTICLE_ATTRIBUTE_CREATION_TIME == 8 );
	COMPILE_TIME_ASSERT( PARTICLE_ATTRIBUTE_SEQUENCE_NUMBER == 9 );
	COMPILE_TIME_ASSERT( PARTICLE_ATTRIBUTE_TRAIL_LENGTH == 10 );
	COMPILE_TIME_ASSERT( PARTICLE_ATTRIBUTE_PARTICLE_ID == 11 );
	COMPILE_TIME_ASSERT( PARTICLE_ATTRIBUTE_YAW == 12 );
	COMPILE_TIME_ASSERT( PARTICLE_ATTRIBUTE_SEQUENCE_NUMBER1 == 13 );
	COMPILE_TIME_ASSERT( PARTICLE_ATTRIBUTE_HITBOX_INDEX == 14 );
	COMPILE_TIME_ASSERT( PARTICLE_ATTRIBUTE_HITBOX_RELATIVE_XYZ == 15 );
	COMPILE_TIME_ASSERT( PARTICLE_ATTRIBUTE_ALPHA2 == 16 );
	COMPILE_TIME_ASSERT( PARTICLE_ATTRIBUTE_TRACE_P0 == 17 );
	COMPILE_TIME_ASSERT( PARTICLE_ATTRIBUTE_TRACE_P1 == 18 );
	COMPILE_TIME_ASSERT( PARTICLE_ATTRIBUTE_TRACE_HIT_T == 19 );
	COMPILE_TIME_ASSERT( PARTICLE_ATTRIBUTE_TRACE_HIT_NORMAL == 20 );
	COMPILE_TIME_ASSERT( PARTICLE_ATTRIBUTE_NORMAL == 21 );
	COMPILE_TIME_ASSERT( PARTICLE_ATTRIBUTE_GLOW_RGB == 22 );
	COMPILE_TIME_ASSERT( PARTICLE_ATTRIBUTE_GLOW_ALPHA == 23 );
}


//-----------------------------------------------------------------------------
// Loads a .psf file (text or binary DMX) into the embedded container
//-----------------------------------------------------------------------------
bool CParticleSnapshot::Unserialize( const char *pFullPath )
{
	DECLARE_DMX_CONTEXT();

	CDmxElement *pRootElement = NULL;
	bool bSuccess = true;

	// Accept either encoding
	if ( !UnserializeDMX( pFullPath, "GAME", true, &pRootElement ) &&
		 !UnserializeDMX( pFullPath, "GAME", false, &pRootElement ) )
	{
		Warning( "ERROR: CParticleSnapshot::Unserialize - could not load file %s!\n", pFullPath );
		return false;
	}

	CDmxElement *pParticleSnapshotElement = pRootElement->GetValue< CDmxElement * >( "particle_snapshot" );
	if ( !pParticleSnapshotElement )
	{
		Warning( "ERROR: CParticleSnapshot::Unserialize - %s is not a particle snapshot (.psf) file!\n", pFullPath );
		bSuccess = false;
	}
	else
	{
		int nVersion = pParticleSnapshotElement->GetValue< int >( "version", -1 );
		if ( nVersion == -1 )
		{
			Warning( "ERROR: CParticleSnapshot::Unserialize - missing version field in file %s\n", pFullPath );
			bSuccess = false;
		}
		else
		{
			Purge();
			pParticleSnapshotElement->UnpackIntoStructure( this, s_pParticleSnapshotUnpack );

			CDmxElement *pContainerElement = pParticleSnapshotElement->GetValue< CDmxElement * >( "container" );
			if ( !UnserializeCSOAContainer( &m_Container, pContainerElement ) )
			{
				Warning( "ERROR: CParticleSnapshot::Unserialize - error reading embedded CSOAContainer in file %s\n", pFullPath );
				bSuccess = false;
			}
			else
			{
				switch ( nVersion )
				{
				case 1:
					{
						// The mapping tables were just unpacked; check them in both directions
						int nForwardMaps = 0, nReverseMaps = 0;
						m_pContainer = &m_Container;

						for ( int i = 0; i < MAX_PARTICLE_ATTRIBUTES; i++ )
						{
							int nFieldNumber = m_ParticleAttributeToContainerAttribute[ i ];
							if ( nFieldNumber == -1 )
								continue;

							if ( !AddAttributeMapping( nFieldNumber, i, "Unserialize" ) ||
								 !ValidateAttributeMapping( nFieldNumber, i, "Unserialize" ) )
							{
								bSuccess = false;
								break;
							}
							nForwardMaps++;
						}

						for ( int i = 0; i < MAX_SOA_FIELDS; i++ )
						{
							int nParticleAttribute = m_ContainerAttributeToParticleAttribute[ i ];
							if ( nParticleAttribute == -1 )
								continue;

							if ( !AddAttributeMapping( i, nParticleAttribute, "Unserialize" ) ||
								 !ValidateAttributeMapping( i, nParticleAttribute, "Unserialize" ) )
							{
								bSuccess = false;
								break;
							}
							nReverseMaps++;
						}

						if ( bSuccess && ( !nForwardMaps || !nReverseMaps ) )
						{
							Warning( "ERROR: CParticleSnapshot::Unserialize - error in data in file %s (no attribute mapping specified)\n", pFullPath );
							bSuccess = false;
						}
					}
					break;

				default:
					// unknown version
					bSuccess = false;
					break;
				}
			}
		}
	}

	if ( !bSuccess )
	{
		Purge();
	}

	CleanupDMX( pRootElement );
	return bSuccess;
}


//-----------------------------------------------------------------------------
// Writes the snapshot to a .psf file
//-----------------------------------------------------------------------------
bool CParticleSnapshot::Serialize( const char *pFullPath, bool bTextMode )
{
	DECLARE_DMX_CONTEXT();

	if ( !m_pContainer )
	{
		Warning( "ERROR: CParticleSnapshot::Serialize - cannot serialize an uninitialized CParticleSnapshot! (%s)\n", pFullPath );
		return false;
	}

	const char *pExt = V_GetFileExtension( pFullPath );
	if ( !pExt || V_stricmp( pExt, "psf" ) )
	{
		Warning( "ERROR: CParticleSnapshot::Serialize - file extension should be '.psf' (%s)\n", pFullPath );
		return false;
	}

	bool bSuccess = true;

	CDmxElement *pRootElement = CreateDmxElement( "CDmeElement" );
	{
		CDmxElementModifyScope modifyRoot( pRootElement );

		CDmxElement *pParticleSnapshotElement = CreateDmxElement( "CDmeParticleSnapshot" );
		pRootElement->SetValue( "particle_snapshot", pParticleSnapshotElement );

		int nVersion = PARTICLE_SNAPSHOT_DMX_VERSION;
		pParticleSnapshotElement->SetValue( "version", nVersion );

		// mapping tables
		pParticleSnapshotElement->AddAttributesFromStructure( this, s_pParticleSnapshotUnpack );

		// container data (an external container is written out as if embedded)
		CDmxElement *pContainerElement = CreateDmxElement( "CDmeSOAContainer" );
		pParticleSnapshotElement->SetValue( "container", pContainerElement );
		if ( !SerializeCSOAContainer( m_pContainer, pContainerElement ) )
		{
			Warning( "ERROR: CParticleSnapshot::Serialize - error serializing embedded CSOAContainer for file %s\n", pFullPath );
			bSuccess = false;
		}
	}

	if ( bSuccess )
	{
		bSuccess = SerializeDMX( pFullPath, "GAME", bTextMode, pRootElement );
	}

	CleanupDMX( pRootElement );
	return bSuccess;
}
#endif
