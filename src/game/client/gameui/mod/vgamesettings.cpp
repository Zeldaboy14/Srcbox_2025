//========= Copyright © 1996-2008, Valve Corporation, All rights reserved. ============//
//
// Purpose: Simplified Game Settings Panel (No Matchmaking)
//
//=====================================================================================//
#include "cbase.h"
#include "VGameSettings.h"
#include "KeyValues.h"
#include "VDropDownMenu.h"
#include "VHybridButton.h"
#include "VFooterPanel.h"
#include "vgui/ISurface.h"
#include "EngineInterface.h"
#include "vgui_controls/ImagePanel.h"
#include "nb_header_footer.h"
#include "fmtstr.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

using namespace vgui;
using namespace BaseModUI;

//=============================================================================
GameSettings::GameSettings( vgui::Panel *parent, const char *panelName ) :
    BaseClass( parent, panelName, true, false ),
    m_pSettings( NULL ),
    m_autodelete_pSettings( (KeyValues *)NULL ),
    m_drpDifficulty( NULL ),
    m_drpGameType( NULL ),
    m_drpFriendlyFire( NULL ),
    m_drpOnslaught( NULL ),
    m_drpStartingMission( NULL ),
    m_pTitle( NULL ),
    m_pHeaderFooter( NULL )
{
    m_pHeaderFooter = new CNB_Header_Footer( this, "HeaderFooter" );
    m_pHeaderFooter->SetTitle( "" );
    //m_pHeaderFooter->SetHeaderEnabled( false );
    //m_pHeaderFooter->SetGradientBarEnabled( true );
    //m_pHeaderFooter->SetGradientBarPos( 140, 190 );

    m_pTitle = new vgui::Label( this, "Title", "" );

    SetDeleteSelfOnClose(true);
    SetProportional( true );
    SetLowerGarnishEnabled( true );
    SetCancelButtonEnabled( true );
}

GameSettings::~GameSettings()
{
}

void GameSettings::SetDataSettings( KeyValues *pSettings )
{
    m_pSettings = pSettings;
}

void GameSettings::PaintBackground()
{
    const char *szMode = m_pSettings->GetString( "game/mode", "campaign" );
    const char *pTitle = "#ASUI_GameSettings_Solo";

    if ( !Q_stricmp( szMode, "campaign" ) )
        pTitle = "#ASUI_GameSettings_MP_campaign";
    else if ( !Q_stricmp( szMode, "single_mission" ) )
        pTitle = "#ASUI_GameSettings_MP_single_mission";

    m_pTitle->SetText( pTitle );
}

void GameSettings::Activate()
{
    BaseClass::Activate();

    if ( m_drpGameType )
    {
        const char *szGameMode = m_pSettings->GetString( "game/mode", "campaign" );
        m_drpGameType->SetCurrentSelection( !Q_stricmp( szGameMode, "campaign" ) ?
            "#ASUI_GameType_Campaign" : "#ASUI_GameType_Single_Mission" );

        UpdateSelectMissionButton();
    }

    if ( m_drpDifficulty )
        m_drpDifficulty->SetCurrentSelection( CFmtStr( "#L4D360UI_Difficulty_%s", m_pSettings->GetString( "game/difficulty", "normal" ) ) );

    if ( m_drpFriendlyFire )
        m_drpFriendlyFire->SetCurrentSelection( m_pSettings->GetInt( "game/hardcoreFF", 0 ) ? "#L4D360UI_HardcoreFF" : "#L4D360UI_RegularFF" );

    if ( m_drpOnslaught )
        m_drpOnslaught->SetCurrentSelection( m_pSettings->GetInt( "game/onslaught", 0 ) ? "#L4D360UI_OnslaughtEnabled" : "#L4D360UI_OnslaughtDisabled" );

    UpdateMissionImage();
    //UpdateFooter();
}

void GameSettings::OnCommand(const char *command)
{
    if ( V_strcmp( command, "cmd_gametype_campaign" ) == 0 )
    {
        m_pSettings->SetString( "game/mode", "campaign" );
        UpdateSelectMissionButton();
        UpdateMissionImage();
    }
    else if ( V_strcmp( command, "cmd_gametype_single_mission" ) == 0 )
    {
        m_pSettings->SetString( "game/mode", "single_mission" );
        UpdateSelectMissionButton();
        UpdateMissionImage();
    }
    else if ( V_strcmp( command, "cmd_change_mission" ) == 0 || V_strcmp( command, "cmd_change_starting_mission" ) == 0 )
    {
        ShowMissionSelect();
    }
    else if ( V_strcmp( command, "StartGame" ) == 0 )
    {
        Navigate();
    }
    else if (V_strcmp(command, "Back") == 0)
    {
        CBaseModPanel::GetSingleton().OpenWindow(WT_MAINMENU, this, false);

        GameSettings* self =
            static_cast<GameSettings*>(CBaseModPanel::GetSingleton().GetWindow(WT_GAMESETTINGS));

        if (self)
        {
            self->Close();
        }
    }
    else if ( const char *szDifficultyValue = StringAfterPrefix( command, "#L4D360UI_Difficulty_" ) )
    {
        m_pSettings->SetString( "game/difficulty", szDifficultyValue );
    }
    else if ( !Q_strcmp( command, "#L4D360UI_RegularFF" ) )
        m_pSettings->SetInt( "game/hardcoreFF", 0 );
    else if ( !Q_strcmp( command, "#L4D360UI_HardcoreFF" ) )
        m_pSettings->SetInt( "game/hardcoreFF", 1 );
    else if ( !Q_strcmp( command, "#L4D360UI_OnslaughtDisabled" ) )
        m_pSettings->SetInt( "game/onslaught", 0 );
    else if ( !Q_strcmp( command, "#L4D360UI_OnslaughtEnabled" ) )
        m_pSettings->SetInt( "game/onslaught", 1 );
    else
    {
        BaseClass::OnCommand( command );
    }
}

void GameSettings::Navigate()
{
    CBaseModPanel::GetSingleton().PlayUISound( UISOUND_ACCEPT );
    Msg( "GameSettings: Starting game with current settings...\n" );
    // Add your launch logic here later
}

void GameSettings::ApplySchemeSettings( vgui::IScheme *pScheme )
{
    BaseClass::ApplySchemeSettings( pScheme );

    LoadControlSettings( "Resource/UI/basemodui/GameSettings.res" );

    SetPaintBackgroundEnabled( true );
    SetupAsDialogStyle();

    m_drpDifficulty      = dynamic_cast<DropDownMenu*>( FindChildByName( "DrpDifficulty" ) );
    m_drpGameType        = dynamic_cast<DropDownMenu*>( FindChildByName( "DrpGameType" ) );
    m_drpFriendlyFire    = dynamic_cast<DropDownMenu*>( FindChildByName( "DrpFriendlyFire" ) );
    m_drpOnslaught       = dynamic_cast<DropDownMenu*>( FindChildByName( "DrpOnslaught" ) );
    m_drpStartingMission = dynamic_cast<DropDownMenu*>( FindChildByName( "DrpStartingMission" ) );

   Activate();
}

void GameSettings::OnKeyCodePressed( KeyCode code )
{
    if ( GetBaseButtonCode( code ) == KEY_XBUTTON_B )
    {
        CBaseModPanel::GetSingleton().PlayUISound( UISOUND_BACK );
        NavigateBack();
    }
    else
        BaseClass::OnKeyCodePressed( code );
}

void GameSettings::UpdateFooter()
{
    CBaseModFooterPanel *footer = BaseModUI::CBaseModPanel::GetSingleton().GetFooterPanel();
    /*if (footer)
    {
        footer->SetButtons( FB_ABUTTON | FB_BBUTTON, FF_AB_ONLY, false );
        footer->SetButtonText( FB_ABUTTON, "#L4D360UI_Select" );
        footer->SetButtonText( FB_BBUTTON, "#L4D360UI_Cancel" );
    }*/
}

// Stubs
void GameSettings::ShowMissionSelect() { Msg( "[GameSettings] Mission select stub\n" ); }
void GameSettings::ShowStartingMissionSelect() { Msg( "[GameSettings] Starting mission stub\n" ); }

void GameSettings::UpdateMissionImage()
{
    ImagePanel* img = dynamic_cast<ImagePanel*>( FindChildByName( "ImgLevelImage" ) );
    if ( img )
    {
        const char *szMission = m_pSettings->GetString( "game/mission", "asi-jac1-landingbay01" );
        img->SetImage( VarArgs( "maps/%s", szMission ) );
    }
}

void GameSettings::UpdateSelectMissionButton()
{
    DropDownMenu *menu = dynamic_cast<DropDownMenu*>( FindChildByName( "DrpSelectMission", true ) );
    if ( !menu ) return;
    BaseModHybridButton *btn = menu->GetButton();
    if ( !btn ) return;

    const char *mode = m_pSettings->GetString( "game/mode", "campaign" );
    btn->SetText( !Q_stricmp( mode, "campaign" ) ? "#ASUI_Select_Campaign" : "#ASUI_Select_Mission" );
}

void GameSettings::OnClose()
{
    BaseClass::OnClose();
    if ( m_drpDifficulty ) m_drpDifficulty->CloseDropDown();
    if ( m_drpGameType ) m_drpGameType->CloseDropDown();
    if ( m_drpFriendlyFire ) m_drpFriendlyFire->CloseDropDown();
    if ( m_drpOnslaught ) m_drpOnslaught->CloseDropDown();
    m_pSettings = NULL;
}

void GameSettings::OnFlyoutMenuClose( vgui::Panel* flyTo )
{
    UpdateFooter();
    UpdateMissionImage();
    UpdateSelectMissionButton();
}

void GameSettings::OnFlyoutMenuCancelled() {}
void GameSettings::OnNotifyChildFocus( vgui::Panel* child ) {}
void GameSettings::OnThink() { BaseClass::OnThink(); }

static void ShowGameSettings()
{
    CBaseModFrame* mainMenu = CBaseModPanel::GetSingleton().GetWindow(WT_MAINMENU);
    CBaseModPanel::GetSingleton().OpenWindow(WT_GAMESETTINGS, mainMenu);
}

ConCommand showGameSettings("showGameSettings", ShowGameSettings);
