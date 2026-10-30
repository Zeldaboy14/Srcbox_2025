//===== Copyright (c) Valve Corporation, All rights reserved. ======//
//
// Purpose: particle system code
//
// Reconstructed from the Alien Swarm SDK particles.lib (particles.obj).
//
//===========================================================================//

#include "tier0/platform.h"
#include "particles/particles.h"
#include "bitmap/psheet.h"
#include "filesystem.h"
#include "tier2/tier2.h"
#include "tier2/fileutils.h"
#include "tier2/meshutils.h"
#include "tier1/utlbuffer.h"
#include "tier1/UtlStringMap.h"
#include "tier1/strtools.h"
#include "tier1/lzmaDecoder.h"
#include "tier1/KeyValues.h"
#include "tier0/vprof.h"
#include "dmxloader/dmxloader.h"
#include "materialsystem/imaterial.h"
#include "materialsystem/imaterialvar.h"
#include "materialsystem/itexture.h"
#include "materialsystem/imesh.h"
#include "materialsystem/imaterialsystemhardwareconfig.h"
#include "vtf/vtf.h"
#include "studio.h"
#include "raytrace.h"
#include "particles_internal.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"


//-----------------------------------------------------------------------------
// Random number table, shared by all particle systems
//-----------------------------------------------------------------------------
#include "random_floats.h"


//-----------------------------------------------------------------------------
// Multiplier applied to every particle system's maximum particle count
//-----------------------------------------------------------------------------
int g_nParticle_Multiplier = 1;


//-----------------------------------------------------------------------------
// Old operator names, remapped to their current names when loaded
//-----------------------------------------------------------------------------
static char *s_RemapOperatorNameTable[] =
{
	"alpha_fade", "Alpha Fade and Decay",
	"alpha_fade_in_random", "Alpha Fade In Random",
	"alpha_fade_out_random", "Alpha Fade Out Random",
	"basic_movement", "Movement Basic",
	"color_fade", "Color Fade",
	"controlpoint_light", "Color Light From Control Point",
	"Dampen Movement Relative to Control Point", "Movement Dampen Relative to Control Point",
	"Distance Between Control Points Scale", "Remap Distance Between Two Control Points to Scalar",
	"Distance to Control Points Scale", "Remap Distance to Control Point to Scalar",
	"lifespan_decay", "Lifespan Decay",
	"lock to bone", "Movement Lock to Bone",
	"postion_lock_to_controlpoint", "Movement Lock to Control Point",
	"maintain position along path", "Movement Maintain Position Along Path",
	"Match Particle Velocities", "Movement Match Particle Velocities",
	"Max Velocity", "Movement Max Velocity",
	"noise", "Noise Scalar",
	"vector noise", "Noise Vector",
	"oscillate_scalar", "Oscillate Scalar",
	"oscillate_vector", "Oscillate Vector",
	"Orient Rotation to 2D Direction", "Rotation Orient to 2D Direction",
	"radius_scale", "Radius Scale",
	"Random Cull", "Cull Random",
	"remap_scalar", "Remap Scalar",
	"rotation_movement", "Rotation Basic",
	"rotation_spin", "Rotation Spin Roll",
	"rotation_spin yaw", "Rotation Spin Yaw",
	"alpha_random", "Alpha Random",
	"color_random", "Color Random",
	"create from parent particles", "Position From Parent Particles",
	"Create In Hierarchy", "Position In CP Hierarchy",
	"random position along path", "Position Along Path Random",
	"random position on model", "Position on Model Random",
	"sequential position along path", "Position Along Path Sequential",
	"position_offset_random", "Position Modify Offset Random",
	"position_warp_random", "Position Modify Warp Random",
	"position_within_box", "Position Within Box Random",
	"position_within_sphere", "Position Within Sphere Random",
	"Inherit Velocity", "Velocity Inherit from Control Point",
	"Initial Repulsion Velocity", "Velocity Repulse from World",
	"Initial Velocity Noise", "Velocity Noise",
	"Initial Scalar Noise", "Remap Noise to Scalar",
	"Lifespan from distance to world", "Lifetime from Time to Impact",
	"Pre-Age Noise", "Lifetime Pre-Age Noise",
	"lifetime_random", "Lifetime Random",
	"radius_random", "Radius Random",
	"random yaw", "Rotation Yaw Random",
	"Randomly Flip Yaw", "Rotation Yaw Flip Random",
	"rotation_random", "Rotation Random",
	"rotation_speed_random", "Rotation Speed Random",
	"sequence_random", "Sequence Random",
	"second_sequence_random", "Sequence Two Random",
	"trail_length_random", "Trail Length Random",
	"velocity_random", "Velocity Random",
};

static char const *RemapOperatorName( char const *pOpName )
{
	for( int i = 0 ; i < ARRAYSIZE( s_RemapOperatorNameTable ) ; i += 2 )
	{
		if ( Q_stricmp( pOpName, s_RemapOperatorNameTable[i] ) == 0 )
		{
			return s_RemapOperatorNameTable[i + 1 ];
		}
	}
	return pOpName;
}


//-----------------------------------------------------------------------------
// Singleton instance
//-----------------------------------------------------------------------------
static CParticleSystemMgr s_ParticleSystemMgr;
CParticleSystemMgr *g_pParticleSystemMgr = &s_ParticleSystemMgr;


//-----------------------------------------------------------------------------
// Particle dictionary
//-----------------------------------------------------------------------------
class CParticleSystemDictionary
{
public:
	~CParticleSystemDictionary();

	CParticleSystemDefinition* AddParticleSystem( CDmxElement *pParticleSystem );
	int Count() const;
	int NameCount() const;
	CParticleSystemDefinition* GetParticleSystem( int i );
	ParticleSystemHandle_t FindParticleSystemHandle( const char *pName );
	ParticleSystemHandle_t FindOrAddParticleSystemHandle( const char *pName );
	CParticleSystemDefinition* FindParticleSystem( ParticleSystemHandle_t h );
	CParticleSystemDefinition* FindParticleSystem( const char *pName );
	CParticleSystemDefinition* FindParticleSystem( const DmObjectId_t &id );

	CParticleSystemDefinition* operator[]( int idx );

private:
	typedef CUtlStringMap< CParticleSystemDefinition * > ParticleNameMap_t;
	typedef CUtlVector< CParticleSystemDefinition* > ParticleIdMap_t;

	void DestroyExistingElement( CDmxElement *pElement );

	ParticleNameMap_t m_ParticleNameMap;
	ParticleIdMap_t m_ParticleIdMap;
};


//-----------------------------------------------------------------------------
// Index by name handle
//-----------------------------------------------------------------------------
CParticleSystemDefinition* CParticleSystemDictionary::operator[]( int idx )
{
	return m_ParticleNameMap[ idx ];
}

CParticleSystemDictionary::~CParticleSystemDictionary()
{
	int nCount = m_ParticleIdMap.Count();
	for ( int i = 0; i < nCount; ++i )
	{
		delete m_ParticleIdMap[i];
	}
}


//-----------------------------------------------------------------------------
// Deletes any definition already registered under this element's name or id
//-----------------------------------------------------------------------------
void CParticleSystemDictionary::DestroyExistingElement( CDmxElement *pElement )
{
	const char *pParticleSystemName = pElement->GetName();
	bool bPreventNameBasedLookup = pElement->GetValue<bool>( "preventNameBasedLookup" );
	if ( !bPreventNameBasedLookup )
	{
		if ( m_ParticleNameMap.Defined( pParticleSystemName ) )
		{
			CParticleSystemDefinition *pDef = m_ParticleNameMap[ pParticleSystemName ];
			delete pDef;
			m_ParticleNameMap[ pParticleSystemName ] = NULL;
		}
		return;
	}

	// Systems that opted out of name lookup are matched by id
	int nCount = m_ParticleIdMap.Count();
	const DmObjectId_t& id = pElement->GetId();
	for ( int i = 0; i < nCount; ++i )
	{
		if ( IsUniqueIdEqual( m_ParticleIdMap[i]->GetId(), id ) )
		{
			CParticleSystemDefinition *pDef = m_ParticleIdMap[i];
			m_ParticleIdMap.FastRemove(i);
			delete pDef;
			break;
		}
	}
}


//-----------------------------------------------------------------------------
// Adds a particle system definition read from a DMX element
//-----------------------------------------------------------------------------
CParticleSystemDefinition* CParticleSystemDictionary::AddParticleSystem( CDmxElement *pParticleSystem )
{
	if ( Q_stricmp( pParticleSystem->GetTypeString(), "DmeParticleSystemDefinition" ) )
		return NULL;

	DestroyExistingElement( pParticleSystem );

	CParticleSystemDefinition *pDef = new CParticleSystemDefinition;

	// Registered before Read(), since reading may add child systems to the dictionary
	bool bPreventNameBasedLookup = pParticleSystem->GetValue<bool>( "preventNameBasedLookup" );
	if ( !bPreventNameBasedLookup )
	{
		m_ParticleNameMap[ pParticleSystem->GetName() ] = pDef;
	}
	else
	{
		m_ParticleIdMap.AddToTail( pDef );
	}

	pDef->Read( pParticleSystem );
	return pDef;
}

int CParticleSystemDictionary::NameCount() const
{
	return m_ParticleNameMap.GetNumStrings();
}

int CParticleSystemDictionary::Count() const
{
	return m_ParticleIdMap.Count();
}

CParticleSystemDefinition* CParticleSystemDictionary::GetParticleSystem( int i )
{
	return m_ParticleIdMap[i];
}

ParticleSystemHandle_t CParticleSystemDictionary::FindParticleSystemHandle( const char *pName )
{
	return m_ParticleNameMap.Find( pName );
}

ParticleSystemHandle_t CParticleSystemDictionary::FindOrAddParticleSystemHandle( const char *pName )
{
	int nCount = m_ParticleNameMap.GetNumStrings();
	int nIndex = m_ParticleNameMap.AddString( pName );
	if ( nIndex >= nCount )
	{
		m_ParticleNameMap[ nIndex ] = NULL;
	}
	return nIndex;
}

CParticleSystemDefinition* CParticleSystemDictionary::FindParticleSystem( ParticleSystemHandle_t h )
{
	if ( h == UTL_INVAL_SYMBOL || h >= m_ParticleNameMap.GetNumStrings() )
		return NULL;
	return m_ParticleNameMap[ h ];
}

CParticleSystemDefinition* CParticleSystemDictionary::FindParticleSystem( const char *pName )
{
	if ( m_ParticleNameMap.Defined( pName ) )
		return m_ParticleNameMap[ pName ];
	return NULL;
}

CParticleSystemDefinition* CParticleSystemDictionary::FindParticleSystem( const DmObjectId_t &id )
{
	int nCount = m_ParticleIdMap.Count();
	for ( int i = 0; i < nCount; ++i )
	{
		if ( IsUniqueIdEqual( m_ParticleIdMap[i]->GetId(), id ) )
			return m_ParticleIdMap[i];
	}
	return NULL;
}


//-----------------------------------------------------------------------------
// Unpack data for child systems, plus a stand-in operator definition for
// children (only GetUnpackStructure does anything)
//-----------------------------------------------------------------------------
BEGIN_DMXELEMENT_UNPACK( ParticleChildrenInfo_t )
	DMXELEMENT_UNPACK_FIELD( "delay", "0.0", float, m_flDelay )
	DMXELEMENT_UNPACK_FIELD( "end cap effect", "0", bool, m_bEndCap )
END_DMXELEMENT_UNPACK( ParticleChildrenInfo_t, s_ChildrenInfoUnpack )

class CChildOperatorDefinition : public IParticleOperatorDefinition
{
public:
	virtual const char *GetName() const { Assert(0); return NULL; }
	virtual CParticleOperatorInstance *CreateInstance( const DmObjectId_t &id ) const { Assert(0); return NULL; }
	virtual const DmxElementUnpackStructure_t* GetUnpackStructure() const
	{
		return s_ChildrenInfoUnpack;
	}
	virtual ParticleOperatorId_t GetId() const { return OPERATOR_GENERIC; }
	virtual uint32 GetFilter() const { return 0; }
	virtual bool IsObsolete() const { return false; }
	virtual size_t GetClassSize() const { return sizeof( ParticleChildrenInfo_t ); }
};

static CChildOperatorDefinition s_ChildOperatorDefinition;


//-----------------------------------------------------------------------------
//
// CParticleSystemDefinition
//
//-----------------------------------------------------------------------------


//-----------------------------------------------------------------------------
// Unpack structure for CParticleSystemDefinition
//-----------------------------------------------------------------------------
BEGIN_DMXELEMENT_UNPACK( CParticleSystemDefinition )
	DMXELEMENT_UNPACK_FIELD( "max_particles", "1000", int, m_nMaxParticles )
	DMXELEMENT_UNPACK_FIELD( "initial_particles", "0", int, m_nInitialParticles )
	DMXELEMENT_UNPACK_FIELD_STRING_USERDATA( "material", "vgui/white", m_pszMaterialName, "vmtPicker" )
	DMXELEMENT_UNPACK_FIELD( "bounding_box_min", "-10 -10 -10", Vector, m_BoundingBoxMin )
	DMXELEMENT_UNPACK_FIELD( "bounding_box_max", "10 10 10", Vector, m_BoundingBoxMax )
	DMXELEMENT_UNPACK_FIELD( "cull_radius", "0", float, m_flCullRadius )
	DMXELEMENT_UNPACK_FIELD( "cull_cost", "1", float, m_flCullFillCost )
	DMXELEMENT_UNPACK_FIELD( "cull_control_point", "0", int, m_nCullControlPoint )
	DMXELEMENT_UNPACK_FIELD_STRING( "cull_replacement_definition", "", m_pszCullReplacementName )
	DMXELEMENT_UNPACK_FIELD_STRING( "fallback replacement definition", "", m_pszFallbackReplacementName )
	DMXELEMENT_UNPACK_FIELD( "fallback max count", "-1", int, m_nFallbackMaxCount )
	DMXELEMENT_UNPACK_FIELD( "radius", "5", float, m_flConstantRadius )
	DMXELEMENT_UNPACK_FIELD( "color", "255 255 255 255", Color, m_ConstantColor )
	DMXELEMENT_UNPACK_FIELD( "rotation", "0", float, m_flConstantRotation )
	DMXELEMENT_UNPACK_FIELD( "rotation_speed", "0", float, m_flConstantRotationSpeed )
	DMXELEMENT_UNPACK_FIELD( "normal", "0 0 1", Vector, m_ConstantNormal )
	DMXELEMENT_UNPACK_FIELD_USERDATA( "sequence_number", "0", int, m_nConstantSequenceNumber, "sheetsequencepicker" )
	DMXELEMENT_UNPACK_FIELD_USERDATA( "sequence_number 1", "0", int, m_nConstantSequenceNumber1, "sheetsequencepicker_second" )
	DMXELEMENT_UNPACK_FIELD( "group id", "0", int, m_nGroupID )
	DMXELEMENT_UNPACK_FIELD( "maximum time step", "0.1", float, m_flMaximumTimeStep )
	DMXELEMENT_UNPACK_FIELD( "maximum sim tick rate", "0.0", float, m_flMaximumSimTime )
	DMXELEMENT_UNPACK_FIELD( "minimum sim tick rate", "0.0", float, m_flMinimumSimTime )
	DMXELEMENT_UNPACK_FIELD( "minimum rendered frames", "0", int, m_nMinimumFrames )
	DMXELEMENT_UNPACK_FIELD( "control point to disable rendering if it is the camera", "-1", int, m_nSkipRenderControlPoint )
	DMXELEMENT_UNPACK_FIELD( "control point to only enable rendering if it is the camera", "-1", int, m_nAllowRenderControlPoint )
	DMXELEMENT_UNPACK_FIELD( "maximum draw distance", "100000.0", float, m_flMaxDrawDistance )
	DMXELEMENT_UNPACK_FIELD( "time to sleep when not drawn", "8", float, m_flNoDrawTimeToGoToSleep )
	DMXELEMENT_UNPACK_FIELD( "Sort particles", "1", bool, m_bShouldSort )
	DMXELEMENT_UNPACK_FIELD( "batch particle systems", "0", bool, m_bShouldBatch )
	DMXELEMENT_UNPACK_FIELD( "view model effect", "0", bool, m_bViewModelEffect )
	DMXELEMENT_UNPACK_FIELD( "screen space effect", "0", bool, m_bScreenSpaceEffect )
	DMXELEMENT_UNPACK_FIELD( "draw through leafsystem", "1", bool, m_bDrawThroughLeafSystem )
	DMXELEMENT_UNPACK_FIELD( "aggregation radius", "0", float, m_flAggregateRadius )
	DMXELEMENT_UNPACK_FIELD( "minimum free particles to aggregate", "0", int, m_nAggregationMinAvailableParticles )
	DMXELEMENT_UNPACK_FIELD( "minimum simulation time step", "0", float, m_flMinimumTimeStep )
	DMXELEMENT_UNPACK_FIELD( "minimum CPU level", "0", int, m_nMinCPULevel )
	DMXELEMENT_UNPACK_FIELD( "minimum GPU level", "0", int, m_nMinGPULevel )
	DMXELEMENT_UNPACK_FIELD( "freeze simulation after time", "1000000000", float, m_flStopSimulationAfterTime )
END_DMXELEMENT_UNPACK( CParticleSystemDefinition, s_pParticleSystemDefinitionUnpack )


//-----------------------------------------------------------------------------
// Unlinks all collections that use this definition
//-----------------------------------------------------------------------------
void CParticleSystemDefinition::UnlinkAllCollections()
{
	while ( m_pFirstCollection )
	{
		m_pFirstCollection->UnlinkFromDefList();
	}
}

const char *CParticleSystemDefinition::GetName() const
{
	return m_Name;
}


//-----------------------------------------------------------------------------
// Should we always precache this?
//-----------------------------------------------------------------------------
bool CParticleSystemDefinition::ShouldAlwaysPrecache() const
{
	return m_bAlwaysPrecache;
}


//-----------------------------------------------------------------------------
// Precache/uncache
//-----------------------------------------------------------------------------
void CParticleSystemDefinition::Precache()
{
	if ( m_bIsPrecached )
		return;

	m_bIsPrecached = true;

	if ( g_pMaterialSystem )
	{
		m_Material.Init( MaterialName(), TEXTURE_GROUP_OTHER, true );
		if ( m_Material->HasProxy() )
		{
			Warning( "Material %s used by particle systems cannot use proxies!\n", m_Material->GetName() );
			m_Material.Init( "debug/particleerror", TEXTURE_GROUP_OTHER, true );
		}
		g_pParticleSystemMgr->FindOrLoadSheet( this );

		m_Material->GetColorModulation( &m_vecMaterialModulation[0], &m_vecMaterialModulation[1], &m_vecMaterialModulation[2] );
		m_vecMaterialModulation[3] = m_Material->GetAlphaModulation();
	}

	if ( HasFallback() )
	{
		CParticleSystemDefinition *pFallback = GetFallbackReplacementDefinition();
		if ( pFallback )
		{
			pFallback->Precache();
		}
	}

	for( int i = 0; i < m_Renderers.Count(); i++ )
	{
		CParticleOperatorInstance *pOp = m_Renderers[i];
		pOp->Precache();
	}

	int nChildCount = m_Children.Count();
	for ( int i = 0; i < nChildCount; ++i )
	{
		CParticleSystemDefinition *pChild;
		if ( m_Children[i].m_bUseNameBasedLookup )
		{
			pChild = g_pParticleSystemMgr->FindParticleSystem( m_Children[i].m_Name );
		}
		else
		{
			pChild = g_pParticleSystemMgr->FindParticleSystem( m_Children[i].m_Id );
		}

		if ( pChild )
		{
			pChild->Precache();
		}
	}
}

void CParticleSystemDefinition::Uncache()
{
	if ( !m_bIsPrecached )
		return;

	m_bIsPrecached = false;
	m_Material.Shutdown();

	if ( HasFallback() )
	{
		CParticleSystemDefinition *pFallback = GetFallbackReplacementDefinition();
		if ( pFallback )
		{
			pFallback->Uncache();
		}
	}

	for( int i = 0; i < m_Renderers.Count(); i++ )
	{
		CParticleOperatorInstance *pOp = m_Renderers[i];
		pOp->Uncache();
	}

	int nChildCount = m_Children.Count();
	for ( int i = 0; i < nChildCount; ++i )
	{
		CParticleSystemDefinition *pChild;
		if ( m_Children[i].m_bUseNameBasedLookup )
		{
			pChild = g_pParticleSystemMgr->FindParticleSystem( m_Children[i].m_Name );
		}
		else
		{
			pChild = g_pParticleSystemMgr->FindParticleSystem( m_Children[i].m_Id );
		}

		if ( pChild )
		{
			pChild->Uncache();
		}
	}
}


//-----------------------------------------------------------------------------
// Has this been precached?
//-----------------------------------------------------------------------------
bool CParticleSystemDefinition::IsPrecached()
{
	return m_bIsPrecached;
}


//-----------------------------------------------------------------------------
// Helper methods to help with unserialization
//-----------------------------------------------------------------------------
void CParticleSystemDefinition::ParseOperators(
	const char *pszOpKey, ParticleFunctionType_t nFunctionType,
	CDmxElement *pElement,
	CUtlVector<CParticleOperatorInstance *> &outList)
{
	const CDmxAttribute* pAttribute = pElement->GetAttribute( pszOpKey );
	if ( !pAttribute || pAttribute->GetType() != AT_ELEMENT_ARRAY )
		return;

	const CUtlVector<IParticleOperatorDefinition *> &flist = g_pParticleSystemMgr->GetAvailableParticleOperatorList( nFunctionType );

	const CUtlVector< CDmxElement* >& ops = pAttribute->GetArray<CDmxElement*>( );
	int nCount = ops.Count();
	for ( int i = 0; i < nCount; ++i )
	{
		const char *pOrigName = ops[i]->GetValueString( "functionName" );
		char const *pOpName = RemapOperatorName( pOrigName );
		if ( pOpName != pOrigName )
		{
			// NOTE: the shipped code writes the new name onto the definition
			// element rather than onto ops[i]; kept as-is
			pElement->SetValue( "functionName", pOpName );
		}
		bool bFound = false;
		int nFunctionCount = flist.Count();
		for( int j = 0; j < nFunctionCount; ++j )
		{
			if ( Q_stricmp( pOpName, flist[j]->GetName() ) )
				continue;

			bFound = true;

			CParticleOperatorInstance *pNewRef = flist[j]->CreateInstance( ops[i]->GetId() );
			const DmxElementUnpackStructure_t *pUnpack = flist[j]->GetUnpackStructure();
			if ( pUnpack )
			{
				ops[i]->UnpackIntoStructure( pNewRef, flist[j]->GetClassSize(), pUnpack );
			}
			pNewRef->InitParams( this );
			pNewRef->CheckForFastPath();

			m_nAttributeReadMask |= pNewRef->GetReadAttributes();
			m_nControlPointReadMask |= pNewRef->GetReadControlPointMask();
			m_nControlPointNonPositionalMask |= pNewRef->GetNonPositionalControlPointMask();

			switch( nFunctionType )
			{
			case FUNCTION_INITIALIZER:
			case FUNCTION_EMITTER:
				m_nPerParticleInitializedAttributeMask |= pNewRef->GetWrittenAttributes();
				Assert( pNewRef->GetReadInitialAttributes() == 0 );
				break;

			case FUNCTION_OPERATOR:
			case FUNCTION_FORCEGENERATOR:
			case FUNCTION_CONSTRAINT:
			case FUNCTION_RENDERER:
				m_nPerParticleUpdatedAttributeMask |= pNewRef->GetWrittenAttributes();
				m_nInitialAttributeReadMask |= pNewRef->GetReadInitialAttributes();
				break;
			}

			// Reading the particle id (current or initial) requires the initial value to be kept
			if ( ( pNewRef->GetReadAttributes() | pNewRef->GetReadInitialAttributes() ) & PARTICLE_ATTRIBUTE_PARTICLE_ID_MASK )
			{
				m_nInitialAttributeReadMask |= PARTICLE_ATTRIBUTE_PARTICLE_ID_MASK;
				m_nPerParticleInitializedAttributeMask |= PARTICLE_ATTRIBUTE_PARTICLE_ID_MASK;
			}

			outList.AddToTail( pNewRef );
			break;
		}

		if ( !bFound )
		{
			// No warning when this function type has no registered operators at all
			if ( flist.Count() )
				Warning( "Didn't find particle function %s\n", pOpName );
		}
	}
}

void CParticleSystemDefinition::ParseChildren( CDmxElement *pElement )
{
	const CUtlVector<CDmxElement*>& children = pElement->GetArray<CDmxElement*>( "children" );
	int nCount = children.Count();
	for ( int i = 0; i < nCount; ++i )
	{
		CDmxElement *pChild = children[i]->GetValue<CDmxElement*>( "child" );
		if ( !pChild || Q_stricmp( pChild->GetTypeString(), "DmeParticleSystemDefinition" ) )
			continue;

		int j = m_Children.AddToTail();
		children[i]->UnpackIntoStructure( &m_Children[j], sizeof( m_Children[j] ), s_ChildrenInfoUnpack );
		m_Children[j].m_bUseNameBasedLookup = !pChild->GetValue<bool>( "preventNameBasedLookup" );
		if ( m_Children[j].m_bUseNameBasedLookup )
		{
			m_Children[j].m_Name = g_pParticleSystemMgr->FindOrAddParticleSystemIndex( pChild->GetName() );
		}
		else
		{
			CopyUniqueId( pChild->GetId(), &m_Children[j].m_Id );
		}

		// Make sure the child system has a definition too
		g_pParticleSystemMgr->AddParticleSystem( pChild );
	}
}

void CParticleSystemDefinition::Read( CDmxElement *pElement )
{
	m_Name = pElement->GetName();
	CopyUniqueId( pElement->GetId(), &m_Id );
	pElement->UnpackIntoStructure( this, sizeof( *this ), s_pParticleSystemDefinitionUnpack );

	if ( m_nInitialParticles < 0 )
	{
		m_nInitialParticles = 0;
	}
	if ( m_nMaxParticles < 1 )
	{
		m_nMaxParticles = 1;
	}
	m_nMaxParticles *= g_nParticle_Multiplier;
	m_nMaxParticles = MIN( m_nMaxParticles, MAX_PARTICLES_IN_A_SYSTEM );
	if ( m_flCullRadius > 0 )
	{
		m_nControlPointReadMask |= 1ULL << m_nCullControlPoint;
	}

	ParseOperators( "renderers", FUNCTION_RENDERER, pElement, m_Renderers );
	ParseOperators( "operators", FUNCTION_OPERATOR, pElement, m_Operators );
	ParseOperators( "initializers", FUNCTION_INITIALIZER, pElement, m_Initializers );
	ParseOperators( "emitters", FUNCTION_EMITTER, pElement, m_Emitters );
	ParseChildren( pElement );
	ParseOperators( "forces", FUNCTION_FORCEGENERATOR, pElement, m_ForceGenerators );
	ParseOperators( "constraints", FUNCTION_CONSTRAINT, pElement, m_Constraints );

	SetupContextData();
}

IMaterial *CParticleSystemDefinition::GetMaterial()
{
	Assert( IsPrecached() );
	if ( !IsPrecached() )
		return NULL;
	return m_Material;
}


//-----------------------------------------------------------------------------
// Sheet symbols
//-----------------------------------------------------------------------------
CUtlSymbol CParticleSystemDefinition::GetSheetSymbol() const
{
	Assert( IsSheetSymbolCached() );
	return m_SheetSymbol;
}

void CParticleSystemDefinition::CacheSheetSymbol( CUtlSymbol sheetSymbol )
{
	m_SheetSymbol = sheetSymbol;
	m_bSheetSymbolCached = true;
}

bool CParticleSystemDefinition::IsSheetSymbolCached() const
{
	return m_bSheetSymbolCached;
}

void CParticleSystemDefinition::InvalidateSheetSymbol()
{
	m_bSheetSymbolCached = false;
}


//-----------------------------------------------------------------------------
// Returns the particle system fallback
//-----------------------------------------------------------------------------
CParticleSystemDefinition *CParticleSystemDefinition::GetFallbackReplacementDefinition() const
{
	if ( HasFallback() )
	{
		if ( !m_pFallback() )
		{
			const_cast< CParticleSystemDefinition* >( this )->m_pFallback.Set( g_pParticleSystemMgr->FindParticleSystem( m_pszFallbackReplacementName ) );
		}
		return m_pFallback();
	}
	return NULL;
}


//-----------------------------------------------------------------------------
// Does the particle system use the frame buffer texture (refraction?)
//-----------------------------------------------------------------------------
bool CParticleSystemDefinition::UsesPowerOfTwoFrameBufferTexture()
{
	Assert( IsPrecached() );
	return g_pMaterialSystem && m_Material->NeedsPowerOfTwoFrameBufferTexture( false );
}

//-----------------------------------------------------------------------------
// Does the particle system use the full frame buffer texture (soft particles)
//-----------------------------------------------------------------------------
bool CParticleSystemDefinition::UsesFullFrameBufferTexture()
{
	Assert( IsPrecached() );
	return g_pMaterialSystem && m_Material->NeedsFullFrameBufferTexture( false );
}


//-----------------------------------------------------------------------------
// Helper methods to write particle systems
//-----------------------------------------------------------------------------
void CParticleSystemDefinition::WriteOperators( CDmxElement *pElement,
	const char *pOpKeyName, const CUtlVector<CParticleOperatorInstance *> &inList )
{
	CDmxElementModifyScope modify( pElement );
	CDmxAttribute* pAttribute = pElement->AddAttribute( pOpKeyName );
	CUtlVector< CDmxElement* >& ops = pAttribute->GetArrayForEdit<CDmxElement*>( );

	int nCount = inList.Count();
	for ( int i = 0; i < nCount; ++i )
	{
		CDmxElement *pOperator = CreateDmxElement( "DmeParticleOperator" );
		ops.AddToTail( pOperator );

		const IParticleOperatorDefinition *pDef = inList[i]->GetDefinition();
		pOperator->SetValue( "name", pDef->GetName() );
		pOperator->SetValue( "functionName", pDef->GetName() );

		const DmxElementUnpackStructure_t *pUnpack = pDef->GetUnpackStructure();
		if ( pUnpack )
		{
			pOperator->AddAttributesFromStructure( inList[i], pUnpack );
		}
	}
}

void CParticleSystemDefinition::WriteChildren( CDmxElement *pElement )
{
	CDmxElementModifyScope modify( pElement );
	CDmxAttribute* pAttribute = pElement->AddAttribute( "children" );
	CUtlVector< CDmxElement* >& children = pAttribute->GetArrayForEdit<CDmxElement*>( );
	int nCount = m_Children.Count();
	for ( int i = 0; i < nCount; ++i )
	{
		CDmxElement *pChildRef = CreateDmxElement( "DmeParticleChild" );
		children.AddToTail( pChildRef );
		children[i]->AddAttributesFromStructure( &m_Children[i], s_ChildrenInfoUnpack );
		CDmxElement *pChildParticleSystem;
		if ( m_Children[i].m_bUseNameBasedLookup )
		{
			pChildParticleSystem = g_pParticleSystemMgr->CreateParticleDmxElement( 
				g_pParticleSystemMgr->GetParticleSystemNameFromIndex( m_Children[i].m_Name ) );
		}
		else
		{
			pChildParticleSystem = g_pParticleSystemMgr->CreateParticleDmxElement( m_Children[i].m_Id );
		}
		pChildRef->SetValue( "name", pChildParticleSystem->GetName() );
		pChildRef->SetValue( "child", pChildParticleSystem );
	}
}

CDmxElement *CParticleSystemDefinition::Write()
{
	const char *pName = GetName();

	CDmxElement *pElement = CreateDmxElement( "DmeParticleSystemDefinition" );
	pElement->SetValue( "name", pName );
	pElement->AddAttributesFromStructure( this, s_pParticleSystemDefinitionUnpack );
	WriteOperators( pElement, "renderers", m_Renderers );
	WriteOperators( pElement, "operators", m_Operators );
	WriteOperators( pElement, "initializers", m_Initializers );
	WriteOperators( pElement, "emitters", m_Emitters );
	WriteChildren( pElement );
	WriteOperators( pElement, "forces", m_ForceGenerators );
	WriteOperators( pElement, "constraints", m_Constraints );

	return pElement;
}


//-----------------------------------------------------------------------------
// Assigns each operator a 16-byte aligned slice of the per-collection context block
//-----------------------------------------------------------------------------
void CParticleSystemDefinition::SetupContextData( void )
{
	CUtlVector<CParticleOperatorInstance *> *olists[] = 
	{
		&m_Operators, &m_Renderers, &m_Initializers, &m_Emitters, &m_ForceGenerators,
		&m_Constraints
	};
	CUtlVector<size_t> *offsetLists[] = 
	{
		&m_nOperatorsCtxOffsets, &m_nRenderersCtxOffsets, 
		&m_nInitializersCtxOffsets, &m_nEmittersCtxOffsets, 
		&m_nForceGeneratorsCtxOffsets, &m_nConstraintsCtxOffsets,
	};

	m_nContextDataSize = 0;
	for ( int i = 0; i < ARRAYSIZE( olists ); i++ )
	{
		int nCount = olists[i]->Count();
		for ( int j = 0; j < nCount; j++ )
		{
			offsetLists[i]->AddToTail( m_nContextDataSize );
			m_nContextDataSize += (*olists[i])[j]->GetRequiredContextBytes();

			// keep each context block 16-byte aligned
			m_nContextDataSize = ( m_nContextDataSize + 15 ) & ( ~0xf );
		}
	}
}


//-----------------------------------------------------------------------------
// Finds an operator by id
//-----------------------------------------------------------------------------
CUtlVector<CParticleOperatorInstance *> *CParticleSystemDefinition::GetOperatorList( ParticleFunctionType_t type )
{
	switch( type )
	{
	case FUNCTION_EMITTER:
		return &m_Emitters;
	case FUNCTION_RENDERER:
		return &m_Renderers;
	case FUNCTION_INITIALIZER:
		return &m_Initializers;
	case FUNCTION_OPERATOR:
		return &m_Operators;
	case FUNCTION_FORCEGENERATOR:
		return &m_ForceGenerators;
	case FUNCTION_CONSTRAINT:
		return &m_Constraints;
	default:
		Assert(0);
		return NULL;
	}
}


//-----------------------------------------------------------------------------
// Finds an operator by id
//-----------------------------------------------------------------------------
CParticleOperatorInstance *CParticleSystemDefinition::FindOperatorById( ParticleFunctionType_t type, const DmObjectId_t &id )
{
	CUtlVector<CParticleOperatorInstance *> *pVec = GetOperatorList( type );
	if ( !pVec )
		return NULL;

	int nCount = pVec->Count();
	for ( int i = 0; i < nCount; ++i )
	{
		if ( IsUniqueIdEqual( id, pVec->Element(i)->GetId() ) )
			return pVec->Element(i);
	}
	return NULL;
}


//-----------------------------------------------------------------------------
// Finds an operator by name (slow!)
//-----------------------------------------------------------------------------
CParticleOperatorInstance *CParticleSystemDefinition::FindOperatorByName( const char *pOperatorName )
{
	for ( int i = 0; i < PARTICLE_FUNCTION_COUNT; i++ )
	{
		CUtlVector<CParticleOperatorInstance *> *pVec = ( i == FUNCTION_CHILDREN ) ? NULL : GetOperatorList( (ParticleFunctionType_t)i );
		if ( !pVec )
			continue;

		int nCount = pVec->Count();
		for ( int j = 0; j < nCount; ++j )
		{
			if ( !Q_stricmp( pOperatorName, pVec->Element(j)->GetDefinition()->GetName() ) )
				return pVec->Element(j);
		}
	}
	return NULL;
}


//-----------------------------------------------------------------------------
// Initializes new particles: scalar for the unaligned head and tail, blocks of 4 in between
//-----------------------------------------------------------------------------
void CParticleOperatorInstance::InitNewParticles( CParticleCollection *pParticles, 
	int nFirstParticle, int nParticleCount, 
	int nAttributeWriteMask, void *pContext ) const
{
	if ( !nParticleCount )
		return;

	if ( nParticleCount < 16 )									// too few to be worth the block path
	{
		InitNewParticlesScalar( pParticles, nFirstParticle, nParticleCount, nAttributeWriteMask, pContext );
		return;
	}

	int nHead = nFirstParticle & 3;
	if ( nHead )
	{
		// scalar init up to the next multiple of 4
		int nHeadCount = MIN( nParticleCount, 4 - nHead );
		InitNewParticlesScalar( pParticles, nFirstParticle, nHeadCount, nAttributeWriteMask, pContext );
		nParticleCount -= nHeadCount;
		nFirstParticle += nHeadCount;
	}

	// nFirstParticle is now a multiple of 4
	int nBlockCount = nParticleCount / 4;
	if ( nBlockCount )
	{
		InitNewParticlesBlock( pParticles, nFirstParticle / 4, nBlockCount, nAttributeWriteMask, pContext );
		nParticleCount -= 4 * nBlockCount;
		nFirstParticle += 4 * nBlockCount;
	}

	// remaining tail
	if ( nParticleCount )
	{
		InitNewParticlesScalar( pParticles, nFirstParticle, nParticleCount, nAttributeWriteMask, pContext );
	}
}


//-----------------------------------------------------------------------------
//
// CParticleCollection
//
//-----------------------------------------------------------------------------

void CParticleCollection::operator delete(void *pData)
{
	if ( pData )
	{
		MemAlloc_FreeAligned( pData );
	}
}

void CParticleCollection::operator delete( void* pData, int nBlockUse, const char *pFileName, int nLine )
{
	if ( pData )
	{
		MemAlloc_FreeAligned( pData );
	}
}

void CWorldCollideContextData::operator delete(void *pData)
{
	if ( pData )
	{
		MemAlloc_FreeAligned( pData );
	}
}

void CWorldCollideContextData::operator delete( void* pData, int nBlockUse, const char *pFileName, int nLine )
{
	if ( pData )
	{
		MemAlloc_FreeAligned( pData );
	}
}


//-----------------------------------------------------------------------------
// Constructor, destructor
//-----------------------------------------------------------------------------
CParticleCollection::CParticleCollection( )
{
	COMPILE_TIME_ASSERT( ( MAX_RANDOM_FLOATS & ( MAX_RANDOM_FLOATS - 1 ) ) == 0 );
	COMPILE_TIME_ASSERT( sizeof( s_pRandomFloats ) / sizeof( float ) >= MAX_RANDOM_FLOATS );

	memset( this, 0, sizeof(CParticleCollection) );
	m_flOOMaxDistSqr = 1.0f;
	m_flPreviousDt = 0.05f;
	m_nParticleFlags = PCFLAGS_FIRST_FRAME;
	m_LocalLightingCP = -1;
	m_LocalLighting = Color(255, 255, 255, 255);
	m_flTargetDrawTime = 0;
	m_bPendingRestart = false;
	m_bQueuedStartEmission = false;
	m_bFrozen = false;
	m_pCPInfo = NULL;
}

CParticleCollection::~CParticleCollection( void )
{
	UnlinkFromDefList();

	m_Children.Purge();

	if ( m_pParticleMemory )
	{
		MemAlloc_FreeAligned( m_pParticleMemory );
	}

	if ( m_pPreviousAttributeMemory )
	{
		MemAlloc_FreeAligned( m_pPreviousAttributeMemory );
	}

	if ( m_pParticleInitialMemory )
	{
		MemAlloc_FreeAligned( m_pParticleInitialMemory );
	}

	if ( m_pConstantMemory )
	{
		MemAlloc_FreeAligned( m_pConstantMemory );
	}

	if ( m_pOperatorContextData )
	{
		MemAlloc_FreeAligned( m_pOperatorContextData );
	}

	for( int i = 0 ; i < ARRAYSIZE( m_pCollisionCacheData ) ; i++ )
	{
		if ( m_pCollisionCacheData[i] )
		{
			delete m_pCollisionCacheData[i];
		}
	}

	if ( m_pCPInfo )
		delete[] m_pCPInfo;
}


//-----------------------------------------------------------------------------
// Initialization
//-----------------------------------------------------------------------------
void CParticleCollection::Init( CParticleSystemDefinition *pDef, float flDelay, int nRandomSeed )
{
	m_pDef = pDef;

	// Link into def list
	LinkIntoDefList();

	m_pRenderable = NULL;

	InitStorage( pDef );

	// Initialize sheet data
	m_Sheet.Set( g_pParticleSystemMgr->FindOrLoadSheet( pDef ) );

	// A non-zero seed makes the system deterministic (scrubbable)
	m_bIsScrubbable = ( nRandomSeed != 0 );
	if ( m_bIsScrubbable )
	{
		m_nRandomSeed = nRandomSeed;
	}
	else
	{
		m_nRandomSeed = (int)(intp)this;
#ifndef _X360
		m_nRandomSeed += Plat_MSTime();
#endif
	}

	SetAttributeToConstant( PARTICLE_ATTRIBUTE_XYZ, 0.0f, 0.0f, 0.0f );
	SetAttributeToConstant( PARTICLE_ATTRIBUTE_PREV_XYZ, 0.0f, 0.0f, 0.0f );
	SetAttributeToConstant( PARTICLE_ATTRIBUTE_LIFE_DURATION, 1.0f );
	SetAttributeToConstant( PARTICLE_ATTRIBUTE_RADIUS, pDef->m_flConstantRadius );
	SetAttributeToConstant( PARTICLE_ATTRIBUTE_ROTATION, pDef->m_flConstantRotation );
	SetAttributeToConstant( PARTICLE_ATTRIBUTE_ROTATION_SPEED, pDef->m_flConstantRotationSpeed );
	SetAttributeToConstant( PARTICLE_ATTRIBUTE_TINT_RGB, 
		pDef->m_ConstantColor.r() / 255.0f, pDef->m_ConstantColor.g() / 255.0f,
		pDef->m_ConstantColor.b() / 255.0f );
	SetAttributeToConstant( PARTICLE_ATTRIBUTE_ALPHA, pDef->m_ConstantColor.a() / 255.0f );
	SetAttributeToConstant( PARTICLE_ATTRIBUTE_CREATION_TIME, 0.0f );
	SetAttributeToConstant( PARTICLE_ATTRIBUTE_SEQUENCE_NUMBER, pDef->m_nConstantSequenceNumber );
	SetAttributeToConstant( PARTICLE_ATTRIBUTE_SEQUENCE_NUMBER1, pDef->m_nConstantSequenceNumber1 );
	SetAttributeToConstant( PARTICLE_ATTRIBUTE_TRAIL_LENGTH, 0.1f );
	SetAttributeToConstant( PARTICLE_ATTRIBUTE_PARTICLE_ID, 0 );
	SetAttributeToConstant( PARTICLE_ATTRIBUTE_YAW, 0 );
	SetAttributeToConstant( PARTICLE_ATTRIBUTE_ALPHA2, 1.0f );
	SetAttributeToConstant( PARTICLE_ATTRIBUTE_NORMAL, 
		pDef->m_ConstantNormal.x, pDef->m_ConstantNormal.y,
		pDef->m_ConstantNormal.z );
	SetAttributeToConstant( PARTICLE_ATTRIBUTE_GLOW_RGB, 1.0f, 1.0f, 1.0f );
	SetAttributeToConstant( PARTICLE_ATTRIBUTE_GLOW_ALPHA, 1.0f );

	// Offset the child in time
	m_flCurTime = -flDelay;
	m_fl4CurTime = ReplicateX4( m_flCurTime );
	if ( m_pDef->m_nContextDataSize )
	{
		m_pOperatorContextData = reinterpret_cast<uint8 *>
			( MemAlloc_AllocAligned( m_pDef->m_nContextDataSize, 16 ) );
	}

	m_flNextSleepTime = g_pParticleSystemMgr->GetLastSimulationTime() + pDef->m_flNoDrawTimeToGoToSleep;

	m_nControlPointReadMask = pDef->m_nControlPointReadMask;
	m_nControlPointNonPositionalMask = pDef->m_nControlPointNonPositionalMask;

	// Instance child particle systems
	int nChildCount = pDef->m_Children.Count();
	for ( int i = 0; i < nChildCount; ++i )
	{
		if ( nRandomSeed != 0 )
		{
			nRandomSeed += 129;
		}

		CParticleCollection *pChild;
		if ( pDef->m_Children[i].m_bUseNameBasedLookup )
		{
			pChild = g_pParticleSystemMgr->CreateParticleCollection( pDef->m_Children[i].m_Name, pDef->m_Children[i].m_flDelay - m_flCurTime, nRandomSeed );
			//pChild = g_pParticleSystemMgr->CreateParticleCollection( pDef->m_Children[i].m_Id, pDef->m_Children[i].m_flDelay - m_flCurTime, nRandomSeed );
		}
		else
		{
			pChild = g_pParticleSystemMgr->CreateParticleCollection( pDef->m_Children[i].m_Id, pDef->m_Children[i].m_flDelay - m_flCurTime, nRandomSeed );
		}
		if ( pChild )
		{
			pChild->m_pParent = this;
			m_Children.AddToTail( pChild );
			m_nControlPointReadMask |= pChild->m_nControlPointReadMask;
			m_nControlPointNonPositionalMask |= pChild->m_nControlPointNonPositionalMask;
			if ( pDef->m_Children[i].m_bEndCap )
			{
				// NOTE: the shipped binary computes this value but never stores it anywhere
				flDelay = -FLT_MAX;
			}
		}
	}

	m_bIsTranslucent = ComputeIsTranslucent();
	m_bIsTwoPass = ComputeIsTwoPass();
	m_bIsBatchable = ComputeIsBatchable();
	m_bIsOrderImportant = ComputeIsOrderImportant();
	m_bRunForParentApplyKillList = ComputeRunForParentApplyKillList();
	LabelTextureUsage();
	m_bAnyUsesPowerOfTwoFrameBufferTexture = ComputeUsesPowerOfTwoFrameBufferTexture();
	m_bAnyUsesFullFrameBufferTexture = ComputeUsesFullFrameBufferTexture();
	m_bInEndCap = false;

	// Only allocate control points up to the highest one this system (or its children) reads
	int i;
	for ( i = MAX_PARTICLE_CONTROL_POINTS - 1; i > 0; --i )
	{
		if ( m_nControlPointReadMask & ( 1ULL << i ) )
			break;
	}
	m_nNumControlPointsAllocated = MIN( MAX_PARTICLE_CONTROL_POINTS, i + 2 );

	m_pCPInfo = new CParticleCPInfo[ m_nNumControlPointsAllocated ];
	memset( m_pCPInfo, 0, sizeof( CParticleCPInfo ) * m_nNumControlPointsAllocated );
	for ( int i = 0; i < m_nNumControlPointsAllocated; ++i )
	{
		ControlPoint( i ).m_ForwardVector.y = 1.0f;
		ControlPoint( i ).m_UpVector.z = 1.0f;
		ControlPoint( i ).m_RightVector.x = 1.0f;
	}

	// let operators set up their context data
	CUtlVector<CParticleOperatorInstance *> *olists[] = 
	{
		&( m_pDef->m_Operators ), &( m_pDef->m_Renderers ),
		&( m_pDef->m_Initializers ), &( m_pDef->m_Emitters ),
		&( m_pDef->m_ForceGenerators ),
		&( m_pDef->m_Constraints ),
	};
	CUtlVector<size_t> *offsetlists[] = 
	{
		&( m_pDef->m_nOperatorsCtxOffsets ), &( m_pDef->m_nRenderersCtxOffsets ),
		&( m_pDef->m_nInitializersCtxOffsets ), &( m_pDef->m_nEmittersCtxOffsets ),
		&( m_pDef->m_nForceGeneratorsCtxOffsets ),
		&( m_pDef->m_nConstraintsCtxOffsets ),
	};

	for ( int i = 0; i < ARRAYSIZE( olists ); i++ )
	{
		int nOperatorCount = olists[i]->Count();
		for ( int j = 0; j < nOperatorCount; j++ )
		{
			(*olists[i])[j]->InitializeContextData( this, m_pOperatorContextData + (*offsetlists[i])[j] );
		}
	}
}

int CParticleCollection::GetCurrentParticleDefCount( CParticleSystemDefinition* pDef )
{
	int nDefCount = pDef->m_nFallbackCurrentCount;
	CParticleSystemDefinition *pFallback = pDef;
	while ( pFallback->HasFallback() )
	{
		pFallback = pFallback->GetFallbackReplacementDefinition();
		if ( !pFallback )
			break;	// the fallback system isn't loaded
		nDefCount += pFallback->m_nFallbackCurrentCount;
	}
	return nDefCount;
}

bool CParticleCollection::Init( CParticleSystemDefinition *pDef )
{
	if ( pDef->GetMinCPULevel() > g_pParticleSystemMgr->GetParticleCPULevel() || pDef->GetMinGPULevel() > g_pParticleSystemMgr->GetParticleGPULevel() )
	{
		return false;
	}

	float flFallbackMultiplier = g_pParticleSystemMgr->GetFallbackMultiplier();
	float flFallbackBase = g_pParticleSystemMgr->GetFallbackBase();
	float flThresholdSimMS = g_pParticleSystemMgr->GetSimFallbackThresholdMs();

	int nFallbackCount = GetCurrentParticleDefCount( pDef );

	// If the last simulation pass ran over the threshold, push systems towards their fallbacks
	float flPreviousSimMS = g_pParticleSystemMgr->GetLastSimulationDuration() * 1000.0f;
	if ( flPreviousSimMS > flThresholdSimMS )
	{
		float flMSOver = flPreviousSimMS - flThresholdSimMS;
		flFallbackBase += flMSOver * g_pParticleSystemMgr->GetSimFallbackBaseMultiplier();
	}

	// Walk down the fallback chain while too many instances of this definition exist
	while ( pDef->HasFallback() && ( ( nFallbackCount * flFallbackMultiplier ) + flFallbackBase ) >= pDef->m_nFallbackMaxCount )
	{
		nFallbackCount -= pDef->m_nFallbackCurrentCount;
		pDef = pDef->GetFallbackReplacementDefinition();
		if ( !pDef )
		{
			Warning( "Particlelib: Missing precache for particle system type \"%s\"!\n", "unknown" );
			CParticleSystemDefinition *pErrorDef = g_pParticleSystemMgr->FindParticleSystem( "error" );
			if ( pErrorDef )
			{
				pDef = pErrorDef;
			}
			break;
		}
	}
	if ( !pDef )
		return false;	// fallback chain ended in a system that isn't loaded, and there's no "error" system either

	Init( pDef, 0.0f, 0 );
	return IsValid();
}

bool CParticleCollection::Init( const char *pParticleSystemName )
{
	if ( !pParticleSystemName )
		return false;

	CParticleSystemDefinition *pDef = g_pParticleSystemMgr->FindParticleSystem( pParticleSystemName );
	if ( !pDef )
	{
		Warning( "Attempted to create unknown particle system type \"%s\"!\n", pParticleSystemName );
		return false;
	}

	return Init( pDef );
}


bool CParticleCollection::IsFullyValid( void ) const
{
	if ( m_pDef.GetObject() == NULL )
		return false;

	for( CParticleCollection *p = m_Children.m_pHead; p; p = p->m_pNext )
	{
		if ( !p->IsFullyValid() )
			return false;
	}

	return true;
}


bool CParticleCollection::DependsOnSystem( const char *pName ) const
{
	if ( m_pDef.GetObject() == NULL )
		return false;

	if ( m_pDef->m_Name == pName )
		return true;

	for( CParticleCollection *p = m_Children.m_pHead; p; p = p->m_pNext )
	{
		if ( p->DependsOnSystem( pName ) )
			return true;
	}

	return false;
}


//-----------------------------------------------------------------------------
// List management for collections sharing the same particle definition
//-----------------------------------------------------------------------------
void CParticleCollection::LinkIntoDefList( )
{
	Assert( !m_pPrevDef && !m_pNextDef );

	++m_pDef->m_nFallbackCurrentCount;

	m_pPrevDef = NULL;
	m_pNextDef = m_pDef->m_pFirstCollection;
	m_pDef->m_pFirstCollection = this;
	if ( m_pNextDef )
	{
		m_pNextDef->m_pPrevDef = this;
	}
}

void CParticleCollection::UnlinkFromDefList( )
{
	if ( !m_pDef )
		return;

	--m_pDef->m_nFallbackCurrentCount;

	if ( m_pDef->m_pFirstCollection == this )
	{
		m_pDef->m_pFirstCollection = m_pNextDef;
		Assert( !m_pPrevDef );
	}
	else
	{
		Assert( m_pPrevDef );
		m_pPrevDef->m_pNextDef = m_pNextDef;
	}

	if ( m_pNextDef )
	{
		m_pNextDef->m_pPrevDef = m_pPrevDef;
	}

	m_pNextDef = m_pPrevDef = NULL;
}


//-----------------------------------------------------------------------------
// Particle memory initialization
//-----------------------------------------------------------------------------
void CParticleCollection::InitStorage( CParticleSystemDefinition *pDef )
{
	Assert( pDef->m_nMaxParticles < 65536 );

	m_nMaxAllowedParticles = MIN( MAX_PARTICLES_IN_A_SYSTEM, pDef->m_nMaxParticles );
	m_nAllocatedParticles = 4 + 4 * ( ( m_nMaxAllowedParticles + 3 ) / 4 );

	// constant values: 3 SIMD vectors per attribute, plus slack
	int nConstantMemorySize = 3 * 4 * MAX_PARTICLE_ATTRIBUTES * sizeof(float) + 16;
	m_pConstantMemory = (unsigned char * )MemAlloc_AllocAligned( nConstantMemorySize, 16 );
	m_pConstantAttributes = (float*)( m_pConstantMemory );

	m_nPerParticleInitializedAttributeMask = pDef->m_nPerParticleInitializedAttributeMask;
	m_nPerParticleUpdatedAttributeMask = pDef->m_nPerParticleUpdatedAttributeMask;

	// Initial values only need their own storage when they are both initialized and later updated
	m_nPerParticleReadInitialAttributeMask = pDef->m_nInitialAttributeReadMask & 
		( pDef->m_nPerParticleInitializedAttributeMask & pDef->m_nPerParticleUpdatedAttributeMask );

	// Initialized, read, but never updated: the initial value is the current value
	int nPerParticleReadConstantAttributeMask = pDef->m_nInitialAttributeReadMask & 
		( pDef->m_nPerParticleInitializedAttributeMask & ~pDef->m_nPerParticleUpdatedAttributeMask );

	int sz = 0;
	int nInitialAttributeSize = 0;
	int nPerParticleAttributeMask = m_nPerParticleInitializedAttributeMask | m_nPerParticleUpdatedAttributeMask;
	for( int bit = 0; bit < MAX_PARTICLE_ATTRIBUTES; bit++ )
	{
		int nAttrSize = ( ( 1 << bit ) & ATTRIBUTES_WHICH_ARE_VEC3S_MASK ) ? 3 : 1;
		if ( nPerParticleAttributeMask & ( 1 << bit ) )
		{
			sz += nAttrSize;
		}
		if ( m_nPerParticleReadInitialAttributeMask & ( 1 << bit ) )
		{
			nInitialAttributeSize += nAttrSize;
		}
	}

	// extra FourVectors of padding at the end
	sz = m_nAllocatedParticles * sz * sizeof(float) + sizeof( FourVectors );
	m_pParticleMemory = ( unsigned char * )MemAlloc_AllocAligned( sz, 16 );
	m_nAttributeMemorySize = sz;
	memset( m_pParticleMemory, 0, sz );

	if ( nInitialAttributeSize != 0 )
	{
		sz = m_nAllocatedParticles * nInitialAttributeSize * sizeof(float) + sizeof( FourVectors );
		m_pParticleInitialMemory = ( unsigned char * )MemAlloc_AllocAligned( sz, 16 );
		memset( m_pParticleInitialMemory, 0, sz );
	}

	float* pMem = (float*)m_pParticleMemory;
	float* pInitialMem = (float*)( m_pParticleInitialMemory );

	// Hand out per-attribute slices; attributes with no per-particle data point at the constants
	for( int bit = 0; bit < MAX_PARTICLE_ATTRIBUTES; bit++ )
	{
		int nAttrSize = ( ( 1 << bit ) & ATTRIBUTES_WHICH_ARE_VEC3S_MASK ) ? 3 : 1;

		if ( nPerParticleAttributeMask & ( 1 << bit ) )
		{
			m_ParticleAttributes.m_pAttributes[ bit ] = pMem;
			m_ParticleAttributes.m_nFloatStrides[ bit ] = nAttrSize * 4;
			pMem += nAttrSize * m_nAllocatedParticles;
		}
		else
		{
			m_ParticleAttributes.m_pAttributes[ bit ] = GetConstantAttributeMemory( bit );
			m_ParticleAttributes.m_nFloatStrides[ bit ] = 0;
		}

		if ( pDef->m_nInitialAttributeReadMask & ( 1 << bit ) )
		{
			if ( m_nPerParticleReadInitialAttributeMask & ( 1 << bit ) )
			{
				Assert( pInitialMem );
				m_ParticleInitialAttributes.m_pAttributes[ bit ] = pInitialMem;
				m_ParticleInitialAttributes.m_nFloatStrides[ bit ] = nAttrSize * 4;
				pInitialMem += nAttrSize * m_nAllocatedParticles;
			}
			else if ( nPerParticleReadConstantAttributeMask & ( 1 << bit ) )
			{
				m_ParticleInitialAttributes.m_pAttributes[ bit ] = m_ParticleAttributes.m_pAttributes[ bit ];
				m_ParticleInitialAttributes.m_nFloatStrides[ bit ] = m_ParticleAttributes.m_nFloatStrides[ bit ];
			}
			else
			{
				m_ParticleInitialAttributes.m_pAttributes[ bit ] = GetConstantAttributeMemory( bit );
				m_ParticleInitialAttributes.m_nFloatStrides[ bit ] = 0;
			}
		}
		else
		{
			// not requested by any operator
			m_ParticleInitialAttributes.m_pAttributes[ bit ] = NULL;
			m_ParticleInitialAttributes.m_nFloatStrides[ bit ] = 0;
		}
	}
}


//-----------------------------------------------------------------------------
// Returns the particle collection name
//-----------------------------------------------------------------------------
const char *CParticleCollection::GetName() const
{
	return m_pDef ? m_pDef->GetName() : "";
}


//-----------------------------------------------------------------------------
// Does the particle system use the frame buffer texture (refraction?)
//-----------------------------------------------------------------------------
bool CParticleCollection::UsesPowerOfTwoFrameBufferTexture( bool bThisFrame ) const
{
	if ( ! m_bAnyUsesPowerOfTwoFrameBufferTexture )			// quick out if neither we nor any children use it
	{
		return false;
	}
	if ( bThisFrame )
	{
		return SystemContainsParticlesWithBoolSet( &CParticleCollection::m_bUsesPowerOfTwoFrameBufferTexture );
	}
	return true;
}

//-----------------------------------------------------------------------------
// Does the particle system use the full frame buffer texture (soft particles)
//-----------------------------------------------------------------------------
bool CParticleCollection::UsesFullFrameBufferTexture( bool bThisFrame ) const
{
	if ( ! m_bAnyUsesFullFrameBufferTexture )
	{
		return false;
	}
	if ( bThisFrame )
	{
		return SystemContainsParticlesWithBoolSet( &CParticleCollection::m_bUsesFullFrameBufferTexture );
	}
	return true;
}

bool CParticleCollection::SystemContainsParticlesWithBoolSet( bool CParticleCollection::*pField ) const
{
	if ( m_nActiveParticles && ( this->*pField ) )
		return true;
	for( CParticleCollection *p = m_Children.m_pHead; p; p = p->m_pNext )
	{
		if ( p->SystemContainsParticlesWithBoolSet( pField ) )
			return true;
	}
	return false;
}

void CParticleCollection::LabelTextureUsage( void )
{
	if ( m_pDef )
	{
		m_bUsesPowerOfTwoFrameBufferTexture = m_pDef->UsesPowerOfTwoFrameBufferTexture();
		m_bUsesFullFrameBufferTexture = m_pDef->UsesFullFrameBufferTexture();
	}

	for( CParticleCollection *p = m_Children.m_pHead; p; p = p->m_pNext )
	{
		p->LabelTextureUsage();
	}
}

bool CParticleCollection::ComputeUsesPowerOfTwoFrameBufferTexture()
{
	if ( !m_pDef )
		return false;

	if ( m_pDef->UsesPowerOfTwoFrameBufferTexture() )
		return true;

	for( CParticleCollection *p = m_Children.m_pHead; p; p = p->m_pNext )
	{
		if ( p->UsesPowerOfTwoFrameBufferTexture( false ) )
			return true;
	}

	return false;
}

bool CParticleCollection::ComputeUsesFullFrameBufferTexture()
{
	if ( !m_pDef )
		return false;

	if ( m_pDef->UsesFullFrameBufferTexture() )
		return true;

	for( CParticleCollection *p = m_Children.m_pHead; p; p = p->m_pNext )
	{
		if ( p->UsesFullFrameBufferTexture( false ) )
			return true;
	}

	return false;
}


//-----------------------------------------------------------------------------
// Is the particle system two-pass?
//-----------------------------------------------------------------------------
bool CParticleCollection::ContainsOpaqueCollections()
{
	if ( !m_pDef )
		return false;

	if ( m_pDef->GetMaterial() &&
		!m_pDef->GetMaterial()->IsTranslucent() )
		return true;

	for( CParticleCollection *p = m_Children.m_pHead; p; p = p->m_pNext )
	{
		if ( p->ContainsOpaqueCollections() )
			return true;
	}

	return false;
}


//-----------------------------------------------------------------------------
// Is the particle system two-pass?
//-----------------------------------------------------------------------------
bool CParticleCollection::IsTwoPass() const
{
	return m_bIsTwoPass;
}

bool CParticleCollection::ComputeIsTwoPass()
{
	if ( !ComputeIsTranslucent() )
		return false;

	return ContainsOpaqueCollections();
}


//-----------------------------------------------------------------------------
// Is the particle system translucent
//-----------------------------------------------------------------------------
bool CParticleCollection::IsTranslucent() const
{
	return m_bIsTranslucent;
}

bool CParticleCollection::ComputeIsTranslucent()
{
	if ( !m_pDef )
		return false;

	if ( m_pDef->GetMaterial() &&
		m_pDef->GetMaterial()->IsTranslucent() )
		return true;

	for( CParticleCollection *p = m_Children.m_pHead; p; p = p->m_pNext )
	{
		if ( p->IsTranslucent() )
			return true;
	}

	return false;
}


//-----------------------------------------------------------------------------
// Is the particle system batchable
//-----------------------------------------------------------------------------
bool CParticleCollection::IsBatchable() const
{
	return m_bIsBatchable;
}

bool CParticleCollection::ComputeIsBatchable()
{
	int nRendererCount = GetRendererCount();
	for( int i = 0; i < nRendererCount; i++ )
	{
		if ( !GetRenderer( i )->IsBatchable() )
			return false;
	}

	for( CParticleCollection *p = m_Children.m_pHead; p; p = p->m_pNext )
	{
		if ( !p->IsBatchable() )
			return false;
	}

	return true;
}


//-----------------------------------------------------------------------------
// Is the order of the particles important
//-----------------------------------------------------------------------------
bool CParticleCollection::IsOrderImportant() const
{
	return m_bIsOrderImportant;
}

bool CParticleCollection::ComputeIsOrderImportant()
{
	int nRendererCount = GetRendererCount();
	for( int i = 0; i < nRendererCount; i++ )
	{
		if ( GetRenderer( i )->IsOrderImportant() )
			return true;
	}

	for( CParticleCollection *p = m_Children.m_pHead; p; p = p->m_pNext )
	{
		if ( p->IsOrderImportant() )
			return true;
	}

	return false;
}


//-----------------------------------------------------------------------------
// Does this system want to run inside the parent's ApplyKillList?
//-----------------------------------------------------------------------------
bool CParticleCollection::ShouldRunForParentApplyKillList() const
{
	return m_bRunForParentApplyKillList;
}

bool CParticleCollection::ComputeRunForParentApplyKillList()
{
	// Only emitters decide this; a system whose emitters run on the parent's kill list
	// gets its initializers run there too
	int nEmitterCount = m_pDef->m_Emitters.Count();
	for( int i = 0; i < nEmitterCount; i++ )
	{
		if ( m_pDef->m_Emitters[i]->ShouldRun( true ) )
			return true;
	}

	return false;
}


//-----------------------------------------------------------------------------
// Renderer iteration
//-----------------------------------------------------------------------------
int CParticleCollection::GetRendererCount() const
{
	return IsValid() ? m_pDef->m_Renderers.Count() : 0;
}

CParticleOperatorInstance *CParticleCollection::GetRenderer( int i )
{
	return IsValid() ? m_pDef->m_Renderers[i] : NULL;
}

void *CParticleCollection::GetRendererContext( int i )
{
	return IsValid() ? m_pOperatorContextData + m_pDef->m_nRenderersCtxOffsets[i] : NULL;
}


//-----------------------------------------------------------------------------
// Visualize operators (for editing/debugging)
//-----------------------------------------------------------------------------
void CParticleCollection::VisualizeOperator( const DmObjectId_t *pOpId )
{
	m_pRenderOp = NULL;
	if ( !pOpId || !m_pDef )
		return;

	m_pRenderOp = m_pDef->FindOperatorById( FUNCTION_EMITTER, *pOpId );
	if ( !m_pRenderOp )
	{
		m_pRenderOp = m_pDef->FindOperatorById( FUNCTION_INITIALIZER, *pOpId );
		if ( !m_pRenderOp )
		{
			m_pRenderOp = m_pDef->FindOperatorById( FUNCTION_OPERATOR, *pOpId );
		}
	}
}


//-----------------------------------------------------------------------------
// Operator strength (fade in/out, random time offsets and scales)
//-----------------------------------------------------------------------------
float FadeInOut( float flFadeInStart, float flFadeInEnd, float flFadeOutStart, float flFadeOutEnd, float flCurTime )
{
	if ( flFadeInStart > flCurTime )						// not started yet
		return 0.0;

	if ( ( flFadeOutEnd > 0. ) && ( flFadeOutEnd < flCurTime ) )	// already over
		return 0.;

	// fix up out-of-order times
	flFadeInEnd = MAX( flFadeInEnd, flFadeInStart );
	flFadeOutStart = MAX( flFadeOutStart, flFadeInEnd );
	flFadeOutEnd = MAX( flFadeOutEnd, flFadeOutStart );

	float flStrength = 1.0;
	if ( ( flFadeInEnd > flCurTime ) && ( flFadeInEnd > flFadeInStart ) )
		flStrength = MIN( flStrength, FLerp( 0, 1, flFadeInStart, flFadeInEnd, flCurTime ) );

	if ( ( flCurTime > flFadeOutStart ) && ( flFadeOutEnd > flFadeOutStart ) )
		flStrength = MIN( flStrength, FLerp( 0, 1, flFadeOutEnd, flFadeOutStart, flCurTime ) );

	return flStrength;
}

bool CParticleCollection::CheckIfOperatorShouldRun( CParticleOperatorInstance const * pOp, float *pflCurStrength, bool bApplyingParentKillList )
{
	if ( !pOp->ShouldRun( bApplyingParentKillList ) )
		return false;

	if ( pOp->m_bStrengthFastPath )
	{
		*pflCurStrength = 1.0;
		return true;
	}

	if ( pOp->m_nOpEndCapState != -1 )
	{
		if ( m_bInEndCap != ( pOp->m_nOpEndCapState == 1 ) )
			return false;
	}

	float flTime = m_flCurTime;
	if ( pOp->m_nOpTimeOffsetSeed )			// per-instance random phase
	{
		float flOffset = RandomFloat( pOp->m_nOpTimeOffsetSeed, pOp->m_flOpTimeOffsetMin, pOp->m_flOpTimeOffsetMax );
		flTime += flOffset;
		flTime = MAX( 0.0, flTime );
	}
	if ( pOp->m_nOpTimeScaleSeed && ( flTime > pOp->m_flOpStartFadeInTime ) )
	{
		float flTimeScalar = 1.0 / MAX( .0001, RandomFloat( pOp->m_nOpTimeScaleSeed, pOp->m_flOpTimeScaleMin, pOp->m_flOpTimeScaleMax ) );
		flTime = pOp->m_flOpStartFadeInTime + flTimeScalar * ( flTime - pOp->m_flOpStartFadeInTime );
	}
	if ( pOp->m_flOpFadeOscillatePeriod > 0.0 )
	{
		flTime = fmod( m_flCurTime * ( 1.0 / pOp->m_flOpFadeOscillatePeriod ), 1.0 );
	}

	float flStrength = FadeInOut( pOp->m_flOpStartFadeInTime, pOp->m_flOpEndFadeInTime, 
		pOp->m_flOpStartFadeOutTime, pOp->m_flOpEndFadeOutTime, flTime );
	if ( pOp->m_nOpStrengthScaleSeed )
	{
		float flStrengthMultiplier = RandomFloat( pOp->m_nOpStrengthScaleSeed, pOp->m_flOpStrengthMinScale, pOp->m_flOpStrengthMaxScale );
		flStrength *= MAX( 0., flStrengthMultiplier );
	}
	*pflCurStrength = flStrength;
	return ( flStrength > 0.0 );
}


//-----------------------------------------------------------------------------
// Operators with no strength modulation take the fast path
//-----------------------------------------------------------------------------
void CParticleOperatorInstance::CheckForFastPath( void )
{
	// NOTE: m_flOpEndFadeInTime and m_flOpFadeOscillatePeriod are not part of this test
	// in the shipped library
	if (
		( m_flOpStartFadeInTime == 0. ) &&
		( m_flOpEndFadeOutTime == 0. ) &&
		( m_flOpStartFadeOutTime == 0. ) &&
		( m_flOpTimeOffsetMin == 0 ) &&
		( m_flOpTimeOffsetMax == 0 ) &&
		( m_flOpTimeScaleMin == 1 ) &&
		( m_flOpTimeScaleMax == 1 ) &&
		( m_flOpStrengthMaxScale == 1 ) &&
		( m_flOpStrengthMinScale == 1 ) &&
		( m_nOpEndCapState == -1 ) )
	{
		m_bStrengthFastPath = true;
	}
	else
	{
		m_bStrengthFastPath = false;
	}
}


bool CParticleOperatorInstance::HasAttribute( CParticleCollection *pParticles, int nAttribute ) const
{
	return ( pParticles->m_ParticleAttributes.Stride( nAttribute ) > 0 );
}

KillListItem_t *CParticleOperatorInstance::GetParentKillList( CParticleCollection *pParticles, int &nNumParticlesToKill ) const
{
	if ( pParticles->m_pParent )
	{
		nNumParticlesToKill = pParticles->m_pParent->m_nNumParticlesToKill;
		return pParticles->m_pParent->m_pParticleKillList;
	}
	nNumParticlesToKill = 0;
	return NULL;
}


//-----------------------------------------------------------------------------
// Runs the emitters of a system (and its children) that was restarted
//-----------------------------------------------------------------------------
void CParticleCollection::RunRestartedEmitters( void )
{
	// control point history isn't set up until the first simulation
	if ( m_nParticleFlags & PCFLAGS_FIRST_FRAME )
	{
		SimulateFirstFrame();
		m_nParticleFlags &= ~PCFLAGS_FIRST_FRAME;
	}
	else
	{
		UpdatePrevControlPoints( m_flPreviousDt );
	}

	int nEmitterCount = m_pDef->m_Emitters.Count();
	for( int i = 0; i < nEmitterCount; i++ )
	{
		int nOldParticleCount = m_nActiveParticles;
		float flEmitStrength;
		if ( CheckIfOperatorShouldRun( m_pDef->m_Emitters[i], &flEmitStrength ) )
		{
			uint32 nInittedMask = m_pDef->m_Emitters[i]->Emit( 
				this, flEmitStrength, 
				m_pOperatorContextData + m_pDef->m_nEmittersCtxOffsets[i] );
			if ( nOldParticleCount != m_nActiveParticles )
			{
				InitializeNewParticles( nOldParticleCount, m_nActiveParticles - nOldParticleCount, nInittedMask );
			}
		}
	}

	for( CParticleCollection *pChild = m_Children.m_pHead; pChild != NULL; pChild = pChild->m_pNext )
	{
		pChild->RunRestartedEmitters();
	}
}


//-----------------------------------------------------------------------------
// Sets the renderable on this system and all children
//-----------------------------------------------------------------------------
void CParticleCollection::SetRenderable( void *pRenderable )
{
	m_pRenderable = pRenderable;

	for( CParticleCollection *p = m_Children.m_pHead; p; p = p->m_pNext )
	{
		p->SetRenderable( pRenderable );
	}
}


//-----------------------------------------------------------------------------
// Restarts a particle system
//-----------------------------------------------------------------------------
void CParticleCollection::Restart( EParticleRestartMode_t eMode )
{
	// reset the drawn frame count used by the minimum-frames logic
	m_nDrawnFrames = 0;

	// process any restart still pending
	if ( m_bPendingRestart )
	{
		RunRestartedEmitters();
		m_bPendingRestart = false;
	}

	if ( eMode == RESTART_RESET_AND_MAKE_SURE_EMITS_HAPPEN )
	{
		// emitters run on the next simulation, with fresh control point history
		m_nParticleFlags &= ~PCFLAGS_PREV_CONTROL_POINTS_INITIALIZED;
		m_bPendingRestart = true;
	}

	int nEmitterCount = m_pDef->m_Emitters.Count();
	for( int i = 0; i < nEmitterCount; i++ )
	{
		m_pDef->m_Emitters[i]->Restart( this, m_pOperatorContextData + m_pDef->m_nEmittersCtxOffsets[i] );
	}

	// Update all children
	for( CParticleCollection *pChild = m_Children.m_pHead; pChild != NULL; pChild = pChild->m_pNext )
	{
		pChild->Restart( eMode );
	}
}


//-----------------------------------------------------------------------------
// Main entry point for rendering
//-----------------------------------------------------------------------------
void CParticleCollection::Render( IMatRenderContext *pRenderContext, const Vector4D &vecDiffuseModulation, bool bTranslucentOnly, void *pCameraObject )
{
	if ( !IsValid() )
		return;

	m_flNextSleepTime = MAX( m_flNextSleepTime, ( g_pParticleSystemMgr->GetLastSimulationTime() + m_pDef->m_flNoDrawTimeToGoToSleep ) );

	if ( m_nActiveParticles != 0 )
	{
		Vector4D vecActualModulation;
		Vector4DMultiply( vecDiffuseModulation, m_pDef->m_vecMaterialModulation, vecActualModulation );

		IMaterial *pMaterial = m_pDef->GetMaterial();
		if ( pMaterial && 
			( !bTranslucentOnly || m_pDef->GetMaterial()->IsTranslucent() || ( vecActualModulation[3] != 1.0f ) ) )
		{
			int nCount = m_pDef->m_Renderers.Count();
			for( int i = 0; i < nCount; i++ )
			{
				float flStrength;
				if ( !CheckIfOperatorShouldRun( m_pDef->m_Renderers[i], &flStrength ) )
					continue;

				if ( m_pDef->IsScreenSpaceEffect() )
				{
					pRenderContext->MatrixMode( MATERIAL_VIEW );
					pRenderContext->PushMatrix();
					pRenderContext->LoadIdentity();
					pRenderContext->MatrixMode( MATERIAL_PROJECTION );
					pRenderContext->PushMatrix();
					pRenderContext->LoadIdentity();
					pRenderContext->Ortho( -100, -100, 100, 100, -100, 100 );
					m_pDef->m_Renderers[i]->Render(
						pRenderContext, this, vecActualModulation, m_pOperatorContextData + m_pDef->m_nRenderersCtxOffsets[i] );
					pRenderContext->MatrixMode( MATERIAL_VIEW );
					pRenderContext->PopMatrix();
					pRenderContext->MatrixMode( MATERIAL_PROJECTION );
					pRenderContext->PopMatrix();
				}
				else
				{
					m_pDef->m_Renderers[i]->Render(
						pRenderContext, this, vecActualModulation, m_pOperatorContextData + m_pDef->m_nRenderersCtxOffsets[i] );
				}
			}
		}
	}

	// let children render
	for( CParticleCollection *p = m_Children.m_pHead; p; p = p->m_pNext )
	{
		p->Render( pRenderContext, vecDiffuseModulation, bTranslucentOnly, pCameraObject );
	}

	// Visualize specific ops for debugging/editing
	if ( m_pRenderOp )
	{
		m_pRenderOp->Render( this );
	}
}

void CParticleCollection::UpdatePrevControlPoints( float dt )
{
	m_flPreviousDt = dt;
	for( int i = 0; i <= m_nHighestCP; ++i )
		ControlPoint( i ).m_PrevPosition = ControlPoint( i ).m_Position;
	m_nParticleFlags |= PCFLAGS_PREV_CONTROL_POINTS_INITIALIZED;
}


//-----------------------------------------------------------------------------
// Runs the initializers over newly created particles
//-----------------------------------------------------------------------------
void CParticleCollection::InitializeNewParticles( int nFirstParticle, int nParticleCount, uint32 nInittedMask, bool bApplyingParentKillList )
{
	VPROF_BUDGET( "CParticleCollection::InitializeNewParticles", VPROF_BUDGETGROUP_PARTICLE_SIMULATION );

#ifdef _DEBUG
	m_bIsRunningInitializers = true;
#endif

	// attributes not yet set by the emitter
	int nAttrsLeftToInit = ( m_nPerParticleInitializedAttributeMask | m_nPerParticleUpdatedAttributeMask ) & ~nInittedMask;
	int nInitializerCount = m_pDef->m_Initializers.Count();
	for ( int i = 0; i < nInitializerCount; i++ )
	{
		CParticleOperatorInstance *pOp = m_pDef->m_Initializers[i];
		int nInitializerAttrMask = pOp->GetWrittenAttributes();
		if ( ( ( nInitializerAttrMask & nAttrsLeftToInit ) == 0 ) || pOp->InitMultipleOverride() )
			continue;

		if ( !pOp->ShouldRun( bApplyingParentKillList ) )
			continue;

		void *pContext = m_pOperatorContextData + m_pDef->m_nInitializersCtxOffsets[i];
		if ( m_bIsScrubbable && !pOp->IsScrubSafe() )
		{
			for ( int j = 0; j < nParticleCount; ++j )
			{
				pOp->InitNewParticlesScalar( this, nFirstParticle + j, 1, nAttrsLeftToInit, pContext );
			}
		}
		else
		{
			pOp->InitNewParticles( this, nFirstParticle, nParticleCount, nAttrsLeftToInit, pContext );
		}
		nAttrsLeftToInit &= ~nInitializerAttrMask;
	}

	// second pass: initializers that want to run regardless of what's already set
	for ( int i = 0; i < nInitializerCount; i++ )
	{
		int nInitializerAttrMask = m_pDef->m_Initializers[i]->GetWrittenAttributes();
		CParticleOperatorInstance *pOp = m_pDef->m_Initializers[i];
		if ( !pOp->InitMultipleOverride() )
			continue;

		if ( !pOp->ShouldRun( bApplyingParentKillList ) )
			continue;

		void *pContext = m_pOperatorContextData + m_pDef->m_nInitializersCtxOffsets[i];
		if ( m_bIsScrubbable && !pOp->IsScrubSafe() )
		{
			for ( int j = 0; j < nParticleCount; ++j )
			{
				pOp->InitNewParticlesScalar( this, nFirstParticle + j, 1, nAttrsLeftToInit, pContext );
			}
		}
		else
		{
			pOp->InitNewParticles( this, nFirstParticle, nParticleCount, nAttrsLeftToInit, pContext );
		}
		nAttrsLeftToInit &= ~nInitializerAttrMask;
	}

#ifdef _DEBUG
	m_bIsRunningInitializers = false;
#endif

	InitParticleAttributes( nFirstParticle, nParticleCount, nAttrsLeftToInit );

	CopyInitialAttributeValues( nFirstParticle, nParticleCount );
}


//-----------------------------------------------------------------------------
// Skip time
//-----------------------------------------------------------------------------
void CParticleCollection::SkipToTime( float t )
{
	if ( t > m_flCurTime )
	{
		UpdatePrevControlPoints( t - m_flCurTime );

		m_nParticleFlags &= ~PCFLAGS_FIRST_FRAME;

		m_flCurTime = t;
		m_fl4CurTime = ReplicateX4( t );

		int nEmitterCount = m_pDef->m_Emitters.Count();
		for ( int i = 0; i < nEmitterCount; ++i )
		{
			m_pDef->m_Emitters[i]->SkipToTime( t, this, m_pOperatorContextData + m_pDef->m_nEmittersCtxOffsets[i] );
		}

		// children, offset by their delay
		int i = 0;
		for( CParticleCollection *pChild = m_Children.m_pHead; pChild != NULL; pChild = pChild->m_pNext, ++i )
		{
			pChild->SkipToTime( t - m_pDef->m_Children[i].m_flDelay );
		}
	}
}


//-----------------------------------------------------------------------------
// Simulate the first frame: control point history, pre-emission operators, initial particles
//-----------------------------------------------------------------------------
void CParticleCollection::SimulateFirstFrame( )
{
	m_flPrevSimTime = 1.0e23;
	m_flDt = 0.0f;
	m_nDrawnFrames = 0;

	// Seed the control point history if nothing has done so yet
	if ( ( m_nParticleFlags & PCFLAGS_PREV_CONTROL_POINTS_INITIALIZED ) == 0 )
	{
		UpdatePrevControlPoints( 0.05f );
	}

	m_nOperatorRandomSampleOffset = 0;
	int nCount = m_pDef->m_Operators.Count();
	for( int i = 0; i < nCount; i++ )
	{
		float flStrength;
		CParticleOperatorInstance *pOp = m_pDef->m_Operators[i];
		if ( pOp->ShouldRunBeforeEmitters() &&
			 CheckIfOperatorShouldRun( pOp, &flStrength ) )
		{
			pOp->Operate( this, flStrength, m_pOperatorContextData + m_pDef->m_nOperatorsCtxOffsets[i] );
			UpdatePrevControlPoints( 0.05f );
		}
		m_nOperatorRandomSampleOffset += 17;
	}

	// initial particles
	int nInitialParticles = MIN( m_pDef->m_nInitialParticles, m_nMaxAllowedParticles );
	if ( nInitialParticles > 0 )
	{
		SetNActiveParticles( nInitialParticles );
		InitializeNewParticles( 0, nInitialParticles, 0 );
	}
}


void CParticleCollection::EmitAndInit( CParticleCollection *pCollection, bool bApplyingParentKillList ) // static
{
	if ( bApplyingParentKillList && !pCollection->ShouldRunForParentApplyKillList() )
		return;

	int nEmitterCount = pCollection->m_pDef->m_Emitters.Count();
	for( int i = 0; i < nEmitterCount; i++ )
	{
		int nOldParticleCount = pCollection->m_nActiveParticles;
		float flEmitStrength;
		if ( pCollection->CheckIfOperatorShouldRun( pCollection->m_pDef->m_Emitters[i], &flEmitStrength, bApplyingParentKillList ) )
		{
			uint32 nInittedMask = pCollection->m_pDef->m_Emitters[i]->Emit( 
				pCollection, flEmitStrength, 
				pCollection->m_pOperatorContextData + pCollection->m_pDef->m_nEmittersCtxOffsets[i] );
			if ( nOldParticleCount != pCollection->m_nActiveParticles )
			{
				pCollection->InitializeNewParticles( nOldParticleCount, pCollection->m_nActiveParticles - nOldParticleCount, nInittedMask, bApplyingParentKillList );
			}
		}
	}
}


//-----------------------------------------------------------------------------
// Simulates a particle system
//-----------------------------------------------------------------------------
void CParticleCollection::Simulate( float dt )
{
	VPROF_BUDGET( "CParticleCollection::Simulate", VPROF_BUDGETGROUP_PARTICLE_SIMULATION );
	if ( ( dt < 0.0f ) || ( m_bFrozen ) )
		return;

	if ( !m_pDef )
		return;

	// Delayed systems (negative time) only advance their clock until they reach t == 0
	if ( m_flCurTime < 0.0f )
	{
		if ( dt >= 1.0e-22 )
		{
			m_flCurTime += dt;
			m_fl4CurTime = ReplicateX4( m_flCurTime );
			UpdatePrevControlPoints( dt );
		}
		return;
	}

	if ( m_nParticleFlags & PCFLAGS_FIRST_FRAME )
	{
		SimulateFirstFrame();
		m_nParticleFlags &= ~PCFLAGS_FIRST_FRAME;
	}
	else
	{
		// after a reset, copy the control points into the previous control points
		if ( ! ( m_nParticleFlags & PCFLAGS_PREV_CONTROL_POINTS_INITIALIZED ) )
			UpdatePrevControlPoints( m_flPreviousDt );
	}

	if ( dt < 1.0e-22 )
		return;

	m_bPendingRestart = false;

	bool bAttachedKillList = false;
	if ( !HasAttachedKillList() )
	{
		g_pParticleSystemMgr->AttachKillList( this );
		bAttachedKillList = true;
	}

	float flRemainingDt = dt;
	float flMaxDT = 0.1;										// default
	if ( m_pDef->m_flMaximumTimeStep > 0.0 )
		flMaxDT = m_pDef->m_flMaximumTimeStep;

	// Cap the time simulated during the first rendered frames
	if ( m_pDef->m_flMaximumSimTime != 0.0 && ( m_nDrawnFrames <= m_pDef->m_nMinimumFrames ) )
	{
		if ( ( flRemainingDt + m_flCurTime ) > m_pDef->m_flMaximumSimTime )
		{
			flRemainingDt = m_pDef->m_flMaximumSimTime - m_flCurTime;
			flRemainingDt = MAX( m_pDef->m_flMinimumSimTime, flRemainingDt );
		}
		m_nDrawnFrames += 1;
	}

	flRemainingDt = MIN( flRemainingDt, 10 * flMaxDT );			// at most 10 steps

	m_flTargetDrawTime += flRemainingDt;
	if ( ( m_flTargetDrawTime >= m_flPrevSimTime ) && ( m_flTargetDrawTime < m_flCurTime ) )
	{
		// still inside the last simulated step; rendering interpolates
		flRemainingDt = 0;
	}

	float flMinTime = m_pDef->m_flMinimumTimeStep;
	bool bSaveOldValuesForInterpolation = false;
	while( flRemainingDt > 0.0 )
	{
		float flDT_ThisStep = flRemainingDt;
		if ( flDT_ThisStep > flMaxDT )
		{
			flDT_ThisStep = flMaxDT;
		}
		else
		{
			if ( flMinTime > flDT_ThisStep )
			{
				flDT_ThisStep = flMinTime;
				bSaveOldValuesForInterpolation = true;
			}
		}
		flRemainingDt -= flDT_ThisStep;

		if ( m_flDt )
			m_flPreviousDt = m_flDt;
		m_flDt = flDT_ThisStep;
		m_flPrevSimTime = m_flCurTime;
		m_flCurTime += flDT_ThisStep;
		m_fl4CurTime = ReplicateX4( m_flCurTime );

		// keep the previous step's attributes around for interpolated rendering
		if ( bSaveOldValuesForInterpolation )
		{
			if ( ! m_pPreviousAttributeMemory )
			{
				m_pPreviousAttributeMemory = ( uint8 * ) MemAlloc_AllocAligned( m_nAttributeMemorySize, 16 );
				memset( m_pPreviousAttributeMemory, 0, m_nAttributeMemorySize );

				// same layout as the current attributes, relocated into the new block
				m_PreviousFrameAttributes = m_ParticleAttributes;
				for( int i = 0; i < MAX_PARTICLE_ATTRIBUTES; i++ )
				{
					if ( m_ParticleAttributes.Stride( i ) )
					{
						m_PreviousFrameAttributes.m_pAttributes[i] = ( float * ) 
							( GetPrevAttributeMemory() + 
							  ( ( uint8 * ) m_ParticleAttributes.m_pAttributes[i] - GetAttributeMemory() ) );
					}
				}
			}
			CopyParticleAttributesToPreviousAttributes();
		}

		m_nOperatorRandomSampleOffset = 0;
		int nCount = m_pDef->m_Operators.Count();
		for( int i = 0; i < nCount; i++ )
		{
			float flStrength;
			CParticleOperatorInstance *pOp = m_pDef->m_Operators[i];
			if ( pOp->ShouldRunBeforeEmitters() &&
				 CheckIfOperatorShouldRun( pOp, &flStrength ) )
			{
				pOp->Operate( this, flStrength, m_pOperatorContextData + m_pDef->m_nOperatorsCtxOffsets[i] );
				if ( m_nNumParticlesToKill )
				{
					ApplyKillList();
				}
				m_nOperatorRandomSampleOffset += 17;
			}
		}

		// emit and initialize new particles
		EmitAndInit( this );

		// operators that run after emission
		m_nOperatorRandomSampleOffset = 0;
		nCount = m_pDef->m_Operators.Count();
		if ( m_nActiveParticles )
		{
			for( int i = 0; i < nCount; i++ )
			{
				float flStrength;
				CParticleOperatorInstance *pOp = m_pDef->m_Operators[i];
				if ( ( !pOp->ShouldRunBeforeEmitters() ) &&
					 CheckIfOperatorShouldRun( pOp, &flStrength ) )
				{
					pOp->Operate( this, flStrength, m_pOperatorContextData + m_pDef->m_nOperatorsCtxOffsets[i] );
					if ( m_nNumParticlesToKill )
					{
						ApplyKillList();
						if ( !m_nActiveParticles )
							break;				// nothing left to operate on
					}
					m_nOperatorRandomSampleOffset += 17;
				}
			}
		}

		nCount = m_pDef->m_Renderers.Count();
		for( int i = 0; i < nCount; i++ )
		{
			CParticleOperatorInstance *pOp = m_pDef->m_Renderers[i];
			pOp->PostSimulate( this, m_pOperatorContextData + m_pDef->m_nRenderersCtxOffsets[i] );
		}
	}

	// children run with this system's kill list on loan
	for( CParticleCollection *i = m_Children.m_pHead; i; i = i->m_pNext )
	{
		LoanKillListTo( i );
		i->Simulate( dt );
		i->m_pParticleKillList = NULL;
	}

	if ( bAttachedKillList )
		g_pParticleSystemMgr->DetachKillList( this );

	UpdatePrevControlPoints( dt );

	// Bloat the bounding box by bounds around the control point
	BloatBoundsUsingControlPoint();

	// stop simulating once past the definition's freeze time
	if ( m_pDef->m_flStopSimulationAfterTime < m_flCurTime )
	{
		m_bFrozen = true;
	}
}


//-----------------------------------------------------------------------------
// Copies the constant attributes into the per-particle attributes
//-----------------------------------------------------------------------------
void CParticleCollection::InitParticleAttributes( int nStartParticle, int nNumParticles, int nAttrsLeftToInit )
{
	if ( nAttrsLeftToInit == 0 )
		return;

	for( int i = nStartParticle; i < nStartParticle + nNumParticles; i++ )
	{
		for ( int nAttr = 0; nAttr < MAX_PARTICLE_ATTRIBUTES; ++nAttr )
		{
			if ( ( nAttrsLeftToInit & ( 1 << nAttr ) ) == 0 )
				continue;

			float *pAttrData = GetFloatAttributePtrForWrite( nAttr, i );

			// particle ids come from the random seed plus a running counter
			if ( nAttr == PARTICLE_ATTRIBUTE_PARTICLE_ID )
			{
				*( (int*)pAttrData ) = ( m_nRandomSeed + m_nUniqueParticleId ) & RANDOM_FLOAT_MASK;
				m_nUniqueParticleId++;
				continue;
			}

			// creation time is the current time
			if ( nAttr == PARTICLE_ATTRIBUTE_CREATION_TIME )
			{
				*pAttrData = m_flCurTime;
				continue;
			}

			// everything else comes from the constant table
			Assert( m_ParticleAttributes.Stride( nAttr ) != 0 );
			float *pConstantAttr = GetConstantAttributeMemory( nAttr );
			*pAttrData = *pConstantAttr;
			if ( m_ParticleAttributes.Stride( nAttr ) == 12 )
			{
				pAttrData[4] = pConstantAttr[4];
				pAttrData[8] = pConstantAttr[8];
			}
		}
	}
}

void CParticleCollection::CopyInitialAttributeValues( int nStartParticle, int nNumParticles )
{
	if ( m_nPerParticleReadInitialAttributeMask == 0 )
		return;

	for ( int i = nStartParticle; i < nStartParticle + nNumParticles; ++i )
	{
		for ( int nAttr = 0; nAttr < MAX_PARTICLE_ATTRIBUTES; ++nAttr )
		{
			if ( m_nPerParticleReadInitialAttributeMask & (1 << nAttr) )
			{
				const float *pSrcAttribute = GetFloatAttributePtr( nAttr, i );
				float *pDestAttribute = GetInitialFloatAttributePtrForWrite( nAttr, i );
				Assert( m_ParticleInitialAttributes.Stride( nAttr ) == m_ParticleAttributes.Stride( nAttr ) );
				Assert( m_ParticleAttributes.Stride( nAttr ) != 0 );
				pDestAttribute[0] = pSrcAttribute[0];
				if ( m_ParticleAttributes.Stride( nAttr ) == 12 )
				{
					pDestAttribute[4] = pSrcAttribute[4];
					pDestAttribute[8] = pSrcAttribute[8];
				}
			}
		}
	}
}

void CParticleCollection::CopyParticleAttributesToPreviousAttributes( void ) const
{
	for( int i = 0; i < MAX_PARTICLE_ATTRIBUTES; i++ )
	{
		if ( m_PreviousFrameAttributes.Stride( i ) )
		{
			memcpy( m_PreviousFrameAttributes.m_pAttributes[i], m_ParticleAttributes.m_pAttributes[i],
					m_nPaddedActiveParticles * m_PreviousFrameAttributes.Stride( i ) * sizeof(float) );
		}
	}
}


//-----------------------------------------------------------------------------
// Uniformly distributed random point inside the unit sphere; returns its radius
//-----------------------------------------------------------------------------
float CParticleCollection::RandomVectorInUnitSphere( int nRandomSampleId, Vector *pVector )
{
	// cos(phi) uniform in [-1,1], theta uniform in [0,2pi), radius from the cube root of a uniform
	float flPhi = acos( 1 - 2 * RandomFloat( nRandomSampleId, 0.0001f, 1.0f ) );
	float flTheta = 2 * M_PI * RandomFloat( nRandomSampleId+1, 0.0001f, 1.0f );
	float flRadius = powf( RandomFloat( nRandomSampleId+2, 0.0001f, 1.0f ), 1.0f / 3.0f );

	float flSinPhi, flCosPhi;
	float flSinTheta, flCosTheta;
	SinCos( flPhi, &flSinPhi, &flCosPhi );
	SinCos( flTheta, &flSinTheta, &flCosTheta );

	pVector->x = flRadius * flSinPhi * flCosTheta;
	pVector->y = flRadius * flSinPhi * flSinTheta;
	pVector->z = flRadius * flCosPhi;
	return flRadius;
}


//-----------------------------------------------------------------------------
// Control point position at a time between the previous and current simulation step
//-----------------------------------------------------------------------------
void CParticleCollection::GetControlPointAtTime( int nControlPoint, float flTime, Vector *pControlPoint )
{
	Assert( m_pDef->ReadsControlPoint( nControlPoint ) );

	float flPrevTime = m_flCurTime - m_flDt;

	// clamp to the start of the current step
	if ( flTime < flPrevTime )
	{
		flTime = flPrevTime;
	}

	if ( m_flDt == 0.0f )
	{
		VectorCopy( ControlPoint( nControlPoint ).m_Position, *pControlPoint );
		return;
	}

	float t = ( flTime - flPrevTime ) / m_flDt;
	VectorLerp( ControlPoint( nControlPoint ).m_PrevPosition, ControlPoint( nControlPoint ).m_Position, t, *pControlPoint );
}

void CParticleCollection::GetControlPointAtPrevTime( int nControlPoint, Vector *pControlPoint )
{
	Assert( m_pDef->ReadsControlPoint( nControlPoint ) );
	*pControlPoint = ControlPoint( nControlPoint ).m_PrevPosition;
}

void CParticleCollection::GetControlPointTransformAtCurrentTime( int nControlPoint, matrix3x4_t *pMat )
{
	Assert( m_pDef->ReadsControlPoint( nControlPoint ) );
	const Vector &vecControlPoint = GetControlPointAtCurrentTime( nControlPoint );

	Vector vecLeft;
	VectorMultiply( ControlPoint( nControlPoint ).m_RightVector, -1.0f, vecLeft );
	pMat->Init( ControlPoint( nControlPoint ).m_ForwardVector, vecLeft, ControlPoint( nControlPoint ).m_UpVector, vecControlPoint );
}

void CParticleCollection::GetControlPointTransformAtCurrentTime( int nControlPoint, VMatrix *pMat )
{
	GetControlPointTransformAtCurrentTime( nControlPoint, &pMat->As3x4() );
	pMat->m[3][0] = pMat->m[3][1] = pMat->m[3][2] = 0.0f;
	pMat->m[3][3] = 1.0f;
}

void CParticleCollection::GetControlPointOrientationAtTime( int nControlPoint, float flTime, Vector *pForward, Vector *pRight, Vector *pUp )
{
	Assert( m_pDef->ReadsControlPoint( nControlPoint ) );

	// orientation is not interpolated; the current one is used
	*pForward = ControlPoint( nControlPoint ).m_ForwardVector;
	*pRight = ControlPoint( nControlPoint ).m_RightVector;
	*pUp = ControlPoint( nControlPoint ).m_UpVector;
}

void CParticleCollection::GetControlPointTransformAtTime( int nControlPoint, float flTime, matrix3x4_t *pMat )
{
	Assert( m_pDef->ReadsControlPoint( nControlPoint ) );
	Vector vecControlPoint;
	GetControlPointAtTime( nControlPoint, flTime, &vecControlPoint );

	// position is interpolated, orientation is the current one
	Vector vecLeft;
	VectorMultiply( ControlPoint( nControlPoint ).m_RightVector, -1.0f, vecLeft );
	pMat->Init( ControlPoint( nControlPoint ).m_ForwardVector, vecLeft, ControlPoint( nControlPoint ).m_UpVector, vecControlPoint );
}

void CParticleCollection::GetControlPointTransformAtTime( int nControlPoint, float flTime, VMatrix *pMat )
{
	GetControlPointTransformAtTime( nControlPoint, flTime, &pMat->As3x4() );
	pMat->m[3][0] = pMat->m[3][1] = pMat->m[3][2] = 0.0f;
	pMat->m[3][3] = 1.0f;
}

void CParticleCollection::GetControlPointTransformAtTime( int nControlPoint, float flTime, CParticleSIMDTransformation *pXForm )
{
	Assert( m_pDef->ReadsControlPoint( nControlPoint ) );
	Vector vecControlPoint;
	GetControlPointAtTime( nControlPoint, flTime, &vecControlPoint );

	pXForm->m_v4Origin.DuplicateVector( vecControlPoint );
	pXForm->m_v4Fwd.DuplicateVector( ControlPoint( nControlPoint ).m_ForwardVector );
	pXForm->m_v4Up.DuplicateVector( ControlPoint( nControlPoint ).m_UpVector );
	pXForm->m_v4Right.DuplicateVector( ControlPoint( nControlPoint ).m_RightVector );
}

int CParticleCollection::GetHighestControlPoint( void ) const
{
	return m_nHighestCP;
}


//-----------------------------------------------------------------------------
// Returns the render bounds
//-----------------------------------------------------------------------------
void CParticleCollection::GetBounds( Vector *pMin, Vector *pMax )
{
	*pMin = m_MinBounds;
	*pMax = m_MaxBounds;
}


//-----------------------------------------------------------------------------
// Recomputes our bounds and merges in the bounds of children with particles
//-----------------------------------------------------------------------------
void CParticleCollection::BloatBoundsUsingControlPoint()
{
	RecomputeBounds();

	// children have already recomputed their own bounds in Simulate
	bool bIsValid = m_bBoundsValid;
	for( CParticleCollection *i = m_Children.m_pHead; i; i = i->m_pNext )
	{
		if ( i->m_nActiveParticles > 0 )
		{
			VectorMin( m_MinBounds, i->m_MinBounds, m_MinBounds );
			VectorMax( m_MaxBounds, i->m_MaxBounds, m_MaxBounds );
			bIsValid = ( bIsValid || i->m_bBoundsValid );
		}
	}

	m_bBoundsValid = bIsValid;
}


//-----------------------------------------------------------------------------
// Bounds helpers (min, max and running sum)
//-----------------------------------------------------------------------------
void UpdateBounds( fltx4 &min_, fltx4 &max_, fltx4 &sum_, fltx4 val )
{
	min_ = MinSIMD( min_, val );
	max_ = MaxSIMD( max_, val );
	sum_ = AddSIMD( sum_, val );
}

void UpdateBounds( float &min_, float &max_, float &sum_, float val )
{
	min_ = MIN( min_, val );
	max_ = MAX( max_, val );
	sum_ += val;
}


//-----------------------------------------------------------------------------
// Recomputes the bounds (and center) from the particle positions, extended by
// each particle's trail if the definition has a maximum tail length
//-----------------------------------------------------------------------------
void CParticleCollection::RecomputeBounds( void )
{
	if ( m_nActiveParticles == 0 )
	{
		m_bBoundsValid = false;
		m_MinBounds.Init( FLT_MAX, FLT_MAX, FLT_MAX );
		m_MaxBounds.Init( -FLT_MAX, -FLT_MAX, -FLT_MAX );
		m_Center.Init();
		return;
	}

	fltx4 min_x = ReplicateX4( 1.0e23 );
	fltx4 min_y = min_x;
	fltx4 min_z = min_x;
	fltx4 max_x = ReplicateX4( -1.0e23 );
	fltx4 max_y = max_x;
	fltx4 max_z = max_x;

	fltx4 sum_x = Four_Zeros;
	fltx4 sum_y = Four_Zeros;
	fltx4 sum_z = Four_Zeros;

	float flMaxTail = m_pDef->GetMaxTailLength();
	float flOODt = ( m_flDt != 0.0f ) ? ( 1.0f / m_flDt ) : 1.0f;

	size_t xyz_stride;
	const fltx4 *xyz = GetM128AttributePtr( PARTICLE_ATTRIBUTE_XYZ, &xyz_stride );
	size_t prev_stride;
	const fltx4 *prev = GetM128AttributePtr( PARTICLE_ATTRIBUTE_PREV_XYZ, &prev_stride );
	size_t trail_stride;
	const fltx4 *trail = GetM128AttributePtr( PARTICLE_ATTRIBUTE_TRAIL_LENGTH, &trail_stride );

	int ctr = m_nActiveParticles / 4;
	fltx4 maxtail = ReplicateX4( flMaxTail );
	fltx4 oodt = ReplicateX4( flOODt );
	bool bHasTail = ( flMaxTail > 0.0f );
	if ( bHasTail )
	{
		while ( ctr-- )
		{
			// extend each particle back towards its previous position by its trail length
			fltx4 delta_x = SubSIMD( prev[0], xyz[0] );
			fltx4 delta_y = SubSIMD( prev[1], xyz[1] );
			fltx4 delta_z = SubSIMD( prev[2], xyz[2] );
			fltx4 len = MaxSIMD( ReplicateX4( 0.001f ), 
				SqrtSIMD( AddSIMD( AddSIMD( MulSIMD( delta_x, delta_x ), MulSIMD( delta_y, delta_y ) ), MulSIMD( delta_z, delta_z ) ) ) );
			fltx4 oolen = ReciprocalSIMD( len );

			// tail length = trail * speed, capped at the definition's maximum
			fltx4 tail = MulSIMD( trail[0], len );
			tail = MulSIMD( tail, oodt );
			tail = MinSIMD( tail, maxtail );

			fltx4 tail_x = AddSIMD( xyz[0], MulSIMD( MulSIMD( oolen, delta_x ), tail ) );
			fltx4 tail_y = AddSIMD( xyz[1], MulSIMD( MulSIMD( oolen, delta_y ), tail ) );
			fltx4 tail_z = AddSIMD( xyz[2], MulSIMD( MulSIMD( oolen, delta_z ), tail ) );

			UpdateBounds( min_x, max_x, sum_x, xyz[0] );
			UpdateBounds( min_x, max_x, sum_x, tail_x );
			UpdateBounds( min_y, max_y, sum_y, xyz[1] );
			UpdateBounds( min_y, max_y, sum_y, tail_y );
			UpdateBounds( min_z, max_z, sum_z, xyz[2] );
			UpdateBounds( min_z, max_z, sum_z, tail_z );

			xyz += xyz_stride;
			prev += prev_stride;
			trail += trail_stride;
		}
	}
	else
	{
		while ( ctr-- )
		{
			UpdateBounds( min_x, max_x, sum_x, xyz[0] );
			UpdateBounds( min_y, max_y, sum_y, xyz[1] );
			UpdateBounds( min_z, max_z, sum_z, xyz[2] );
			xyz += xyz_stride;
		}
	}

	m_bBoundsValid = true;
	m_MinBounds.x = MIN( MIN( SubFloat( min_x, 0 ), SubFloat( min_x, 1 ) ), MIN( SubFloat( min_x, 2 ), SubFloat( min_x, 3 ) ) );
	m_MinBounds.y = MIN( MIN( SubFloat( min_y, 0 ), SubFloat( min_y, 1 ) ), MIN( SubFloat( min_y, 2 ), SubFloat( min_y, 3 ) ) );
	m_MinBounds.z = MIN( MIN( SubFloat( min_z, 0 ), SubFloat( min_z, 1 ) ), MIN( SubFloat( min_z, 2 ), SubFloat( min_z, 3 ) ) );

	m_MaxBounds.x = MAX( MAX( SubFloat( max_x, 0 ), SubFloat( max_x, 1 ) ), MAX( SubFloat( max_x, 2 ), SubFloat( max_x, 3 ) ) );
	m_MaxBounds.y = MAX( MAX( SubFloat( max_y, 0 ), SubFloat( max_y, 1 ) ), MAX( SubFloat( max_y, 2 ), SubFloat( max_y, 3 ) ) );
	m_MaxBounds.z = MAX( MAX( SubFloat( max_z, 0 ), SubFloat( max_z, 1 ) ), MAX( SubFloat( max_z, 2 ), SubFloat( max_z, 3 ) ) );

	float fsum_x = SubFloat( sum_x, 3 ) + SubFloat( sum_x, 2 ) + SubFloat( sum_x, 1 ) + SubFloat( sum_x, 0 );
	float fsum_y = SubFloat( sum_y, 3 ) + SubFloat( sum_y, 2 ) + SubFloat( sum_y, 1 ) + SubFloat( sum_y, 0 );
	float fsum_z = SubFloat( sum_z, 3 ) + SubFloat( sum_z, 2 ) + SubFloat( sum_z, 1 ) + SubFloat( sum_z, 0 );

	// remaining 0-3 particles, scalar
	for ( int i = 0; i < ( m_nActiveParticles & 3 ); i++ )
	{
		Vector pos( SubFloat( xyz[0], i ), SubFloat( xyz[1], i ), SubFloat( xyz[2], i ) );
		UpdateBounds( m_MinBounds.x, m_MaxBounds.x, fsum_x, pos.x );
		UpdateBounds( m_MinBounds.y, m_MaxBounds.y, fsum_y, pos.y );
		UpdateBounds( m_MinBounds.z, m_MaxBounds.z, fsum_z, pos.z );
		if ( bHasTail )
		{
			if ( flMaxTail > 0.0f )
			{
				Vector dir( SubFloat( prev[0], i ) - pos.x, SubFloat( prev[1], i ) - pos.y, SubFloat( prev[2], i ) - pos.z );

				float len = VectorNormalize( dir );
				len = MIN( MAX( 0.001f, len ) * SubFloat( trail[0], i ) * flOODt, flMaxTail );
				pos += len * dir;

				UpdateBounds( m_MinBounds.x, m_MaxBounds.x, fsum_x, pos.x );
				UpdateBounds( m_MinBounds.y, m_MaxBounds.y, fsum_y, pos.y );
				UpdateBounds( m_MinBounds.z, m_MaxBounds.z, fsum_z, pos.z );
			}
		}
	}

	VectorAdd( m_MinBounds, m_pDef->m_BoundingBoxMin, m_MinBounds );
	VectorAdd( m_MaxBounds, m_pDef->m_BoundingBoxMax, m_MaxBounds );

	// center = mean of all points that went into the bounds
	float flOONumParticles = 1.0 / ( ( flMaxTail > 0.0f ) ? 2 * m_nActiveParticles : m_nActiveParticles );
	m_Center.x = flOONumParticles * fsum_x;
	m_Center.y = flOONumParticles * fsum_y;
	m_Center.z = flOONumParticles * fsum_z;
}


//-----------------------------------------------------------------------------
// IsFinished returns true when a system has no particles and won't be creating any more
//-----------------------------------------------------------------------------
bool CParticleCollection::IsFinished( void ) const
{
	if ( !m_pDef )
		return true;
	if ( m_nParticleFlags & PCFLAGS_FIRST_FRAME )
		return false;
	if ( m_nActiveParticles )
		return false;
	if ( m_bDormant )
		return false;

	// no particles; see whether any emitter may still create some
	int nEmitterCount = m_pDef->m_Emitters.Count();
	for( int i = 0; i < nEmitterCount; i++ )
	{
		if ( m_pDef->m_Emitters[i]->MayCreateMoreParticles( this, m_pOperatorContextData + m_pDef->m_nEmittersCtxOffsets[i] ) )
			return false;
	}

	// all children must be finished too (end cap children only matter while the end cap plays)
	int i = 0;
	for( CParticleCollection *pChild = m_Children.m_pHead; pChild != NULL; pChild = pChild->m_pNext, ++i )
	{
		if ( !pChild->IsFinished() && !m_pDef->m_Children[i].m_bEndCap )
			return false;

		if ( m_pDef->m_Children[i].m_bEndCap && !pChild->IsFinished() && m_bInEndCap )
			return false;
	}

	return true;
}


//-----------------------------------------------------------------------------
// Stop emitting particles
//-----------------------------------------------------------------------------
void CParticleCollection::StopEmission( bool bInfiniteOnly, bool bRemoveAllParticles, bool bWakeOnStop, bool bPlayEndCap )
{
	if ( !m_pDef )
		return;

	// stopping emission always clears dormancy
	m_bDormant = false;
	if ( bWakeOnStop )
	{
		// stay awake a while so existing particles can finish
		m_flNextSleepTime = MAX( m_flNextSleepTime, ( g_pParticleSystemMgr->GetLastSimulationTime() + 10 ) );
	}

	m_bEmissionStopped = true;
	for( int i = 0; i < m_pDef->m_Emitters.Count(); i++ )
	{
		m_pDef->m_Emitters[i]->StopEmission( this, m_pOperatorContextData + m_pDef->m_nEmittersCtxOffsets[i], bInfiniteOnly );
	}

	if ( bRemoveAllParticles )
	{
		SetNActiveParticles( 0 );
	}

	// Stop our children as well
	if ( bPlayEndCap )
	{
		m_bInEndCap = true;
		int i = 0;
		for( CParticleCollection *pChild = m_Children.m_pHead; pChild != NULL; pChild = pChild->m_pNext, ++i )
		{
			pChild->m_bInEndCap = true;
			if ( m_pDef->m_Children[i].m_bEndCap )
			{
				// end cap children start playing now
				pChild->m_flCurTime = 0.0f;
				pChild->StartEmission( bInfiniteOnly );
			}
			else
			{
				pChild->StopEmission( bInfiniteOnly, bRemoveAllParticles, bWakeOnStop, bPlayEndCap );
			}
		}
	}
	else
	{
		for( CParticleCollection *pChild = m_Children.m_pHead; pChild != NULL; pChild = pChild->m_pNext )
		{
			pChild->StopEmission( bInfiniteOnly, bRemoveAllParticles );
		}
	}
}


//-----------------------------------------------------------------------------
// Start emitting particles
//-----------------------------------------------------------------------------
void CParticleCollection::StartEmission( bool bInfiniteOnly )
{
	if ( !m_pDef )
		return;

	m_bEmissionStopped = false;
	for( int i = 0; i < m_pDef->m_Emitters.Count(); i++ )
	{
		m_pDef->m_Emitters[i]->StartEmission( this, m_pOperatorContextData + m_pDef->m_nEmittersCtxOffsets[i], bInfiniteOnly );
	}

	// Start our children as well (end cap children only play when emission stops)
	CParticleCollection *pChild = m_Children.m_pHead;
	for( int i = 0; pChild != NULL; pChild = pChild->m_pNext, ++i )
	{
		if ( !m_pDef->m_Children[i].m_bEndCap )
		{
			pChild->StartEmission( bInfiniteOnly );
		}
	}

	// stay awake so we get simulated again
	m_flNextSleepTime = g_pParticleSystemMgr->GetLastSimulationTime() + m_pDef->m_flNoDrawTimeToGoToSleep;
}


//-----------------------------------------------------------------------------
// Dormancy
//-----------------------------------------------------------------------------
void CParticleCollection::SetDormant( bool bDormant )
{
	if ( bDormant == m_bDormant )
		return;

	// a system whose emission was stopped is dying; it doesn't go dormant
	if ( m_bEmissionStopped && bDormant )
		return;

	if ( bDormant )
	{
		StopEmission();
		m_bQueuedStartEmission = false;
	}
	else
	{
		// emission restarts on the next simulation
		m_bQueuedStartEmission = true;
		m_flNextSleepTime = g_pParticleSystemMgr->GetLastSimulationTime() + m_pDef->m_flNoDrawTimeToGoToSleep;
	}

	m_bDormant = bDormant;
}

bool CParticleCollection::IsEmitting() const
{
	return !m_bEmissionStopped;
}


//-----------------------------------------------------------------------------
// Copies one particle's attributes over another's
//-----------------------------------------------------------------------------
void CParticleAttributeAddressTable::CopyParticleAttributes( int nSrcIndex, int nDestIndex ) const
{
	for( int p = 0; p < ARRAYSIZE( m_pAttributes ); p++ )
	{
		switch( m_nFloatStrides[p] )
		{
			case 4:										// a float
				m_pAttributes[p][nDestIndex] = m_pAttributes[p][nSrcIndex];
				break;

			case 12:									// a vec3, stored as 3 SIMD lanes
			{
				int oldidxsse = 12 * ( nDestIndex >> 2 );
				int oldofs = oldidxsse + ( nDestIndex & 3 );
				int lastidxsse = 12 * ( nSrcIndex >> 2 );
				int lastofs = lastidxsse + ( nSrcIndex & 3 );

				m_pAttributes[p][oldofs] = m_pAttributes[p][lastofs];
				m_pAttributes[p][4 + oldofs] = m_pAttributes[p][4 + lastofs];
				m_pAttributes[p][8 + oldofs] = m_pAttributes[p][8 + lastofs];
				break;
			}
		}
	}
}

void CParticleCollection::MoveParticle( int nInitialIndex, int nNewIndex )
{
	m_ParticleAttributes.CopyParticleAttributes( nInitialIndex, nNewIndex );
	m_ParticleInitialAttributes.CopyParticleAttributes( nInitialIndex, nNewIndex );
	if ( m_pPreviousAttributeMemory )
	{
		m_PreviousFrameAttributes.CopyParticleAttributes( nInitialIndex, nNewIndex );
	}
}


//-----------------------------------------------------------------------------
// Kill lists: a pool of buffers handed to collections while they simulate
//-----------------------------------------------------------------------------
#define MAX_SIMULTANEOUS_KILL_LISTS 16

static volatile int g_nKillBufferInUse[MAX_SIMULTANEOUS_KILL_LISTS];
static KillListItem_t *g_pKillBuffers[MAX_SIMULTANEOUS_KILL_LISTS];

void CParticleSystemMgr::DetachKillList( CParticleCollection *pParticles )
{
	if ( pParticles->m_pParticleKillList )
	{
		for( int i = 0; i < ARRAYSIZE( g_pKillBuffers ); i++ )
		{
			if ( g_pKillBuffers[i] == pParticles->m_pParticleKillList )
			{
				pParticles->m_pParticleKillList = NULL;
				g_nKillBufferInUse[i] = 0;				// releasing doesn't need an interlocked op
				return;
			}
		}
		Assert( 0 );									// not one of ours
	}
}

void CParticleSystemMgr::AttachKillList( CParticleCollection *pParticles )
{
	for(;;)
	{
		for( int i = 0; i < ARRAYSIZE( g_nKillBufferInUse ); i++ )
		{
			if ( ! g_nKillBufferInUse[i] )				// looks free
			{
				if ( ThreadInterlockedAssignIf( &( g_nKillBufferInUse[i] ), 1, 0 ) )
				{
					if ( ! g_pKillBuffers[i] )
					{
						g_pKillBuffers[i] = new KillListItem_t[MAX_PARTICLES_IN_A_SYSTEM];
					}
					pParticles->m_pParticleKillList = g_pKillBuffers[i];
					return;
				}
			}
		}
		Assert( 0 );									// all buffers busy
		ThreadSleep();
	}
}


//-----------------------------------------------------------------------------
// Removes the particles on the kill list
//-----------------------------------------------------------------------------
void CParticleCollection::ApplyKillList( void )
{
	// drop entries past the end of the active particles
	const KillListItem_t *pCurKillListSlot = m_pParticleKillList;
	while( m_nNumParticlesToKill && pCurKillListSlot[ m_nNumParticlesToKill - 1 ].nIndex >= (uint)m_nActiveParticles )
	{
		m_nNumParticlesToKill--;
	}
	Assert( m_nNumParticlesToKill <= m_nActiveParticles );

	if ( m_nNumParticlesToKill == 0 )
		return;

	// children that want the about-to-die particles get to emit/initialize from them first
	for( CParticleCollection *pChild = m_Children.m_pHead; pChild != NULL; pChild = pChild->m_pNext )
	{
		EmitAndInit( pChild, true );
	}

	uint nParticlesActiveNow = m_nActiveParticles;
	int nLeftInKillList = m_nNumParticlesToKill;
	if ( nLeftInKillList == m_nActiveParticles )
	{
		nParticlesActiveNow = 0;						// everything dies
	}
	else if ( IsOrderImportant() )
	{
		// compact in place, preserving order; the list is sorted, so add a sentinel at the end
		m_pParticleKillList[nLeftInKillList].nIndex = m_nActiveParticles;
		for( int nKilled = 0; nKilled < nLeftInKillList; )
		{
			int nWriteIndex = m_pParticleKillList[nKilled].nIndex - nKilled;
			nKilled++;
			int nNextIndexToKill = m_pParticleKillList[nKilled].nIndex - nKilled;
			for( ; nWriteIndex < nNextIndexToKill; nWriteIndex++ )
			{
				MoveParticle( ( nWriteIndex + nKilled ), nWriteIndex );
			}
		}
		nParticlesActiveNow -= nLeftInKillList;
	}
	else
	{
		while( nLeftInKillList )
		{
			int nKillIndex = ( pCurKillListSlot++ )->nIndex;
			nLeftInKillList--;

			// trim particles off the end that are themselves on the kill list
			while ( nLeftInKillList && ( pCurKillListSlot[ nLeftInKillList - 1 ].nIndex == nParticlesActiveNow - 1 ) )
			{
				nLeftInKillList--;
				nParticlesActiveNow--;
			}

			if ( nKillIndex == nParticlesActiveNow - 1 )
			{
				// it was the last particle
				nParticlesActiveNow--;
				break;
			}

			// fill the hole with the last particle
			MoveParticle( nParticlesActiveNow - 1, nKillIndex );
			nParticlesActiveNow--;
		}
	}

	SetNActiveParticles( nParticlesActiveNow );
	m_nNumParticlesToKill = 0;
}


//-----------------------------------------------------------------------------
// Start, mid and end points of a path between two control points
//-----------------------------------------------------------------------------
void CParticleCollection::CalculatePathValues( CPathParameters const &PathIn,
	float flTimeStamp, Vector *pStartPnt, Vector *pMidPnt, Vector *pEndPnt )
{
	Vector StartPnt;
	GetControlPointAtTime( PathIn.m_nStartControlPointNumber, flTimeStamp, &StartPnt );
	Vector EndPnt;
	GetControlPointAtTime( PathIn.m_nEndControlPointNumber, flTimeStamp, &EndPnt );

	Vector MidP;
	VectorLerp( StartPnt, EndPnt, PathIn.m_flMidPoint, MidP );
	if ( PathIn.m_nBulgeControl )
	{
		// bulge along the forward vector of the start (1) or end (2) control point,
		// scaled down when that vector lines up with the path
		float flBulgeScale = 0.0;
		int nCP = PathIn.m_nStartControlPointNumber;
		if ( PathIn.m_nBulgeControl == 2 )
			nCP = PathIn.m_nEndControlPointNumber;
		Vector Fwd = ControlPoint( nCP ).m_ForwardVector;
		Vector vTarget = EndPnt - StartPnt;
		float flTotalDistance = vTarget.Length();
		if ( flTotalDistance > 1.0e-6 )
		{
			vTarget /= flTotalDistance;
			flBulgeScale = 1.0f - fabs( DotProduct( vTarget, Fwd ) );
		}
		Vector Potential_MidP = Fwd;
		float flOffsetDist = Potential_MidP.Length();
		if ( flOffsetDist > 1.0e-6 )
		{
			Potential_MidP *= ( PathIn.m_flBulge * flTotalDistance * flBulgeScale ) / flOffsetDist;
			MidP += Potential_MidP;
		}
	}
	else
	{
		Vector RndVector;
		RandomVector( 0, -PathIn.m_flBulge, PathIn.m_flBulge, &RndVector );
		MidP += RndVector;
	}

	*pStartPnt = StartPnt;
	*pMidPnt = MidP;
	*pEndPnt = EndPnt;
}


//-----------------------------------------------------------------------------
// Query used when the application doesn't supply one
//-----------------------------------------------------------------------------
class CDefaultParticleSystemQuery : public CBaseAppSystem< IParticleSystemQuery >
{
public:
	virtual bool IsEditor( ) { return false; }

	virtual void GetLightingAtPoint( const Vector& vecOrigin, Color &tint )
	{
		tint.SetColor( 255, 255, 255, 255 );
	}

	virtual void TraceLine( const Vector& vecAbsStart,
		const Vector& vecAbsEnd, unsigned int mask, 
		const class IHandleEntity *ignore,
		int collisionGroup, CBaseTrace *ptr )
	{
		ptr->fraction = 1.0;								// no hit
	}

	virtual void GetRandomPointsOnControllingObjectHitBox( 
		CParticleCollection *pParticles,
		int nControlPointNumber, 
		int nNumPtsOut,
		float flBBoxScale,
		int nNumTrysToGetAPointInsideTheModel,
		Vector *pPntsOut,
		Vector vecDirectionBias,
		Vector *pHitBoxRelativeCoordOut, int *pHitBoxIndexOut, int nDesiredHitbox )
	{
		for( int i = 0; i < nNumPtsOut; i++ )
		{
			pPntsOut[i].Init();
		}
	}

	virtual void TraceAgainstRayTraceEnv( int envnumber, const FourRays &rays, fltx4 TMin, fltx4 TMax,
		RayTracingResult *rslt_out, int32 skip_id ) const
	{
		rslt_out->HitDistance = Four_Ones;
		rslt_out->surface_normal.DuplicateVector( vec3_origin );
	}

	virtual Vector GetCurrentViewOrigin()
	{
		return vec3_origin;
	}

	virtual int GetActivityCount() { return 0; }

	virtual const char *GetActivityNameFromIndex( int nActivityIndex ) { return 0; }

	virtual float GetPixelVisibility( int *pQueryHandle, const Vector &vecOrigin, float flScale ) { return 0.0f; }

	virtual void PreSimulate( ) { }

	virtual void PostSimulate( ) { }

	virtual void DebugDrawLine( const Vector& origin, const Vector& target, int r, int g, int b, bool noDepthTest, float duration )
	{
	}

	virtual void DrawModel( void *pModel, const matrix3x4_t &DrawMatrix, CParticleCollection *pParticles, int nParticleNumber, int nBodyPart, int nSubModel,
		int nAnimationSequence = 0, float flAnimationRate = 30.0f, float r = 1.0f, float g = 1.0f, float b = 1.0f, float a = 1.0f ) { }

	virtual void UpdateProjectedTexture( const int nParticleID, IMaterial *pMaterial, Vector &vOrigin, float flRadius, float flRotation, float r, float g, float b, float a, void *&pUserVar ) { }
};

static CDefaultParticleSystemQuery s_DefaultParticleSystemQuery;


//-----------------------------------------------------------------------------
//
// Particle system manager
//
//-----------------------------------------------------------------------------

CParticleSystemMgr::CParticleSystemMgr()
{
	m_pQuery = &s_DefaultParticleSystemQuery;
	m_bDidInit = false;
	m_bUsingDefaultQuery = true;
	m_bShouldLoadSheets = true;
	m_bAllowPrecache = true;
	m_pParticleSystemDictionary = NULL;
	m_nNumFramesMeasured = 0;
	m_flLastSimulationTime = 0.0f;
	m_flLastSimulationDuration = 0.0f;
	InitAttributeTable();
}

CParticleSystemMgr::~CParticleSystemMgr()
{
	FlushAllSheets();
	if ( m_pParticleSystemDictionary )
	{
		delete m_pParticleSystemDictionary;
		m_pParticleSystemDictionary = NULL;
	}
}


//-----------------------------------------------------------------------------
// Initialize the particle system
//-----------------------------------------------------------------------------
bool CParticleSystemMgr::Init( IParticleSystemQuery *pQuery, bool bAllowPrecache )
{
	if ( g_pMaterialSystem && ( !g_pMaterialSystem->QueryInterface( MATERIAL_SYSTEM_INTERFACE_VERSION ) ) )
	{
		Msg( "CParticleSystemMgr compiled using an old IMaterialSystem\n" );
		return false;
	}

	if ( m_bUsingDefaultQuery && pQuery )
	{
		m_pQuery = pQuery;
		m_bUsingDefaultQuery = false;
	}

	if ( !m_bDidInit )
	{
		m_pParticleSystemDictionary = new CParticleSystemDictionary;

		// the stand-in definition for children (used by editors)
		AddParticleOperator( FUNCTION_CHILDREN, &s_ChildOperatorDefinition );

		m_pShadowDepthMaterial = NULL;
		if ( g_pMaterialSystem )
		{
			KeyValues *pVMTKeyValues = new KeyValues( "DepthWrite" );
			pVMTKeyValues->SetInt( "$no_fullbright", 1 );
			pVMTKeyValues->SetInt( "$model", 0 );
			pVMTKeyValues->SetInt( "$alphatest", 0 );
			m_pShadowDepthMaterial = g_pMaterialSystem->CreateMaterial( "__particlesDepthWrite", pVMTKeyValues );
		}
		SeedRandSIMD( 12345678 );
		m_bDidInit = true;
	}

	m_bAllowPrecache = bAllowPrecache;

	return true;
}


//-----------------------------------------------------------------------------
// Attribute names and data types
//-----------------------------------------------------------------------------
CParticleSystemMgr::ParticleAttribute_t CParticleSystemMgr::s_AttributeTable[MAX_PARTICLE_ATTRIBUTES];

#define ADD_ATTRIBUTE( _attr, _type )															\
	COMPILE_TIME_ASSERT( PARTICLE_ATTRIBUTE_##_attr >= 0 &&										\
						 PARTICLE_ATTRIBUTE_##_attr < MAX_PARTICLE_ATTRIBUTES );				\
	s_AttributeTable[ PARTICLE_ATTRIBUTE_##_attr ].nDataType = _type;							\
	s_AttributeTable[ PARTICLE_ATTRIBUTE_##_attr ].pName = #_attr;

void CParticleSystemMgr::InitAttributeTable( void )
{
	memset( s_AttributeTable, 0, sizeof( s_AttributeTable ) );

	ADD_ATTRIBUTE( XYZ,						ATTRDATATYPE_4V );
	ADD_ATTRIBUTE( LIFE_DURATION,			ATTRDATATYPE_FLOAT );
	ADD_ATTRIBUTE( PREV_XYZ,				ATTRDATATYPE_4V );
	ADD_ATTRIBUTE( RADIUS,					ATTRDATATYPE_FLOAT );
	ADD_ATTRIBUTE( ROTATION,				ATTRDATATYPE_FLOAT );
	ADD_ATTRIBUTE( ROTATION_SPEED,			ATTRDATATYPE_FLOAT );
	ADD_ATTRIBUTE( TINT_RGB,				ATTRDATATYPE_4V );
	ADD_ATTRIBUTE( ALPHA,					ATTRDATATYPE_FLOAT );
	ADD_ATTRIBUTE( CREATION_TIME,			ATTRDATATYPE_FLOAT );
	ADD_ATTRIBUTE( SEQUENCE_NUMBER,			ATTRDATATYPE_FLOAT );
	ADD_ATTRIBUTE( TRAIL_LENGTH,			ATTRDATATYPE_FLOAT );
	ADD_ATTRIBUTE( PARTICLE_ID,				ATTRDATATYPE_INT );
	ADD_ATTRIBUTE( YAW,						ATTRDATATYPE_FLOAT );
	ADD_ATTRIBUTE( SEQUENCE_NUMBER1,		ATTRDATATYPE_FLOAT );
	ADD_ATTRIBUTE( HITBOX_INDEX,			ATTRDATATYPE_INT );
	ADD_ATTRIBUTE( HITBOX_RELATIVE_XYZ,		ATTRDATATYPE_4V );
	ADD_ATTRIBUTE( ALPHA2,					ATTRDATATYPE_FLOAT );
	ADD_ATTRIBUTE( TRACE_P0,				ATTRDATATYPE_4V );
	ADD_ATTRIBUTE( TRACE_P1,				ATTRDATATYPE_4V );
	ADD_ATTRIBUTE( TRACE_HIT_T,				ATTRDATATYPE_FLOAT );
	ADD_ATTRIBUTE( TRACE_HIT_NORMAL,		ATTRDATATYPE_4V );
	ADD_ATTRIBUTE( NORMAL,					ATTRDATATYPE_4V );
	ADD_ATTRIBUTE( GLOW_RGB,				ATTRDATATYPE_4V );
	ADD_ATTRIBUTE( GLOW_ALPHA,				ATTRDATATYPE_FLOAT );

	for ( int i = 0; i < MAX_PARTICLE_ATTRIBUTES; i++ )
	{
		if ( !s_AttributeTable[i].pName )
		{
			Warning( "CParticleSystemMgr::InitAttributeTable has an out-of-date attribute list! (element %d not set up)\n", i );
			//Assert( 0 );
		}
	}
}

int CParticleSystemMgr::GetParticleAttributeByName( const char *pName ) const
{
	// SLOW! linear search
	for ( int i = 0; i < MAX_PARTICLE_ATTRIBUTES; i++ )
	{
		if ( !Q_stricmp( pName, s_AttributeTable[i].pName ) )
			return i;
	}
	return -1;
}

const char *CParticleSystemMgr::GetParticleAttributeName( int nAttribute ) const
{
	if ( ( nAttribute < 0 ) || ( nAttribute >= MAX_PARTICLE_ATTRIBUTES ) )
	{
		Assert( 0 );
		return "unknown";
	}
	return s_AttributeTable[nAttribute].pName;
}

EAttributeDataType CParticleSystemMgr::GetParticleAttributeDataType( int nAttribute ) const
{
	Assert( ( nAttribute >= 0 ) && ( nAttribute < MAX_PARTICLE_ATTRIBUTES ) );
	return s_AttributeTable[nAttribute].nDataType;
}


//-----------------------------------------------------------------------------
// Precache lookup tables. Positive string numbers index the server table,
// negative ones (encoded as -n-1) index the client table.
//-----------------------------------------------------------------------------
void CParticleSystemMgr::PrecacheParticleSystem( int nStringNumber, const char *pName )
{
	if ( !pName || !pName[0] )
		return;

	ParticleSystemHandle_t hParticleSystem = m_pParticleSystemDictionary->FindParticleSystemHandle( pName );

	if ( hParticleSystem == UTL_INVAL_SYMBOL )
	{
		Warning( "Attempted to precache unknown particle system \"%s\"!\n", pName );
		hParticleSystem = m_pParticleSystemDictionary->FindParticleSystemHandle( "error" );
	}

	CParticleSystemDefinition *pDef = m_pParticleSystemDictionary->FindParticleSystem( hParticleSystem );

	CUtlVector< ParticleSystemHandle_t > &lookup = ( nStringNumber >= 0 ) ? m_PrecacheLookup : m_ClientPrecacheLookup;
	if ( nStringNumber < 0 )
	{
		nStringNumber = - nStringNumber - 1;
	}

	int nCountToAdd = nStringNumber + 1 - lookup.Count();
	for ( int i = 0; i < nCountToAdd; ++i )
	{
		lookup.AddToTail( UTL_INVAL_SYMBOL );
	}
	lookup[ nStringNumber ] = hParticleSystem;

	if ( !pDef )
	{
		Warning( "Attempted to precache unknown particle system \"%s\"!\n", pName );
		return;
	}

	pDef->Precache();
}

void CParticleSystemMgr::LevelShutdown( void )
{
}

void CParticleSystemMgr::UncacheAllParticleSystems()
{
	m_PrecacheLookup.RemoveAll();
	m_ClientPrecacheLookup.RemoveAll();

	if ( m_pParticleSystemDictionary )
	{
		int nCount = m_pParticleSystemDictionary->Count();
		for ( int i = 0; i < nCount; ++i )
		{
			m_pParticleSystemDictionary->GetParticleSystem( i )->Uncache();
		}

		nCount = m_pParticleSystemDictionary->NameCount();
		for ( ParticleSystemHandle_t h = 0; h < nCount; ++h )
		{
			m_pParticleSystemDictionary->FindParticleSystem( h )->Uncache();
		}
	}

	// Sheets are owned by the manager and keyed off the materials, drop them too
	FlushAllSheets();
}


//-----------------------------------------------------------------------------
// Display names for the particle attribute fields (NULL = not user editable)
//-----------------------------------------------------------------------------
static const char *s_pParticleFieldNames[MAX_PARTICLE_ATTRIBUTES] =
{
	"Position",				// XYZ
	"Life Duration",		// LIFE_DURATION
	"Position Previous",	// PREV_XYZ
	"Radius",				// RADIUS
	"Roll",					// ROTATION
	"Roll Speed",			// ROTATION_SPEED
	"Color",				// TINT_RGB
	"Alpha",				// ALPHA
	"Creation Time",		// CREATION_TIME
	"Sequence Number",		// SEQUENCE_NUMBER
	"Trail Length",			// TRAIL_LENGTH
	"Particle ID",			// PARTICLE_ID
	"Yaw",					// YAW
	"Sequence Number 1",	// SEQUENCE_NUMBER1
	"Hitbox Index",			// HITBOX_INDEX
	NULL,					// HITBOX_RELATIVE_XYZ
	"Alpha Alternate",		// ALPHA2
	NULL,					// TRACE_P0
	NULL,					// TRACE_P1
	NULL,					// TRACE_HIT_T
	NULL,					// TRACE_HIT_NORMAL
	"Normal",				// NORMAL
	"Glow RGB",				// GLOW_RGB
	"Glow Alpha",			// GLOW_ALPHA
};

const char* CParticleSystemMgr::GetParticleFieldName( int nParticleField ) const
{
	return s_pParticleFieldNames[nParticleField];
}


//-----------------------------------------------------------------------------
// Operator registration
//-----------------------------------------------------------------------------
void CParticleSystemMgr::AddParticleOperator( ParticleFunctionType_t nOpType, IParticleOperatorDefinition *pOpFactory )
{
	m_ParticleOperators[nOpType].AddToTail( pOpFactory );
}

static const char *s_pFilterNames[FILTER_COUNT] =
{
	"All",
	"Position and Velocity",
	"Life Duration",
	"Parameter Remapping",
	"Rotation",
	"Size",
	"Color and Opacity",
	"Animation Sequence",
	"Hitbox",
	"Normal",
	"Control Points",
};

const char *CParticleSystemMgr::GetFilterName( ParticleFilterType_t nFilterType ) const
{
	COMPILE_TIME_ASSERT( ARRAYSIZE( s_pFilterNames ) == FILTER_COUNT );
	return s_pFilterNames[nFilterType];
}

CUtlVector< IParticleOperatorDefinition *> &CParticleSystemMgr::GetAvailableParticleOperatorList( ParticleFunctionType_t nWhichList )
{
	return m_ParticleOperators[nWhichList];
}

const DmxElementUnpackStructure_t *CParticleSystemMgr::GetParticleSystemDefinitionUnpackStructure()
{
	return s_pParticleSystemDefinitionUnpack;
}

void CParticleOperatorInstance::operator delete( void *pData )
{
	if ( pData )
	{
		MemAlloc_FreeAligned( pData );
	}
}

void CParticleOperatorInstance::operator delete( void* pData, int nBlockUse, const char *pFileName, int nLine )
{
	if ( pData )
	{
		MemAlloc_FreeAligned( pData );
	}
}


//-----------------------------------------------------------------------------
// Lists the particle systems contained in a file or buffer (no loading)
//-----------------------------------------------------------------------------
void CParticleSystemMgr::GetParticleSystemsInFile( const char *pFileName, CUtlVector<CUtlString> *pOutSystemNameList )
{
	if ( !pOutSystemNameList )
		return;

	pOutSystemNameList->RemoveAll();

	CUtlBuffer buf;
	if ( !g_pFullFileSystem->ReadFile( pFileName, "GAME", buf ) )
		return;

	GetParticleSystemsInBuffer( buf, pOutSystemNameList );
}

void CParticleSystemMgr::GetParticleSystemsInBuffer( CUtlBuffer &buf, CUtlVector<CUtlString> *pOutSystemNameList )
{
	if ( !pOutSystemNameList )
		return;

	pOutSystemNameList->RemoveAll();

	DECLARE_DMX_CONTEXT();

	CDmxElement *pRoot;
	if ( !UnserializeDMX( buf, &pRoot ) )
		return;

	if ( !Q_stricmp( pRoot->GetTypeString(), "DmeParticleSystemDefinition" ) )
	{
		pOutSystemNameList->AddToTail( pRoot->GetName() );
		CleanupDMX( pRoot );
		return;
	}

	const CDmxAttribute *pDefinitions = pRoot->GetAttribute( "particleSystemDefinitions" );
	if ( !pDefinitions || pDefinitions->GetType() != AT_ELEMENT_ARRAY )
	{
		CleanupDMX( pRoot );
		return;
	}

	const CUtlVector< CDmxElement* >& definitions = pDefinitions->GetArray<CDmxElement*>( );
	int nCount = definitions.Count();
	for ( int i = 0; i < nCount; ++i )
	{
		pOutSystemNameList->AddToTail( definitions[i]->GetName() );
	}

	CleanupDMX( pRoot );
}


//-----------------------------------------------------------------------------
// Srcbox: append-only mode ( see SetParticleSkipExistingDefinitions )
//-----------------------------------------------------------------------------
static bool s_bParticleSkipExistingDefinitions = false;

void SetParticleSkipExistingDefinitions( bool bSkip )
{
	s_bParticleSkipExistingDefinitions = bSkip;
}

bool GetParticleSkipExistingDefinitions()
{
	return s_bParticleSkipExistingDefinitions;
}


//-----------------------------------------------------------------------------
// Srcbox: converts newer binary DMX encodings ( binary 3 - 5: L4D2, Alien Swarm, Portal 2,
// CS:GO pcfs ) to binary 2, the newest encoding the 2013 dmxloader reads. Done in memory so
// particle files from those games load without converting them first.
//
//   binary 2: string table count = short, indices = short, element names and string values inline
//   binary 3: same layout as binary 2
//   binary 4: string table count = int, element names and string values in the string table
//   binary 5: as binary 4 with int string table indices
//
// Those games' attribute type 7 is a time ( int, 1/10000 s ) where 2013 has a 16 byte object id,
// so times are written as floats in seconds.
//-----------------------------------------------------------------------------
namespace
{
	class CDmxUpgradeReader
	{
	public:
		CDmxUpgradeReader( const unsigned char *pData, int nSize ) : m_pData( pData ), m_nSize( nSize ), m_nPos( 0 ), m_bOk( true ) {}

		bool Has( int n )
		{
			if ( n < 0 || m_nPos + n > m_nSize )
				m_bOk = false;
			return m_bOk;
		}
		int Int()
		{
			int v = 0;
			if ( Has( 4 ) ) { memcpy( &v, m_pData + m_nPos, 4 ); m_nPos += 4; }
			return v;
		}
		int Short()
		{
			short v = 0;
			if ( Has( 2 ) ) { memcpy( &v, m_pData + m_nPos, 2 ); m_nPos += 2; }
			return v;
		}
		int Byte()
		{
			return Has( 1 ) ? m_pData[ m_nPos++ ] : 0;
		}
		const unsigned char *Bytes( int n )
		{
			if ( !Has( n ) )
				return NULL;
			const unsigned char *p = m_pData + m_nPos;
			m_nPos += n;
			return p;
		}
		const char *String()
		{
			int nEnd = m_nPos;
			while ( nEnd < m_nSize && m_pData[ nEnd ] )
				++nEnd;
			if ( nEnd >= m_nSize )
			{
				m_bOk = false;
				return "";
			}
			const char *p = (const char *)( m_pData + m_nPos );
			m_nPos = nEnd + 1;
			return p;
		}

		const unsigned char *m_pData;
		int m_nSize;
		int m_nPos;
		bool m_bOk;
	};

	enum
	{
		DMXUP_AT_ELEMENT = 1,
		DMXUP_AT_INT,
		DMXUP_AT_FLOAT,
		DMXUP_AT_BOOL,
		DMXUP_AT_STRING,
		DMXUP_AT_VOID,
		DMXUP_AT_TIME,		// 2013: AT_OBJECTID
		DMXUP_AT_COLOR,
		DMXUP_AT_VECTOR2,
		DMXUP_AT_VECTOR3,
		DMXUP_AT_VECTOR4,
		DMXUP_AT_QANGLE,
		DMXUP_AT_QUATERNION,
		DMXUP_AT_VMATRIX,
		DMXUP_AT_FIRST_ARRAY,	// 15
		DMXUP_AT_LAST_ARRAY = DMXUP_AT_FIRST_ARRAY + DMXUP_AT_VMATRIX - 1,
	};

	int DmxUpgradeFixedSize( int nType )
	{
		switch ( nType )
		{
		case DMXUP_AT_INT:
		case DMXUP_AT_FLOAT:
		case DMXUP_AT_COLOR:		return 4;
		case DMXUP_AT_BOOL:			return 1;
		case DMXUP_AT_VECTOR2:		return 8;
		case DMXUP_AT_VECTOR3:
		case DMXUP_AT_QANGLE:		return 12;
		case DMXUP_AT_VECTOR4:
		case DMXUP_AT_QUATERNION:	return 16;
		case DMXUP_AT_VMATRIX:		return 64;
		}
		return -1;
	}

	void DmxUpgradePutString( CUtlBuffer &out, const char *pString )
	{
		out.Put( pString, V_strlen( pString ) + 1 );
	}

	// Copies one value of nType ( not an array type ), converting it
	bool DmxUpgradeCopyValue( CDmxUpgradeReader &in, CUtlBuffer &out, int nType, int nEncoding, const CUtlVector< const char * > &strings )
	{
		switch ( nType )
		{
		case DMXUP_AT_ELEMENT:
			{
				int nIndex = in.Int();
				out.PutInt( nIndex );
				if ( nIndex == -2 )	// external element, followed by its id as a string
					DmxUpgradePutString( out, in.String() );
				return in.m_bOk;
			}

		case DMXUP_AT_STRING:
			{
				const char *pString;
				if ( nEncoding >= 4 )
				{
					int nIndex = ( nEncoding >= 5 ) ? in.Int() : in.Short();
					if ( nIndex < 0 || nIndex >= strings.Count() )
						return false;
					pString = strings[ nIndex ];
				}
				else
				{
					pString = in.String();
				}
				DmxUpgradePutString( out, pString );
				return in.m_bOk;
			}

		case DMXUP_AT_VOID:
			{
				int nLen = in.Int();
				const unsigned char *p = in.Bytes( nLen );
				if ( !p )
					return false;
				out.PutInt( nLen );
				out.Put( p, nLen );
				return true;
			}

		case DMXUP_AT_TIME:
			out.PutFloat( in.Int() / 10000.0f );
			return in.m_bOk;
		}

		int nSize = DmxUpgradeFixedSize( nType );
		const unsigned char *p = ( nSize > 0 ) ? in.Bytes( nSize ) : NULL;
		if ( !p )
			return false;
		out.Put( p, nSize );
		return true;
	}

	// String array values are written inline in every encoding ( only single strings use the table )
	bool DmxUpgradeCopyArrayValue( CDmxUpgradeReader &in, CUtlBuffer &out, int nType, int nEncoding, const CUtlVector< const char * > &strings )
	{
		if ( nType == DMXUP_AT_STRING )
		{
			DmxUpgradePutString( out, in.String() );
			return in.m_bOk;
		}
		return DmxUpgradeCopyValue( in, out, nType, nEncoding, strings );
	}
}

// Returns true and fills 'out' when buf holds a binary 3 - 5 DMX that was converted to binary 2
static bool UpgradeParticleDMXEncoding( CUtlBuffer &buf, CUtlBuffer &out, const char *pFileName )
{
	const unsigned char *pData = (const unsigned char *)buf.PeekGet();
	int nSize = buf.GetBytesRemaining();
	static const char s_szPrefix[] = "<!-- dmx encoding binary ";
	const int nPrefixLen = sizeof( s_szPrefix ) - 1;
	if ( !pData || nSize < nPrefixLen + 8 || V_strncmp( (const char *)pData, s_szPrefix, nPrefixLen ) )
		return false;

	// Header line: <!-- dmx encoding binary %d format %s %d -->\n ( then usually a terminating 0 )
	int nHeaderEnd = 0;
	while ( nHeaderEnd < nSize && nHeaderEnd < 512 && pData[ nHeaderEnd ] != '\n' )
		++nHeaderEnd;
	if ( nHeaderEnd >= nSize || nHeaderEnd >= 512 )
		return false;

	char szHeader[ 512 ];
	V_strncpy( szHeader, (const char *)pData, MIN( nHeaderEnd + 1, (int)sizeof( szHeader ) ) );

	int nEncoding = 0, nFormatVersion = 0;
	char szFormat[ 64 ] = { 0 };
	if ( sscanf( szHeader, "<!-- dmx encoding binary %d format %63s %d", &nEncoding, szFormat, &nFormatVersion ) < 1 )
		return false;
	if ( nEncoding < 3 || nEncoding > 5 )
		return false;
	if ( !szFormat[ 0 ] )
		V_strncpy( szFormat, "pcf", sizeof( szFormat ) );

	int nStart = nHeaderEnd + 1;
	if ( nStart < nSize && pData[ nStart ] == 0 )
		++nStart;

	CDmxUpgradeReader in( pData + nStart, nSize - nStart );

	// String table
	int nStrings = ( nEncoding >= 4 ) ? in.Int() : in.Short();
	if ( !in.m_bOk || nStrings < 0 || nStrings > 32767 )
	{
		Warning( "Particles: '%s' ( dmx binary %d ) has %d strings, too many to convert to binary 2\n", pFileName ? pFileName : "?", nEncoding, nStrings );
		return false;
	}
	CUtlVector< const char * > strings;
	strings.EnsureCapacity( nStrings );
	for ( int i = 0; i < nStrings && in.m_bOk; ++i )
		strings.AddToTail( in.String() );

#define DMXUP_STRING_INDEX() ( ( nEncoding >= 5 ) ? in.Int() : in.Short() )
#define DMXUP_FAIL() { Warning( "Particles: failed converting '%s' from dmx binary %d at offset %d\n", pFileName ? pFileName : "?", nEncoding, nStart + in.m_nPos ); return false; }

	out.Purge();
	out.SetBigEndian( false );
	char szNewHeader[ 256 ];
	// Format version 1: the 2013 particle code was written for pcf 1 and reads pcf 2's attributes the same way
	V_snprintf( szNewHeader, sizeof( szNewHeader ), "<!-- dmx encoding binary 2 format %s %d -->\n", szFormat, !V_stricmp( szFormat, "pcf" ) ? 1 : nFormatVersion );
	DmxUpgradePutString( out, szNewHeader );

	out.PutShort( (short)nStrings );
	for ( int i = 0; i < nStrings; ++i )
		DmxUpgradePutString( out, strings[ i ] );

	// Elements: type, name, id
	int nElements = in.Int();
	if ( !in.m_bOk || nElements < 0 )
		DMXUP_FAIL();
	out.PutInt( nElements );
	for ( int i = 0; i < nElements; ++i )
	{
		int nType = DMXUP_STRING_INDEX();
		if ( nType < 0 || nType >= nStrings )
			DMXUP_FAIL();
		out.PutShort( (short)nType );

		if ( nEncoding >= 4 )
		{
			int nName = DMXUP_STRING_INDEX();
			if ( nName < 0 || nName >= nStrings )
				DMXUP_FAIL();
			DmxUpgradePutString( out, strings[ nName ] );
		}
		else
		{
			DmxUpgradePutString( out, in.String() );
		}

		const unsigned char *pId = in.Bytes( 16 );
		if ( !pId )
			DMXUP_FAIL();
		out.Put( pId, 16 );
	}

	// Attributes
	for ( int i = 0; i < nElements; ++i )
	{
		int nAttributes = in.Int();
		if ( !in.m_bOk || nAttributes < 0 )
			DMXUP_FAIL();
		out.PutInt( nAttributes );

		for ( int a = 0; a < nAttributes; ++a )
		{
			int nName = DMXUP_STRING_INDEX();
			if ( nName < 0 || nName >= nStrings )
				DMXUP_FAIL();
			out.PutShort( (short)nName );

			int nType = in.Byte();
			if ( !in.m_bOk || nType < DMXUP_AT_ELEMENT || nType > DMXUP_AT_LAST_ARRAY )
				DMXUP_FAIL();

			if ( nType >= DMXUP_AT_FIRST_ARRAY )
			{
				int nValueType = nType - DMXUP_AT_FIRST_ARRAY + 1;
				int nOutValueType = ( nValueType == DMXUP_AT_TIME ) ? DMXUP_AT_FLOAT : nValueType;
				out.PutChar( (char)( nOutValueType + DMXUP_AT_FIRST_ARRAY - 1 ) );

				int nCount = in.Int();
				if ( !in.m_bOk || nCount < 0 )
					DMXUP_FAIL();
				out.PutInt( nCount );
				for ( int k = 0; k < nCount; ++k )
				{
					if ( !DmxUpgradeCopyArrayValue( in, out, nValueType, nEncoding, strings ) )
						DMXUP_FAIL();
				}
			}
			else
			{
				out.PutChar( (char)( ( nType == DMXUP_AT_TIME ) ? DMXUP_AT_FLOAT : nType ) );
				if ( !DmxUpgradeCopyValue( in, out, nType, nEncoding, strings ) )
					DMXUP_FAIL();
			}
		}
	}

#undef DMXUP_STRING_INDEX
#undef DMXUP_FAIL

	return in.m_bOk;
}


//-----------------------------------------------------------------------------
// Unserializes a DMX buffer holding one definition or a list of them
//-----------------------------------------------------------------------------
bool CParticleSystemMgr::ReadParticleDefinitions( CUtlBuffer &buf, const char *pFileName, bool bPrecache, bool bDecommitTempMemory )
{
	DECLARE_DMX_CONTEXT_DECOMMIT( bDecommitTempMemory );

	// Srcbox: newer binary encodings ( binary 3 - 5 ) are converted to binary 2 first
	CUtlBuffer upgradedBuf;
	CUtlBuffer *pReadBuf = &buf;
	if ( UpgradeParticleDMXEncoding( buf, upgradedBuf, pFileName ) )
	{
		pReadBuf = &upgradedBuf;
	}

	CDmxElement *pRoot;
	if ( !UnserializeDMX( *pReadBuf, &pRoot, pFileName ) )
	{
		Warning( "Unable to read particle definition %s! UtlBuffer is probably the wrong type!\n", pFileName );
		return false;
	}

	if ( !Q_stricmp( pRoot->GetTypeString(), "DmeParticleSystemDefinition" ) )
	{
		if ( s_bParticleSkipExistingDefinitions && m_pParticleSystemDictionary->FindParticleSystem( pRoot->GetName() ) )
		{
			CleanupDMX( pRoot );
			return true;
		}
		CParticleSystemDefinition *pDef = m_pParticleSystemDictionary->AddParticleSystem( pRoot );
		if ( pDef && bPrecache )
		{
			pDef->m_bAlwaysPrecache = true;
			if ( m_bAllowPrecache )
			{
				pDef->Precache();
			}
		}
		CleanupDMX( pRoot );
		return true;
	}

	const CDmxAttribute *pDefinitions = pRoot->GetAttribute( "particleSystemDefinitions" );
	if ( !pDefinitions || pDefinitions->GetType() != AT_ELEMENT_ARRAY )
	{
		CleanupDMX( pRoot );
		return false;
	}

	const CUtlVector< CDmxElement* >& definitions = pDefinitions->GetArray<CDmxElement*>( );
	int nCount = definitions.Count();
	for ( int i = 0; i < nCount; ++i )
	{
		// Srcbox: append-only mode keeps systems that are already defined
		if ( s_bParticleSkipExistingDefinitions && m_pParticleSystemDictionary->FindParticleSystem( definitions[i]->GetName() ) )
			continue;
		CParticleSystemDefinition *pDef = m_pParticleSystemDictionary->AddParticleSystem( definitions[i] );
		if ( pDef && bPrecache )
		{
			pDef->m_bAlwaysPrecache = true;
			if ( m_bAllowPrecache )
			{
				pDef->Precache();
			}
		}
	}

	CleanupDMX( pRoot );
	return true;
}


//-----------------------------------------------------------------------------
// Frees the scratch memory used while reading DMX
//-----------------------------------------------------------------------------
void CParticleSystemMgr::DecommitTempMemory()
{
	DecommitDMXMemory();
}


//-----------------------------------------------------------------------------
// Simulation timing and fallback configuration
//-----------------------------------------------------------------------------
void CParticleSystemMgr::SetLastSimulationTime( float flTime )
{
	m_flLastSimulationTime = flTime;
}

float CParticleSystemMgr::GetLastSimulationTime() const
{
	return m_flLastSimulationTime;
}

void CParticleSystemMgr::SetLastSimulationDuration( float flDuration )
{
	m_flLastSimulationDuration = flDuration;
}

float CParticleSystemMgr::GetLastSimulationDuration() const
{
	return m_flLastSimulationDuration;
}

void CParticleSystemMgr::SetSystemLevel( int nCPULevel, int nGPULevel )
{
	m_nCPULevel = nCPULevel;
	m_nGPULevel = nGPULevel;
}

int CParticleSystemMgr::GetParticleCPULevel() const
{
	return m_nCPULevel;
}

int CParticleSystemMgr::GetParticleGPULevel() const
{
	return m_nGPULevel;
}

void CParticleSystemMgr::SetFallbackParameters( float flBase, float flMultiplier, float flSimFallbackBaseMultiplier, float flSimThresholdMs )
{
	m_flFallbackBase = flBase;
	m_flFallbackMultiplier = flMultiplier;
	m_flSimFallbackBaseMultiplier = flSimFallbackBaseMultiplier;
	m_flSimThresholdMs = flSimThresholdMs;
}

float CParticleSystemMgr::GetFallbackBase() const
{
	return m_flFallbackBase;
}

float CParticleSystemMgr::GetFallbackMultiplier() const
{
	return m_flFallbackMultiplier;
}

float CParticleSystemMgr::GetSimFallbackThresholdMs() const
{
	return m_flSimThresholdMs;
}

float CParticleSystemMgr::GetSimFallbackBaseMultiplier() const
{
	return m_flSimFallbackBaseMultiplier;
}


//-----------------------------------------------------------------------------
// Dictionary access
//-----------------------------------------------------------------------------
void CParticleSystemMgr::AddParticleSystem( CDmxElement *pParticleSystem )
{
	m_pParticleSystemDictionary->AddParticleSystem( pParticleSystem );
}

CParticleSystemDefinition* CParticleSystemMgr::FindParticleSystem( const char *pName )
{
	return m_pParticleSystemDictionary->FindParticleSystem( pName );
}

CParticleSystemDefinition* CParticleSystemMgr::FindParticleSystem( const DmObjectId_t& id )
{
	return m_pParticleSystemDictionary->FindParticleSystem( id );
}

CParticleSystemDefinition* CParticleSystemMgr::FindParticleSystem( ParticleSystemHandle_t hParticleSystem )
{
	return m_pParticleSystemDictionary->FindParticleSystem( hParticleSystem );
}

CParticleSystemDefinition* CParticleSystemMgr::FindPrecachedParticleSystem( int nPrecacheIndex )
{
	CUtlVector< ParticleSystemHandle_t > &lookup = ( nPrecacheIndex >= 0 ) ? m_PrecacheLookup : m_ClientPrecacheLookup;
	if ( nPrecacheIndex < 0 )
	{
		nPrecacheIndex = - nPrecacheIndex - 1;
	}

	if ( nPrecacheIndex >= lookup.Count() )
		return NULL;
	return FindParticleSystem( lookup[nPrecacheIndex] );
}


//-----------------------------------------------------------------------------
// Reads a particle config file
//-----------------------------------------------------------------------------
bool CParticleSystemMgr::ReadParticleConfigFile( CUtlBuffer &buf, bool bPrecache, bool bDecommitTempMemory, const char *pFileName )
{
	return ReadParticleDefinitions( buf, pFileName, bPrecache, bDecommitTempMemory );
}

bool CParticleSystemMgr::ReadParticleConfigFile( const char *pFileName, bool bPrecache, bool bDecommitTempMemory )
{
	// A leading '!' forces the file's systems to always be precached
	if ( pFileName[0] == '!' )
	{
		bPrecache = true;
		++pFileName;
	}

	// The shipped build computes these but no longer uses them
	char pTemp[MAX_PATH];
	Q_StripExtension( pFileName, pTemp, sizeof(pTemp) );
	Q_GetFileExtension( pFileName );

	CUtlBuffer buf;
	if ( g_pFullFileSystem->ReadFile( pFileName, "GAME", buf ) )
	{
		return ReadParticleDefinitions( buf, pFileName, bPrecache, bDecommitTempMemory );
	}

	Warning( "Particles: Missing '%s'\n", pFileName );
	return false;
}


//-----------------------------------------------------------------------------
// Writes a particle system (and its children) to a buffer in DMX format
//-----------------------------------------------------------------------------
bool CParticleSystemMgr::WriteParticleConfigFile( const char *pParticleSystemName, CUtlBuffer &buf, bool bPreventNameBasedLookup )
{
	DECLARE_DMX_CONTEXT();
	CDmxElement *pParticleSystem = CreateParticleDmxElement( pParticleSystemName );
	return WriteParticleConfigFile( pParticleSystem, buf, bPreventNameBasedLookup );
}

bool CParticleSystemMgr::WriteParticleConfigFile( const DmObjectId_t& id, CUtlBuffer &buf, bool bPreventNameBasedLookup )
{
	DECLARE_DMX_CONTEXT();
	CDmxElement *pParticleSystem = CreateParticleDmxElement( id );
	return WriteParticleConfigFile( pParticleSystem, buf, bPreventNameBasedLookup );
}

bool CParticleSystemMgr::WriteParticleConfigFile( CDmxElement *pParticleSystem, CUtlBuffer &buf, bool bPreventNameBasedLookup )
{
	pParticleSystem->SetValue( "preventNameBasedLookup", bPreventNameBasedLookup );

	CDmxAttribute* pAttribute = pParticleSystem->GetAttribute( "children" );
	const CUtlVector< CDmxElement* >& children = pAttribute->GetArray< CDmxElement* >( );
	int nCount = children.Count();
	for ( int i = 0; i < nCount; ++i )
	{
		CDmxElement *pChildRef = children[ i ];
		CDmxElement *pChild = pChildRef->GetValue<CDmxElement*>( "child" );
		pChild->SetValue( "preventNameBasedLookup", bPreventNameBasedLookup );
	}

	// Now write the DMX out
	bool bOk = SerializeDMX( buf, pParticleSystem );
	CleanupDMX( pParticleSystem );
	return bOk;
}


//-----------------------------------------------------------------------------
// Name <-> handle mapping
//-----------------------------------------------------------------------------
ParticleSystemHandle_t CParticleSystemMgr::GetParticleSystemIndex( const char *pParticleSystemName )
{
	if ( !pParticleSystemName )
		return UTL_INVAL_SYMBOL;

	return m_pParticleSystemDictionary->FindParticleSystemHandle( pParticleSystemName );
}

ParticleSystemHandle_t CParticleSystemMgr::FindOrAddParticleSystemIndex( const char *pParticleSystemName )
{
	if ( !pParticleSystemName )
		return UTL_INVAL_SYMBOL;

	return m_pParticleSystemDictionary->FindOrAddParticleSystemHandle( pParticleSystemName );
}

const char *CParticleSystemMgr::GetParticleSystemNameFromIndex( ParticleSystemHandle_t iIndex )
{
	CParticleSystemDefinition *pDef = m_pParticleSystemDictionary->FindParticleSystem( iIndex );
	return pDef ? pDef->GetName() : "Unknown";
}

int CParticleSystemMgr::GetParticleSystemCount( void )
{
	return m_pParticleSystemDictionary->NameCount();
}


//-----------------------------------------------------------------------------
// Overrides one operator parameter of an already loaded definition, using the
// text representation of the value
//-----------------------------------------------------------------------------
bool CParticleSystemMgr::OverrideEffectParameter( const char *pParticleSystemName, const char *pOperatorName, const char *pParameterName, const char *pParameterValue )
{
	CParticleSystemDefinition *pDef = m_pParticleSystemDictionary->FindParticleSystem( pParticleSystemName );
	if ( !pDef )
	{
		Warning( "CParticleSystemMgr::OverrideEffectParameter - particle effect (%s) is not loaded\n", pParticleSystemName );
		return false;
	}

	CParticleOperatorInstance *pOp = pDef->FindOperatorByName( pOperatorName );
	if ( !pOp )
	{
		Warning( "CParticleSystemMgr::OverrideEffectParameter - particle operator (%s) is not defined for this effect (%s)\n", pOperatorName, pParticleSystemName );
		return false;
	}

	DECLARE_DMX_CONTEXT();
	const DmxElementUnpackStructure_t *pUnpack = pOp->GetDefinition()->GetUnpackStructure();

	// Find the unpack entry for the requested parameter
	DmAttributeType_t nParameterType = AT_UNKNOWN;
	for ( ; pUnpack->m_AttributeType != AT_UNKNOWN; ++pUnpack )
	{
		if ( !Q_stricmp( pParameterName, pUnpack->m_pAttributeName ) )
		{
			nParameterType = pUnpack->m_AttributeType;
			break;
		}
	}

	if ( nParameterType == AT_UNKNOWN )
	{
		Warning( "CParticleSystemMgr::OverrideEffectParameter - parameter (%s) is not defined for this operator (%s)\n", pParameterName, pOperatorName );
		return false;
	}

	// Parse the value into a temporary element
	CUtlBuffer valueBuffer( 0, 64, CUtlBuffer::TEXT_BUFFER );
	valueBuffer.PutString( pParameterValue );
	CDmxElement *pElement = CreateDmxElement( "CDmeElement" );
	CDmxAttribute *pAttribute = pElement->AddAttribute( pParameterName );
	if ( !pAttribute->Unserialize( nParameterType, valueBuffer ) )
	{
		// NOTE: pElement is not cleaned up on this path in the shipped code either
		//Warning( "CParticleSystemMgr::OverrideEffectParameter - parameter ('%s') has invalid value (%s) for its type (%s)\n", pParameterName, pParameterValue, pAttribute->GetTypeString() );
		return false;
	}

	// Unpack only that one field into the operator, then let it refresh its derived state
	DmxElementUnpackStructure_t singleUnpack[2] = { *pUnpack, *pUnpack };
	singleUnpack[1].m_AttributeType = AT_UNKNOWN;
	pElement->UnpackIntoStructure( pOp, pOp->GetDefinition()->GetClassSize(), singleUnpack );
	CleanupDMX( pElement );

	pOp->InitParams( pDef );
	pOp->CheckForFastPath();
	return true;
}


//-----------------------------------------------------------------------------
// Creates a particle collection
//-----------------------------------------------------------------------------
CParticleCollection *CParticleSystemMgr::CreateParticleCollection( const char *pParticleSystemName, float flDelay, int nRandomSeed )
{
	if ( !pParticleSystemName )
		return NULL;

	CParticleSystemDefinition *pDef = m_pParticleSystemDictionary->FindParticleSystem( pParticleSystemName );
	if ( !pDef )
	{
		Warning( "Attempted to create unknown particle system type %s\n", pParticleSystemName );
		return NULL;
	}

	CParticleCollection *pParticleCollection = new CParticleCollection;
	pParticleCollection->Init( pDef, flDelay, nRandomSeed );
	return pParticleCollection;
}

CParticleCollection *CParticleSystemMgr::CreateParticleCollection( ParticleSystemHandle_t particleSystemName, float flDelay, int nRandomSeed )
{
	if ( particleSystemName == UTL_INVAL_SYMBOL )
		return NULL;

	CParticleSystemDefinition *pDef = m_pParticleSystemDictionary->FindParticleSystem( particleSystemName );
	if ( !pDef )
	{
		Warning( "Attempted to create unknown particle system with unknown symbol\n" );
		return NULL;
	}

	CParticleCollection *pParticleCollection = new CParticleCollection;
	pParticleCollection->Init( pDef, flDelay, nRandomSeed );
	return pParticleCollection;
}

CParticleCollection *CParticleSystemMgr::CreateParticleCollection( const DmObjectId_t &id, float flDelay, int nRandomSeed )
{
	if ( !IsUniqueIdValid( id ) )
		return NULL;

	CParticleSystemDefinition *pDef = m_pParticleSystemDictionary->FindParticleSystem( id );
	if ( !pDef )
	{
		char pBuf[256];
		UniqueIdToString( id, pBuf, sizeof(pBuf) );
		Warning( "Attempted to create unknown particle system id %s\n", pBuf );
		return NULL;
	}

	CParticleCollection *pParticleCollection = new CParticleCollection;
	pParticleCollection->Init( pDef, flDelay, nRandomSeed );
	return pParticleCollection;
}


//-----------------------------------------------------------------------------
// Is a particular particle system defined?
//-----------------------------------------------------------------------------
bool CParticleSystemMgr::IsParticleSystemDefined( const DmObjectId_t &id )
{
	if ( !IsUniqueIdValid( id ) )
		return false;

	CParticleSystemDefinition *pDef = m_pParticleSystemDictionary->FindParticleSystem( id );
	return ( pDef != NULL );
}

bool CParticleSystemMgr::IsParticleSystemDefined( const char *pName )
{
	if ( !pName || !pName[0] )
		return false;

	CParticleSystemDefinition *pDef = m_pParticleSystemDictionary->FindParticleSystem( pName );
	return ( pDef != NULL );
}


//-----------------------------------------------------------------------------
// Serialization helpers
//-----------------------------------------------------------------------------
CDmxElement *CParticleSystemMgr::CreateParticleDmxElement( const DmObjectId_t &id )
{
	CParticleSystemDefinition *pDef = m_pParticleSystemDictionary->FindParticleSystem( id );

	// Write the particle system into a DMX element
	return pDef->Write();
}

CDmxElement *CParticleSystemMgr::CreateParticleDmxElement( const char *pParticleSystemName )
{
	CParticleSystemDefinition *pDef = m_pParticleSystemDictionary->FindParticleSystem( pParticleSystemName );

	// Write the particle system into a DMX element
	return pDef->Write();
}


//-----------------------------------------------------------------------------
// Sprite sheets, extracted from the base texture's VTF resource data
//-----------------------------------------------------------------------------
static unsigned int s_nBaseTextureVarCache = 0;

void CParticleSystemMgr::ShouldLoadSheets( bool bLoadSheets )
{
	m_bShouldLoadSheets = bLoadSheets;
}

CSheet *CParticleSystemMgr::FindOrLoadSheet( CParticleSystemDefinition *pDef )
{
	if ( !m_bShouldLoadSheets )
		return NULL;

	if ( pDef->m_bSheetSymbolCached )
	{
		if ( !pDef->m_SheetSymbol.IsValid() )
			return NULL;
		return m_SheetList[ pDef->m_SheetSymbol ];
	}

	pDef->m_SheetSymbol = UTL_INVAL_SYMBOL;
	pDef->m_bSheetSymbolCached = true;

	IMaterial *pMaterial = pDef->GetMaterial();
	if ( !pMaterial )
		return NULL;

	IMaterialVar *pVar = pMaterial->FindVarFast( "$basetexture", &s_nBaseTextureVarCache );
	if ( !pVar || !pVar->IsDefined() )
		return NULL;

	ITexture *pTex = pVar->GetTextureValue();
	if ( !pTex || pTex->IsError() )
		return NULL;

	CSheet *pNewSheet = NULL;
	int nCurCount = m_SheetList.GetNumStrings();
	CUtlSymbol sheetName = m_SheetList.AddString( pTex->GetName() );
	if ( sheetName < nCurCount )
	{
		// Already seen this texture
		pNewSheet = m_SheetList[ sheetName ];
	}
	else
	{
		size_t numBytes;
		void const *pSheetData = pTex->GetResourceData( VTF_RSRC_SHEET, &numBytes );
		if ( pSheetData )
		{
			CUtlBuffer bufLoad( pSheetData, numBytes, CUtlBuffer::READ_ONLY );
			pNewSheet = new CSheet( bufLoad );
		}
		m_SheetList[ sheetName ] = pNewSheet;
	}

	pDef->m_SheetSymbol = sheetName;
	pDef->m_bSheetSymbolCached = true;
	return pNewSheet;
}

void CParticleSystemMgr::FlushAllSheets( void )
{
	m_SheetList.PurgeAndDeleteElements();
	if ( !m_pParticleSystemDictionary )
		return;

	int nCount = m_pParticleSystemDictionary->Count();
	for ( int i = 0; i < nCount; ++i )
	{
		CParticleSystemDefinition* pDef = m_pParticleSystemDictionary->GetParticleSystem( i );
		pDef->m_bSheetSymbolCached = false;
	}

	nCount = m_pParticleSystemDictionary->NameCount();
	for ( ParticleSystemHandle_t h = 0; h < nCount; ++h )
	{
		CParticleSystemDefinition* pDef = m_pParticleSystemDictionary->FindParticleSystem( h );
		pDef->m_bSheetSymbolCached = false;
	}
}


//-----------------------------------------------------------------------------
// Render cache: opaque collections are grouped by material and drawn in
// batches at the end of the frame
//-----------------------------------------------------------------------------
void CParticleSystemMgr::ResetRenderCache( void )
{
	int nCount = m_RenderCache.Count();
	for ( int i = 0; i < nCount; ++i )
	{
		m_RenderCache[i].m_ParticleCollections.RemoveAll();
	}
}

void CParticleSystemMgr::AddToRenderCache( CParticleCollection *pParticles )
{
	if ( !pParticles->m_pDef )
		return;

	IMaterial *pMaterial = pParticles->m_pDef->GetMaterial();
	if ( !pMaterial || pMaterial->IsTranslucent() )
		return;

	// Being queued for drawing counts as being visible
	pParticles->m_flNextSleepTime = MAX( pParticles->m_flNextSleepTime, g_pParticleSystemMgr->GetLastSimulationTime() + pParticles->m_pDef->m_flNoDrawTimeToGoToSleep );

	int nCount = m_RenderCache.Count();
	int i;
	for ( i = 0; i < nCount; ++i )
	{
		if ( pParticles->m_pDef->GetMaterial() == m_RenderCache[i].m_pMaterial )
			break;
	}

	if ( i == nCount )
	{
		i = m_RenderCache.AddToTail();
		m_RenderCache[i].m_pMaterial = pParticles->m_pDef->GetMaterial();
	}

	m_RenderCache[i].m_ParticleCollections.AddToTail( pParticles );

	for( CParticleCollection *p = pParticles->m_Children.m_pHead; p; p = p->m_pNext )
	{
		AddToRenderCache( p );
	}
}

void CParticleSystemMgr::BuildBatchList( int iRenderCache, IMatRenderContext *pRenderContext, CUtlVector< Batch_t >& batches )
{
	batches.RemoveAll();

	IMaterial *pMaterial = m_RenderCache[iRenderCache].m_pMaterial;
	int nMaxVertices = pRenderContext->GetMaxVerticesToRender( pMaterial );
	int nMaxIndices = pRenderContext->GetMaxIndicesToRender();

	int nBatchIndex = batches.AddToTail();
	Batch_t* pBatch = &batches[nBatchIndex];
	pBatch->m_nVertCount = 0;
	pBatch->m_nIndexCount = 0;

	// Pack as many particles as fit into each batch, splitting a renderer's
	// particles across batches where necessary
	int nCacheCount = m_RenderCache[iRenderCache].m_ParticleCollections.Count();
	for ( int iCache = 0; iCache < nCacheCount; ++iCache )
	{
		CParticleCollection *pParticles = m_RenderCache[iRenderCache].m_ParticleCollections[iCache];
		if ( !pParticles->m_pDef )
			continue;

		int nRenderCount = pParticles->GetRendererCount();
		for ( int j = 0; j < nRenderCount; ++j )
		{
			int i = 0;
			while ( i < pParticles->m_nActiveParticles )
			{
				int nIndices;

				BatchStep_t step;
				step.m_pParticles = pParticles;
				step.m_pRenderer = pParticles->GetRenderer( j );
				step.m_pContext = pParticles->GetRendererContext( j );
				step.m_nFirstParticle = i;
				step.m_nParticleCount = step.m_pRenderer->GetParticlesToRender( pParticles,
					step.m_pContext, step.m_nFirstParticle, nMaxVertices, nMaxIndices, &step.m_nVertCount, &nIndices );
				i += step.m_nParticleCount;

				if ( step.m_nParticleCount > 0 )
				{
					pBatch->m_nVertCount += step.m_nVertCount;
					pBatch->m_nIndexCount += nIndices;
					pBatch->m_BatchStep.AddToTail( step );
					continue;
				}

				// Nothing fit even in an empty batch; give up on this renderer
				if ( pBatch->m_nVertCount == 0 )
					break;

				// Current batch is full, start a new one
				nBatchIndex = batches.AddToTail();
				pBatch = &batches[nBatchIndex];
				pBatch->m_nVertCount = 0;
				pBatch->m_nIndexCount = 0;
			}
		}
	}

	if ( pBatch->m_nVertCount <= 0 || pBatch->m_nIndexCount <= 0 )
	{
		batches.FastRemove( batches.Count() - 1 );
	}
}


//-----------------------------------------------------------------------------
// Debug output
//-----------------------------------------------------------------------------
void CParticleSystemMgr::DumpParticleList( const char *pNameSubstring )
{
	if ( pNameSubstring )
	{
		DevMsg( "New Particle Systems Matching '%s':\n", pNameSubstring );
	}
	else
	{
		DevMsg( "All Particle Systems:\n" );
	}

	for ( int i = 0; i < m_pParticleSystemDictionary->NameCount(); i++ )
	{
		CParticleSystemDefinition *pDict = ( *m_pParticleSystemDictionary )[i];
		if ( !pNameSubstring || V_stristr( pDict->GetName(), pNameSubstring ) )
		{
			for ( CParticleCollection *p = pDict->FirstCollection(); p; p = p->GetNextCollectionUsingSameDef() )
			{
				Vector vecMins = p->m_MinBounds, vecMaxs = p->m_MaxBounds;
				Vector vecCenter = ( vecMins + vecMaxs ) * 0.5f;
				DevMsg( "%40s: Age: %6.2f, NumActive: %3d, Bounds Center: (%.2f,%.2f,%.2f) (0x%08X)\n",
					pDict->GetName(), p->m_flCurTime, p->m_nActiveParticles, vecCenter.x, vecCenter.y, vecCenter.z, p );
			}
		}
	}
}

void CParticleSystemMgr::DumpProfileInformation( void )
{
	// Profiling output is compiled out of the shipped library
}

void CParticleSystemMgr::CommitProfileInformation( bool bCommit )
{
	// Profiling is compiled out of the shipped library
}


//-----------------------------------------------------------------------------
// Draws everything queued by AddToRenderCache, then empties the cache
//-----------------------------------------------------------------------------
void CParticleSystemMgr::DrawRenderCache( bool bShadowDepth )
{
	int nRenderCacheCount = m_RenderCache.Count();
	if ( nRenderCacheCount == 0 )
		return;

	VPROF_BUDGET( "CParticleSystemMgr::DrawRenderCache", VPROF_BUDGETGROUP_PARTICLE_RENDERING );

	CMatRenderContextPtr pRenderContext( g_pMaterialSystem );
	pRenderContext->MatrixMode( MATERIAL_MODEL );
	pRenderContext->PushMatrix();
	pRenderContext->LoadIdentity();

	CUtlVector< Batch_t > batches( 0, 8 );

	for ( int iRenderCache = 0; iRenderCache < nRenderCacheCount; ++iRenderCache )
	{
		int nCacheCount = m_RenderCache[iRenderCache].m_ParticleCollections.Count();
		if ( !nCacheCount )
			continue;

		IMaterial *pMaterial = bShadowDepth ? m_pShadowDepthMaterial : m_RenderCache[iRenderCache].m_pMaterial;

		BuildBatchList( iRenderCache, pRenderContext, batches );
		int nBatchCount = batches.Count();
		if ( nBatchCount == 0 )
			continue;

		pRenderContext->Bind( pMaterial );
		CMeshBuilder meshBuilder;
		IMesh* pMesh = pRenderContext->GetDynamicMesh();
		for ( int i = 0; i < nBatchCount; ++i )
		{
			const Batch_t& batch = batches[i];

			meshBuilder.Begin( pMesh, MATERIAL_TRIANGLES, batch.m_nVertCount, batch.m_nIndexCount );

			int nVertexOffset = 0;
			int nBatchStepCount = batch.m_BatchStep.Count();
			for ( int j = 0; j < nBatchStepCount; ++j )
			{
				const BatchStep_t &step = batch.m_BatchStep[j];
				step.m_pRenderer->RenderUnsorted( step.m_pParticles, step.m_pContext, pRenderContext,
					meshBuilder, nVertexOffset, step.m_nFirstParticle, step.m_nParticleCount );
				nVertexOffset += step.m_nVertCount;
			}

			meshBuilder.End();
			pMesh->Draw();
		}
	}

	ResetRenderCache( );

	pRenderContext->MatrixMode( MATERIAL_MODEL );
	pRenderContext->PopMatrix();
}


//-----------------------------------------------------------------------------
// Fallback used when the query has no access to model hitboxes: every point
// lands on the control point itself
//-----------------------------------------------------------------------------
void IParticleSystemQuery::GetRandomPointsOnControllingObjectHitBox( 
	CParticleCollection *pParticles,
	int nControlPointNumber, 
	int nNumPtsOut,
	float flBBoxScale,
	int nNumTrysToGetAPointInsideTheModel,
	Vector *pPntsOut,
	Vector vecDirectionalBias,
	Vector *pHitBoxRelativeCoordOut,
	int *pHitBoxIndexOut,
	int nDesiredHitbox )
{
	for ( int i = 0; i < nNumPtsOut; ++i )
	{
		pPntsOut[i] = pParticles->GetControlPointAtCurrentTime( nControlPointNumber );
		if ( pHitBoxRelativeCoordOut )
		{
			pHitBoxRelativeCoordOut[i].Init();
		}
		if ( pHitBoxIndexOut )
		{
			pHitBoxIndexOut[i] = -1;
		}
	}
}


//-----------------------------------------------------------------------------
// Refreshes the cached hitboxes of a control point's controlling object,
// keeping the previous frame's set for interpolation
//-----------------------------------------------------------------------------
void CParticleCollection::UpdateHitBoxInfo( int nControlPointNumber )
{
	CModelHitBoxesInfo &hb = ControlPointHitBox( nControlPointNumber );

	if ( hb.m_flLastUpdateTime == m_flCurTime )
		return;													// up to date

	hb.m_flLastUpdateTime = m_flCurTime;

	// make sure space allocated
	if ( ! hb.m_pHitBoxes )
		hb.m_pHitBoxes = new ModelHitBoxInfo_t[ MAXSTUDIOBONES ];
	if ( ! hb.m_pPrevBoxes )
		hb.m_pPrevBoxes = new ModelHitBoxInfo_t[ MAXSTUDIOBONES ];

	// current becomes previous
	hb.m_nNumPrevHitBoxes = hb.m_nNumHitBoxes;
	hb.m_flPrevLastUpdateTime = hb.m_flLastUpdateTime;
	V_swap( hb.m_pHitBoxes, hb.m_pPrevBoxes );

	// fetch the new set
	hb.m_nNumHitBoxes = g_pParticleSystemMgr->Query()->GetControllingObjectHitBoxInfo(
		this, nControlPointNumber, MAXSTUDIOBONES, hb.m_pHitBoxes );
}


//-----------------------------------------------------------------------------
// Reads the list of particle files from a manifest
//-----------------------------------------------------------------------------
void GetParticleManifest( CUtlVector<CUtlString>& list )
{
	GetParticleManifest( list, "particles/particles_manifest.txt" );
}

void GetParticleManifest( CUtlVector<CUtlString>& list, const char *pFile )
{
	KeyValues *manifest = new KeyValues( pFile );
	if ( manifest->LoadFromFile( g_pFullFileSystem, pFile, "GAME" ) )
	{
		for ( KeyValues *sub = manifest->GetFirstSubKey(); sub != NULL; sub = sub->GetNextKey() )
		{
			if ( !Q_stricmp( sub->GetName(), "file" ) )
			{
				list.AddToTail( sub->GetString() );
				continue;
			}

			Warning( "CParticleMgr::Init:  Manifest '%s' with bogus file type '%s', expecting 'file'\n", pFile, sub->GetName() );
		}
	}
	else
	{
		Warning( "PARTICLE SYSTEM: Unable to load manifest file '%s'\n", pFile );
	}

	manifest->deleteThis();
}

// the memory-debug "new" macro would break these operator new definitions
#include "tier0/memdbgoff.h"

//-----------------------------------------------------------------------------
// need custom new/delete for alignment for simd
//-----------------------------------------------------------------------------
void* CParticleCollection::operator new(size_t nSize)
{
	return MemAlloc_AllocAligned(nSize, 16);
}

void* CParticleCollection::operator new(size_t nSize, int nBlockUse, const char* pFileName, int nLine)
{
	return MemAlloc_AllocAlignedFileLine(nSize, 16, pFileName, nLine);
}

void* CWorldCollideContextData::operator new(size_t nSize)
{
	return MemAlloc_AllocAligned(nSize, 16);
}

void* CWorldCollideContextData::operator new(size_t nSize, int nBlockUse, const char* pFileName, int nLine)
{
	return MemAlloc_AllocAlignedFileLine(nSize, 16, pFileName, nLine);
}

//-----------------------------------------------------------------------------
// Operator instances are 16-byte aligned (they may hold SIMD members)
//-----------------------------------------------------------------------------
void* CParticleOperatorInstance::operator new(size_t nSize)
{
	return MemAlloc_AllocAligned(nSize, 16);
}

void* CParticleOperatorInstance::operator new(size_t nSize, int nBlockUse, const char* pFileName, int nLine)
{
	return MemAlloc_AllocAlignedFileLine(nSize, 16, pFileName, nLine);
}
