//========= Srcbox ============================================================//
//
// Purpose: Per-instance color modulation for static props ("rendercolor" on
//          prop_static), as in L4D / L4D2 / Alien Swarm / CS:GO.
//
// The TF2 SDK engine reads static props but has no per-prop color, so this
// is done from the client:
//
//  1. On level load the map's static prop game lump ('sprp') is read here,
//     and m_DiffuseModulation is kept for every prop that has one. Lump
//     versions that carry it: v7/v8 (L4D), v9 (L4D2/ASW), v10/v11 (CS:GO).
//     TF2-format lumps (v7*/v10 with 72-byte props) have no color, and then
//     nothing below runs.
//
//  2. IStudioRender::DrawModelStaticProp and ::DrawStaticPropShadows
//     (CStudioRenderContext, studiorender.dll) are detoured. Both engine
//     paths that draw static props (CModelRender::DrawModelExStaticProp and
//     the batched DrawStaticPropArrayFast) put the prop's IClientRenderable in
//     DrawModelInfo_t::m_pClientEntity; it is matched once to its lump entry
//     by origin/angles/model and cached, and the color is set with
//     SetColorModulation around the original call.
//
//=============================================================================//

#include "cbase.h"
#include "istudiorender.h"
#include "filesystem.h"
#include "staticprop_colormod.h"
#include "bsp_compat.h"
#include "materialsystem/imaterial.h"
#include "materialsystem/imaterialvar.h"
#include "materialsystem/imaterialsystem.h"
#include "texture_group_names.h"
#include "studio.h"

#include "MinHook.h"
#undef CreateEvent

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

// What happened, for r_staticprop_colormod_info
static char		s_szLoadStatus[256] = "no map loaded yet";
static bool		s_bHookInstalled = false;
static int		s_nDrawCalls = 0;
static int		s_nTintedCalls = 0;
static int		s_nResolvedProps = 0;
static int		s_nUnmatchedProps = 0;
#define SPCM_STATUS( ... )	V_snprintf( s_szLoadStatus, sizeof( s_szLoadStatus ), __VA_ARGS__ )

static ConVar r_staticprop_colormod( "r_staticprop_colormod", "1", FCVAR_NONE, "Apply the per-prop color (rendercolor) stored in the map's static prop lump." );

//-----------------------------------------------------------------------------
// Map data
//-----------------------------------------------------------------------------
struct StaticPropColor_t
{
	Vector	m_vecOrigin;
	QAngle	m_angAngles;
	int		m_nDictIndex;
	int		m_nNextInBucket;
	color32	m_Color;
	bool	m_bClaimed;
};

static CUtlVector< StaticPropColor_t >			s_PropColors;
static CUtlVector< CUtlString >					s_PropDict;
static CUtlMap< unsigned int, int, int >		s_OriginBuckets( DefLessFunc( unsigned int ) );	// origin hash -> first prop
static CUtlMap< IClientRenderable *, int, int >	s_ResolvedProps( DefLessFunc( IClientRenderable * ) );	// -1 = no color
static bool										s_bHasPropColors = false;

static unsigned int HashOrigin( const Vector &v )
{
	// props are matched against the exact same floats the engine read from the lump,
	// but round to 1/8 unit so tiny differences still land in the same bucket
	int x = (int)floorf( v.x * 8.0f + 0.5f );
	int y = (int)floorf( v.y * 8.0f + 0.5f );
	int z = (int)floorf( v.z * 8.0f + 0.5f );
	return (unsigned int)( x * 73856093 ) ^ (unsigned int)( y * 19349663 ) ^ (unsigned int)( z * 83492791 );
}

static void ClearPropColors()
{
	s_PropColors.Purge();
	s_PropDict.Purge();
	s_OriginBuckets.Purge();
	s_ResolvedProps.Purge();
	s_bHasPropColors = false;
}

//-----------------------------------------------------------------------------
// BSP reading
//-----------------------------------------------------------------------------
#define SPCM_BSP_IDENT			( ( 'P' << 24 ) + ( 'S' << 16 ) + ( 'B' << 8 ) + 'V' )
#define SPCM_HEADER_LUMPS		64
#define SPCM_LUMP_ENTITIES		0
#define SPCM_LUMP_GAME_LUMP		35
#define SPCM_GAMELUMP_SPRP		( ( 's' << 24 ) + ( 'p' << 16 ) + ( 'r' << 8 ) + 'p' )
#define SPCM_GAMELUMP_SPRC		( ( 's' << 24 ) + ( 'p' << 16 ) + ( 'r' << 8 ) + 'c' )	// Srcbox vbsp: color32 per prop
#define SPCM_GAMELUMP_COMPRESSED 0x0001
#define SPCM_PROP_NAME_LENGTH	128

// The map is read from the original file, not through the version 20 view that
// bsp_compat.cpp gives the engine for newer maps: that view has the static props
// in the TF2 layout, which has no color. BSPCOMPAT_BYPASS_ATTRIBUTE tells the
// redirect to leave this open alone. Maps in VPKs (no file path) go through the
// filesystem instead.
struct SPCMFile_t
{
	HANDLE			m_hFile;
	FileHandle_t	m_hFSFile;
	unsigned int	m_nSize;
};

static SPCMFile_t *OpenOriginalMap( const char *pszMapFile )
{
	char szFullPath[MAX_PATH];
	if ( filesystem->RelativePathToFullPath( pszMapFile, "GAME", szFullPath, sizeof( szFullPath ) ) && szFullPath[0] &&
		 !V_stricmp( V_GetFileExtension( szFullPath ) ? V_GetFileExtension( szFullPath ) : "", "bsp" ) )
	{
		HANDLE hFile = CreateFileA( szFullPath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
			FILE_ATTRIBUTE_NORMAL | BSPCOMPAT_BYPASS_ATTRIBUTE, NULL );
		if ( hFile != INVALID_HANDLE_VALUE )
		{
			LARGE_INTEGER size;
			if ( GetFileSizeEx( hFile, &size ) && size.QuadPart < 0x7FFFFFFF )
			{
				SPCMFile_t *pFile = new SPCMFile_t;
				pFile->m_hFile = hFile;
				pFile->m_hFSFile = FILESYSTEM_INVALID_HANDLE;
				pFile->m_nSize = (unsigned int)size.QuadPart;
				return pFile;
			}
			CloseHandle( hFile );
		}
	}

	FileHandle_t hFSFile = filesystem->Open( pszMapFile, "rb", "GAME" );
	if ( !hFSFile )
		return NULL;

	SPCMFile_t *pFile = new SPCMFile_t;
	pFile->m_hFile = INVALID_HANDLE_VALUE;
	pFile->m_hFSFile = hFSFile;
	pFile->m_nSize = filesystem->Size( hFSFile );
	return pFile;
}

static void CloseMap( SPCMFile_t *pFile )
{
	if ( pFile->m_hFile != INVALID_HANDLE_VALUE )
		CloseHandle( pFile->m_hFile );
	else
		filesystem->Close( pFile->m_hFSFile );
	delete pFile;
}

static bool ReadAt( SPCMFile_t *pFile, unsigned int nOffset, void *pDest, int nSize )
{
	if ( pFile->m_hFile != INVALID_HANDLE_VALUE )
	{
		OVERLAPPED ov;
		memset( &ov, 0, sizeof( ov ) );
		ov.Offset = nOffset;
		DWORD nRead = 0;
		return ReadFile( pFile->m_hFile, pDest, nSize, &nRead, &ov ) && nRead == (DWORD)nSize;
	}

	filesystem->Seek( pFile->m_hFSFile, nOffset, FILESYSTEM_SEEK_HEAD );
	return filesystem->Read( pDest, nSize, pFile->m_hFSFile ) == nSize;
}

// Byte offset of m_DiffuseModulation inside one serialized prop, or -1 if this
// lump version / prop size has none.
static int GetColorOffset( int nVersion, int nStride )
{
	switch ( nVersion )
	{
	case 7:		// L4D v7 (68 bytes); TF2's "v7*" is 72 bytes without color
	case 8:		// L4D v8
		return ( nStride == 68 ) ? 64 : -1;
	case 9:		// L4D2 / Alien Swarm
		return ( nStride == 72 ) ? 64 : -1;
	case 10:	// CS:GO (76 bytes); TF2's v10 is 72 bytes without color
		return ( nStride == 76 ) ? 64 : -1;
	case 11:	// CS:GO (+ uniform scale) and Black Mesa use different layouts; see below
		return ( nStride >= 80 ) ? 64 : -1;
	}
	return -1;
}

static void LoadPropColors( const char *pszMapFile )
{
	ClearPropColors();
	s_nDrawCalls = s_nTintedCalls = s_nResolvedProps = s_nUnmatchedProps = 0;

	if ( !pszMapFile || !pszMapFile[0] )
	{
		SPCM_STATUS( "no level name" );
		return;
	}

	SPCMFile_t *hFile = OpenOriginalMap( pszMapFile );
	if ( !hFile )
	{
		SPCM_STATUS( "%s: couldn't open the map file", pszMapFile );
		return;
	}
	SPCM_STATUS( "%s: opened (%s), %u bytes", pszMapFile, hFile->m_hFile != INVALID_HANDLE_VALUE ? "original file" : "through the filesystem", hFile->m_nSize );

	unsigned int nFileSize = hFile->m_nSize;

	int header[2 + SPCM_HEADER_LUMPS * 4];
	if ( !ReadAt( hFile, 0, header, sizeof( header ) ) || header[0] != SPCM_BSP_IDENT )
	{
		SPCM_STATUS( "%s: couldn't read the BSP header", pszMapFile );
		CloseMap( hFile );
		return;
	}

	// lump_t is { fileofs, filelen, version, fourCC } in v20 and CS:GO maps, but
	// { version, fileofs, filelen, fourCC } in L4D2's v21. Use whichever layout
	// gives an entity lump that starts with '{'.
	const int *pLumps = &header[2];
	int nGameLumpOfs = 0, nGameLumpLen = 0;
	for ( int nLayout = 0; nLayout < 2; nLayout++ )
	{
		int nOfsField = ( nLayout == 0 ) ? 0 : 1;
		int nEntOfs = pLumps[SPCM_LUMP_ENTITIES * 4 + nOfsField];
		int nEntLen = pLumps[SPCM_LUMP_ENTITIES * 4 + nOfsField + 1];
		int nGLOfs = pLumps[SPCM_LUMP_GAME_LUMP * 4 + nOfsField];
		int nGLLen = pLumps[SPCM_LUMP_GAME_LUMP * 4 + nOfsField + 1];

		if ( nEntOfs <= 0 || nEntLen <= 0 || (unsigned int)nEntOfs + (unsigned int)nEntLen > nFileSize )
			continue;
		if ( nGLOfs <= 0 || nGLLen < 4 || (unsigned int)nGLOfs + (unsigned int)nGLLen > nFileSize )
			continue;

		char c = 0;
		if ( !ReadAt( hFile, nEntOfs, &c, 1 ) || c != '{' )
			continue;

		nGameLumpOfs = nGLOfs;
		nGameLumpLen = nGLLen;
		break;
	}

	if ( !nGameLumpLen )
	{
		SPCM_STATUS( "%s: couldn't find the entity/game lumps (header %d, version %d)", pszMapFile, header[0], header[1] );
		CloseMap( hFile );
		return;
	}

	// game lump directory
	struct GameLump_t
	{
		int				m_nId;
		unsigned short	m_nFlags;
		unsigned short	m_nVersion;
		int				m_nFileOfs;
		int				m_nFileLen;
	};

	int nGameLumps = 0;
	ReadAt( hFile, nGameLumpOfs, &nGameLumps, sizeof( int ) );
	if ( nGameLumps <= 0 || nGameLumps > 128 || 4 + nGameLumps * (int)sizeof( GameLump_t ) > nGameLumpLen )
	{
		SPCM_STATUS( "%s: bad game lump directory (%d lumps)", pszMapFile, nGameLumps );
		CloseMap( hFile );
		return;
	}

	CUtlVector< GameLump_t > gameLumps;
	gameLumps.SetCount( nGameLumps );
	ReadAt( hFile, nGameLumpOfs + 4, gameLumps.Base(), nGameLumps * sizeof( GameLump_t ) );

	const GameLump_t *pSprp = NULL;
	const GameLump_t *pSprc = NULL;
	for ( int i = 0; i < nGameLumps; i++ )
	{
		if ( gameLumps[i].m_nId == SPCM_GAMELUMP_SPRP )
			pSprp = &gameLumps[i];
		else if ( gameLumps[i].m_nId == SPCM_GAMELUMP_SPRC )
			pSprc = &gameLumps[i];
	}

	if ( !pSprp || pSprp->m_nFileLen < 12 || pSprp->m_nFileOfs <= 0 ||
		 (unsigned int)pSprp->m_nFileOfs + (unsigned int)pSprp->m_nFileLen > nFileSize )
	{
		SPCM_STATUS( "%s: no usable static prop lump", pszMapFile );
		CloseMap( hFile );
		return;
	}

	if ( pSprp->m_nFlags & SPCM_GAMELUMP_COMPRESSED )
	{
		DevWarning( "Static prop colors: %s has a compressed static prop lump, colors not loaded\n", pszMapFile );
		SPCM_STATUS( "%s: static prop lump is compressed, colors not loaded", pszMapFile );
		CloseMap( hFile );
		return;
	}

	CUtlVector< unsigned char > lumpData;
	lumpData.SetCount( pSprp->m_nFileLen );
	bool bRead = ReadAt( hFile, pSprp->m_nFileOfs, lumpData.Base(), pSprp->m_nFileLen );

	// Colors written by Srcbox's vbsp into a separate lump ('sprc'): int count, then
	// one color32 per prop in static prop lump order. Used when the prop lump itself
	// has no color (TF2 format).
	CUtlVector< color32 > separateColors;
	if ( bRead && pSprc && !( pSprc->m_nFlags & SPCM_GAMELUMP_COMPRESSED ) && pSprc->m_nFileLen >= 4 &&
		 pSprc->m_nFileOfs > 0 && (unsigned int)pSprc->m_nFileOfs + (unsigned int)pSprc->m_nFileLen <= nFileSize )
	{
		int nCount = 0;
		ReadAt( hFile, pSprc->m_nFileOfs, &nCount, sizeof( int ) );
		if ( nCount > 0 && 4 + nCount * (int)sizeof( color32 ) <= pSprc->m_nFileLen )
		{
			separateColors.SetCount( nCount );
			if ( !ReadAt( hFile, pSprc->m_nFileOfs + 4, separateColors.Base(), nCount * sizeof( color32 ) ) )
				separateColors.Purge();
		}
	}

	CloseMap( hFile );
	if ( !bRead )
	{
		SPCM_STATUS( "%s: couldn't read the static prop lump", pszMapFile );
		return;
	}

	// Parse with plain bounds-checked reads. (A READ_ONLY CUtlBuffer reports
	// TellPut() == 0, which made every size check below fail.)
	const unsigned char *pLump = lumpData.Base();
	const int nLumpLen = lumpData.Count();
	int nPos = 0;

	const int nVersion = pSprp->m_nVersion;

	// dictionary
	int nDict = 0;
	if ( nPos + 4 > nLumpLen )
	{
		SPCM_STATUS( "%s: static prop lump is truncated (dictionary)", pszMapFile );
		return;
	}
	memcpy( &nDict, pLump + nPos, 4 ); nPos += 4;
	if ( nDict < 0 || nDict > ( nLumpLen - nPos ) / SPCM_PROP_NAME_LENGTH )
	{
		SPCM_STATUS( "%s: bad static prop dictionary (%d names)", pszMapFile, nDict );
		return;
	}
	s_PropDict.SetCount( nDict );
	for ( int i = 0; i < nDict; i++ )
	{
		char szName[SPCM_PROP_NAME_LENGTH + 1];
		memcpy( szName, pLump + nPos, SPCM_PROP_NAME_LENGTH ); nPos += SPCM_PROP_NAME_LENGTH;
		szName[SPCM_PROP_NAME_LENGTH] = 0;
		V_FixSlashes( szName );
		s_PropDict[i] = szName;
	}

	// leaves
	int nLeaves = 0;
	if ( nPos + 4 > nLumpLen )
	{
		SPCM_STATUS( "%s: static prop lump is truncated (leaves)", pszMapFile );
		return;
	}
	memcpy( &nLeaves, pLump + nPos, 4 ); nPos += 4;
	if ( nLeaves < 0 || nLeaves > ( nLumpLen - nPos ) / 2 )
	{
		SPCM_STATUS( "%s: bad static prop leaf list (%d leaves)", pszMapFile, nLeaves );
		return;
	}
	nPos += nLeaves * 2;

	// props
	int nProps = 0;
	if ( nPos + 4 > nLumpLen )
	{
		SPCM_STATUS( "%s: static prop lump is truncated (props)", pszMapFile );
		return;
	}
	memcpy( &nProps, pLump + nPos, 4 ); nPos += 4;
	int nRemaining = nLumpLen - nPos;
	if ( nProps <= 0 || nRemaining <= 0 || ( nRemaining % nProps ) != 0 )
	{
		SPCM_STATUS( "%s: static prop lump v%d has %d props in %d bytes", pszMapFile, nVersion, nProps, nRemaining );
		return;
	}

	const int nStride = nRemaining / nProps;
	int nColorOfs = GetColorOffset( nVersion, nStride );
	const bool bUseSeparateColors = ( nColorOfs < 0 && separateColors.Count() == nProps );
	if ( nColorOfs < 0 && !bUseSeparateColors )
	{
		if ( separateColors.Count() )
			DevWarning( "Static prop colors: %s has %d prop colors for %d props, ignored\n", pszMapFile, separateColors.Count(), nProps );
		SPCM_STATUS( "%s: static prop lump v%d (%d-byte props) has no colors, and no 'sprc' lump", pszMapFile, nVersion, nStride );
		return;	// no per-prop color in this map
	}

	const unsigned char *pProps = pLump + nPos;

	if ( nVersion == 11 && !bUseSeparateColors )
	{
		// CS:GO v11 keeps the color at 64. Black Mesa's v11 has a 32-bit flags field
		// and the lightmap resolution there, and the color at 72. Vote on which
		// offset holds an opaque alpha.
		int nAlphaAt64 = 0, nAlphaAt72 = 0;
		for ( int i = 0; i < nProps; i++ )
		{
			const unsigned char *pProp = pProps + i * nStride;
			nAlphaAt64 += ( pProp[64 + 3] == 255 );
			nAlphaAt72 += ( pProp[72 + 3] == 255 );
		}
		if ( nAlphaAt72 > nAlphaAt64 )
			nColorOfs = 72;
	}

	bool bAnyColored = false;

	s_PropColors.SetCount( nProps );
	for ( int i = 0; i < nProps; i++ )
	{
		const unsigned char *pProp = pProps + i * nStride;
		StaticPropColor_t &prop = s_PropColors[i];

		memcpy( &prop.m_vecOrigin, pProp + 0, sizeof( Vector ) );
		memcpy( &prop.m_angAngles, pProp + 12, sizeof( QAngle ) );
		prop.m_nDictIndex = *(const unsigned short *)( pProp + 24 );
		if ( bUseSeparateColors )
			prop.m_Color = separateColors[i];
		else
			memcpy( &prop.m_Color, pProp + nColorOfs, sizeof( color32 ) );
		prop.m_bClaimed = false;

		if ( prop.m_Color.r != 255 || prop.m_Color.g != 255 || prop.m_Color.b != 255 )
			bAnyColored = true;

		unsigned int nHash = HashOrigin( prop.m_vecOrigin );
		int nBucket = s_OriginBuckets.Find( nHash );
		if ( s_OriginBuckets.IsValidIndex( nBucket ) )
		{
			prop.m_nNextInBucket = s_OriginBuckets[nBucket];
			s_OriginBuckets[nBucket] = i;
		}
		else
		{
			prop.m_nNextInBucket = -1;
			s_OriginBuckets.Insert( nHash, i );
		}
	}

	if ( !bAnyColored )
	{
		// every prop is white: nothing to do for this map
		SPCM_STATUS( "%s: %d props, all white", pszMapFile, nProps );
		ClearPropColors();
		return;
	}

	s_bHasPropColors = true;
	SPCM_STATUS( "%s: %d props with colors (lump v%d%s)", pszMapFile, nProps, nVersion, bUseSeparateColors ? ", 'sprc' lump" : "" );
	DevMsg( "Static prop colors: loaded %d props (lump v%d) from %s\n", nProps, nVersion, pszMapFile );
}

//-----------------------------------------------------------------------------
// Matching an engine static prop to its lump entry
//-----------------------------------------------------------------------------
static bool ModelNameMatches( IClientRenderable *pRenderable, int nDictIndex )
{
	if ( !s_PropDict.IsValidIndex( nDictIndex ) )
		return false;

	const model_t *pModel = pRenderable->GetModel();
	const char *pszName = pModel ? modelinfo->GetModelName( pModel ) : NULL;
	if ( !pszName )
		return false;

	char szName[MAX_PATH];
	V_strncpy( szName, pszName, sizeof( szName ) );
	V_FixSlashes( szName );
	return V_stricmp( szName, s_PropDict[nDictIndex].Get() ) == 0;
}

static int ResolveProp( IClientRenderable *pRenderable )
{
	const Vector &vecOrigin = pRenderable->GetRenderOrigin();
	const QAngle &angAngles = pRenderable->GetRenderAngles();

	int nBucket = s_OriginBuckets.Find( HashOrigin( vecOrigin ) );
	if ( !s_OriginBuckets.IsValidIndex( nBucket ) )
		return -1;

	int nFallback = -1;
	for ( int i = s_OriginBuckets[nBucket]; i != -1; i = s_PropColors[i].m_nNextInBucket )
	{
		StaticPropColor_t &prop = s_PropColors[i];
		if ( prop.m_bClaimed || !VectorsAreEqual( prop.m_vecOrigin, vecOrigin, 0.01f ) )
			continue;

		if ( QAnglesAreEqual( prop.m_angAngles, angAngles, 0.01f ) && ModelNameMatches( pRenderable, prop.m_nDictIndex ) )
		{
			prop.m_bClaimed = true;
			return i;
		}

		if ( nFallback == -1 )
			nFallback = i;
	}

	if ( nFallback != -1 )
		s_PropColors[nFallback].m_bClaimed = true;
	return nFallback;
}

// Returns true (and the color) if this static prop should be drawn tinted.
static bool GetStaticPropColor( const DrawModelInfo_t &drawInfo, Vector &vecColor )
{
	s_nDrawCalls++;
	if ( !s_bHasPropColors || !r_staticprop_colormod.GetBool() || !drawInfo.m_pClientEntity )
		return false;

	IClientRenderable *pRenderable = static_cast< IClientRenderable * >( drawInfo.m_pClientEntity );

	int nProp;
	int nCached = s_ResolvedProps.Find( pRenderable );
	if ( s_ResolvedProps.IsValidIndex( nCached ) )
	{
		nProp = s_ResolvedProps[nCached];
	}
	else
	{
		nProp = ResolveProp( pRenderable );
		s_ResolvedProps.Insert( pRenderable, nProp );
		if ( nProp >= 0 )
			s_nResolvedProps++;
		else
			s_nUnmatchedProps++;
	}

	if ( nProp < 0 )
		return false;

	const color32 &c = s_PropColors[nProp].m_Color;
	if ( c.r == 255 && c.g == 255 && c.b == 255 )
		return false;

	vecColor.Init( c.r / 255.0f, c.g / 255.0f, c.b / 255.0f );
	s_nTintedCalls++;
	return true;
}

//-----------------------------------------------------------------------------
// Detours
//-----------------------------------------------------------------------------
typedef void ( __fastcall *DrawStaticPropFn_t )( IStudioRender *pThis, const DrawModelInfo_t &drawInfo, const matrix3x4_t &modelToWorld, int flags );

static DrawStaticPropFn_t s_pfnOrigDrawModelStaticProp = NULL;
static DrawStaticPropFn_t s_pfnOrigDrawStaticPropShadows = NULL;

static const Vector s_vecWhite( 1.0f, 1.0f, 1.0f );

static void __fastcall Detour_DrawModelStaticProp( IStudioRender *pThis, const DrawModelInfo_t &drawInfo, const matrix3x4_t &modelToWorld, int flags )
{
	Vector vecColor;
	if ( !GetStaticPropColor( drawInfo, vecColor ) )
	{
		s_pfnOrigDrawModelStaticProp( pThis, drawInfo, modelToWorld, flags );
		return;
	}

	pThis->SetColorModulation( vecColor.Base() );
	s_pfnOrigDrawModelStaticProp( pThis, drawInfo, modelToWorld, flags );
	// IStudioRender has no getter; the engine draws static props with white modulation
	pThis->SetColorModulation( s_vecWhite.Base() );
}

static void __fastcall Detour_DrawStaticPropShadows( IStudioRender *pThis, const DrawModelInfo_t &drawInfo, const matrix3x4_t &modelToWorld, int flags )
{
	Vector vecColor;
	if ( !GetStaticPropColor( drawInfo, vecColor ) )
	{
		s_pfnOrigDrawStaticPropShadows( pThis, drawInfo, modelToWorld, flags );
		return;
	}

	pThis->SetColorModulation( vecColor.Base() );
	s_pfnOrigDrawStaticPropShadows( pThis, drawInfo, modelToWorld, flags );
	pThis->SetColorModulation( s_vecWhite.Base() );
}

//-----------------------------------------------------------------------------
// Finds the vtable slot the compiler uses for a virtual member function, by
// reading the MSVC x64 vcall thunk that a pointer-to-virtual-member points at.
// This keeps the slots in step with istudiorender.h (including MSVC's reversed
// ordering of overloads) without hard-coding numbers.
//-----------------------------------------------------------------------------
template < typename PMF >
static int GetVTableIndex( PMF pmf )
{
	static_assert( sizeof( PMF ) == sizeof( void * ), "unexpected member function pointer size" );
	const unsigned char *p = *reinterpret_cast< const unsigned char ** >( &pmf );

	// incremental linking: jmp rel32 to the real thunk
	for ( int i = 0; i < 4 && p[0] == 0xE9; i++ )
		p = p + 5 + *reinterpret_cast< const int * >( p + 1 );

	// mov rax, [rcx]
	if ( p[0] != 0x48 || p[1] != 0x8B || p[2] != 0x01 )
		return -1;
	p += 3;

	// jmp qword ptr [rax + disp]
	if ( p[0] == 0xFF && p[1] == 0x60 ) return p[2] / sizeof( void * );
	if ( p[0] == 0xFF && p[1] == 0xA0 ) return *reinterpret_cast< const int * >( p + 2 ) / sizeof( void * );
	if ( p[0] == 0xFF && p[1] == 0x20 ) return 0;

	// /guard:cf thunk: mov rax, qword ptr [rax + disp]; jmp __guard_dispatch_icall_fptr
	if ( p[0] == 0x48 && p[1] == 0x8B && p[2] == 0x40 ) return p[3] / sizeof( void * );
	if ( p[0] == 0x48 && p[1] == 0x8B && p[2] == 0x80 ) return *reinterpret_cast< const int * >( p + 3 ) / sizeof( void * );
	if ( p[0] == 0x48 && p[1] == 0x8B && p[2] == 0x00 ) return 0;

	return -1;
}

void StaticPropColorMod_Install( CreateInterfaceFn appSystemFactory )
{
	static bool s_bInstalled = false;
	if ( s_bInstalled )
		return;
	s_bInstalled = true;

	IStudioRender *pStudioRender = (IStudioRender *)appSystemFactory( STUDIO_RENDER_INTERFACE_VERSION, NULL );
	if ( !pStudioRender )
	{
		Warning( "Static prop colors: couldn't get %s\n", STUDIO_RENDER_INTERFACE_VERSION );
		return;
	}

	// Checked against the TF2 SDK x64 studiorender.dll: SetColorModulation 27, DrawModelStaticProp 30, DrawStaticPropShadows 32
	int nDrawSlot = GetVTableIndex( &IStudioRender::DrawModelStaticProp );
	int nShadowSlot = GetVTableIndex( &IStudioRender::DrawStaticPropShadows );
	if ( nDrawSlot < 0 )
		nDrawSlot = 30;
	if ( nShadowSlot < 0 )
		nShadowSlot = 32;

	MH_STATUS status = MH_Initialize();
	if ( status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED )
	{
		Warning( "Static prop colors: MinHook failed to initialize (%d)\n", (int)status );
		return;
	}

	void **pVTable = *reinterpret_cast< void *** >( pStudioRender );
	void *pDraw = pVTable[nDrawSlot];
	void *pShadows = pVTable[nShadowSlot];

	if ( MH_CreateHook( pDraw, (void *)&Detour_DrawModelStaticProp, reinterpret_cast< void ** >( &s_pfnOrigDrawModelStaticProp ) ) != MH_OK ||
		 MH_CreateHook( pShadows, (void *)&Detour_DrawStaticPropShadows, reinterpret_cast< void ** >( &s_pfnOrigDrawStaticPropShadows ) ) != MH_OK )
	{
		Warning( "Static prop colors: couldn't hook IStudioRender\n" );
		MH_RemoveHook( pDraw );
		MH_RemoveHook( pShadows );
		return;
	}

	MH_STATUS nEnable1 = MH_EnableHook( pDraw );
	MH_STATUS nEnable2 = MH_EnableHook( pShadows );
	s_bHookInstalled = ( nEnable1 == MH_OK && nEnable2 == MH_OK );
	if ( !s_bHookInstalled )
		Warning( "Static prop colors: couldn't enable the IStudioRender hooks (%d, %d)\n", (int)nEnable1, (int)nEnable2 );
	DevMsg( "Static prop colors: hooked IStudioRender slots %d and %d\n", nDrawSlot, nShadowSlot );
}

CON_COMMAND( r_staticprop_colormod_info, "Shows what the static prop color code found for this map" )
{
	Msg( "Static prop colors:\n" );
	Msg( "  hooks installed: %s\n", s_bHookInstalled ? "yes" : "NO" );
	Msg( "  map: %s\n", s_szLoadStatus );
	Msg( "  r_staticprop_colormod: %d\n", r_staticprop_colormod.GetInt() );
	Msg( "  static prop draws seen: %d, tinted: %d\n", s_nDrawCalls, s_nTintedCalls );
	Msg( "  props matched to the map: %d, not matched: %d\n", s_nResolvedProps, s_nUnmatchedProps );
}

// Lists the materials of every tinted model: whether each will mask the color by
// its base texture's alpha ($blendtintbybasealpha), and which file it came from.
CON_COMMAND( r_staticprop_colormod_materials, "Lists the materials of tinted static prop models (optional: a model name filter)" )
{
	const char *pszFilter = ( args.ArgC() > 1 ) ? args[1] : NULL;

	for ( int d = 0; d < s_PropDict.Count(); d++ )
	{
		const char *pszModel = s_PropDict[d].Get();
		if ( pszFilter && !V_stristr( pszModel, pszFilter ) )
			continue;

		// the distinct colors this model is tinted with
		CUtlVector< color32 > colors;
		for ( int i = 0; i < s_PropColors.Count(); i++ )
		{
			const color32 &c = s_PropColors[i].m_Color;
			if ( s_PropColors[i].m_nDictIndex != d || ( c.r == 255 && c.g == 255 && c.b == 255 ) )
				continue;
			bool bSeen = false;
			for ( int k = 0; k < colors.Count() && !bSeen; k++ )
				bSeen = ( colors[k].r == c.r && colors[k].g == c.g && colors[k].b == c.b );
			if ( !bSeen )
				colors.AddToTail( c );
		}
		if ( !colors.Count() )
			continue;

		Msg( "%s:", pszModel );
		for ( int k = 0; k < colors.Count() && k < 6; k++ )
			Msg( " (%d %d %d)", colors[k].r, colors[k].g, colors[k].b );
		Msg( "%s\n", colors.Count() > 6 ? " ..." : "" );

		char szModel[MAX_PATH];
		V_strncpy( szModel, pszModel, sizeof( szModel ) );
		V_FixSlashes( szModel, '/' );
		const int nIndex = modelinfo->GetModelIndex( szModel );
		const model_t *pModel = ( nIndex >= 0 ) ? modelinfo->GetModel( nIndex ) : NULL;
		if ( !pModel )
			pModel = modelinfo->FindOrLoadModel( szModel );	// static prop models aren't in the precache table
		if ( !pModel )
		{
			Msg( "    (model not loaded)\n" );
			continue;
		}

		// The model's textures, resolved like the engine does: the first $cdmaterials
		// folder that has the material wins
		const studiohdr_t *pStudio = modelinfo->GetStudiomodel( pModel );
		if ( !pStudio )
		{
			Msg( "    (no studio header)\n" );
			continue;
		}
		Msg( "    %d textures, %d skin families\n", pStudio->numtextures, pStudio->numskinfamilies );

		for ( int m = 0; m < pStudio->numtextures; m++ )
		{
			IMaterial *pMaterial = NULL;
			for ( int cd = 0; cd < pStudio->numcdtextures && !pMaterial; cd++ )
			{
				char szName[MAX_PATH];
				V_ComposeFileName( pStudio->pCdtexture( cd ), pStudio->pTexture( m )->pszName(), szName, sizeof( szName ) );
				IMaterial *pTry = materials->FindMaterial( szName, TEXTURE_GROUP_MODEL, false );
				if ( pTry && !pTry->IsErrorMaterial() )
					pMaterial = pTry;
			}
			if ( !pMaterial )
			{
				Msg( "    %s: not found in any $cdmaterials folder\n", pStudio->pTexture( m )->pszName() );
				continue;
			}

			bool bFound = false;
			IMaterialVar *pBlend = pMaterial->FindVar( "$blendtintbybasealpha", &bFound, false );
			const char *pszBlend = ( bFound && pBlend ) ? ( pBlend->GetIntValue() ? "on" : "off" ) : "not set";

			char szVmt[MAX_PATH], szPath[MAX_PATH];
			V_snprintf( szVmt, sizeof( szVmt ), "materials/%s.vmt", pMaterial->GetName() );
			if ( !filesystem->RelativePathToFullPath( szVmt, "GAME", szPath, sizeof( szPath ) ) )
				V_strncpy( szPath, "?", sizeof( szPath ) );

			Msg( "    %s  [%s] blendtint %s%s%s\n        from %s\n", pMaterial->GetName(), pMaterial->GetShaderName(), pszBlend,
				pMaterial->GetMaterialVarFlag( MATERIAL_VAR_SELFILLUM ) ? ", selfillum (disables blendtint)" : "",
				pMaterial->IsErrorMaterial() ? ", ERROR MATERIAL" : "", szPath );
		}
	}
}

//-----------------------------------------------------------------------------
// Level load / unload
//-----------------------------------------------------------------------------
class CStaticPropColorModSystem : public CAutoGameSystem
{
public:
	CStaticPropColorModSystem() : CAutoGameSystem( "CStaticPropColorModSystem" ) {}

	virtual void LevelInitPreEntity()
	{
		LoadPropColors( engine->GetLevelName() );
	}

	virtual void LevelShutdownPostEntity()
	{
		ClearPropColors();
	}
};

static CStaticPropColorModSystem s_StaticPropColorModSystem;
