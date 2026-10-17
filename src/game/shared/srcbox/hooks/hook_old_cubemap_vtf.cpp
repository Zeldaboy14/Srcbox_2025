//=============================================================================//
//
// Purpose: Runtime compatibility for old Source seven-face cubemap VTFs.
//
//=============================================================================//

#include "cbase.h"
#include "hook_old_cubemap_vtf.h"
#include "../shared/l4dasw/l4dasw_hooks_util.h"
#include "filesystem.h"
#include "tier1/utlbuffer.h"
#include "tier1/utlvector.h"
#include "bitmap/imageformat.h"
#include "vtf/vtf.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

#if defined( WIN32 ) && !defined( _X360 )
namespace
{

enum
{
	VTFSIG = ( 'V' ) | ( 'T' << 8 ) | ( 'F' << 16 ),
	VTF_LEGACY_IMAGE_RESOURCE = 0x30,
	VTF_BASE_HEADER_SIZE = 16,
	VTF_RESOURCE_TABLE_OFFSET = 80,
	MAX_TRACKED_VTF_FILES = 128,
	MAX_RUNTIME_VTF_CONVERT_SIZE = 16 * 1024 * 1024,

	IBASEFILESYSTEM_CLOSE_INDEX = 3,
	IBASEFILESYSTEM_READFILE_INDEX = 14,

	// IFileSystem primary vtable: IAppSystem has 8 virtuals, then IFileSystem's own
	// methods. IDASQL on Alien Swarm materialsystem.dll confirms OpenEx at +280 and
	// ReadEx at +284, i.e. indices 70 and 71.
	IFILESYSTEM_OPENEX_INDEX = 70,
	IFILESYSTEM_READEX_INDEX = 71
};

typedef void ( __thiscall *CloseFn )( IBaseFileSystem *pThis, FileHandle_t file );
typedef bool ( __thiscall *ReadFileFn )( IBaseFileSystem *pThis, const char *pFileName, const char *pPath, CUtlBuffer &buf, int nMaxBytes, int nStartingByte, FSAllocFunc_t pfnAlloc );
typedef FileHandle_t ( __thiscall *OpenExFn )( IFileSystem *pThis, const char *pFileName, const char *pOptions, unsigned flags, const char *pathID, char **ppszResolvedFilename );
typedef int ( __thiscall *ReadExFn )( IFileSystem *pThis, void *pOutput, int sizeDest, int size, FileHandle_t file );

CloseFn g_pOriginalClose = NULL;
ReadFileFn g_pOriginalReadFile = NULL;
OpenExFn g_pOriginalOpenEx = NULL;
ReadExFn g_pOriginalReadEx = NULL;

void **g_ppCloseSlot = NULL;
void **g_ppReadFileSlot = NULL;
void **g_ppOpenExSlot = NULL;
void **g_ppReadExSlot = NULL;

IBaseFileSystem *g_pHookedBaseFileSystem = NULL;
IFileSystem *g_pHookedFileSystem = NULL;

struct TrackedVTFFile_t
{
	FileHandle_t file;
	char filename[MAX_PATH];
};

CThreadFastMutex g_TrackedFilesMutex;
CUtlVector<TrackedVTFFile_t> g_TrackedFiles;

uint16 ReadU16( const byte *pData )
{
	return (uint16)( pData[0] | ( pData[1] << 8 ) );
}

uint32 ReadU32( const byte *pData )
{
	return (uint32)pData[0]
		| ( (uint32)pData[1] << 8 )
		| ( (uint32)pData[2] << 16 )
		| ( (uint32)pData[3] << 24 );
}

void WriteU32( byte *pData, uint32 nValue )
{
	pData[0] = (byte)( nValue & 0xFF );
	pData[1] = (byte)( ( nValue >> 8 ) & 0xFF );
	pData[2] = (byte)( ( nValue >> 16 ) & 0xFF );
	pData[3] = (byte)( ( nValue >> 24 ) & 0xFF );
}

bool IsVTFName( const char *pFileName )
{
	if ( !pFileName )
		return false;

	int nLen = Q_strlen( pFileName );
	return nLen >= 4 && !Q_stricmp( pFileName + nLen - 4, ".vtf" );
}

void AppendBytes( CUtlVector<byte> &out, const byte *pData, int nBytes )
{
	if ( nBytes <= 0 )
		return;

	int nOldCount = out.Count();
	out.AddMultipleToTail( nBytes );
	Q_memcpy( &out[nOldCount], pData, nBytes );
}

void TrackVTFFile( FileHandle_t file, const char *pFileName )
{
	if ( file == FILESYSTEM_INVALID_HANDLE || !IsVTFName( pFileName ) )
		return;

	AUTO_LOCK( g_TrackedFilesMutex );

	for ( int i = 0; i < g_TrackedFiles.Count(); ++i )
	{
		if ( g_TrackedFiles[i].file == file )
		{
			Q_strncpy( g_TrackedFiles[i].filename, pFileName, sizeof( g_TrackedFiles[i].filename ) );
			return;
		}
	}

	if ( g_TrackedFiles.Count() >= MAX_TRACKED_VTF_FILES )
		g_TrackedFiles.FastRemove( 0 );

	TrackedVTFFile_t &entry = g_TrackedFiles[g_TrackedFiles.AddToTail()];
	entry.file = file;
	Q_strncpy( entry.filename, pFileName, sizeof( entry.filename ) );
}

void UntrackFile( FileHandle_t file )
{
	AUTO_LOCK( g_TrackedFilesMutex );

	for ( int i = 0; i < g_TrackedFiles.Count(); ++i )
	{
		if ( g_TrackedFiles[i].file == file )
		{
			g_TrackedFiles.FastRemove( i );
			return;
		}
	}
}

bool GetTrackedFileName( FileHandle_t file, char *pOut, int nOutSize )
{
	AUTO_LOCK( g_TrackedFilesMutex );

	for ( int i = 0; i < g_TrackedFiles.Count(); ++i )
	{
		if ( g_TrackedFiles[i].file == file )
		{
			Q_strncpy( pOut, g_TrackedFiles[i].filename, nOutSize );
			return true;
		}
	}

	return false;
}

int ComputeImageDataOffset( const byte *pData, int nDataSize, int nMinorVersion, int nHeaderSize )
{
	if ( nHeaderSize < VTF_BASE_HEADER_SIZE || nHeaderSize > nDataSize )
		return -1;

	if ( nMinorVersion >= 3 && nHeaderSize >= VTF_RESOURCE_TABLE_OFFSET + 8 )
	{
		for ( int nResourceOffset = VTF_RESOURCE_TABLE_OFFSET; nResourceOffset + 8 <= nHeaderSize; nResourceOffset += 8 )
		{
			uint32 nType = ReadU32( pData + nResourceOffset ) & 0x00FFFFFF;
			if ( nType == VTF_LEGACY_IMAGE_RESOURCE )
			{
				uint32 nImageOffset = ReadU32( pData + nResourceOffset + 4 );
				if ( nImageOffset >= (uint32)nHeaderSize && nImageOffset <= (uint32)nDataSize )
					return (int)nImageOffset;
			}
		}
	}

	int nImageOffset = nHeaderSize;
	if ( nDataSize >= 63 )
	{
		ImageFormat lowResFormat = (ImageFormat)ReadU32( pData + 57 );
		int nLowResWidth = pData[61];
		int nLowResHeight = pData[62];

		if ( lowResFormat != IMAGE_FORMAT_UNKNOWN && nLowResWidth > 0 && nLowResHeight > 0 )
		{
			nImageOffset += ImageLoader::GetMemRequired( nLowResWidth, nLowResHeight, 1, lowResFormat, false );
		}
	}

	return ( nImageOffset <= nDataSize ) ? nImageOffset : -1;
}

bool ConvertOldSevenFaceCubemapVTFData( const byte *pData, int nDataSize, CUtlVector<byte> &converted )
{
	if ( nDataSize < 88 || !pData )
		return false;

	if ( ReadU32( pData ) != VTFSIG )
		return false;

	int nMajorVersion = (int)ReadU32( pData + 4 );
	int nMinorVersion = (int)ReadU32( pData + 8 );
	if ( nMajorVersion != 7 || nMinorVersion < 1 || nMinorVersion >= 5 )
		return false;

	int nHeaderSize = (int)ReadU32( pData + 12 );
	int nWidth = ReadU16( pData + 16 );
	int nHeight = ReadU16( pData + 18 );
	uint32 nFlags = ReadU32( pData + 20 );
	int nFrameCount = ReadU16( pData + 24 );
	ImageFormat imageFormat = (ImageFormat)ReadU32( pData + 52 );
	int nMipCount = pData[56];

	if ( ( nFlags & TEXTUREFLAGS_ENVMAP ) == 0 )
		return false;

	if ( nWidth <= 0 || nHeight <= 0 || nFrameCount <= 0 || nMipCount <= 0 )
		return false;

	int nImageOffset = ComputeImageDataOffset( pData, nDataSize, nMinorVersion, nHeaderSize );
	if ( nImageOffset < 0 )
		return false;

	const byte *pImageCursor = pData + nImageOffset;
	const byte *pEnd = pData + nDataSize;
	int nSixFaceImageBytes = 0;
	int nSevenFaceImageBytes = 0;

	for ( int nMip = nMipCount - 1; nMip >= 0; --nMip )
	{
		int nMipWidth = MAX( 1, nWidth >> nMip );
		int nMipHeight = MAX( 1, nHeight >> nMip );
		int nFaceSize = ImageLoader::GetMemRequired( nMipWidth, nMipHeight, 1, imageFormat, false );

		if ( nFaceSize <= 0 )
			return false;

		nSixFaceImageBytes += nFaceSize * 6 * nFrameCount;
		nSevenFaceImageBytes += nFaceSize * 7 * nFrameCount;
	}

	if ( nDataSize - nImageOffset < nSevenFaceImageBytes )
		return false;

	converted.RemoveAll();
	converted.EnsureCapacity( nDataSize - ( nSevenFaceImageBytes - nSixFaceImageBytes ) );
	AppendBytes( converted, pData, nImageOffset );

	// Present the converted stream as a modern six-face VTF. This matches the offline
	// BSP conversion that fixes L4D1 cubemaps in Alien Swarm.
	WriteU32( &converted[8], 5 );

	for ( int nMip = nMipCount - 1; nMip >= 0; --nMip )
	{
		int nMipWidth = MAX( 1, nWidth >> nMip );
		int nMipHeight = MAX( 1, nHeight >> nMip );
		int nFaceSize = ImageLoader::GetMemRequired( nMipWidth, nMipHeight, 1, imageFormat, false );

		for ( int nFrame = 0; nFrame < nFrameCount; ++nFrame )
		{
			if ( pImageCursor + nFaceSize * 7 > pEnd )
				return false;

			AppendBytes( converted, pImageCursor, nFaceSize * 6 );
			pImageCursor += nFaceSize * 7;
		}
	}

	if ( pImageCursor < pEnd )
		AppendBytes( converted, pImageCursor, (int)( pEnd - pImageCursor ) );

	return true;
}

bool ConvertOldSevenFaceCubemapVTF( CUtlBuffer &buf )
{
	const int nDataSize = buf.TellMaxPut();
	const byte *pData = (const byte *)buf.Base();
	CUtlVector<byte> converted;

	if ( !ConvertOldSevenFaceCubemapVTFData( pData, nDataSize, converted ) )
		return false;

	buf.Clear();
	buf.Put( converted.Base(), converted.Count() );
	buf.SeekGet( CUtlBuffer::SEEK_HEAD, 0 );
	return true;
}

int TryReadConvertedVTF( IFileSystem *pThis, void *pOutput, int sizeDest, int size, FileHandle_t file )
{
	if ( !g_pOriginalReadEx || !g_pHookedBaseFileSystem || !pOutput || size <= 1024 || sizeDest < size )
		return -1;

	char filename[MAX_PATH];
	if ( !GetTrackedFileName( file, filename, sizeof( filename ) ) )
		return -1;

	int nOriginalTell = g_pHookedBaseFileSystem->Tell( file );
	if ( nOriginalTell != 0 )
		return -1;

	int nSourceSize = g_pHookedBaseFileSystem->Size( file );
	if ( nSourceSize <= size || nSourceSize > MAX_RUNTIME_VTF_CONVERT_SIZE )
		return -1;

	CUtlVector<byte> source;
	source.AddMultipleToTail( nSourceSize );

	g_pHookedBaseFileSystem->Seek( file, 0, FILESYSTEM_SEEK_HEAD );
	int nBytesRead = g_pOriginalReadEx( pThis, source.Base(), nSourceSize, nSourceSize, file );
	if ( nBytesRead != nSourceSize )
	{
		g_pHookedBaseFileSystem->Seek( file, nOriginalTell, FILESYSTEM_SEEK_HEAD );
		return -1;
	}

	CUtlVector<byte> converted;
	if ( !ConvertOldSevenFaceCubemapVTFData( source.Base(), nBytesRead, converted ) || converted.Count() > sizeDest || converted.Count() > size )
	{
		g_pHookedBaseFileSystem->Seek( file, nOriginalTell, FILESYSTEM_SEEK_HEAD );
		return -1;
	}

	Q_memcpy( pOutput, converted.Base(), converted.Count() );
	g_pHookedBaseFileSystem->Seek( file, nOriginalTell + converted.Count(), FILESYSTEM_SEEK_HEAD );
	DevMsg( "L4DASW: converted old cubemap VTF '%s' in memory\n", filename );
	return converted.Count();
}

void __fastcall HookedClose( IBaseFileSystem *pThis, void * /*edx*/, FileHandle_t file )
{
	UntrackFile( file );
	g_pOriginalClose( pThis, file );
}

bool __fastcall HookedReadFile( IBaseFileSystem *pThis, void * /*edx*/, const char *pFileName, const char *pPath, CUtlBuffer &buf, int nMaxBytes, int nStartingByte, FSAllocFunc_t pfnAlloc )
{
	bool bResult = g_pOriginalReadFile( pThis, pFileName, pPath, buf, nMaxBytes, nStartingByte, pfnAlloc );
	if ( bResult && ConvertOldSevenFaceCubemapVTF( buf ) )
		DevMsg( "L4DASW: converted old cubemap VTF '%s' in memory\n", pFileName ? pFileName : "<unknown>" );
	return bResult;
}

FileHandle_t __fastcall HookedOpenEx( IFileSystem *pThis, void * /*edx*/, const char *pFileName, const char *pOptions, unsigned flags, const char *pathID, char **ppszResolvedFilename )
{
	FileHandle_t file = g_pOriginalOpenEx( pThis, pFileName, pOptions, flags, pathID, ppszResolvedFilename );
	TrackVTFFile( file, pFileName );
	return file;
}

int __fastcall HookedReadEx( IFileSystem *pThis, void * /*edx*/, void *pOutput, int sizeDest, int size, FileHandle_t file )
{
	int nConvertedBytes = TryReadConvertedVTF( pThis, pOutput, sizeDest, size, file );
	if ( nConvertedBytes >= 0 )
		return nConvertedBytes;

	return g_pOriginalReadEx( pThis, pOutput, sizeDest, size, file );
}

} // namespace

void L4DASW_InstallOldCubemapVTFReadHook()
{
	if ( g_pOriginalReadFile )
		return;

	IBaseFileSystem *pBaseFileSystem = static_cast<IBaseFileSystem *>( filesystem );
	void **ppBaseVTable = *(void ***)pBaseFileSystem;
	void **ppFileSystemVTable = *(void ***)filesystem;

	g_pHookedBaseFileSystem = pBaseFileSystem;
	g_pHookedFileSystem = filesystem;

	g_ppCloseSlot = &ppBaseVTable[IBASEFILESYSTEM_CLOSE_INDEX];
	g_ppReadFileSlot = &ppBaseVTable[IBASEFILESYSTEM_READFILE_INDEX];
	g_ppOpenExSlot = &ppFileSystemVTable[IFILESYSTEM_OPENEX_INDEX];
	g_ppReadExSlot = &ppFileSystemVTable[IFILESYSTEM_READEX_INDEX];

	if ( ReplaceVTableSlot( g_ppCloseSlot, (void *)&HookedClose, (void **)&g_pOriginalClose )
		&& ReplaceVTableSlot( g_ppReadFileSlot, (void *)&HookedReadFile, (void **)&g_pOriginalReadFile )
		&& ReplaceVTableSlot( g_ppOpenExSlot, (void *)&HookedOpenEx, (void **)&g_pOriginalOpenEx )
		&& ReplaceVTableSlot( g_ppReadExSlot, (void *)&HookedReadEx, (void **)&g_pOriginalReadEx ) )
	{
		DevMsg( "L4DASW: installed old cubemap VTF filesystem hooks\n" );
	}
	else
	{
		L4DASW_RemoveOldCubemapVTFReadHook();
		Warning( "L4DASW: failed to install old cubemap VTF filesystem hooks\n" );
	}
}

void L4DASW_RemoveOldCubemapVTFReadHook()
{
	void *pIgnoredOriginal = NULL;

	if ( g_pOriginalReadEx && g_ppReadExSlot )
		ReplaceVTableSlot( g_ppReadExSlot, (void *)g_pOriginalReadEx, &pIgnoredOriginal );
	if ( g_pOriginalOpenEx && g_ppOpenExSlot )
		ReplaceVTableSlot( g_ppOpenExSlot, (void *)g_pOriginalOpenEx, &pIgnoredOriginal );
	if ( g_pOriginalReadFile && g_ppReadFileSlot )
		ReplaceVTableSlot( g_ppReadFileSlot, (void *)g_pOriginalReadFile, &pIgnoredOriginal );
	if ( g_pOriginalClose && g_ppCloseSlot )
		ReplaceVTableSlot( g_ppCloseSlot, (void *)g_pOriginalClose, &pIgnoredOriginal );

	g_pOriginalClose = NULL;
	g_pOriginalReadFile = NULL;
	g_pOriginalOpenEx = NULL;
	g_pOriginalReadEx = NULL;
	g_ppCloseSlot = NULL;
	g_ppReadFileSlot = NULL;
	g_ppOpenExSlot = NULL;
	g_ppReadExSlot = NULL;
	g_pHookedBaseFileSystem = NULL;
	g_pHookedFileSystem = NULL;

	AUTO_LOCK( g_TrackedFilesMutex );
	g_TrackedFiles.RemoveAll();
}

#else // Stubs

void L4DASW_InstallOldCubemapVTFReadHook()
{
}

void L4DASW_RemoveOldCubemapVTFReadHook()
{
}

#endif