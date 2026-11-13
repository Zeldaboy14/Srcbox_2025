//========= Srcbox ============================================================//
//
// Purpose: Loading newer BSPs (version 21: L4D2, Alien Swarm, Portal 2, CS:GO)
//          on the TF2 SDK engine, in real time, the way the BSPX plugin meant to.
//
// The engine and filesystem_stdio only accept version 19-20 maps and read
// several lumps in their older layouts. Rather than patching each place they
// do that (version checks in engine.dll and filesystem_stdio.dll, the lump
// table, the static prop reader, ...), the map file itself is redirected:
// whenever any code in the process opens a .bsp for reading, the open goes
// through a hook on the Windows file API, and if the map needs it, every read
// from that handle is served from a "virtual" version 20 view of the file.
// Nothing is written to disk.
//
// The view is the original file with a few ranges overlaid:
//  - the header: version 20, lump table in the v20 order { fileofs, filelen,
//    version, uncompressedSize } (L4D2 stores { version, fileofs, filelen,
//    fourCC }); lumps the TF2 engine doesn't know are emptied (static prop
//    collision 22-25 and 49, overlay system levels 61, physlevel 62, CS:GO
//    displacement multiblend 63)
//  - LUMP_WORLDLIGHTS / _HDR version 1 (100 bytes, with shadow_cast_offset)
//    rewritten in place as version 0 (88 bytes)
//  - LUMP_BRUSHSIDES: the 'thin' byte next to 'bevel' (ASW/P2/CS:GO) cleared;
//    the TF2 engine reads bevel as a short
//  - the static prop game lump rewritten in the TF2 v10 layout, from v7/v8
//    (L4D), v9 (L4D2/ASW/P2) and v10/v11 (CS:GO, Black Mesa). It goes in place
//    when it fits (v9-v11); L4D's 68-byte props grow, so those go past the end
//    of the file, and the view reports the larger size.
//  - Black Mesa's HDR cubemaps in the pak lump ( materials/maps/<map>/c*.hdr.vtf ):
//    16 bit integer texels labelled as half floats are converted to half floats
//    in place ( same size ), see FixIntegerHDRCubemaps. This one also applies to
//    version 19/20 maps, which otherwise get no view.
// Every other lump, including the pak lump, keeps its original offset, so
// the engine's map loader, the map CRC and filesystem_stdio's pak mount all
// read the same bytes as the original file. Per-prop colors are read from the
// original file by staticprop_colormod.cpp (opened with BSPCOMPAT_BYPASS_ATTRIBUTE).
//
// Version 18 maps (Half-Life 2, 2003 era) get a view too: their 72-byte faces
// lose a 16-byte prefix, their leaves are marked as the 32-byte kind, and the
// leaf ambient lighting lumps they never had are made from their lightmaps
// (see ConvertVersion18).
//
// Models go through the same redirect: a version 37 .mdl (the 2003 Half-Life
// 2 beta) is read as a version 48 model, its .vvd (v37 has none: the vertices
// are in the .mdl) is made up from the model, and its version 6 .vtx files
// are read as version 7. A .dx90.vtx the model doesn't have comes from its
// .dx80.vtx (or .sw.vtx). The conversion itself is in mdl_compat.h.
//
// Maps inside VPKs aren't opened as files: filesystem_stdio reads them out of
// the VPK's archive chunks. A chunk that holds maps is tracked instead, and each
// map's view is built the first time a read reaches its bytes and laid over the
// chunk at the map's offset ( see "Maps inside VPKs" ). Views that would make a
// map bigger ( L4D static props, version 18 ) don't fit there and are skipped.
// The TF2 filesystem only mounts a map's pak lump from a .bsp on disk, so a map
// inside a VPK is also served as a file when its .bsp isn't on disk ( see
// "Maps inside VPKs, as files" ), which gets its pak mounted.
//
// Hooked (KernelBase): CreateFileW/A, ReadFile, SetFilePointer(Ex),
// GetFileSize(Ex), GetFileInformationByHandle(Ex), CloseHandle,
// GetFileAttributes(Ex)W/A, FindFirstFileExW/A, FindNextFileW/A, FindClose.
// Untracked handles pass straight through. Other files inside VPKs aren't
// redirected.
//
// The same hooks serve Garry's Mod addons (.gma) mounted in place; see
// gma_mount.cpp.
//
//=============================================================================//

#include "cbase.h"
#include "tier1/utlbuffer.h"
#include "bsp_compat.h"
#include "mdl_compat.h"
#include "gma_mount.h"
#include "tier1/checksum_crc.h"
#include "tier0/icommandline.h"

#include "MinHook.h"
#undef CreateEvent

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

// ---- core (no Windows API; also built by the test harness) -------------------

#define BC_IDENT					( ( 'P' << 24 ) + ( 'S' << 16 ) + ( 'B' << 8 ) + 'V' )
#define BC_HEADER_LUMPS				64
#define BC_HEADER_SIZE				( 8 + BC_HEADER_LUMPS * 16 + 4 )

#define BC_LUMP_ENTITIES			0
#define BC_LUMP_WORLDLIGHTS			15
#define BC_LUMP_BRUSHSIDES			19
#define BC_LUMP_GAME_LUMP			35
#define BC_LUMP_WORLDLIGHTS_HDR		54
#define BC_LUMP_PAKFILE				40

// used by the version 18 conversion
#define BC_LUMP_TEXINFO				6
#define BC_LUMP_FACES				7
#define BC_LUMP_EDGES				12
#define BC_LUMP_SURFEDGES			13
#define BC_LUMP_VERTNORMALINDICES	31
#define BC_LUMP_LIGHTING			8
#define BC_LUMP_VISIBILITY			4
#define BC_LUMP_LEAFS				10
#define BC_LUMP_LEAFFACES			16
#define BC_LUMP_ORIGINALFACES		27
#define BC_LUMP_LEAF_AMBIENT_INDEX	52
#define BC_LUMP_LEAF_AMBIENT_LIGHTING	56

#define BC_FACE_V18_SIZE			72	// 16 bytes of per-style average colors + the v20 face
#define BC_FACE_SIZE				56
#define BC_LEAF_SIZE				32
#define BC_AMBIENT_SAMPLE_SIZE		28	// CompressedLightCube + x, y, z, pad
#define BC_CONTENTS_SOLID			0x1
#define BC_TEXINFO_SIZE				72
#define BC_SURF_HIDDEN				( 0x80 | 0x100 | 0x200 )	// SURF_NODRAW | SURF_HINT | SURF_SKIP
#define BC_STATIC_PROP_NO_PER_VERTEX_LIGHTING	0x40
#define BC_SURF_SKY2D				0x2
#define BC_SURF_SKY					0x4
#define BC_SURF_BUMPLIGHT			0x800
#define BC_LEAF_FLAGS_SKY			0x01
#define BC_LEAF_FLAGS_SKY2D			0x04

// vrad stores leaf ambient light in 0-1 units, lightmaps in 0-255. The ratio of
// leaf ambient to the average lightmap around it in a v20 recompile of a v18 map
// (d1_garage_01) is about 0.5/255: vrad's ambient is bounced light only, the
// engine adds direct light from the world lights itself.
#define BC_AMBIENT_FROM_LIGHTMAP_SCALE	( 0.5f / 255.0f )

#define BC_GAMELUMP_SPRP			( ( 's' << 24 ) + ( 'p' << 16 ) + ( 'r' << 8 ) + 'p' )
#define BC_GAMELUMP_COMPRESSED		0x0001

#define BC_WORLDLIGHT_V0_SIZE		88
#define BC_WORLDLIGHT_V1_SIZE		100

#define BC_STATICPROP_TF2_SIZE		72
#define BC_STATICPROP_NAME_LENGTH	128

typedef long long bc_int64;

// Reads nSize bytes at nOffset of the original file
typedef bool ( *BCReadFn_t )( void *pContext, bc_int64 nOffset, void *pDest, int nSize );

static bool IsDroppedLump( int nLump )
{
	switch ( nLump )
	{
	case 22: case 23: case 24: case 25:	// static prop collision (ASW+)
	case 49:							// static prop blob (ASW+); TF2: deprecated PHYSCOLLIDESURFACE
	case 61:							// overlay system levels
	case 62:							// physlevel
	case 63:							// displacement multiblend (CS:GO)
		return true;
	}
	return false;
}

struct BCLump_t
{
	int m_nOfs;
	int m_nLen;
	int m_nVersion;
	int m_nUncompressed;
};

static void ReadLumpTable( const int *pLumpInts, bool bL4D2Order, BCLump_t *pLumps )
{
	for ( int i = 0; i < BC_HEADER_LUMPS; i++ )
	{
		const int *p = pLumpInts + i * 4;
		if ( bL4D2Order )
		{
			pLumps[i].m_nVersion = p[0];
			pLumps[i].m_nOfs = p[1];
			pLumps[i].m_nLen = p[2];
			pLumps[i].m_nUncompressed = 0;	// fourCC, unused
		}
		else
		{
			pLumps[i].m_nOfs = p[0];
			pLumps[i].m_nLen = p[1];
			pLumps[i].m_nVersion = p[2];
			pLumps[i].m_nUncompressed = p[3];
		}
	}
}

//-----------------------------------------------------------------------------
// World lights v1 (100 bytes) -> v0 (88): drops shadow_cast_offset (bytes 36-47)
//-----------------------------------------------------------------------------
static void ConvertWorldLightsV1( const unsigned char *pSrc, int nCount, unsigned char *pDst )
{
	for ( int k = 0; k < nCount; k++ )
	{
		const unsigned char *pFrom = pSrc + k * BC_WORLDLIGHT_V1_SIZE;
		unsigned char *pTo = pDst + k * BC_WORLDLIGHT_V0_SIZE;
		memcpy( pTo, pFrom, 36 );
		memcpy( pTo + 36, pFrom + 48, BC_WORLDLIGHT_V1_SIZE - 48 );
	}
}

//-----------------------------------------------------------------------------
// Static props -> TF2 v10 (72 bytes)
//-----------------------------------------------------------------------------

// The layout of one serialized prop of this lump version and size: where its
// color is, and whether its flags are 32-bit (Black Mesa). False for the
// layouts the engine reads itself (v4-v6, TF2's own 72-byte v7 and v10).
static bool GetStaticPropLayout( int nVersion, int nStride, const unsigned char *pProps, int nProps, int &nColorOfs, bool &bFlags32 )
{
	nColorOfs = -1;
	bFlags32 = false;
	if ( ( nVersion == 7 || nVersion == 8 ) && nStride == 68 )
		nColorOfs = 64;		// L4D
	else if ( nVersion == 9 && nStride == 72 )
		nColorOfs = 64;		// L4D2 / ASW / Portal 2
	else if ( nVersion == 10 && nStride == 76 )
		nColorOfs = 64;		// CS:GO
	else if ( nVersion == 11 && nStride >= 80 )
	{
		// CS:GO keeps the color at 64; Black Mesa has 32-bit flags at 64 and the
		// color at 72. Vote on which offset holds an opaque alpha.
		int nAlpha64 = 0, nAlpha72 = 0;
		for ( int i = 0; i < nProps; i++ )
		{
			nAlpha64 += ( pProps[i * nStride + 67] == 255 );
			nAlpha72 += ( pProps[i * nStride + 75] == 255 );
		}
		nColorOfs = ( nAlpha72 > nAlpha64 ) ? 72 : 64;
		bFlags32 = ( nColorOfs == 72 );
	}
	else
	{
		return false;
	}
	return true;
}

// One prop in any of the layouts above -> TF2's 72 bytes
static void ConvertStaticProp( const unsigned char *pSrc, bool bFlags32, unsigned char *pDst )
{
	memset( pDst, 0, BC_STATICPROP_TF2_SIZE );

	// origin, angles, proptype, firstleaf, leafcount, solid: 0-30
	memcpy( pDst, pSrc, 31 );
	// skin, fade min/max, lighting origin, forced fade scale: 32-59
	memcpy( pDst + 32, pSrc + 32, 28 );
	// 60-63: min/max DX level = 0 (no limit). The source CPU/GPU levels have no TF2 equivalent.

	unsigned int nFlags = bFlags32 ? *(const unsigned int *)( pSrc + 64 ) : pSrc[31];
	// keep FADES, USE_LIGHTING_ORIGIN, IGNORE_NORMALS, NO_SHADOW, NO_PER_VERTEX_LIGHTING,
	// NO_SELF_SHADOWING; NO_DRAW is computed at load and 0x20 is unused in these
	// formats (TF2: screen space fade). No per-texel lightmaps in these maps.
	nFlags = ( nFlags & 0xDB ) | 0x100;
	memcpy( pDst + 64, &nFlags, 4 );
	// 68-71: lightmap resolution = 0
}

static bool ConvertStaticProps( const unsigned char *pData, int nLen, int nVersion, CUtlBuffer &out, CUtlVector< color32 > &colors )
{
	CUtlBuffer in( pData, nLen, CUtlBuffer::READ_ONLY );

	int nDict = in.GetInt();
	if ( nDict < 0 || in.TellGet() + nDict * BC_STATICPROP_NAME_LENGTH > nLen )
		return false;
	in.SeekGet( CUtlBuffer::SEEK_CURRENT, nDict * BC_STATICPROP_NAME_LENGTH );

	int nLeaves = in.GetInt();
	if ( nLeaves < 0 || in.TellGet() + nLeaves * 2 > nLen )
		return false;
	in.SeekGet( CUtlBuffer::SEEK_CURRENT, nLeaves * 2 );

	int nProps = in.GetInt();
	if ( !in.IsValid() || nProps < 0 )
		return false;

	const int nHeadLen = in.TellGet();
	const int nRemaining = nLen - nHeadLen;
	if ( nProps == 0 || nRemaining <= 0 || ( nRemaining % nProps ) != 0 )
		return false;

	const int nStride = nRemaining / nProps;
	const unsigned char *pProps = pData + nHeadLen;

	int nColorOfs;
	bool bFlags32;
	if ( !GetStaticPropLayout( nVersion, nStride, pProps, nProps, nColorOfs, bFlags32 ) )
		return false;	// v4-v6 and TF2's own v10 are read by the engine as they are

	out.Put( pData, nHeadLen );
	colors.SetCount( nProps );

	for ( int i = 0; i < nProps; i++ )
	{
		const unsigned char *pSrc = pProps + i * nStride;
		unsigned char dst[BC_STATICPROP_TF2_SIZE];
		ConvertStaticProp( pSrc, bFlags32, dst );
		out.Put( dst, sizeof( dst ) );
		memcpy( &colors[i], pSrc + nColorOfs, sizeof( color32 ) );
	}

	return true;
}

//-----------------------------------------------------------------------------
// The virtual view of one map file
//-----------------------------------------------------------------------------
struct BCPatch_t
{
	bc_int64					m_nOffset;
	CUtlVector< unsigned char >	m_Data;
};

struct BCVirtualMap_t
{
	bc_int64					m_nOriginalSize;
	bc_int64					m_nVirtualSize;
	CUtlVector< BCPatch_t * >	m_Patches;		// sorted by offset, not overlapping

	~BCVirtualMap_t() { m_Patches.PurgeAndDeleteElements(); }
};

static BCPatch_t *AddPatch( BCVirtualMap_t *pMap, bc_int64 nOffset, const void *pData, int nSize, int nPadTo = 0 )
{
	BCPatch_t *pPatch = new BCPatch_t;
	pPatch->m_nOffset = nOffset;
	pPatch->m_Data.SetCount( MAX( nSize, nPadTo ) );
	if ( nSize > 0 )
		memcpy( pPatch->m_Data.Base(), pData, nSize );
	if ( nPadTo > nSize )
		memset( pPatch->m_Data.Base() + nSize, 0, nPadTo - nSize );

	int nInsert = 0;
	while ( nInsert < pMap->m_Patches.Count() && pMap->m_Patches[nInsert]->m_nOffset < nOffset )
		nInsert++;
	pMap->m_Patches.InsertBefore( nInsert, pPatch );
	return pPatch;
}

static bool ReadLump( BCReadFn_t pfnRead, void *pContext, const BCLump_t &lump, CUtlVector< unsigned char > &data )
{
	data.SetCount( lump.m_nLen );
	return lump.m_nLen <= 0 || pfnRead( pContext, lump.m_nOfs, data.Base(), lump.m_nLen );
}

//-----------------------------------------------------------------------------
// Version 18 (Half-Life 2 2003 era)
//
// Faces carry 16 extra bytes in front (average light color per style), and
// leaves are the 32-byte kind but with no ambient lighting anywhere: v19 kept
// it inside 56-byte leaves, v20 in LUMP_LEAF_AMBIENT_INDEX/_LIGHTING. Those two
// lumps are made here, one sample per leaf, from the average of the style-0
// lightmaps of the leaf's faces (falling back to the leaf's cluster, then the
// whole map). It's a flat ambient term, not what vrad would compute, but
// models get lit instead of black.
//
// The 2003 compiler also wrote faces that modern vbsp leaves out: nodraw
// (toolsnodraw, toolsinvisible, clips...), hint and skip. The engine doesn't
// check SURF_NODRAW when it loads faces (Mod_LoadFaces maps NOLIGHT, NOSHADOWS,
// WARP and SKY only), so those would be drawn. They're turned into a zero-area
// triangle instead (three surfedges on the dummy edge 0), which keeps every
// face index valid; LUMP_VERTNORMALINDICES, which holds one entry per face
// edge in face order, is rebuilt to match.
//
// Static props of that era have no per-vertex lighting files (sp_*.vhv), so
// they're flagged STATIC_PROP_NO_PER_VERTEX_LIGHTING and get lit like models.
//-----------------------------------------------------------------------------
static void ColorRGBExpToLinear( const unsigned char *pColor, float *pOut )
{
	const float flScale = ldexpf( 1.0f, (signed char)pColor[3] );
	pOut[0] = pColor[0] * flScale;
	pOut[1] = pColor[1] * flScale;
	pOut[2] = pColor[2] * flScale;
}

static void LinearToColorRGBExp( const float *pIn, unsigned char *pColor )
{
	float flMax = MAX( pIn[0], MAX( pIn[1], pIn[2] ) );
	if ( flMax <= 0.0f )
	{
		pColor[0] = pColor[1] = pColor[2] = 0;
		pColor[3] = 0;
		return;
	}
	int nExp = 0;
	frexpf( flMax / 255.0f, &nExp );	// flMax / 2^nExp <= 255
	nExp = MAX( -128, MIN( 127, nExp ) );
	const float flScale = ldexpf( 1.0f, -nExp );
	for ( int i = 0; i < 3; i++ )
		pColor[i] = (unsigned char)MIN( 255.0f, pIn[i] * flScale + 0.5f );
	pColor[3] = (unsigned char)(signed char)nExp;
}

//-----------------------------------------------------------------------------
// v18: makes nodraw/hint/skip faces zero-area triangles (see above).
// pFaces is the 56-byte face array, changed in place.
//-----------------------------------------------------------------------------
static int HideToolFaces( BCReadFn_t pfnRead, void *pContext, BCLump_t *pLumps, BCVirtualMap_t *pMap,
	CUtlVector< unsigned char > &faces, const char *pszName )
{
	BCLump_t &surfedgesLump = pLumps[BC_LUMP_SURFEDGES];
	BCLump_t &normalsLump = pLumps[BC_LUMP_VERTNORMALINDICES];
	const BCLump_t &edgesLump = pLumps[BC_LUMP_EDGES];
	if ( surfedgesLump.m_nUncompressed || normalsLump.m_nUncompressed || edgesLump.m_nUncompressed || edgesLump.m_nLen < 8 )
		return 0;

	CUtlVector< unsigned char > texinfo, surfedges, normals;
	if ( !ReadLump( pfnRead, pContext, pLumps[BC_LUMP_TEXINFO], texinfo ) || !ReadLump( pfnRead, pContext, surfedgesLump, surfedges ) ||
		 !ReadLump( pfnRead, pContext, normalsLump, normals ) )
		return 0;

	unsigned short edge0[2];
	if ( !pfnRead( pContext, edgesLump.m_nOfs, edge0, sizeof( edge0 ) ) || edge0[0] != edge0[1] )
	{
		Warning( "BSP v18: %s: edge 0 isn't a point, tool faces stay visible\n", pszName );
		return 0;
	}

	const int nFaces = faces.Count() / BC_FACE_SIZE;
	const int nTexinfo = texinfo.Count() / BC_TEXINFO_SIZE;
	const int nSurfedges = surfedges.Count() / 4;
	const int nNormals = normals.Count() / 2;

	// which faces, and one of them whose first three surfedges can become { 0, 0, 0 }
	CUtlVector< unsigned char > hide;
	hide.SetCount( nFaces );
	int nEdgeTotal = 0, nHidden = 0, nShared = -1;
	for ( int i = 0; i < nFaces; i++ )
	{
		const unsigned char *pFace = faces.Base() + i * BC_FACE_SIZE;
		const int nFirstEdge = *(const int *)( pFace + 4 );
		const short nNumEdges = *(const short *)( pFace + 8 );
		const short nTexinfoIndex = *(const short *)( pFace + 10 );
		const short nDispinfo = *(const short *)( pFace + 12 );
		nEdgeTotal += nNumEdges;

		hide[i] = ( nTexinfoIndex >= 0 && nTexinfoIndex < nTexinfo && nDispinfo == -1 && nNumEdges >= 3 &&
			( *(const int *)( texinfo.Base() + nTexinfoIndex * BC_TEXINFO_SIZE + 64 ) & BC_SURF_HIDDEN ) ) ? 1 : 0;
		if ( hide[i] )
		{
			nHidden++;
			if ( nShared < 0 && nFirstEdge >= 0 && nFirstEdge + 3 <= nSurfedges )
				nShared = nFirstEdge;
		}
	}

	if ( !nHidden || nShared < 0 )
		return 0;
	if ( nEdgeTotal != nNormals )
	{
		Warning( "BSP v18: %s: vertex normal indices don't match the faces, tool faces stay visible\n", pszName );
		return 0;
	}

	// surfedges: the shared degenerate triangle (same size, in place)
	memset( surfedges.Base() + nShared * 4, 0, 12 );
	AddPatch( pMap, surfedgesLump.m_nOfs, surfedges.Base(), surfedges.Count() );

	// faces and vertex normal indices
	CUtlVector< unsigned char > newNormals;
	newNormals.SetCount( normals.Count() );
	int nIn = 0, nOut = 0;
	for ( int i = 0; i < nFaces; i++ )
	{
		unsigned char *pFace = faces.Base() + i * BC_FACE_SIZE;
		const short nNumEdges = *(const short *)( pFace + 8 );
		const int nKeep = hide[i] ? 3 : nNumEdges;
		memcpy( newNormals.Base() + nOut * 2, normals.Base() + nIn * 2, nKeep * 2 );
		nIn += nNumEdges;
		nOut += nKeep;
		if ( hide[i] )
		{
			*(int *)( pFace + 4 ) = nShared;
			*(short *)( pFace + 8 ) = 3;
		}
	}
	AddPatch( pMap, normalsLump.m_nOfs, newNormals.Base(), nOut * 2, normalsLump.m_nLen );
	normalsLump.m_nLen = nOut * 2;
	return nHidden;
}

//-----------------------------------------------------------------------------
// v18: static props get STATIC_PROP_NO_PER_VERTEX_LIGHTING (no sp_*.vhv files)
//-----------------------------------------------------------------------------
static void FlagStaticPropsUnlit( BCReadFn_t pfnRead, void *pContext, const BCLump_t &gameLump, BCVirtualMap_t *pMap )
{
	if ( gameLump.m_nLen < 4 || gameLump.m_nUncompressed )
		return;

	CUtlVector< unsigned char > dir;
	if ( !ReadLump( pfnRead, pContext, gameLump, dir ) )
		return;

	const int nCount = *(const int *)dir.Base();
	if ( nCount <= 0 || 4 + nCount * 16 > dir.Count() )
		return;

	for ( int k = 0; k < nCount; k++ )
	{
		const unsigned char *pEntry = dir.Base() + 4 + k * 16;
		const int nId = *(const int *)pEntry;
		const unsigned short nFlags = *(const unsigned short *)( pEntry + 4 );
		const unsigned short nVersion = *(const unsigned short *)( pEntry + 6 );
		const int nOfs = *(const int *)( pEntry + 8 );
		const int nLen = *(const int *)( pEntry + 12 );
		if ( nId != BC_GAMELUMP_SPRP || ( nFlags & BC_GAMELUMP_COMPRESSED ) || nVersion < 4 || nVersion > 6 || nLen <= 0 || nOfs <= 0 )
			continue;

		CUtlVector< unsigned char > props;
		props.SetCount( nLen );
		if ( !pfnRead( pContext, nOfs, props.Base(), nLen ) )
			return;

		CUtlBuffer in( props.Base(), nLen, CUtlBuffer::READ_ONLY );
		int nDict = in.GetInt();
		if ( nDict < 0 || in.TellGet() + nDict * BC_STATICPROP_NAME_LENGTH > nLen )
			return;
		in.SeekGet( CUtlBuffer::SEEK_CURRENT, nDict * BC_STATICPROP_NAME_LENGTH );
		int nLeaves = in.GetInt();
		if ( nLeaves < 0 || in.TellGet() + nLeaves * 2 > nLen )
			return;
		in.SeekGet( CUtlBuffer::SEEK_CURRENT, nLeaves * 2 );
		int nProps = in.GetInt();
		const int nHead = in.TellGet();
		if ( !in.IsValid() || nProps <= 0 || ( nLen - nHead ) % nProps )
			return;
		const int nStride = ( nLen - nHead ) / nProps;
		if ( nStride < 32 )
			return;

		for ( int i = 0; i < nProps; i++ )
			props[nHead + i * nStride + 31] |= BC_STATIC_PROP_NO_PER_VERTEX_LIGHTING;	// m_Flags (byte) in v4-v6
		AddPatch( pMap, nOfs, props.Base(), nLen );
	}
}

//-----------------------------------------------------------------------------
// v18: the 2003 lighting compiler left pure red luxels (green = blue = 0, 8x
// to 32x overbright) where it had no valid sample, on ordinary surfaces all
// over the map. Each one is refilled from its valid neighbours in the same
// lightmap, spreading inwards; returns how many were replaced.
//-----------------------------------------------------------------------------
static bool IsInvalidLuxel( const unsigned char *pLuxel )
{
	return pLuxel[1] == 0 && pLuxel[2] == 0 && pLuxel[0] != 0 && (signed char)pLuxel[3] >= 3;
}

static int RepairLightmaps( CUtlVector< unsigned char > &lighting, const CUtlVector< unsigned char > &faces,
	const CUtlVector< unsigned char > &texinfo )
{
	const int nFaces = faces.Count() / BC_FACE_SIZE;
	const int nTexinfo = texinfo.Count() / BC_TEXINFO_SIZE;
	int nRepaired = 0;

	CUtlVector< unsigned char > invalid;
	for ( int i = 0; i < nFaces; i++ )
	{
		const unsigned char *pFace = faces.Base() + i * BC_FACE_SIZE;
		const short nTexinfoIndex = *(const short *)( pFace + 10 );
		const int nLightOfs = *(const int *)( pFace + 20 );
		const int nWidth = *(const int *)( pFace + 36 ) + 1;
		const int nHeight = *(const int *)( pFace + 40 ) + 1;
		if ( nLightOfs < 0 || nWidth <= 0 || nHeight <= 0 || nWidth > 1024 || nHeight > 1024 )
			continue;

		int nStyles = 0;
		while ( nStyles < 4 && pFace[16 + nStyles] != 255 )
			nStyles++;
		const bool bBumped = nTexinfoIndex >= 0 && nTexinfoIndex < nTexinfo &&
			( *(const int *)( texinfo.Base() + nTexinfoIndex * BC_TEXINFO_SIZE + 64 ) & BC_SURF_BUMPLIGHT );
		const int nLuxels = nWidth * nHeight;
		const int nBlocks = nStyles * ( bBumped ? 4 : 1 );
		if ( !nBlocks || nLightOfs + nBlocks * nLuxels * 4 > lighting.Count() )
			continue;

		invalid.SetCount( nLuxels );
		for ( int nBlock = 0; nBlock < nBlocks; nBlock++ )
		{
			unsigned char *pBlock = lighting.Base() + nLightOfs + nBlock * nLuxels * 4;
			int nBad = 0;
			for ( int k = 0; k < nLuxels; k++ )
			{
				invalid[k] = IsInvalidLuxel( pBlock + k * 4 ) ? 1 : 0;
				nBad += invalid[k];
			}
			if ( !nBad )
				continue;
			nRepaired += nBad;

			if ( nBad == nLuxels )
			{
				memset( pBlock, 0, nLuxels * 4 );	// nothing to fill from
				continue;
			}

			// fill from 8-neighbours that are valid, a ring at a time
			while ( nBad > 0 )
			{
				int nFixedThisPass = 0;
				for ( int y = 0; y < nHeight; y++ )
				{
					for ( int x = 0; x < nWidth; x++ )
					{
						const int k = y * nWidth + x;
						if ( invalid[k] != 1 )
							continue;
						float sum[3] = { 0, 0, 0 };
						int nCount = 0;
						for ( int dy = -1; dy <= 1; dy++ )
						{
							for ( int dx = -1; dx <= 1; dx++ )
							{
								const int nx = x + dx, ny = y + dy;
								if ( ( !dx && !dy ) || nx < 0 || ny < 0 || nx >= nWidth || ny >= nHeight )
									continue;
								const int n = ny * nWidth + nx;
								if ( invalid[n] == 1 )
									continue;	// 0 = valid, 2 = filled earlier in an earlier pass
								float c[3];
								ColorRGBExpToLinear( pBlock + n * 4, c );
								sum[0] += c[0]; sum[1] += c[1]; sum[2] += c[2];
								nCount++;
							}
						}
						if ( !nCount )
							continue;
						for ( int c = 0; c < 3; c++ )
							sum[c] /= nCount;
						LinearToColorRGBExp( sum, pBlock + k * 4 );
						invalid[k] = 3;	// filled this pass; usable from the next one
						nFixedThisPass++;
					}
				}
				for ( int k = 0; k < nLuxels; k++ )
				{
					if ( invalid[k] == 3 )
						invalid[k] = 2;
				}
				nBad -= nFixedThisPass;
				if ( !nFixedThisPass )
					break;
			}
		}
	}
	return nRepaired;
}

//-----------------------------------------------------------------------------
// v18: leaves don't have LEAF_FLAGS_SKY / _SKY2D, which the engine uses to
// decide whether to draw the sky. Set them the way vbsp does: a leaf gets the
// flag if any cluster it can see has a leaf with a sky face.
//-----------------------------------------------------------------------------
static int SetLeafSkyFlags( CUtlVector< unsigned char > &leafs, const CUtlVector< unsigned char > &leafFaces,
	const CUtlVector< unsigned char > &faces, const CUtlVector< unsigned char > &texinfo, const CUtlVector< unsigned char > &vis )
{
	const int nLeafs = leafs.Count() / BC_LEAF_SIZE;
	const int nFaces = faces.Count() / BC_FACE_SIZE;
	const int nTexinfo = texinfo.Count() / BC_TEXINFO_SIZE;
	const int nLeafFaces = leafFaces.Count() / 2;
	if ( vis.Count() < 4 )
		return 0;

	const int nClusters = *(const int *)vis.Base();
	if ( nClusters <= 0 || 4 + nClusters * 8 > vis.Count() )
		return 0;

	// clusters holding sky faces: bit 0 = 3D sky, bit 1 = 2D sky only
	CUtlVector< unsigned char > clusterSky;
	clusterSky.SetCount( nClusters );
	memset( clusterSky.Base(), 0, nClusters );
	for ( int i = 0; i < nLeafs; i++ )
	{
		const unsigned char *pLeaf = leafs.Base() + i * BC_LEAF_SIZE;
		const short nCluster = *(const short *)( pLeaf + 4 );
		if ( nCluster < 0 || nCluster >= nClusters )
			continue;
		const unsigned short nFirst = *(const unsigned short *)( pLeaf + 20 );
		const unsigned short nCount = *(const unsigned short *)( pLeaf + 22 );
		for ( int k = 0; k < nCount && nFirst + k < nLeafFaces; k++ )
		{
			const unsigned short nFace = *(const unsigned short *)( leafFaces.Base() + ( nFirst + k ) * 2 );
			if ( nFace >= nFaces )
				continue;
			const short nTexinfoIndex = *(const short *)( faces.Base() + nFace * BC_FACE_SIZE + 10 );
			if ( nTexinfoIndex < 0 || nTexinfoIndex >= nTexinfo )
				continue;
			const int nFlags = *(const int *)( texinfo.Base() + nTexinfoIndex * BC_TEXINFO_SIZE + 64 );
			if ( nFlags & BC_SURF_SKY )
				clusterSky[nCluster] |= 1;
			else if ( nFlags & BC_SURF_SKY2D )
				clusterSky[nCluster] |= 2;
		}
	}

	// what each cluster can see (PVS, run-length encoded zero bytes)
	const int nRowBytes = ( nClusters + 7 ) / 8;
	CUtlVector< unsigned char > clusterFlags, row;
	clusterFlags.SetCount( nClusters );
	row.SetCount( nRowBytes );
	for ( int c = 0; c < nClusters; c++ )
	{
		const int nOfs = *(const int *)( vis.Base() + 4 + c * 8 );
		memset( row.Base(), 0, nRowBytes );
		if ( nOfs > 0 && nOfs < vis.Count() )
		{
			const unsigned char *pIn = vis.Base() + nOfs;
			const unsigned char *pEnd = vis.Base() + vis.Count();
			int nOut = 0;
			while ( nOut < nRowBytes && pIn < pEnd )
			{
				if ( *pIn )
				{
					row[nOut++] = *pIn++;
				}
				else
				{
					if ( pIn + 1 >= pEnd )
						break;
					nOut += pIn[1];
					pIn += 2;
				}
			}
		}
		else
		{
			memset( row.Base(), 0xFF, nRowBytes );	// no vis data: everything visible
		}

		unsigned char nSky = 0;
		for ( int v = 0; v < nClusters && nSky != 3; v++ )
		{
			if ( row[v >> 3] & ( 1 << ( v & 7 ) ) )
				nSky |= clusterSky[v];
		}
		clusterFlags[c] = ( nSky & 1 ) ? BC_LEAF_FLAGS_SKY : ( nSky & 2 ) ? BC_LEAF_FLAGS_SKY2D : 0;
	}

	// Non-solid leaves outside any cluster can't be checked with the PVS; vbsp
	// flags all of them when the map has sky (checked against a v20 recompile)
	unsigned char nAnySky = 0;
	for ( int c = 0; c < nClusters; c++ )
		nAnySky |= clusterSky[c];
	const unsigned char nNoClusterFlags = ( nAnySky & 1 ) ? BC_LEAF_FLAGS_SKY : ( nAnySky & 2 ) ? BC_LEAF_FLAGS_SKY2D : 0;

	int nFlagged = 0;
	for ( int i = 0; i < nLeafs; i++ )
	{
		unsigned char *pLeaf = leafs.Base() + i * BC_LEAF_SIZE;
		const int nContents = *(const int *)pLeaf;
		const short nCluster = *(const short *)( pLeaf + 4 );
		unsigned char nFlags;
		if ( nCluster >= 0 && nCluster < nClusters )
			nFlags = clusterFlags[nCluster];
		else
			nFlags = ( nContents & BC_CONTENTS_SOLID ) ? 0 : nNoClusterFlags;
		if ( !nFlags )
			continue;
		unsigned short &nAreaFlags = *(unsigned short *)( pLeaf + 6 );	// area:9, flags:7
		nAreaFlags |= (unsigned short)( nFlags << 9 );
		nFlagged++;
	}
	return nFlagged;
}

static bool ConvertVersion18( BCReadFn_t pfnRead, void *pContext, BCLump_t *pLumps, BCVirtualMap_t *pMap,
	bc_int64 &nAppendAt, const char *pszName )
{
	BCLump_t &faces = pLumps[BC_LUMP_FACES];
	BCLump_t &leafs = pLumps[BC_LUMP_LEAFS];
	if ( faces.m_nLen <= 0 || ( faces.m_nLen % BC_FACE_V18_SIZE ) || ( leafs.m_nLen % BC_LEAF_SIZE ) ||
		 faces.m_nUncompressed || leafs.m_nUncompressed )
	{
		Warning( "BSP v18: %s: unexpected face/leaf lump sizes, not loaded\n", pszName );
		return false;
	}

	BCLump_t &lightingLump = pLumps[BC_LUMP_LIGHTING];
	CUtlVector< unsigned char > faceData, leafData, leafFaces, lighting, texinfo, vis;
	if ( !ReadLump( pfnRead, pContext, faces, faceData ) || !ReadLump( pfnRead, pContext, leafs, leafData ) ||
		 !ReadLump( pfnRead, pContext, pLumps[BC_LUMP_LEAFFACES], leafFaces ) ||
		 !ReadLump( pfnRead, pContext, pLumps[BC_LUMP_TEXINFO], texinfo ) ||
		 ( !lightingLump.m_nUncompressed && !ReadLump( pfnRead, pContext, lightingLump, lighting ) ) )
		return false;
	if ( !pLumps[BC_LUMP_VISIBILITY].m_nUncompressed )
		ReadLump( pfnRead, pContext, pLumps[BC_LUMP_VISIBILITY], vis );

	const int nFaces = faces.m_nLen / BC_FACE_V18_SIZE;
	const int nLeafs = leafs.m_nLen / BC_LEAF_SIZE;
	const int nLeafFaces = leafFaces.Count() / 2;

	// faces: drop the 16-byte prefix; the shorter lump stays in place
	CUtlVector< unsigned char > newFaces;
	newFaces.SetCount( nFaces * BC_FACE_SIZE );
	for ( int i = 0; i < nFaces; i++ )
		memcpy( newFaces.Base() + i * BC_FACE_SIZE, faceData.Base() + i * BC_FACE_V18_SIZE + 16, BC_FACE_SIZE );

	const int nHiddenFaces = HideToolFaces( pfnRead, pContext, pLumps, pMap, newFaces, pszName );

	// lightmaps: the compiler's invalid-sample red, refilled
	const int nRepaired = RepairLightmaps( lighting, newFaces, texinfo );
	if ( nRepaired > 0 )
		AddPatch( pMap, lightingLump.m_nOfs, lighting.Base(), lighting.Count() );

	// leaf sky flags (patched with the leaves below)
	const int nSkyLeafs = SetLeafSkyFlags( leafData, leafFaces, newFaces, texinfo, vis );
	DevMsg( "BSP v18: %s: %d tool faces hidden, %d invalid luxels refilled, %d leaves see the sky\n",
		pszName, nHiddenFaces, nRepaired, nSkyLeafs );

	// average style-0 lightmap color of each face
	CUtlVector< float > faceLight;	// r, g, b, weight
	faceLight.SetCount( nFaces * 4 );
	memset( faceLight.Base(), 0, faceLight.Count() * sizeof( float ) );
	for ( int i = 0; i < nFaces; i++ )
	{
		const unsigned char *pFace = newFaces.Base() + i * BC_FACE_SIZE;
		const unsigned char nStyle0 = pFace[16];
		const int nLightOfs = *(const int *)( pFace + 20 );
		const int nWidth = *(const int *)( pFace + 36 ) + 1;
		const int nHeight = *(const int *)( pFace + 40 ) + 1;
		if ( nStyle0 == 255 || nLightOfs < 0 || nWidth <= 0 || nHeight <= 0 || nWidth > 1024 || nHeight > 1024 )
			continue;
		// (bumped faces store the plain lightmap first, so the first block works either way)
		const int nLuxels = nWidth * nHeight;
		if ( nLightOfs + nLuxels * 4 > lighting.Count() )
			continue;

		// Some faces in these maps hold pure-red luxels thousands of times too bright
		// (old or broken data); nothing above 4x overbright is real ambient light
		float sum[3] = { 0, 0, 0 };
		int nUsed = 0;
		for ( int k = 0; k < nLuxels; k++ )
		{
			const unsigned char *pLuxel = lighting.Base() + nLightOfs + k * 4;
			if ( (signed char)pLuxel[3] > 2 )
				continue;
			float c[3];
			ColorRGBExpToLinear( pLuxel, c );
			sum[0] += c[0]; sum[1] += c[1]; sum[2] += c[2];
			nUsed++;
		}
		if ( !nUsed )
			continue;
		float *pOut = faceLight.Base() + i * 4;
		pOut[0] = sum[0] / nUsed;
		pOut[1] = sum[1] / nUsed;
		pOut[2] = sum[2] / nUsed;
		pOut[3] = 1.0f;
	}

	// per leaf, then per cluster and the whole map as fallbacks
	CUtlVector< float > leafLight;
	leafLight.SetCount( nLeafs * 4 );
	memset( leafLight.Base(), 0, leafLight.Count() * sizeof( float ) );
	int nMaxCluster = -1;
	float global[4] = { 0, 0, 0, 0 };
	for ( int i = 0; i < nLeafs; i++ )
	{
		const unsigned char *pLeaf = leafData.Base() + i * BC_LEAF_SIZE;
		const unsigned short nFirst = *(const unsigned short *)( pLeaf + 20 );
		const unsigned short nCount = *(const unsigned short *)( pLeaf + 22 );
		nMaxCluster = MAX( nMaxCluster, (int)*(const short *)( pLeaf + 4 ) );
		float *pOut = leafLight.Base() + i * 4;
		for ( int k = 0; k < nCount && nFirst + k < nLeafFaces; k++ )
		{
			const unsigned short nFace = *(const unsigned short *)( leafFaces.Base() + ( nFirst + k ) * 2 );
			if ( nFace >= nFaces || faceLight[nFace * 4 + 3] == 0.0f )
				continue;
			for ( int c = 0; c < 4; c++ )
				pOut[c] += faceLight[nFace * 4 + c];
		}
		for ( int c = 0; c < 4; c++ )
			global[c] += pOut[c];
	}

	CUtlVector< float > clusterLight;
	clusterLight.SetCount( ( nMaxCluster + 1 ) * 4 );
	if ( clusterLight.Count() )
		memset( clusterLight.Base(), 0, clusterLight.Count() * sizeof( float ) );
	for ( int i = 0; i < nLeafs; i++ )
	{
		const short nCluster = *(const short *)( leafData.Base() + i * BC_LEAF_SIZE + 4 );
		if ( nCluster < 0 )
			continue;
		for ( int c = 0; c < 4; c++ )
			clusterLight[nCluster * 4 + c] += leafLight[i * 4 + c];
	}

	// LUMP_LEAF_AMBIENT_INDEX { ushort count; ushort first } and LUMP_LEAF_AMBIENT_LIGHTING
	CUtlVector< unsigned char > ambientIndex, ambientLighting;
	ambientIndex.SetCount( nLeafs * 4 );
	ambientLighting.SetCount( nLeafs * BC_AMBIENT_SAMPLE_SIZE );
	memset( ambientLighting.Base(), 0, ambientLighting.Count() );
	for ( int i = 0; i < nLeafs; i++ )
	{
		const unsigned char *pLeaf = leafData.Base() + i * BC_LEAF_SIZE;
		const int nContents = *(const int *)pLeaf;
		const short nCluster = *(const short *)( pLeaf + 4 );

		const float *pSource = leafLight.Base() + i * 4;
		if ( pSource[3] == 0.0f && nCluster >= 0 && clusterLight[nCluster * 4 + 3] > 0.0f )
			pSource = clusterLight.Base() + nCluster * 4;
		if ( pSource[3] == 0.0f )
			pSource = global;

		float color[3] = { 0, 0, 0 };
		if ( pSource[3] > 0.0f && !( nContents & BC_CONTENTS_SOLID ) )
		{
			for ( int c = 0; c < 3; c++ )
				color[c] = pSource[c] / pSource[3] * BC_AMBIENT_FROM_LIGHTMAP_SCALE;
		}

		unsigned short *pIndex = (unsigned short *)( ambientIndex.Base() + i * 4 );
		pIndex[0] = 1;
		pIndex[1] = (unsigned short)MIN( i, 0xFFFF );

		unsigned char *pSample = ambientLighting.Base() + i * BC_AMBIENT_SAMPLE_SIZE;
		for ( int nSide = 0; nSide < 6; nSide++ )
			LinearToColorRGBExp( color, pSample + nSide * 4 );
		pSample[24] = pSample[25] = pSample[26] = 128;	// middle of the leaf
	}
	if ( nLeafs > 0xFFFF )
		Warning( "BSP v18: %s has more than 65535 leaves; ambient lighting past that is shared\n", pszName );

	// Write the faces, then put the two new lumps in the space they freed, or
	// past the end of the file if that's too small
	const int nOldFaceLen = faces.m_nLen;
	AddPatch( pMap, faces.m_nOfs, newFaces.Base(), newFaces.Count(), nOldFaceLen );
	faces.m_nLen = newFaces.Count();

	int nFreeOfs = faces.m_nOfs + ( ( faces.m_nLen + 3 ) & ~3 );
	const int nFreeEnd = faces.m_nOfs + nOldFaceLen;
	BCLump_t &index = pLumps[BC_LUMP_LEAF_AMBIENT_INDEX];
	BCLump_t &ambient = pLumps[BC_LUMP_LEAF_AMBIENT_LIGHTING];
	const int nNeeded = ambientIndex.Count() + ambientLighting.Count();

	bc_int64 nPlaceAt;
	if ( nFreeOfs + nNeeded <= nFreeEnd )
	{
		nPlaceAt = nFreeOfs;
	}
	else
	{
		nPlaceAt = nAppendAt;
		nAppendAt = ( nAppendAt + nNeeded + 3 ) & ~3LL;
		pMap->m_nVirtualSize = MAX( pMap->m_nVirtualSize, nAppendAt );
	}

	if ( nPlaceAt > 0x7FFFFFFF - nNeeded )
		return false;

	AddPatch( pMap, nPlaceAt, ambientIndex.Base(), ambientIndex.Count() );
	index.m_nOfs = (int)nPlaceAt;
	index.m_nLen = ambientIndex.Count();
	index.m_nVersion = 0;
	index.m_nUncompressed = 0;

	AddPatch( pMap, nPlaceAt + ambientIndex.Count(), ambientLighting.Base(), ambientLighting.Count() );
	ambient.m_nOfs = (int)( nPlaceAt + ambientIndex.Count() );
	ambient.m_nLen = ambientLighting.Count();
	ambient.m_nVersion = 1;
	ambient.m_nUncompressed = 0;

	// original faces: same 16-byte prefix
	BCLump_t &origFaces = pLumps[BC_LUMP_ORIGINALFACES];
	CUtlVector< unsigned char > origData;
	if ( origFaces.m_nLen > 0 && !origFaces.m_nUncompressed && !( origFaces.m_nLen % BC_FACE_V18_SIZE ) &&
		 ReadLump( pfnRead, pContext, origFaces, origData ) )
	{
		const int nOrig = origFaces.m_nLen / BC_FACE_V18_SIZE;
		CUtlVector< unsigned char > newOrig;
		newOrig.SetCount( nOrig * BC_FACE_SIZE );
		for ( int i = 0; i < nOrig; i++ )
			memcpy( newOrig.Base() + i * BC_FACE_SIZE, origData.Base() + i * BC_FACE_V18_SIZE + 16, BC_FACE_SIZE );
		AddPatch( pMap, origFaces.m_nOfs, newOrig.Base(), newOrig.Count(), origFaces.m_nLen );
		origFaces.m_nLen = newOrig.Count();
	}

	FlagStaticPropsUnlit( pfnRead, pContext, pLumps[BC_LUMP_GAME_LUMP], pMap );

	// leaves: sky flags set above
	AddPatch( pMap, leafs.m_nOfs, leafData.Base(), leafData.Count() );

	// leaves without ambient lighting are version 1
	leafs.m_nVersion = 1;
	return true;
}

//-----------------------------------------------------------------------------
// Returns the view of a map, or NULL if the map doesn't need one (already
// v19/v20 in the v20 lump order) or can't be read.
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
// Black Mesa HDR cubemaps
//
// Black Mesa packs its HDR cubemaps ( materials/maps/<map>/c*.hdr.vtf ) as 16 bit
// unsigned integer texels, 65535 = 1.0, but labels them IMAGE_FORMAT_RGBA16161616F.
// The TF2 SDK reads them as half floats: every reflection comes out nearly black,
// and alpha is garbage ( NaN and Inf included ). Each one is converted to real half
// floats in place, same size, alpha 1, and the two zip CRCs of the entry follow.
// The pak's layout doesn't change, so filesystem_stdio's pak mount still works.
//
// Real half float cubemaps are left alone: their alpha is 1.0 everywhere ( Black
// Mesa's is garbage, often Inf / NaN as a half ), and their lit texels sit around
// 0x3C00 ( 1.0 ) while Black Mesa's integers top out near 0x2480. Cubemaps that
// were already converted ( alpha 1.0 ) are never converted twice. sdk_tools/bms/bms_fix_cubemaps.py
// does the same offline.
//-----------------------------------------------------------------------------
#define BC_IMAGE_FORMAT_RGBA16161616F	24
#define BC_INTEGER_HDR_CUBEMAP_ONE		65535.0f	// texel value Black Mesa means as 1.0
#define BC_ZIP_MAX_EOCD_SEARCH			( 65535 + 22 )

static unsigned short FloatToHalf( float f )
{
	unsigned int x;
	memcpy( &x, &f, 4 );
	const unsigned int nSign = ( x >> 16 ) & 0x8000;
	int nExp = (int)( ( x >> 23 ) & 0xFF ) - 127 + 15;
	unsigned int nMant = x & 0x7FFFFF;

	if ( ( ( x >> 23 ) & 0xFF ) == 0 || nExp <= 0 )
	{
		if ( nExp < -10 )
			return (unsigned short)nSign;
		nMant |= 0x800000;
		const int nShift = 14 - nExp;
		unsigned int nHalf = nMant >> nShift;
		if ( ( nMant >> ( nShift - 1 ) ) & 1 )
			nHalf++;
		return (unsigned short)( nSign | nHalf );
	}
	if ( nExp >= 31 )
		return (unsigned short)( nSign | 0x7BFF );	// largest finite half

	unsigned int nHalf = nSign | ( nExp << 10 ) | ( nMant >> 13 );
	if ( nMant & 0x1000 )
		nHalf++;	// round to nearest
	return (unsigned short)nHalf;
}

// Offset of the high res image data in a VTF ( 7.3+ keep it in a resource entry )
static int VTFImageOffset( const unsigned char *pVTF, int nSize )
{
	if ( nSize < 80 )
		return -1;
	const int nMajor = *(const int *)( pVTF + 4 );
	const int nMinor = *(const int *)( pVTF + 8 );
	const int nHeaderSize = *(const int *)( pVTF + 12 );
	if ( nMajor == 7 && nMinor >= 3 )
	{
		const int nResources = *(const int *)( pVTF + 68 );
		for ( int r = 0; r < nResources && 80 + r * 8 + 8 <= nSize; r++ )
		{
			const unsigned char *pRes = pVTF + 80 + r * 8;
			if ( pRes[0] == 0x30 && pRes[1] == 0 && pRes[2] == 0 )
				return *(const int *)( pRes + 4 );
		}
	}
	return nHeaderSize;
}

static bool IsIntegerHDRCubemap( const unsigned short *pTexels, int nTexels )
{
	// Real half float cubemaps ( and ones already converted, offline or here ) have
	// alpha 1.0 everywhere. Black Mesa's integer ones have garbage there.
	bool bAlphaOne = true;
	for ( int i = 3; i < nTexels && bAlphaOne; i += 4 )
		bAlphaOne = ( pTexels[i] == 0x3C00 );
	if ( bAlphaOne )
		return false;

	int nColor = 0, nSmall = 0;
	for ( int i = 0; i + 3 < nTexels; i += 4 )
	{
		for ( int c = 0; c < 4; c++ )
		{
			const unsigned short t = pTexels[i + c];
			if ( ( t & 0x7C00 ) == 0x7C00 )
				return true;	// Inf / NaN as a half
			if ( c < 3 )
			{
				nColor++;
				nSmall += ( t < 0x3000 );
			}
		}
	}
	return nColor > 0 && nSmall * 2 > nColor;
}

static int FixIntegerHDRCubemaps( BCReadFn_t pfnRead, void *pContext, const BCLump_t &pak, BCVirtualMap_t *pMap, const char *pszName )
{
	if ( pak.m_nLen < 22 || pak.m_nUncompressed != 0 )
		return 0;

	// End of central directory, in the last 64k of the pak
	const int nTail = MIN( pak.m_nLen, BC_ZIP_MAX_EOCD_SEARCH );
	CUtlVector< unsigned char > tail;
	tail.SetCount( nTail );
	if ( !pfnRead( pContext, (bc_int64)pak.m_nOfs + pak.m_nLen - nTail, tail.Base(), nTail ) )
		return 0;

	int nEOCD = -1;
	for ( int i = nTail - 22; i >= 0; i-- )
	{
		if ( tail[i] == 'P' && tail[i + 1] == 'K' && tail[i + 2] == 5 && tail[i + 3] == 6 )
		{
			nEOCD = i;
			break;
		}
	}
	if ( nEOCD < 0 )
		return 0;

	const int nEntries = *(const unsigned short *)( tail.Base() + nEOCD + 10 );
	const int nDirSize = *(const int *)( tail.Base() + nEOCD + 12 );
	const int nDirOfs = *(const int *)( tail.Base() + nEOCD + 16 );
	if ( nDirOfs < 0 || nDirSize <= 0 || nDirOfs + nDirSize > pak.m_nLen )
		return 0;

	CUtlVector< unsigned char > dir;
	dir.SetCount( nDirSize );
	if ( !pfnRead( pContext, (bc_int64)pak.m_nOfs + nDirOfs, dir.Base(), nDirSize ) )
		return 0;

	int nFixed = 0;
	CUtlVector< unsigned char > vtf;
	int p = 0;
	for ( int e = 0; e < nEntries && p + 46 <= nDirSize; e++ )
	{
		const unsigned char *pEntry = dir.Base() + p;
		if ( *(const unsigned int *)pEntry != 0x02014B50 )
			break;

		const int nMethod = *(const unsigned short *)( pEntry + 10 );
		const int nCompSize = *(const int *)( pEntry + 20 );
		const int nSize = *(const int *)( pEntry + 24 );
		const int nNameLen = *(const unsigned short *)( pEntry + 28 );
		const int nExtraLen = *(const unsigned short *)( pEntry + 30 );
		const int nCommentLen = *(const unsigned short *)( pEntry + 32 );
		const int nLocalOfs = *(const int *)( pEntry + 42 );
		const int nDirEntry = p;
		p += 46 + nNameLen + nExtraLen + nCommentLen;
		if ( p > nDirSize )
			break;

		char szName[MAX_PATH];
		const int nCopy = MIN( nNameLen, (int)sizeof( szName ) - 1 );
		memcpy( szName, pEntry + 46, nCopy );
		szName[nCopy] = 0;
		Q_strlower( szName );
		if ( Q_strncmp( szName, "materials/maps/", 15 ) || !Q_stristr( szName, ".hdr.vtf" ) )
			continue;
		if ( nMethod != 0 || nCompSize != nSize || nSize < 96 || nLocalOfs < 0 || nLocalOfs + 30 > pak.m_nLen )
			continue;

		unsigned char local[30];
		if ( !pfnRead( pContext, (bc_int64)pak.m_nOfs + nLocalOfs, local, sizeof( local ) ) || *(const unsigned int *)local != 0x04034B50 )
			continue;
		const int nDataOfs = nLocalOfs + 30 + *(const unsigned short *)( local + 26 ) + *(const unsigned short *)( local + 28 );
		if ( nDataOfs + nSize > pak.m_nLen )
			continue;

		// The header first: most maps' cubemaps are something else, or already half floats
		// ( alpha 1.0 ), which the end of the file ( the largest mip ) shows without reading it all
		unsigned char head[512];
		const int nHead = MIN( nSize, (int)sizeof( head ) );
		if ( !pfnRead( pContext, (bc_int64)pak.m_nOfs + nDataOfs, head, nHead ) )
			continue;
		if ( memcmp( head, "VTF\0", 4 ) || *(const int *)( head + 52 ) != BC_IMAGE_FORMAT_RGBA16161616F )
			continue;
		const int nImageOfs = VTFImageOffset( head, nHead );
		if ( nImageOfs <= 0 || nImageOfs >= nSize )
			continue;

		{
			unsigned short sample[2048];
			const int nSampleBytes = MIN( (int)sizeof( sample ), ( nSize - nImageOfs ) & ~7 );
			if ( nSampleBytes <= 0 || !pfnRead( pContext, (bc_int64)pak.m_nOfs + nDataOfs + nSize - nSampleBytes, sample, nSampleBytes ) )
				continue;
			bool bAlphaOne = true;
			for ( int i = 3; i < nSampleBytes / 2 && bAlphaOne; i += 4 )
				bAlphaOne = ( sample[i] == 0x3C00 );
			if ( bAlphaOne )
				continue;
		}

		vtf.SetCount( nSize );
		if ( !pfnRead( pContext, (bc_int64)pak.m_nOfs + nDataOfs, vtf.Base(), nSize ) )
			continue;
		unsigned short *pTexels = (unsigned short *)( vtf.Base() + nImageOfs );
		const int nTexels = ( ( nSize - nImageOfs ) / 2 ) & ~3;
		if ( !IsIntegerHDRCubemap( pTexels, nTexels ) )
			continue;

		const unsigned short nOne = FloatToHalf( 1.0f );
		for ( int i = 0; i + 3 < nTexels; i += 4 )
		{
			pTexels[i + 0] = FloatToHalf( pTexels[i + 0] / BC_INTEGER_HDR_CUBEMAP_ONE );
			pTexels[i + 1] = FloatToHalf( pTexels[i + 1] / BC_INTEGER_HDR_CUBEMAP_ONE );
			pTexels[i + 2] = FloatToHalf( pTexels[i + 2] / BC_INTEGER_HDR_CUBEMAP_ONE );
			pTexels[i + 3] = nOne;
		}

		CRC32_t crc;
		CRC32_Init( &crc );
		CRC32_ProcessBuffer( &crc, vtf.Base(), nSize );
		CRC32_Final( &crc );

		AddPatch( pMap, (bc_int64)pak.m_nOfs + nLocalOfs + 14, &crc, 4 );
		AddPatch( pMap, (bc_int64)pak.m_nOfs + nDataOfs, vtf.Base(), nSize );
		AddPatch( pMap, (bc_int64)pak.m_nOfs + nDirOfs + nDirEntry + 16, &crc, 4 );
		nFixed++;
	}

	if ( nFixed )
		//DevMsg( "BSP compat: %s: converted %d integer HDR cubemaps\n", pszName, nFixed );
	return nFixed;
}

static BCVirtualMap_t *BuildVirtualMap( BCReadFn_t pfnRead, void *pContext, bc_int64 nFileSize, const char *pszName )
{
	if ( nFileSize < BC_HEADER_SIZE || nFileSize > 0x7FFFFFFF )
		return NULL;

	int header[BC_HEADER_SIZE / 4];
	if ( !pfnRead( pContext, 0, header, sizeof( header ) ) || header[0] != BC_IDENT )
		return NULL;

	const int nVersion = header[1];

	// lump table order: the one whose entity lump starts with '{'
	BCLump_t lumps[BC_HEADER_LUMPS];
	bool bFound = false, bL4D2Order = false;
	for ( int nOrder = 0; nOrder < 2 && !bFound; nOrder++ )
	{
		ReadLumpTable( &header[2], nOrder == 1, lumps );
		const BCLump_t &ent = lumps[BC_LUMP_ENTITIES];
		if ( ent.m_nOfs < BC_HEADER_SIZE || ent.m_nLen <= 0 || (bc_int64)ent.m_nOfs + ent.m_nLen > nFileSize )
			continue;
		char c = 0;
		if ( pfnRead( pContext, ent.m_nOfs, &c, 1 ) && c == '{' )
		{
			bFound = true;
			bL4D2Order = ( nOrder == 1 );
		}
	}

	const bool bVersion18 = ( nVersion == 18 && !bL4D2Order );
	if ( !bFound )
		return NULL;

	if ( nVersion <= 20 && !bL4D2Order && !bVersion18 )
	{
		// Version 19/20: the engine reads these as they are. Only Black Mesa's cubemaps
		// need a view, which then patches nothing but the pak's contents ( no header ).
		ReadLumpTable( &header[2], false, lumps );
		const BCLump_t &pak = lumps[BC_LUMP_PAKFILE];
		if ( pak.m_nLen <= 0 || pak.m_nOfs < BC_HEADER_SIZE || (bc_int64)pak.m_nOfs + pak.m_nLen > nFileSize )
			return NULL;

		BCVirtualMap_t *pMap = new BCVirtualMap_t;
		pMap->m_nOriginalSize = nFileSize;
		pMap->m_nVirtualSize = nFileSize;
		if ( !FixIntegerHDRCubemaps( pfnRead, pContext, pak, pMap, pszName ) )
		{
			delete pMap;
			return NULL;
		}
		return pMap;
	}

	ReadLumpTable( &header[2], bL4D2Order, lumps );
	for ( int i = 0; i < BC_HEADER_LUMPS; i++ )
	{
		if ( lumps[i].m_nLen < 0 || ( lumps[i].m_nLen > 0 && ( lumps[i].m_nOfs < BC_HEADER_SIZE ||
			 (bc_int64)lumps[i].m_nOfs + lumps[i].m_nLen > nFileSize ) ) )
		{
			Warning( "BSP v21: %s has a bad lump %d, not loaded as v21\n", pszName, i );
			return NULL;
		}
	}

	BCVirtualMap_t *pMap = new BCVirtualMap_t;
	pMap->m_nOriginalSize = nFileSize;
	pMap->m_nVirtualSize = nFileSize;
	bc_int64 nAppendAt = ( nFileSize + 3 ) & ~3LL;

	CUtlVector< unsigned char > data;

	for ( int i = 0; i < BC_HEADER_LUMPS; i++ )
	{
		BCLump_t &lump = lumps[i];
		if ( lump.m_nLen <= 0 )
			continue;

		if ( IsDroppedLump( i ) )
		{
			lump.m_nOfs = 0;
			lump.m_nLen = 0;
			lump.m_nVersion = 0;
			lump.m_nUncompressed = 0;
			continue;
		}

		const bool bCompressed = ( lump.m_nUncompressed != 0 );

		if ( ( i == BC_LUMP_WORLDLIGHTS || i == BC_LUMP_WORLDLIGHTS_HDR ) && lump.m_nVersion == 1 )
		{
			if ( bCompressed || ( lump.m_nLen % BC_WORLDLIGHT_V1_SIZE ) || !ReadLump( pfnRead, pContext, lump, data ) )
			{
				Warning( "BSP v21: %s: couldn't convert world light lump %d\n", pszName, i );
				continue;
			}

			// drop shadow_cast_offset (bytes 36-47); the shorter lump stays where it was
			const int nCount = lump.m_nLen / BC_WORLDLIGHT_V1_SIZE;
			CUtlVector< unsigned char > lights;
			lights.SetCount( nCount * BC_WORLDLIGHT_V0_SIZE );
			ConvertWorldLightsV1( data.Base(), nCount, lights.Base() );
			AddPatch( pMap, lump.m_nOfs, lights.Base(), lights.Count(), lump.m_nLen );
			lump.m_nLen = lights.Count();
			lump.m_nVersion = 0;
		}
		else if ( i == BC_LUMP_BRUSHSIDES )
		{
			if ( bCompressed || ( lump.m_nLen % 8 ) || !ReadLump( pfnRead, pContext, lump, data ) )
				continue;

			// dbrushside_t { ushort planenum; short texinfo; short dispinfo; byte bevel; byte thin; }
			bool bAnyThin = false;
			for ( int k = 7; k < lump.m_nLen; k += 8 )
			{
				bAnyThin |= ( data[k] != 0 );
				data[k] = 0;
			}
			if ( bAnyThin )
				AddPatch( pMap, lump.m_nOfs, data.Base(), lump.m_nLen );
		}
	}

	if ( bVersion18 && !ConvertVersion18( pfnRead, pContext, lumps, pMap, nAppendAt, pszName ) )
	{
		delete pMap;
		return NULL;
	}

	// static props
	const BCLump_t &gameLump = lumps[BC_LUMP_GAME_LUMP];
	if ( gameLump.m_nLen >= 4 && gameLump.m_nUncompressed == 0 && ReadLump( pfnRead, pContext, gameLump, data ) )
	{
		struct GameLump_t
		{
			int				m_nId;
			unsigned short	m_nFlags;
			unsigned short	m_nVersion;
			int				m_nFileOfs;
			int				m_nFileLen;
		};

		const int nCount = *(const int *)data.Base();
		if ( nCount > 0 && 4 + nCount * (int)sizeof( GameLump_t ) <= gameLump.m_nLen )
		{
			GameLump_t *pEntries = (GameLump_t *)( data.Base() + 4 );
			bool bDirChanged = false;

			for ( int k = 0; k < nCount; k++ )
			{
				GameLump_t &entry = pEntries[k];
				if ( entry.m_nId != BC_GAMELUMP_SPRP || ( entry.m_nFlags & BC_GAMELUMP_COMPRESSED ) || entry.m_nFileLen <= 0 ||
					 entry.m_nFileOfs <= 0 || (bc_int64)entry.m_nFileOfs + entry.m_nFileLen > nFileSize )
					continue;

				CUtlVector< unsigned char > props;
				props.SetCount( entry.m_nFileLen );
				if ( !pfnRead( pContext, entry.m_nFileOfs, props.Base(), entry.m_nFileLen ) )
					continue;

				CUtlBuffer converted;
				CUtlVector< color32 > colors;
				if ( !ConvertStaticProps( props.Base(), props.Count(), entry.m_nVersion, converted, colors ) )
					continue;

				const int nNewLen = converted.TellPut();
				if ( nNewLen <= entry.m_nFileLen )
				{
					AddPatch( pMap, entry.m_nFileOfs, converted.Base(), nNewLen, entry.m_nFileLen );
				}
				else
				{
					// bigger (68-byte L4D props): put it past the end of the file
					entry.m_nFileOfs = (int)nAppendAt;
					AddPatch( pMap, nAppendAt, converted.Base(), nNewLen );
					nAppendAt = ( nAppendAt + nNewLen + 3 ) & ~3LL;
					pMap->m_nVirtualSize = nAppendAt;
				}
				entry.m_nFileLen = nNewLen;
				entry.m_nVersion = 10;
				bDirChanged = true;
			}

			if ( bDirChanged )
				AddPatch( pMap, gameLump.m_nOfs, data.Base(), 4 + nCount * (int)sizeof( GameLump_t ) );
		}
	}
	else if ( gameLump.m_nLen > 0 && gameLump.m_nUncompressed != 0 )
	{
		Warning( "BSP v21: %s has a compressed game lump, static props left as they are\n", pszName );
	}

	// Black Mesa style cubemaps in the pak ( offsets unchanged ). Black Mesa's maps are version 20,
	// handled above; v21 games ( L4D2, ASW, Portal 2, CS:GO ) write real half float cubemaps whose
	// alpha isn't 1.0, and dark ones would pass the integer check, so they're left alone.
	if ( nVersion <= 20 )
	{
		FixIntegerHDRCubemaps( pfnRead, pContext, lumps[BC_LUMP_PAKFILE], pMap, pszName );
	}

	// header
	int newHeader[BC_HEADER_SIZE / 4];
	newHeader[0] = BC_IDENT;
	newHeader[1] = 20;
	for ( int i = 0; i < BC_HEADER_LUMPS; i++ )
	{
		int *p = &newHeader[2 + i * 4];
		p[0] = lumps[i].m_nLen > 0 ? lumps[i].m_nOfs : 0;
		p[1] = lumps[i].m_nLen > 0 ? lumps[i].m_nLen : 0;
		p[2] = lumps[i].m_nVersion;
		p[3] = lumps[i].m_nLen > 0 ? lumps[i].m_nUncompressed : 0;
	}
	newHeader[2 + BC_HEADER_LUMPS * 4] = header[2 + BC_HEADER_LUMPS * 4];	// map revision
	AddPatch( pMap, 0, newHeader, sizeof( newHeader ) );

	return pMap;
}

//-----------------------------------------------------------------------------
// Reads from the view. Returns the number of bytes read (0 at/after the end).
//-----------------------------------------------------------------------------
static int VirtualRead( const BCVirtualMap_t *pMap, BCReadFn_t pfnRead, void *pContext, bc_int64 nOffset, void *pDest, int nSize )
{
	if ( nOffset < 0 || nSize <= 0 || nOffset >= pMap->m_nVirtualSize )
		return 0;
	if ( nOffset + nSize > pMap->m_nVirtualSize )
		nSize = (int)( pMap->m_nVirtualSize - nOffset );

	unsigned char *pOut = (unsigned char *)pDest;
	const bc_int64 nEnd = nOffset + nSize;

	// the original file's bytes
	if ( nOffset < pMap->m_nOriginalSize )
	{
		int nFromFile = (int)( MIN( nEnd, pMap->m_nOriginalSize ) - nOffset );
		if ( !pfnRead( pContext, nOffset, pOut, nFromFile ) )
			return 0;
		if ( nFromFile < nSize )
			memset( pOut + nFromFile, 0, nSize - nFromFile );
	}
	else
	{
		memset( pOut, 0, nSize );
	}

	// overlaid ranges
	for ( int i = 0; i < pMap->m_Patches.Count(); i++ )
	{
		const BCPatch_t *pPatch = pMap->m_Patches[i];
		const bc_int64 nPatchEnd = pPatch->m_nOffset + pPatch->m_Data.Count();
		if ( pPatch->m_nOffset >= nEnd )
			break;
		if ( nPatchEnd <= nOffset )
			continue;

		const bc_int64 nFrom = MAX( nOffset, pPatch->m_nOffset );
		const bc_int64 nTo = MIN( nEnd, nPatchEnd );
		memcpy( pOut + ( nFrom - nOffset ), pPatch->m_Data.Base() + ( nFrom - pPatch->m_nOffset ), (size_t)( nTo - nFrom ) );
	}

	return nSize;
}

// ---- end of core --------------------------------------------------------------

//-----------------------------------------------------------------------------
// Windows file API redirect
//-----------------------------------------------------------------------------
typedef HANDLE ( WINAPI *CreateFileW_t )( LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE );
typedef HANDLE ( WINAPI *CreateFileA_t )( LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE );
typedef BOOL ( WINAPI *ReadFile_t )( HANDLE, LPVOID, DWORD, LPDWORD, LPOVERLAPPED );
typedef DWORD ( WINAPI *SetFilePointer_t )( HANDLE, LONG, PLONG, DWORD );
typedef BOOL ( WINAPI *SetFilePointerEx_t )( HANDLE, LARGE_INTEGER, PLARGE_INTEGER, DWORD );
typedef DWORD ( WINAPI *GetFileSize_t )( HANDLE, LPDWORD );
typedef BOOL ( WINAPI *GetFileSizeEx_t )( HANDLE, PLARGE_INTEGER );
typedef BOOL ( WINAPI *GetFileInformationByHandle_t )( HANDLE, LPBY_HANDLE_FILE_INFORMATION );
typedef BOOL ( WINAPI *GetFileInformationByHandleEx_t )( HANDLE, FILE_INFO_BY_HANDLE_CLASS, LPVOID, DWORD );
typedef BOOL ( WINAPI *CloseHandle_t )( HANDLE );
typedef DWORD ( WINAPI *GetFileAttributesW_t )( LPCWSTR );
typedef DWORD ( WINAPI *GetFileAttributesA_t )( LPCSTR );
typedef BOOL ( WINAPI *GetFileAttributesExW_t )( LPCWSTR, GET_FILEEX_INFO_LEVELS, LPVOID );
typedef BOOL ( WINAPI *GetFileAttributesExA_t )( LPCSTR, GET_FILEEX_INFO_LEVELS, LPVOID );
typedef HANDLE ( WINAPI *FindFirstFileExW_t )( LPCWSTR, FINDEX_INFO_LEVELS, LPVOID, FINDEX_SEARCH_OPS, LPVOID, DWORD );
typedef HANDLE ( WINAPI *FindFirstFileA_t )( LPCSTR, LPWIN32_FIND_DATAA );
typedef BOOL ( WINAPI *FindNextFileW_t )( HANDLE, LPWIN32_FIND_DATAW );
typedef BOOL ( WINAPI *FindNextFileA_t )( HANDLE, LPWIN32_FIND_DATAA );
typedef BOOL ( WINAPI *FindClose_t )( HANDLE );

static CreateFileW_t					s_pfnCreateFileW;
static CreateFileA_t					s_pfnCreateFileA;
static ReadFile_t						s_pfnReadFile;
static SetFilePointer_t					s_pfnSetFilePointer;
static SetFilePointerEx_t				s_pfnSetFilePointerEx;
static GetFileSize_t					s_pfnGetFileSize;
static GetFileSizeEx_t					s_pfnGetFileSizeEx;
static GetFileInformationByHandle_t		s_pfnGetFileInformationByHandle;
static GetFileInformationByHandleEx_t	s_pfnGetFileInformationByHandleEx;
static CloseHandle_t					s_pfnCloseHandle;
static GetFileAttributesW_t				s_pfnGetFileAttributesW;
static GetFileAttributesA_t				s_pfnGetFileAttributesA;
static GetFileAttributesExW_t			s_pfnGetFileAttributesExW;
static GetFileAttributesExA_t			s_pfnGetFileAttributesExA;
static FindFirstFileExW_t				s_pfnFindFirstFileExW;
static FindFirstFileA_t					s_pfnFindFirstFileA;
static FindNextFileW_t					s_pfnFindNextFileW;
static FindNextFileA_t					s_pfnFindNextFileA;
static FindClose_t						s_pfnFindClose;

// Views, one per (file, size, write time); kept for the life of the process
struct BCCachedMap_t
{
	wchar_t			m_szPath[MAX_PATH];
	bc_int64		m_nSize;
	FILETIME		m_WriteTime;
	BCVirtualMap_t	*m_pMap;	// NULL: this file needs no view
};

// A handle opened on a redirected map
struct BCSharedReader_t;
struct BCVPKChunk_t;

struct BCTrackedHandle_t
{
	HANDLE			m_hFile;	// the caller's handle
	HANDLE			m_hReader;	// our own synchronous handle on the same file
	BCVirtualMap_t	*m_pMap;
	bc_int64		m_nPosition;
	bc_int64		m_nBaseOffset;	// > 0: a file inside a .gma, at this offset of it
	bool			m_bOwnsMap;		// m_pMap belongs to this handle
	BCSharedReader_t	*m_pShared;	// .gma files: the reader and view, released by the last user
	BCVPKChunk_t		*m_pVPK;	// a VPK archive chunk holding maps ( m_pMap is its size only )
};

static SRWLOCK								s_Lock = SRWLOCK_INIT;
static CUtlVector< BCCachedMap_t >			s_CachedMaps;
static CUtlVector< BCTrackedHandle_t >		s_TrackedHandles;
static volatile LONG						s_nTrackedHandles = 0;	// lock-free fast path

// A file inside a .gma: its reader handle and view are counted, so a CloseHandle
// on one thread can't close the reader or free the view while another thread is
// still reading through it (the reader's handle value could otherwise be reused
// by a new handle mid-read)
struct BCSharedReader_t
{
	volatile LONG	m_nRefs;
	HANDLE			m_hReader;
	BCVirtualMap_t	*m_pOwnedMap;
};

static void ReleaseSharedReader( BCSharedReader_t *pShared, BOOL ( WINAPI *pfnClose )( HANDLE ) )
{
	if ( !pShared || InterlockedDecrement( &pShared->m_nRefs ) != 0 )
		return;
	if ( pShared->m_hReader && pShared->m_hReader != INVALID_HANDLE_VALUE )
		pfnClose( pShared->m_hReader );
	delete pShared->m_pOwnedMap;
	delete pShared;
}

static bool ReaderRead( void *pContext, bc_int64 nOffset, void *pDest, int nSize )
{
	HANDLE hReader = (HANDLE)pContext;
	unsigned char *pOut = (unsigned char *)pDest;
	while ( nSize > 0 )
	{
		OVERLAPPED ov;
		memset( &ov, 0, sizeof( ov ) );
		ov.Offset = (DWORD)( nOffset & 0xFFFFFFFF );
		ov.OffsetHigh = (DWORD)( nOffset >> 32 );
		DWORD nRead = 0;
		if ( !s_pfnReadFile( hReader, pOut, (DWORD)nSize, &nRead, &ov ) || nRead == 0 )
			return false;
		pOut += nRead;
		nOffset += nRead;
		nSize -= nRead;
	}
	return true;
}

static bool IsMapPath( LPCWSTR pszPath )
{
	if ( !pszPath )
		return false;
	size_t nLen = wcslen( pszPath );
	return nLen > 4 && !_wcsicmp( pszPath + nLen - 4, L".bsp" );
}

// Finds a tracked handle; call with s_Lock held
static int FindTracked( HANDLE hFile )
{
	for ( int i = 0; i < s_TrackedHandles.Count(); i++ )
	{
		if ( s_TrackedHandles[i].m_hFile == hFile )
			return i;
	}
	return -1;
}


//-----------------------------------------------------------------------------
// Models (version 37). Views are memory-only: the whole converted file is one
// patch over an "original" of size 0.
//-----------------------------------------------------------------------------
enum BCModelFile_t
{
	BC_MODELFILE_NONE,
	BC_MODELFILE_MDL,
	BC_MODELFILE_VVD,
	BC_MODELFILE_VTX,
};

// The .vtx kinds, best source first when one has to be made from another
static const wchar_t *s_pszVTXSuffixes[] = { L".dx90.vtx", L".dx80.vtx", L".sw.vtx", L".dx7_2bone.vtx", L".dx7_1bone.vtx", L".vtx" };

// What kind of model file szPath (lowercase) is; szBase gets the path without its extension(s)
static BCModelFile_t ClassifyModelPath( const wchar_t *szPath, wchar_t *szBase )
{
	size_t nLen = wcslen( szPath );
	BCModelFile_t kind = BC_MODELFILE_NONE;
	size_t nSuffix = 0;
	if ( nLen > 4 && !wcscmp( szPath + nLen - 4, L".mdl" ) )
	{
		kind = BC_MODELFILE_MDL;
		nSuffix = 4;
	}
	else if ( nLen > 4 && !wcscmp( szPath + nLen - 4, L".vvd" ) )
	{
		kind = BC_MODELFILE_VVD;
		nSuffix = 4;
	}
	else
	{
		for ( int i = 0; i < ARRAYSIZE( s_pszVTXSuffixes ); i++ )
		{
			size_t nSuffixLen = wcslen( s_pszVTXSuffixes[i] );
			if ( nLen > nSuffixLen && !wcscmp( szPath + nLen - nSuffixLen, s_pszVTXSuffixes[i] ) )
			{
				kind = BC_MODELFILE_VTX;
				nSuffix = nSuffixLen;
				break;
			}
		}
	}
	if ( kind == BC_MODELFILE_NONE || nLen - nSuffix >= MAX_PATH - 16 )
		return BC_MODELFILE_NONE;
	wcsncpy_s( szBase, MAX_PATH, szPath, nLen - nSuffix );
	return kind;
}

// Cheap test on the raw path, before any normalizing
static bool IsModelPath( LPCWSTR pszPath )
{
	if ( !pszPath )
		return false;
	size_t nLen = wcslen( pszPath );
	return nLen > 4 && ( !_wcsicmp( pszPath + nLen - 4, L".mdl" ) || !_wcsicmp( pszPath + nLen - 4, L".vvd" ) || !_wcsicmp( pszPath + nLen - 4, L".vtx" ) );
}

// szOut = szBase + pszSuffix (szOut holds MAX_PATH)
static void JoinPath( wchar_t *szOut, const wchar_t *szBase, const wchar_t *pszSuffix )
{
	wcsncpy_s( szOut, MAX_PATH, szBase, _TRUNCATE );
	wcsncat_s( szOut, MAX_PATH, pszSuffix, _TRUNCATE );
}

static bool NormalizePath( LPCWSTR pszPath, wchar_t *szOut )
{
	if ( !GetFullPathNameW( pszPath, MAX_PATH, szOut, NULL ) )
		return false;
	_wcslwr_s( szOut, MAX_PATH );
	return true;
}

static bool StatFile( const wchar_t *szPath, bc_int64 *pnSize, FILETIME *pTime )
{
	WIN32_FILE_ATTRIBUTE_DATA data;
	if ( !s_pfnGetFileAttributesExW( szPath, GetFileExInfoStandard, &data ) || ( data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY ) )
		return false;
	*pnSize = ( (bc_int64)data.nFileSizeHigh << 32 ) | data.nFileSizeLow;
	*pTime = data.ftLastWriteTime;
	return true;
}

// Reads up to nMax bytes from the start of the file
static bool ReadFileStart( const wchar_t *szPath, CUtlVector< unsigned char > &data, int nMax )
{
	HANDLE hFile = s_pfnCreateFileW( szPath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
		NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL );
	if ( hFile == INVALID_HANDLE_VALUE )
		return false;

	bool bOK = false;
	BY_HANDLE_FILE_INFORMATION info;
	if ( s_pfnGetFileInformationByHandle( hFile, &info ) && !info.nFileSizeHigh && info.nFileSizeLow < 0x10000000 )
	{
		int nSize = MIN( (int)info.nFileSizeLow, nMax );
		data.SetCount( nSize );
		bOK = true;
		int nDone = 0;
		while ( nDone < nSize )
		{
			DWORD nRead = 0;
			if ( !s_pfnReadFile( hFile, data.Base() + nDone, (DWORD)( nSize - nDone ), &nRead, NULL ) || !nRead )
			{
				bOK = false;
				break;
			}
			nDone += nRead;
		}
	}
	s_pfnCloseHandle( hFile );
	return bOK;
}

static BCVirtualMap_t *MakeMemoryView( const void *pData, int nSize )
{
	BCVirtualMap_t *pView = new BCVirtualMap_t;
	pView->m_nOriginalSize = 0;
	pView->m_nVirtualSize = nSize;
	AddPatch( pView, 0, pData, nSize );
	return pView;
}

static unsigned int HashPath( const wchar_t *sz )
{
	unsigned int n = 2166136261u;
	for ( ; *sz; sz++ )
		n = ( n ^ (unsigned int)*sz ) * 16777619u;
	return n;
}

// One per .mdl seen, v37 or not; kept for the life of the process
struct BCModelEntry_t
{
	wchar_t					m_szBase[MAX_PATH];		// lowercase full path without ".mdl"
	unsigned int			m_nHash;
	bc_int64				m_nSize;
	FILETIME				m_WriteTime;
	BCVirtualMap_t			*m_pMdlView;			// NULL: not a model that needs converting
	BCVirtualMap_t			*m_pVvdView;
	MDLCompat::CConvert37	*m_pConvert;			// what the VTX conversion needs to know
};

// A converted or stand-in .vtx
struct BCVtxEntry_t
{
	wchar_t					m_szPath[MAX_PATH];		// the .vtx asked for
	unsigned int			m_nHash;
	bc_int64				m_nSourceSize;			// the file it was made from
	FILETIME				m_SourceTime;
	const BCModelEntry_t	*m_pModel;
	BCVirtualMap_t			*m_pView;				// NULL: read the file as it is
};

static CUtlVector< BCModelEntry_t * >	s_Models;
static CUtlVector< BCVtxEntry_t * >		s_Vtxs;

// The entry for the model at szBase + ".mdl", or NULL if there's no such file
// -heapcheck (the engine's own switch: it checks the heap every frame): also
// check it right before and after each model conversion here, and name the
// model if the heap breaks while bsp_compat is working on it.
static void CheckHeap( const char *pszWhen, const wchar_t *szFile )
{
	static int s_nHeapCheck = -1;
	if ( s_nHeapCheck < 0 )
		s_nHeapCheck = CommandLine()->FindParm( "-heapcheck" ) ? 1 : 0;
	if ( !s_nHeapCheck )
		return;
	const int nResult = g_pMemAlloc->heapchk();
	if ( nResult != -2 /* _HEAPOK */ )
	{
		static bool s_bReported = false;
		if ( !s_bReported )
		{
			s_bReported = true;
			char szName[MAX_PATH];
			WideCharToMultiByte( CP_UTF8, 0, szFile, -1, szName, sizeof( szName ), NULL, NULL );
			Warning( "HEAP CORRUPT (%d) %s %s\n", nResult, pszWhen, szName );
			DebuggerBreakIfDebugging();
		}
	}
}

static BCModelEntry_t *GetModelEntry( const wchar_t *szBase )
{
	wchar_t szMdl[MAX_PATH];
	JoinPath( szMdl, szBase, L".mdl" );
	bc_int64 nSize;
	FILETIME time;
	if ( !StatFile( szMdl, &nSize, &time ) )
		return NULL;

	const unsigned int nHash = HashPath( szBase );
	BCModelEntry_t *pFound = NULL;
	AcquireSRWLockShared( &s_Lock );
	for ( int i = 0; i < s_Models.Count(); i++ )
	{
		BCModelEntry_t *pEntry = s_Models[i];
		if ( pEntry->m_nHash == nHash && pEntry->m_nSize == nSize && !CompareFileTime( &pEntry->m_WriteTime, &time ) && !wcscmp( pEntry->m_szBase, szBase ) )
		{
			pFound = pEntry;
			break;
		}
	}
	ReleaseSRWLockShared( &s_Lock );
	if ( pFound )
		return pFound;

	BCModelEntry_t *pEntry = new BCModelEntry_t;
	memset( pEntry, 0, sizeof( *pEntry ) );
	wcsncpy_s( pEntry->m_szBase, szBase, _TRUNCATE );
	pEntry->m_nHash = nHash;
	pEntry->m_nSize = nSize;
	pEntry->m_WriteTime = time;

	CUtlVector< unsigned char > data;
	if ( ReadFileStart( szMdl, data, 8 ) && data.Count() == 8 &&
		 !memcmp( data.Base(), "IDST", 4 ) && *(const int *)( data.Base() + 4 ) == 37 &&
		 ReadFileStart( szMdl, data, 0x10000000 ) )
	{
		char szName[MAX_PATH];
		WideCharToMultiByte( CP_UTF8, 0, szMdl, -1, szName, sizeof( szName ), NULL, NULL );

		// the .vvd lists as many LODs as the model's VTX has
		int nLODs = 1;
		for ( int i = 0; i < ARRAYSIZE( s_pszVTXSuffixes ); i++ )
		{
			wchar_t szVtx[MAX_PATH];
			JoinPath( szVtx, szBase, s_pszVTXSuffixes[i] );
			CUtlVector< unsigned char > vtxHeader;
			if ( ReadFileStart( szVtx, vtxHeader, 36 ) && vtxHeader.Count() == 36 )
			{
				nLODs = *(const int *)( vtxHeader.Base() + 20 );
				break;
			}
		}

		CheckHeap( "before converting", szMdl );
		MDLCompat::CConvert37 *pConvert = new MDLCompat::CConvert37( data.Base(), data.Count() );
		MDLCompat::CByteBuf mdl, vvd;
		char szError[256];
		if ( pConvert->Convert( mdl, vvd, nLODs, szError, sizeof( szError ) ) )
		{
			pEntry->m_pMdlView = MakeMemoryView( mdl.Base(), mdl.Size() );
			pEntry->m_pVvdView = MakeMemoryView( vvd.Base(), vvd.Size() );
			pEntry->m_pConvert = pConvert;
			pConvert->ReleaseWorkData();
			CheckHeap( "after converting", szMdl );
			DevMsg( "MDL v37: %s is read as a version 48 model\n", szName );
		}
		else
		{
			delete pConvert;
			Warning( "MDL v37: couldn't convert %s: %s\n", szName, szError );
		}
	}

	AcquireSRWLockExclusive( &s_Lock );
	s_Models.AddToTail( pEntry );	// a second thread may have made the same one; both stay valid
	ReleaseSRWLockExclusive( &s_Lock );
	return pEntry;
}

// The view of a .vtx of a converted model. szPath is the one asked for;
// szSource gets the file the view stands in for.
static BCVirtualMap_t *GetVtxView( const BCModelEntry_t *pModel, const wchar_t *szPath, const wchar_t *szBase, bool bExists, wchar_t *szSource )
{
	bc_int64 nSize = 0;
	FILETIME time;
	if ( bExists )
	{
		wcsncpy_s( szSource, MAX_PATH, szPath, _TRUNCATE );
		if ( !StatFile( szSource, &nSize, &time ) )
			return NULL;
	}
	else
	{
		bool bFound = false;
		for ( int i = 0; i < ARRAYSIZE( s_pszVTXSuffixes ) && !bFound; i++ )
		{
			JoinPath( szSource, szBase, s_pszVTXSuffixes[i] );
			bFound = StatFile( szSource, &nSize, &time );
		}
		if ( !bFound )
			return NULL;
	}

	const unsigned int nHash = HashPath( szPath );
	BCVtxEntry_t *pFound = NULL;
	AcquireSRWLockShared( &s_Lock );
	for ( int i = 0; i < s_Vtxs.Count(); i++ )
	{
		BCVtxEntry_t *pEntry = s_Vtxs[i];
		if ( pEntry->m_nHash == nHash && pEntry->m_pModel == pModel && pEntry->m_nSourceSize == nSize &&
			 !CompareFileTime( &pEntry->m_SourceTime, &time ) && !wcscmp( pEntry->m_szPath, szPath ) )
		{
			pFound = pEntry;
			break;
		}
	}
	ReleaseSRWLockShared( &s_Lock );
	if ( pFound )
		return pFound->m_pView;

	BCVtxEntry_t *pEntry = new BCVtxEntry_t;
	memset( pEntry, 0, sizeof( *pEntry ) );
	wcsncpy_s( pEntry->m_szPath, szPath, _TRUNCATE );
	pEntry->m_nHash = nHash;
	pEntry->m_nSourceSize = nSize;
	pEntry->m_SourceTime = time;
	pEntry->m_pModel = pModel;

	CUtlVector< unsigned char > data;
	CheckHeap( "before the VTX of", szSource );
	if ( ReadFileStart( szSource, data, 0x10000000 ) && data.Count() >= 4 )
	{
		char szName[MAX_PATH];
		WideCharToMultiByte( CP_UTF8, 0, szSource, -1, szName, sizeof( szName ), NULL, NULL );
		const int nVersion = *(const int *)data.Base();
		if ( nVersion == 6 )
		{
			MDLCompat::CByteBuf vtx;
			char szError[256];
			if ( MDLCompat::ConvertVTX6( data.Base(), data.Count(), *pModel->m_pConvert, vtx, szError, sizeof( szError ) ) )
				pEntry->m_pView = MakeMemoryView( vtx.Base(), vtx.Size() );
			else
				Warning( "MDL v37: couldn't convert %s: %s\n", szName, szError );
		}
		else if ( !bExists )
		{
			// already version 7: stands in as it is
			pEntry->m_pView = MakeMemoryView( data.Base(), data.Count() );
		}
	}

	CheckHeap( "after the VTX of", szSource );
	AcquireSRWLockExclusive( &s_Lock );
	s_Vtxs.AddToTail( pEntry );
	ReleaseSRWLockExclusive( &s_Lock );
	return pEntry->m_pView;
}

// The view to serve for a model file, or NULL to leave it alone. szPath is
// lowercase and full. bExists: the file is on disk (otherwise only files a
// converted model needs are made up). szSource gets the file to open for it.
static BCVirtualMap_t *GetModelFileView( const wchar_t *szPath, bool bExists, wchar_t *szSource )
{
	wchar_t szBase[MAX_PATH];
	BCModelFile_t kind = ClassifyModelPath( szPath, szBase );
	if ( kind == BC_MODELFILE_NONE )
		return NULL;
	if ( kind == BC_MODELFILE_MDL && !bExists )
		return NULL;

	BCModelEntry_t *pModel = GetModelEntry( szBase );
	if ( !pModel || !pModel->m_pMdlView )
		return NULL;

	switch ( kind )
	{
	case BC_MODELFILE_MDL:
		wcsncpy_s( szSource, MAX_PATH, szPath, _TRUNCATE );
		return pModel->m_pMdlView;

	case BC_MODELFILE_VVD:
		if ( bExists )
			wcsncpy_s( szSource, MAX_PATH, szPath, _TRUNCATE );
		else
			JoinPath( szSource, szBase, L".mdl" );
		return pModel->m_pVvdView;

	case BC_MODELFILE_VTX:
		return GetVtxView( pModel, szPath, szBase, bExists, szSource );

	default:
		return NULL;
	}
}

static void AddTrackedHandle( HANDLE hFile, HANDLE hReader, BCVirtualMap_t *pMap, bc_int64 nBaseOffset = 0, bool bOwnsMap = false, BCSharedReader_t *pShared = NULL,
	BCVPKChunk_t *pVPK = NULL )
{
	AcquireSRWLockExclusive( &s_Lock );
	BCTrackedHandle_t &tracked = s_TrackedHandles[s_TrackedHandles.AddToTail()];
	tracked.m_hFile = hFile;
	tracked.m_hReader = hReader;
	tracked.m_pMap = pMap;
	tracked.m_nPosition = 0;
	tracked.m_nBaseOffset = nBaseOffset;
	tracked.m_bOwnsMap = bOwnsMap;
	tracked.m_pShared = pShared;
	tracked.m_pVPK = pVPK;
	InterlockedIncrement( &s_nTrackedHandles );
	ReleaseSRWLockExclusive( &s_Lock );
}

static bool IsWriteAccess( DWORD nAccess )
{
	return ( nAccess & ( GENERIC_WRITE | GENERIC_ALL | FILE_WRITE_DATA | FILE_APPEND_DATA ) ) != 0;
}

// An opened model file: served from a view if it needs one. Handles opened
// only for attributes are tracked too, so the size they report is the view's.
static void TrackIfModel( HANDLE hFile, LPCWSTR pszPath, DWORD nAccess, DWORD nFlagsAndAttributes )
{
	if ( ( nFlagsAndAttributes & BSPCOMPAT_BYPASS_ATTRIBUTE ) || IsWriteAccess( nAccess ) )
		return;
	wchar_t szPath[MAX_PATH], szSource[MAX_PATH];
	if ( !NormalizePath( pszPath, szPath ) )
		return;
	BCVirtualMap_t *pView = GetModelFileView( szPath, true, szSource );
	if ( pView )
		AddTrackedHandle( hFile, NULL, pView );
}

// A model file that isn't on disk but that a converted model needs (its
// .vvd, or a .vtx kind it doesn't have): opens the file the view stands in
// for, and serves the view from it.
static HANDLE OpenMadeUpModelFile( LPCWSTR pszPath, DWORD nAccess, DWORD nShare, LPSECURITY_ATTRIBUTES pSecurity,
	DWORD nDisposition, DWORD nFlagsAndAttributes, HANDLE hTemplate )
{
	if ( ( nFlagsAndAttributes & BSPCOMPAT_BYPASS_ATTRIBUTE ) || IsWriteAccess( nAccess ) || nDisposition != OPEN_EXISTING )
		return INVALID_HANDLE_VALUE;
	wchar_t szPath[MAX_PATH], szSource[MAX_PATH];
	if ( !NormalizePath( pszPath, szPath ) )
		return INVALID_HANDLE_VALUE;
	BCVirtualMap_t *pView = GetModelFileView( szPath, false, szSource );
	if ( !pView )
		return INVALID_HANDLE_VALUE;
	HANDLE hFile = s_pfnCreateFileW( szSource, nAccess, nShare | FILE_SHARE_READ, pSecurity, OPEN_EXISTING, nFlagsAndAttributes, hTemplate );
	if ( hFile != INVALID_HANDLE_VALUE )
		AddTrackedHandle( hFile, NULL, pView );
	return hFile;
}

// Attributes of a model file as the views have it: the view's size, and made-up
// files exist. Returns false to report what the real call did.
static bool GetModelFileAttributes( LPCWSTR pszPath, bool bExists, WIN32_FILE_ATTRIBUTE_DATA *pData )
{
	wchar_t szPath[MAX_PATH], szSource[MAX_PATH];
	if ( !NormalizePath( pszPath, szPath ) )
		return false;
	BCVirtualMap_t *pView = GetModelFileView( szPath, bExists, szSource );
	if ( !pView )
		return false;
	if ( !bExists && !s_pfnGetFileAttributesExW( szSource, GetFileExInfoStandard, pData ) )
		return false;
	pData->nFileSizeHigh = (DWORD)( pView->m_nVirtualSize >> 32 );
	pData->nFileSizeLow = (DWORD)( pView->m_nVirtualSize & 0xFFFFFFFF );
	return true;
}

static bool IsNotFoundError( DWORD nError )
{
	return nError == ERROR_FILE_NOT_FOUND;
}

static bool TrackIfVPK( HANDLE hFile, LPCWSTR pszPath, DWORD nAccess, DWORD nFlagsAndAttributes );

static void TrackIfMap( HANDLE hFile, LPCWSTR pszPath, DWORD nAccess, DWORD nFlagsAndAttributes )
{
	if ( hFile == INVALID_HANDLE_VALUE )
		return;
	if ( TrackIfVPK( hFile, pszPath, nAccess, nFlagsAndAttributes ) )
		return;
	if ( IsModelPath( pszPath ) )
	{
		TrackIfModel( hFile, pszPath, nAccess, nFlagsAndAttributes );
		return;
	}
	if ( !IsMapPath( pszPath ) )
		return;
	if ( nFlagsAndAttributes & BSPCOMPAT_BYPASS_ATTRIBUTE )
		return;
	if ( nAccess & ( GENERIC_WRITE | GENERIC_ALL | FILE_WRITE_DATA | FILE_APPEND_DATA ) )
		return;
	if ( !( nAccess & ( GENERIC_READ | FILE_READ_DATA ) ) )
		return;

	HANDLE hReader = s_pfnCreateFileW( pszPath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
		NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL );
	if ( hReader == INVALID_HANDLE_VALUE )
		return;

	BY_HANDLE_FILE_INFORMATION info;
	if ( !s_pfnGetFileInformationByHandle( hReader, &info ) )
	{
		s_pfnCloseHandle( hReader );
		return;
	}
	const bc_int64 nSize = ( (bc_int64)info.nFileSizeHigh << 32 ) | info.nFileSizeLow;

	wchar_t szPath[MAX_PATH];
	if ( !GetFullPathNameW( pszPath, MAX_PATH, szPath, NULL ) )
		wcsncpy_s( szPath, pszPath, _TRUNCATE );
	_wcslwr_s( szPath );

	// Nothing that can log, allocate through the engine or close handles runs
	// while s_Lock is held: those can come back into these hooks.
	BCVirtualMap_t *pMap = NULL;
	bool bCached = false;

	AcquireSRWLockShared( &s_Lock );
	for ( int i = 0; i < s_CachedMaps.Count(); i++ )
	{
		BCCachedMap_t &cached = s_CachedMaps[i];
		if ( cached.m_nSize == nSize && !CompareFileTime( &cached.m_WriteTime, &info.ftLastWriteTime ) && !wcscmp( cached.m_szPath, szPath ) )
		{
			pMap = cached.m_pMap;
			bCached = true;
			break;
		}
	}
	ReleaseSRWLockShared( &s_Lock );

	if ( !bCached )
	{
		char szName[MAX_PATH];
		WideCharToMultiByte( CP_UTF8, 0, szPath, -1, szName, sizeof( szName ), NULL, NULL );
		pMap = BuildVirtualMap( ReaderRead, hReader, nSize, szName );
		if ( pMap )
			DevMsg( "BSP compat: %s is read through a converted view\n", szName );

		AcquireSRWLockExclusive( &s_Lock );
		BCCachedMap_t &cached = s_CachedMaps[s_CachedMaps.AddToTail()];
		wcsncpy_s( cached.m_szPath, szPath, _TRUNCATE );
		cached.m_nSize = nSize;
		cached.m_WriteTime = info.ftLastWriteTime;
		cached.m_pMap = pMap;	// a second thread may have built the same one; both stay valid
		ReleaseSRWLockExclusive( &s_Lock );
	}

	if ( !pMap )
	{
		s_pfnCloseHandle( hReader );
		return;
	}

	AddTrackedHandle( hFile, hReader, pMap );
}

//-----------------------------------------------------------------------------
// Garry's Mod addons (gma_mount.cpp): files under the mount folder are served
// from inside their .gma. Nothing under it can be written.
//-----------------------------------------------------------------------------
// -gmadebug: every file operation on mounted addon files, to <mod>/cache/gma_debug.log
static HANDLE	s_hGMALog = INVALID_HANDLE_VALUE;
static SRWLOCK	s_GMALogLock = SRWLOCK_INIT;

static void GMALog( const char *pszFormat, ... )
{
	if ( s_hGMALog == INVALID_HANDLE_VALUE )
		return;
	const DWORD nError = GetLastError();
	char szLine[1024];
	va_list args;
	va_start( args, pszFormat );
	int nLen = _vsnprintf_s( szLine, sizeof( szLine ), _TRUNCATE, pszFormat, args );
	va_end( args );
	if ( nLen < 0 )
		nLen = (int)strlen( szLine );
	AcquireSRWLockExclusive( &s_GMALogLock );
	DWORD nWritten;
	if ( s_hGMALog != INVALID_HANDLE_VALUE )	// gma_debuglog 0 may have closed it meanwhile
		WriteFile( s_hGMALog, szLine, (DWORD)nLen, &nWritten, NULL );
	ReleaseSRWLockExclusive( &s_GMALogLock );
	SetLastError( nError );
}

// At startup with -gmadebug, or any time with the gma_debuglog command (bForce)
static void OpenGMALog( bool bForce = false )
{
	if ( s_hGMALog != INVALID_HANDLE_VALUE )
		return;
	if ( !bForce && !CommandLine()->FindParm( "-gmadebug" ) )
		return;
	if ( !GMAMount_GetRoot()[0] )
	{
		Warning( "GMA: no addons are mounted, nothing to log\n" );
		return;
	}
	wchar_t szLog[MAX_PATH];
	wcsncpy_s( szLog, GMAMount_GetRoot(), _TRUNCATE );
	wchar_t *pSlash = wcsrchr( szLog, L'\\' );
	if ( !pSlash )
		return;
	wcscpy_s( pSlash + 1, MAX_PATH - ( pSlash + 1 - szLog ), L"gma_debug.log" );
	HANDLE hLog = s_pfnCreateFileW( szLog, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, NULL );
	if ( hLog == INVALID_HANDLE_VALUE )
	{
		Warning( "GMA: couldn't create %ls (error %u)\n", szLog, GetLastError() );
		return;
	}
	s_hGMALog = hLog;
	Msg( "GMA: logging addon file access to %ls\n", szLog );
	GMALog( "gma debug log, thread %u\n", GetCurrentThreadId() );
}

static void CloseGMALog()
{
	AcquireSRWLockExclusive( &s_GMALogLock );
	HANDLE hLog = s_hGMALog;
	s_hGMALog = INVALID_HANDLE_VALUE;
	ReleaseSRWLockExclusive( &s_GMALogLock );
	if ( hLog != INVALID_HANDLE_VALUE )
		CloseHandle( hLog );	// may run after the hooks are gone
}

#define GMA_LOGGING() ( s_hGMALog != INVALID_HANDLE_VALUE )
struct BCWindow_t
{
	HANDLE		m_hReader;
	bc_int64	m_nBase;
};

static bool WindowRead( void *pContext, bc_int64 nOffset, void *pDest, int nSize )
{
	const BCWindow_t *pWindow = (const BCWindow_t *)pContext;
	return ReaderRead( pWindow->m_hReader, pWindow->m_nBase + nOffset, pDest, nSize );
}

//-----------------------------------------------------------------------------
// Maps inside VPKs
//
// filesystem_stdio reads a file inside a VPK straight out of the archive chunk
// that holds it ( <name>_NNN.vpk, or <name>_dir.vpk itself for archive index
// 0x7FFF ), so the .bsp open TrackIfMap waits for never happens. Instead, a
// chunk whose _dir.vpk lists maps in it is tracked. Each map's view is built
// ( BuildVirtualMap, through a window at the map's offset ) the first time a
// read reaches the map's bytes, so a chunk holding twenty maps only converts the
// one being loaded. Its patches are laid over the chunk at the map's offset.
//
// A view has to keep the map's size: the bytes after it belong to other files.
// That rules out the views that append ( L4D's 68-byte static props, version 18
// maps ): those maps are left as they are, with a warning to extract them.
// Maps with preload bytes ( kept in the _dir.vpk tree ) are left alone too.
//-----------------------------------------------------------------------------
#define BC_VPK_SIGNATURE		0x55AA1234
#define BC_VPK_DIR_ARCHIVE		0x7FFF
#define BC_VPK_MAX_TREE			( 512 * 1024 * 1024 )

struct BCVPKDirEntry_t
{
	int			m_nArchive;
	bc_int64	m_nOffset;		// in its chunk ( in the _dir.vpk: past the tree )
	bc_int64	m_nLength;
	char		m_szName[MAX_PATH];
};

struct BCVPKDir_t
{
	CUtlVector< BCVPKDirEntry_t >	m_Maps;
};

struct BCCachedVPKDir_t
{
	wchar_t		m_szPath[MAX_PATH];
	bc_int64	m_nSize;
	FILETIME	m_WriteTime;
	BCVPKDir_t	*m_pDir;	// NULL: not a VPK we can read
};

struct BCVPKEntry_t
{
	wchar_t						m_szArchive[MAX_PATH];	// the chunk ( lowercase )
	bc_int64					m_nOffset;
	bc_int64					m_nLength;
	char						m_szName[MAX_PATH];		// maps/<name>.bsp
	BCVirtualMap_t * volatile	m_pMap;		// NULL: not built yet, &s_VPKNoView: needs nothing
};

struct BCVPKChunk_t
{
	BCVirtualMap_t					m_Size;		// no patches: the chunk's size, for seeks and size queries
	CUtlVector< BCVPKEntry_t * >	m_Entries;	// sorted by offset
};

struct BCCachedVPK_t
{
	wchar_t			m_szPath[MAX_PATH];
	bc_int64		m_nSize;
	FILETIME		m_WriteTime;
	BCVPKChunk_t	*m_pChunk;	// NULL: holds no maps
};

static BCVirtualMap_t						s_VPKNoView;
static CUtlVector< BCCachedVPKDir_t >		s_CachedVPKDirs;	// under s_Lock
static CUtlVector< BCCachedVPK_t >			s_CachedVPKs;		// under s_Lock
static CUtlVector< BCVPKEntry_t * >			s_VPKEntries;		// under s_Lock: every map in a VPK seen so far, never freed

// The one entry for the map at nOffset of szArchive, so the chunk redirect and the
// .bsp served from a VPK ( OpenVPKMapPath ) share its view. Call without s_Lock.
static BCVPKEntry_t *GetVPKEntry( const wchar_t *szArchive, bc_int64 nOffset, bc_int64 nLength, const char *pszName )
{
	BCVPKEntry_t *pNew = new BCVPKEntry_t;	// allocated before the lock
	wcsncpy_s( pNew->m_szArchive, szArchive, _TRUNCATE );
	pNew->m_nOffset = nOffset;
	pNew->m_nLength = nLength;
	V_strncpy( pNew->m_szName, pszName, sizeof( pNew->m_szName ) );
	pNew->m_pMap = NULL;

	BCVPKEntry_t *pFound = NULL;
	AcquireSRWLockExclusive( &s_Lock );
	for ( int i = 0; i < s_VPKEntries.Count() && !pFound; i++ )
	{
		if ( s_VPKEntries[i]->m_nOffset == nOffset && !wcscmp( s_VPKEntries[i]->m_szArchive, szArchive ) )
			pFound = s_VPKEntries[i];
	}
	if ( !pFound )
		s_VPKEntries.AddToTail( pNew );
	ReleaseSRWLockExclusive( &s_Lock );

	if ( pFound )
	{
		delete pNew;
		return pFound;
	}
	return pNew;
}

// The chunk an entry of szDir ( a _dir.vpk, lowercase ) lives in
static void GetVPKArchivePath( const wchar_t *szDir, int nArchive, wchar_t *szArchive )
{
	wcsncpy_s( szArchive, MAX_PATH, szDir, _TRUNCATE );
	if ( nArchive == BC_VPK_DIR_ARCHIVE )
		return;
	const size_t nLen = wcslen( szArchive );
	if ( nLen < 8 )
		return;
	swprintf_s( szArchive + nLen - 8, MAX_PATH - ( nLen - 8 ), L"_%03d.vpk", nArchive );
}

// pszPath lowercase. szDir gets the _dir.vpk, nArchive the chunk's index.
static bool GetVPKDirPath( const wchar_t *pszPath, wchar_t *szDir, int *pnArchive )
{
	const size_t nLen = wcslen( pszPath );
	if ( nLen < 9 || wcscmp( pszPath + nLen - 4, L".vpk" ) )
		return false;

	if ( !wcscmp( pszPath + nLen - 8, L"_dir.vpk" ) )
	{
		wcsncpy_s( szDir, MAX_PATH, pszPath, _TRUNCATE );
		*pnArchive = BC_VPK_DIR_ARCHIVE;
		return true;
	}

	const wchar_t *pIndex = pszPath + nLen - 7;	// "NNN.vpk"
	if ( pIndex[-1] != L'_' || pIndex[0] < L'0' || pIndex[0] > L'9' || pIndex[1] < L'0' || pIndex[1] > L'9' || pIndex[2] < L'0' || pIndex[2] > L'9' )
		return false;
	if ( nLen - 8 + 8 >= MAX_PATH )
		return false;

	wcsncpy_s( szDir, MAX_PATH, pszPath, nLen - 8 );
	wcscat_s( szDir, MAX_PATH, L"_dir.vpk" );
	*pnArchive = ( pIndex[0] - L'0' ) * 100 + ( pIndex[1] - L'0' ) * 10 + ( pIndex[2] - L'0' );
	return true;
}

static const char *VPKNextString( const unsigned char *pTree, int nTree, int &p )
{
	const int nStart = p;
	while ( p < nTree && pTree[p] )
		p++;
	if ( p >= nTree )
		return NULL;
	p++;
	return (const char *)pTree + nStart;
}

// The tree: extension { path { name { entry } "" } "" } ""
static void ParseVPKTree( const unsigned char *pTree, int nTree, bc_int64 nDataStart, BCVPKDir_t *pDir )
{
	int p = 0;
	for ( ;; )
	{
		const char *pszExt = VPKNextString( pTree, nTree, p );
		if ( !pszExt || !*pszExt )
			return;
		const bool bMap = !V_stricmp( pszExt, "bsp" );

		for ( ;; )
		{
			const char *pszPath = VPKNextString( pTree, nTree, p );
			if ( !pszPath )
				return;
			if ( !*pszPath )
				break;

			for ( ;; )
			{
				const char *pszName = VPKNextString( pTree, nTree, p );
				if ( !pszName )
					return;
				if ( !*pszName )
					break;

				// crc, preload bytes, archive index, offset, length, terminator, preload data
				if ( p + 18 > nTree )
					return;
				const int nPreload = *(const unsigned short *)( pTree + p + 4 );
				const int nArchive = *(const unsigned short *)( pTree + p + 6 );
				const unsigned int nOfs = *(const unsigned int *)( pTree + p + 8 );
				const unsigned int nLen = *(const unsigned int *)( pTree + p + 12 );
				p += 18 + nPreload;

				if ( !bMap || nLen == 0 )
					continue;
				if ( nPreload )
				{
					DevMsg( "BSP compat: %s/%s.bsp has preload bytes in its VPK, left as it is\n", pszPath, pszName );
					continue;
				}

				BCVPKDirEntry_t &entry = pDir->m_Maps[pDir->m_Maps.AddToTail()];
				entry.m_nArchive = nArchive;
				entry.m_nOffset = ( nArchive == BC_VPK_DIR_ARCHIVE ) ? nDataStart + nOfs : nOfs;
				entry.m_nLength = nLen;
				if ( !V_strcmp( pszPath, " " ) )
					V_snprintf( entry.m_szName, sizeof( entry.m_szName ), "%s.bsp", pszName );
				else
					V_snprintf( entry.m_szName, sizeof( entry.m_szName ), "%s/%s.bsp", pszPath, pszName );
			}
		}
	}
}

static BCVPKDir_t *ParseVPKDir( const wchar_t *szDir )
{
	HANDLE hDir = s_pfnCreateFileW( szDir, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
		NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL );
	if ( hDir == INVALID_HANDLE_VALUE )
		return NULL;

	BCVPKDir_t *pDir = NULL;
	unsigned int header[3];
	if ( ReaderRead( hDir, 0, header, sizeof( header ) ) && header[0] == BC_VPK_SIGNATURE && ( header[1] == 1 || header[1] == 2 ) &&
		 header[2] > 0 && header[2] < BC_VPK_MAX_TREE )
	{
		const int nHeaderSize = ( header[1] == 1 ) ? 12 : 28;
		const int nTree = (int)header[2];
		CUtlVector< unsigned char > tree;
		tree.SetCount( nTree );
		if ( ReaderRead( hDir, nHeaderSize, tree.Base(), nTree ) )
		{
			pDir = new BCVPKDir_t;
			ParseVPKTree( tree.Base(), nTree, (bc_int64)nHeaderSize + nTree, pDir );
		}
	}

	s_pfnCloseHandle( hDir );
	return pDir;
}

// The parsed _dir.vpk, cached per ( path, size, write time ). Call without s_Lock.
static BCVPKDir_t *GetVPKDir( const wchar_t *szDir )
{
	WIN32_FILE_ATTRIBUTE_DATA attr;
	if ( !s_pfnGetFileAttributesExW( szDir, GetFileExInfoStandard, &attr ) )
		return NULL;
	const bc_int64 nSize = ( (bc_int64)attr.nFileSizeHigh << 32 ) | attr.nFileSizeLow;

	AcquireSRWLockShared( &s_Lock );
	for ( int i = 0; i < s_CachedVPKDirs.Count(); i++ )
	{
		const BCCachedVPKDir_t &cached = s_CachedVPKDirs[i];
		if ( cached.m_nSize == nSize && !CompareFileTime( &cached.m_WriteTime, &attr.ftLastWriteTime ) && !wcscmp( cached.m_szPath, szDir ) )
		{
			BCVPKDir_t *pDir = cached.m_pDir;
			ReleaseSRWLockShared( &s_Lock );
			return pDir;
		}
	}
	ReleaseSRWLockShared( &s_Lock );

	BCVPKDir_t *pDir = ParseVPKDir( szDir );

	AcquireSRWLockExclusive( &s_Lock );
	BCCachedVPKDir_t &cached = s_CachedVPKDirs[s_CachedVPKDirs.AddToTail()];
	wcsncpy_s( cached.m_szPath, szDir, _TRUNCATE );
	cached.m_nSize = nSize;
	cached.m_WriteTime = attr.ftLastWriteTime;
	cached.m_pDir = pDir;
	ReleaseSRWLockExclusive( &s_Lock );
	return pDir;
}

static BCVPKChunk_t *BuildVPKChunk( const BCVPKDir_t *pDir, int nArchive, bc_int64 nChunkSize, const wchar_t *szChunk )
{
	if ( !pDir )
		return NULL;

	BCVPKChunk_t *pChunk = NULL;
	for ( int i = 0; i < pDir->m_Maps.Count(); i++ )
	{
		const BCVPKDirEntry_t &map = pDir->m_Maps[i];
		if ( map.m_nArchive != nArchive || map.m_nOffset < 0 || map.m_nOffset + map.m_nLength > nChunkSize )
			continue;

		if ( !pChunk )
		{
			pChunk = new BCVPKChunk_t;
			pChunk->m_Size.m_nOriginalSize = nChunkSize;
			pChunk->m_Size.m_nVirtualSize = nChunkSize;
		}

		BCVPKEntry_t *pEntry = GetVPKEntry( szChunk, map.m_nOffset, map.m_nLength, map.m_szName );

		int nInsert = 0;
		while ( nInsert < pChunk->m_Entries.Count() && pChunk->m_Entries[nInsert]->m_nOffset < pEntry->m_nOffset )
			nInsert++;
		pChunk->m_Entries.InsertBefore( nInsert, pEntry );
	}
	return pChunk;
}

// A .vpk opened for reading. Returns true for any .vpk ( TrackIfMap has nothing more to do ).
static bool TrackIfVPK( HANDLE hFile, LPCWSTR pszPath, DWORD nAccess, DWORD nFlagsAndAttributes )
{
	if ( !pszPath )
		return false;
	const size_t nLen = wcslen( pszPath );
	if ( nLen < 4 || _wcsicmp( pszPath + nLen - 4, L".vpk" ) )
		return false;
	if ( ( nFlagsAndAttributes & BSPCOMPAT_BYPASS_ATTRIBUTE ) || IsWriteAccess( nAccess ) || !( nAccess & ( GENERIC_READ | FILE_READ_DATA ) ) )
		return true;

	wchar_t szPath[MAX_PATH];
	if ( !GetFullPathNameW( pszPath, MAX_PATH, szPath, NULL ) )
		wcsncpy_s( szPath, pszPath, _TRUNCATE );
	_wcslwr_s( szPath );

	wchar_t szDir[MAX_PATH];
	int nArchive;
	if ( !GetVPKDirPath( szPath, szDir, &nArchive ) )
		return true;

	HANDLE hReader = s_pfnCreateFileW( szPath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
		NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL );
	if ( hReader == INVALID_HANDLE_VALUE )
		return true;

	BY_HANDLE_FILE_INFORMATION info;
	if ( !s_pfnGetFileInformationByHandle( hReader, &info ) )
	{
		s_pfnCloseHandle( hReader );
		return true;
	}
	const bc_int64 nSize = ( (bc_int64)info.nFileSizeHigh << 32 ) | info.nFileSizeLow;

	BCVPKChunk_t *pChunk = NULL;
	bool bCached = false;
	AcquireSRWLockShared( &s_Lock );
	for ( int i = 0; i < s_CachedVPKs.Count(); i++ )
	{
		const BCCachedVPK_t &cached = s_CachedVPKs[i];
		if ( cached.m_nSize == nSize && !CompareFileTime( &cached.m_WriteTime, &info.ftLastWriteTime ) && !wcscmp( cached.m_szPath, szPath ) )
		{
			pChunk = cached.m_pChunk;
			bCached = true;
			break;
		}
	}
	ReleaseSRWLockShared( &s_Lock );

	if ( !bCached )
	{
		pChunk = BuildVPKChunk( GetVPKDir( szDir ), nArchive, nSize, szPath );

		AcquireSRWLockExclusive( &s_Lock );
		BCCachedVPK_t &cached = s_CachedVPKs[s_CachedVPKs.AddToTail()];
		wcsncpy_s( cached.m_szPath, szPath, _TRUNCATE );
		cached.m_nSize = nSize;
		cached.m_WriteTime = info.ftLastWriteTime;
		cached.m_pChunk = pChunk;	// a second thread may have built the same one; both stay valid
		ReleaseSRWLockExclusive( &s_Lock );
	}

	if ( !pChunk )
	{
		s_pfnCloseHandle( hReader );
		return true;
	}

	AddTrackedHandle( hFile, hReader, &pChunk->m_Size, 0, false, NULL, pChunk );
	return true;
}

// The view of one map in a chunk, built on first use. NULL: it needs none. Call without s_Lock.
static BCVirtualMap_t *GetVPKEntryView( BCVPKEntry_t *pEntry, HANDLE hReader )
{
	BCVirtualMap_t *pMap = pEntry->m_pMap;
	if ( pMap )
		return ( pMap == &s_VPKNoView ) ? NULL : pMap;

	BCWindow_t window = { hReader, pEntry->m_nOffset };
	BCVirtualMap_t *pNew = BuildVirtualMap( WindowRead, &window, pEntry->m_nLength, pEntry->m_szName );
	if ( pNew && ( pNew->m_nVirtualSize != pEntry->m_nLength || pNew->m_nOriginalSize != pEntry->m_nLength ) )
	{
		Warning( "BSP compat: %s needs to grow to be converted, which can't be done inside a VPK. Extract it to a loose .bsp.\n", pEntry->m_szName );
		delete pNew;
		pNew = NULL;
	}

	BCVirtualMap_t *pPublish = pNew ? pNew : &s_VPKNoView;
	BCVirtualMap_t *pPrev = (BCVirtualMap_t *)InterlockedCompareExchangePointer( (PVOID volatile *)&pEntry->m_pMap, pPublish, NULL );
	if ( pPrev )
	{
		delete pNew;	// another thread got there first
		pPublish = pPrev;
	}
	else if ( pNew )
	{
		DevMsg( "BSP compat: %s ( in a VPK ) is read through a converted view\n", pEntry->m_szName );
	}
	return ( pPublish == &s_VPKNoView ) ? NULL : pPublish;
}

// Reads from a tracked chunk: the file's bytes, with the views of the maps in range laid over them
static int VPKRead( BCVPKChunk_t *pChunk, HANDLE hReader, bc_int64 nOffset, void *pDest, int nSize )
{
	const bc_int64 nChunkSize = pChunk->m_Size.m_nVirtualSize;
	if ( nOffset < 0 || nSize <= 0 || nOffset >= nChunkSize )
		return 0;
	if ( nOffset + nSize > nChunkSize )
		nSize = (int)( nChunkSize - nOffset );
	if ( !ReaderRead( hReader, nOffset, pDest, nSize ) )
		return 0;

	unsigned char *pOut = (unsigned char *)pDest;
	const bc_int64 nEnd = nOffset + nSize;
	for ( int i = 0; i < pChunk->m_Entries.Count(); i++ )
	{
		BCVPKEntry_t *pEntry = pChunk->m_Entries[i];
		if ( pEntry->m_nOffset >= nEnd )
			break;
		if ( pEntry->m_nOffset + pEntry->m_nLength <= nOffset )
			continue;

		const BCVirtualMap_t *pMap = GetVPKEntryView( pEntry, hReader );
		if ( !pMap )
			continue;

		for ( int k = 0; k < pMap->m_Patches.Count(); k++ )
		{
			const BCPatch_t *pPatch = pMap->m_Patches[k];
			const bc_int64 nPatchStart = pEntry->m_nOffset + pPatch->m_nOffset;
			const bc_int64 nPatchEnd = nPatchStart + pPatch->m_Data.Count();
			if ( nPatchStart >= nEnd )
				break;
			if ( nPatchEnd <= nOffset )
				continue;

			const bc_int64 nFrom = MAX( nOffset, nPatchStart );
			const bc_int64 nTo = MIN( nEnd, nPatchEnd );
			memcpy( pOut + ( nFrom - nOffset ), pPatch->m_Data.Base() + ( nFrom - nPatchStart ), (size_t)( nTo - nFrom ) );
		}
	}
	return nSize;
}

static bool IsOpenOnly( DWORD nAccess, DWORD nDisposition )
{
	return !IsWriteAccess( nAccess ) && ( nDisposition == OPEN_EXISTING || nDisposition == OPEN_ALWAYS );
}

// Returns false if the path isn't the mount's to answer (the real call should run)
static bool OpenGMAPath( LPCWSTR pszPath, DWORD nAccess, DWORD nShare, LPSECURITY_ATTRIBUTES pSecurity,
	DWORD nDisposition, DWORD nFlagsAndAttributes, HANDLE hTemplate, HANDLE *phFile )
{
	GMAFileInfo_t info;
	const GMAPathKind_t nKind = GMAMount_Lookup( pszPath, &info );
	if ( nKind == GMA_PATH_OUTSIDE || nKind == GMA_PATH_ROOT )
		return false;

	GMALog( "OPEN kind=%d acc=%08x share=%x disp=%u flags=%08x tid=%u %ls\n", (int)nKind, nAccess, nShare, nDisposition, nFlagsAndAttributes, GetCurrentThreadId(), pszPath );
	*phFile = INVALID_HANDLE_VALUE;
	if ( !IsOpenOnly( nAccess, nDisposition ) )
	{
		SetLastError( ERROR_ACCESS_DENIED );
		return true;
	}

	if ( nKind == GMA_PATH_MISSING )
	{
		SetLastError( ERROR_FILE_NOT_FOUND );
		return true;
	}

	if ( nKind == GMA_PATH_DIRECTORY )
	{
		// a folder handle (stat and the like): the real, empty mount folder stands in
		if ( !( nFlagsAndAttributes & FILE_FLAG_BACKUP_SEMANTICS ) )
		{
			SetLastError( ERROR_ACCESS_DENIED );
			return true;
		}
		*phFile = s_pfnCreateFileW( GMAMount_GetRoot(), nAccess, nShare | FILE_SHARE_READ, pSecurity, OPEN_EXISTING, nFlagsAndAttributes, hTemplate );
		return true;
	}

	// a file: a handle on its .gma, read through a window onto its bytes. Nothing
	// is ever really read through this handle (the hooks serve it), so it's opened
	// synchronous and buffered whatever the caller asked: an overlapped handle here
	// could start real I/O on the whole .gma if anything reached it unhooked.
	const DWORD nStripFlags = FILE_FLAG_DELETE_ON_CLOSE | FILE_FLAG_OVERLAPPED | FILE_FLAG_NO_BUFFERING | FILE_FLAG_WRITE_THROUGH | FILE_FLAG_RANDOM_ACCESS | FILE_FLAG_SEQUENTIAL_SCAN;
	HANDLE hFile = s_pfnCreateFileW( info.m_pszArchive, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, pSecurity, OPEN_EXISTING,
		( nFlagsAndAttributes & ~nStripFlags ) | FILE_ATTRIBUTE_NORMAL, NULL );
	if ( hFile == INVALID_HANDLE_VALUE )
	{
		GMALog( "  -> open failed, error %u: %ls\n", GetLastError(), info.m_pszArchive );
		return true;
	}
	HANDLE hReader = s_pfnCreateFileW( info.m_pszArchive, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
		NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL );
	if ( hReader == INVALID_HANDLE_VALUE )
	{
		DWORD nError = GetLastError();
		s_pfnCloseHandle( hFile );
		SetLastError( nError );
		return true;
	}

	BCVirtualMap_t *pView = new BCVirtualMap_t;
	pView->m_nOriginalSize = info.m_nSize;
	pView->m_nVirtualSize = info.m_nSize;
	BCSharedReader_t *pShared = new BCSharedReader_t;
	pShared->m_nRefs = 1;	// the tracked handle's
	pShared->m_hReader = hReader;
	pShared->m_pOwnedMap = pView;
	AddTrackedHandle( hFile, NULL, pView, info.m_nOffset, false, pShared );
	GMALog( "  -> h=%p reader=%p size=%lld base=%lld archive=%ls\n", hFile, hReader, info.m_nSize, info.m_nOffset, info.m_pszArchive );
	*phFile = hFile;
	SetLastError( NO_ERROR );
	return true;
}


//-----------------------------------------------------------------------------
// Maps inside VPKs, as files
//
// The TF2 filesystem mounts a map's pak lump ( its cubemaps, the materials vbsp
// made for them, ... ) by opening the .bsp as a file on disk. For a map inside a
// VPK that open fails and the pak is quietly never mounted: brushes using those
// materials go missing ( checkerboard ) and everything reflects the default
// cubemap. So when a ...\maps\<name>.bsp open or attribute query finds nothing on
// disk and a VPK holds maps/<name>.bsp, the answer comes from the VPK, through the
// map's view ( Black Mesa's cubemaps converted, v21 converted ).
//
// Which VPK: the one whose copy has already been read ( the map being loaded ),
// else the first _dir.vpk listing it, else the other _dir.vpks in the folders of
// the VPKs seen so far.
//-----------------------------------------------------------------------------

// "maps/<name>.bsp" from a path ending in \maps\<name>.bsp
static bool GetVPKMapName( LPCWSTR pszPath, char *szName, int nSize )
{
	if ( !pszPath || !IsMapPath( pszPath ) )
		return false;
	const wchar_t *pFile = wcsrchr( pszPath, L'\\' );
	const wchar_t *pFileSlash = wcsrchr( pszPath, L'/' );
	if ( pFileSlash > pFile )
		pFile = pFileSlash;
	if ( !pFile || pFile - pszPath < 4 || _wcsnicmp( pFile - 4, L"maps", 4 ) )
		return false;
	if ( pFile - pszPath > 4 && pFile[-5] != L'\\' && pFile[-5] != L'/' )
		return false;

	char szFile[MAX_PATH];
	if ( !WideCharToMultiByte( CP_UTF8, 0, pFile + 1, -1, szFile, sizeof( szFile ), NULL, NULL ) )
		return false;
	V_snprintf( szName, nSize, "maps/%s", szFile );
	V_strlower( szName );
	return true;
}

static BCVPKEntry_t *FindVPKMapEntry( const char *pszName, bool bReadOnly )
{
	BCVPKEntry_t *pFound = NULL;
	AcquireSRWLockShared( &s_Lock );
	for ( int i = 0; i < s_VPKEntries.Count() && !pFound; i++ )
	{
		BCVPKEntry_t *pEntry = s_VPKEntries[i];
		if ( ( !bReadOnly || pEntry->m_pMap ) && !V_stricmp( pEntry->m_szName, pszName ) )
			pFound = pEntry;
	}
	ReleaseSRWLockShared( &s_Lock );
	return pFound;
}

static BCVPKEntry_t *FindVPKMapInDirs( const char *pszName )
{
	// copy the dirs out, GetVPKEntry takes the lock
	struct DirMap_t { wchar_t m_szArchive[MAX_PATH]; BCVPKDirEntry_t m_Map; };
	CUtlVector< DirMap_t > found;
	AcquireSRWLockShared( &s_Lock );
	for ( int i = 0; i < s_CachedVPKDirs.Count() && !found.Count(); i++ )
	{
		const BCCachedVPKDir_t &dir = s_CachedVPKDirs[i];
		if ( !dir.m_pDir )
			continue;
		for ( int k = 0; k < dir.m_pDir->m_Maps.Count(); k++ )
		{
			if ( V_stricmp( dir.m_pDir->m_Maps[k].m_szName, pszName ) )
				continue;
			DirMap_t &map = found[found.AddToTail()];
			GetVPKArchivePath( dir.m_szPath, dir.m_pDir->m_Maps[k].m_nArchive, map.m_szArchive );
			map.m_Map = dir.m_pDir->m_Maps[k];
			break;
		}
	}
	ReleaseSRWLockShared( &s_Lock );

	if ( !found.Count() )
		return NULL;
	return GetVPKEntry( found[0].m_szArchive, found[0].m_Map.m_nOffset, found[0].m_Map.m_nLength, found[0].m_Map.m_szName );
}

// Parses the _dir.vpks next to the ones seen so far ( VPKs mounted before the hooks went in )
static void ScanVPKFolders()
{
	CUtlVector< wchar_t * > folders;
	AcquireSRWLockShared( &s_Lock );
	const int nDirs = s_CachedVPKDirs.Count();
	CUtlVector< wchar_t > paths;
	paths.SetCount( nDirs * MAX_PATH );
	for ( int i = 0; i < nDirs; i++ )
		wcsncpy_s( paths.Base() + i * MAX_PATH, MAX_PATH, s_CachedVPKDirs[i].m_szPath, _TRUNCATE );
	ReleaseSRWLockShared( &s_Lock );

	for ( int i = 0; i < nDirs; i++ )
	{
		wchar_t *szFolder = paths.Base() + i * MAX_PATH;
		wchar_t *pSlash = wcsrchr( szFolder, L'\\' );
		if ( !pSlash )
			continue;
		pSlash[1] = 0;

		bool bSeen = false;
		for ( int k = 0; k < folders.Count() && !bSeen; k++ )
			bSeen = !wcscmp( folders[k], szFolder );
		if ( bSeen )
			continue;
		folders.AddToTail( szFolder );

		wchar_t szPattern[MAX_PATH];
		swprintf_s( szPattern, L"%s*_dir.vpk", szFolder );
		WIN32_FIND_DATAW data;
		HANDLE hFind = s_pfnFindFirstFileExW( szPattern, FindExInfoBasic, &data, FindExSearchNameMatch, NULL, 0 );
		if ( hFind == INVALID_HANDLE_VALUE )
			continue;
		do
		{
			wchar_t szDir[MAX_PATH];
			swprintf_s( szDir, L"%s%s", szFolder, data.cFileName );
			_wcslwr_s( szDir );
			GetVPKDir( szDir );
		}
		while ( s_pfnFindNextFileW( hFind, &data ) );
		s_pfnFindClose( hFind );
	}
}

// A map the engine probes for in every search path that no VPK holds ( any loose
// map ) would otherwise go through the whole lookup on every probe. Misses are
// remembered until another _dir.vpk is parsed, and the folders are scanned only
// when _dir.vpks were added since the last scan.
struct BCVPKMiss_t
{
	char	m_szName[MAX_PATH];
};
static CUtlVector< BCVPKMiss_t >	s_VPKMisses;			// under s_Lock
static int							s_nVPKMissDirs = -1;	// s_CachedVPKDirs.Count() the misses are good for
static int							s_nVPKScannedDirs = -1;	// s_CachedVPKDirs.Count() at the last folder scan

static bool IsKnownVPKMiss( const char *pszName )
{
	bool bMiss = false;
	AcquireSRWLockShared( &s_Lock );
	if ( s_nVPKMissDirs == s_CachedVPKDirs.Count() )
	{
		for ( int i = 0; i < s_VPKMisses.Count() && !bMiss; i++ )
			bMiss = !V_stricmp( s_VPKMisses[i].m_szName, pszName );
	}
	ReleaseSRWLockShared( &s_Lock );
	return bMiss;
}

static void AddVPKMiss( const char *pszName )
{
	BCVPKMiss_t miss;
	V_strncpy( miss.m_szName, pszName, sizeof( miss.m_szName ) );
	AcquireSRWLockExclusive( &s_Lock );
	if ( s_nVPKMissDirs != s_CachedVPKDirs.Count() )
	{
		s_VPKMisses.RemoveAll();
		s_nVPKMissDirs = s_CachedVPKDirs.Count();
	}
	if ( s_VPKMisses.Count() < 1024 )
		s_VPKMisses.AddToTail( miss );
	ReleaseSRWLockExclusive( &s_Lock );
}

static BCVPKEntry_t *FindVPKMap( LPCWSTR pszPath )
{
	char szName[MAX_PATH];
	if ( !GetVPKMapName( pszPath, szName, sizeof( szName ) ) )
		return NULL;

	BCVPKEntry_t *pEntry = FindVPKMapEntry( szName, true );
	if ( !pEntry )
		pEntry = FindVPKMapEntry( szName, false );
	if ( pEntry )
		return pEntry;
	if ( IsKnownVPKMiss( szName ) )
		return NULL;

	pEntry = FindVPKMapInDirs( szName );
	if ( !pEntry )
	{
		AcquireSRWLockShared( &s_Lock );
		const int nDirs = s_CachedVPKDirs.Count();
		const bool bScan = ( nDirs != s_nVPKScannedDirs );
		ReleaseSRWLockShared( &s_Lock );
		if ( bScan )
		{
			ScanVPKFolders();
			AcquireSRWLockExclusive( &s_Lock );
			s_nVPKScannedDirs = s_CachedVPKDirs.Count();	// what the scan added counts as scanned
			ReleaseSRWLockExclusive( &s_Lock );
			pEntry = FindVPKMapInDirs( szName );
		}
	}
	if ( !pEntry )
		AddVPKMiss( szName );
	return pEntry;
}

// A handle on the VPK chunk, read through a window at the map's offset, through its view
static HANDLE OpenVPKMapPath( BCVPKEntry_t *pEntry, LPCWSTR pszPath, LPSECURITY_ATTRIBUTES pSecurity, DWORD nFlagsAndAttributes )
{
	const DWORD nStripFlags = FILE_FLAG_DELETE_ON_CLOSE | FILE_FLAG_OVERLAPPED | FILE_FLAG_NO_BUFFERING | FILE_FLAG_WRITE_THROUGH | FILE_FLAG_RANDOM_ACCESS | FILE_FLAG_SEQUENTIAL_SCAN;
	HANDLE hFile = s_pfnCreateFileW( pEntry->m_szArchive, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, pSecurity, OPEN_EXISTING,
		( nFlagsAndAttributes & ~nStripFlags & ~BSPCOMPAT_BYPASS_ATTRIBUTE ) | FILE_ATTRIBUTE_NORMAL, NULL );
	if ( hFile == INVALID_HANDLE_VALUE )
		return INVALID_HANDLE_VALUE;
	HANDLE hReader = s_pfnCreateFileW( pEntry->m_szArchive, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
		NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL );
	if ( hReader == INVALID_HANDLE_VALUE )
	{
		s_pfnCloseHandle( hFile );
		return INVALID_HANDLE_VALUE;
	}

	// BSPCOMPAT_BYPASS_ATTRIBUTE ( staticprop_colormod ): the original bytes
	BCVirtualMap_t *pView = ( nFlagsAndAttributes & BSPCOMPAT_BYPASS_ATTRIBUTE ) ? NULL : GetVPKEntryView( pEntry, hReader );
	BCVirtualMap_t *pOwned = NULL;
	if ( !pView )
	{
		pOwned = new BCVirtualMap_t;
		pOwned->m_nOriginalSize = pEntry->m_nLength;
		pOwned->m_nVirtualSize = pEntry->m_nLength;
		pView = pOwned;
	}

	BCSharedReader_t *pShared = new BCSharedReader_t;
	pShared->m_nRefs = 1;	// the tracked handle's
	pShared->m_hReader = hReader;
	pShared->m_pOwnedMap = pOwned;
	AddTrackedHandle( hFile, NULL, pView, pEntry->m_nOffset, false, pShared );

	DevMsg( "BSP compat: %ls is served from %ls ( %s )\n", pszPath, pEntry->m_szArchive, pEntry->m_szName );
	return hFile;
}

static bool GetVPKMapAttributes( LPCWSTR pszPath, WIN32_FILE_ATTRIBUTE_DATA *pData )
{
	BCVPKEntry_t *pEntry = FindVPKMap( pszPath );
	if ( !pEntry )
		return false;
	WIN32_FILE_ATTRIBUTE_DATA archive;
	if ( !s_pfnGetFileAttributesExW( pEntry->m_szArchive, GetFileExInfoStandard, &archive ) )
		return false;
	memset( pData, 0, sizeof( *pData ) );
	pData->dwFileAttributes = FILE_ATTRIBUTE_ARCHIVE | FILE_ATTRIBUTE_READONLY;
	pData->ftCreationTime = archive.ftCreationTime;
	pData->ftLastAccessTime = archive.ftLastAccessTime;
	pData->ftLastWriteTime = archive.ftLastWriteTime;
	pData->nFileSizeHigh = (DWORD)( (unsigned long long)pEntry->m_nLength >> 32 );
	pData->nFileSizeLow = (DWORD)( pEntry->m_nLength & 0xFFFFFFFF );
	return true;
}

// Attributes of a path under the mount folder. Returns false if it isn't the mount's to answer.
static bool GetGMAAttributes( LPCWSTR pszPath, WIN32_FILE_ATTRIBUTE_DATA *pData, BOOL *pbResult )
{
	GMAFileInfo_t info;
	const GMAPathKind_t nKind = GMAMount_Lookup( pszPath, &info );
	if ( nKind == GMA_PATH_OUTSIDE || nKind == GMA_PATH_ROOT )
		return false;

	//GMALog( "ATTR kind=%d %ls\n", (int)nKind, pszPath );
	if ( nKind == GMA_PATH_MISSING )
	{
		*pbResult = FALSE;
		SetLastError( ERROR_FILE_NOT_FOUND );
		return true;
	}

	memset( pData, 0, sizeof( *pData ) );
	if ( nKind == GMA_PATH_DIRECTORY )
	{
		pData->dwFileAttributes = FILE_ATTRIBUTE_DIRECTORY;
	}
	else
	{
		pData->dwFileAttributes = FILE_ATTRIBUTE_ARCHIVE;
		pData->ftLastWriteTime.dwLowDateTime = (DWORD)( info.m_nWriteTime & 0xFFFFFFFF );
		pData->ftLastWriteTime.dwHighDateTime = (DWORD)( info.m_nWriteTime >> 32 );
		pData->ftCreationTime = pData->ftLastAccessTime = pData->ftLastWriteTime;
		pData->nFileSizeHigh = (DWORD)( (unsigned long long)info.m_nSize >> 32 );
		pData->nFileSizeLow = (DWORD)( info.m_nSize & 0xFFFFFFFF );
	}
	*pbResult = TRUE;
	SetLastError( NO_ERROR );
	return true;
}

static bool ToWide( LPCSTR pszPath, wchar_t *szOut )
{
	return pszPath && MultiByteToWideChar( CP_ACP, 0, pszPath, -1, szOut, MAX_PATH ) > 0;
}

static HANDLE WINAPI Hook_CreateFileW( LPCWSTR pszPath, DWORD nAccess, DWORD nShare, LPSECURITY_ATTRIBUTES pSecurity,
	DWORD nDisposition, DWORD nFlagsAndAttributes, HANDLE hTemplate )
{
	HANDLE hGMA;
	if ( OpenGMAPath( pszPath, nAccess, nShare, pSecurity, nDisposition, nFlagsAndAttributes, hTemplate, &hGMA ) )
		return hGMA;

	HANDLE hFile = s_pfnCreateFileW( pszPath, nAccess, nShare, pSecurity, nDisposition, nFlagsAndAttributes, hTemplate );
	DWORD nError = GetLastError();	// keep what the real call reported (e.g. ERROR_ALREADY_EXISTS)
	if ( hFile != INVALID_HANDLE_VALUE )
	{
		TrackIfMap( hFile, pszPath, nAccess, nFlagsAndAttributes );
	}
	else if ( IsNotFoundError( nError ) && IsOpenOnly( nAccess, nDisposition ) && IsMapPath( pszPath ) )
	{
		BCVPKEntry_t *pEntry = FindVPKMap( pszPath );
		if ( pEntry )
		{
			hFile = OpenVPKMapPath( pEntry, pszPath, pSecurity, nFlagsAndAttributes );
			if ( hFile != INVALID_HANDLE_VALUE )
				nError = NO_ERROR;
		}
	}
	else if ( IsNotFoundError( nError ) && IsModelPath( pszPath ) )
	{
		hFile = OpenMadeUpModelFile( pszPath, nAccess, nShare, pSecurity, nDisposition, nFlagsAndAttributes, hTemplate );
		if ( hFile != INVALID_HANDLE_VALUE )
			nError = NO_ERROR;
	}
	SetLastError( nError );
	return hFile;
}

static HANDLE WINAPI Hook_CreateFileA( LPCSTR pszPath, DWORD nAccess, DWORD nShare, LPSECURITY_ATTRIBUTES pSecurity,
	DWORD nDisposition, DWORD nFlagsAndAttributes, HANDLE hTemplate )
{
	wchar_t szWide[MAX_PATH];
	HANDLE hGMA;
	if ( ToWide( pszPath, szWide ) && OpenGMAPath( szWide, nAccess, nShare, pSecurity, nDisposition, nFlagsAndAttributes, hTemplate, &hGMA ) )
		return hGMA;

	HANDLE hFile = s_pfnCreateFileA( pszPath, nAccess, nShare, pSecurity, nDisposition, nFlagsAndAttributes, hTemplate );
	if ( hFile == INVALID_HANDLE_VALUE && pszPath )
	{
		DWORD nError = GetLastError();
		wchar_t szPath[MAX_PATH];
		BCVPKEntry_t *pEntry = NULL;
		if ( IsNotFoundError( nError ) && IsOpenOnly( nAccess, nDisposition ) && MultiByteToWideChar( CP_ACP, 0, pszPath, -1, szPath, MAX_PATH ) &&
			 IsMapPath( szPath ) && ( pEntry = FindVPKMap( szPath ) ) != NULL )
		{
			hFile = OpenVPKMapPath( pEntry, szPath, pSecurity, nFlagsAndAttributes );
			if ( hFile != INVALID_HANDLE_VALUE )
				nError = NO_ERROR;
		}
		else if ( IsNotFoundError( nError ) && MultiByteToWideChar( CP_ACP, 0, pszPath, -1, szPath, MAX_PATH ) && IsModelPath( szPath ) )
		{
			hFile = OpenMadeUpModelFile( szPath, nAccess, nShare, pSecurity, nDisposition, nFlagsAndAttributes, hTemplate );
			if ( hFile != INVALID_HANDLE_VALUE )
				nError = NO_ERROR;
		}
		SetLastError( nError );
		return hFile;
	}
	if ( hFile != INVALID_HANDLE_VALUE && pszPath )
	{
		DWORD nError = GetLastError();
		wchar_t szPath[MAX_PATH];
		if ( MultiByteToWideChar( CP_ACP, 0, pszPath, -1, szPath, MAX_PATH ) )
		{
			// KernelBase's CreateFileA may go through the hooked CreateFileW itself
			bool bAlreadyTracked;
			AcquireSRWLockShared( &s_Lock );
			bAlreadyTracked = ( FindTracked( hFile ) >= 0 );
			ReleaseSRWLockShared( &s_Lock );
			if ( !bAlreadyTracked )
				TrackIfMap( hFile, szPath, nAccess, nFlagsAndAttributes );
		}
		SetLastError( nError );
	}
	return hFile;
}

static BOOL WINAPI Hook_ReadFile( HANDLE hFile, LPVOID pBuffer, DWORD nToRead, LPDWORD pnRead, LPOVERLAPPED pOverlapped )
{
	if ( !s_nTrackedHandles )
		return s_pfnReadFile( hFile, pBuffer, nToRead, pnRead, pOverlapped );

	AcquireSRWLockShared( &s_Lock );
	int nTracked = FindTracked( hFile );
	BCTrackedHandle_t tracked;
	if ( nTracked >= 0 )
	{
		tracked = s_TrackedHandles[nTracked];
		if ( tracked.m_pShared )
			InterlockedIncrement( &tracked.m_pShared->m_nRefs );	// kept alive until this read is done
	}
	ReleaseSRWLockShared( &s_Lock );

	if ( nTracked < 0 )
		return s_pfnReadFile( hFile, pBuffer, nToRead, pnRead, pOverlapped );

	bc_int64 nOffset = pOverlapped ? ( ( (bc_int64)pOverlapped->OffsetHigh << 32 ) | pOverlapped->Offset ) : tracked.m_nPosition;
	int nRead;
	bool bReadError = false;
	if ( tracked.m_pShared )
	{
		BCWindow_t window = { tracked.m_pShared->m_hReader, tracked.m_nBaseOffset };
		const int nWant = (int)MIN( nToRead, (DWORD)0x7FFFFFFF );
		nRead = VirtualRead( tracked.m_pMap, WindowRead, &window, nOffset, pBuffer, nWant );
		// VirtualRead gives 0 both at the end of the file and when the .gma couldn't be read
		const bc_int64 nLeft = tracked.m_pMap->m_nVirtualSize - nOffset;
		bReadError = ( nRead == 0 && nWant > 0 && nOffset >= 0 && nLeft > 0 );
		const DWORD nReadError = bReadError ? GetLastError() : 0;
		if ( GMA_LOGGING() || bReadError )
			GMALog( "READ h=%p ofs=%lld want=%u got=%d ov=%d ev=%p tid=%u%s\n", hFile, nOffset, nToRead, nRead, pOverlapped != NULL,
				pOverlapped ? pOverlapped->hEvent : NULL, GetCurrentThreadId(), bReadError ? " ** READ ERROR **" : "" );
		if ( bReadError )
		{
			static volatile LONG s_nReadWarnings = 0;
			if ( InterlockedIncrement( &s_nReadWarnings ) <= 20 )
				Warning( "GMA: reading %d bytes at %lld of a mounted addon file failed (error %u)\n", nWant, nOffset, nReadError );
			SetLastError( nReadError );
		}
		ReleaseSharedReader( tracked.m_pShared, s_pfnCloseHandle );
	}
	else if ( tracked.m_pVPK )
	{
		nRead = VPKRead( tracked.m_pVPK, tracked.m_hReader, nOffset, pBuffer, (int)MIN( nToRead, (DWORD)0x7FFFFFFF ) );
	}
	else
	{
		nRead = VirtualRead( tracked.m_pMap, ReaderRead, tracked.m_hReader, nOffset, pBuffer, (int)MIN( nToRead, (DWORD)0x7FFFFFFF ) );
	}

	// like Windows, a read moves the file pointer (overlapped handles don't use it)
	AcquireSRWLockExclusive( &s_Lock );
	nTracked = FindTracked( hFile );
	if ( nTracked >= 0 )
		s_TrackedHandles[nTracked].m_nPosition = nOffset + nRead;
	ReleaseSRWLockExclusive( &s_Lock );

	if ( pnRead )
		*pnRead = (DWORD)nRead;

	if ( bReadError )
	{
		// a real failure, not the end of the file: say so rather than pass it off as EOF
		if ( pOverlapped )
		{
			pOverlapped->Internal = 0xC00000E9;	// STATUS_UNEXPECTED_IO_ERROR
			pOverlapped->InternalHigh = 0;
			if ( pOverlapped->hEvent )
				SetEvent( (HANDLE)( (ULONG_PTR)pOverlapped->hEvent & ~(ULONG_PTR)1 ) );
		}
		SetLastError( ERROR_READ_FAULT );
		return FALSE;
	}

	if ( pOverlapped )
	{
		// completed right away; GetOverlappedResult reads these
		const bool bEOF = ( nRead == 0 && nToRead > 0 );
		pOverlapped->Internal = bEOF ? 0xC0000011 /* STATUS_END_OF_FILE */ : 0 /* STATUS_SUCCESS */;
		pOverlapped->InternalHigh = (ULONG_PTR)nRead;
		if ( pOverlapped->hEvent )
			SetEvent( (HANDLE)( (ULONG_PTR)pOverlapped->hEvent & ~(ULONG_PTR)1 ) );
		if ( bEOF )
		{
			SetLastError( ERROR_HANDLE_EOF );
			return FALSE;
		}
	}
	return TRUE;
}

static bool SeekTracked( HANDLE hFile, bc_int64 nDistance, DWORD nMethod, bc_int64 *pNewPos, bool *pbTracked )
{
	*pbTracked = false;
	if ( !s_nTrackedHandles )
		return false;

	AcquireSRWLockExclusive( &s_Lock );
	int nTracked = FindTracked( hFile );
	bool bOK = false;
	if ( nTracked >= 0 )
	{
		*pbTracked = true;
		BCTrackedHandle_t &tracked = s_TrackedHandles[nTracked];
		bc_int64 nBase = ( nMethod == FILE_BEGIN ) ? 0 : ( nMethod == FILE_CURRENT ) ? tracked.m_nPosition : tracked.m_pMap->m_nVirtualSize;
		bc_int64 nNew = nBase + nDistance;
		if ( nNew >= 0 )
		{
			tracked.m_nPosition = nNew;
			*pNewPos = nNew;
			bOK = true;
		}
		if ( tracked.m_nBaseOffset > 0 )
			GMALog( "SEEK h=%p dist=%lld method=%u -> %lld\n", hFile, nDistance, nMethod, nNew );
	}
	ReleaseSRWLockExclusive( &s_Lock );
	return bOK;
}

static DWORD WINAPI Hook_SetFilePointer( HANDLE hFile, LONG nDistance, PLONG pnDistanceHigh, DWORD nMethod )
{
	bc_int64 nDist = pnDistanceHigh ? ( ( (bc_int64)*pnDistanceHigh << 32 ) | (DWORD)nDistance ) : (bc_int64)nDistance;
	bc_int64 nNew;
	bool bTracked;
	bool bOK = SeekTracked( hFile, nDist, nMethod, &nNew, &bTracked );
	if ( !bTracked )
		return s_pfnSetFilePointer( hFile, nDistance, pnDistanceHigh, nMethod );
	if ( !bOK )
	{
		SetLastError( ERROR_NEGATIVE_SEEK );
		return INVALID_SET_FILE_POINTER;
	}
	if ( pnDistanceHigh )
		*pnDistanceHigh = (LONG)( nNew >> 32 );
	SetLastError( NO_ERROR );
	return (DWORD)( nNew & 0xFFFFFFFF );
}

static BOOL WINAPI Hook_SetFilePointerEx( HANDLE hFile, LARGE_INTEGER nDistance, PLARGE_INTEGER pnNewPos, DWORD nMethod )
{
	bc_int64 nNew;
	bool bTracked;
	bool bOK = SeekTracked( hFile, nDistance.QuadPart, nMethod, &nNew, &bTracked );
	if ( !bTracked )
		return s_pfnSetFilePointerEx( hFile, nDistance, pnNewPos, nMethod );
	if ( !bOK )
	{
		SetLastError( ERROR_NEGATIVE_SEEK );
		return FALSE;
	}
	if ( pnNewPos )
		pnNewPos->QuadPart = nNew;
	return TRUE;
}

// Size of the view if hFile is tracked
static bool GetTrackedSize( HANDLE hFile, bc_int64 *pnSize )
{
	if ( !s_nTrackedHandles )
		return false;
	AcquireSRWLockShared( &s_Lock );
	int nTracked = FindTracked( hFile );
	bool bGMA = false;
	if ( nTracked >= 0 )
	{
		*pnSize = s_TrackedHandles[nTracked].m_pMap->m_nVirtualSize;
		bGMA = s_TrackedHandles[nTracked].m_nBaseOffset > 0;
	}
	ReleaseSRWLockShared( &s_Lock );
	//if ( bGMA )
		//GMALog( "SIZE h=%p -> %lld\n", hFile, *pnSize );
	return nTracked >= 0;
}

static DWORD WINAPI Hook_GetFileSize( HANDLE hFile, LPDWORD pnSizeHigh )
{
	bc_int64 nSize;
	if ( !GetTrackedSize( hFile, &nSize ) )
		return s_pfnGetFileSize( hFile, pnSizeHigh );
	if ( pnSizeHigh )
		*pnSizeHigh = (DWORD)( nSize >> 32 );
	SetLastError( NO_ERROR );
	return (DWORD)( nSize & 0xFFFFFFFF );
}

static BOOL WINAPI Hook_GetFileSizeEx( HANDLE hFile, PLARGE_INTEGER pnSize )
{
	bc_int64 nSize;
	if ( !GetTrackedSize( hFile, &nSize ) )
		return s_pfnGetFileSizeEx( hFile, pnSize );
	pnSize->QuadPart = nSize;
	return TRUE;
}

static BOOL WINAPI Hook_GetFileInformationByHandle( HANDLE hFile, LPBY_HANDLE_FILE_INFORMATION pInfo )
{
	BOOL bResult = s_pfnGetFileInformationByHandle( hFile, pInfo );
	bc_int64 nSize;
	if ( bResult && pInfo && GetTrackedSize( hFile, &nSize ) )
	{
		pInfo->nFileSizeHigh = (DWORD)( nSize >> 32 );
		pInfo->nFileSizeLow = (DWORD)( nSize & 0xFFFFFFFF );
	}
	return bResult;
}

static BOOL WINAPI Hook_GetFileInformationByHandleEx( HANDLE hFile, FILE_INFO_BY_HANDLE_CLASS nClass, LPVOID pInfo, DWORD nSize )
{
	BOOL bResult = s_pfnGetFileInformationByHandleEx( hFile, nClass, pInfo, nSize );
	bc_int64 nFileSize;
	if ( bResult && nClass == FileStandardInfo && pInfo && nSize >= sizeof( FILE_STANDARD_INFO ) && GetTrackedSize( hFile, &nFileSize ) )
	{
		FILE_STANDARD_INFO *pStandard = (FILE_STANDARD_INFO *)pInfo;
		pStandard->EndOfFile.QuadPart = nFileSize;
		if ( pStandard->AllocationSize.QuadPart < nFileSize )
			pStandard->AllocationSize.QuadPart = nFileSize;
	}
	return bResult;
}

static BOOL WINAPI Hook_CloseHandle( HANDLE hObject )
{
	if ( s_nTrackedHandles )
	{
		HANDLE hReader = NULL;
		BCVirtualMap_t *pOwnedMap = NULL;
		BCSharedReader_t *pShared = NULL;
		AcquireSRWLockExclusive( &s_Lock );
		int nTracked = FindTracked( hObject );
		if ( nTracked >= 0 )
		{
			pShared = s_TrackedHandles[nTracked].m_pShared;
			if ( s_TrackedHandles[nTracked].m_nBaseOffset > 0 )
				GMALog( "CLOSE h=%p tid=%u\n", hObject, GetCurrentThreadId() );
			hReader = s_TrackedHandles[nTracked].m_hReader;
			if ( s_TrackedHandles[nTracked].m_bOwnsMap )
				pOwnedMap = s_TrackedHandles[nTracked].m_pMap;
			s_TrackedHandles.FastRemove( nTracked );
			InterlockedDecrement( &s_nTrackedHandles );
		}
		ReleaseSRWLockExclusive( &s_Lock );
		if ( hReader )
			s_pfnCloseHandle( hReader );
		delete pOwnedMap;
		ReleaseSharedReader( pShared, s_pfnCloseHandle );	// a read still going on another thread keeps it until it's done
	}
	return s_pfnCloseHandle( hObject );
}


//-----------------------------------------------------------------------------
// File attributes: model files report their view's size, and the files made
// up for converted models exist.
//-----------------------------------------------------------------------------
static BOOL WINAPI Hook_GetFileAttributesExW( LPCWSTR pszPath, GET_FILEEX_INFO_LEVELS nLevel, LPVOID pInfo )
{
	BOOL bGMA;
	if ( nLevel == GetFileExInfoStandard && pInfo && GetGMAAttributes( pszPath, (WIN32_FILE_ATTRIBUTE_DATA *)pInfo, &bGMA ) )
		return bGMA;
	BOOL bResult = s_pfnGetFileAttributesExW( pszPath, nLevel, pInfo );
	if ( !bResult && nLevel == GetFileExInfoStandard && pInfo && IsMapPath( pszPath ) )
	{
		const DWORD nMapError = GetLastError();
		if ( IsNotFoundError( nMapError ) && GetVPKMapAttributes( pszPath, (WIN32_FILE_ATTRIBUTE_DATA *)pInfo ) )
		{
			SetLastError( NO_ERROR );
			return TRUE;
		}
		SetLastError( nMapError );
		return bResult;
	}
	if ( nLevel != GetFileExInfoStandard || !pInfo || !IsModelPath( pszPath ) )
		return bResult;

	DWORD nError = GetLastError();
	WIN32_FILE_ATTRIBUTE_DATA *pData = (WIN32_FILE_ATTRIBUTE_DATA *)pInfo;
	if ( bResult )
	{
		if ( !( pData->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY ) )
			GetModelFileAttributes( pszPath, true, pData );
	}
	else if ( IsNotFoundError( nError ) && GetModelFileAttributes( pszPath, false, pData ) )
	{
		bResult = TRUE;
		nError = NO_ERROR;
	}
	SetLastError( nError );
	return bResult;
}

static BOOL WINAPI Hook_GetFileAttributesExA( LPCSTR pszPath, GET_FILEEX_INFO_LEVELS nLevel, LPVOID pInfo )
{
	wchar_t szWide[MAX_PATH];
	BOOL bGMA;
	if ( nLevel == GetFileExInfoStandard && pInfo && ToWide( pszPath, szWide ) && GetGMAAttributes( szWide, (WIN32_FILE_ATTRIBUTE_DATA *)pInfo, &bGMA ) )
		return bGMA;
	BOOL bResult = s_pfnGetFileAttributesExA( pszPath, nLevel, pInfo );
	wchar_t szPath[MAX_PATH];
	if ( !bResult && nLevel == GetFileExInfoStandard && pInfo && pszPath && MultiByteToWideChar( CP_ACP, 0, pszPath, -1, szPath, MAX_PATH ) && IsMapPath( szPath ) )
	{
		const DWORD nMapError = GetLastError();
		if ( IsNotFoundError( nMapError ) && GetVPKMapAttributes( szPath, (WIN32_FILE_ATTRIBUTE_DATA *)pInfo ) )
		{
			SetLastError( NO_ERROR );
			return TRUE;
		}
		SetLastError( nMapError );
		return bResult;
	}
	if ( nLevel != GetFileExInfoStandard || !pInfo || !pszPath || !MultiByteToWideChar( CP_ACP, 0, pszPath, -1, szPath, MAX_PATH ) || !IsModelPath( szPath ) )
		return bResult;

	DWORD nError = GetLastError();
	WIN32_FILE_ATTRIBUTE_DATA *pData = (WIN32_FILE_ATTRIBUTE_DATA *)pInfo;
	if ( bResult )
	{
		if ( !( pData->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY ) )
			GetModelFileAttributes( szPath, true, pData );
	}
	else if ( IsNotFoundError( nError ) && GetModelFileAttributes( szPath, false, pData ) )
	{
		bResult = TRUE;
		nError = NO_ERROR;
	}
	SetLastError( nError );
	return bResult;
}

static DWORD WINAPI Hook_GetFileAttributesW( LPCWSTR pszPath )
{
	WIN32_FILE_ATTRIBUTE_DATA gmaData;
	BOOL bGMA;
	if ( GetGMAAttributes( pszPath, &gmaData, &bGMA ) )
		return bGMA ? gmaData.dwFileAttributes : INVALID_FILE_ATTRIBUTES;
	DWORD nAttributes = s_pfnGetFileAttributesW( pszPath );
	if ( nAttributes == INVALID_FILE_ATTRIBUTES && IsMapPath( pszPath ) )
	{
		const DWORD nMapError = GetLastError();
		WIN32_FILE_ATTRIBUTE_DATA mapData;
		if ( IsNotFoundError( nMapError ) && GetVPKMapAttributes( pszPath, &mapData ) )
		{
			SetLastError( NO_ERROR );
			return mapData.dwFileAttributes;
		}
		SetLastError( nMapError );
		return nAttributes;
	}
	if ( nAttributes != INVALID_FILE_ATTRIBUTES || !IsModelPath( pszPath ) )
		return nAttributes;
	DWORD nError = GetLastError();
	WIN32_FILE_ATTRIBUTE_DATA data;
	if ( IsNotFoundError( nError ) && GetModelFileAttributes( pszPath, false, &data ) )
	{
		SetLastError( NO_ERROR );
		return data.dwFileAttributes;
	}
	SetLastError( nError );
	return nAttributes;
}

static DWORD WINAPI Hook_GetFileAttributesA( LPCSTR pszPath )
{
	wchar_t szWide[MAX_PATH];
	WIN32_FILE_ATTRIBUTE_DATA gmaData;
	BOOL bGMA;
	if ( ToWide( pszPath, szWide ) && GetGMAAttributes( szWide, &gmaData, &bGMA ) )
		return bGMA ? gmaData.dwFileAttributes : INVALID_FILE_ATTRIBUTES;
	DWORD nAttributes = s_pfnGetFileAttributesA( pszPath );
	wchar_t szPath[MAX_PATH];
	if ( nAttributes == INVALID_FILE_ATTRIBUTES && pszPath && MultiByteToWideChar( CP_ACP, 0, pszPath, -1, szPath, MAX_PATH ) && IsMapPath( szPath ) )
	{
		const DWORD nMapError = GetLastError();
		WIN32_FILE_ATTRIBUTE_DATA mapData;
		if ( IsNotFoundError( nMapError ) && GetVPKMapAttributes( szPath, &mapData ) )
		{
			SetLastError( NO_ERROR );
			return mapData.dwFileAttributes;
		}
		SetLastError( nMapError );
		return nAttributes;
	}
	if ( nAttributes != INVALID_FILE_ATTRIBUTES || !pszPath || !MultiByteToWideChar( CP_ACP, 0, pszPath, -1, szPath, MAX_PATH ) || !IsModelPath( szPath ) )
		return nAttributes;
	DWORD nError = GetLastError();
	WIN32_FILE_ATTRIBUTE_DATA data;
	if ( IsNotFoundError( nError ) && GetModelFileAttributes( szPath, false, &data ) )
	{
		SetLastError( NO_ERROR );
		return data.dwFileAttributes;
	}
	SetLastError( nError );
	return nAttributes;
}

//-----------------------------------------------------------------------------
// Folder listings under the Garry's Mod addon mount (gma_mount.cpp)
//-----------------------------------------------------------------------------
static void FindDataToA( const WIN32_FIND_DATAW &w, WIN32_FIND_DATAA *pA )
{
	pA->dwFileAttributes = w.dwFileAttributes;
	pA->ftCreationTime = w.ftCreationTime;
	pA->ftLastAccessTime = w.ftLastAccessTime;
	pA->ftLastWriteTime = w.ftLastWriteTime;
	pA->nFileSizeHigh = w.nFileSizeHigh;
	pA->nFileSizeLow = w.nFileSizeLow;
	pA->dwReserved0 = pA->dwReserved1 = 0;
	WideCharToMultiByte( CP_ACP, 0, w.cFileName, -1, pA->cFileName, sizeof( pA->cFileName ), NULL, NULL );
	pA->cAlternateFileName[0] = 0;
}

static HANDLE WINAPI Hook_FindFirstFileExW( LPCWSTR pszPattern, FINDEX_INFO_LEVELS nLevel, LPVOID pData, FINDEX_SEARCH_OPS nSearchOp, LPVOID pFilter, DWORD nFlags )
{
	if ( pData && ( nLevel == FindExInfoStandard || nLevel == FindExInfoBasic ) )
	{
		bool bHandled;
		void *pFind = GMAMount_FindFirst( pszPattern, (WIN32_FIND_DATAW *)pData, &bHandled );
		if ( bHandled )
		{
			GMALog( "FIND %ls -> %p\n", pszPattern, pFind );
			return pFind ? (HANDLE)pFind : INVALID_HANDLE_VALUE;
		}
	}
	return s_pfnFindFirstFileExW( pszPattern, nLevel, pData, nSearchOp, pFilter, nFlags );
}

static HANDLE WINAPI Hook_FindFirstFileA( LPCSTR pszPattern, LPWIN32_FIND_DATAA pData )
{
	wchar_t szWide[MAX_PATH];
	if ( pData && ToWide( pszPattern, szWide ) )
	{
		bool bHandled;
		WIN32_FIND_DATAW data;
		void *pFind = GMAMount_FindFirst( szWide, &data, &bHandled );
		if ( bHandled )
		{
			if ( !pFind )
				return INVALID_HANDLE_VALUE;
			FindDataToA( data, pData );
			return (HANDLE)pFind;
		}
	}
	return s_pfnFindFirstFileA( pszPattern, pData );
}

static BOOL WINAPI Hook_FindNextFileW( HANDLE hFind, LPWIN32_FIND_DATAW pData )
{
	if ( GMAMount_IsFind( hFind ) )
		return GMAMount_FindNext( hFind, pData ) ? TRUE : FALSE;
	return s_pfnFindNextFileW( hFind, pData );
}

static BOOL WINAPI Hook_FindNextFileA( HANDLE hFind, LPWIN32_FIND_DATAA pData )
{
	if ( GMAMount_IsFind( hFind ) )
	{
		WIN32_FIND_DATAW data;
		if ( !GMAMount_FindNext( hFind, &data ) )
			return FALSE;
		FindDataToA( data, pData );
		return TRUE;
	}
	return s_pfnFindNextFileA( hFind, pData );
}

static BOOL WINAPI Hook_FindClose( HANDLE hFind )
{
	if ( GMAMount_IsFind( hFind ) )
	{
		GMAMount_FindClose( hFind );
		return TRUE;
	}
	return s_pfnFindClose( hFind );
}

//-----------------------------------------------------------------------------
// Native engine support (method A, after the BSPX plugin)
//
// The redirect above only sees maps opened as files. Maps inside VPKs (Black
// Mesa ships every map that way) reach the engine untouched, so engine.dll is
// taught the parts of those maps it doesn't know:
//  - static props: the reader in CStaticPropMgr only sends lump versions 7 and
//    10 through its 72-byte CUtlBuffer::Get; the patch sends 7-11 there, and
//    while that reader runs a CUtlBuffer::Get detour reads each prop in its real
//    layout and hands back the TF2 one. The layout comes from the lump itself
//    (bytes left / prop count), so TF2's own 72-byte v7 and v10 props, and maps
//    the redirect already converted, pass through untouched. BSPX read every
//    v10 lump as CS:GO's 76 bytes, which broke TF2's own maps.
//  - world lights: Mod_LoadWorldlights reads version 1 lumps (100 bytes, Black
//    Mesa / L4D2 / ASW) as version 0 (88)
//  - version 21: CRC_MapFile, MD5_MapFile and CMapLoadHelper::Init /
//    InitFromMemory accept 19-21 instead of 19-20, and once the header is read
//    an L4D2 lump table ({ version, fileofs, filelen, fourCC }) is put back in
//    the v20 order and the lumps the TF2 engine doesn't know are emptied. These
//    are only applied when that header fix is in place.
// Not taken from BSPX: its DispInfo_LoadDisplacements patch raises the 2048
// displacement limit, but the engine reads them into a fixed 2048-entry array
// on the stack; a map with more would overwrite the stack.
// Not handled natively: the 'thin' brushside byte of ASW/P2/CS:GO maps in VPKs.
//
// Signatures and offsets are from the TF2 SDK's x64 engine.dll. Each signature
// must match exactly once and each patch checks the bytes it replaces, so a
// different engine build skips what it doesn't recognize instead of corrupting it.
//-----------------------------------------------------------------------------
#ifdef PLATFORM_64BITS

// Code patterns: hex bytes, '?' for any byte
static bool BCParsePattern( const char *pszPattern, unsigned char *pBytes, bool *pWild, int nMax, int &nLen )
{
	nLen = 0;
	for ( const char *p = pszPattern; *p; )
	{
		if ( *p == ' ' ) { p++; continue; }
		if ( nLen >= nMax )
			return false;
		if ( *p == '?' )
		{
			pWild[nLen] = true;
			pBytes[nLen++] = 0;
			while ( *p == '?' ) p++;
			continue;
		}
		char sz[3] = { p[0], p[1], 0 };
		if ( !p[1] )
			return false;
		pWild[nLen] = false;
		pBytes[nLen++] = (unsigned char)strtoul( sz, NULL, 16 );
		p += 2;
	}
	return nLen > 0;
}

// The one place in the module's code that matches, or NULL (none, or more than one)
static unsigned char *BCFindPattern( HMODULE hModule, const char *pszPattern )
{
	unsigned char bytes[64];
	bool wild[64];
	int nLen;
	if ( !hModule || !BCParsePattern( pszPattern, bytes, wild, ARRAYSIZE( bytes ), nLen ) )
		return NULL;

	unsigned char *pBase = (unsigned char *)hModule;
	IMAGE_DOS_HEADER *pDos = (IMAGE_DOS_HEADER *)pBase;
	IMAGE_NT_HEADERS *pNt = (IMAGE_NT_HEADERS *)( pBase + pDos->e_lfanew );
	IMAGE_SECTION_HEADER *pSection = IMAGE_FIRST_SECTION( pNt );

	unsigned char *pFound = NULL;
	for ( int s = 0; s < pNt->FileHeader.NumberOfSections; s++, pSection++ )
	{
		if ( !( pSection->Characteristics & IMAGE_SCN_MEM_EXECUTE ) )
			continue;

		unsigned char *pStart = pBase + pSection->VirtualAddress;
		const size_t nSize = MAX( pSection->Misc.VirtualSize, pSection->SizeOfRawData );
		if ( nSize < (size_t)nLen )
			continue;

		unsigned char *pEnd = pStart + nSize - nLen;
		for ( unsigned char *p = pStart; p <= pEnd; p++ )
		{
			if ( p[0] != bytes[0] )
				continue;
			int i = 1;
			while ( i < nLen && ( wild[i] || p[i] == bytes[i] ) )
				i++;
			if ( i < nLen )
				continue;
			if ( pFound )
				return NULL;	// not unique
			pFound = p;
		}
	}
	return pFound;
}

#define BCSIG_GAMELUMPVERSION		"44 8B 0D ? ? ? ? 41 83 E9 01 49 63 D1"
#define BCSIG_UTLBUFFER_GET			"45 85 C0 0F 8E ? ? ? ? 48 89 5C 24 ? 48 89 74 24"
#define BCSIG_UNSERIALIZE_PROPS		"48 89 54 24 ? 55 56 57 41 56 48 8D AC 24"
#define BCSIG_LOAD_WORLDLIGHTS		"40 56 41 56 41 57 48 83 EC 50 4C 8B 05 ? ? ? ? 33 F6 44 0F B6 FA 4C 8B F1"
#define BCSIG_CRC_MAPFILE			"48 89 5C 24 ? 48 89 6C 24 ? 56 57 41 56 48 81 EC 30 08 00 00"
#define BCSIG_MD5_MAPFILE			"48 89 5C 24 ? 48 89 74 24 ? 48 89 7C 24 ? 55 41 56 41 57 48 8D AC 24 ? ? ? ? 48 81 EC 90 08 00 00"
#define BCSIG_MAPLOADHELPER_INIT	"48 89 5C 24 ? 57 48 83 EC 30 8B 05"
#define BCSIG_MAPLOADHELPER_INITMEM	"48 89 5C 24 ? 48 89 6C 24 ? 56 48 83 EC 30 83 7C 24 ? 00"

// engine.dll's CUtlBuffer (the same layout as tier1's; checked against CUtlBuffer::Get)
struct BCEngineUtlBuffer_t
{
	unsigned char	*m_pMemory;			// 0x00
	int				m_nAllocationCount;	// 0x08
	int				m_nGrowSize;		// 0x0C
	int				m_Get;				// 0x10
	int				m_Put;				// 0x14
	unsigned char	m_Error;			// 0x18
	unsigned char	m_Flags;
	unsigned char	m_Reserved;
	unsigned char	m_Pad;
	int				m_nTab;				// 0x1C
	int				m_nMaxPut;			// 0x20
	int				m_nOffset;			// 0x24
};

// engine.dll's CMapLoadHelper (checked against its constructor)
struct BCEngineMapLoadHelper_t
{
	int				m_nLumpSize;		// 0x00
	int				m_nLumpOffset;		// 0x04
	int				m_nLumpVersion;		// 0x08
	int				m_nPad;
	unsigned char	*m_pRawData;		// 0x10
	unsigned char	*m_pData;			// 0x18, what LumpBase() returns
	unsigned char	*m_pUncompressedData;	// 0x20
	int				m_nLumpID;			// 0x28
};

typedef int ( *BCGameLumpVersionFn_t )( int nLumpID );
typedef void ( *BCUtlBufferGetFn_t )( void *pBuffer, void *pMem, int nSize );
typedef void ( *BCUnserializePropsFn_t )( void *pThis, void *pBuffer );
typedef void ( *BCLoadWorldlightsFn_t )( void *pLoadHelper, bool bIsHDR );
typedef void ( *BCMapLoadInitFn_t )( void *pArg1, void *pArg2, void *pArg3, void *pArg4 );

static BCGameLumpVersionFn_t	s_pfnGameLumpVersion = NULL;
static BCUtlBufferGetFn_t		s_pfnUtlBufferGet = NULL;
static BCUnserializePropsFn_t	s_pfnUnserializeProps = NULL;
static BCLoadWorldlightsFn_t	s_pfnLoadWorldlights = NULL;
static BCMapLoadInitFn_t		s_pfnMapLoadInit = NULL;
static BCMapLoadInitFn_t		s_pfnMapLoadInitFromMemory = NULL;

// The engine's map header (s_MapHeader: ident, version, 64 lumps, map revision)
static int *s_pEngineMapHeader = NULL;

// Set while the engine reads the static props of a map that needs converting
static volatile DWORD	s_nPropReaderThread = 0;
static int				s_nPropStride = 0;
static bool				s_bPropFlags32 = false;

static void Detour_UtlBufferGet( void *pBuffer, void *pMem, int nSize )
{
	if ( nSize == BC_STATICPROP_TF2_SIZE && s_nPropReaderThread && s_nPropReaderThread == GetCurrentThreadId() )
	{
		unsigned char src[128];
		memset( src, 0, sizeof( src ) );
		s_pfnUtlBufferGet( pBuffer, src, s_nPropStride );
		ConvertStaticProp( src, s_bPropFlags32, (unsigned char *)pMem );
		return;
	}
	s_pfnUtlBufferGet( pBuffer, pMem, nSize );
}

// Reads the prop count and the prop structs; the buffer is at the count
static void Detour_UnserializeProps( void *pThis, void *pBuffer )
{
	const BCEngineUtlBuffer_t *pBuf = (const BCEngineUtlBuffer_t *)pBuffer;
	const int nVersion = s_pfnGameLumpVersion( BC_GAMELUMP_SPRP );
	const int nPos = pBuf->m_Get - pBuf->m_nOffset;
	const int nEnd = pBuf->m_nMaxPut - pBuf->m_nOffset;

	if ( nVersion >= 7 && nVersion <= 11 && pBuf->m_pMemory && nPos >= 0 && nEnd - nPos >= 4 )
	{
		const int nProps = *(const int *)( pBuf->m_pMemory + nPos );
		const int nBytes = nEnd - nPos - 4;
		if ( nProps > 0 && nBytes > 0 && ( nBytes % nProps ) == 0 )
		{
			const int nStride = nBytes / nProps;
			int nColorOfs;
			bool bFlags32;
			if ( nStride <= 128 && GetStaticPropLayout( nVersion, nStride, pBuf->m_pMemory + nPos + 4, nProps, nColorOfs, bFlags32 ) )
			{
				s_nPropStride = nStride;
				s_bPropFlags32 = bFlags32;
				s_nPropReaderThread = GetCurrentThreadId();
				DevMsg( "BSP: reading %d static props from lump version %d (%d bytes each)%s\n",
					nProps, nVersion, nStride, bFlags32 ? ", Black Mesa layout" : "" );
			}
		}
		else if ( nProps > 0 && nVersion != 7 && nVersion != 10 )
		{
			Warning( "BSP: static prop lump version %d: %d props in %d bytes, can't tell their size\n", nVersion, nProps, nBytes );
		}
	}

	s_pfnUnserializeProps( pThis, pBuffer );
	s_nPropReaderThread = 0;
}

static void Detour_LoadWorldlights( void *pLoadHelper, bool bIsHDR )
{
	BCEngineMapLoadHelper_t *pHelper = (BCEngineMapLoadHelper_t *)pLoadHelper;
	if ( pHelper->m_nLumpVersion == 1 && pHelper->m_pData && pHelper->m_nLumpSize > 0 &&
		 ( pHelper->m_nLumpSize % BC_WORLDLIGHT_V1_SIZE ) == 0 )
	{
		const int nCount = pHelper->m_nLumpSize / BC_WORLDLIGHT_V1_SIZE;
		CUtlVector< unsigned char > lights;
		lights.SetCount( nCount * BC_WORLDLIGHT_V0_SIZE );
		ConvertWorldLightsV1( pHelper->m_pData, nCount, lights.Base() );

		// The engine copies the lump out; point it at the converted one for the call
		unsigned char *pData = pHelper->m_pData;
		const int nSize = pHelper->m_nLumpSize;
		pHelper->m_pData = lights.Base();
		pHelper->m_nLumpSize = lights.Count();
		pHelper->m_nLumpVersion = 0;
		s_pfnLoadWorldlights( pLoadHelper, bIsHDR );
		pHelper->m_pData = pData;
		pHelper->m_nLumpSize = nSize;
		pHelper->m_nLumpVersion = 1;
		return;
	}
	s_pfnLoadWorldlights( pLoadHelper, bIsHDR );
}

// After the engine read a version 21 header: the v20 lump order, unknown lumps emptied
static void FixEngineMapHeader()
{
	int *pHeader = s_pEngineMapHeader;
	if ( !pHeader || pHeader[0] != BC_IDENT || pHeader[1] != 21 )
		return;

	// The entity lump is never empty and never inside the header, so its offset
	// tells the orders apart: v20 { fileofs, filelen, ... }, L4D2 { version, fileofs, ... }
	const int *pEnt = pHeader + 2;
	const bool bL4D2Order = ( pEnt[0] < BC_HEADER_SIZE && pEnt[1] >= BC_HEADER_SIZE );

	BCLump_t lumps[BC_HEADER_LUMPS];
	ReadLumpTable( pHeader + 2, bL4D2Order, lumps );
	for ( int i = 0; i < BC_HEADER_LUMPS; i++ )
	{
		int *p = pHeader + 2 + i * 4;
		if ( IsDroppedLump( i ) )
		{
			p[0] = p[1] = p[2] = p[3] = 0;
			continue;
		}
		p[0] = lumps[i].m_nOfs;
		p[1] = lumps[i].m_nLen;
		p[2] = lumps[i].m_nVersion;
		p[3] = lumps[i].m_nUncompressed;
	}
	DevMsg( "BSP: version 21 map header read natively%s\n", bL4D2Order ? " (L4D2 lump order)" : "" );
}

static void Detour_MapLoadInit( void *pArg1, void *pArg2, void *pArg3, void *pArg4 )
{
	s_pfnMapLoadInit( pArg1, pArg2, pArg3, pArg4 );
	FixEngineMapHeader();
}

static void Detour_MapLoadInitFromMemory( void *pArg1, void *pArg2, void *pArg3, void *pArg4 )
{
	s_pfnMapLoadInitFromMemory( pArg1, pArg2, pArg3, pArg4 );
	FixEngineMapHeader();
}

struct BCEnginePatch_t
{
	const char			*m_pszName;
	unsigned char		*m_pFunction;
	int					m_nOffset;
	const char			*m_pOriginal;
	const char			*m_pPatched;
	int					m_nLen;
	unsigned char		*m_pApplied;	// where it went, for shutdown
};

static BCEnginePatch_t s_EnginePatches[] =
{
	{ "CStaticPropMgr: lump versions 7-11",		NULL, 0xCF,  "\x83\xF9\x03\x74\x28", "\x83\xF9\x05\x78\x28", 5 },	// cmp ecx,3 / je  ->  cmp ecx,5 / js
	{ "CRC_MapFile: version 21",				NULL, 0xBC,  "\x83\xF8\x01", "\x83\xF8\x02", 3 },	// cmp eax,1 -> 2 (version - 19)
	{ "MD5_MapFile: version 21",				NULL, 0xF6,  "\x83\xF8\x01", "\x83\xF8\x02", 3 },
	{ "CMapLoadHelper::Init: version 21",		NULL, 0x162, "\x83\xF8\x01", "\x83\xF8\x02", 3 },
	{ "CMapLoadHelper::InitFromMemory: version 21", NULL, 0x17D, "\x83\xF8\x01", "\x83\xF8\x02", 3 },
};
enum { BCPATCH_PROPS, BCPATCH_CRC, BCPATCH_MD5, BCPATCH_INIT, BCPATCH_INITMEM };

static bool ApplyEnginePatch( BCEnginePatch_t &patch )
{
	if ( !patch.m_pFunction )
		return false;
	unsigned char *pAddress = patch.m_pFunction + patch.m_nOffset;
	if ( memcmp( pAddress, patch.m_pOriginal, patch.m_nLen ) != 0 )
	{
		Warning( "BSP: %s: unexpected code, not patched\n", patch.m_pszName );
		return false;
	}

	DWORD nProtect;
	if ( !VirtualProtect( pAddress, patch.m_nLen, PAGE_EXECUTE_READWRITE, &nProtect ) )
		return false;
	memcpy( pAddress, patch.m_pPatched, patch.m_nLen );
	VirtualProtect( pAddress, patch.m_nLen, nProtect, &nProtect );
	FlushInstructionCache( GetCurrentProcess(), pAddress, patch.m_nLen );
	patch.m_pApplied = pAddress;
	return true;
}

static void RevertEnginePatch( BCEnginePatch_t &patch )
{
	unsigned char *pAddress = patch.m_pApplied;
	patch.m_pApplied = NULL;
	if ( !pAddress || memcmp( pAddress, patch.m_pPatched, patch.m_nLen ) != 0 )
		return;

	DWORD nProtect;
	if ( VirtualProtect( pAddress, patch.m_nLen, PAGE_EXECUTE_READWRITE, &nProtect ) )
	{
		memcpy( pAddress, patch.m_pOriginal, patch.m_nLen );
		VirtualProtect( pAddress, patch.m_nLen, nProtect, &nProtect );
		FlushInstructionCache( GetCurrentProcess(), pAddress, patch.m_nLen );
	}
}

static void *s_EngineHookTargets[8];
static int s_nEngineHookTargets = 0;

static bool CreateEngineHook( unsigned char *pTarget, void *pDetour, void **ppOriginal, const char *pszName )
{
	if ( !pTarget )
	{
		Warning( "BSP: %s not found in engine.dll\n", pszName );
		return false;
	}
	if ( MH_CreateHook( pTarget, pDetour, ppOriginal ) != MH_OK )
	{
		Warning( "BSP: couldn't hook %s\n", pszName );
		return false;
	}
	if ( MH_EnableHook( pTarget ) != MH_OK )
	{
		MH_RemoveHook( pTarget );
		Warning( "BSP: couldn't enable the %s hook\n", pszName );
		return false;
	}
	s_EngineHookTargets[s_nEngineHookTargets++] = pTarget;
	return true;
}

static bool InstallEngineSupport()
{
	HMODULE hEngine = GetModuleHandleA( "engine.dll" );
	if ( !hEngine )
		return false;

	s_pfnGameLumpVersion = (BCGameLumpVersionFn_t)BCFindPattern( hEngine, BCSIG_GAMELUMPVERSION );
	unsigned char *pGet = BCFindPattern( hEngine, BCSIG_UTLBUFFER_GET );
	unsigned char *pProps = BCFindPattern( hEngine, BCSIG_UNSERIALIZE_PROPS );
	unsigned char *pLights = BCFindPattern( hEngine, BCSIG_LOAD_WORLDLIGHTS );
	unsigned char *pInit = BCFindPattern( hEngine, BCSIG_MAPLOADHELPER_INIT );
	unsigned char *pInitMem = BCFindPattern( hEngine, BCSIG_MAPLOADHELPER_INITMEM );

	s_EnginePatches[BCPATCH_PROPS].m_pFunction = pProps;
	s_EnginePatches[BCPATCH_CRC].m_pFunction = BCFindPattern( hEngine, BCSIG_CRC_MAPFILE );
	s_EnginePatches[BCPATCH_MD5].m_pFunction = BCFindPattern( hEngine, BCSIG_MD5_MAPFILE );
	s_EnginePatches[BCPATCH_INIT].m_pFunction = pInit;
	s_EnginePatches[BCPATCH_INITMEM].m_pFunction = pInitMem;

	// static props: the version patch only goes in with both hooks, or the newer
	// lumps would be read as TF2 props unconverted
	bool bProps = false;
	if ( !s_pfnGameLumpVersion )
	{
		Warning( "BSP: Mod_GameLumpVersion not found in engine.dll\n" );
	}
	else if ( CreateEngineHook( pGet, (void *)&Detour_UtlBufferGet, (void **)&s_pfnUtlBufferGet, "CUtlBuffer::Get" ) )
	{
		if ( CreateEngineHook( pProps, (void *)&Detour_UnserializeProps, (void **)&s_pfnUnserializeProps, "CStaticPropMgr::UnserializeModels" ) )
			bProps = ApplyEnginePatch( s_EnginePatches[BCPATCH_PROPS] );
	}

	const bool bLights = CreateEngineHook( pLights, (void *)&Detour_LoadWorldlights, (void **)&s_pfnLoadWorldlights, "Mod_LoadWorldlights" );

	// version 21: only with the header fix, or an L4D2 lump table would be read as it is
	bool bV21 = false;
	if ( pInit && pInitMem && pInit[0x2C] == 0x48 && pInit[0x2D] == 0x8D && pInit[0x2E] == 0x0D )
	{
		// lea rcx, s_MapHeader (the memset at the top of CMapLoadHelper::Init)
		s_pEngineMapHeader = (int *)( pInit + 0x2C + 7 + *(const int *)( pInit + 0x2F ) );
		if ( CreateEngineHook( pInit, (void *)&Detour_MapLoadInit, (void **)&s_pfnMapLoadInit, "CMapLoadHelper::Init" ) &&
			 CreateEngineHook( pInitMem, (void *)&Detour_MapLoadInitFromMemory, (void **)&s_pfnMapLoadInitFromMemory, "CMapLoadHelper::InitFromMemory" ) )
		{
			bV21 = ApplyEnginePatch( s_EnginePatches[BCPATCH_INIT] );
			bV21 &= ApplyEnginePatch( s_EnginePatches[BCPATCH_INITMEM] );
			// the map CRC/MD5 refuse v21 maps otherwise; not fatal if they don't match
			ApplyEnginePatch( s_EnginePatches[BCPATCH_CRC] );
			ApplyEnginePatch( s_EnginePatches[BCPATCH_MD5] );
		}
	}
	else
	{
		Warning( "BSP: CMapLoadHelper::Init not recognized, version 21 maps in VPKs won't load\n" );
	}

	DevMsg( "BSP: engine support: static props %s, world lights %s, version 21 %s\n",
		bProps ? "on" : "OFF", bLights ? "on" : "OFF", bV21 ? "on" : "OFF" );
	return s_nEngineHookTargets > 0;
}

static void RemoveEngineSupport()
{
	for ( int i = 0; i < ARRAYSIZE( s_EnginePatches ); i++ )
		RevertEnginePatch( s_EnginePatches[i] );

	for ( int i = 0; i < s_nEngineHookTargets; i++ )
	{
		MH_DisableHook( s_EngineHookTargets[i] );
		MH_RemoveHook( s_EngineHookTargets[i] );
	}
	s_nEngineHookTargets = 0;
	s_nPropReaderThread = 0;
}

#else

static bool InstallEngineSupport() { return false; }
static void RemoveEngineSupport() {}

#endif // PLATFORM_64BITS

//-----------------------------------------------------------------------------
// Install / shutdown. client.dll and server.dll both call these; the first to
// install owns the hooks (marked with an environment variable in this process).
//-----------------------------------------------------------------------------
#define BSPCOMPAT_INSTALLED_VAR "SRCBOX_BSP_REDIRECT"

struct BCHook_t
{
	const char	*m_pszFunction;
	void		*m_pDetour;
	void		**m_ppOriginal;
	void		*m_pTarget;
	bool		m_bOptional;	// only for the addon mount: if it can't be hooked, the rest still goes in
};

static BCHook_t s_Hooks[] =
{
	{ "CreateFileW",					(void *)&Hook_CreateFileW,					(void **)&s_pfnCreateFileW },
	{ "CreateFileA",					(void *)&Hook_CreateFileA,					(void **)&s_pfnCreateFileA },
	{ "ReadFile",						(void *)&Hook_ReadFile,						(void **)&s_pfnReadFile },
	{ "SetFilePointer",					(void *)&Hook_SetFilePointer,				(void **)&s_pfnSetFilePointer },
	{ "SetFilePointerEx",				(void *)&Hook_SetFilePointerEx,				(void **)&s_pfnSetFilePointerEx },
	{ "GetFileSize",					(void *)&Hook_GetFileSize,					(void **)&s_pfnGetFileSize },
	{ "GetFileSizeEx",					(void *)&Hook_GetFileSizeEx,				(void **)&s_pfnGetFileSizeEx },
	{ "GetFileInformationByHandle",		(void *)&Hook_GetFileInformationByHandle,	(void **)&s_pfnGetFileInformationByHandle },
	{ "GetFileInformationByHandleEx",	(void *)&Hook_GetFileInformationByHandleEx,	(void **)&s_pfnGetFileInformationByHandleEx },
	{ "CloseHandle",					(void *)&Hook_CloseHandle,					(void **)&s_pfnCloseHandle },
	{ "GetFileAttributesW",				(void *)&Hook_GetFileAttributesW,			(void **)&s_pfnGetFileAttributesW },
	{ "GetFileAttributesA",				(void *)&Hook_GetFileAttributesA,			(void **)&s_pfnGetFileAttributesA },
	{ "GetFileAttributesExW",			(void *)&Hook_GetFileAttributesExW,			(void **)&s_pfnGetFileAttributesExW },
	{ "GetFileAttributesExA",			(void *)&Hook_GetFileAttributesExA,			(void **)&s_pfnGetFileAttributesExA },
	{ "FindFirstFileExW",				(void *)&Hook_FindFirstFileExW,				(void **)&s_pfnFindFirstFileExW, NULL, true },
	{ "FindFirstFileA",					(void *)&Hook_FindFirstFileA,				(void **)&s_pfnFindFirstFileA, NULL, true },
	{ "FindNextFileW",					(void *)&Hook_FindNextFileW,				(void **)&s_pfnFindNextFileW, NULL, true },
	{ "FindNextFileA",					(void *)&Hook_FindNextFileA,				(void **)&s_pfnFindNextFileA, NULL, true },
	{ "FindClose",						(void *)&Hook_FindClose,					(void **)&s_pfnFindClose, NULL, true },
};

static bool s_bOwnsHooks = false;
static bool s_bAddonHooks = true;	// the folder listing hooks the addon mount needs

static void RemoveHooks()
{
	for ( int i = 0; i < ARRAYSIZE( s_Hooks ); i++ )
	{
		if ( s_Hooks[i].m_pTarget )
		{
			MH_DisableHook( s_Hooks[i].m_pTarget );
			MH_RemoveHook( s_Hooks[i].m_pTarget );
			s_Hooks[i].m_pTarget = NULL;
		}
	}
}

// The Windows file API redirect (loose map and model files)
static bool InstallFileHooks()
{
	// The real implementations are in KernelBase (kernel32's exports forward there);
	// fall back to kernel32 on systems without it.
	HMODULE hKernelBase = GetModuleHandleA( "kernelbase.dll" );
	HMODULE hKernel32 = GetModuleHandleA( "kernel32.dll" );

	for ( int i = 0; i < ARRAYSIZE( s_Hooks ); i++ )
	{
		BCHook_t &hook = s_Hooks[i];
		void *pTarget = hKernelBase ? (void *)GetProcAddress( hKernelBase, hook.m_pszFunction ) : NULL;
		if ( !pTarget && hKernel32 )
			pTarget = (void *)GetProcAddress( hKernel32, hook.m_pszFunction );

		if ( !pTarget || MH_CreateHook( pTarget, hook.m_pDetour, hook.m_ppOriginal ) != MH_OK )
		{
			if ( hook.m_bOptional )
			{
				s_bAddonHooks = false;
				continue;
			}
			Warning( "BSP v21: couldn't hook %s, newer maps won't load\n", hook.m_pszFunction );
			RemoveHooks();
			return false;
		}
		hook.m_pTarget = pTarget;
	}

	// The originals must all be in place before any detour can run
	for ( int i = 0; i < ARRAYSIZE( s_Hooks ); i++ )
	{
		if ( !s_Hooks[i].m_pTarget )
			continue;	// an optional one that didn't go in
		if ( MH_EnableHook( s_Hooks[i].m_pTarget ) != MH_OK )
		{
			Warning( "BSP v21: couldn't enable the %s hook, newer maps won't load\n", s_Hooks[i].m_pszFunction );
			RemoveHooks();
			return false;
		}
	}

	return true;
}


//-----------------------------------------------------------------------------
// VPK handles opened before the hooks went in
//
// filesystem_stdio can open an archive chunk before whichever DLL installs the
// hooks is loaded, and keep reading through that handle. Reads through it would
// get the original bytes while reads through handles opened later get the view,
// so the engine sees a mix. Every open file handle is checked once, right after
// the hooks go in, and the VPKs among them are tracked from their current file
// position. GetFileType first: only disk files get a name lookup ( a pipe's
// can block ).
//-----------------------------------------------------------------------------
#define BC_MAX_HANDLE_VALUE		0x40000

static bool GetOpenFilePath( HANDLE hFile, wchar_t *szPath )
{
	if ( GetFileType( hFile ) != FILE_TYPE_DISK )
		return false;
	wchar_t szFinal[MAX_PATH + 8];
	const DWORD nLen = GetFinalPathNameByHandleW( hFile, szFinal, MAX_PATH, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS );
	if ( !nLen || nLen >= MAX_PATH )
		return false;
	const wchar_t *p = szFinal;
	if ( !wcsncmp( p, L"\\\\?\\", 4 ) )
		p += 4;
	wcsncpy_s( szPath, MAX_PATH, p, _TRUNCATE );
	const size_t n = wcslen( szPath );
	return n > 4 && !_wcsicmp( szPath + n - 4, L".vpk" );
}

// Returns how many it started tracking. pnOpen: VPK handles open in the process. With bAdopt false
// nothing is tracked, and pnMissed counts handles on chunks holding maps that aren't redirected.
static int AdoptOpenVPKHandles( int *pnOpen = NULL, bool bAdopt = true, int *pnMissed = NULL )
{
	int nAdopted = 0, nOpen = 0, nMissed = 0;
	for ( ULONG_PTR nValue = 4; nValue < BC_MAX_HANDLE_VALUE; nValue += 4 )
	{
		HANDLE hFile = (HANDLE)nValue;
		wchar_t szPath[MAX_PATH];
		if ( !GetOpenFilePath( hFile, szPath ) )
			continue;
		nOpen++;

		AcquireSRWLockShared( &s_Lock );
		const bool bTracked = ( FindTracked( hFile ) >= 0 );
		bool bMapChunk = false;
		if ( !bTracked && !bAdopt )
		{
			_wcslwr_s( szPath );
			bool bReaderHandle = false;
			for ( int i = 0; i < s_TrackedHandles.Count() && !bReaderHandle; i++ )
				bReaderHandle = ( s_TrackedHandles[i].m_hReader == hFile );
			for ( int i = 0; i < s_CachedVPKs.Count() && !bReaderHandle && !bMapChunk; i++ )
				bMapChunk = s_CachedVPKs[i].m_pChunk && !wcscmp( s_CachedVPKs[i].m_szPath, szPath );
		}
		ReleaseSRWLockShared( &s_Lock );
		if ( bMapChunk )
		{
			nMissed++;
			Msg( "  NOT redirected: handle %p on %ls\n", hFile, szPath );
		}
		if ( bTracked || !bAdopt )
			continue;

		// our own reader handles are never in s_TrackedHandles as m_hFile, but they are
		// as m_hReader: leave those alone
		bool bReader = false;
		AcquireSRWLockShared( &s_Lock );
		for ( int i = 0; i < s_TrackedHandles.Count() && !bReader; i++ )
			bReader = ( s_TrackedHandles[i].m_hReader == hFile );
		ReleaseSRWLockShared( &s_Lock );
		if ( bReader )
			continue;

		LARGE_INTEGER zero, position;
		zero.QuadPart = 0;
		if ( !s_pfnSetFilePointerEx( hFile, zero, &position, FILE_CURRENT ) )
			position.QuadPart = 0;

		TrackIfVPK( hFile, szPath, GENERIC_READ, 0 );

		AcquireSRWLockExclusive( &s_Lock );
		const int nTracked = FindTracked( hFile );
		if ( nTracked >= 0 )
			s_TrackedHandles[nTracked].m_nPosition = position.QuadPart;
		ReleaseSRWLockExclusive( &s_Lock );
		if ( nTracked >= 0 )
			nAdopted++;
	}
	if ( pnOpen )
		*pnOpen = nOpen;
	if ( pnMissed )
		*pnMissed = nMissed;
	return nAdopted;
}

static void PublishGMALogToggle( bool bPublish );

void BSPCompat_Install()
{
	// The variable holds the process ID, so a child process that inherits it still installs
	char szProcess[32], szInstalled[32];
	V_snprintf( szProcess, sizeof( szProcess ), "%u", (unsigned int)GetCurrentProcessId() );
	if ( GetEnvironmentVariableA( BSPCOMPAT_INSTALLED_VAR, szInstalled, sizeof( szInstalled ) ) > 0 && !V_strcmp( szInstalled, szProcess ) )
		return;	// the other DLL did it

	MH_STATUS status = MH_Initialize();
	if ( status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED )
	{
		Warning( "BSP v21: MinHook failed to initialize (%d), newer maps won't load\n", (int)status );
		return;
	}

	// Each works without the other: the redirect for loose files, the engine
	// support for maps in VPKs
	const bool bFiles = InstallFileHooks();
	const bool bEngine = InstallEngineSupport();
	if ( !bFiles && !bEngine )
		return;

	s_bOwnsHooks = true;
	SetEnvironmentVariableA( BSPCOMPAT_INSTALLED_VAR, szProcess );

	// Archive chunks the filesystem opened before now
	if ( bFiles )
	{
		const int nAdopted = AdoptOpenVPKHandles();
		if ( nAdopted )
			DevMsg( "BSP compat: %d VPK handles opened before the hooks are redirected now\n", nAdopted );
	}
	PublishGMALogToggle( true );

	// Garry's Mod's cfg/mount.cfg: plain search paths, so it doesn't need the hooks.
	// First, so a Garry's Mod folder mounted there is found by the addon mount.
	MountCfg_Init();

	// Garry's Mod addons are served through the file hooks, so they live in this DLL too
	if ( bFiles && s_bAddonHooks )
	{
		GMAMount_Init();
		OpenGMALog();
	}
	else if ( bFiles )
		Warning( "GMA: couldn't hook the folder listing functions, Garry's Mod addons won't be mounted\n" );
}

// The hooks (and so the log) live in whichever DLL installed them first. The
// server's commands don't run at the main menu, so both DLLs have the command:
// the owner publishes its toggle in an environment variable (same process, so
// the address is valid in the other DLL) and the other one calls through it.
#define GMA_LOGFN_VAR	"SRCBOX_GMA_LOGFN"
typedef void ( *GMALogToggle_t )( int nState );	// 1 on, 0 off, -1 toggle

static void GMALogToggle( int nState )
{
	const bool bOn = ( nState < 0 ) ? ( s_hGMALog == INVALID_HANDLE_VALUE ) : ( nState != 0 );
	if ( bOn )
	{
		if ( s_hGMALog != INVALID_HANDLE_VALUE )
			Msg( "GMA: already logging\n" );
		OpenGMALog( true );
	}
	else if ( s_hGMALog != INVALID_HANDLE_VALUE )
	{
		CloseGMALog();
		Msg( "GMA: logging stopped\n" );
	}
}

static void BSPCompatStatus();
#define BSPCOMPAT_STATUSFN_VAR	"SRCBOX_BSP_STATUSFN"

static void PublishGMALogToggle( bool bPublish )
{
	if ( !bPublish )
	{
		SetEnvironmentVariableA( GMA_LOGFN_VAR, NULL );
		SetEnvironmentVariableA( BSPCOMPAT_STATUSFN_VAR, NULL );
		return;
	}
	char szStatus[32];
	V_snprintf( szStatus, sizeof( szStatus ), "%llx", (unsigned long long)(uintptr_t)&BSPCompatStatus );
	SetEnvironmentVariableA( BSPCOMPAT_STATUSFN_VAR, szStatus );

	char szAddress[32];
	V_snprintf( szAddress, sizeof( szAddress ), "%llx", (unsigned long long)(uintptr_t)&GMALogToggle );
	SetEnvironmentVariableA( GMA_LOGFN_VAR, szAddress );
}

static void GMADebugLogCommand( const CCommand &args )
{
	const int nState = ( args.ArgC() < 2 ) ? -1 : ( atoi( args[1] ) != 0 ? 1 : 0 );
	if ( s_bOwnsHooks )
	{
		GMALogToggle( nState );
		return;
	}
	char szAddress[32];
	if ( GetEnvironmentVariableA( GMA_LOGFN_VAR, szAddress, sizeof( szAddress ) ) == 0 )
	{
		Warning( "GMA: the file hooks aren't installed, so there's nothing to log\n" );
		return;
	}
	GMALogToggle_t pfnToggle = (GMALogToggle_t)(uintptr_t)_strtoui64( szAddress, NULL, 16 );
	if ( pfnToggle )
		pfnToggle( nState );
}

#ifdef CLIENT_DLL
CON_COMMAND( gma_debuglog, "Log every access to files inside mounted .gma addons to <mod>/cache/gma_debug.log (1/0)" )
#else
CON_COMMAND( sv_gma_debuglog, "Log every access to files inside mounted .gma addons to <mod>/cache/gma_debug.log (1/0)" )
#endif
{
	GMADebugLogCommand( args );
}

//-----------------------------------------------------------------------------
// bspcompat_status: what the redirect has seen ( VPKs holding maps, map views )
//-----------------------------------------------------------------------------
// Like gma_debuglog: the owner publishes this function's address ( BSPCOMPAT_STATUSFN_VAR ), the other DLL calls through it
typedef void ( *BSPCompatStatus_t )();

static void BSPCompatStatus()
{
	if ( !s_bOwnsHooks )
	{
		char szAddress[32];
		if ( GetEnvironmentVariableA( BSPCOMPAT_STATUSFN_VAR, szAddress, sizeof( szAddress ) ) > 0 )
		{
			BSPCompatStatus_t pfnStatus = (BSPCompatStatus_t)(uintptr_t)_strtoui64( szAddress, NULL, 16 );
			if ( pfnStatus && pfnStatus != &BSPCompatStatus )
			{
				pfnStatus();
				return;
			}
		}
		Msg( "BSP compat: the hooks aren't installed\n" );
		return;
	}

	// Copy what's needed under the lock, print after ( Msg can come back into the hooks )
	struct Line_t { char m_szText[MAX_PATH + 128]; };
	CUtlVector< Line_t > lines;
	int nTracked = 0, nTrackedVPK = 0;

	AcquireSRWLockShared( &s_Lock );
	for ( int i = 0; i < s_TrackedHandles.Count(); i++ )
	{
		nTracked++;
		nTrackedVPK += ( s_TrackedHandles[i].m_pVPK != NULL );
	}
	for ( int i = 0; i < s_CachedVPKDirs.Count(); i++ )
	{
		const BCCachedVPKDir_t &dir = s_CachedVPKDirs[i];
		if ( !dir.m_pDir || !dir.m_pDir->m_Maps.Count() )
			continue;
		Line_t &line = lines[lines.AddToTail()];
		V_snprintf( line.m_szText, sizeof( line.m_szText ), "  dir   %ls: %d maps\n", dir.m_szPath, dir.m_pDir->m_Maps.Count() );
	}
	for ( int i = 0; i < s_CachedVPKs.Count(); i++ )
	{
		const BCCachedVPK_t &chunk = s_CachedVPKs[i];
		if ( !chunk.m_pChunk )
			continue;
		Line_t &line = lines[lines.AddToTail()];
		V_snprintf( line.m_szText, sizeof( line.m_szText ), "  chunk %ls: %d maps\n", chunk.m_szPath, chunk.m_pChunk->m_Entries.Count() );
		for ( int k = 0; k < chunk.m_pChunk->m_Entries.Count(); k++ )
		{
			const BCVPKEntry_t *pEntry = chunk.m_pChunk->m_Entries[k];
			const BCVirtualMap_t *pMap = pEntry->m_pMap;
			if ( !pMap )
				continue;	// never read
			Line_t &mapLine = lines[lines.AddToTail()];
			V_snprintf( mapLine.m_szText, sizeof( mapLine.m_szText ), "        %s: %s\n", pEntry->m_szName,
				pMap == &s_VPKNoView ? "read as it is" : "converted view" );
		}
	}
	const int nVPKsSeen = s_CachedVPKs.Count();
	ReleaseSRWLockShared( &s_Lock );

	Msg( "BSP compat: hooks installed. %d open handles redirected ( %d VPK chunks ), %d VPK files opened since\n", nTracked, nTrackedVPK, nVPKsSeen );

	int nOpenVPK = 0, nMissed = 0;
	AdoptOpenVPKHandles( &nOpenVPK, false, &nMissed );
	Msg( "  %d VPK handles open in the process, %d on chunks holding maps that are NOT redirected\n", nOpenVPK, nMissed );
	for ( int i = 0; i < lines.Count(); i++ )
		Msg( "%s", lines[i].m_szText );
	if ( !lines.Count() )
		Msg( "  no VPK holding maps has been opened since the hooks went in\n" );
}

#ifdef CLIENT_DLL
CON_COMMAND( bspcompat_status, "Show which VPKs holding maps the BSP redirect has seen, and which maps it converted" )
#else
CON_COMMAND( sv_bspcompat_status, "Show which VPKs holding maps the BSP redirect has seen, and which maps it converted" )
#endif
{
	BSPCompatStatus();
}

void BSPCompat_Shutdown()
{
	if ( !s_bOwnsHooks )
		return;

	RemoveEngineSupport();
	RemoveHooks();
	GMAMount_Shutdown();
	CloseGMALog();
	s_bOwnsHooks = false;
	SetEnvironmentVariableA( BSPCOMPAT_INSTALLED_VAR, NULL );
	PublishGMALogToggle( false );

	// Handles still open now read the original file; their reader handles are released
	AcquireSRWLockExclusive( &s_Lock );
	for ( int i = 0; i < s_TrackedHandles.Count(); i++ )
	{
		if ( s_TrackedHandles[i].m_hReader )
			CloseHandle( s_TrackedHandles[i].m_hReader );
		ReleaseSharedReader( s_TrackedHandles[i].m_pShared, CloseHandle );	// the hooks are gone: the real CloseHandle
	}
	s_TrackedHandles.Purge();
	s_nTrackedHandles = 0;
	for ( int i = 0; i < s_CachedMaps.Count(); i++ )
		delete s_CachedMaps[i].m_pMap;
	s_CachedMaps.Purge();
	for ( int i = 0; i < s_Vtxs.Count(); i++ )
	{
		delete s_Vtxs[i]->m_pView;
		delete s_Vtxs[i];
	}
	s_Vtxs.Purge();
	for ( int i = 0; i < s_Models.Count(); i++ )
	{
		delete s_Models[i]->m_pMdlView;
		delete s_Models[i]->m_pVvdView;
		delete s_Models[i]->m_pConvert;
		delete s_Models[i];
	}
	s_Models.Purge();
	ReleaseSRWLockExclusive( &s_Lock );
}
