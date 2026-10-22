//========= Copyright © 1996-2008, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
//=====================================================================================//

#include "VGenericConfirmation.h"

#include "vgui_controls/Label.h"
#include "vgui/ISurface.h"
#include "../vgui/nb_button.h"
//#include "VHybridButton.h"
#include "cdll_util.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"


using namespace vgui;
using namespace BaseModUI;

int GenericConfirmation::sm_currentUsageId = 0;

GenericConfirmation::Data_t::Data_t() : 
	pWindowTitle( NULL ),
	pMessageText( NULL ),
	pMessageTextW( NULL ),
	bOkButtonEnabled( false ),
	pfnOkCallback( NULL ),
	bCancelButtonEnabled( false ),
	pfnCancelCallback( NULL ),
	bCheckBoxEnabled( false ),
	pCheckBoxLabelText( NULL ),
	pCheckBoxCvarName( NULL )
{
}

//=============================================================================
GenericConfirmation::GenericConfirmation( Panel *parent, const char *panelName ):
	BaseClass( parent, panelName, true, true, false ),
	m_pLblMessage( 0 ),
	m_pLblCheckBox( 0 ),
	m_data(),
	m_usageId( 0 ),
	m_pCheckBox( 0 )
{
	SetProportional( true );

	m_pLblOkButton = new vgui::Label( this, "LblOkButton", "#GameUI_Icons_A_3DBUTTON" );
	m_pLblOkText = new vgui::Label( this, "LblOkText", "#L4D360UI_Ok" );
	m_pLblCancelButton = new vgui::Label( this, "LblCancelButton", "#GameUI_Icons_B_3DBUTTON" );
	m_pLblCancelText = new vgui::Label( this, "LblCancelText", "#L4D360UI_Cancel" );
	m_pPnlLowerGarnish = new vgui::Panel( this, "PnlLowerGarnish" );

	m_pBtnOK = new CNB_Button( this, "BtnOK", "", this, "OK" );
	m_pBtnCancel = new CNB_Button( this, "BtnCancel", "", this, "cancel" );

	SetTitle( "", false );
	SetDeleteSelfOnClose( true );
	SetLowerGarnishEnabled( false );
	SetMoveable( false );
}

//=============================================================================
GenericConfirmation::~GenericConfirmation()
{
	delete m_pLblMessage;
	delete m_pLblOkButton;
	delete m_pLblOkText;
	delete m_pLblCancelButton;
	delete m_pLblCancelText;
	delete m_pPnlLowerGarnish;
}

//=============================================================================
void GenericConfirmation::OnCommand(const char *command)
{
	if ( Q_stricmp( command, "OK" ) == 0 )
	{
		OnKeyCodePressed( KEY_XBUTTON_A );
	}
	else if ( Q_stricmp( command, "cancel" ) == 0 )
	{
		OnKeyCodePressed( KEY_XBUTTON_B );
	}
}

//=============================================================================
void GenericConfirmation::OnKeyCodePressed( KeyCode keycode )
{
	vgui::KeyCode code = GetBaseButtonCode( keycode );

	switch ( code )
	{
	case KEY_XBUTTON_A:
		if ( m_OkButtonEnabled )
		{
			if ( m_pCheckBox )
			{
				//m_pCheckBox->ApplyChanges();
			}

			CBaseModPanel::GetSingleton().PlayUISound( UISOUND_ACCEPT );
			if ( !NavigateBack() )
			{
				Close();
			}

			if ( m_data.pfnOkCallback != 0 )
			{
				m_data.pfnOkCallback();
			}
		}
		break;

	case KEY_XBUTTON_B:
		if ( m_CancelButtonEnabled )
		{
			if ( m_pCheckBox )
			{
				//m_pCheckBox->ApplyChanges();
			}

			CBaseModPanel::GetSingleton().PlayUISound( UISOUND_BACK );
			if ( !NavigateBack() )
			{
				Close();
			}

			if ( m_data.pfnCancelCallback != 0 )
			{
				m_data.pfnCancelCallback();
			}
		}
		break;
	default:
		BaseClass::OnKeyCodePressed(keycode);
		break;
	}
}

void GenericConfirmation::OnKeyCodeTyped( vgui::KeyCode code )
{
	// For PC, this maps space bar and enter to OK and esc to cancel
	switch ( code )
	{
	case KEY_SPACE:
	case KEY_ENTER:
		return OnKeyCodePressed( KEY_XBUTTON_A );

	case KEY_ESCAPE:
		return OnKeyCodePressed( KEY_XBUTTON_B );
	}

	BaseClass::OnKeyTyped( code );
}

void GenericConfirmation::OnOpen( )
{
	BaseClass::OnOpen();

	m_bNeedsMoveToFront = true;
}

void ExpandButtonWidthIfNecessary( vgui::Button *pButton )
{
	int originalWide, originalTall;
	pButton->GetSize( originalWide, originalTall );

	pButton->SizeToContents();

	// restore tall
	pButton->SetTall( originalTall );

	const int iMinButtonWidth = MAX( originalWide, scheme()->GetProportionalScaledValue( 45 ) );

	// if we're smaller than original wide, restore wide
	if ( pButton->GetWide() < iMinButtonWidth )
	{
		pButton->SetWide( originalWide );
	}
}

//=============================================================================
// Srcbox: L4D2-style layout, all in proportional units (screen height / 480) so it
// lands in the same place at any resolution. Measured off Left 4 Dead 2 at 1080p:
//   title band     y 108-144 (244-325 px), title text x 86
//   message band   y 178-377 (400-848 px), message x 95 (resource file)
//   CANCEL / OK    rows 20 apart from y 329 (740 px), text x 110, icons x 95
// The dialog is the full screen width with its top at y 89; everything below is
// relative to that, and the resource file's positions already match it.
// (This used to mix pixels and scaled values with magic factors, e.g. the buttons at
// dialogHeight * 2.25 / 2.445 and the bands at fixed pixel rows, so it only lined
// up at one resolution.)
//=============================================================================
#define GC_DIALOG_Y			89
#define GC_TITLEBAND_Y		19		// relative to the dialog
#define GC_TITLEBAND_TALL	36
#define GC_TITLE_X			86
#define GC_MSGBAND_Y		89
#define GC_MSGBAND_TALL		199
#define GC_MSG_WIDE			545

void GenericConfirmation::LoadLayout()
{
	BaseClass::LoadLayout();

	int screenWidth, screenHeight;
	CBaseModPanel::GetSingleton().GetSize( screenWidth, screenHeight );

	const int nScheme = GetScheme();
	#define GC_P( v ) vgui::scheme()->GetProportionalScaledValueEx( nScheme, ( v ) )

	SetPos( 0, GC_P( GC_DIALOG_Y ) );
	SetSize( screenWidth, MAX( GetTall(), GC_P( GC_MSGBAND_Y + GC_MSGBAND_TALL ) ) );

	vgui::Label *pLblTitle = dynamic_cast< vgui::Label* >( FindChildByName( "LblTitle" ) );
	if ( pLblTitle )
	{
		// left aligned, centred in the title band
		int titleWide, titleTall;
		pLblTitle->GetContentSize( titleWide, titleTall );
		const int nBandTall = GC_P( GC_TITLEBAND_TALL );
		pLblTitle->SetContentAlignment( vgui::Label::a_west );
		pLblTitle->SetBounds( GC_P( GC_TITLE_X ), GC_P( GC_TITLEBAND_Y ) + ( nBandTall - titleTall ) / 2,
			MAX( titleWide, screenWidth - GC_P( GC_TITLE_X ) * 2 ), titleTall );
	}

	if ( m_pLblMessage )
	{
		// position and font come from the resource file; long text wraps at its width
		int msgWide, msgTall;
		m_pLblMessage->GetContentSize( msgWide, msgTall );
		if ( msgWide > GC_P( GC_MSG_WIDE ) )
		{
			m_pLblMessage->SetWrap( true );
			m_pLblMessage->SetWide( GC_P( GC_MSG_WIDE ) );
			m_pLblMessage->SetTextInset( 0, 0 );
			m_pLblMessage->GetContentSize( msgWide, msgTall );
		}
		m_pLblMessage->SetTall( msgTall );
	}

	m_pLblCancelButton->SetVisible( false );
	m_pLblCancelText->SetVisible( false );
	m_pLblOkButton->SetVisible( false );
	m_pLblOkText->SetVisible( false );

	vgui::Button *pOkButton = dynamic_cast< vgui::Button* >( FindChildByName( "BtnOk" ) );
	vgui::Button *pCancelButton = dynamic_cast< vgui::Button* >( FindChildByName( "BtnCancel" ) );
	vgui::Panel *pOkIcon = FindChildByName( "IconForwardArrow" );
	vgui::Panel *pCancelIcon = FindChildByName( "IconBackArrow" );

	if ( pOkButton )
		pOkButton->SetVisible( m_data.bOkButtonEnabled );
	if ( pCancelButton )
		pCancelButton->SetVisible( m_data.bCancelButtonEnabled );
	if ( pOkIcon )
		pOkIcon->SetVisible( m_data.bOkButtonEnabled );
	if ( pCancelIcon )
		pCancelIcon->SetVisible( m_data.bCancelButtonEnabled );

	// a lone OK takes the first row (where CANCEL sits), with its icon
	if ( pOkButton && pCancelButton && m_data.bOkButtonEnabled && !m_data.bCancelButtonEnabled )
	{
		int x, y;
		pCancelButton->GetPos( x, y );
		int okX, okY;
		pOkButton->GetPos( okX, okY );
		pOkButton->SetPos( okX, y );
		if ( pOkIcon && pCancelIcon )
		{
			int ix, iy, cx, cy;
			pOkIcon->GetPos( ix, iy );
			pCancelIcon->GetPos( cx, cy );
			pOkIcon->SetPos( ix, cy );
		}
	}

	// keep each icon centred on its button's row
	struct { vgui::Panel *pButton; vgui::Panel *pIcon; } rows[] = { { pOkButton, pOkIcon }, { pCancelButton, pCancelIcon } };
	for ( int i = 0; i < ARRAYSIZE( rows ); i++ )
	{
		if ( !rows[i].pButton || !rows[i].pIcon )
			continue;
		int bx, by, ix, iy;
		rows[i].pButton->GetPos( bx, by );
		rows[i].pIcon->GetPos( ix, iy );
		rows[i].pIcon->SetPos( ix, by + ( rows[i].pButton->GetTall() - rows[i].pIcon->GetTall() ) / 2 );
	}

	#undef GC_P
}

//=============================================================================
void GenericConfirmation::PaintBackground()
{
	// Alien Swarm stuff. Don't use
	//BaseClass::DrawGenericBackground();

	const int nScheme = GetScheme();
	const int nTitleY = vgui::scheme()->GetProportionalScaledValueEx( nScheme, GC_TITLEBAND_Y );
	const int nTitleTall = vgui::scheme()->GetProportionalScaledValueEx( nScheme, GC_TITLEBAND_TALL );
	const int nMsgY = vgui::scheme()->GetProportionalScaledValueEx( nScheme, GC_MSGBAND_Y );
	const int nMsgTall = vgui::scheme()->GetProportionalScaledValueEx( nScheme, GC_MSGBAND_TALL );

	// two translucent black bands across the screen: the title, then message + buttons
	vgui::surface()->DrawSetColor( Color( 0, 0, 0, 160 ) );
	vgui::surface()->DrawFilledRect( 0, nTitleY, GetWide(), nTitleY + nTitleTall );
	vgui::surface()->DrawFilledRect( 0, nMsgY, GetWide(), nMsgY + nMsgTall );

	if ( m_bNeedsMoveToFront )
	{
		vgui::ipanel()->MoveToFront( GetVPanel() );
		m_bNeedsMoveToFront = false;
	}
}

//=============================================================================
// returns the usageId, which gets incremented each time this function is called
int GenericConfirmation::SetUsageData( const Data_t & data )
{
	m_data = data;

	SetTitle( data.pWindowTitle, false );

	if ( m_pLblMessage )
	{
		m_pLblMessage->DeletePanel();
		m_pLblMessage = NULL;
	}
	
	if ( data.pMessageTextW )
		m_pLblMessage = new Label( this, "LblMessage", data.pMessageTextW );
	else
		m_pLblMessage = new Label( this, "LblMessage", data.pMessageText );

	// tell our base version so input is disabled
	m_OkButtonEnabled = data.bOkButtonEnabled;
	m_CancelButtonEnabled = data.bCancelButtonEnabled;

	if ( data.bCheckBoxEnabled && data.pCheckBoxCvarName )
	{
		/*m_pCheckBox = new CvarToggleCheckButton<ConVarRef>(
			this, 
			"CheckButton", 
			"",
			data.pCheckBoxCvarName,
			true );*/

		if ( m_pLblCheckBox )
		{
			m_pLblCheckBox->DeletePanel();
			m_pLblCheckBox = NULL;
		}

		m_pLblCheckBox = new Label( this, "LblCheckButton", data.pCheckBoxLabelText );
	}

	if ( UI_IsDebug() )
	{
		Msg( "[GAMEUI] GenericConfirmation::SetWindowTitle : %s\n", data.pWindowTitle!=NULL ? data.pWindowTitle : "<NULL>" );
		Msg( "[GAMEUI] GenericConfirmation::SetMessageText : %s\n", data.pMessageText!=NULL ? data.pMessageText : "<NULL>" );
	}

	// the window may need to be resized.
	LoadLayout();

	return m_usageId = ++sm_currentUsageId;
}


//=============================================================================
void GenericConfirmation::ApplySchemeSettings(IScheme *pScheme)
{
	BaseClass::ApplySchemeSettings(pScheme);

	LoadLayout();

	SetPaintBackgroundEnabled( true );

	//
	// Override title and msg font
	//
	m_pLblOkButton->SetFont( pScheme->GetFont( "GameUIButtonsMini", true ) );
	m_pLblCancelButton->SetFont( pScheme->GetFont( "GameUIButtonsMini", true ) );

	m_hTitleFont = pScheme->GetFont( "MainBold", true );
	m_hMessageFont = pScheme->GetFont( "Default", true );

	if ( m_LblTitle )
	{
		m_LblTitle->SetFont( m_hTitleFont );
	}

	if ( m_pLblMessage )
	{
		m_pLblMessage->SetFont( m_hMessageFont );
	}

	if ( m_pLblCheckBox )
	{
		m_pLblCheckBox->SetFont( m_hMessageFont );
	}
}
