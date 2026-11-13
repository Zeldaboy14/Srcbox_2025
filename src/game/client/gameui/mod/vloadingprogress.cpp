//========= Copyright © 1996-2008, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
//=====================================================================================//
#include "cbase.h"
#include "VLoadingProgress.h"
#include "../EngineInterface.h"
#include "vgui_controls/Label.h"
#include "vgui_controls/ProgressBar.h"
#include "vgui/ISurface.h"
#include "vgui/ILocalize.h"
#include "vgui_controls/Image.h"
#include "vgui_controls/ImagePanel.h"
#include "../gameui_util.h"
#include "KeyValues.h"
#include "fmtstr.h"
#include "FileSystem.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

using namespace vgui;
using namespace BaseModUI;

static bool IsAvatarFemale( int iAvatar )
{
	return ( iAvatar == 1 ); // Zoey or Producer
}

//=============================================================================
LoadingProgress::LoadingProgress(Panel *parent, const char *panelName, LoadingWindowType eLoadingType ):
	BaseClass( parent, panelName, false, false, false ),
	m_pDefaultPosterDataKV( NULL ),
	m_LoadingWindowType( eLoadingType )
{
	memset( m_szGameMode, 0, sizeof( m_szGameMode ) );
	m_szLayoutGamemode[0] = '\0';
	m_szBGTexture[0] = '\0';
	m_nBGTextureID = -1;
	m_flNextGamemodeCheck = 0.0f;

	if ( eLoadingType == LWT_LOADINGPLAQUE )
	{
		MakePopup( false );
	}

	SetDeleteSelfOnClose( true );
	SetProportional( true );

	m_flPeakProgress = 0.0f;
	m_pProTotalProgress = NULL;
	m_pWorkingAnim = NULL;
	m_LoadingType = LT_UNDEFINED;

	m_pBGImage = NULL;
	m_pPoster = NULL;

	m_pFooter = NULL;

	// purposely not pre-caching the poster images
	// as they do not appear in-game, and are 1MB each, we will demand load them and ALWAYS discard them
	m_pMissionInfo = NULL;
	m_pChapterInfo = NULL;
	m_botFlags = 0xFF;
	
	memset( m_PlayerNames, 0, sizeof( m_PlayerNames ) );
    //const char *pPlayerNames[NUM_LOADING_CHARACTERS] = { "#L4D360UI_NamVet", "#L4D360UI_TeenGirl", "#L4D360UI_Biker", "#L4D360UI_Manager" };
	const char *pPlayerNames[NUM_LOADING_CHARACTERS] = { "#L4D360UI_Coach", "#L4D360UI_Mechanic", "#L4D360UI_Producer", "#L4D360UI_Gambler" };
	//const char *pPlayerNames[NUM_LOADING_CHARACTERS] = { "", "", "", "" };
	for ( int k = 0; k < NUM_LOADING_CHARACTERS; ++ k )
		Q_strncpy( m_PlayerNames[k], pPlayerNames[k], MAX_PLAYER_NAME_LENGTH );

	m_textureID_LoadingBar = -1;
	m_textureID_LoadingBarBG = -1;
	m_textureID_DefaultPosterImage = -1;

	m_bDrawBackground = false;
	m_bDrawPoster = true;
	m_bDrawProgress = false;
	m_bDrawSpinner = false;
	m_bFullscreenPoster = false;

	m_flLastEngineTime = 0;
	m_nSpinnerFrame = 0;

	// marked to indicate the controls exist
	m_bValid = false;

	MEM_ALLOC_CREDIT();
	m_pDefaultPosterDataKV = new KeyValues( "DefaultPosterData" );
	if ( !m_pDefaultPosterDataKV->LoadFromFile( g_pFullFileSystem, "resource/UI/BaseModUI/LoadingPosterDefault.res", "MOD" ) )
	{
		DevWarning( "Failed to load default poster information!\n" );
		m_pDefaultPosterDataKV->deleteThis();
		m_pDefaultPosterDataKV = NULL;
	}

	// Srcbox: pick the gamemode's layout now, before ApplySchemeSettings loads the .res
	char szGamemode[64];
	GetCurrentGamemode( szGamemode, sizeof( szGamemode ) );
	SelectGamemodeLayout( szGamemode );
}

//=============================================================================
LoadingProgress::~LoadingProgress()
{
	if ( m_pDefaultPosterDataKV )
		m_pDefaultPosterDataKV->deleteThis();
	m_pDefaultPosterDataKV = NULL;
}

//=============================================================================
void LoadingProgress::OnThink()
{
	CheckGamemodeChanged();
	UpdateWorkingAnim();
}

//=============================================================================
void LoadingProgress::OnCommand(const char *command)
{
}

//=============================================================================
void LoadingProgress::ApplySchemeSettings( IScheme *pScheme )
{
	// Srcbox: the base class (re)loads the .res, which deletes and recreates every child.
	// Drop the cached child pointers first so nothing can touch a deleted control, and
	// SetupControlStates() fetches the new ones below.
	ClearControlPointers();
	m_bValid = false;

	// will cause the controls to be instanced
	BaseClass::ApplySchemeSettings( pScheme );

	SetPaintBackgroundEnabled( true );
	
	// now have controls, can now do further initing
	m_bValid = true;

	// (the loading bar textures are picked per gamemode in ApplyGamemodeVisuals, below)

	// need to get the default image loaded now
	// find or create pattern
	// Purposely not freeing these, need this image to be resident always. We flip to
	// this image on a sign out during loading and cannot bring it into	existence then.
//#define SUPPORT_DEFAULT_LOADING_POSTER 1
#if defined ( SUPPORT_DEFAULT_LOADING_POSTER )
	if ( m_pDefaultPosterDataKV )
	{
		static ConVarRef mat_xbox_iswidescreen( "mat_xbox_iswidescreen" );
		bool bIsWidescreen = mat_xbox_iswidescreen.GetBool();
		bool bFullscreenPoster = m_pDefaultPosterDataKV->GetBool( "fullscreen", false );
		const char *pszPosterImage = ( bFullscreenPoster && bIsWidescreen ) ? m_pDefaultPosterDataKV->GetString( "posterImage_widescreen" ) : m_pDefaultPosterDataKV->GetString( "posterImage" );

		// have to do this to mimic what the bowels of the scheme manager does with bitmaps
		bool bPrependVguiFix = V_strnicmp( pszPosterImage, "vgui", 4 ) != 0;
		CFmtStr sPosterImageFmt( "%s%s", ( bPrependVguiFix ? "vgui/" : "" ), pszPosterImage );
		pszPosterImage = sPosterImageFmt;

		m_textureID_DefaultPosterImage = vgui::surface()->DrawGetTextureId( pszPosterImage );
		if ( m_textureID_DefaultPosterImage == -1 )
		{
			m_textureID_DefaultPosterImage = vgui::surface()->CreateNewTextureID();
			vgui::surface()->DrawSetTextureFile( m_textureID_DefaultPosterImage, pszPosterImage, true, false );	
		}
	}
#endif

	SetupControlStates();
	ApplyGamemodeVisuals();
}

void LoadingProgress::Close()
{
	// hint to force any of the movie posters out of memory (purposely ignoring specific map search)
	// if the image is unreferenced (as it should be), it will evict, if already evicted or empty, then harmless
	if ( m_pPoster )
	{
		m_pPoster->EvictImage();
	}

	if ( m_pBGImage && ( m_LoadingType == LT_POSTER ) )
	{
		m_pBGImage->EvictImage();
	}
		
	BaseClass::Close();
}

//=============================================================================
// this is where the spinner gets updated.
void LoadingProgress::UpdateWorkingAnim()
{
	if ( m_pWorkingAnim && ( m_bDrawSpinner || m_LoadingType == LT_TRANSITION ) )
	{
		// clock the anim at 10hz
		float time = Plat_FloatTime();
		if ( ( m_flLastEngineTime + 0.1f ) < time )
		{
			m_flLastEngineTime = time;
			m_pWorkingAnim->SetFrame( ++m_nSpinnerFrame );
		}
	}
}

//=============================================================================
void LoadingProgress::SetProgress( float progress )
{
	if ( m_pProTotalProgress && m_bDrawProgress )
	{
		if ( progress > m_flPeakProgress )
		{
			m_flPeakProgress = progress;
		}
		m_pProTotalProgress->SetProgress( m_flPeakProgress );
	}
	
	CheckGamemodeChanged();
	UpdateWorkingAnim();
}

//=============================================================================
float LoadingProgress::GetProgress()
{
	float retVal = -1.0f;

	if ( m_pProTotalProgress )
	{
		retVal = m_pProTotalProgress->GetProgress();
	}

	return retVal;
}

#include "hl2mp_gamerules.h"

// Srcbox: the full-screen image behind the loading screen (also faded out over the menu's background map
// by CBaseModPanel once the startup load is done)
const char *LoadingProgress::GetBackgroundImageName()
{
	return "vgui/appchooser/background_hl2";
}

void LoadingProgress::PaintBackground()
{
	int screenWide, screenTall;
	surface()->GetScreenSize( screenWide, screenTall );
	static int s_nBGTextureID = -1;

	//vgui::ImagePanel *pBackground = dynamic_cast< vgui::ImagePanel* >(FindChildByName("Poster"));
	if ( m_bDrawBackground && m_pBGImage )
	{
		int x, y, wide, tall;
		m_pBGImage->GetBounds( x, y, wide, tall );
		surface()->DrawSetColor( Color( 255, 255, 255, 255 ) );
		// Srcbox: this created a new texture ID and reloaded the image every frame (leaking IDs).
		// The gamemode's background (ApplyGamemodeVisuals) wins, else the default one.
		int nTexture = m_nBGTextureID;
		if ( nTexture == -1 )
		{
			if ( s_nBGTextureID == -1 )
			{
				s_nBGTextureID = vgui::surface()->CreateNewTextureID();
				vgui::surface()->DrawSetTextureFile( s_nBGTextureID, GetBackgroundImageName(), true, false );
			}
			nTexture = s_nBGTextureID;
		}
		surface()->DrawSetTexture( nTexture );
		//surface()->DrawSetTexture( m_pBGImage->GetImage()->GetID() );
		surface()->DrawTexturedRect( x, y, x+wide, y+tall );
	}

	if ( m_bDrawPoster && m_pPoster && m_pPoster->GetImage() )
	{
		//Disabled. It seems swarms team runs a check, and does this really weird. This may be original, but breaks atm.
		/*if (m_bFullscreenPoster)
		{
			surface()->DrawSetColor( Color( 255, 255, 255, 255 ) );
			surface()->DrawSetTexture( m_pPoster->GetImage()->GetID() );
			surface()->DrawTexturedRect( 0, 0, screenWide, screenTall );
		}*/
		//else
		//{
			// fill black
			//surface()->DrawSetColor( Color( 0, 0, 0, 255 ) );
			//surface()->DrawFilledRect( 0, 0, screenWide, screenTall );

			// overlay poster
			int x, y, wide, tall;
			m_pPoster->GetBounds( x, y, wide, tall );
			surface()->DrawSetColor( Color( 255, 255, 255, 255 ) );
			surface()->DrawSetTexture( m_pPoster->GetImage()->GetID() );
			surface()->DrawTexturedRect( x, y, x+wide, y+tall );
		//}		
	}

	if ( m_bDrawPoster && m_pFooter )
	{
		int screenWidth, screenHeight;
		CBaseModPanel::GetSingleton().GetSize( screenWidth, screenHeight );

		int x, y, wide, tall;
		m_pFooter->GetBounds( x, y, wide, tall );
		surface()->DrawSetColor( m_pFooter->GetBgColor() );
		surface()->DrawFilledRect( 0, y, x+screenWidth, y+tall );
	}

	// this is where the spinner draws
	// (a gamemode .res can leave WorkingAnim without an image; GetImage() is NULL then)
	bool bRenderSpinner = ( m_bDrawSpinner && m_pWorkingAnim && m_pWorkingAnim->GetImage() );
	Panel *pWaitscreen = CBaseModPanel::GetSingleton().GetWindow( WT_GENERICWAITSCREEN );
	if ( pWaitscreen && pWaitscreen->IsVisible() && ( m_LoadingWindowType == LWT_BKGNDSCREEN ) )
		bRenderSpinner = ( m_pWorkingAnim && m_pWorkingAnim->GetImage() );	// Don't render spinner if the progress screen is displaying progress
	if ( bRenderSpinner  )
	{
		int x, y, wide, tall;

		wide = tall = scheme()->GetProportionalScaledValue( 45 );
		x = scheme()->GetProportionalScaledValue( 45 ) - wide/2;
		y = screenTall - scheme()->GetProportionalScaledValue( 32 ) - tall/2;

		surface()->DrawSetColor( Color( 255, 255, 255, 255 ) );
		surface()->DrawSetTexture( m_pWorkingAnim->GetImage()->GetID() );
		surface()->DrawTexturedRect( x, y, x+wide, y+tall );
	}

	if ( m_bDrawProgress && m_pProTotalProgress )
	{
		int x, y, wide, tall;
		m_pProTotalProgress->GetBounds( x, y, wide, tall );

		int iScreenWide, iScreenTall;
		surface()->GetScreenSize( iScreenWide, iScreenTall );

		float f = m_pProTotalProgress->GetProgress();
		f = clamp( f, 0, 1.0f );

		// Textured bar
		surface()->DrawSetColor( Color( 255, 255, 255, 255 ) );

		// Texture BG
		surface()->DrawSetTexture( m_textureID_LoadingBarBG );
		surface()->DrawTexturedRect( x, y, x + wide, y + tall );

		surface()->DrawSetTexture( m_textureID_LoadingBar );

		// YWB 10/13/2009:  If we don't crop the texture coordinate down then we will see a jitter of the texture as the texture coordinate and the rounded width 
		//  alias

		int nIntegerWide = f * wide;		
		float flUsedFrac = (float)nIntegerWide / (float)wide;
		

		surface()->DrawTexturedSubRect( x, y, x + nIntegerWide, y + tall, 0, 0, flUsedFrac, 1 );
	}

	// Need to call this periodically to collect sign in and sign out notifications,
	// do NOT dispatch events here in the middle of loading and rendering!
	if ( ThreadInMainThread() )
	{
		XBX_ProcessEvents();
	}
}

//=============================================================================
// Must be called first. Establishes the loading style
//=============================================================================
void LoadingProgress::SetLoadingType( LoadingProgress::LoadingType loadingType )
{
	m_LoadingType = loadingType;

	// the first time initing occurs during ApplySchemeSettings() or if the panel is deleted
	// if the panel is re-used, this is for the second time the panel gets used
	SetupControlStates();
}

//=============================================================================
LoadingProgress::LoadingType LoadingProgress::GetLoadingType()
{
	return m_LoadingType;
}

//=============================================================================
void LoadingProgress::SetupControlStates()
{
	m_flPeakProgress = 0.0f;

	if ( !m_bValid )
	{
		// haven't been functionally initialized yet
		// can't set or query control states until they get established
		return;
	}

	m_bDrawBackground = false;
	m_bDrawPoster = false;
	m_bDrawSpinner = false;
	m_bDrawProgress = false;

	switch( m_LoadingType )
	{
	case LT_MAINMENU:
		m_bDrawBackground = true;
		m_bDrawProgress = true;
		m_bDrawSpinner = true;
		break;
	case LT_TRANSITION:
		m_bDrawBackground = true;
		m_bDrawProgress = true;
		m_bDrawSpinner = true;
		// the transition screen shows stats and owns all the drawing
		// spinner needs to be drawn as child of the window that is shown while loading
		break;
	case LT_POSTER:
		m_bDrawBackground = true;
		m_bDrawPoster = false;
		m_bDrawProgress = true;
		m_bDrawSpinner = true;
		break;
	default:
		break;
	}

	// Srcbox: the Orange Box loading bar (gameui_interface.cpp) is drawn on top instead
	if ( UsesClassicLoadingBar() )
	{
		m_bDrawProgress = false;
		m_bDrawSpinner = false;
	}

	m_pFooter = dynamic_cast< vgui::EditablePanel* >( FindChildByName( "CampaignFooter" ) );

	m_pBGImage = dynamic_cast< vgui::ImagePanel* >( FindChildByName( "BGImage" ) );
	if ( m_pBGImage )
	{
		// set the correct background image
		if ( m_LoadingType == LT_POSTER )
		{
			//m_pBGImage->SetImage( "../vgui/LoadingScreen_background" );
		}
		else
		{
			int screenWide, screenTall;
			surface()->GetScreenSize( screenWide, screenTall );
			char filename[MAX_PATH];
			V_snprintf( filename, sizeof( filename ), "console/background01" ); // TODO: engine->GetStartupImage( filename, sizeof( filename ), screenWide, screenTall );
			m_pBGImage->SetImage( CFmtStr( "../%s", filename ) );
		}

		// we will custom draw
		m_pBGImage->SetVisible( false );
	}

	m_pPoster = dynamic_cast< vgui::ImagePanel* >( FindChildByName( "Poster" ) );
	if ( m_pPoster )
	{
		// we will custom draw
		m_pPoster->SetVisible( false );
	}
	
	m_pProTotalProgress = dynamic_cast< vgui::ProgressBar* >( FindChildByName( "ProTotalProgress" ) );
	if ( m_pProTotalProgress )
	{
		// we will custom draw
		m_pProTotalProgress->SetVisible( false );
	}

	m_pWorkingAnim = dynamic_cast< vgui::ImagePanel* >( FindChildByName( "WorkingAnim" ) );
	if ( m_pWorkingAnim )
	{
		// we will custom draw
		m_pWorkingAnim->SetVisible( false );
	}

	vgui::Label *pLoadingLabel = dynamic_cast< vgui::Label *>( FindChildByName( "LoadingText" ) );
	if ( pLoadingLabel )
	{
		pLoadingLabel->SetVisible( m_bDrawProgress );
	}

	ApplyLoadingBarLayout();

	SetControlVisible( "PlayerNames", m_bDrawPoster );
	SetControlVisible( "StarringLabel", m_bDrawPoster );
	// Srcbox: gamemode name on everything but the startup/menu load (a gamemode .res can
	// leave the control out entirely to hide it)
	SetControlVisible( "GameModeLabel", m_LoadingType != LT_MAINMENU && m_szGameMode[0] != '\0' );

	if ( m_LoadingType == LT_POSTER )
	{
		SetupPoster();
	}

	// Hold on to start frame slightly
	m_flLastEngineTime = Plat_FloatTime() + 0.2f;
}

void LoadingProgress::SetPosterData( KeyValues *pMissionInfo, KeyValues *pChapterInfo, const char **pPlayerNames, unsigned int botFlags, const char *pszGameMode )
{
	m_botFlags = botFlags;
	m_pMissionInfo = pMissionInfo;
	m_pChapterInfo = pChapterInfo;

	RearrangeNames( pMissionInfo->GetString( "poster/character_order", NULL ), pPlayerNames );

	//Q_snprintf( m_szGameMode, sizeof( m_szGameMode ), "#L4D360UI_Loading_GameMode_%s", pszGameMode );
	//todo: get this localized to the specific game mode in srcbox
	//Q_snprintf(m_szGameMode, sizeof(m_szGameMode), "#Srcbox_Loading_Gamemode_sandbox");
}

//rearrange names to match the poster character order... sigh..
void LoadingProgress::RearrangeNames( const char *pszCharacterOrder, const char **pPlayerNames )
{
	int iNewOrder[NUM_LOADING_CHARACTERS]; 
	char m_SortPlayerNames[NUM_LOADING_CHARACTERS][MAX_PLAYER_NAME_LENGTH];

	for ( int k = 0; k < NUM_LOADING_CHARACTERS; ++ k )
	{
		iNewOrder[k] = k;
		if ( pPlayerNames[k] )
			Q_strncpy( m_PlayerNames[k], pPlayerNames[k], MAX_PLAYER_NAME_LENGTH );

		Q_strncpy( m_SortPlayerNames[k], m_PlayerNames[k], MAX_PLAYER_NAME_LENGTH );
	}

	if ( pszCharacterOrder )
	{
		char szOrder[MAX_PATH];
		Q_strncpy( szOrder, pszCharacterOrder, sizeof( szOrder ) );

		char	*p = szOrder;
		int i = 0;

		p = strtok( p, ";" );

		const char * const arrCharacterNamesInConfig[ NUM_LOADING_CHARACTERS ] = { "gambler", "producer", "coach", "mechanic" };

		while ( p != NULL && *p )
		{
			for ( int k = 0; k < NUM_LOADING_CHARACTERS; ++ k )
			{
				if ( !Q_stricmp( p, arrCharacterNamesInConfig[k] ) )
				{
					iNewOrder[k] = i;
					break;
				}
			}

			i++;

			p = strtok( NULL, ";" );
		}
	}

	for ( int k = 0; k < NUM_LOADING_CHARACTERS; ++ k )
	{
		Q_strncpy( m_PlayerNames[k], m_SortPlayerNames[iNewOrder[k]], MAX_PLAYER_NAME_LENGTH );
	}
}

//=============================================================================
bool LoadingProgress::ShouldShowPosterForLevel( KeyValues *pMissionInfo, KeyValues *pChapterInfo )
{
	// Do not show loading poster for credits
	if ( pMissionInfo && !Q_stricmp( pMissionInfo->GetString( "name" ), "credits" ) )
		return false;

	if ( pMissionInfo )
	{
		char const *szPosterImage = pMissionInfo->GetString( "poster/posterImage", NULL );
		if ( szPosterImage && *szPosterImage )
			// If the campaign specifies a valid poster, then we'll use it
			return true;
	}

	// All other campaigns can fall back to a default poster
	return ( m_pDefaultPosterDataKV != NULL );
}

//=============================================================================
void LoadingProgress::SetupPoster( void )
{
	int i;
	
	bool bNamesVisible = false;
	vgui::ImagePanel *pPoster = dynamic_cast< vgui::ImagePanel* >( FindChildByName( "Poster" ) );
	if ( pPoster )
	{ 
		int screenWide, screenTall;
		surface()->GetScreenSize( screenWide, screenTall );
		float aspectRatio = (float)screenWide/(float)screenTall;
		bool bIsWidescreen = aspectRatio >= 1.5999f;

		const char *pszPosterImage;
		int nChosenLoadingImage = RandomInt( 1, 4 );
		switch( nChosenLoadingImage )
		{
			//case 1: pszPosterImage = ( m_bFullscreenPoster && bIsWidescreen ) ? "swarm/loading/BGFX01_wide" : "swarm/loading/BGFX01"; break;
			//case 2: pszPosterImage = ( m_bFullscreenPoster && bIsWidescreen ) ? "swarm/loading/BGFX02_wide" : "swarm/loading/BGFX02"; break;
			//case 3: pszPosterImage = ( m_bFullscreenPoster && bIsWidescreen ) ? "swarm/loading/BGFX03_wide" : "swarm/loading/BGFX03"; break;
			case 1: pszPosterImage = (m_bFullscreenPoster && bIsWidescreen) ? "loadingscreen_deadair2" : "loadingscreen_deadair"; break;
			case 2: pszPosterImage = (m_bFullscreenPoster && bIsWidescreen) ? "loadingscreen_deadair2" : "loadingscreen_deadair"; break;
			case 3: pszPosterImage = (m_bFullscreenPoster && bIsWidescreen) ? "loadingscreen_deadair2" : "loadingscreen_deadair"; break;
			case 4:
			//default: pszPosterImage = ( m_bFullscreenPoster && bIsWidescreen ) ? "swarm/loading/BGFX04_wide" : "swarm/loading/BGFX04"; break;
			default: pszPosterImage = (m_bFullscreenPoster && bIsWidescreen) ? "loadingscreen_deadair2" : "loadingscreen_deadair"; break;
		}

		pPoster->MoveToFront();

		// if the image was cached this will just hook it up, otherwise it will load it
		pPoster->SetImage( pszPosterImage );

		if ( pPoster->GetImage() )
		{
			bNamesVisible = false;
		}
	}

	SetControlVisible( "LocalizedCampaignName", false );
	SetControlVisible( "LocalizedCampaignTagline", false );

	wchar_t szPlayerNames[MAX_PATH];
	Q_memset( szPlayerNames, 0, sizeof( szPlayerNames ) );

	int nNumNames = 0;
	for ( i=0;i<NUM_LOADING_CHARACTERS;i++ )
	{
		if ( !m_PlayerNames[i] || !m_PlayerNames[i][0] )
		{
			continue;
		}

		if ( nNumNames != 0 )
		{
			wcsncat( szPlayerNames, L", ", sizeof( szPlayerNames ) );
		}
		wchar_t szName[64];

		if ( m_PlayerNames[i] && m_PlayerNames[i][0] == '#' )
		{
			wchar_t *pName = g_pVGuiLocalize->Find( m_PlayerNames[i] );

			if ( pName == NULL )
			{
				g_pVGuiLocalize->ConvertANSIToUnicode( m_PlayerNames[i], szName, sizeof( szPlayerNames ) );
			}
			else
			{
				Q_wcsncpy( szName, pName, sizeof( szName ) );
			}
			nNumNames++;
		}
		else
		{
			g_pVGuiLocalize->ConvertANSIToUnicode( m_PlayerNames[i], szName, sizeof( szPlayerNames ) );
			nNumNames++;
		}

		wcsncat( szPlayerNames, szName, sizeof( szPlayerNames ) );
	}

	if ( nNumNames != 0 )
	{
		wcsncat( szPlayerNames, L".", sizeof( szPlayerNames ) );
	}

	SetControlString( "GameModeLabel", m_szGameMode );

	SetControlVisible( "PlayerNames", ( nNumNames > 1 ) );

	SetControlVisible( "StarringLabel", ( nNumNames > 1 ) );
	SetControlVisible( "GameModeLabel", m_szGameMode[0] != '\0' );

	//SetControlString( "playernames", szPlayerNames );

	SetControlEnabled( "LoadingTipPanel", false );
}

//=============================================================================
// Srcbox: per-gamemode loading screen
//
// For the gamemode in the "gamemode" convar (replicated, so a remote server's value
// arrives with the server info, before the client loads the map):
//   layout:     resource/ui/basemodui/loadingprogress_<gamemode>.res  (else loadingprogress.res)
//   background: set in code like ASW/L4D do (the .res BGImage is only a placement):
//               materials/vgui/loading/<gamemode>.vmt, else the default (GetBackgroundImageName)
//   label:      GameModeLabel = #Srcbox_Gamemode_<gamemode> if localized, else the folder name
//   bar:        s_GamemodeLoadingBars below picks a style from s_LoadingBarStyles
//               (classic / srcbox / swarm / terror); unlisted gamemodes follow ui_loadingbar_classic
//
// The layout is chosen in the constructor, before the .res is first loaded. If the
// gamemode changes while the screen is up (joining a server: the window opens just before
// the server's convars arrive) the .res is reloaded through InvalidateLayout, which runs
// ApplySchemeSettings again. That path clears the cached child pointers before the reload.
//=============================================================================
// one texture ID per material for the whole session, never one per frame
static int FindOrCreateLoadingTexture( const char *pszMaterial )
{
	int nID = vgui::surface()->DrawGetTextureId( pszMaterial );
	if ( nID == -1 )
	{
		nID = vgui::surface()->CreateNewTextureID();
		vgui::surface()->DrawSetTextureFile( nID, pszMaterial, true, false );
	}
	return nID;
}

//-----------------------------------------------------------------------------
// Loading bar styles. "classic" is the Orange Box bar drawn by CClassicLoadingBar
// (gameui_interface.cpp); the rest are this screen's own bar with their own textures,
// spinner and placement. Swarm and L4D2 ship their bar as vgui/loadingbar(_bg) under the
// same name, so Srcbox keeps its own copies in materials/vgui/loading/.
//-----------------------------------------------------------------------------
struct LoadingBarStyle_t
{
	const char	*m_pszName;
	bool		m_bClassic;
	const char	*m_pszBar;			// materials (relative to materials/)
	const char	*m_pszBarBG;
	const char	*m_pszSpinner;		// ImagePanel image (relative to materials/vgui/), NULL = the .res one
	bool		m_bCornerLayout;	// bar and "Loading" text in the lower right like ASW/L4D2, else the .res placement
};

static const LoadingBarStyle_t s_LoadingBarStyles[] =
{
	//  name		classic	bar								bar bg								spinner						corner
	{ "classic",	true,	NULL,							NULL,								NULL,						false },
	{ "srcbox",		false,	"vgui/loadingbar_orangebox",	"vgui/loadingbar_bg_orangebox",		NULL,						false },	// this screen's bar with the .res layout
	{ "swarm",		false,	"vgui/loading/bar_swarm",		"vgui/loading/bar_swarm_bg",		"loading/spinner_swarm",	true },
	{ "terror",		false,	"vgui/loading/bar_terror",		"vgui/loading/bar_terror_bg",		"loading/spinner_terror",	true },
};

//-----------------------------------------------------------------------------
// Which style each gamemode uses. Gamemodes not listed here get "classic", or "srcbox"
// with ui_loadingbar_classic 0. Names are the gamemode folder names, lowercase.
//-----------------------------------------------------------------------------
struct GamemodeLoadingBar_t
{
	const char	*m_pszGamemode;
	const char	*m_pszStyle;
};

static const GamemodeLoadingBar_t s_GamemodeLoadingBars[] =
{
	{ "base_hl2",	"classic" },
	{ "terror",		"terror" },
};

extern ConVar ui_loadingbar_classic;

static const LoadingBarStyle_t *FindLoadingBarStyle( const char *pszName )
{
	for ( int i = 0; i < ARRAYSIZE( s_LoadingBarStyles ); i++ )
	{
		if ( !V_stricmp( pszName, s_LoadingBarStyles[ i ].m_pszName ) )
			return &s_LoadingBarStyles[ i ];
	}
	return NULL;
}

const LoadingBarStyle_t *LoadingProgress::GetLoadingBarStyle( void ) const
{
	for ( int i = 0; i < ARRAYSIZE( s_GamemodeLoadingBars ); i++ )
	{
		if ( !V_stricmp( m_szLayoutGamemode, s_GamemodeLoadingBars[ i ].m_pszGamemode ) )
		{
			const LoadingBarStyle_t *pStyle = FindLoadingBarStyle( s_GamemodeLoadingBars[ i ].m_pszStyle );
			if ( pStyle )
				return pStyle;
			break;	// misspelled style: fall through to the default
		}
	}
	return FindLoadingBarStyle( ui_loadingbar_classic.GetBool() ? "classic" : "srcbox" );
}

bool LoadingProgress::UsesClassicLoadingBar( void ) const
{
	return GetLoadingBarStyle()->m_bClassic;
}

// Places the screen's bar, "Loading" text and spinner for styles that don't use the .res
// placement. Values are the ASW/L4D2 loadingprogress.res ones (640x480 proportional).
void LoadingProgress::ApplyLoadingBarLayout( void )
{
	const LoadingBarStyle_t *pStyle = GetLoadingBarStyle();

	if ( m_pWorkingAnim && pStyle->m_pszSpinner )
	{
		m_pWorkingAnim->SetImage( pStyle->m_pszSpinner );
		m_nSpinnerFrame = 0;
	}

	if ( !pStyle->m_bCornerLayout )
		return;

	int screenWide, screenTall;
	surface()->GetScreenSize( screenWide, screenTall );

	if ( m_pProTotalProgress )
	{
		m_pProTotalProgress->SetBounds( screenWide - scheme()->GetProportionalScaledValue( 158 ),
			screenTall - scheme()->GetProportionalScaledValue( 45 ),
			scheme()->GetProportionalScaledValue( 135 ), scheme()->GetProportionalScaledValue( 33 ) );
	}

	vgui::Label *pLoadingLabel = dynamic_cast< vgui::Label * >( FindChildByName( "LoadingText" ) );
	if ( pLoadingLabel )
	{
		pLoadingLabel->SetAutoResize( vgui::Panel::PIN_TOPLEFT, vgui::Panel::AUTORESIZE_NO, 0, 0, 0, 0 );
		pLoadingLabel->SetBounds( screenWide - scheme()->GetProportionalScaledValue( 223 ),
			screenTall - scheme()->GetProportionalScaledValue( 55 ),
			scheme()->GetProportionalScaledValue( 200 ), scheme()->GetProportionalScaledValue( 20 ) );
		pLoadingLabel->SetContentAlignment( vgui::Label::a_east );
		pLoadingLabel->SetText( "#L4D360UI_Loading" );

		vgui::IScheme *pScheme = scheme()->GetIScheme( GetScheme() );
		if ( pScheme )
			pLoadingLabel->SetFont( pScheme->GetFont( "DefaultBold", true ) );
	}
}

// Asked by CClassicLoadingBar (gameui_interface.cpp) every paint, so a gamemode switch
// while the screen is up (joining a server) switches the bar too.
bool LoadingProgress_UseClassicLoadingBar( void )
{
	LoadingProgress *pScreen = static_cast< LoadingProgress* >( CBaseModPanel::GetSingleton().GetWindow( WT_LOADINGPROGRESS ) );
	return pScreen ? pScreen->UsesClassicLoadingBar() : ui_loadingbar_classic.GetBool();
}

void LoadingProgress::GetCurrentGamemode( char *pszOut, int nOutSize )
{
	pszOut[0] = '\0';

	// not static: a ref made before the Lua convars register would stay invalid forever
	ConVarRef gamemodeVar( "gamemode", true );
	if ( !gamemodeVar.IsValid() )
		return;

	// the value can come from a server, so only keep characters that are safe in a file name
	const char *pszValue = gamemodeVar.GetString();
	int n = 0;
	for ( ; pszValue && *pszValue && n < nOutSize - 1; ++pszValue )
	{
		char c = *pszValue;
		if ( ( c >= 'a' && c <= 'z' ) || ( c >= '0' && c <= '9' ) || c == '_' || c == '-' )
			pszOut[ n++ ] = c;
		else if ( c >= 'A' && c <= 'Z' )
			pszOut[ n++ ] = c - 'A' + 'a';
		else
		{
			n = 0;	// anything else (paths, spaces): treat as no gamemode
			break;
		}
	}
	pszOut[ n ] = '\0';
}

void LoadingProgress::SelectGamemodeLayout( const char *pszGamemode )
{
	V_strncpy( m_szLayoutGamemode, pszGamemode, sizeof( m_szLayoutGamemode ) );

	char szRes[ sizeof( m_ResourceName ) ];
	szRes[0] = '\0';
	if ( pszGamemode[0] )
	{
		int nLen = V_snprintf( szRes, sizeof( szRes ), "Resource/UI/BaseModUI/LoadingProgress_%s.res", pszGamemode );
		if ( nLen <= 0 || nLen >= (int)sizeof( szRes ) || !g_pFullFileSystem->FileExists( szRes, "GAME" ) )
			szRes[0] = '\0';	// (too long for m_ResourceName, or no such file)
	}

	if ( !szRes[0] )
		V_strncpy( szRes, "Resource/UI/BaseModUI/LoadingProgress.res", sizeof( szRes ) );

	V_strncpy( m_ResourceName, szRes, sizeof( m_ResourceName ) );
}

void LoadingProgress::CheckGamemodeChanged( void )
{
	float flNow = Plat_FloatTime();
	if ( flNow < m_flNextGamemodeCheck )
		return;
	m_flNextGamemodeCheck = flNow + 0.25f;

	char szGamemode[64];
	GetCurrentGamemode( szGamemode, sizeof( szGamemode ) );
	if ( !V_strcmp( szGamemode, m_szLayoutGamemode ) )
		return;

	char szOldRes[ sizeof( m_ResourceName ) ];
	V_strncpy( szOldRes, m_ResourceName, sizeof( szOldRes ) );
	const LoadingBarStyle_t *pOldStyle = GetLoadingBarStyle();

	SelectGamemodeLayout( szGamemode );

	if ( m_bValid && ( V_stricmp( szOldRes, m_ResourceName ) || pOldStyle != GetLoadingBarStyle() ) )
	{
		// different .res or bar style: reload the .res on the next layout pass (never from
		// inside a paint), so the new style starts from the .res placement. The bar refills
		// to where the load is on the engine's next progress update.
		InvalidateLayout( false, true );
	}
	else
	{
		ApplyGamemodeVisuals();
	}
}

void LoadingProgress::ApplyGamemodeVisuals( void )
{
	// the screen's own bar textures (unused by the classic style, which draws its own)
	{
		const LoadingBarStyle_t *pStyle = GetLoadingBarStyle();
		m_textureID_LoadingBar = FindOrCreateLoadingTexture( pStyle->m_pszBar ? pStyle->m_pszBar : "vgui/loadingbar_orangebox" );
		m_textureID_LoadingBarBG = FindOrCreateLoadingTexture( pStyle->m_pszBarBG ? pStyle->m_pszBarBG : "vgui/loadingbar_bg_orangebox" );
	}

	// background
	m_szBGTexture[0] = '\0';
	if ( m_szLayoutGamemode[0] )
	{
		char szVMT[ MAX_PATH ];
		V_snprintf( szVMT, sizeof( szVMT ), "materials/vgui/loading/%s.vmt", m_szLayoutGamemode );
		if ( g_pFullFileSystem->FileExists( szVMT, "GAME" ) )
			V_snprintf( m_szBGTexture, sizeof( m_szBGTexture ), "vgui/loading/%s", m_szLayoutGamemode );
	}

	m_nBGTextureID = m_szBGTexture[0] ? FindOrCreateLoadingTexture( m_szBGTexture ) : -1;

	// label
	m_szGameMode[0] = '\0';
	if ( m_szLayoutGamemode[0] )
	{
		char szToken[ 96 ];
		V_snprintf( szToken, sizeof( szToken ), "#Srcbox_Gamemode_%s", m_szLayoutGamemode );
		if ( g_pVGuiLocalize->Find( szToken ) )
			V_strncpy( m_szGameMode, szToken, sizeof( m_szGameMode ) );
		else
			V_strncpy( m_szGameMode, m_szLayoutGamemode, sizeof( m_szGameMode ) );
	}

	if ( m_bValid )
	{
		SetControlString( "GameModeLabel", m_szGameMode );
		SetControlVisible( "GameModeLabel", m_LoadingType != LT_MAINMENU && m_szGameMode[0] != '\0' );
	}
}

void LoadingProgress::ClearControlPointers( void )
{
	m_pProTotalProgress = NULL;
	m_pWorkingAnim = NULL;
	m_pBGImage = NULL;
	m_pPoster = NULL;
	m_pFooter = NULL;
}
