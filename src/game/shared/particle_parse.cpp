//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
//=============================================================================

#include "cbase.h"
#include "filesystem.h"
#include <KeyValues.h>
#include "particle_parse.h"
#include "particles/particles.h"
#include "tier0/icommandline.h"

#ifdef GAME_DLL
#include "te_effect_dispatch.h"
#include "networkstringtable_gamedll.h"
#else
#include "c_te_effect_dispatch.h"
#include "networkstringtable_clientdll.h"
#endif

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

#define PARTICLES_MANIFEST_FILE				"particles/particles_manifest.txt"

// How many particle manifests can your map reference.  Was being used to drop bogus manifests into download directory
// to DoS rival community maps because we can't just be cool.
//
// We also prefer to load particles manifests from the BSP now to prevent this, but we have no way with the legacy
// download system to prevent maps with no particles manifest from having one provided by another source.
#define PARTICLES_MANIFEST_MAX_MAP_ENTRIES	64

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
int GetAttachTypeFromString( const char *pszString )
{
	if ( !pszString || !pszString[0] )
		return -1;

	// If you add new attach types, you need to add them to this list
	static const char *pAttachmentNames[MAX_PATTACH_TYPES] =
	{
		"start_at_origin",		// PATTACH_ABSORIGIN = 0,
		"follow_origin",		// PATTACH_ABSORIGIN_FOLLOW,
		"start_at_customorigin",// PATTACH_CUSTOMORIGIN,
		"start_at_attachment",	// PATTACH_POINT,
		"follow_attachment",	// PATTACH_POINT_FOLLOW,
		"follow_rootbone",		// PATTACH_ROOTBONE_FOLLOW
	};

	for ( int i = 0; i < MAX_PATTACH_TYPES; i++ )
	{
		if ( FStrEq( pAttachmentNames[i], pszString ) )
			return i;
	}

	return -1;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : list - 
//-----------------------------------------------------------------------------
#ifndef USE_NEW_PARTICLE_SYSTEM
void GetParticleManifest(CUtlVector<CUtlString>& list)
{
	// Open the manifest file, and read the particles specified inside it
	KeyValues *manifest = new KeyValues( PARTICLES_MANIFEST_FILE );
	if ( manifest->LoadFromFile( filesystem, PARTICLES_MANIFEST_FILE, "GAME" ) )
	{
		for ( KeyValues *sub = manifest->GetFirstSubKey(); sub != NULL; sub = sub->GetNextKey() )
		{
			if ( !Q_stricmp( sub->GetName(), "file" ) )
			{
				list.AddToTail( sub->GetString() );
				continue;
			}

			Warning( "CParticleMgr::Init:  Manifest '%s' with bogus file type '%s', expecting 'file'\n", PARTICLES_MANIFEST_FILE, sub->GetName() );
		}
	}
	else
	{
		Warning( "PARTICLE SYSTEM: Unable to load manifest file '%s'\n", PARTICLES_MANIFEST_FILE );
	}

	manifest->deleteThis();
}
#endif


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
#ifdef SRCBOX
//-----------------------------------------------------------------------------
// Srcbox: appends particle systems from every mounted game ( mountdepots, gamepacks, gameinfo
// search paths, mount.cfg ). Each game's own particles/particles_manifest.txt is read from that
// game's search paths only, and only systems that aren't defined yet are added, so the mod's
// manifest always wins, then games in search path order. Disable with -noparticleappend.
//-----------------------------------------------------------------------------
// Reads files from one mounted game's folders and VPKs directly ( by absolute path ), so the
// search paths are never touched.
class CParticleAppendSource
{
public:
	struct VPKEntry_t
	{
		CUtlString m_Path;		// lowercase, forward slashes
		int m_nArchive;
		unsigned int m_nOffset;
		unsigned int m_nLength;
		int m_nPreloadOffset;	// into m_Tree
		int m_nPreloadBytes;
	};

	struct VPK_t
	{
		CUtlString m_DirFile;
		CUtlString m_ArchiveBase;	// "<path>/<name>" for "<name>_%03d.vpk"
		CUtlBuffer m_Tree;			// header + tree
		int m_nDataStart;			// where embedded ( 0x7fff ) data starts in the dir file
		CUtlVector< VPKEntry_t > m_Entries;
	};

	~CParticleAppendSource()
	{
		m_VPKs.PurgeAndDeleteElements();
	}

	void AddPath( const char *pPath )
	{
		const char *pExt = V_GetFileExtension( pPath );
		if ( pExt && !V_stricmp( pExt, "vpk" ) )
			AddVPK( pPath );
		else
			m_Folders.AddToTail( CUtlString( pPath ) );
	}

	bool ReadFile( const char *pRelative, CUtlBuffer &buf )
	{
		char szRel[ MAX_PATH ];
		V_strncpy( szRel, pRelative, sizeof( szRel ) );
		V_FixSlashes( szRel, '/' );
		V_strlower( szRel );

		for ( int i = 0; i < m_Folders.Count(); ++i )
		{
			char szFull[ MAX_PATH ];
			V_snprintf( szFull, sizeof( szFull ), "%s/%s", m_Folders[ i ].Get(), szRel );
			V_FixSlashes( szFull );
			if ( g_pFullFileSystem->FileExists( szFull ) && g_pFullFileSystem->ReadFile( szFull, NULL, buf ) )
				return true;
		}

		for ( int i = 0; i < m_VPKs.Count(); ++i )
		{
			VPK_t *pVPK = m_VPKs[ i ];
			for ( int e = 0; e < pVPK->m_Entries.Count(); ++e )
			{
				if ( V_strcmp( pVPK->m_Entries[ e ].m_Path.Get(), szRel ) )
					continue;
				return ReadVPKEntry( pVPK, pVPK->m_Entries[ e ], buf );
			}
		}
		return false;
	}

private:
	void AddVPK( const char *pPath )
	{
		// Search paths can name either "name.vpk" or "name_dir.vpk"
		char szBase[ MAX_PATH ];
		V_StripExtension( pPath, szBase, sizeof( szBase ) );
		int nBaseLen = V_strlen( szBase );
		if ( nBaseLen > 4 && !V_stricmp( szBase + nBaseLen - 4, "_dir" ) )
			szBase[ nBaseLen - 4 ] = 0;

		char szDir[ MAX_PATH ];
		V_snprintf( szDir, sizeof( szDir ), "%s_dir.vpk", szBase );
		V_FixSlashes( szDir );
		if ( !g_pFullFileSystem->FileExists( szDir ) )
		{
			V_strncpy( szDir, pPath, sizeof( szDir ) );	// single file VPK
			V_FixSlashes( szDir );
		}

		for ( int i = 0; i < m_VPKs.Count(); ++i )
		{
			if ( !V_stricmp( m_VPKs[ i ]->m_DirFile.Get(), szDir ) )
				return;
		}

		FileHandle_t hFile = g_pFullFileSystem->Open( szDir, "rb" );
		if ( !hFile )
			return;

		unsigned int nHeader[ 7 ] = { 0 };
		if ( g_pFullFileSystem->Read( nHeader, 12, hFile ) != 12 || nHeader[ 0 ] != 0x55aa1234 || ( nHeader[ 1 ] != 1 && nHeader[ 1 ] != 2 ) )
		{
			g_pFullFileSystem->Close( hFile );
			return;
		}
		int nHeaderSize = ( nHeader[ 1 ] == 2 ) ? 28 : 12;
		unsigned int nTreeSize = nHeader[ 2 ];
		if ( nTreeSize == 0 || nTreeSize > ( 256u << 20 ) )
		{
			g_pFullFileSystem->Close( hFile );
			return;
		}

		VPK_t *pVPK = new VPK_t;
		pVPK->m_DirFile = szDir;
		pVPK->m_ArchiveBase = szBase;
		pVPK->m_nDataStart = nHeaderSize + nTreeSize;
		pVPK->m_Tree.EnsureCapacity( nHeaderSize + nTreeSize );
		g_pFullFileSystem->Seek( hFile, 0, FILESYSTEM_SEEK_HEAD );
		int nRead = g_pFullFileSystem->Read( pVPK->m_Tree.Base(), nHeaderSize + nTreeSize, hFile );
		g_pFullFileSystem->Close( hFile );
		if ( nRead != (int)( nHeaderSize + nTreeSize ) )
		{
			delete pVPK;
			return;
		}

		ParseTree( pVPK, (const unsigned char *)pVPK->m_Tree.Base(), nHeaderSize, nHeaderSize + nTreeSize );
		m_VPKs.AddToTail( pVPK );
	}

	static const char *TreeString( const unsigned char *pData, int &nPos, int nEnd )
	{
		int nStart = nPos;
		while ( nPos < nEnd && pData[ nPos ] )
			++nPos;
		if ( nPos >= nEnd )
			return NULL;
		++nPos;
		return (const char *)( pData + nStart );
	}

	// Only .pcf and .txt entries are kept ( manifests and particle files )
	static void ParseTree( VPK_t *pVPK, const unsigned char *pData, int nPos, int nEnd )
	{
		for ( ;; )
		{
			const char *pExt = TreeString( pData, nPos, nEnd );
			if ( !pExt || !pExt[ 0 ] )
				return;
			bool bWanted = !V_stricmp( pExt, "pcf" ) || !V_stricmp( pExt, "txt" );
			for ( ;; )
			{
				const char *pDir = TreeString( pData, nPos, nEnd );
				if ( !pDir )
					return;
				if ( !pDir[ 0 ] )
					break;
				for ( ;; )
				{
					const char *pName = TreeString( pData, nPos, nEnd );
					if ( !pName )
						return;
					if ( !pName[ 0 ] )
						break;
					if ( nPos + 18 > nEnd )
						return;

					unsigned short nPreload, nArchive;
					unsigned int nOffset, nLength;
					memcpy( &nPreload, pData + nPos + 4, 2 );
					memcpy( &nArchive, pData + nPos + 6, 2 );
					memcpy( &nOffset, pData + nPos + 8, 4 );
					memcpy( &nLength, pData + nPos + 12, 4 );
					nPos += 18;
					int nPreloadOffset = nPos;
					nPos += nPreload;
					if ( nPos > nEnd )
						return;

					if ( bWanted && ( !V_stricmp( pExt, "txt" ) ? !V_stricmp( pName, "particles_manifest" ) : true ) )
					{
						char szPath[ MAX_PATH ];
						if ( !V_strcmp( pDir, " " ) )
							V_snprintf( szPath, sizeof( szPath ), "%s.%s", pName, pExt );
						else
							V_snprintf( szPath, sizeof( szPath ), "%s/%s.%s", pDir, pName, pExt );
						V_FixSlashes( szPath, '/' );
						V_strlower( szPath );

						VPKEntry_t &entry = pVPK->m_Entries[ pVPK->m_Entries.AddToTail() ];
						entry.m_Path = szPath;
						entry.m_nArchive = nArchive;
						entry.m_nOffset = nOffset;
						entry.m_nLength = nLength;
						entry.m_nPreloadOffset = nPreloadOffset;
						entry.m_nPreloadBytes = nPreload;
					}
				}
			}
		}
	}

	static bool ReadVPKEntry( VPK_t *pVPK, const VPKEntry_t &entry, CUtlBuffer &buf )
	{
		buf.Purge();
		if ( entry.m_nPreloadBytes )
			buf.Put( (const unsigned char *)pVPK->m_Tree.Base() + entry.m_nPreloadOffset, entry.m_nPreloadBytes );
		if ( !entry.m_nLength )
			return true;

		char szArchive[ MAX_PATH ];
		unsigned int nOffset = entry.m_nOffset;
		if ( entry.m_nArchive == 0x7fff )
		{
			V_strncpy( szArchive, pVPK->m_DirFile.Get(), sizeof( szArchive ) );
			nOffset += pVPK->m_nDataStart;
		}
		else
		{
			V_snprintf( szArchive, sizeof( szArchive ), "%s_%03d.vpk", pVPK->m_ArchiveBase.Get(), entry.m_nArchive );
			V_FixSlashes( szArchive );
		}

		FileHandle_t hFile = g_pFullFileSystem->Open( szArchive, "rb" );
		if ( !hFile )
			return false;

		int nOld = buf.TellPut();
		buf.EnsureCapacity( nOld + entry.m_nLength );
		g_pFullFileSystem->Seek( hFile, nOffset, FILESYSTEM_SEEK_HEAD );
		int nRead = g_pFullFileSystem->Read( (unsigned char *)buf.Base() + nOld, entry.m_nLength, hFile );
		g_pFullFileSystem->Close( hFile );
		if ( nRead != (int)entry.m_nLength )
			return false;
		buf.SeekPut( CUtlBuffer::SEEK_HEAD, nOld + entry.m_nLength );
		return true;
	}

	CUtlVector< CUtlString > m_Folders;
	CUtlVector< VPK_t * > m_VPKs;
};

static void NormalizeParticleAppendPath( char *pPath )
{
	V_FixSlashes( pPath, '/' );
	V_strlower( pPath );
	int nLen = V_strlen( pPath );
	while ( nLen > 0 && pPath[ nLen - 1 ] == '/' )
		pPath[ --nLen ] = 0;
}

static void AppendMountedGameParticles()
{
	if ( CommandLine()->FindParm( "-noparticleappend" ) )
		return;

	// The mod's own folder ( its particles come from the main manifest )
	char szModPath[ MAX_PATH ] = { 0 };
	{
		char szModPaths[ 4096 ];
		if ( g_pFullFileSystem->GetSearchPath( "MOD", false, szModPaths, sizeof( szModPaths ) ) > 0 )
		{
			char *pSemi = strchr( szModPaths, ';' );
			if ( pSemi )
				*pSemi = 0;
			V_strncpy( szModPath, szModPaths, sizeof( szModPath ) );
			NormalizeParticleAppendPath( szModPath );
		}
	}

	// Every GAME search path, including VPKs
	CUtlVector< char > searchPaths;
	searchPaths.SetCount( 1 << 16 );
	int nLen = g_pFullFileSystem->GetSearchPath( "GAME", true, searchPaths.Base(), searchPaths.Count() );
	if ( nLen >= searchPaths.Count() )
	{
		searchPaths.SetCount( nLen + 1 );
		g_pFullFileSystem->GetSearchPath( "GAME", true, searchPaths.Base(), searchPaths.Count() );
	}
	searchPaths[ searchPaths.Count() - 1 ] = 0;

	// Group the paths by game folder ( a VPK belongs to the folder it's in ), keeping search order
	CUtlVector< CUtlString > groupKeys;
	CUtlVector< CParticleAppendSource * > groupSources;

	CUtlStringList entries;
	V_SplitString( searchPaths.Base(), ";", entries );
	for ( int i = 0; i < entries.Count(); ++i )
	{
		char szEntry[ MAX_PATH ];
		V_strncpy( szEntry, entries[ i ], sizeof( szEntry ) );
		NormalizeParticleAppendPath( szEntry );
		if ( !szEntry[ 0 ] )
			continue;

		const char *pExt = V_GetFileExtension( szEntry );
		if ( pExt && !V_stricmp( pExt, "bsp" ) )
			continue;	// map pak files

		char szKey[ MAX_PATH ];
		V_strncpy( szKey, szEntry, sizeof( szKey ) );
		if ( pExt && !V_stricmp( pExt, "vpk" ) )
		{
			V_StripFilename( szKey );
			NormalizeParticleAppendPath( szKey );
		}

		if ( szModPath[ 0 ] && !V_strnicmp( szKey, szModPath, V_strlen( szModPath ) ) )
			continue;	// the mod's own folder, custom/ and anything under it

		int nGroup = -1;
		for ( int g = 0; g < groupKeys.Count(); ++g )
		{
			if ( !V_stricmp( groupKeys[ g ].Get(), szKey ) )
			{
				nGroup = g;
				break;
			}
		}
		if ( nGroup < 0 )
		{
			nGroup = groupKeys.AddToTail( CUtlString( szKey ) );
			groupSources.AddToTail( new CParticleAppendSource );
		}
		groupSources[ nGroup ]->AddPath( szEntry );
	}

	SetParticleSkipExistingDefinitions( true );

	int nTotalAdded = 0;
	for ( int g = 0; g < groupKeys.Count(); ++g )
	{
		CParticleAppendSource *pSource = groupSources[ g ];

		CUtlBuffer manifestBuf( 0, 0, CUtlBuffer::TEXT_BUFFER );
		if ( !pSource->ReadFile( PARTICLES_MANIFEST_FILE, manifestBuf ) )
			continue;

		KeyValues *pManifest = new KeyValues( PARTICLES_MANIFEST_FILE );
		if ( pManifest->LoadFromBuffer( PARTICLES_MANIFEST_FILE, manifestBuf ) )
		{
			int nBefore = g_pParticleSystemMgr->GetParticleSystemCount();
			int nFiles = 0;

			for ( KeyValues *pSub = pManifest->GetFirstSubKey(); pSub != NULL; pSub = pSub->GetNextKey() )
			{
				if ( Q_stricmp( pSub->GetName(), "file" ) )
					continue;

				const char *pFile = pSub->GetString();
				if ( pFile[ 0 ] == '!' )
					++pFile;
				if ( !pFile[ 0 ] )
					continue;

				CUtlBuffer buf;
				if ( !pSource->ReadFile( pFile, buf ) )
					continue;

				g_pParticleSystemMgr->ReadParticleConfigFile( buf, false, false, pFile );
				++nFiles;
			}

			int nAdded = g_pParticleSystemMgr->GetParticleSystemCount() - nBefore;
			nTotalAdded += nAdded;
			DevMsg( "Particles: appended %d new system(s) from %d file(s) in %s\n", nAdded, nFiles, groupKeys[ g ].Get() );
		}
		pManifest->deleteThis();
	}

	SetParticleSkipExistingDefinitions( false );
	groupSources.PurgeAndDeleteElements();

	if ( nTotalAdded )
		Msg( "Particles: appended %d particle system(s) from mounted games\n", nTotalAdded );
}
#endif // SRCBOX

void ParseParticleEffects( bool bLoadSheets, bool bPrecache )
{
	MEM_ALLOC_CREDIT();

	g_pParticleSystemMgr->ShouldLoadSheets( bLoadSheets );

	CUtlVector<CUtlString> files;
	GetParticleManifest( files );

	int nCount = files.Count();
	for ( int i = 0; i < nCount; ++i )
	{
		g_pParticleSystemMgr->ReadParticleConfigFile( files[i], bPrecache, false );
	}

#ifdef SRCBOX
	AppendMountedGameParticles();
#endif

	g_pParticleSystemMgr->DecommitTempMemory();
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void ReloadParticleEffects()
{
	MEM_ALLOC_CREDIT();

	CUtlVector<CUtlString> files;
	GetParticleManifest( files );

	// CAB 2/17/11 Reload all the particles regardless (Fixes filename change exploits).
	bool bReloadAll = true;

	//int nCount = files.Count();
	//for ( int i = 0; i < nCount; ++i )
	//{
	//	// Skip the precache marker
	//	const char *pFile = files[i];
 //		if ( pFile[0] == '!' )
 //		{
 //			pFile++;
 //		}

	//	char szDX80Filename[MAX_PATH];
	//	V_strncpy( szDX80Filename, pFile, sizeof( szDX80Filename ) );
	//	V_StripExtension( pFile, szDX80Filename, sizeof( szDX80Filename ) );
	//	V_strncat( szDX80Filename, "_dx80.", sizeof( szDX80Filename ) );
	//	V_strncat( szDX80Filename, V_GetFileExtension( pFile ), sizeof( szDX80Filename ) );

	//	if ( pFilesToReload->IsFileInList( pFile ) || pFilesToReload->IsFileInList( szDX80Filename ) )
	//	{
	//		Msg( "Reloading all particle files due to pure settings.\n" );
	//		bReloadAll = true;
	//		break;
	//	}
	//}

	// Then check to see if we need to reload the map's particles
	const char *pszMapName = NULL;
#ifdef CLIENT_DLL
	pszMapName = engine->GetLevelName();	
#else
	pszMapName = STRING( gpGlobals->mapname );
#endif
	if ( pszMapName && pszMapName[0] )
	{
		char mapname[MAX_MAP_NAME];
		Q_FileBase( pszMapName, mapname, sizeof( mapname ) );
		Q_strlower( mapname );
		ParseParticleEffectsMap( mapname, true );
	}

	if ( bReloadAll )
	{
		ParseParticleEffects( true, true );
	}
	
	g_pParticleSystemMgr->DecommitTempMemory();
}

//-----------------------------------------------------------------------------
// Purpose: loads per-map manifest!
//-----------------------------------------------------------------------------
void ParseParticleEffectsMap( const char *pMapName, bool bLoadSheets )
{
	MEM_ALLOC_CREDIT();

	CUtlVector<CUtlString> files;
	char szMapManifestFilename[MAX_PATH] = { 0 };

	szMapManifestFilename[0] = NULL;

	if ( pMapName && *pMapName )
	{
		V_snprintf( szMapManifestFilename, sizeof( szMapManifestFilename ), "maps/%s_particles.txt", pMapName );
	}

	KeyValues *manifest = new KeyValues( szMapManifestFilename );

	// In order:
	//  - particles.txt within the map BSP
	//  - mapname_particles.txt within the map BSP
	//  - mapname_particles.txt in the GAME path
	bool bLoaded = ( manifest->LoadFromFile( filesystem, "particles.txt", "BSP" ) ||
	                 manifest->LoadFromFile( filesystem, szMapManifestFilename, "BSP" ) ||
	                 manifest->LoadFromFile( filesystem, szMapManifestFilename, "GAME" ) );

	// Open the manifest file, and read the particles specified inside it
	if ( bLoaded )
	{
		DevMsg( "Successfully loaded particle effects manifest '%s' for map '%s'\n", szMapManifestFilename, pMapName );
		for ( KeyValues *sub = manifest->GetFirstSubKey(); sub != NULL; sub = sub->GetNextKey() )
		{
			if ( files.Count() >= PARTICLES_MANIFEST_MAX_MAP_ENTRIES )
			{
				Warning( "CParticleMgr::LevelInit:  Map particles manifest '%s' specifies more than %d entries.\n",
				         szMapManifestFilename, PARTICLES_MANIFEST_MAX_MAP_ENTRIES );
				break;
			}

			if ( !Q_stricmp( sub->GetName(), "file" ) )
			{
				// Ensure the particles are in the particles directory
				char szPath[ 512 ];
				Q_strncpy( szPath, sub->GetString(), sizeof( szPath ) );
				Q_StripFilename( szPath );
				char *pszPath = (szPath[0] == '!') ? &szPath[1] : &szPath[0];
				if ( pszPath && pszPath[0] && !Q_stricmp( pszPath, "particles" ) )
				{
					files.AddToTail( sub->GetString() );
					continue;
				}
				else
				{
					Warning( "CParticleMgr::LevelInit:  Manifest '%s' contains a particle file '%s' that's not under the particles directory. Custom particles must be placed in the particles directory.\n", szMapManifestFilename, sub->GetString() );
				}
			}
			else
			{
				Warning( "CParticleMgr::LevelInit:  Manifest '%s' with bogus file type '%s', expecting 'file'\n", szMapManifestFilename, sub->GetName() );
			}
		}
	}
	else
	{
		// Don't print a warning, and don't proceed any further if the file doesn't exist!
		return;
	}

	int nCount = files.Count();
	if ( !nCount )
	{
		return;
	}

	g_pParticleSystemMgr->ShouldLoadSheets( bLoadSheets );

	for ( int i = 0; i < nCount; ++i )
	{
		g_pParticleSystemMgr->ReadParticleConfigFile( files[i], true, true );
	}

	g_pParticleSystemMgr->DecommitTempMemory();
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void PrecacheStandardParticleSystems( )
{
#ifdef GAME_DLL
	int nTotalPrecacheCount = 0;
	// Now add each particle system name to the network string pool, so we can send string_t's 
	// down to the client instead of full particle system names.
	for ( int i = 0; i < g_pParticleSystemMgr->GetParticleSystemCount(); i++ )
	{
		const char *pParticleSystemName = g_pParticleSystemMgr->GetParticleSystemNameFromIndex(i);
		CParticleSystemDefinition *pParticleSystem = g_pParticleSystemMgr->FindParticleSystem( pParticleSystemName );
		if ( pParticleSystem->ShouldAlwaysPrecache() )
		{
			PrecacheParticleSystem( pParticleSystemName );
			nTotalPrecacheCount++;
		}
	}
#endif // GAME_DLL
}



//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void DispatchParticleEffect( const char *pszParticleName, ParticleAttachment_t iAttachType, CBaseEntity *pEntity, const char *pszAttachmentName, bool bResetAllParticlesOnEntity )
{
	int iAttachment = -1;
	if ( pEntity && pEntity->GetBaseAnimating() )
	{
		// Find the attachment point index
		iAttachment = pEntity->GetBaseAnimating()->LookupAttachment( pszAttachmentName );
		if ( iAttachment <= 0 )
		{
			return;
		}
	}

	DispatchParticleEffect( pszParticleName, iAttachType, pEntity, iAttachment, bResetAllParticlesOnEntity );
}



//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void DispatchParticleEffect( const char *pszParticleName, ParticleAttachment_t iAttachType, CBaseEntity *pEntity, int iAttachmentPoint, bool bResetAllParticlesOnEntity )
{
	CEffectData	data;

	data.m_nHitBox = GetParticleSystemIndex( pszParticleName );
	if ( pEntity )
	{
#ifdef CLIENT_DLL
		data.m_hEntity = pEntity;
#else
		data.m_nEntIndex = pEntity->entindex();
#endif
		data.m_fFlags |= PARTICLE_DISPATCH_FROM_ENTITY;
		data.m_vOrigin = pEntity->GetAbsOrigin();
	}
	data.m_nDamageType = iAttachType;
	data.m_nAttachmentIndex = iAttachmentPoint;

	if ( bResetAllParticlesOnEntity )
	{
		data.m_fFlags |= PARTICLE_DISPATCH_RESET_PARTICLES;
	}

#ifdef GAME_DLL
	if ( ( data.m_fFlags & PARTICLE_DISPATCH_FROM_ENTITY ) != 0 &&
		 ( iAttachType == PATTACH_ABSORIGIN_FOLLOW || iAttachType == PATTACH_POINT_FOLLOW || iAttachType == PATTACH_ROOTBONE_FOLLOW ) )
	{
		CBroadcastRecipientFilter filter;
		DispatchEffect( "ParticleEffect", data, filter );
	}
	else
#endif
	{
		DispatchEffect( "ParticleEffect", data );
	}
}


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void DispatchParticleEffect( const char *pszParticleName, ParticleAttachment_t iAttachType, CBaseEntity *pEntity, const char *pszAttachmentName, Vector vecColor1, Vector vecColor2, bool bUseColors, bool bResetAllParticlesOnEntity )
{
	int iAttachment = -1;
	if ( pEntity && pEntity->GetBaseAnimating() )
	{
		// Find the attachment point index
		iAttachment = pEntity->GetBaseAnimating()->LookupAttachment( pszAttachmentName );
		if ( iAttachment <= 0 )
		{
			return;
		}
	}

	CEffectData	data;

	data.m_nHitBox = GetParticleSystemIndex( pszParticleName );
	if ( pEntity )
	{
#ifdef CLIENT_DLL
		data.m_hEntity = pEntity;
#else
		data.m_nEntIndex = pEntity->entindex();
#endif
		data.m_fFlags |= PARTICLE_DISPATCH_FROM_ENTITY;
		data.m_vOrigin = pEntity->GetAbsOrigin();
	}
	data.m_nDamageType = iAttachType;
	data.m_nAttachmentIndex = iAttachment;

	if ( bResetAllParticlesOnEntity )
	{
		data.m_fFlags |= PARTICLE_DISPATCH_RESET_PARTICLES;
	}

	if ( bUseColors )
	{
		data.m_bCustomColors = true;
		data.m_CustomColors.m_vecColor1 = vecColor1;
		data.m_CustomColors.m_vecColor2 = vecColor2;
	}

#ifdef GAME_DLL
	if ( ( data.m_fFlags & PARTICLE_DISPATCH_FROM_ENTITY ) != 0 &&
		 ( iAttachType == PATTACH_ABSORIGIN_FOLLOW || iAttachType == PATTACH_POINT_FOLLOW || iAttachType == PATTACH_ROOTBONE_FOLLOW ) )
	{
		CReliableBroadcastRecipientFilter filter;
		DispatchEffect( "ParticleEffect", data, filter );
	}
	else
#endif
	{
		DispatchEffect( "ParticleEffect", data );
	}
}


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void DispatchParticleEffect( int iEffectIndex, Vector vecOrigin, Vector vecStart, QAngle vecAngles, CBaseEntity *pEntity )
{
	CEffectData	data;

	data.m_nHitBox = iEffectIndex;
	data.m_vOrigin = vecOrigin;
	data.m_vStart = vecStart;
	data.m_vAngles = vecAngles;

	if ( pEntity )
	{
#ifdef CLIENT_DLL
		data.m_hEntity = pEntity;
#else
		data.m_nEntIndex = pEntity->entindex();
#endif
		data.m_fFlags |= PARTICLE_DISPATCH_FROM_ENTITY;
		data.m_nDamageType = PATTACH_CUSTOMORIGIN;
	}
	else
	{
#ifdef CLIENT_DLL
		data.m_hEntity = NULL;
#else
		data.m_nEntIndex = 0;
#endif
	}

	DispatchEffect( "ParticleEffect", data );
}


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void DispatchParticleEffect( const char *pszParticleName, Vector vecOrigin, QAngle vecAngles, Vector vecColor1, Vector vecColor2, bool bUseColors, CBaseEntity *pEntity, int iAttachType )
{
	int iEffectIndex = GetParticleSystemIndex( pszParticleName );

	CEffectData	data;

	data.m_nHitBox = iEffectIndex;
	data.m_vOrigin = vecOrigin;
	data.m_vAngles = vecAngles;

	if ( pEntity )
	{
#ifdef CLIENT_DLL
		data.m_hEntity = pEntity;
#else
		data.m_nEntIndex = pEntity->entindex();
#endif
		data.m_fFlags |= PARTICLE_DISPATCH_FROM_ENTITY;
		data.m_nDamageType = PATTACH_CUSTOMORIGIN;
	}
	else
	{
#ifdef CLIENT_DLL
		data.m_hEntity = NULL;
#else
		data.m_nEntIndex = 0;
#endif
	}

	if ( bUseColors )
	{
		data.m_bCustomColors = true;
		data.m_CustomColors.m_vecColor1 = vecColor1;
		data.m_CustomColors.m_vecColor2 = vecColor2;
	}

	DispatchEffect( "ParticleEffect", data );
}


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void DispatchParticleEffect( const char *pszParticleName, Vector vecOrigin, QAngle vecAngles, CBaseEntity *pEntity )
{
	int iIndex = GetParticleSystemIndex( pszParticleName );
	DispatchParticleEffect( iIndex, vecOrigin, vecOrigin, vecAngles, pEntity );
}

//-----------------------------------------------------------------------------
// Purpose: Yet another overload, lets us supply vecStart
//-----------------------------------------------------------------------------
void DispatchParticleEffect( const char *pszParticleName, Vector vecOrigin, Vector vecStart, QAngle vecAngles, CBaseEntity *pEntity )
{
	int iIndex = GetParticleSystemIndex( pszParticleName );
	DispatchParticleEffect( iIndex, vecOrigin, vecStart, vecAngles, pEntity );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void StopParticleEffects( CBaseEntity *pEntity )
{
	CEffectData	data;

	if ( pEntity )
	{
#ifdef CLIENT_DLL
		data.m_hEntity = pEntity;
#else
		data.m_nEntIndex = pEntity->entindex();
#endif
	}

#ifdef GAME_DLL
	CReliableBroadcastRecipientFilter filter;
	DispatchEffect( "ParticleEffectStop", data, filter );
#else
	DispatchEffect( "ParticleEffectStop", data );
#endif
}

#ifndef CLIENT_DLL

	extern CBaseEntity *GetNextCommandEntity( CBasePlayer *pPlayer, const char *name, CBaseEntity *ent );

	ConVar particle_test_file( "particle_test_file", "", FCVAR_CHEAT, "Name of the particle system to dynamically spawn" );
	ConVar particle_test_attach_mode( "particle_test_attach_mode", "follow_attachment", FCVAR_CHEAT, "Possible Values: 'start_at_attachment', 'follow_attachment', 'start_at_origin', 'follow_origin'" );
	ConVar particle_test_attach_attachment( "particle_test_attach_attachment", "0", FCVAR_CHEAT, "Attachment index for attachment mode" );

	void Particle_Test_Start( CBasePlayer* pPlayer, const char *name, bool bStart )
	{
		if ( !pPlayer )
			return;

		int iAttachType = GetAttachTypeFromString( particle_test_attach_mode.GetString() );

		if ( iAttachType < 0 )
		{
			Warning( "Invalid attach type specified for particle_test in cvar 'particle_test_attach_mode.\n" );
			return;
		}

		int iAttachmentIndex = particle_test_attach_attachment.GetInt();

		const char *pszParticleFile = particle_test_file.GetString();

		CBaseEntity *pEntity = NULL;
		while ( (pEntity = GetNextCommandEntity( pPlayer, name, pEntity )) != NULL )
		{
			/* 
			Fire the test particle system on this entity
			*/

			DispatchParticleEffect( 
				pszParticleFile,
				(ParticleAttachment_t)iAttachType,
				pEntity,
				iAttachmentIndex,
				true );				// stops existing particle systems
		}
	}

	void CC_Particle_Test_Start( const CCommand& args )
	{
		Particle_Test_Start( UTIL_GetCommandClient(), args[1], true );
	}
	static ConCommand particle_test_start("particle_test_start", CC_Particle_Test_Start, "Dispatches the test particle system with the parameters specified in particle_test_file,\n particle_test_attach_mode and particle_test_attach_param on the entity the player is looking at.\n\tArguments:   	{entity_name} / {class_name} / no argument picks what player is looking at ", FCVAR_CHEAT);


	void Particle_Test_Stop( CBasePlayer* pPlayer, const char *name, bool bStart )
	{
		if ( !pPlayer )
			return;

		CBaseEntity *pEntity = NULL;
		while ( (pEntity = GetNextCommandEntity( pPlayer, name, pEntity )) != NULL )
		{
			//Stop all particle systems on the selected entity
			DispatchParticleEffect( "", PATTACH_ABSORIGIN, pEntity, 0, true );
		}
	}

	void CC_Particle_Test_Stop( const CCommand& args )
	{
		Particle_Test_Stop( UTIL_GetCommandClient(), args[1], false );
	}
	static ConCommand particle_test_stop("particle_test_stop", CC_Particle_Test_Stop, "Stops all particle systems on the selected entities.\n\tArguments:   	{entity_name} / {class_name} / no argument picks what player is looking at ", FCVAR_CHEAT);

#endif	//!CLIENT_DLL

