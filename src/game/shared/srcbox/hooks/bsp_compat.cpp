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
// Hooked (KernelBase): CreateFileW/A, ReadFile, SetFilePointer(Ex),
// GetFileSize(Ex), GetFileInformationByHandle(Ex), CloseHandle,
// GetFileAttributes(Ex)W/A. Untracked handles pass straight through. Files
// inside VPKs aren't opened as files and aren't redirected.
//
//=============================================================================//

#include "cbase.h"
#include "tier1/utlbuffer.h"
#include "bsp_compat.h"
#include "mdl_compat.h"

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
// Static props -> TF2 v10 (72 bytes)
//-----------------------------------------------------------------------------
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

	int nColorOfs = -1;
	bool bFlags32 = false;
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
		return false;	// v4-v6 and TF2's own v10 are read by the engine as they are
	}

	out.Put( pData, nHeadLen );
	colors.SetCount( nProps );

	for ( int i = 0; i < nProps; i++ )
	{
		const unsigned char *pSrc = pProps + i * nStride;
		unsigned char dst[BC_STATICPROP_TF2_SIZE];
		memset( dst, 0, sizeof( dst ) );

		// origin, angles, proptype, firstleaf, leafcount, solid: 0-30
		memcpy( dst, pSrc, 31 );
		// skin, fade min/max, lighting origin, forced fade scale: 32-59
		memcpy( dst + 32, pSrc + 32, 28 );
		// 60-63: min/max DX level = 0 (no limit). The source CPU/GPU levels have no TF2 equivalent.

		unsigned int nFlags = bFlags32 ? *(const unsigned int *)( pSrc + 64 ) : pSrc[31];
		// keep FADES, USE_LIGHTING_ORIGIN, IGNORE_NORMALS, NO_SHADOW, NO_PER_VERTEX_LIGHTING,
		// NO_SELF_SHADOWING; NO_DRAW is computed at load and 0x20 is unused in these
		// formats (TF2: screen space fade). No per-texel lightmaps in these maps.
		nFlags = ( nFlags & 0xDB ) | 0x100;
		memcpy( dst + 64, &nFlags, 4 );
		// 68-71: lightmap resolution = 0

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
	if ( !bFound || ( nVersion <= 20 && !bL4D2Order && !bVersion18 ) )
		return NULL;

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
			for ( int k = 0; k < nCount; k++ )
			{
				const unsigned char *pSrc = data.Base() + k * BC_WORLDLIGHT_V1_SIZE;
				unsigned char *pDst = lights.Base() + k * BC_WORLDLIGHT_V0_SIZE;
				memcpy( pDst, pSrc, 36 );
				memcpy( pDst + 36, pSrc + 48, BC_WORLDLIGHT_V1_SIZE - 48 );
			}
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

// Views, one per (file, size, write time); kept for the life of the process
struct BCCachedMap_t
{
	wchar_t			m_szPath[MAX_PATH];
	bc_int64		m_nSize;
	FILETIME		m_WriteTime;
	BCVirtualMap_t	*m_pMap;	// NULL: this file needs no view
};

// A handle opened on a redirected map
struct BCTrackedHandle_t
{
	HANDLE			m_hFile;	// the caller's handle
	HANDLE			m_hReader;	// our own synchronous handle on the same file
	BCVirtualMap_t	*m_pMap;
	bc_int64		m_nPosition;
};

static SRWLOCK								s_Lock = SRWLOCK_INIT;
static CUtlVector< BCCachedMap_t >			s_CachedMaps;
static CUtlVector< BCTrackedHandle_t >		s_TrackedHandles;
static volatile LONG						s_nTrackedHandles = 0;	// lock-free fast path

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

		MDLCompat::CConvert37 *pConvert = new MDLCompat::CConvert37( data.Base(), data.Count() );
		MDLCompat::CByteBuf mdl, vvd;
		char szError[256];
		if ( pConvert->Convert( mdl, vvd, nLODs, szError, sizeof( szError ) ) )
		{
			pEntry->m_pMdlView = MakeMemoryView( mdl.Base(), mdl.Size() );
			pEntry->m_pVvdView = MakeMemoryView( vvd.Base(), vvd.Size() );
			pEntry->m_pConvert = pConvert;
			pConvert->ReleaseWorkData();
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

static void AddTrackedHandle( HANDLE hFile, HANDLE hReader, BCVirtualMap_t *pMap )
{
	AcquireSRWLockExclusive( &s_Lock );
	BCTrackedHandle_t &tracked = s_TrackedHandles[s_TrackedHandles.AddToTail()];
	tracked.m_hFile = hFile;
	tracked.m_hReader = hReader;
	tracked.m_pMap = pMap;
	tracked.m_nPosition = 0;
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

static void TrackIfMap( HANDLE hFile, LPCWSTR pszPath, DWORD nAccess, DWORD nFlagsAndAttributes )
{
	if ( hFile == INVALID_HANDLE_VALUE )
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
			DevMsg( "BSP v21: %s is read as a version 20 map\n", szName );

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

static HANDLE WINAPI Hook_CreateFileW( LPCWSTR pszPath, DWORD nAccess, DWORD nShare, LPSECURITY_ATTRIBUTES pSecurity,
	DWORD nDisposition, DWORD nFlagsAndAttributes, HANDLE hTemplate )
{
	HANDLE hFile = s_pfnCreateFileW( pszPath, nAccess, nShare, pSecurity, nDisposition, nFlagsAndAttributes, hTemplate );
	DWORD nError = GetLastError();	// keep what the real call reported (e.g. ERROR_ALREADY_EXISTS)
	if ( hFile != INVALID_HANDLE_VALUE )
	{
		TrackIfMap( hFile, pszPath, nAccess, nFlagsAndAttributes );
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
	HANDLE hFile = s_pfnCreateFileA( pszPath, nAccess, nShare, pSecurity, nDisposition, nFlagsAndAttributes, hTemplate );
	if ( hFile == INVALID_HANDLE_VALUE && pszPath )
	{
		DWORD nError = GetLastError();
		wchar_t szPath[MAX_PATH];
		if ( IsNotFoundError( nError ) && MultiByteToWideChar( CP_ACP, 0, pszPath, -1, szPath, MAX_PATH ) && IsModelPath( szPath ) )
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
		tracked = s_TrackedHandles[nTracked];
	ReleaseSRWLockShared( &s_Lock );

	if ( nTracked < 0 )
		return s_pfnReadFile( hFile, pBuffer, nToRead, pnRead, pOverlapped );

	bc_int64 nOffset = pOverlapped ? ( ( (bc_int64)pOverlapped->OffsetHigh << 32 ) | pOverlapped->Offset ) : tracked.m_nPosition;
	int nRead = VirtualRead( tracked.m_pMap, ReaderRead, tracked.m_hReader, nOffset, pBuffer, (int)MIN( nToRead, (DWORD)0x7FFFFFFF ) );

	// like Windows, a read moves the file pointer (overlapped handles don't use it)
	AcquireSRWLockExclusive( &s_Lock );
	nTracked = FindTracked( hFile );
	if ( nTracked >= 0 )
		s_TrackedHandles[nTracked].m_nPosition = nOffset + nRead;
	ReleaseSRWLockExclusive( &s_Lock );

	if ( pnRead )
		*pnRead = (DWORD)nRead;

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
	if ( nTracked >= 0 )
		*pnSize = s_TrackedHandles[nTracked].m_pMap->m_nVirtualSize;
	ReleaseSRWLockShared( &s_Lock );
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
		AcquireSRWLockExclusive( &s_Lock );
		int nTracked = FindTracked( hObject );
		if ( nTracked >= 0 )
		{
			hReader = s_TrackedHandles[nTracked].m_hReader;
			s_TrackedHandles.FastRemove( nTracked );
			InterlockedDecrement( &s_nTrackedHandles );
		}
		ReleaseSRWLockExclusive( &s_Lock );
		if ( hReader )
			s_pfnCloseHandle( hReader );
	}
	return s_pfnCloseHandle( hObject );
}


//-----------------------------------------------------------------------------
// File attributes: model files report their view's size, and the files made
// up for converted models exist.
//-----------------------------------------------------------------------------
static BOOL WINAPI Hook_GetFileAttributesExW( LPCWSTR pszPath, GET_FILEEX_INFO_LEVELS nLevel, LPVOID pInfo )
{
	BOOL bResult = s_pfnGetFileAttributesExW( pszPath, nLevel, pInfo );
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
	BOOL bResult = s_pfnGetFileAttributesExA( pszPath, nLevel, pInfo );
	wchar_t szPath[MAX_PATH];
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
	DWORD nAttributes = s_pfnGetFileAttributesW( pszPath );
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
	DWORD nAttributes = s_pfnGetFileAttributesA( pszPath );
	wchar_t szPath[MAX_PATH];
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
};

static bool s_bOwnsHooks = false;

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
			Warning( "BSP v21: couldn't hook %s, newer maps won't load\n", hook.m_pszFunction );
			RemoveHooks();
			return;
		}
		hook.m_pTarget = pTarget;
	}

	// The originals must all be in place before any detour can run
	for ( int i = 0; i < ARRAYSIZE( s_Hooks ); i++ )
	{
		if ( MH_EnableHook( s_Hooks[i].m_pTarget ) != MH_OK )
		{
			Warning( "BSP v21: couldn't enable the %s hook, newer maps won't load\n", s_Hooks[i].m_pszFunction );
			RemoveHooks();
			return;
		}
	}

	s_bOwnsHooks = true;
	SetEnvironmentVariableA( BSPCOMPAT_INSTALLED_VAR, szProcess );
}

void BSPCompat_Shutdown()
{
	if ( !s_bOwnsHooks )
		return;

	RemoveHooks();
	s_bOwnsHooks = false;
	SetEnvironmentVariableA( BSPCOMPAT_INSTALLED_VAR, NULL );

	// Handles still open now read the original file; their reader handles are released
	AcquireSRWLockExclusive( &s_Lock );
	for ( int i = 0; i < s_TrackedHandles.Count(); i++ )
	{
		if ( s_TrackedHandles[i].m_hReader )
			CloseHandle( s_TrackedHandles[i].m_hReader );
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
