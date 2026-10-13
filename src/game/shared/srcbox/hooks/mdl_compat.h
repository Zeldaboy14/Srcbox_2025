//========= Srcbox ============================================================//
//
// Purpose: MDL v37 (Half-Life 2 beta, 2003) -> v48 (Source 2013) conversion,
//          used by the file redirect in bsp_compat.cpp.
//
// Self-contained (no engine or tier1 dependencies) so the offline test
// harness builds exactly this code. Include it in one .cpp only.
//
// v37 differs from v48 nearly everywhere: most structs changed size, the
// vertices live in the .mdl (there is no .vvd), vertices carry four bone
// weights instead of three, animations are one fixed-size record per bone
// with int offsets, and the VTX (version 6) has four bone slots per vertex.
// CConvert37 rebuilds the .mdl in the v48 layout and makes the .vvd from the
// model's vertices; ConvertVTX6 rewrites the VTX vertices in place.
//
// Per-bone animation encoding: v37 and v48 both store animated values as
// RLE shorts scaled by the bone's scale and added to its default value, so
// those streams are copied as-is. v37 records that hold a fixed pose become
// either nothing (when the pose is the bone's rest pose), constant streams in
// the bone's own encoding, or a raw Quaternion64 + Vector48.
//
//=============================================================================//

#ifndef MDL_COMPAT_H
#define MDL_COMPAT_H
#ifdef _WIN32
#pragma once
#endif

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

namespace MDLCompat
{

//-----------------------------------------------------------------------------
// Growable byte buffer
//-----------------------------------------------------------------------------
class CByteBuf
{
public:
	CByteBuf() : m_pData( NULL ), m_nSize( 0 ), m_nAlloc( 0 ) {}
	~CByteBuf() { free( m_pData ); }

	int Size() const { return m_nSize; }
	unsigned char *Base() { return m_pData; }
	const unsigned char *Base() const { return m_pData; }

	// Takes ownership of the memory away from the buffer
	unsigned char *Detach( int *pnSize )
	{
		unsigned char *p = m_pData;
		*pnSize = m_nSize;
		m_pData = NULL;
		m_nSize = m_nAlloc = 0;
		return p;
	}

	bool Reserve( int nSize )
	{
		if ( nSize <= m_nAlloc )
			return true;
		int nNew = m_nAlloc ? m_nAlloc : 4096;
		while ( nNew < nSize )
			nNew *= 2;
		unsigned char *p = (unsigned char *)realloc( m_pData, nNew );
		if ( !p )
			return false;
		m_pData = p;
		m_nAlloc = nNew;
		return true;
	}

	// Appends zeroed space, returns its offset
	int Alloc( int nSize, int nAlign = 4 )
	{
		int nStart = ( m_nSize + nAlign - 1 ) & ~( nAlign - 1 );
		if ( !Reserve( nStart + nSize ) )
			return -1;
		memset( m_pData + m_nSize, 0, nStart + nSize - m_nSize );
		m_nSize = nStart + nSize;
		return nStart;
	}

	int Append( const void *pData, int nSize, int nAlign = 1 )
	{
		int nOfs = Alloc( nSize, nAlign );
		if ( nOfs >= 0 && nSize > 0 )
			memcpy( m_pData + nOfs, pData, nSize );
		return nOfs;
	}

	void SetInt( int nOfs, int nValue ) { memcpy( m_pData + nOfs, &nValue, 4 ); }
	void SetFloat( int nOfs, float flValue ) { memcpy( m_pData + nOfs, &flValue, 4 ); }
	void SetShort( int nOfs, short nValue ) { memcpy( m_pData + nOfs, &nValue, 2 ); }
	void SetByte( int nOfs, unsigned char nValue ) { m_pData[nOfs] = nValue; }
	void SetBytes( int nOfs, const void *pData, int nSize ) { memcpy( m_pData + nOfs, pData, nSize ); }
	int GetInt( int nOfs ) const { int n; memcpy( &n, m_pData + nOfs, 4 ); return n; }

private:
	CByteBuf( const CByteBuf & );
	CByteBuf &operator=( const CByteBuf & );

	unsigned char	*m_pData;
	int				m_nSize;
	int				m_nAlloc;
};

//-----------------------------------------------------------------------------
// Simple growable array of POD
//-----------------------------------------------------------------------------
template< class T >
class CPodArray
{
public:
	CPodArray() : m_p( NULL ), m_n( 0 ), m_nAlloc( 0 ) {}
	~CPodArray() { free( m_p ); }
	int Count() const { return m_n; }
	T &operator[]( int i ) { return m_p[i]; }
	const T &operator[]( int i ) const { return m_p[i]; }
	T *Base() { return m_p; }
	void Purge() { free( m_p ); m_p = NULL; m_n = m_nAlloc = 0; }
	bool SetCount( int n )
	{
		if ( n > m_nAlloc )
		{
			int nNew = m_nAlloc ? m_nAlloc : 16;
			while ( nNew < n )
				nNew *= 2;
			T *p = (T *)realloc( m_p, nNew * sizeof( T ) );
			if ( !p )
				return false;
			m_p = p;
			m_nAlloc = nNew;
		}
		if ( n > m_n )
			memset( m_p + m_n, 0, ( n - m_n ) * sizeof( T ) );
		m_n = n;
		return true;
	}
	int AddToTail( const T &t )
	{
		if ( !SetCount( m_n + 1 ) )
			return -1;
		m_p[m_n - 1] = t;
		return m_n - 1;
	}
private:
	CPodArray( const CPodArray & );
	CPodArray &operator=( const CPodArray & );
	T *m_p;
	int m_n, m_nAlloc;
};

//-----------------------------------------------------------------------------
// Bounds-checked reader over the input file. Any read outside the file sets
// the error flag and returns zeroes, so a damaged file can't crash the game.
//-----------------------------------------------------------------------------
class CReader
{
public:
	CReader( const unsigned char *pData, int nSize ) : m_pData( pData ), m_nSize( nSize ), m_bError( false ) {}

	bool InRange( long long nOfs, long long nSize ) const
	{
		return nOfs >= 0 && nSize >= 0 && nOfs <= m_nSize && nSize <= m_nSize - nOfs;
	}
	bool Check( long long nOfs, long long nSize )
	{
		if ( InRange( nOfs, nSize ) )
			return true;
		m_bError = true;
		return false;
	}
	int Int( int nOfs ) { int n = 0; if ( Check( nOfs, 4 ) ) memcpy( &n, m_pData + nOfs, 4 ); return n; }
	float Float( int nOfs ) { float f = 0; if ( Check( nOfs, 4 ) ) memcpy( &f, m_pData + nOfs, 4 ); return f; }
	short Short( int nOfs ) { short n = 0; if ( Check( nOfs, 2 ) ) memcpy( &n, m_pData + nOfs, 2 ); return n; }
	unsigned char Byte( int nOfs ) { return Check( nOfs, 1 ) ? m_pData[nOfs] : 0; }
	void Bytes( int nOfs, void *pDest, int nSize )
	{
		if ( Check( nOfs, nSize ) )
			memcpy( pDest, m_pData + nOfs, nSize );
		else
			memset( pDest, 0, nSize );
	}
	// A NUL-terminated string at nOfs, or "" if there isn't one inside the file
	const char *String( int nOfs ) const
	{
		if ( nOfs <= 0 || nOfs >= m_nSize )
			return "";
		const unsigned char *pEnd = (const unsigned char *)memchr( m_pData + nOfs, 0, m_nSize - nOfs );
		return pEnd ? (const char *)( m_pData + nOfs ) : "";
	}
	const unsigned char *Ptr( int nOfs ) const { return m_pData + nOfs; }
	int Size() const { return m_nSize; }
	bool Error() const { return m_bError; }
	void SetError() { m_bError = true; }

private:
	const unsigned char	*m_pData;
	int					m_nSize;
	bool				m_bError;
};

//-----------------------------------------------------------------------------
// Constants and layouts
//-----------------------------------------------------------------------------
enum
{
	MC_IDST = ( 'T' << 24 ) + ( 'S' << 16 ) + ( 'D' << 8 ) + 'I',
	MC_IDSV = ( 'V' << 24 ) + ( 'S' << 16 ) + ( 'D' << 8 ) + 'I',

	// v37 (HL2 beta) struct sizes
	V37_HEADER_SIZE			= 388,
	V37_BONE_SIZE			= 196,
	V37_BONECONTROLLER_SIZE	= 56,
	V37_HITBOXSET_SIZE		= 12,
	V37_HITBOX_SIZE			= 68,
	V37_ANIMDESC_SIZE		= 92,
	V37_ANIM_SIZE			= 32,
	V37_MOVEMENT_SIZE		= 44,
	V37_IKRULE_SIZE			= 108,
	V37_IKERROR_SIZE		= 28,
	V37_SEQDESC_SIZE		= 188,
	V37_EVENT_SIZE			= 76,
	V37_AUTOLAYER_SIZE		= 24,
	V37_IKLOCK_SIZE			= 12,
	V37_SEQGROUP_SIZE		= 16,
	V37_ANIMGROUP_SIZE		= 8,
	V37_TEXTURE_SIZE		= 32,
	V37_BODYPART_SIZE		= 16,
	V37_MODEL_SIZE			= 140,
	V37_MESH_SIZE			= 68,
	V37_VERTEX_SIZE			= 64,
	V37_EYEBALL_SIZE		= 140,
	V37_FLEX_SIZE			= 28,
	V37_VERTANIM_SIZE		= 28,
	V37_ATTACHMENT_SIZE		= 60,
	V37_FLEXDESC_SIZE		= 4,
	V37_FLEXCONTROLLER_SIZE	= 20,
	V37_FLEXRULE_SIZE		= 12,
	V37_FLEXOP_SIZE			= 8,
	V37_IKCHAIN_SIZE		= 16,
	V37_IKLINK_SIZE			= 28,
	V37_MOUTH_SIZE			= 20,
	V37_POSEPARAM_SIZE		= 20,
	V37_QUATINTERPINFO_SIZE	= 60,

	// v48 (Source 2013) struct sizes, 32-bit file layout
	V48_HEADER_SIZE			= 408,
	V48_HEADER2_SIZE		= 256,
	V48_BONE_SIZE			= 216,
	V48_BONECONTROLLER_SIZE	= 56,
	V48_HITBOXSET_SIZE		= 12,
	V48_HITBOX_SIZE			= 68,
	V48_ANIMDESC_SIZE		= 100,
	V48_MOVEMENT_SIZE		= 44,
	V48_IKRULE_SIZE			= 152,
	V48_IKERROR_SIZE		= 28,
	V48_SEQDESC_SIZE		= 212,
	V48_EVENT_SIZE			= 80,
	V48_AUTOLAYER_SIZE		= 24,
	V48_IKLOCK_SIZE			= 32,
	V48_TEXTURE_SIZE		= 64,
	V48_BODYPART_SIZE		= 16,
	V48_MODEL_SIZE			= 148,
	V48_MESH_SIZE			= 116,
	V48_EYEBALL_SIZE		= 172,
	V48_FLEX_SIZE			= 60,
	V48_VERTANIM_SIZE		= 16,
	V48_ATTACHMENT_SIZE		= 92,
	V48_FLEXDESC_SIZE		= 4,
	V48_FLEXCONTROLLER_SIZE	= 20,
	V48_FLEXRULE_SIZE		= 12,
	V48_FLEXOP_SIZE			= 8,
	V48_IKCHAIN_SIZE		= 16,
	V48_IKLINK_SIZE			= 28,
	V48_MOUTH_SIZE			= 20,
	V48_POSEPARAM_SIZE		= 20,
	V48_MODELGROUP_SIZE		= 8,
	V48_QUATINTERPBONE_SIZE	= 12,
	V48_QUATINTERPINFO_SIZE	= 60,

	VVD_HEADER_SIZE			= 64,
	VVD_VERTEX_SIZE			= 48,
	VVD_TANGENT_SIZE		= 16,
	MC_MAX_LODS				= 8,

	MC_STUDIO_PROC_QUATINTERP	= 2,

	MC_STUDIO_ANIM_RAWPOS		= 0x01,
	MC_STUDIO_ANIM_RAWROT2		= 0x20,
	MC_STUDIO_ANIM_ANIMPOS		= 0x04,
	MC_STUDIO_ANIM_ANIMROT		= 0x08,
	MC_STUDIO_ANIM_DELTA		= 0x10,

	V37_POS_ANIMATED		= 0x01,
	V37_ROT_ANIMATED		= 0x02,

	MC_STUDIO_DELTA			= 0x0004,

	MC_BONE_USED_BY_ANYTHING_BUT_MERGE	= 0x0003FF00,	// hitbox, attachment, vertex LOD0-7
};

//-----------------------------------------------------------------------------
// Math helpers (Source conventions: RadianEuler x = roll, y = pitch, z = yaw)
//-----------------------------------------------------------------------------
static void AngleQuaternion( const float *a, float *q )
{
	float sr = sinf( a[0] * 0.5f ), cr = cosf( a[0] * 0.5f );
	float sp = sinf( a[1] * 0.5f ), cp = cosf( a[1] * 0.5f );
	float sy = sinf( a[2] * 0.5f ), cy = cosf( a[2] * 0.5f );
	float srXcp = sr * cp, crXsp = cr * sp;
	q[0] = srXcp * cy - crXsp * sy;
	q[1] = crXsp * cy + srXcp * sy;
	float crXcp = cr * cp, srXsp = sr * sp;
	q[2] = crXcp * sy - srXsp * cy;
	q[3] = crXcp * cy + srXsp * sy;
}

static void QuaternionAngles( const float *q, float *a )
{
	float m00 = 1.0f - 2.0f * q[1] * q[1] - 2.0f * q[2] * q[2];
	float m10 = 2.0f * q[0] * q[1] + 2.0f * q[3] * q[2];
	float m20 = 2.0f * q[0] * q[2] - 2.0f * q[3] * q[1];
	float m01 = 2.0f * q[0] * q[1] - 2.0f * q[3] * q[2];
	float m11 = 1.0f - 2.0f * q[0] * q[0] - 2.0f * q[2] * q[2];
	float m21 = 2.0f * q[1] * q[2] + 2.0f * q[3] * q[0];
	float m22 = 1.0f - 2.0f * q[0] * q[0] - 2.0f * q[1] * q[1];
	float xyDist = sqrtf( m00 * m00 + m10 * m10 );
	if ( xyDist > 0.001f )
	{
		a[2] = atan2f( m10, m00 );		// yaw
		a[1] = atan2f( -m20, xyDist );	// pitch
		a[0] = atan2f( m21, m22 );		// roll
	}
	else
	{
		a[2] = atan2f( -m01, m11 );
		a[1] = atan2f( -m20, xyDist );
		a[0] = 0.0f;
	}
}

static bool QuaternionsMatch( const float *a, const float *b, float flTolerance )
{
	float flDot = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
	return fabsf( flDot ) >= 1.0f - flTolerance;
}

static unsigned short FloatToHalf( float f )
{
	unsigned int i;
	memcpy( &i, &f, 4 );
	unsigned int nSign = ( i >> 16 ) & 0x8000;
	int nExp = (int)( ( i >> 23 ) & 0xFF ) - 127 + 15;
	unsigned int nMant = i & 0x7FFFFF;
	if ( nExp <= 0 )
	{
		if ( nExp < -10 )
			return (unsigned short)nSign;
		nMant |= 0x800000;
		unsigned int nShift = (unsigned int)( 14 - nExp );
		unsigned int nHalf = nMant >> nShift;
		if ( ( nMant >> ( nShift - 1 ) ) & 1 )
			nHalf++;
		return (unsigned short)( nSign | nHalf );
	}
	if ( nExp >= 31 )
		return (unsigned short)( nSign | 0x7BFF );	// clamp to the largest finite value
	unsigned int nHalf = nSign | ( nExp << 10 ) | ( nMant >> 13 );
	if ( nMant & 0x1000 )
		nHalf++;	// round
	return (unsigned short)nHalf;
}

//-----------------------------------------------------------------------------
// Strings written at the end of the output, referenced relative to a base
//-----------------------------------------------------------------------------
struct StringRef_t
{
	int			m_nField;	// where the offset goes
	int			m_nBase;	// what it's relative to
	const char	*m_pszString;
};

class CStringTable
{
public:
	void Add( int nField, int nBase, const char *pszString )
	{
		StringRef_t ref = { nField, nBase, pszString ? pszString : "" };
		m_Refs.AddToTail( ref );
	}
	void Write( CByteBuf &out )
	{
		// identical strings share storage
		CPodArray< int > written;
		written.SetCount( m_Refs.Count() );
		for ( int i = 0; i < m_Refs.Count(); i++ )
		{
			int nOfs = -1;
			for ( int j = 0; j < i; j++ )
			{
				if ( !strcmp( m_Refs[j].m_pszString, m_Refs[i].m_pszString ) )
				{
					nOfs = written[j];
					break;
				}
			}
			if ( nOfs < 0 )
				nOfs = out.Append( m_Refs[i].m_pszString, (int)strlen( m_Refs[i].m_pszString ) + 1 );
			written[i] = nOfs;
			out.SetInt( m_Refs[i].m_nField, nOfs - m_Refs[i].m_nBase );
		}
	}
private:
	CPodArray< StringRef_t > m_Refs;
};

//-----------------------------------------------------------------------------
// Per-bone animation encoding parameters (shared by all animations)
//-----------------------------------------------------------------------------
struct BoneInfo_t
{
	float	m_Value[6];		// default pos + euler
	float	m_Scale[6];		// pos scale + euler scale
	float	m_Quat[4];
};

// Length in shorts of an RLE animation value stream covering nFrames
static int AnimValueStreamLength( CReader &in, int nOfs, int nFrames )
{
	int nShorts = 0;
	int nRemaining = nFrames;
	while ( nRemaining > 0 )
	{
		if ( !in.InRange( nOfs + nShorts * 2, 2 ) )
			return -1;
		unsigned char nValid = in.Byte( nOfs + nShorts * 2 );
		unsigned char nTotal = in.Byte( nOfs + nShorts * 2 + 1 );
		if ( nTotal == 0 )
			return -1;
		nShorts += 1 + nValid;
		nRemaining -= nTotal;
		if ( nShorts > 0x10000 )
			return -1;
	}
	return in.InRange( nOfs, (long long)( nShorts ) * 2 ) ? nShorts : -1;
}

// Writes a stream that holds one value for every frame
static int WriteConstantStream( CByteBuf &out, int nFrames, short nValue )
{
	// { valid, total } headers cover at most 255 frames each
	int nStart = -1;
	while ( nFrames > 0 )
	{
		int nTotal = nFrames > 255 ? 255 : nFrames;
		short header;
		unsigned char *p = (unsigned char *)&header;
		p[0] = 1;
		p[1] = (unsigned char)nTotal;
		int nOfs = out.Append( &header, 2, 2 );
		if ( nStart < 0 )
			nStart = nOfs;
		out.Append( &nValue, 2, 2 );
		nFrames -= nTotal;
	}
	return nStart;
}

//-----------------------------------------------------------------------------
// The converter
//-----------------------------------------------------------------------------
class CConvert37
{
public:
	CConvert37( const unsigned char *pData, int nSize ) : m_In( pData, nSize ) {}

	bool Convert( CByteBuf &mdl, CByteBuf &vvd, int nVVDLODs, char *pszError, int nErrorSize );

	// After Convert: frees what only the conversion needed (ConvertVTX6 still works)
	void ReleaseWorkData()
	{
		m_Verts.SetCount( 0 );
		m_Verts.Purge();
		m_Bones.Purge();
		m_AnimDescOfs.Purge();
		m_AnimIsDelta.Purge();
	}

	// Bone weights of the converted vertices: for each model vertex (in
	// body part / model order), where each of the four v37 weights went
	// (0-2, or -1 when it was dropped). Used to convert the VTX.
	CPodArray< signed char > m_WeightRemap;	// 4 per vertex
	CPodArray< short > m_SlotBone;				// the v37 bone of each of those four weights
	CPodArray< int > m_ModelVertexStart;		// first global vertex of each model, body part order
	CPodArray< int > m_MeshVertexOffset;		// vertexoffset of each mesh, body part / model order
	CPodArray< int > m_ModelMeshStart;			// first mesh of each model

private:
	int H( int nField ) { return m_In.Int( 80 + 72 + nField * 4 ); }	// v37 header ints after the vectors
	void Fail( const char *psz ) { if ( !m_pszFirstError ) m_pszFirstError = psz; m_In.SetError(); }

	void WriteBones();
	void WriteBoneControllers();
	void WriteHitboxes();
	void PrepareAnimScales();
	void WriteAnimations();
	int WriteAnimRecords( int nAnimDesc37, int nNumFrames, bool bDelta );
	void WriteSequences();
	void WriteTextures();
	void WriteBodyParts();
	void WriteVertexFile( CByteBuf &vvd, int nVVDLODs );
	void WriteAttachments();
	void WriteFlexes();
	void WriteIK();
	void WriteMisc();

	CReader			m_In;
	CByteBuf		*m_pOut;
	CStringTable	m_Strings;
	const char		*m_pszFirstError;

	int				m_nNumBones;
	CPodArray< BoneInfo_t > m_Bones;
	CPodArray< int > m_AnimDescOfs;	// v48 offsets of the animdescs
	CPodArray< unsigned char > m_AnimIsDelta;

	// vertices gathered from the models, in body part / model order
	struct Vertex_t
	{
		float			m_Weight[3];
		char			m_Bone[3];
		unsigned char	m_NumBones;
		float			m_Pos[3];
		float			m_Normal[3];
		float			m_UV[2];
		float			m_Tangent[4];
		bool			m_bHasTangent;
	};
	CPodArray< Vertex_t > m_Verts;
	bool			m_bAnyTangents;
	char			m_szNodeNames[256][12];
};

// v37 header field indexes (ints after the six vectors)
enum
{
	H37_FLAGS, H37_NUMBONES, H37_BONEINDEX, H37_NUMBONECONTROLLERS, H37_BONECONTROLLERINDEX,
	H37_NUMHITBOXSETS, H37_HITBOXSETINDEX, H37_NUMANIM, H37_ANIMDESCINDEX, H37_NUMANIMGROUP,
	H37_ANIMGROUPINDEX, H37_NUMBONEDESC, H37_BONEDESCINDEX, H37_NUMSEQ, H37_SEQINDEX,
	H37_SEQUENCESINDEXED, H37_NUMSEQGROUPS, H37_SEQGROUPINDEX, H37_NUMTEXTURES, H37_TEXTUREINDEX,
	H37_NUMCDTEXTURES, H37_CDTEXTUREINDEX, H37_NUMSKINREF, H37_NUMSKINFAMILIES, H37_SKININDEX,
	H37_NUMBODYPARTS, H37_BODYPARTINDEX, H37_NUMATTACHMENTS, H37_ATTACHMENTINDEX, H37_NUMTRANSITIONS,
	H37_TRANSITIONINDEX, H37_NUMFLEXDESC, H37_FLEXDESCINDEX, H37_NUMFLEXCONTROLLERS, H37_FLEXCONTROLLERINDEX,
	H37_NUMFLEXRULES, H37_FLEXRULEINDEX, H37_NUMIKCHAINS, H37_IKCHAININDEX, H37_NUMMOUTHS,
	H37_MOUTHINDEX, H37_NUMPOSEPARAMETERS, H37_POSEPARAMINDEX, H37_SURFACEPROPINDEX, H37_KEYVALUEINDEX,
	H37_KEYVALUESIZE, H37_NUMIKAUTOPLAYLOCKS, H37_IKAUTOPLAYLOCKINDEX, H37_MASS, H37_CONTENTS,
};

// v48 header field offsets
enum
{
	H48_ID = 0, H48_VERSION = 4, H48_CHECKSUM = 8, H48_NAME = 12, H48_LENGTH = 76,
	H48_EYEPOSITION = 80,	// 6 vectors through 152
	H48_FLAGS = 152, H48_NUMBONES = 156, H48_BONEINDEX = 160, H48_NUMBONECONTROLLERS = 164, H48_BONECONTROLLERINDEX = 168,
	H48_NUMHITBOXSETS = 172, H48_HITBOXSETINDEX = 176, H48_NUMLOCALANIM = 180, H48_LOCALANIMINDEX = 184,
	H48_NUMLOCALSEQ = 188, H48_LOCALSEQINDEX = 192, H48_ACTIVITYLISTVERSION = 196, H48_EVENTSINDEXED = 200,
	H48_NUMTEXTURES = 204, H48_TEXTUREINDEX = 208, H48_NUMCDTEXTURES = 212, H48_CDTEXTUREINDEX = 216,
	H48_NUMSKINREF = 220, H48_NUMSKINFAMILIES = 224, H48_SKININDEX = 228, H48_NUMBODYPARTS = 232, H48_BODYPARTINDEX = 236,
	H48_NUMLOCALATTACHMENTS = 240, H48_LOCALATTACHMENTINDEX = 244, H48_NUMLOCALNODES = 248, H48_LOCALNODEINDEX = 252,
	H48_LOCALNODENAMEINDEX = 256, H48_NUMFLEXDESC = 260, H48_FLEXDESCINDEX = 264, H48_NUMFLEXCONTROLLERS = 268,
	H48_FLEXCONTROLLERINDEX = 272, H48_NUMFLEXRULES = 276, H48_FLEXRULEINDEX = 280, H48_NUMIKCHAINS = 284,
	H48_IKCHAININDEX = 288, H48_NUMMOUTHS = 292, H48_MOUTHINDEX = 296, H48_NUMLOCALPOSEPARAMETERS = 300,
	H48_LOCALPOSEPARAMINDEX = 304, H48_SURFACEPROPINDEX = 308, H48_KEYVALUEINDEX = 312, H48_KEYVALUESIZE = 316,
	H48_NUMLOCALIKAUTOPLAYLOCKS = 320, H48_LOCALIKAUTOPLAYLOCKINDEX = 324, H48_MASS = 328, H48_CONTENTS = 332,
	H48_NUMINCLUDEMODELS = 336, H48_INCLUDEMODELINDEX = 340, H48_VIRTUALMODEL = 344, H48_SZANIMBLOCKNAMEINDEX = 348,
	H48_NUMANIMBLOCKS = 352, H48_ANIMBLOCKINDEX = 356, H48_ANIMBLOCKMODEL = 360, H48_BONETABLEBYNAMEINDEX = 364,
	H48_VERTEXBASE = 368, H48_INDEXBASE = 372, H48_CONSTDIRECTIONALLIGHTDOT = 376, H48_ROOTLOD = 377,
	H48_NUMALLOWEDROOTLODS = 378, H48_UNUSED4 = 380, H48_NUMFLEXCONTROLLERUI = 384, H48_FLEXCONTROLLERUIINDEX = 388,
	H48_VERTANIMFIXEDPOINTSCALE = 392, H48_STUDIOHDR2INDEX = 400,
};

//-----------------------------------------------------------------------------
// Bones
//-----------------------------------------------------------------------------
void CConvert37::WriteBones()
{
	CByteBuf &out = *m_pOut;
	const int nBones = m_nNumBones;
	const int nBone37 = H( H37_BONEINDEX );
	if ( !nBones )
		return;

	int nOut = out.Alloc( nBones * V48_BONE_SIZE );
	out.SetInt( H48_NUMBONES, nBones );
	out.SetInt( H48_BONEINDEX, nOut );

	for ( int i = 0; i < nBones; i++ )
	{
		const int s = nBone37 + i * V37_BONE_SIZE;
		const int d = nOut + i * V48_BONE_SIZE;
		BoneInfo_t &info = m_Bones[i];

		m_Strings.Add( d + 0, d, m_In.String( s + m_In.Int( s + 0 ) ) );
		out.SetInt( d + 4, m_In.Int( s + 4 ) );										// parent
		for ( int j = 0; j < 6; j++ )
			out.SetInt( d + 8 + j * 4, m_In.Int( s + 8 + j * 4 ) );					// bonecontroller[6]
		for ( int j = 0; j < 6; j++ )
		{
			info.m_Value[j] = m_In.Float( s + 32 + j * 4 );
			info.m_Scale[j] = m_In.Float( s + 56 + j * 4 );
		}
		for ( int j = 0; j < 4; j++ )
			info.m_Quat[j] = m_In.Float( s + 164 + j * 4 );

		// pos, quat, rot, posscale, rotscale are written after the animation
		// pass, which may have to pick scales for constant values
		unsigned char poseToBone[48], qAlignment[16];
		m_In.Bytes( s + 80, poseToBone, 48 );
		m_In.Bytes( s + 128, qAlignment, 16 );
		out.SetBytes( d + 96, poseToBone, 48 );
		out.SetBytes( d + 144, qAlignment, 16 );

		out.SetInt( d + 160, m_In.Int( s + 144 ) | MC_BONE_USED_BY_ANYTHING_BUT_MERGE );	// flags
		out.SetInt( d + 172, m_In.Int( s + 156 ) );									// physicsbone
		m_Strings.Add( d + 176, d, m_In.String( s + m_In.Int( s + 160 ) ) );		// surfacepropidx
		out.SetInt( d + 180, m_In.Int( s + 180 ) );									// contents
	}

	// procedural bones: quaternion interpolation keeps its layout. Axis
	// interpolation in v37 has no axis field; those bones are left to animate
	// normally.
	for ( int i = 0; i < nBones; i++ )
	{
		const int s = nBone37 + i * V37_BONE_SIZE;
		const int d = nOut + i * V48_BONE_SIZE;
		int nProcType = m_In.Int( s + 148 );
		int nProcIndex = m_In.Int( s + 152 );
		if ( nProcType != MC_STUDIO_PROC_QUATINTERP || !nProcIndex )
			continue;
		const int p = s + nProcIndex;
		int nControl = m_In.Int( p + 0 );
		int nTriggers = m_In.Int( p + 4 );
		int nTriggerIndex = m_In.Int( p + 8 );
		if ( nTriggers <= 0 || nTriggers > 64 || !m_In.InRange( p + nTriggerIndex, (long long)( nTriggers ) * V37_QUATINTERPINFO_SIZE ) )
			continue;
		int nProc = out.Alloc( V48_QUATINTERPBONE_SIZE );
		int nInfo = out.Alloc( nTriggers * V48_QUATINTERPINFO_SIZE );
		out.SetInt( nProc + 0, nControl );
		out.SetInt( nProc + 4, nTriggers );
		out.SetInt( nProc + 8, nInfo - nProc );
		out.SetBytes( nInfo, m_In.Ptr( p + nTriggerIndex ), nTriggers * V48_QUATINTERPINFO_SIZE );
		out.SetInt( d + 164, MC_STUDIO_PROC_QUATINTERP );
		out.SetInt( d + 168, nProc - d );
	}

	// bone table sorted by name
	int nTable = out.Alloc( nBones, 1 );
	out.SetInt( H48_BONETABLEBYNAMEINDEX, nTable );
	CPodArray< int > order;
	order.SetCount( nBones );
	for ( int i = 0; i < nBones; i++ )
		order[i] = i;
	for ( int i = 1; i < nBones; i++ )
	{
		int n = order[i];
		const char *pszName = m_In.String( nBone37 + n * V37_BONE_SIZE + m_In.Int( nBone37 + n * V37_BONE_SIZE ) );
		int j = i - 1;
		while ( j >= 0 )
		{
			int m = order[j];
			const char *pszOther = m_In.String( nBone37 + m * V37_BONE_SIZE + m_In.Int( nBone37 + m * V37_BONE_SIZE ) );
#ifdef _WIN32
			if ( _stricmp( pszOther, pszName ) <= 0 )
#else
			if ( strcasecmp( pszOther, pszName ) <= 0 )
#endif
				break;
			order[j + 1] = order[j];
			j--;
		}
		order[j + 1] = n;
	}
	for ( int i = 0; i < nBones; i++ )
		out.SetByte( nTable + i, (unsigned char)order[i] );
}

// The per-bone values that the animation pass may have adjusted
static void FinishBones( CByteBuf &out, const CPodArray< BoneInfo_t > &bones )
{
	if ( !bones.Count() )
		return;
	int nBoneIndex = out.GetInt( H48_BONEINDEX );
	for ( int i = 0; i < bones.Count(); i++ )
	{
		const int d = nBoneIndex + i * V48_BONE_SIZE;
		const BoneInfo_t &info = bones[i];
		for ( int j = 0; j < 3; j++ )
		{
			out.SetFloat( d + 32 + j * 4, info.m_Value[j] );		// pos
			out.SetFloat( d + 60 + j * 4, info.m_Value[3 + j] );	// rot
			out.SetFloat( d + 72 + j * 4, info.m_Scale[j] );		// posscale
			out.SetFloat( d + 84 + j * 4, info.m_Scale[3 + j] );	// rotscale
		}
		for ( int j = 0; j < 4; j++ )
			out.SetFloat( d + 44 + j * 4, info.m_Quat[j] );			// quat
	}
}

void CConvert37::WriteBoneControllers()
{
	CByteBuf &out = *m_pOut;
	int nCount = H( H37_NUMBONECONTROLLERS );
	int nIn = H( H37_BONECONTROLLERINDEX );
	if ( nCount <= 0 || !m_In.InRange( nIn, (long long)( nCount ) * V37_BONECONTROLLER_SIZE ) )
		return;
	int nOut = out.Alloc( nCount * V48_BONECONTROLLER_SIZE );
	out.SetInt( H48_NUMBONECONTROLLERS, nCount );
	out.SetInt( H48_BONECONTROLLERINDEX, nOut );
	for ( int i = 0; i < nCount; i++ )
		out.SetBytes( nOut + i * V48_BONECONTROLLER_SIZE, m_In.Ptr( nIn + i * V37_BONECONTROLLER_SIZE ), 24 );	// padding stays zero
}

void CConvert37::WriteHitboxes()
{
	CByteBuf &out = *m_pOut;
	int nSets = H( H37_NUMHITBOXSETS );
	int nIn = H( H37_HITBOXSETINDEX );
	if ( nSets <= 0 || !m_In.InRange( nIn, (long long)( nSets ) * V37_HITBOXSET_SIZE ) )
		return;

	int nOut = out.Alloc( nSets * V48_HITBOXSET_SIZE );
	out.SetInt( H48_NUMHITBOXSETS, nSets );
	out.SetInt( H48_HITBOXSETINDEX, nOut );
	for ( int i = 0; i < nSets; i++ )
	{
		const int s = nIn + i * V37_HITBOXSET_SIZE;
		const int d = nOut + i * V48_HITBOXSET_SIZE;
		int nBoxes = m_In.Int( s + 4 );
		int nBoxIndex = s + m_In.Int( s + 8 );
		m_Strings.Add( d + 0, d, m_In.String( s + m_In.Int( s ) ) );
		if ( nBoxes <= 0 || !m_In.InRange( nBoxIndex, (long long)( nBoxes ) * V37_HITBOX_SIZE ) )
			continue;

		int nBoxOut = out.Alloc( nBoxes * V48_HITBOX_SIZE );
		out.SetInt( d + 4, nBoxes );
		out.SetInt( d + 8, nBoxOut - d );
		for ( int j = 0; j < nBoxes; j++ )
		{
			const int bs = nBoxIndex + j * V37_HITBOX_SIZE;
			const int bd = nBoxOut + j * V48_HITBOX_SIZE;
			out.SetBytes( bd, m_In.Ptr( bs ), 32 );	// bone, group, bbmin, bbmax
			// v37 hitbox names are offsets from the start of the file
			const char *pszName = m_In.String( m_In.Int( bs + 32 ) );
			if ( pszName[0] )
				m_Strings.Add( bd + 32, bd, pszName );
		}
	}
}

//-----------------------------------------------------------------------------
// Animations
//-----------------------------------------------------------------------------

// Bones whose scale is zero on some axis can't hold constant values on it.
// No v37 animation has data on such an axis, so the scale can be set freely:
// pick one that covers every constant the animations will need there.
void CConvert37::PrepareAnimScales()
{
	const int nAnims = H( H37_NUMANIM );
	const int nDescIndex = H( H37_ANIMDESCINDEX );

	CPodArray< float > maxDelta;
	maxDelta.SetCount( m_nNumBones * 6 );

	for ( int a = 0; a < nAnims; a++ )
	{
		const int ad = nDescIndex + a * V37_ANIMDESC_SIZE;
		const int nAnimIndex = ad + m_In.Int( ad + 48 );
		const bool bDelta = m_AnimIsDelta[a] != 0;
		if ( !m_In.InRange( nAnimIndex, (long long)( m_nNumBones ) * V37_ANIM_SIZE ) )
			continue;

		for ( int b = 0; b < m_nNumBones; b++ )
		{
			const int r = nAnimIndex + b * V37_ANIM_SIZE;
			const int nFlags = m_In.Int( r );
			const bool bPosAnim = ( nFlags & V37_POS_ANIMATED ) != 0;
			const bool bRotAnim = ( nFlags & V37_ROT_ANIMATED ) != 0;
			if ( bPosAnim == bRotAnim )
				continue;	// both raw or both animated: no constants needed

			const BoneInfo_t &bone = m_Bones[b];
			if ( !bPosAnim )
			{
				for ( int j = 0; j < 3; j++ )
				{
					float flBase = bDelta ? 0.0f : bone.m_Value[j];
					float d = fabsf( m_In.Float( r + 4 + j * 4 ) - flBase );
					if ( d > maxDelta[b * 6 + j] )
						maxDelta[b * 6 + j] = d;
				}
			}
			else
			{
				float q[4], angles[3];
				for ( int j = 0; j < 4; j++ )
					q[j] = m_In.Float( r + 16 + j * 4 );
				QuaternionAngles( q, angles );
				for ( int j = 0; j < 3; j++ )
				{
					float flBase = bDelta ? 0.0f : bone.m_Value[3 + j];
					float d = fabsf( angles[j] - flBase );
					if ( d > maxDelta[b * 6 + 3 + j] )
						maxDelta[b * 6 + 3 + j] = d;
				}
			}
		}
	}

	for ( int b = 0; b < m_nNumBones; b++ )
	{
		for ( int j = 0; j < 6; j++ )
		{
			if ( m_Bones[b].m_Scale[j] == 0.0f && maxDelta[b * 6 + j] > 0.0f )
				m_Bones[b].m_Scale[j] = maxDelta[b * 6 + j] / 32000.0f;
		}
	}
}

// Writes the v48 mstudioanim_t chain for one v37 animation; returns its offset
int CConvert37::WriteAnimRecords( int nAnimIndex37, int nNumFrames, bool bDelta )
{
	CByteBuf &out = *m_pOut;
	int nFirst = -1, nPrev = -1;

	for ( int b = 0; b < m_nNumBones; b++ )
	{
		const int r = nAnimIndex37 + b * V37_ANIM_SIZE;
		const int nFlags = m_In.Int( r );
		const bool bPosAnim = ( nFlags & V37_POS_ANIMATED ) != 0;
		const bool bRotAnim = ( nFlags & V37_ROT_ANIMATED ) != 0;
		const BoneInfo_t &bone = m_Bones[b];

		float pos[3], q[4];
		if ( !bPosAnim )
			for ( int j = 0; j < 3; j++ )
				pos[j] = m_In.Float( r + 4 + j * 4 );
		if ( !bRotAnim )
			for ( int j = 0; j < 4; j++ )
				q[j] = m_In.Float( r + 16 + j * 4 );

		int nOutFlags = bDelta ? MC_STUDIO_ANIM_DELTA : 0;
		bool bAnimForm = bPosAnim || bRotAnim;

		if ( !bAnimForm )
		{
			// A single pose. When it's the bone's own rest pose (or no change, for
			// a delta) the record needs no data at all.
			const float identity[4] = { 0, 0, 0, 1 };
			const float *pRestQ = bDelta ? identity : bone.m_Quat;
			bool bSamePos = true;
			for ( int j = 0; j < 3; j++ )
			{
				float flRest = bDelta ? 0.0f : bone.m_Value[j];
				if ( fabsf( pos[j] - flRest ) > 0.0005f )
					bSamePos = false;
			}
			bool bSameRot = QuaternionsMatch( q, pRestQ, 1e-7f );

			if ( !bSamePos || !bSameRot )
			{
				// Constants in the bone's own encoding are exact to its scale;
				// use them when they fit, otherwise raw values.
				bool bFits = true;
				float angles[3];
				QuaternionAngles( q, angles );
				for ( int j = 0; j < 6 && bFits; j++ )
				{
					float flBase = bDelta ? 0.0f : bone.m_Value[j];
					float flTarget = j < 3 ? pos[j] : angles[j - 3];
					float d = flTarget - flBase;
					if ( fabsf( d ) < 1e-6f )
						continue;
					if ( bone.m_Scale[j] == 0.0f || !( fabsf( d / bone.m_Scale[j] ) <= 32000.0f ) )	// also rejects NaN
						bFits = false;
					else if ( j < 3 && fabsf( (float)(int)floorf( d / bone.m_Scale[j] + 0.5f ) * bone.m_Scale[j] - d ) > 0.01f )
						bFits = false;
				}
				if ( bFits )
					bAnimForm = true;
				else
					nOutFlags |= MC_STUDIO_ANIM_RAWROT2 | MC_STUDIO_ANIM_RAWPOS;
			}
		}

		if ( bAnimForm )
			nOutFlags |= MC_STUDIO_ANIM_ANIMROT | MC_STUDIO_ANIM_ANIMPOS;

		// header: bone, flags, nextoffset
		int nRec = out.Alloc( 4, 4 );
		out.SetByte( nRec + 0, (unsigned char)b );
		out.SetByte( nRec + 1, (unsigned char)nOutFlags );
		if ( nPrev >= 0 )
			out.SetShort( nPrev + 2, (short)( nRec - nPrev ) );
		if ( nFirst < 0 )
			nFirst = nRec;
		nPrev = nRec;

		if ( nOutFlags & MC_STUDIO_ANIM_RAWROT2 )
		{
			// Quaternion64: x, y, z 21 bits each, w's sign in the top bit
			unsigned long long n = 0;
			float qn[4];
			float flLen = sqrtf( q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3] );
			for ( int j = 0; j < 4; j++ )
				qn[j] = flLen > 0 ? q[j] / flLen : ( j == 3 ? 1.0f : 0.0f );
			for ( int j = 0; j < 3; j++ )
			{
				float flBits = qn[j] * 1048576.5f + 1048576.0f;
				if ( !( flBits >= 0.0f ) ) flBits = 0.0f;			// also NaN
				if ( flBits > 2097151.0f ) flBits = 2097151.0f;
				int v = (int)floorf( flBits );
				if ( v < 0 ) v = 0;
				if ( v > 2097151 ) v = 2097151;
				n |= (unsigned long long)v << ( 21 * j );
			}
			if ( qn[3] < 0 )
				n |= 1ULL << 63;
			out.Append( &n, 8, 1 );

			// Vector48
			unsigned short v48[3];
			for ( int j = 0; j < 3; j++ )
				v48[j] = FloatToHalf( pos[j] );
			out.Append( v48, 6, 1 );
			continue;
		}

		if ( !( nOutFlags & MC_STUDIO_ANIM_ANIMROT ) )
			continue;	// rest pose, no data

		// two value pointers: rotation, then position
		int nRotV = out.Alloc( 6, 2 );
		int nPosV = out.Alloc( 6, 2 );

		for ( int j = 0; j < 6; j++ )
		{
			const int nPtr = ( j < 3 ) ? nRotV + j * 2 : nPosV + ( j - 3 ) * 2;
			const int nAxis = ( j < 3 ) ? 3 + j : j - 3;	// v37 order: x, y, z, xr, yr, zr
			const bool bAxisAnimated = ( nAxis < 3 ) ? bPosAnim : bRotAnim;
			int nStream = -1;

			if ( bAxisAnimated )
			{
				int nOfs = m_In.Int( r + 4 + nAxis * 4 );
				if ( !nOfs )
					continue;
				int nLen = AnimValueStreamLength( m_In, r + nOfs, nNumFrames );
				if ( nLen <= 0 )
				{
					Fail( "bad animation data" );
					continue;
				}
				nStream = out.Append( m_In.Ptr( r + nOfs ), nLen * 2, 2 );
			}
			else
			{
				// constant value in the bone's encoding
				float flTarget;
				if ( nAxis < 3 )
				{
					flTarget = pos[nAxis];
				}
				else
				{
					float angles[3];
					QuaternionAngles( q, angles );
					flTarget = angles[nAxis - 3];
				}
				float flBase = bDelta ? 0.0f : bone.m_Value[nAxis];
				float d = flTarget - flBase;
				if ( fabsf( d ) < 1e-6f || bone.m_Scale[nAxis] == 0.0f )
					continue;	// no stream: the value is the base
				float flSteps = d / bone.m_Scale[nAxis];
				if ( !( flSteps == flSteps ) ) flSteps = 0.0f;	// NaN
				if ( flSteps > 32767.0f ) flSteps = 32767.0f;
				if ( flSteps < -32768.0f ) flSteps = -32768.0f;
				nStream = WriteConstantStream( out, nNumFrames, (short)floorf( flSteps + 0.5f ) );
			}

			int nRel = nStream - ( ( j < 3 ) ? nRotV : nPosV );
			if ( nRel <= 0 || nRel > 32767 )
			{
				Fail( "animation too large" );
				continue;
			}
			out.SetShort( nPtr, (short)nRel );
		}
	}
	return nFirst;
}

void CConvert37::WriteAnimations()
{
	CByteBuf &out = *m_pOut;
	const int nAnims = H( H37_NUMANIM );
	const int nDescIndex = H( H37_ANIMDESCINDEX );
	if ( nAnims <= 0 || !m_In.InRange( nDescIndex, (long long)( nAnims ) * V37_ANIMDESC_SIZE ) )
		return;

	int nOut = out.Alloc( nAnims * V48_ANIMDESC_SIZE );
	out.SetInt( H48_NUMLOCALANIM, nAnims );
	out.SetInt( H48_LOCALANIMINDEX, nOut );
	m_AnimDescOfs.SetCount( nAnims );

	for ( int a = 0; a < nAnims; a++ )
	{
		const int s = nDescIndex + a * V37_ANIMDESC_SIZE;
		const int d = nOut + a * V48_ANIMDESC_SIZE;
		m_AnimDescOfs[a] = d;

		int nFrames = m_In.Int( s + 12 );
		if ( nFrames < 1 || nFrames > 65535 )
			nFrames = 1;
		const bool bDelta = m_AnimIsDelta[a] != 0;

		out.SetInt( d + 0, -d );												// baseptr
		m_Strings.Add( d + 4, d, m_In.String( s + m_In.Int( s ) ) );
		out.SetFloat( d + 8, m_In.Float( s + 4 ) );								// fps
		out.SetInt( d + 12, m_In.Int( s + 8 ) | ( bDelta ? MC_STUDIO_DELTA : 0 ) );	// flags
		out.SetInt( d + 16, nFrames );

		// movements
		int nMovements = m_In.Int( s + 16 );
		int nMovementIndex = s + m_In.Int( s + 20 );
		if ( nMovements > 0 && m_In.InRange( nMovementIndex, (long long)( nMovements ) * V37_MOVEMENT_SIZE ) )
		{
			int nMov = out.Append( m_In.Ptr( nMovementIndex ), nMovements * V48_MOVEMENT_SIZE, 4 );
			out.SetInt( d + 20, nMovements );
			out.SetInt( d + 24, nMov - d );
			// v37 leaves endframe 0; the engine finds the piece for a frame by
			// it, so a lone movement must end on the last frame
			if ( nMovements == 1 && out.GetInt( nMov ) == 0 )
				out.SetInt( nMov, nFrames - 1 );
		}

		// bone data
		int nAnimIndex = s + m_In.Int( s + 48 );
		if ( m_nNumBones > 0 && m_In.InRange( nAnimIndex, (long long)( m_nNumBones ) * V37_ANIM_SIZE ) )
		{
			int nFirst = WriteAnimRecords( nAnimIndex, nFrames, bDelta );
			out.SetInt( d + 52, 0 );		// animblock
			out.SetInt( d + 56, nFirst >= 0 ? nFirst - d : 0 );
		}

		// IK rules
		int nRules = m_In.Int( s + 52 );
		int nRuleIndex = s + m_In.Int( s + 56 );
		if ( nRules > 0 && nRules < 64 && m_In.InRange( nRuleIndex, (long long)( nRules ) * V37_IKRULE_SIZE ) )
		{
			int nRuleOut = out.Alloc( nRules * V48_IKRULE_SIZE );
			out.SetInt( d + 60, nRules );
			out.SetInt( d + 64, nRuleOut - d );
			for ( int k = 0; k < nRules; k++ )
			{
				const int rs = nRuleIndex + k * V37_IKRULE_SIZE;
				const int rd = nRuleOut + k * V48_IKRULE_SIZE;
				out.SetBytes( rd, m_In.Ptr( rs ), 60 );				// index .. q
				out.SetInt( rd + 68, m_In.Int( rs + 68 ) );			// iStart
				for ( int f = 0; f < 4; f++ )
					out.SetFloat( rd + 76 + f * 4, m_In.Float( rs + 76 + f * 4 ) );	// start, peak, tail, end
				out.SetFloat( rd + 96, m_In.Float( rs + 96 ) );		// contact

				// errors: one per frame from iStart through the end of influence
				int nErrorIndex = m_In.Int( rs + 72 );
				if ( nErrorIndex )
				{
					int nStart = m_In.Int( rs + 68 );
					float flEnd = m_In.Float( rs + 88 );
					double flLast = (double)flEnd * ( nFrames - 1 ) + 0.5;
					if ( !( flLast >= 0.0 ) ) flLast = 0.0;		// also catches NaN
					if ( flLast > nFrames ) flLast = nFrames;
					long long nCountL = (long long)flLast - nStart + 1;
					if ( nCountL < 1 ) nCountL = 1;
					if ( nCountL > nFrames ) nCountL = nFrames;
					int nCount = (int)nCountL;
					if ( m_In.InRange( rs + nErrorIndex, (long long)( nCount ) * V37_IKERROR_SIZE ) )
					{
						int nErr = out.Append( m_In.Ptr( rs + nErrorIndex ), nCount * V48_IKERROR_SIZE, 4 );
						out.SetInt( rd + 72, nErr - rd );
					}
				}
			}
		}
	}
}

//-----------------------------------------------------------------------------
// Sequences
//-----------------------------------------------------------------------------
void CConvert37::WriteSequences()
{
	CByteBuf &out = *m_pOut;
	const int nSeqs = H( H37_NUMSEQ );
	const int nSeqIndex = H( H37_SEQINDEX );
	const int nAnims = H( H37_NUMANIM );
	if ( nSeqs <= 0 || !m_In.InRange( nSeqIndex, (long long)( nSeqs ) * V37_SEQDESC_SIZE ) )
		return;

	int nOut = out.Alloc( nSeqs * V48_SEQDESC_SIZE );
	out.SetInt( H48_NUMLOCALSEQ, nSeqs );
	out.SetInt( H48_LOCALSEQINDEX, nOut );

	for ( int i = 0; i < nSeqs; i++ )
	{
		const int s = nSeqIndex + i * V37_SEQDESC_SIZE;
		const int d = nOut + i * V48_SEQDESC_SIZE;
		int f[47];
		for ( int k = 0; k < 47; k++ )
			f[k] = m_In.Int( s + k * 4 );

		out.SetInt( d + 0, -d );									// baseptr
		m_Strings.Add( d + 4, d, m_In.String( s + f[0] ) );		// label
		m_Strings.Add( d + 8, d, m_In.String( s + f[1] ) );		// activity name
		out.SetInt( d + 12, f[2] );								// flags
		out.SetInt( d + 16, f[3] );								// activity
		out.SetInt( d + 20, f[4] );								// actweight
		out.SetBytes( d + 32, m_In.Ptr( s + 28 ), 24 );			// bbmin, bbmax

		// events
		int nEvents = f[5];
		if ( nEvents > 0 && nEvents < 1024 && m_In.InRange( s + f[6], (long long)( nEvents ) * V37_EVENT_SIZE ) )
		{
			int nEv = out.Alloc( nEvents * V48_EVENT_SIZE );
			out.SetInt( d + 24, nEvents );
			out.SetInt( d + 28, nEv - d );
			for ( int k = 0; k < nEvents; k++ )
				out.SetBytes( nEv + k * V48_EVENT_SIZE, m_In.Ptr( s + f[6] + k * V37_EVENT_SIZE ), V37_EVENT_SIZE );
		}

		// blends: animation indexes
		int nGroup0 = f[16] > 0 ? f[16] : 1;
		int nGroup1 = f[17] > 0 ? f[17] : 1;
		if ( nGroup0 > 4096 || nGroup1 > 4096 || nGroup0 * nGroup1 > 4096 )
			nGroup0 = nGroup1 = 1;
		int nBlend = out.Alloc( nGroup0 * nGroup1 * 2, 4 );
		for ( int k = 0; k < nGroup0 * nGroup1; k++ )
		{
			int nAnim = f[14] ? (unsigned short)m_In.Short( s + f[14] + k * 2 ) : 0;
			if ( nAnim >= nAnims )
				nAnim = 0;
			out.SetShort( nBlend + k * 2, (short)nAnim );
		}
		out.SetInt( d + 56, f[13] );								// numblends
		out.SetInt( d + 60, nBlend - d );						// animindexindex
		out.SetInt( d + 68, nGroup0 );
		out.SetInt( d + 72, nGroup1 );
		for ( int k = 0; k < 7; k++ )
			out.SetInt( d + 76 + k * 4, f[18 + k] );			// paramindex[2], paramstart[2], paramend[2], paramparent
		out.SetInt( d + 104, f[25] );							// fadeintime
		out.SetInt( d + 108, f[26] );							// fadeouttime
		out.SetInt( d + 112, f[27] );							// localentrynode
		out.SetInt( d + 116, f[28] );							// localexitnode
		out.SetInt( d + 120, f[29] );							// nodeflags
		out.SetInt( d + 124, f[30] );							// entryphase
		out.SetInt( d + 128, f[31] );							// exitphase
		out.SetInt( d + 132, f[32] );							// lastframe
		out.SetInt( d + 136, f[33] );							// nextseq
		out.SetInt( d + 140, f[34] );							// pose
		out.SetInt( d + 144, f[35] );							// numikrules

		// autolayers
		int nLayers = f[36];
		if ( nLayers > 0 && nLayers < 64 && m_In.InRange( s + f[37], (long long)( nLayers ) * V37_AUTOLAYER_SIZE ) )
		{
			int nAl = out.Alloc( nLayers * V48_AUTOLAYER_SIZE );
			out.SetInt( d + 148, nLayers );
			out.SetInt( d + 152, nAl - d );
			for ( int k = 0; k < nLayers; k++ )
			{
				const int as = s + f[37] + k * V37_AUTOLAYER_SIZE;
				const int ad = nAl + k * V48_AUTOLAYER_SIZE;
				out.SetShort( ad + 0, (short)m_In.Int( as + 0 ) );	// iSequence
				out.SetShort( ad + 2, 0 );							// iPose
				out.SetBytes( ad + 4, m_In.Ptr( as + 4 ), 20 );		// flags, start, peak, tail, end
			}
		}

		// bone weights
		int nWeights = out.Alloc( m_nNumBones * 4 );
		for ( int k = 0; k < m_nNumBones; k++ )
		{
			float w = 1.0f;
			if ( f[38] && m_In.InRange( s + f[38] + k * 4, 4 ) )
				w = m_In.Float( s + f[38] + k * 4 );
			out.SetFloat( nWeights + k * 4, w );
		}
		out.SetInt( d + 156, nWeights - d );

		// pose keys
		if ( f[39] )
		{
			int nKeys = nGroup0 + nGroup1;
			if ( m_In.InRange( s + f[39], (long long)( nKeys ) * 4 ) )
			{
				int nPk = out.Append( m_In.Ptr( s + f[39] ), nKeys * 4, 4 );
				out.SetInt( d + 160, nPk - d );
			}
		}

		// IK locks
		int nLocks = f[40];
		if ( nLocks > 0 && nLocks < 64 && m_In.InRange( s + f[41], (long long)( nLocks ) * V37_IKLOCK_SIZE ) )
		{
			int nLk = out.Alloc( nLocks * V48_IKLOCK_SIZE );
			out.SetInt( d + 164, nLocks );
			out.SetInt( d + 168, nLk - d );
			for ( int k = 0; k < nLocks; k++ )
				out.SetBytes( nLk + k * V48_IKLOCK_SIZE, m_In.Ptr( s + f[41] + k * V37_IKLOCK_SIZE ), V37_IKLOCK_SIZE );
		}

		// key values
		int nKvSize = f[43];
		if ( nKvSize > 0 && m_In.InRange( s + f[42], nKvSize ) )
		{
			int nKv = out.Append( m_In.Ptr( s + f[42] ), nKvSize, 1 );
			out.SetByte( nKv + nKvSize - 1, 0 );
			out.SetInt( d + 172, nKv - d );
			out.SetInt( d + 176, nKvSize );
		}
	}

	// transition graph
	int nNodes = H( H37_NUMTRANSITIONS );
	int nTransIndex = H( H37_TRANSITIONINDEX );
	if ( nNodes > 0 && nNodes < 256 && m_In.InRange( nTransIndex, (long long)( nNodes ) * nNodes ) )
	{
		int nNames = out.Alloc( nNodes * 4 );
		int nTable = out.Append( m_In.Ptr( nTransIndex ), nNodes * nNodes, 1 );
		out.SetInt( H48_NUMLOCALNODES, nNodes );
		out.SetInt( H48_LOCALNODEINDEX, nTable );
		out.SetInt( H48_LOCALNODENAMEINDEX, nNames );
		for ( int k = 0; k < nNodes; k++ )
		{
			// v37 nodes have no names; give them unique ones
			snprintf( m_szNodeNames[k], sizeof( m_szNodeNames[k] ), "node%d", k + 1 );
			m_Strings.Add( nNames + k * 4, 0, m_szNodeNames[k] );
		}
	}

	// shared animation files -> include models
	int nGroups = H( H37_NUMSEQGROUPS );
	int nGroupIndex = H( H37_SEQGROUPINDEX );
	if ( nGroups > 1 && nGroups < 64 && m_In.InRange( nGroupIndex, (long long)( nGroups ) * V37_SEQGROUP_SIZE ) )
	{
		int nIncludes = 0;
		for ( int k = 1; k < nGroups; k++ )
			if ( m_In.String( nGroupIndex + k * V37_SEQGROUP_SIZE + m_In.Int( nGroupIndex + k * V37_SEQGROUP_SIZE + 4 ) )[0] )
				nIncludes++;
		if ( nIncludes )
		{
			int nInc = out.Alloc( nIncludes * V48_MODELGROUP_SIZE );
			out.SetInt( H48_NUMINCLUDEMODELS, nIncludes );
			out.SetInt( H48_INCLUDEMODELINDEX, nInc );
			int n = 0;
			for ( int k = 1; k < nGroups; k++ )
			{
				const int g = nGroupIndex + k * V37_SEQGROUP_SIZE;
				const char *pszFile = m_In.String( g + m_In.Int( g + 4 ) );
				if ( !pszFile[0] )
					continue;
				const int gd = nInc + n * V48_MODELGROUP_SIZE;
				m_Strings.Add( gd + 0, gd, m_In.String( g + m_In.Int( g ) ) );
				m_Strings.Add( gd + 4, gd, pszFile );
				n++;
			}
		}
	}
}

//-----------------------------------------------------------------------------
// Materials
//-----------------------------------------------------------------------------
void CConvert37::WriteTextures()
{
	CByteBuf &out = *m_pOut;

	int nTextures = H( H37_NUMTEXTURES );
	int nTexIndex = H( H37_TEXTUREINDEX );
	if ( nTextures > 0 && nTextures < 4096 && m_In.InRange( nTexIndex, (long long)( nTextures ) * V37_TEXTURE_SIZE ) )
	{
		int nOut = out.Alloc( nTextures * V48_TEXTURE_SIZE );
		out.SetInt( H48_NUMTEXTURES, nTextures );
		out.SetInt( H48_TEXTUREINDEX, nOut );
		for ( int i = 0; i < nTextures; i++ )
		{
			const int s = nTexIndex + i * V37_TEXTURE_SIZE;
			const int d = nOut + i * V48_TEXTURE_SIZE;
			m_Strings.Add( d + 0, d, m_In.String( s + m_In.Int( s ) ) );
			out.SetInt( d + 4, m_In.Int( s + 4 ) );	// flags
		}
	}

	int nCd = H( H37_NUMCDTEXTURES );
	int nCdIndex = H( H37_CDTEXTUREINDEX );
	if ( nCd > 0 && nCd < 256 && m_In.InRange( nCdIndex, (long long)( nCd ) * 4 ) )
	{
		int nOut = out.Alloc( nCd * 4 );
		out.SetInt( H48_NUMCDTEXTURES, nCd );
		out.SetInt( H48_CDTEXTUREINDEX, nOut );
		for ( int i = 0; i < nCd; i++ )
			m_Strings.Add( nOut + i * 4, 0, m_In.String( m_In.Int( nCdIndex + i * 4 ) ) );
	}

	int nSkinRef = H( H37_NUMSKINREF );
	int nFamilies = H( H37_NUMSKINFAMILIES );
	int nSkinIndex = H( H37_SKININDEX );
	if ( nSkinRef > 0 && nFamilies > 0 && nSkinRef < 65536 && nFamilies < 65536 && (long long)nSkinRef * nFamilies < 65536 && m_In.InRange( nSkinIndex, (long long)( nSkinRef ) * nFamilies * 2 ) )
	{
		int nOut = out.Append( m_In.Ptr( nSkinIndex ), nSkinRef * nFamilies * 2, 4 );
		out.SetInt( H48_NUMSKINREF, nSkinRef );
		out.SetInt( H48_NUMSKINFAMILIES, nFamilies );
		out.SetInt( H48_SKININDEX, nOut );
	}
}

//-----------------------------------------------------------------------------
// Geometry
//-----------------------------------------------------------------------------
void CConvert37::WriteBodyParts()
{
	CByteBuf &out = *m_pOut;
	int nParts = H( H37_NUMBODYPARTS );
	int nPartIndex = H( H37_BODYPARTINDEX );
	if ( nParts <= 0 || nParts > 256 || !m_In.InRange( nPartIndex, (long long)( nParts ) * V37_BODYPART_SIZE ) )
		return;

	int nOut = out.Alloc( nParts * V48_BODYPART_SIZE );
	out.SetInt( H48_NUMBODYPARTS, nParts );
	out.SetInt( H48_BODYPARTINDEX, nOut );

	for ( int p = 0; p < nParts; p++ )
	{
		const int ps = nPartIndex + p * V37_BODYPART_SIZE;
		const int pd = nOut + p * V48_BODYPART_SIZE;
		int nModels = m_In.Int( ps + 4 );
		int nModelIndex = ps + m_In.Int( ps + 12 );
		m_Strings.Add( pd + 0, pd, m_In.String( ps + m_In.Int( ps ) ) );
		out.SetInt( pd + 4, nModels );
		out.SetInt( pd + 8, m_In.Int( ps + 8 ) );	// base
		if ( nModels <= 0 || nModels > 256 || !m_In.InRange( nModelIndex, (long long)( nModels ) * V37_MODEL_SIZE ) )
		{
			out.SetInt( pd + 4, 0 );
			continue;
		}

		int nModelOut = out.Alloc( nModels * V48_MODEL_SIZE );
		out.SetInt( pd + 12, nModelOut - pd );

		for ( int m = 0; m < nModels; m++ )
		{
			const int ms = nModelIndex + m * V37_MODEL_SIZE;
			const int md = nModelOut + m * V48_MODEL_SIZE;
			int nMeshes = m_In.Int( ms + 72 );
			int nMeshIndex = ms + m_In.Int( ms + 76 );
			int nVerts = m_In.Int( ms + 80 );
			int nVertIndex = ms + m_In.Int( ms + 84 );
			int nTanIndex = m_In.Int( ms + 88 ) ? ms + m_In.Int( ms + 88 ) : 0;
			int nEyeballs = m_In.Int( ms + 100 );
			int nEyeIndex = ms + m_In.Int( ms + 104 );

			char szName[64];
			m_In.Bytes( ms, szName, 64 );
			szName[63] = 0;
			out.SetBytes( md, szName, 64 );
			out.SetInt( md + 64, m_In.Int( ms + 64 ) );		// type
			out.SetFloat( md + 68, m_In.Float( ms + 68 ) );	// boundingradius

			if ( nVerts < 0 || nVerts > 65536 * 4 || ( nVerts && !m_In.InRange( nVertIndex, (long long)( nVerts ) * V37_VERTEX_SIZE ) ) )
			{
				Fail( "bad vertex data" );
				nVerts = 0;
			}
			if ( nTanIndex && !m_In.InRange( nTanIndex, (long long)( nVerts ) * 16 ) )
				nTanIndex = 0;

			// vertices: into the vertex file
			const int nFirstVert = m_Verts.Count();
			m_ModelVertexStart.AddToTail( nFirstVert );
			m_Verts.SetCount( nFirstVert + nVerts );
			m_WeightRemap.SetCount( ( nFirstVert + nVerts ) * 4 );
			m_SlotBone.SetCount( ( nFirstVert + nVerts ) * 4 );
			for ( int v = 0; v < nVerts; v++ )
			{
				const int vs = nVertIndex + v * V37_VERTEX_SIZE;
				Vertex_t &vert = m_Verts[nFirstVert + v];
				signed char *pRemap = &m_WeightRemap[( nFirstVert + v ) * 4];

				float w[4];
				int bone[4];
				for ( int k = 0; k < 4; k++ )
				{
					w[k] = m_In.Float( vs + k * 4 );
					bone[k] = m_In.Short( vs + 16 + k * 2 );
				}
				int nNumBones = m_In.Short( vs + 24 );
				if ( nNumBones < 0 ) nNumBones = 0;
				if ( nNumBones > 4 ) nNumBones = 4;
				for ( int k = 0; k < 4; k++ )
					m_SlotBone[( nFirstVert + v ) * 4 + k] = (short)( ( bone[k] >= 0 && bone[k] < m_nNumBones ) ? bone[k] : 0 );

				// keep the three largest weights
				int slots[4] = { 0, 1, 2, 3 };
				for ( int a = 0; a < nNumBones; a++ )
					for ( int b = a + 1; b < nNumBones; b++ )
						if ( w[slots[b]] > w[slots[a]] ) { int t = slots[a]; slots[a] = slots[b]; slots[b] = t; }
				int nKeep = nNumBones > 3 ? 3 : nNumBones;
				for ( int k = 0; k < 4; k++ )
					pRemap[k] = -1;

				// kept weights stay in their original relative order
				int nOutSlot = 0;
				float flSum = 0.0f;
				for ( int k = 0; k < nNumBones; k++ )
				{
					bool bKept = false;
					for ( int a = 0; a < nKeep; a++ )
						if ( slots[a] == k ) bKept = true;
					if ( !bKept )
						continue;
					pRemap[k] = (signed char)nOutSlot;
					vert.m_Weight[nOutSlot] = w[k];
					vert.m_Bone[nOutSlot] = (char)( ( bone[k] >= 0 && bone[k] < m_nNumBones ) ? bone[k] : 0 );
					flSum += w[k];
					nOutSlot++;
				}
				vert.m_NumBones = (unsigned char)nOutSlot;
				if ( nOutSlot && flSum > 0.0f )
					for ( int k = 0; k < nOutSlot; k++ )
						vert.m_Weight[k] /= flSum;
				if ( !nOutSlot )
				{
					vert.m_NumBones = 1;
					vert.m_Weight[0] = 1.0f;
				}

				for ( int k = 0; k < 3; k++ )
				{
					vert.m_Pos[k] = m_In.Float( vs + 32 + k * 4 );
					vert.m_Normal[k] = m_In.Float( vs + 44 + k * 4 );
				}
				vert.m_UV[0] = m_In.Float( vs + 56 );
				vert.m_UV[1] = m_In.Float( vs + 60 );
				if ( nTanIndex )
				{
					for ( int k = 0; k < 4; k++ )
						vert.m_Tangent[k] = m_In.Float( nTanIndex + v * 16 + k * 4 );
					vert.m_bHasTangent = true;
					m_bAnyTangents = true;
				}
			}

			out.SetInt( md + 72, nMeshes );
			out.SetInt( md + 80, nVerts );
			out.SetInt( md + 84, nFirstVert * VVD_VERTEX_SIZE );		// vertexindex: bytes into the vertex block
			out.SetInt( md + 88, nFirstVert * VVD_TANGENT_SIZE );		// tangentsindex

			// meshes
			m_ModelMeshStart.AddToTail( m_MeshVertexOffset.Count() );
			if ( nMeshes < 0 || nMeshes > 1024 || !m_In.InRange( nMeshIndex, (long long)( nMeshes ) * V37_MESH_SIZE ) )
			{
				out.SetInt( md + 72, 0 );
				nMeshes = 0;
			}
			int nMeshOut = nMeshes ? out.Alloc( nMeshes * V48_MESH_SIZE ) : 0;
			out.SetInt( md + 76, nMeshes ? nMeshOut - md : 0 );
			for ( int e = 0; e < nMeshes; e++ )
			{
				const int es = nMeshIndex + e * V37_MESH_SIZE;
				const int ed = nMeshOut + e * V48_MESH_SIZE;
				int nMeshVerts = m_In.Int( es + 8 );
				int nMeshVertOfs = m_In.Int( es + 12 );
				m_MeshVertexOffset.AddToTail( nMeshVertOfs );

				out.SetInt( ed + 0, m_In.Int( es + 0 ) );		// material
				out.SetInt( ed + 4, md - ed );					// modelindex
				out.SetInt( ed + 8, nMeshVerts );
				out.SetInt( ed + 12, nMeshVertOfs );
				out.SetInt( ed + 24, m_In.Int( es + 24 ) );	// materialtype
				out.SetInt( ed + 28, m_In.Int( es + 28 ) );	// materialparam
				// meshid: studiorender indexes its per-mesh data with it, so it
				// must number the meshes 0, 1, 2... across the whole model, as
				// studiomdl does. v37 files leave it unset.
				out.SetInt( ed + 32, m_MeshVertexOffset.Count() - 1 );
				out.SetBytes( ed + 36, m_In.Ptr( es + 36 ), 12 );	// center
				for ( int l = 0; l < MC_MAX_LODS; l++ )
					out.SetInt( ed + 52 + l * 4, nMeshVerts );		// vertexdata.numLODVertexes

				// flexes
				int nFlexes = m_In.Int( es + 16 );
				int nFlexIndex = es + m_In.Int( es + 20 );
				if ( nFlexes > 0 && nFlexes < 1024 && m_In.InRange( nFlexIndex, (long long)( nFlexes ) * V37_FLEX_SIZE ) )
				{
					int nFlexOut = out.Alloc( nFlexes * V48_FLEX_SIZE );
					out.SetInt( ed + 16, nFlexes );
					out.SetInt( ed + 20, nFlexOut - ed );
					for ( int x = 0; x < nFlexes; x++ )
					{
						const int fs = nFlexIndex + x * V37_FLEX_SIZE;
						const int fd = nFlexOut + x * V48_FLEX_SIZE;
						out.SetBytes( fd, m_In.Ptr( fs ), 20 );		// flexdesc, target0-3
						int nFlexVerts = m_In.Int( fs + 20 );
						int nVaIndex = fs + m_In.Int( fs + 24 );
						if ( nFlexVerts <= 0 || nFlexVerts > 65535 || !m_In.InRange( nVaIndex, (long long)( nFlexVerts ) * V37_VERTANIM_SIZE ) )
							continue;
						int nVaOut = out.Alloc( nFlexVerts * V48_VERTANIM_SIZE );
						out.SetInt( fd + 20, nFlexVerts );
						out.SetInt( fd + 24, nVaOut - fd );
						for ( int y = 0; y < nFlexVerts; y++ )
						{
							const int vs = nVaIndex + y * V37_VERTANIM_SIZE;
							const int vd = nVaOut + y * V48_VERTANIM_SIZE;
							out.SetShort( vd + 0, (short)m_In.Int( vs ) );
							out.SetByte( vd + 2, 255 );	// speed
							out.SetByte( vd + 3, 0 );	// side
							for ( int k = 0; k < 3; k++ )
							{
								out.SetShort( vd + 4 + k * 2, (short)FloatToHalf( m_In.Float( vs + 4 + k * 4 ) ) );
								out.SetShort( vd + 10 + k * 2, (short)FloatToHalf( m_In.Float( vs + 16 + k * 4 ) ) );
							}
						}
					}
				}
			}

			// eyeballs
			if ( nEyeballs > 0 && nEyeballs < 16 && m_In.InRange( nEyeIndex, (long long)( nEyeballs ) * V37_EYEBALL_SIZE ) )
			{
				int nEyeOut = out.Alloc( nEyeballs * V48_EYEBALL_SIZE );
				out.SetInt( md + 100, nEyeballs );
				out.SetInt( md + 104, nEyeOut - md );
				for ( int e = 0; e < nEyeballs; e++ )
				{
					const int es = nEyeIndex + e * V37_EYEBALL_SIZE;
					const int ed = nEyeOut + e * V48_EYEBALL_SIZE;
					m_Strings.Add( ed + 0, ed, m_In.String( es + m_In.Int( es ) ) );
					out.SetBytes( ed + 4, m_In.Ptr( es + 4 ), 48 );		// bone .. forward
					// texture: the material of the eye mesh that draws this eyeball
					// (what studiomdl stores; v37 leaves it 0)
					int nTexture = m_In.Int( es + 52 );
					for ( int x = 0; x < nMeshes; x++ )
					{
						const int xs = nMeshIndex + x * V37_MESH_SIZE;
						if ( m_In.Int( xs + 24 ) == 1 && m_In.Int( xs + 28 ) == e )
						{
							nTexture = m_In.Int( xs + 0 );
							break;
						}
					}
					out.SetInt( ed + 52, nTexture );
					out.SetFloat( ed + 60, m_In.Float( es + 60 ) );		// iris_scale
					out.SetBytes( ed + 68, m_In.Ptr( es + 68 ), 56 );	// upper/lower flexdesc, targets, lid flexdescs
				}
			}
		}
	}
}

void CConvert37::WriteVertexFile( CByteBuf &vvd, int nVVDLODs )
{
	const int nVerts = m_Verts.Count();
	if ( nVVDLODs < 1 ) nVVDLODs = 1;
	if ( nVVDLODs > MC_MAX_LODS ) nVVDLODs = MC_MAX_LODS;

	int nHeader = vvd.Alloc( VVD_HEADER_SIZE );
	vvd.SetInt( nHeader + 0, MC_IDSV );
	vvd.SetInt( nHeader + 4, 4 );
	vvd.SetInt( nHeader + 8, m_pOut->GetInt( H48_CHECKSUM ) );
	vvd.SetInt( nHeader + 12, nVVDLODs );
	for ( int l = 0; l < MC_MAX_LODS; l++ )
		vvd.SetInt( nHeader + 16 + l * 4, nVerts );
	vvd.SetInt( nHeader + 48, 0 );	// numFixups
	vvd.SetInt( nHeader + 52, 0 );	// fixupTableStart

	int nVertStart = vvd.Alloc( nVerts * VVD_VERTEX_SIZE, 16 );
	vvd.SetInt( nHeader + 52, nVertStart );	// fixup table (empty) where the vertices begin
	vvd.SetInt( nHeader + 56, nVertStart );
	for ( int v = 0; v < nVerts; v++ )
	{
		const Vertex_t &vert = m_Verts[v];
		const int d = nVertStart + v * VVD_VERTEX_SIZE;
		for ( int k = 0; k < 3; k++ )
		{
			vvd.SetFloat( d + k * 4, k < vert.m_NumBones ? vert.m_Weight[k] : 0.0f );
			vvd.SetByte( d + 12 + k, (unsigned char)( k < vert.m_NumBones ? vert.m_Bone[k] : 0 ) );
		}
		vvd.SetByte( d + 15, vert.m_NumBones );
		vvd.SetBytes( d + 16, vert.m_Pos, 12 );
		vvd.SetBytes( d + 28, vert.m_Normal, 12 );
		vvd.SetBytes( d + 40, vert.m_UV, 8 );
	}

	int nTanStart = vvd.Alloc( nVerts * VVD_TANGENT_SIZE, 16 );
	vvd.SetInt( nHeader + 60, nTanStart );
	for ( int v = 0; v < nVerts; v++ )
	{
		const Vertex_t &vert = m_Verts[v];
		float t[4] = { 1, 0, 0, 1 };
		if ( vert.m_bHasTangent )
			memcpy( t, vert.m_Tangent, 16 );
		vvd.SetBytes( nTanStart + v * VVD_TANGENT_SIZE, t, 16 );
	}
}

//-----------------------------------------------------------------------------
// Attachments, flex controllers, IK, the rest
//-----------------------------------------------------------------------------
void CConvert37::WriteAttachments()
{
	CByteBuf &out = *m_pOut;
	int nCount = H( H37_NUMATTACHMENTS );
	int nIn = H( H37_ATTACHMENTINDEX );
	if ( nCount <= 0 || nCount > 256 || !m_In.InRange( nIn, (long long)( nCount ) * V37_ATTACHMENT_SIZE ) )
		return;
	int nOut = out.Alloc( nCount * V48_ATTACHMENT_SIZE );
	out.SetInt( H48_NUMLOCALATTACHMENTS, nCount );
	out.SetInt( H48_LOCALATTACHMENTINDEX, nOut );
	for ( int i = 0; i < nCount; i++ )
	{
		const int s = nIn + i * V37_ATTACHMENT_SIZE;
		const int d = nOut + i * V48_ATTACHMENT_SIZE;
		m_Strings.Add( d, d, m_In.String( s + m_In.Int( s ) ) );
		out.SetInt( d + 4, 0 );									// flags
		out.SetInt( d + 8, m_In.Int( s + 8 ) );					// bone
		out.SetBytes( d + 12, m_In.Ptr( s + 12 ), 48 );			// local
	}
}

void CConvert37::WriteFlexes()
{
	CByteBuf &out = *m_pOut;

	int nDesc = H( H37_NUMFLEXDESC );
	int nDescIndex = H( H37_FLEXDESCINDEX );
	if ( nDesc > 0 && nDesc < 4096 && m_In.InRange( nDescIndex, (long long)( nDesc ) * V37_FLEXDESC_SIZE ) )
	{
		int nOut = out.Alloc( nDesc * V48_FLEXDESC_SIZE );
		out.SetInt( H48_NUMFLEXDESC, nDesc );
		out.SetInt( H48_FLEXDESCINDEX, nOut );
		for ( int i = 0; i < nDesc; i++ )
		{
			const int s = nDescIndex + i * V37_FLEXDESC_SIZE;
			const int d = nOut + i * V48_FLEXDESC_SIZE;
			m_Strings.Add( d, d, m_In.String( s + m_In.Int( s ) ) );
		}
	}

	int nCtrl = H( H37_NUMFLEXCONTROLLERS );
	int nCtrlIndex = H( H37_FLEXCONTROLLERINDEX );
	if ( nCtrl > 0 && nCtrl < 4096 && m_In.InRange( nCtrlIndex, (long long)( nCtrl ) * V37_FLEXCONTROLLER_SIZE ) )
	{
		int nOut = out.Alloc( nCtrl * V48_FLEXCONTROLLER_SIZE );
		out.SetInt( H48_NUMFLEXCONTROLLERS, nCtrl );
		out.SetInt( H48_FLEXCONTROLLERINDEX, nOut );
		for ( int i = 0; i < nCtrl; i++ )
		{
			const int s = nCtrlIndex + i * V37_FLEXCONTROLLER_SIZE;
			const int d = nOut + i * V48_FLEXCONTROLLER_SIZE;
			m_Strings.Add( d + 0, d, m_In.String( s + m_In.Int( s + 0 ) ) );
			m_Strings.Add( d + 4, d, m_In.String( s + m_In.Int( s + 4 ) ) );
			out.SetInt( d + 8, m_In.Int( s + 8 ) );
			out.SetFloat( d + 12, m_In.Float( s + 12 ) );
			out.SetFloat( d + 16, m_In.Float( s + 16 ) );
		}
	}

	int nRules = H( H37_NUMFLEXRULES );
	int nRuleIndex = H( H37_FLEXRULEINDEX );
	if ( nRules > 0 && nRules < 4096 && m_In.InRange( nRuleIndex, (long long)( nRules ) * V37_FLEXRULE_SIZE ) )
	{
		int nOut = out.Alloc( nRules * V48_FLEXRULE_SIZE );
		out.SetInt( H48_NUMFLEXRULES, nRules );
		out.SetInt( H48_FLEXRULEINDEX, nOut );
		for ( int i = 0; i < nRules; i++ )
		{
			const int s = nRuleIndex + i * V37_FLEXRULE_SIZE;
			const int d = nOut + i * V48_FLEXRULE_SIZE;
			out.SetInt( d, m_In.Int( s ) );
			int nOps = m_In.Int( s + 4 );
			int nOpIndex = s + m_In.Int( s + 8 );
			if ( nOps > 0 && nOps < 4096 && m_In.InRange( nOpIndex, (long long)( nOps ) * V37_FLEXOP_SIZE ) )
			{
				int nOpOut = out.Append( m_In.Ptr( nOpIndex ), nOps * V48_FLEXOP_SIZE, 4 );
				out.SetInt( d + 4, nOps );
				out.SetInt( d + 8, nOpOut - d );
			}
		}
	}

	int nMouths = H( H37_NUMMOUTHS );
	int nMouthIndex = H( H37_MOUTHINDEX );
	if ( nMouths > 0 && nMouths < 16 && m_In.InRange( nMouthIndex, (long long)( nMouths ) * V37_MOUTH_SIZE ) )
	{
		int nOut = out.Append( m_In.Ptr( nMouthIndex ), nMouths * V48_MOUTH_SIZE, 4 );
		out.SetInt( H48_NUMMOUTHS, nMouths );
		out.SetInt( H48_MOUTHINDEX, nOut );
	}
}

void CConvert37::WriteIK()
{
	CByteBuf &out = *m_pOut;

	int nChains = H( H37_NUMIKCHAINS );
	int nChainIndex = H( H37_IKCHAININDEX );
	if ( nChains > 0 && nChains < 64 && m_In.InRange( nChainIndex, (long long)( nChains ) * V37_IKCHAIN_SIZE ) )
	{
		int nOut = out.Alloc( nChains * V48_IKCHAIN_SIZE );
		out.SetInt( H48_NUMIKCHAINS, nChains );
		out.SetInt( H48_IKCHAININDEX, nOut );
		for ( int i = 0; i < nChains; i++ )
		{
			const int s = nChainIndex + i * V37_IKCHAIN_SIZE;
			const int d = nOut + i * V48_IKCHAIN_SIZE;
			m_Strings.Add( d, d, m_In.String( s + m_In.Int( s ) ) );
			out.SetInt( d + 4, m_In.Int( s + 4 ) );	// linktype
			int nLinks = m_In.Int( s + 8 );
			int nLinkIndex = s + m_In.Int( s + 12 );
			if ( nLinks > 0 && nLinks < 16 && m_In.InRange( nLinkIndex, (long long)( nLinks ) * V37_IKLINK_SIZE ) )
			{
				int nLinkOut = out.Alloc( nLinks * V48_IKLINK_SIZE );
				out.SetInt( d + 8, nLinks );
				out.SetInt( d + 12, nLinkOut - d );
				for ( int k = 0; k < nLinks; k++ )
					out.SetInt( nLinkOut + k * V48_IKLINK_SIZE, m_In.Int( nLinkIndex + k * V37_IKLINK_SIZE ) );	// bone; knee direction left for the engine to work out
			}
		}
	}

	int nLocks = H( H37_NUMIKAUTOPLAYLOCKS );
	int nLockIndex = H( H37_IKAUTOPLAYLOCKINDEX );
	if ( nLocks > 0 && nLocks < 64 && m_In.InRange( nLockIndex, (long long)( nLocks ) * V37_IKLOCK_SIZE ) )
	{
		int nOut = out.Alloc( nLocks * V48_IKLOCK_SIZE );
		out.SetInt( H48_NUMLOCALIKAUTOPLAYLOCKS, nLocks );
		out.SetInt( H48_LOCALIKAUTOPLAYLOCKINDEX, nOut );
		for ( int i = 0; i < nLocks; i++ )
			out.SetBytes( nOut + i * V48_IKLOCK_SIZE, m_In.Ptr( nLockIndex + i * V37_IKLOCK_SIZE ), V37_IKLOCK_SIZE );
	}
}

void CConvert37::WriteMisc()
{
	CByteBuf &out = *m_pOut;

	int nParams = H( H37_NUMPOSEPARAMETERS );
	int nParamIndex = H( H37_POSEPARAMINDEX );
	if ( nParams > 0 && nParams < 64 && m_In.InRange( nParamIndex, (long long)( nParams ) * V37_POSEPARAM_SIZE ) )
	{
		int nOut = out.Alloc( nParams * V48_POSEPARAM_SIZE );
		out.SetInt( H48_NUMLOCALPOSEPARAMETERS, nParams );
		out.SetInt( H48_LOCALPOSEPARAMINDEX, nOut );
		for ( int i = 0; i < nParams; i++ )
		{
			const int s = nParamIndex + i * V37_POSEPARAM_SIZE;
			const int d = nOut + i * V48_POSEPARAM_SIZE;
			m_Strings.Add( d, d, m_In.String( s + m_In.Int( s ) ) );
			out.SetBytes( d + 4, m_In.Ptr( s + 4 ), 16 );	// flags, start, end, loop
		}
	}

	int nKvSize = H( H37_KEYVALUESIZE );
	int nKvIndex = H( H37_KEYVALUEINDEX );
	if ( nKvSize > 0 && m_In.InRange( nKvIndex, nKvSize ) )
	{
		int nOut = out.Append( m_In.Ptr( nKvIndex ), nKvSize, 1 );
		out.SetByte( nOut + nKvSize - 1, 0 );
		out.SetInt( H48_KEYVALUEINDEX, nOut );
		out.SetInt( H48_KEYVALUESIZE, nKvSize );
	}

	m_Strings.Add( H48_SURFACEPROPINDEX, 0, m_In.String( H( H37_SURFACEPROPINDEX ) ) );
}

//-----------------------------------------------------------------------------
bool CConvert37::Convert( CByteBuf &mdl, CByteBuf &vvd, int nVVDLODs, char *pszError, int nErrorSize )
{
	m_pOut = &mdl;
	m_pszFirstError = NULL;
	m_bAnyTangents = false;

	if ( m_In.Size() < V37_HEADER_SIZE || m_In.Int( 0 ) != MC_IDST || m_In.Int( 4 ) != 37 )
	{
		snprintf( pszError, nErrorSize, "not a version 37 model" );
		return false;
	}

	m_nNumBones = H( H37_NUMBONES );
	if ( m_nNumBones < 0 || m_nNumBones > 255 || !m_In.InRange( H( H37_BONEINDEX ), (long long)( m_nNumBones ) * V37_BONE_SIZE ) )
	{
		snprintf( pszError, nErrorSize, "bad bone count (%d)", m_nNumBones );
		return false;
	}
	m_Bones.SetCount( m_nNumBones );

	// header
	mdl.Alloc( V48_HEADER_SIZE );
	mdl.SetInt( H48_ID, MC_IDST );
	mdl.SetInt( H48_VERSION, 48 );
	mdl.SetInt( H48_CHECKSUM, m_In.Int( 8 ) );
	char szName[64];
	m_In.Bytes( 12, szName, 64 );
	szName[63] = 0;
	mdl.SetBytes( H48_NAME, szName, 64 );
	mdl.SetBytes( H48_EYEPOSITION, m_In.Ptr( 80 ), 72 );			// eye, illum, hull, view bbox
	mdl.SetInt( H48_FLAGS, H( H37_FLAGS ) & 0x1F );
	mdl.SetFloat( H48_MASS, m_In.Float( 80 + 72 + H37_MASS * 4 ) );
	mdl.SetInt( H48_CONTENTS, H( H37_CONTENTS ) );

	int nHdr2 = mdl.Alloc( V48_HEADER2_SIZE );
	mdl.SetInt( H48_STUDIOHDR2INDEX, nHdr2 );
	m_Strings.Add( nHdr2 + 20, nHdr2, szName );						// sznameindex

	// which animations are deltas (the flag can be on the animation or on a
	// sequence that plays it)
	const int nAnims = H( H37_NUMANIM );
	if ( nAnims < 0 || nAnims > 65535 )
	{
		snprintf( pszError, nErrorSize, "bad animation count" );
		return false;
	}
	m_AnimIsDelta.SetCount( nAnims );
	for ( int a = 0; a < nAnims; a++ )
		m_AnimIsDelta[a] = ( m_In.Int( H( H37_ANIMDESCINDEX ) + a * V37_ANIMDESC_SIZE + 8 ) & MC_STUDIO_DELTA ) ? 1 : 0;
	for ( int s = 0; s < H( H37_NUMSEQ ) && s < 65536; s++ )
	{
		const int sq = H( H37_SEQINDEX ) + s * V37_SEQDESC_SIZE;
		if ( !( m_In.Int( sq + 8 ) & MC_STUDIO_DELTA ) )
			continue;
		long long nBlends = (long long)m_In.Int( sq + 64 ) * m_In.Int( sq + 68 );
		int nBlendIndex = m_In.Int( sq + 56 );
		for ( int k = 0; k < nBlends && k < 4096 && nBlendIndex; k++ )
		{
			int nAnim = (unsigned short)m_In.Short( sq + nBlendIndex + k * 2 );
			if ( nAnim < nAnims )
				m_AnimIsDelta[nAnim] = 1;
		}
	}

	WriteBones();
	WriteBoneControllers();
	WriteHitboxes();
	PrepareAnimScales();
	WriteAnimations();
	FinishBones( mdl, m_Bones );
	WriteSequences();
	WriteTextures();
	WriteBodyParts();
	WriteAttachments();
	WriteFlexes();
	WriteIK();
	WriteMisc();

	m_Strings.Write( mdl );
	mdl.Alloc( 0, 16 );
	mdl.SetInt( H48_LENGTH, mdl.Size() );

	if ( m_In.Error() )
	{
		snprintf( pszError, nErrorSize, "%s", m_pszFirstError ? m_pszFirstError : "the file is damaged" );
		return false;
	}

	WriteVertexFile( vvd, nVVDLODs );
	return true;
}

//-----------------------------------------------------------------------------
// VTX v6 -> v7. Only Vertex_t changed: v6 has four bone slots (with short
// bone IDs), v7 three. The vertices are rewritten in place, so every other
// offset in the file stays valid.
//-----------------------------------------------------------------------------
enum
{
	VTX6_VERTEX_SIZE = 15,
	VTX7_VERTEX_SIZE = 9,
	VTX_HEADER_SIZE = 36,
	VTX_BODYPART_SIZE = 8,
	VTX_MODEL_SIZE = 8,
	VTX_LOD_SIZE = 12,
	VTX_MESH_SIZE = 9,
	VTX_STRIPGROUP_SIZE = 25,
	VTX_STRIP_SIZE = 27,
};

struct Vtx6Vertex_t
{
	unsigned char	m_WeightIndex[4];
	short			m_BoneID[4];
	unsigned short	m_nOrig;
	int				m_nNumBones;
	int				m_nVert;		// model vertex, -1 if out of range
};

static bool ConvertVTX6( const unsigned char *pData, int nSize, const CConvert37 &model, CByteBuf &out, char *pszError, int nErrorSize )
{
	CReader in( pData, nSize );
	if ( nSize < VTX_HEADER_SIZE || in.Int( 0 ) != 6 )
	{
		snprintf( pszError, nErrorSize, "not a version 6 VTX" );
		return false;
	}
	out.Append( pData, nSize );
	out.SetInt( 0, 7 );
	if ( in.Int( 8 + 4 ) > 3 || in.Int( 8 + 4 ) < 1 )	// maxBonesPerVert (ushort, ushort, int after vertCacheSize)
		out.SetInt( 12, 3 );

	const int nLODs = in.Int( 20 );
	const int nParts = in.Int( 28 );
	const int nPartIndex = in.Int( 32 );
	if ( nParts < 0 || nParts > 256 || !in.InRange( nPartIndex, (long long)( nParts ) * VTX_BODYPART_SIZE ) )
	{
		snprintf( pszError, nErrorSize, "bad VTX body parts" );
		return false;
	}

	int nModelOrdinal = 0;
	for ( int p = 0; p < nParts; p++ )
	{
		const int bp = nPartIndex + p * VTX_BODYPART_SIZE;
		const int nModels = in.Int( bp );
		const int nModelIndex = bp + in.Int( bp + 4 );
		if ( nModels < 0 || nModels > 256 || !in.InRange( nModelIndex, (long long)( nModels ) * VTX_MODEL_SIZE ) )
		{
			snprintf( pszError, nErrorSize, "bad VTX models" );
			return false;
		}
		for ( int m = 0; m < nModels; m++, nModelOrdinal++ )
		{
			const int mo = nModelIndex + m * VTX_MODEL_SIZE;
			const int nModelLODs = in.Int( mo );
			const int nLodIndex = mo + in.Int( mo + 4 );
			if ( nModelOrdinal >= model.m_ModelVertexStart.Count() )
			{
				snprintf( pszError, nErrorSize, "VTX doesn't match the model" );
				return false;
			}
			const int nModelFirstVert = model.m_ModelVertexStart[nModelOrdinal];
			const int nModelFirstMesh = model.m_ModelMeshStart[nModelOrdinal];
			const int nModelVerts = ( nModelOrdinal + 1 < model.m_ModelVertexStart.Count() ? model.m_ModelVertexStart[nModelOrdinal + 1] : model.m_WeightRemap.Count() / 4 ) - nModelFirstVert;
			const int nModelMeshes = ( nModelOrdinal + 1 < model.m_ModelMeshStart.Count() ? model.m_ModelMeshStart[nModelOrdinal + 1] : model.m_MeshVertexOffset.Count() ) - nModelFirstMesh;

			if ( nModelLODs < 0 || nModelLODs > 8 || !in.InRange( nLodIndex, (long long)( nModelLODs ) * VTX_LOD_SIZE ) )
				continue;
			for ( int l = 0; l < nModelLODs; l++ )
			{
				const int lo = nLodIndex + l * VTX_LOD_SIZE;
				const int nMeshes = in.Int( lo );
				const int nMeshIndex = lo + in.Int( lo + 4 );
				if ( nMeshes < 0 || nMeshes > 1024 || !in.InRange( nMeshIndex, (long long)( nMeshes ) * VTX_MESH_SIZE ) )
					continue;
				for ( int e = 0; e < nMeshes; e++ )
				{
					const int me = nMeshIndex + e * VTX_MESH_SIZE;
					const int nGroups = in.Int( me );
					const int nGroupIndex = me + in.Int( me + 4 );
					const int nMeshVertOfs = e < nModelMeshes ? model.m_MeshVertexOffset[nModelFirstMesh + e] : 0;
					if ( nGroups < 0 || nGroups > 64 || !in.InRange( nGroupIndex, (long long)( nGroups ) * VTX_STRIPGROUP_SIZE ) )
						continue;
					for ( int g = 0; g < nGroups; g++ )
					{
						const int sg = nGroupIndex + g * VTX_STRIPGROUP_SIZE;
						const int nVerts = in.Int( sg );
						const int nVertIndex = sg + in.Int( sg + 4 );
						if ( nVerts <= 0 || !in.InRange( nVertIndex, (long long)( nVerts ) * VTX6_VERTEX_SIZE ) )
							continue;
						// The v7 vertex must use exactly the weights the .vvd kept for its
						// mesh vertex (studiorender sums them and expects 1). Their hardware
						// bone IDs come from the bones the strip already uses: its bone state
						// changes and what its v6 vertices referred to.
						const int nStrips = in.Int( sg + 16 );
						const int nStripIndex = sg + in.Int( sg + 20 );
						const bool bStripsOK = nStrips > 0 && nStrips < 4096 && in.InRange( nStripIndex, (long long)( nStrips ) * VTX_STRIP_SIZE );

						// v6 vertices are read before any v7 one is written over them
						CPodArray< Vtx6Vertex_t > src;
						src.SetCount( nVerts );
						for ( int v = 0; v < nVerts; v++ )
						{
							const int vs = nVertIndex + v * VTX6_VERTEX_SIZE;
							Vtx6Vertex_t &sv = src[v];
							for ( int k = 0; k < 4; k++ )
							{
								sv.m_WeightIndex[k] = in.Byte( vs + k );
								sv.m_BoneID[k] = in.Short( vs + 4 + k * 2 );
							}
							sv.m_nOrig = (unsigned short)in.Short( vs + 12 );
							sv.m_nNumBones = in.Byte( vs + 14 );
							if ( sv.m_nNumBones > 4 ) sv.m_nNumBones = 4;
							const long long nLocal = (long long)nMeshVertOfs + sv.m_nOrig;
							const long long nVert = nModelFirstVert + nLocal;
							sv.m_nVert = ( nLocal >= 0 && nLocal < nModelVerts && nVert >= 0 && nVert * 4 + 3 < model.m_WeightRemap.Count() ) ? (int)nVert : -1;
						}

						const int nRanges = bStripsOK ? nStrips : 1;
						for ( int r = 0; r < nRanges; r++ )
						{
							int nFirst = 0, nCount = nVerts;
							int nChanges = 0, nChangeIndex = 0;
							if ( bStripsOK )
							{
								const int st = nStripIndex + r * VTX_STRIP_SIZE;
								nFirst = in.Int( st + 12 );
								nCount = in.Int( st + 8 );
								nChanges = in.Int( st + 19 );
								nChangeIndex = st + in.Int( st + 23 );
								if ( nFirst < 0 || nCount <= 0 || nFirst > nVerts || nCount > nVerts - nFirst )
									continue;
								if ( nChanges < 0 || nChanges > 512 || !in.InRange( nChangeIndex, (long long)( nChanges ) * 8 ) )
									nChanges = 0;
							}

							// global bone -> hardware ID for this strip
							short palette[256];
							for ( int b = 0; b < 256; b++ )
								palette[b] = -1;
							for ( int c = 0; c < nChanges; c++ )
							{
								int nHardware = in.Int( nChangeIndex + c * 8 );
								int nBone = in.Int( nChangeIndex + c * 8 + 4 );
								if ( nBone >= 0 && nBone < 256 && nHardware >= 0 && nHardware < 128 )
									palette[nBone] = (short)nHardware;
							}
							for ( int v = nFirst; v < nFirst + nCount; v++ )
							{
								const Vtx6Vertex_t &sv = src[v];
								if ( sv.m_nVert < 0 )
									continue;
								for ( int k = 0; k < sv.m_nNumBones; k++ )
								{
									if ( sv.m_WeightIndex[k] > 3 || sv.m_BoneID[k] < 0 || sv.m_BoneID[k] > 127 )
										continue;
									int nBone = model.m_SlotBone[sv.m_nVert * 4 + sv.m_WeightIndex[k]];
									if ( nBone >= 0 && nBone < 256 && palette[nBone] < 0 )
										palette[nBone] = sv.m_BoneID[k];
								}
							}

							for ( int v = nFirst; v < nFirst + nCount; v++ )
							{
								const Vtx6Vertex_t &sv = src[v];
								unsigned char newIndex[3] = { 0, 0, 0 };
								char newBone[3] = { 0, 0, 0 };
								int nNew = 0;

								if ( sv.m_nVert >= 0 )
								{
									// the kept weights, in their .vvd order
									const signed char *pRemap = &model.m_WeightRemap[sv.m_nVert * 4];
									for ( int nSlot = 0; nSlot < 3; nSlot++ )
									{
										for ( int k = 0; k < 4; k++ )
										{
											if ( pRemap[k] != nSlot )
												continue;
											int nBone = model.m_SlotBone[sv.m_nVert * 4 + k];
											short nHardware = ( nBone >= 0 && nBone < 256 ) ? palette[nBone] : -1;
											if ( nHardware >= 0 )
											{
												newIndex[nNew] = (unsigned char)nSlot;
												newBone[nNew] = (char)nHardware;
												nNew++;
											}
											break;
										}
									}
								}
								else
								{
									for ( int k = 0; k < sv.m_nNumBones && nNew < 3; k++ )
									{
										if ( sv.m_WeightIndex[k] > 2 )
											continue;
										newIndex[nNew] = sv.m_WeightIndex[k];
										newBone[nNew] = (char)sv.m_BoneID[k];
										nNew++;
									}
								}

								const int vd = nVertIndex + v * VTX7_VERTEX_SIZE;
								unsigned char v7[VTX7_VERTEX_SIZE];
								v7[0] = newIndex[0];
								v7[1] = newIndex[1];
								v7[2] = newIndex[2];
								v7[3] = (unsigned char)nNew;
								memcpy( v7 + 4, &sv.m_nOrig, 2 );
								v7[6] = (unsigned char)newBone[0];
								v7[7] = (unsigned char)newBone[1];
								v7[8] = (unsigned char)newBone[2];
								out.SetBytes( vd, v7, VTX7_VERTEX_SIZE );
							}
						}
					}
				}
			}
		}
	}
	(void)nLODs;
	return !in.Error();
}

} // namespace MDLCompat

#endif // MDL_COMPAT_H
