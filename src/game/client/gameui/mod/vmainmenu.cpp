//========= Copyright � 1996-2008, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
//=====================================================================================//

#include "VMainMenu.h"
#include "nb_header_footer.h"
#include "../EngineInterface.h"
#include "VFooterPanel.h"
#include "VHybridButton.h"
#include "VFlyoutMenu.h"
#include "vGenericConfirmation.h"
#include "basemodpanel.h"
#include "../VGuiSystemModuleLoader.h"

#include "vgui/ILocalize.h"
#include "vgui_controls/Label.h"
#include "vgui_controls/Button.h"
#include "vgui_controls/Tooltip.h"
#include "vgui_controls/ImagePanel.h"
#include "vgui_controls/Image.h"

#include "steam/steam_api.h"
#include "materialsystem/materialsystem_config.h"

#include "ienginevgui.h"
#include "../basepanel.h"
#include "vgui/ISurface.h"
#include "tier0/icommandline.h"
#include "fmtstr.h"
#include "filesystem.h"
#include "cbase.h"
#include <cdll_client_int.h>
#include <KeyValues.h>
#include "iclientmode.h"

#include "vgui/IVGui.h"

#include "KeyValues.h"


// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

using namespace vgui;
using namespace BaseModUI;

//=============================================================================
// Enabled!!!! until we fix swarm ui, DO NOT DISABLE
static ConVar ui_old_options_menu( "ui_old_options_menu", "0", FCVAR_HIDDEN, "Brings up the old tabbed options dialog from Keyboard/Mouse when set to 1." );

void OpenGammaDialog( VPANEL parent );

//=============================================================================
MainMenu::MainMenu(Panel* parent, const char* panelName) :
	BaseClass(parent, panelName, true, false, false, false)
{
	SetProportional( true );
	SetTitle( "", false );
	SetMoveable( false );
	SetSizeable( false );

	SetLowerGarnishEnabled( false );

	m_iQuickJoinHelpText = MMQJHT_NONE;

#ifdef PLATFORM_64BITS
	// Main menu panel, which will handle the video background
	m_pMainMenuPanel = new CAnimatedBackgroundMovie(this, "BackgroundVideo");
#endif
}

//=============================================================================
MainMenu::~MainMenu()
{
	RemoveFrameListener( this );

#ifdef PLATFORM_64BITS
	if (m_pMainMenuPanel && m_pMainMenuPanel->IsVisible())
	{
		m_pMainMenuPanel->StopVideo();
		m_pMainMenuPanel->SetVisible(false);
		ConColorMsg(Color(255, 0, 0, 255), "MainMenu: Stopping video\n");
	}
#endif
}

// Re-open the main menu. Required to replication
CON_COMMAND(OpenMainMenu, "")
{
	CBaseModPanel::GetSingleton().OpenFrontScreen();
};

static void OpenMainMenuCancelCallback()
{
	CBaseModPanel::GetSingleton().CloseAllWindows();
	CBaseModPanel::GetSingleton().OpenFrontScreen();
}

//=============================================================================
void MainMenu::OnCommand( const char *command )
{
	bool bOpeningFlyout = false;
	if ( !Q_strcmp( command, "DeveloperCommentary" ) )
	{
		// Explain the rules of commentary
		GenericConfirmation* confirmation = 
			static_cast< GenericConfirmation* >( CBaseModPanel::GetSingleton().OpenWindow( WT_GENERICCONFIRMATION, this, false ) );

		GenericConfirmation::Data_t data;

		data.pWindowTitle = "#GAMEUI_CommentaryDialogTitle";
		data.pMessageText = "#L4D360UI_Commentary_Explanation";

		data.bOkButtonEnabled = true;
		data.pfnOkCallback = &AcceptCommentaryRulesCallback;
		data.bCancelButtonEnabled = true;

		confirmation->SetUsageData(data);
		NavigateFrom();
	}
	else if ( !Q_strcmp( command, "FlmExtrasFlyoutCheck" ) )
	{
		OnCommand( "FlmExtrasFlyout_Simple" );
		return;
	}
	else if (!Q_strcmp(command, "AudioVideo"))
	{
		if ( m_ActiveControl )
		{
			m_ActiveControl->NavigateFrom( );
		}
		CBaseModPanel::GetSingleton().OpenWindow(WT_AUDIOVIDEO, this, true );
	}
	else if (!Q_strcmp(command, "CreateGame"))
	{
		KeyValues* pSettings = new KeyValues("settings");

		KeyValues* pSystem = pSettings->FindKey("system", true);
		pSystem->SetString("network", "LIVE");
		pSystem->SetString("access", "public");

		KeyValues* pGame = pSettings->FindKey("game", true);

		const char* szGameMode = "campaign";

		pGame->SetString("mode", szGameMode);
		pGame->SetString("campaign", "jacob");
		pGame->SetString("mission", "asi-jac1-landingbay_01");
		pGame->SetString("difficulty", "normal");

		if (StringHasPrefix(szGameMode, "team"))
		{
			pSystem->SetString("netflag", "teamlobby");
		}

		KeyValues* pOptions = pSettings->FindKey("options", true);
		pOptions->SetString("action", "create");

		CBaseModPanel::GetSingleton().PlayUISound(UISOUND_ACCEPT);
		CBaseModPanel::GetSingleton().CloseAllWindows();
		CBaseModPanel::GetSingleton().OpenWindow(WT_GAMESETTINGS, NULL, true, pSettings);
	}
	else if (!Q_strcmp(command, "Srcbox_Singleplayer_Menu"))
	{
		engine->ClientCmd("srcbox_singleplayer");
	}
	else if (!Q_strcmp(command, "NewGameDialog"))
	{
		engine->ClientCmd("OpenNewGameDialog");
	}
	else if (!Q_strcmp(command, "OpenFriendsDialog"))
	{
		engine->ClientCmd("options_legacy");
	}
	else if (!Q_strcmp(command, "OpenLegacyOptions"))
	{
		CBaseModPanel::GetSingleton().OpenOptionsDialog(this);
	}
	else if (!Q_strcmp(command, "Quit"))
	{
		if (ui_old_menus.GetBool())
		{
			MainMenu* self =
				static_cast<MainMenu*>(CBaseModPanel::GetSingleton().GetWindow(WT_MAINMENU));

			if (self)
			{
				self->Close();
			}

			engine->ClientCmd("OpenQuitDialog");
		} else {
			GenericConfirmation* confirmation =
				static_cast<GenericConfirmation*>(CBaseModPanel::GetSingleton().OpenWindow(WT_GENERICCONFIRMATION, this, false));

			GenericConfirmation::Data_t data;

			data.pWindowTitle = "#Srcbox_GameTitle";
			data.pMessageText = "#L4D360UI_MainMenu_Quit_ConfirmMsg";

			data.bOkButtonEnabled = true;
			data.pfnOkCallback = &AcceptQuitGameCallback;
			data.bCancelButtonEnabled = true;
			data.pfnCancelCallback = &OpenMainMenuCancelCallback;

			confirmation->SetUsageData(data);

			NavigateFrom();
		}

		MainMenu* self =
			static_cast<MainMenu*>(CBaseModPanel::GetSingleton().GetWindow(WT_MAINMENU));

		if (self)
		{
			self->Close();
		}
	} 
	else if (!Q_stricmp(command, "QuitGame_NoConfirm"))
	{
		if (IsPC())
		{
			engine->ClientCmd("quit");
		}
	}
	else if (!Q_strcmp(command, "Audio"))
	{
		if ( ui_old_options_menu.GetBool() )
		{
			CBaseModPanel::GetSingleton().OpenOptionsDialog( this );
		}
		else
		{
			// audio options dialog, PC only
			if ( m_ActiveControl )
			{
				m_ActiveControl->NavigateFrom( );
			}
			CBaseModPanel::GetSingleton().OpenWindow(WT_AUDIO, this, true );
		}
	}
	else if (!Q_strcmp(command, "Video"))
	{
		if ( ui_old_options_menu.GetBool() )
		{
			CBaseModPanel::GetSingleton().OpenOptionsDialog( this );
			//engine->ClientCmd("Options");
		}
		else
		{
			// video options dialog, PC only
			if ( m_ActiveControl )
			{
				m_ActiveControl->NavigateFrom( );
			}
			CBaseModPanel::GetSingleton().OpenWindow(WT_VIDEO, this, true );
		}
	}
	else if (!Q_strcmp(command, "Brightness"))
	{
		if ( ui_old_options_menu.GetBool() )
		{
			CBaseModPanel::GetSingleton().OpenOptionsDialog( this );
		}
		else
		{
			// brightness options dialog, PC only
			OpenGammaDialog( GetVParent() );
		}
	}
	else if (!Q_strcmp(command, "KeyboardMouse"))
	{
		if ( ui_old_options_menu.GetBool() )
		{
			CBaseModPanel::GetSingleton().OpenOptionsDialog( this );
		}
		else
		{
			// standalone keyboard/mouse dialog, PC only
			if ( m_ActiveControl )
			{
				m_ActiveControl->NavigateFrom( );
			}
			CBaseModPanel::GetSingleton().OpenWindow(WT_KEYBOARDMOUSE, this, true );
		}
	}
	else if( Q_stricmp( "#L4D360UI_Controller_Edit_Keys_Buttons", command ) == 0 )
	{
		FlyoutMenu::CloseActiveMenu();
		CBaseModPanel::GetSingleton().OpenKeyBindingsDialog( this );
	}
	else if (!Q_strcmp(command, "MultiplayerSettings"))
	{
		if ( ui_old_options_menu.GetBool() )
		{
			CBaseModPanel::GetSingleton().OpenOptionsDialog( this );
		}
		else
		{
			// standalone multiplayer settings dialog, PC only
			if ( m_ActiveControl )
			{
				m_ActiveControl->NavigateFrom( );
			}
			CBaseModPanel::GetSingleton().OpenWindow(WT_MULTIPLAYER, this, true );
		}
	}
	else if ( !Q_strcmp( command, "OpenServerBrowser" ) )
	{
		if ( CheckAndDisplayErrorIfNotLoggedIn() )
			return;

		// on PC, bring up the server browser and switch it to the LAN tab (tab #5)
		engine->ClientCmd( "openserverbrowser" );
	}
	else if ( !Q_strcmp( command, "OpenCreateMultiplayerGameDialog" ) )
	{
		CBaseModPanel::GetSingleton().OpenCreateMultiplayerGameDialog( this );
	}
	else if (command && command[0] == '#')
	{
		// Pass it straight to the engine as a command
		engine->ClientCmd( command+1 );
	}
	else
	{
		const char *pchCommand = command;
		if ( !Q_strcmp(command, "FlmOptionsFlyout") )
		{
		}
		else if ( !Q_strcmp(command, "FlmVersusFlyout") )
		{
			command = "VersusSoftLock";
		}
		else if ( !Q_strcmp( command, "FlmSurvivalFlyout" ) )
		{
			command = "SurvivalCheck";
		}
		else if ( !Q_strcmp( command, "FlmScavengeFlyout" ) )
		{
			command = "ScavengeCheck";
		}
		else if ( StringHasPrefix( command, "FlmExtrasFlyout_" ) )
		{
			command = "FlmExtrasFlyoutCheck";
		}

		// does this command match a flyout menu?
		BaseModUI::FlyoutMenu *flyout = dynamic_cast< FlyoutMenu* >( FindChildByName( pchCommand ) );
		if ( flyout )
		{
			bOpeningFlyout = true;

			// If so, enumerate the buttons on the menu and find the button that issues this command.
			// (No other way to determine which button got pressed; no notion of "current" button on PC.)
			for ( int iChild = 0; iChild < GetChildCount(); iChild++ )
			{
				bool bFound = false;
				/*
				GameModes *pGameModes = dynamic_cast< GameModes *>( GetChild( iChild ) );
				if ( pGameModes )
				{
					for ( int iGameMode = 0; iGameMode < pGameModes->GetNumGameInfos(); iGameMode++ )
					{
						BaseModHybridButton *pHybrid = pGameModes->GetHybridButton( iGameMode );
						if ( pHybrid && pHybrid->GetCommand() && !Q_strcmp( pHybrid->GetCommand()->GetString( "command"), command ) )
						{
							pHybrid->NavigateFrom();
							// open the menu next to the button that got clicked
							flyout->OpenMenu( pHybrid );
							flyout->SetListener( this );
							bFound = true;
							break;
						}
					}
				}*/

				if ( !bFound )
				{
					BaseModHybridButton *hybrid = dynamic_cast<BaseModHybridButton *>( GetChild( iChild ) );
					if ( hybrid && hybrid->GetCommand() && !Q_strcmp( hybrid->GetCommand()->GetString( "command"), command ) )
					{
						hybrid->NavigateFrom();
						// open the menu next to the button that got clicked
						flyout->OpenMenu( hybrid );
						flyout->SetListener( this );
						break;
					}
				}
			}
		}
		else
		{
			BaseClass::OnCommand( command );
		}
	}

	if( !bOpeningFlyout )
	{
		FlyoutMenu::CloseActiveMenu(); //due to unpredictability of mouse navigation over keyboard, we should just close any flyouts that may still be open anywhere.
	}
}

//=============================================================================
void MainMenu::OpenMainMenuJoinFailed( const char *msg )
{
}

//=============================================================================
void MainMenu::OnNotifyChildFocus( vgui::Panel* child )
{
}

void MainMenu::OnFlyoutMenuClose( vgui::Panel* flyTo )
{
	SetFooterState();
}

void MainMenu::OnFlyoutMenuCancelled()
{
}

//=============================================================================
void MainMenu::OnKeyCodePressed( KeyCode code )
{
	switch( GetBaseButtonCode( code ) )
	{
	case KEY_XBUTTON_B:
		// Capture the B key so it doesn't play the cancel sound effect
		break;
	default:
		BaseClass::OnKeyCodePressed( code );
		break;
	}
}

//=============================================================================
void MainMenu::OnThink()
{

	FlyoutMenu *pFlyout = dynamic_cast< FlyoutMenu* >( FindChildByName( "FlmOptionsFlyout" ) );
	if ( pFlyout )
	{
		const MaterialSystem_Config_t &config = materials->GetCurrentConfigForVideoCard();
		pFlyout->SetControlEnabled( "BtnBrightness", !config.Windowed() );
	}

	BaseClass::OnThink();
}

//=============================================================================
void MainMenu::OnOpen()
{
	BaseClass::OnOpen();

	SetFooterState();

#ifdef PLATFORM_64BITS
	if (m_pMainMenuPanel)
	{
		m_pMainMenuPanel->StartVideo();
	}
#endif
}

//=============================================================================
void MainMenu::RunFrame()
{
	BaseClass::RunFrame();
}

//=============================================================================
void MainMenu::PaintBackground() 
{
}

void MainMenu::SetFooterState()
{
	CBaseModFooterPanel *footer = BaseModUI::CBaseModPanel::GetSingleton().GetFooterPanel();
	if ( footer )
	{
		CBaseModFooterPanel::FooterButtons_t buttons = FB_ABUTTON;

		footer->SetButtons( buttons, FF_MAINMENU, false );
		footer->SetButtonText( FB_ABUTTON, "#L4D360UI_Select" );
		footer->SetButtonText( FB_XBUTTON, "#L4D360UI_MainMenu_SeeAll" );
	}
}

wchar_t* wstrdup(const wchar_t* str)
{
	if (!str)
		return NULL;

	size_t len = wcslen(str) + 1;
	wchar_t* copy = (wchar_t*)malloc(len * sizeof(wchar_t));
	if (copy)
	{
		for (size_t i = 0; i < len; i++)
			copy[i] = str[i];
	}
	return copy;
}

//=============================================================================
void MainMenu::ApplySchemeSettings(IScheme* pScheme)
{
	BaseClass::ApplySchemeSettings(pScheme);

	const char* pSettings = "Resource/UI/BaseModUI/MainMenu.res";

	LoadControlSettings(pSettings);

	vgui::IScheme* scheme = vgui::scheme()->GetIScheme(vgui::scheme()->GetScheme("ClientScheme"));

	int nParentW, nParentH;
	GetParent()->GetSize(nParentW, nParentH);
	SetBounds(0, 0, nParentW, nParentH);

	BaseModHybridButton *button = dynamic_cast< BaseModHybridButton* >( FindChildByName( "BtnPlaySolo" ) );
	if (button)
	{
#ifdef _X360
		button->SetText( ( XBX_GetNumGameUsers() > 1 ) ? ( "#L4D360UI_MainMenu_PlaySplitscreen" ) : ( "#L4D360UI_MainMenu_PlaySolo" ) );
		button->SetHelpText((XBX_GetNumGameUsers() > 1) ? ("#L4D360UI_MainMenu_OfflineCoOp_Tip") : ("#L4D360UI_MainMenu_PlaySolo_Tip"));
#endif
}

	if (IsPC())
	{
		FlyoutMenu *pFlyout = dynamic_cast< FlyoutMenu* >(FindChildByName("FlmOptionsFlyout"));
		if (pFlyout)
		{
			bool bUsesCloud = false;

#ifdef IS_WINDOWS_PC
			//ISteamRemoteStorage *pRemoteStorage = SteamClient()?(ISteamRemoteStorage *)SteamClient()->GetISteamGenericInterface(
				//SteamAPI_GetHSteamUser(), SteamAPI_GetHSteamPipe(), STEAMREMOTESTORAGE_INTERFACE_VERSION ):NULL;
#else
			ISteamRemoteStorage *pRemoteStorage = NULL;
			AssertMsg(false, "This branch run on a PC build without IS_WINDOWS_PC defined.");
#endif

			//int32 availableBytes, totalBytes = 0;
			/*if (pRemoteStorage && pRemoteStorage->GetQuota(&totalBytes, &availableBytes))
			{
				if (totalBytes > 0)
				{
					bUsesCloud = true;
				}
			}*/

			pFlyout->SetControlEnabled("BtnCloud", bUsesCloud);
		}
	}

	SetFooterState();

}

void MainMenu::AcceptCommentaryRulesCallback() 
{
}

void MainMenu::AcceptQuitGameCallback()
{
	if ( MainMenu *pMainMenu = static_cast< MainMenu* >( CBaseModPanel::GetSingleton().GetWindow( WT_MAINMENU ) ) )
	{
		pMainMenu->OnCommand( "QuitGame_NoConfirm" );
	}
}

void MainMenu::AcceptVersusSoftLockCallback()
{
	if ( MainMenu *pMainMenu = static_cast< MainMenu* >( CBaseModPanel::GetSingleton().GetWindow( WT_MAINMENU ) ) )
	{
		pMainMenu->OnCommand( "FlmVersusFlyout" );
	}
}


CON_COMMAND_F( openserverbrowser, "Opens server browser", 0 )
{
	bool isSteam = steamapicontext->SteamFriends() && steamapicontext->SteamUtils();
	if ( isSteam )
	{
		// show the server browser
		g_VModuleLoader.ActivateModule("Servers");

		// if an argument was passed, that's the tab index to show, send a message to server browser to switch to that tab
		if ( args.ArgC() > 1 )
		{
			KeyValues *pKV = new KeyValues( "ShowServerBrowserPage" );
			pKV->SetInt( "page", atoi( args[1] ) );
			g_VModuleLoader.PostMessageToAllModules( pKV );
		}

#ifdef INFESTED_DLL
		KeyValues *pSchemeKV = new KeyValues( "SetCustomScheme" );
		pSchemeKV->SetString( "SchemeName", "SwarmServerBrowserScheme" );
		g_VModuleLoader.PostMessageToAllModules( pSchemeKV );
#else
		KeyValues *pSchemeKV = new KeyValues( "SetCustomScheme" );
		pSchemeKV->SetString( "SchemeName", "SourceScheme" );
		g_VModuleLoader.PostMessageToAllModules( pSchemeKV );
#endif
	}
}

