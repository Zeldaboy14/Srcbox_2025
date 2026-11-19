//========= Srcbox ============================================================//
//
// Purpose: GMod-style bone manipulation and physics bone lookups
//          ( see srcbox_bonemanip.h ).
//
//=============================================================================//

#include "cbase.h"

#ifdef LUA_SDK

#include "srcbox/srcbox_bonemanip.h"
#include "vphysics_interface.h"
#include "vcollide_parse.h"
#include "physics_shared.h"
#include "tier1/utlmap.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

#ifdef CLIENT_DLL
#define CSrcboxBoneManip C_SrcboxBoneManip
#endif

//-----------------------------------------------------------------------------
// The networked holder of an entity's manipulations
//-----------------------------------------------------------------------------
class CSrcboxBoneManip : public CBaseEntity
{
public:
	DECLARE_CLASS( CSrcboxBoneManip, CBaseEntity );
	DECLARE_NETWORKCLASS();

	CSrcboxBoneManip();

#ifdef CLIENT_DLL
	virtual ~CSrcboxBoneManip();
	virtual void	OnDataChanged( DataUpdateType_t updateType );
	virtual bool	ShouldDraw( void )	{ return false; }

	Vector			m_vecManipPos[MAXSTUDIOBONES];
	QAngle			m_angManipAng[MAXSTUDIOBONES];
	Vector			m_vecManipScale[MAXSTUDIOBONES];
	EHANDLE			m_hTarget;
	EHANDLE			m_hRegisteredTarget;
#else
	virtual void	Spawn( void );
	virtual int		UpdateTransmitState( void )	{ return SetTransmitState( FL_EDICT_ALWAYS ); }
	virtual void	UpdateOnRemove( void );
	void			CheckTarget( void );

	void			SetBone( int iBone, const Vector *pPos, const QAngle *pAng, const Vector *pScale );

	CNetworkArray( Vector, m_vecManipPos, MAXSTUDIOBONES );
	CNetworkArray( QAngle, m_angManipAng, MAXSTUDIOBONES );
	CNetworkArray( Vector, m_vecManipScale, MAXSTUDIOBONES );
	CNetworkHandle( CBaseEntity, m_hTarget );

	SrcboxBoneManipData_t	m_Data;
#endif
};

LINK_ENTITY_TO_CLASS( srcbox_bonemanip, CSrcboxBoneManip );

IMPLEMENT_NETWORKCLASS_ALIASED( SrcboxBoneManip, DT_SrcboxBoneManip )

BEGIN_NETWORK_TABLE( CSrcboxBoneManip, DT_SrcboxBoneManip )
#ifdef CLIENT_DLL
	RecvPropEHandle( RECVINFO( m_hTarget ) ),
	RecvPropArray3( RECVINFO_ARRAY( m_vecManipPos ), RecvPropVector( RECVINFO( m_vecManipPos[0] ) ) ),
	RecvPropArray3( RECVINFO_ARRAY( m_angManipAng ), RecvPropQAngles( RECVINFO( m_angManipAng[0] ) ) ),
	RecvPropArray3( RECVINFO_ARRAY( m_vecManipScale ), RecvPropVector( RECVINFO( m_vecManipScale[0] ) ) ),
#else
	SendPropEHandle( SENDINFO( m_hTarget ) ),
	SendPropArray3( SENDINFO_ARRAY3( m_vecManipPos ), SendPropVector( SENDINFO_ARRAY( m_vecManipPos ), -1, SPROP_NOSCALE ) ),
	SendPropArray3( SENDINFO_ARRAY3( m_angManipAng ), SendPropQAngles( SENDINFO_ARRAY( m_angManipAng ), 32 ) ),
	SendPropArray3( SENDINFO_ARRAY3( m_vecManipScale ), SendPropVector( SENDINFO_ARRAY( m_vecManipScale ), -1, SPROP_NOSCALE ) ),
#endif
END_NETWORK_TABLE()

//-----------------------------------------------------------------------------
// Lookup: target entity -> its manipulations
//-----------------------------------------------------------------------------
static bool BoneManipKeyLess( const unsigned long &a, const unsigned long &b ) { return a < b; }

#ifdef CLIENT_DLL
// Client: the values to apply, from the server's holder and/or client Lua
static CUtlMap< unsigned long, SrcboxBoneManipData_t * > s_ManipData( BoneManipKeyLess );

static SrcboxBoneManipData_t *ClientManipData( const CBaseEntity *pTarget, bool bCreate )
{
	unsigned long key = pTarget->GetRefEHandle().ToInt();
	unsigned short idx = s_ManipData.Find( key );
	if ( idx != s_ManipData.InvalidIndex() )
		return s_ManipData[idx];
	if ( !bCreate )
		return NULL;

	SrcboxBoneManipData_t *pData = new SrcboxBoneManipData_t;
	s_ManipData.Insert( key, pData );
	return pData;
}

static void ClientManipRemove( const EHANDLE &hTarget )
{
	unsigned short idx = s_ManipData.Find( hTarget.ToInt() );
	if ( idx != s_ManipData.InvalidIndex() )
	{
		delete s_ManipData[idx];
		s_ManipData.RemoveAt( idx );
	}
}
#else
// Server: target -> its holder entity
static CUtlMap< unsigned long, CHandle< CSrcboxBoneManip > > s_ManipEntities( BoneManipKeyLess );

static CSrcboxBoneManip *ServerManipEntity( CBaseEntity *pTarget, bool bCreate )
{
	unsigned long key = pTarget->GetRefEHandle().ToInt();
	unsigned short idx = s_ManipEntities.Find( key );
	if ( idx != s_ManipEntities.InvalidIndex() )
	{
		CSrcboxBoneManip *pManip = s_ManipEntities[idx].Get();
		if ( pManip )
			return pManip;
		s_ManipEntities.RemoveAt( idx );
	}

	if ( !bCreate )
		return NULL;

	CSrcboxBoneManip *pManip = static_cast< CSrcboxBoneManip * >( CreateEntityByName( "srcbox_bonemanip" ) );
	if ( !pManip )
		return NULL;

	pManip->m_hTarget = pTarget;
	DispatchSpawn( pManip );
	s_ManipEntities.Insert( key, CHandle< CSrcboxBoneManip >( pManip ) );
	return pManip;
}
#endif

//-----------------------------------------------------------------------------
// The holder entity
//-----------------------------------------------------------------------------
CSrcboxBoneManip::CSrcboxBoneManip()
{
#ifdef CLIENT_DLL
	for ( int i = 0; i < MAXSTUDIOBONES; i++ )
	{
		m_vecManipPos[i].Init();
		m_angManipAng[i].Init();
		m_vecManipScale[i].Init( 1.0f, 1.0f, 1.0f );
	}
#else
	for ( int i = 0; i < MAXSTUDIOBONES; i++ )
	{
		m_vecManipPos.Set( i, vec3_origin );
		m_angManipAng.Set( i, vec3_angle );
		m_vecManipScale.Set( i, Vector( 1.0f, 1.0f, 1.0f ) );
	}
#endif
}

#ifdef CLIENT_DLL

CSrcboxBoneManip::~CSrcboxBoneManip()
{
	if ( m_hRegisteredTarget.ToInt() != 0 )
		ClientManipRemove( m_hRegisteredTarget );
}

void CSrcboxBoneManip::OnDataChanged( DataUpdateType_t updateType )
{
	BaseClass::OnDataChanged( updateType );

	CBaseEntity *pTarget = m_hTarget.Get();
	if ( !pTarget )
		return;

	if ( m_hRegisteredTarget != m_hTarget && m_hRegisteredTarget.ToInt() != 0 )
		ClientManipRemove( m_hRegisteredTarget );
	m_hRegisteredTarget = m_hTarget;

	// The server's values replace whatever client Lua set for this entity
	SrcboxBoneManipData_t *pData = ClientManipData( pTarget, true );
	for ( int i = 0; i < MAXSTUDIOBONES; i++ )
	{
		pData->m_Pos[i] = m_vecManipPos[i];
		pData->m_Ang[i] = m_angManipAng[i];
		pData->m_Scale[i] = m_vecManipScale[i];
	}
	pData->UpdateAny();
}

#else

void CSrcboxBoneManip::Spawn( void )
{
	BaseClass::Spawn();
	SetSolid( SOLID_NONE );
	SetMoveType( MOVETYPE_NONE );
	AddEffects( EF_NODRAW );

	SetThink( &CSrcboxBoneManip::CheckTarget );
	SetNextThink( gpGlobals->curtime + 1.0f );
}

void CSrcboxBoneManip::CheckTarget( void )
{
	// Go away with the entity we manipulate
	if ( !m_hTarget.Get() )
	{
		UTIL_Remove( this );
		return;
	}
	SetNextThink( gpGlobals->curtime + 1.0f );
}

void CSrcboxBoneManip::UpdateOnRemove( void )
{
	for ( unsigned short idx = s_ManipEntities.FirstInorder(); idx != s_ManipEntities.InvalidIndex(); idx = s_ManipEntities.NextInorder( idx ) )
	{
		if ( s_ManipEntities[idx].Get() == this )
		{
			s_ManipEntities.RemoveAt( idx );
			break;
		}
	}
	BaseClass::UpdateOnRemove();
}

void CSrcboxBoneManip::SetBone( int iBone, const Vector *pPos, const QAngle *pAng, const Vector *pScale )
{
	if ( pPos )
	{
		m_Data.m_Pos[iBone] = *pPos;
		m_vecManipPos.Set( iBone, *pPos );
	}
	if ( pAng )
	{
		m_Data.m_Ang[iBone] = *pAng;
		m_angManipAng.Set( iBone, *pAng );
	}
	if ( pScale )
	{
		m_Data.m_Scale[iBone] = *pScale;
		m_vecManipScale.Set( iBone, *pScale );
	}
	m_Data.UpdateAny();
}

#endif

//-----------------------------------------------------------------------------
// API
//-----------------------------------------------------------------------------
const SrcboxBoneManipData_t *SrcboxBoneManip_Get( const CBaseEntity *pEntity )
{
	if ( !pEntity )
		return NULL;

#ifdef CLIENT_DLL
	const SrcboxBoneManipData_t *pData = ClientManipData( pEntity, false );
#else
	CSrcboxBoneManip *pManip = ServerManipEntity( const_cast< CBaseEntity * >( pEntity ), false );
	const SrcboxBoneManipData_t *pData = pManip ? &pManip->m_Data : NULL;
#endif

	return ( pData && pData->m_bAny ) ? pData : NULL;
}

static void BoneManipSet( CBaseAnimating *pEntity, int iBone, const Vector *pPos, const QAngle *pAng, const Vector *pScale )
{
	if ( !pEntity || iBone < 0 || iBone >= MAXSTUDIOBONES )
		return;

#ifdef CLIENT_DLL
	SrcboxBoneManipData_t *pData = ClientManipData( pEntity, true );
	if ( pPos )		pData->m_Pos[iBone] = *pPos;
	if ( pAng )		pData->m_Ang[iBone] = *pAng;
	if ( pScale )	pData->m_Scale[iBone] = *pScale;
	pData->UpdateAny();
#else
	// Don't create a holder just to store the defaults
	bool bDefault = ( pPos && *pPos == vec3_origin ) || ( pAng && *pAng == vec3_angle ) || ( pScale && *pScale == Vector( 1, 1, 1 ) );
	CSrcboxBoneManip *pManip = ServerManipEntity( pEntity, !bDefault );
	if ( !pManip )
		return;
	pManip->SetBone( iBone, pPos, pAng, pScale );
	pEntity->InvalidateBoneCache();
#endif
}

void SrcboxBoneManip_SetPosition( CBaseAnimating *pEntity, int iBone, const Vector &vecPos )	{ BoneManipSet( pEntity, iBone, &vecPos, NULL, NULL ); }
void SrcboxBoneManip_SetAngles( CBaseAnimating *pEntity, int iBone, const QAngle &ang )			{ BoneManipSet( pEntity, iBone, NULL, &ang, NULL ); }
void SrcboxBoneManip_SetScale( CBaseAnimating *pEntity, int iBone, const Vector &vecScale )		{ BoneManipSet( pEntity, iBone, NULL, NULL, &vecScale ); }

void SrcboxBoneManip_ApplyLocal( const CBaseEntity *pEntity, const CStudioHdr *pHdr, Vector *pos, Quaternion *q, int boneMask )
{
	const SrcboxBoneManipData_t *pData = SrcboxBoneManip_Get( pEntity );
	if ( !pData || !pHdr )
		return;

	int nBones = MIN( pHdr->numbones(), MAXSTUDIOBONES );
	for ( int i = 0; i < nBones; i++ )
	{
		if ( !( pHdr->boneFlags( i ) & boneMask ) )
			continue;

		// Position: an offset in the bone's own ( animated ) frame
		if ( pData->m_Pos[i] != vec3_origin )
		{
			matrix3x4_t boneLocal;
			QuaternionMatrix( q[i], boneLocal );
			Vector vecOffset;
			VectorRotate( pData->m_Pos[i], boneLocal, vecOffset );
			pos[i] += vecOffset;
		}

		// Angles: a rotation in the bone's own frame
		if ( pData->m_Ang[i] != vec3_angle )
		{
			Quaternion qManip, qOut;
			AngleQuaternion( pData->m_Ang[i], qManip );
			QuaternionMult( q[i], qManip, qOut );
			q[i] = qOut;
		}
	}
}

void SrcboxBoneManip_ApplyScale( const CBaseEntity *pEntity, const CStudioHdr *pHdr, matrix3x4_t *pBoneToWorld, int boneMask )
{
	const SrcboxBoneManipData_t *pData = SrcboxBoneManip_Get( pEntity );
	if ( !pData || !pHdr || !pBoneToWorld )
		return;

	// After the whole skeleton is built, so a bone's scale doesn't carry into its children
	int nBones = MIN( pHdr->numbones(), MAXSTUDIOBONES );
	for ( int i = 0; i < nBones; i++ )
	{
		if ( !( pHdr->boneFlags( i ) & boneMask ) )
			continue;

		const Vector &vecScale = pData->m_Scale[i];
		if ( vecScale == Vector( 1, 1, 1 ) )
			continue;

		matrix3x4_t &m = pBoneToWorld[i];
		for ( int r = 0; r < 3; r++ )
		{
			m[r][0] *= vecScale.x;
			m[r][1] *= vecScale.y;
			m[r][2] *= vecScale.z;
		}
	}
}

//-----------------------------------------------------------------------------
// Physics bones: the model's collision solids in order, each tied to a bone by name
//-----------------------------------------------------------------------------
static bool PhysBoneModelLess( const model_t * const &a, const model_t * const &b ) { return a < b; }
static CUtlMap< const model_t *, CUtlVector< int > * > s_PhysBoneMaps( PhysBoneModelLess );

static const CUtlVector< int > *PhysBoneMap( CBaseAnimating *pEntity )
{
	const model_t *pModel = pEntity ? pEntity->GetModel() : NULL;
	if ( !pModel )
		return NULL;

	unsigned short idx = s_PhysBoneMaps.Find( pModel );
	if ( idx != s_PhysBoneMaps.InvalidIndex() )
		return s_PhysBoneMaps[idx];

	CUtlVector< int > *pMap = new CUtlVector< int >;

	vcollide_t *pCollide = modelinfo->GetVCollide( pModel );
	if ( pCollide && pCollide->pKeyValues && physcollision )
	{
		IVPhysicsKeyParser *pParse = physcollision->VPhysicsKeyParserCreate( pCollide->pKeyValues );
		while ( !pParse->Finished() )
		{
			const char *pszBlock = pParse->GetCurrentBlockName();
			if ( !V_stricmp( pszBlock, "solid" ) )
			{
				solid_t solid;
				pParse->ParseSolid( &solid, NULL );
				if ( solid.index >= 0 && solid.index < MAXSTUDIOBONES )
				{
					while ( pMap->Count() <= solid.index )
						pMap->AddToTail( -1 );
					( *pMap )[solid.index] = pEntity->LookupBone( solid.name );
				}
			}
			else
			{
				pParse->SkipBlock();
			}
		}
		physcollision->VPhysicsKeyParserDestroy( pParse );
	}

	s_PhysBoneMaps.Insert( pModel, pMap );
	return pMap;
}

int SrcboxPhysBoneToBone( CBaseAnimating *pEntity, int iPhysBone )
{
	const CUtlVector< int > *pMap = PhysBoneMap( pEntity );
	if ( !pMap || !pMap->Count() )
		return ( iPhysBone == 0 ) ? 0 : -1;
	if ( iPhysBone < 0 || iPhysBone >= pMap->Count() )
		return -1;
	return ( *pMap )[iPhysBone];
}

int SrcboxBoneToPhysBone( CBaseAnimating *pEntity, int iBone )
{
	const CUtlVector< int > *pMap = PhysBoneMap( pEntity );
	CStudioHdr *pHdr = pEntity ? pEntity->GetModelPtr() : NULL;
	if ( !pMap || !pHdr || iBone < 0 || iBone >= pHdr->numbones() )
		return -1;
	if ( !pMap->Count() )
		return 0;

	// The physics bone this bone belongs to: its own, or its nearest parent's
	for ( int iCur = iBone; iCur >= 0; iCur = pHdr->pBone( iCur )->parent )
	{
		int iPhys = pMap->Find( iCur );
		if ( iPhys != pMap->InvalidIndex() )
			return iPhys;
	}
	return -1;
}

#endif // LUA_SDK
