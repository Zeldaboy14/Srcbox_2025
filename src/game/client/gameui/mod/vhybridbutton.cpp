//========= Copyright � 1996-2008, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
//=====================================================================================//

#include "VHybridButton.h"
#include "basemodpanel.h"
#include "VFooterPanel.h"
#include "VFlyoutMenu.h"
#include "../EngineInterface.h"
#include "vgui/ISurface.h"
#include "vgui_controls/Tooltip.h"
#include "vgui/IVgui.h"
#include "tier1/KeyValues.h"
#include "vgui/ilocalize.h"
#include "VDropDownMenu.h"
#include "VSliderControl.h"

#include <ctype.h>

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

using namespace BaseModUI;
using namespace vgui;

DECLARE_BUILD_FACTORY_DEFAULT_TEXT( BaseModHybridButton, HybridButton );


//=============================================================================
// Constructor / Destructor
//=============================================================================

BaseModUI::BaseModHybridButton::BaseModHybridButton( Panel *parent, const char *panelName, const char *text, Panel *pActionSignalTarget, const char *pCmd )
	: BaseClass( parent, panelName, text, pActionSignalTarget, pCmd )
{
	SetPaintBorderEnabled( false );
	SetPaintBackgroundEnabled( false );
	SetContentAlignment( a_northwest );
	SetClosed();
	SetButtonActivationType( ACTIVATE_ONRELEASED );
	SetConsoleStylePanel( true );

	m_isNavigateTo = false;
	m_bIgnoreButtonA = false;

	m_nStyle = BUTTON_SIMPLE;
	m_hTextFont = 0;
	m_hTextBlurFont = 0;
	m_hHintTextFont = 0;
	m_hSelectionFont = 0;
	m_hSelectionBlurFont = 0;

	m_originalTall = 0;
	m_textInsetX = 0;
	m_textInsetY = 0;
	m_bSchemeStyle = false;
	m_bAllCaps = false;

	m_iSelectedArrow = -1;
	m_iUnselectedArrow = -1;

	m_nWideAtOpen = 0;
}

BaseModUI::BaseModHybridButton::BaseModHybridButton( Panel *parent, const char *panelName, const wchar_t *text, Panel *pActionSignalTarget, const char *pCmd )
	: BaseClass( parent, panelName, text, pActionSignalTarget, pCmd )
{
	SetPaintBorderEnabled( false );
	SetPaintBackgroundEnabled( false );
	SetContentAlignment( a_northwest );
	SetClosed();
	SetButtonActivationType( ACTIVATE_ONRELEASED );

	m_isNavigateTo = false;
	
	m_nStyle = BUTTON_SIMPLE;
	m_hTextFont = 0;
	m_hTextBlurFont = 0;
	m_hHintTextFont = 0;
	m_hSelectionFont = 0;
	m_hSelectionBlurFont = 0;

	m_originalTall = 0;
	m_textInsetX = 0;
	m_textInsetY = 0;
	m_bSchemeStyle = false;
	m_bAllCaps = false;

	m_iSelectedArrow = -1;
	m_iUnselectedArrow = -1;
}

BaseModUI::BaseModHybridButton::~BaseModHybridButton()
{
	// purposely not destroying our textures
	// otherwise i/o constantly on these
}

BaseModHybridButton::State BaseModHybridButton::GetCurrentState()
{
	State curState = Enabled;
	if ( HasFocus() )
	{
		curState = IsEnabled() ? Focus : FocusDisabled;
	}
	if( m_isOpen )
	{
		curState = Open;
	}
	else if( IsArmed() || m_isNavigateTo ) //NavigateTo doesn't instantly give focus to the control
	{										//so this little boolean value is meant to let us know we should have focus for the "focus state"
		if( IsEnabled() )					//until vgui catches up and registers us as having focus.
		{
			curState = Focus;
		}
		else
		{
			curState = FocusDisabled;
		}

		if( IsArmed() )
		{
			m_isNavigateTo = false;
		}
	}
	else if( !IsEnabled() )
	{
		curState = Disabled;
	}

	return curState;
}

void BaseModHybridButton::SetOpen()
{
	if ( m_isOpen )
		return;
	m_isOpen = true;
	PostMessageToAllSiblingsOfType< BaseModHybridButton >( new KeyValues( "OnSiblingHybridButtonOpened" ) );
}

void BaseModHybridButton::SetClosed()
{
	if ( !m_isOpen )
		return;

	m_isOpen = false;
}

void BaseModHybridButton::OnSiblingHybridButtonOpened()
{
	bool bClosed = false;

	FlyoutMenu *pActiveFlyout = FlyoutMenu::GetActiveMenu();

	if ( pActiveFlyout )
	{
		BaseModHybridButton *button = dynamic_cast< BaseModHybridButton* >( pActiveFlyout->GetNavFrom() );
		if ( button && button == this )
		{
			// We need to close the flyout attached to this button
			FlyoutMenu::CloseActiveMenu();
			bClosed = true;
		}
	}

	if ( !bClosed )
	{
		SetClosed();
	}

	m_isNavigateTo = false;
}

char const *BaseModHybridButton::GetHelpText( bool bEnabled ) const
{
	return bEnabled ? m_enabledToolText.String() : m_disabledToolText.String();
}

void BaseModHybridButton::UpdateFooterHelpText()
{
	CBaseModFooterPanel *footer = BaseModUI::CBaseModPanel::GetSingleton().GetFooterPanel();
	if ( footer )
	{
		//Directly set the new tooltips
		footer->SetHelpText( IsEnabled() ? m_enabledToolText.String() : m_disabledToolText.String() );
	}
}

void BaseModHybridButton::OnMousePressed( vgui::MouseCode code )
{
	BaseClass::OnMousePressed( code );
	if( code == MOUSE_RIGHT )
	{
		FlyoutMenu::CloseActiveMenu( this );
	}
	else
	{
		if( (code == MOUSE_LEFT) && (IsEnabled() == false) && (dynamic_cast<FlyoutMenu *>( GetParent() ) == NULL) )
		{
			//when trying to use an inactive item that isn't part of a flyout. Close any open flyouts.
			FlyoutMenu::CloseActiveMenu( this );
		}
		RequestFocus( 0 );			
	}
}

void BaseModHybridButton::NavigateTo( )
{
	BaseClass::NavigateTo( );

	UpdateFooterHelpText();

	FlyoutMenu* parentMenu = dynamic_cast< FlyoutMenu* >( GetParent() );
	if( parentMenu )
	{
		parentMenu->NotifyChildFocus( this );
	}

	if (GetVParent())
	{
		KeyValues *msg = new KeyValues("OnHybridButtonNavigatedTo");
		msg->SetInt("button", ToHandle() );

		ivgui()->PostMessage(GetVParent(), msg, GetVPanel());
	}

	m_isNavigateTo = true;
	RequestFocus( 0 );
}

void BaseModHybridButton::NavigateFrom( )
{
	BaseClass::NavigateFrom( );

	if ( CBaseModPanel::GetSingleton().GetFooterPanel() )
	{
		// Show no help text if they left the button
		CBaseModPanel::GetSingleton().GetFooterPanel()->FadeHelpText();
	}

	m_isNavigateTo = false;
}

void BaseModHybridButton::SetHelpText( const char* tooltip, bool enabled )
{
	if ( enabled )
	{
		m_enabledToolText = tooltip;
	}
	else
	{
		m_disabledToolText = tooltip;
	}

	// if we have the focus update the footer
	if ( HasFocus() )
	{
		UpdateFooterHelpText();
	}
}

void BaseModHybridButton::DrawRoundedBox(int x, int y, int wide, int tall, Color color, float normalizedAlpha, bool bHighlightGradient, Color highlightCenterColor)
{
	if (m_nNBBgTextureId1 == -1 ||
		m_nNBBgTextureId2 == -1 ||
		m_nNBBgTextureId3 == -1 ||
		m_nNBBgTextureId4 == -1)
	{
		return;
	}

	color[3] *= normalizedAlpha;

	// work out our bounds
	int cornerWide, cornerTall;
	GetCornerTextureSize(cornerWide, cornerTall);

	// draw the background in the areas not occupied by the corners
	// draw it in three horizontal strips
	surface()->DrawSetColor(color);
	surface()->DrawFilledRect(x + cornerWide, y, x + wide - cornerWide, y + cornerTall);
	surface()->DrawFilledRect(x, y + cornerTall, x + wide, y + tall - cornerTall);
	surface()->DrawFilledRect(x + cornerWide, y + tall - cornerTall, x + wide - cornerWide, y + tall);

	// draw the corners
	surface()->DrawSetTexture(m_nNBBgTextureId1);
	surface()->DrawTexturedRect(x, y, x + cornerWide, y + cornerTall);
	surface()->DrawSetTexture(m_nNBBgTextureId2);
	surface()->DrawTexturedRect(x + wide - cornerWide, y, x + wide, y + cornerTall);
	surface()->DrawSetTexture(m_nNBBgTextureId3);
	surface()->DrawTexturedRect(x + wide - cornerWide, y + tall - cornerTall, x + wide, y + tall);
	surface()->DrawSetTexture(m_nNBBgTextureId4);
	surface()->DrawTexturedRect(x + 0, y + tall - cornerTall, x + cornerWide, y + tall);

	if (bHighlightGradient)
	{
		surface()->DrawSetColor(highlightCenterColor);
		surface()->DrawFilledRectFade(x + cornerWide, y, x + wide * 0.5f, y + tall, 0, 255, true);
		surface()->DrawFilledRectFade(x + wide * 0.5f, y, x + wide - cornerWide, y + tall, 255, 0, true);
	}
}

//-----------------------------------------------------------------------------
// Srcbox: the Orange Box (360) menu highlight, measured off HL2 at 1080p (Xenia):
// a 2 px orange (195,122,6) border with ~8 px round corners on the left, filled
// with 137,82,6; fill and the top/bottom border fade out to the right, gone ~560 px
// from the left edge. Unfocused text 205 grey, focused white, 17 px in, no glow.
//-----------------------------------------------------------------------------
static float OrangeBoxScale()
{
	int screenWide, screenTall;
	surface()->GetScreenSize( screenWide, screenTall );
	return screenTall / 1080.0f;
}

static void DrawOrangeBoxHighlight( int wide, int tall )
{
	if ( wide <= 0 || tall <= 0 )
		return;

	const float flScale = OrangeBoxScale();
	const float flFade = MAX( (float)wide, 560.0f * flScale );	// alpha reaches 0 here
	const int nRadius = MAX( 2, (int)( 8 * flScale + 0.5f ) );
	const int nBorder = MAX( 1, (int)( 2 * flScale + 0.5f ) );
	const Color fill( 137, 82, 6, 255 );
	const Color border( 195, 122, 6, 255 );

	for ( int row = 0; row < tall; row++ )
	{
		// round the left corners
		int inset = 0;
		float d = -1.0f;
		if ( row < nRadius )
			d = nRadius - row - 0.5f;
		else if ( row >= tall - nRadius )
			d = row - ( tall - nRadius ) + 0.5f;
		if ( d >= 0.0f )
			inset = (int)( nRadius - sqrtf( MAX( 0.0f, nRadius * nRadius - d * d ) ) + 0.5f );

		const int x0 = inset;
		if ( x0 >= wide )
			continue;
		const int a0 = (int)( 255.0f * MAX( 0.0f, 1.0f - x0 / flFade ) );
		const int a1 = (int)( 255.0f * MAX( 0.0f, 1.0f - wide / flFade ) );

		const bool bEdgeRow = ( row < nBorder || row >= tall - nBorder );
		if ( bEdgeRow )
		{
			surface()->DrawSetColor( border );
			surface()->DrawFilledRectFade( x0, row, wide, row + 1, a0, a1, true );
			continue;
		}

		// left border, then the fading fill
		surface()->DrawSetColor( border );
		surface()->DrawFilledRect( x0, row, MIN( x0 + nBorder, wide ), row + 1 );
		if ( x0 + nBorder < wide )
		{
			const int af = (int)( 255.0f * MAX( 0.0f, 1.0f - ( x0 + nBorder ) / flFade ) );
			surface()->DrawSetColor( fill );
			surface()->DrawFilledRectFade( x0 + nBorder, row, wide, row + 1, af, a1, true );
		}
	}
}

// The Orange Box menu text is spaced wider than Verdana: ~2 px between letters at 1080
static int OrangeBoxTracking()
{
	return (int)( 2.0f * OrangeBoxScale() + 0.5f );
}

bool BaseModHybridButton::IsOrangeBoxMenuStyle() const
{
	return m_nStyle == BUTTON_ALIENSWARMMENUBUTTON || m_nStyle == BUTTON_ALIENSWARMMENUBUTTONSMALL || m_nStyle == BUTTON_ALIENSWARMDEFAULT;
}

// 0 = Deprecated CA buttons
// 1 = Normal Button
// 2 = MainMenu Button or InGameMenu Butto
// 3 = Flyout button
void BaseModHybridButton::PaintButtonEx()
{
	vgui::IScheme *pScheme = vgui::scheme()->GetIScheme( GetScheme() );

	int x, y;
	int wide, tall;
	GetSize( wide, tall );

	x = 0;

	// due to vertical resizing, center within the control
	y = ( tall - m_originalTall ) / 2;
	tall = m_originalTall;

	if ( m_nStyle == BUTTON_GAMEMODE )
	{
		y = 0;
	}

	if ( ( m_nStyle == BUTTON_DROPDOWN || m_nStyle == BUTTON_GAMEMODE ) && GetCurrentState() == Open && m_nWideAtOpen )
	{
		wide = m_nWideAtOpen;
	}

	bool bAnimateGlow = false;
	bool bDrawGlow = false;
	bool bDrawCursor = false;
	bool bDrawText = true;
	Color col;
	State curState = GetCurrentState();

	// Srcbox: buttons whose style is in the scheme (other than the Orange Box menu
	// ones) draw exactly as in Alien Swarm: the options screens' drop downs, flyout
	// items, dialog and default buttons. Buttons with a style the scheme doesn't
	// have keep the look they had.
	const bool bSwarmLook = !IsOrangeBoxMenuStyle() && ( m_nStyle != BUTTON_SIMPLE || m_bSchemeStyle );
	switch ( curState )
	{
		case Enabled:
			// selectable, just not highlighted
			if ( m_nStyle == BUTTON_RED || m_nStyle == BUTTON_REDMAIN )
			{
				//col.SetColor( 0, 128, 128, 255 );
				//col.SetColor( 169, 213, 255, 255 );
				col.SetColor(190, 121, 6, 255);
			}
			else if ( IsOrangeBoxMenuStyle() )
			{
				//col.SetColor( 135, 170, 193, 255 );
				col.SetColor( 205, 205, 205, 255 );	// Orange Box unselected item
			}
			// we shouldn't have this
			/*else if (bSwarmLook)
			{
				col.SetColor( 83, 148, 192, 255 );	// Alien Swarm
			}*/
			else // Not focused
			{
				//col.SetColor( 125, 125, 125, 255 );
				//col.SetColor( 83, 148, 192, 255 );
				col.SetColor(177, 177, 177, 255);
			}
			break;
		case Disabled:
			//col.SetColor( 88, 97, 104, 255 );
			//col.SetColor( 65, 74, 96, 255 );
			//col.SetColor( 32, 59, 82, 255 );
			/*if (bSwarmLook)
				col.SetColor( 32, 59, 82, 255 );
			else*/
			//col.SetColor(255, 255, 255, 255);
			col.SetColor(177, 177, 177, 255);
			bDrawText = true;
			bDrawGlow = false;
			break;
		case FocusDisabled:
			col.SetColor( 182, 189, 194, 255 );
			bDrawText = false;
			bDrawGlow = true;
			break;
		case Open:
			// flyout menu is attached
			//col.SetColor( 200, 200, 200, 255 );
			col.SetColor( 169, 213, 255, 255 );
			bDrawGlow = true;
			bDrawCursor = true;
			if ( IsOrangeBoxMenuStyle() )
			{
				// Srcbox: an Orange Box menu item with its flyout open looks like the
				// highlighted item (white, no glow, which also wasn't letter-spaced)
				col.SetColor( 255, 255, 255, 255 );
				bDrawGlow = false;
			}
			break;
		case Focus:
			// active item
			col.SetColor( 255, 255, 255, 255 );
			// Srcbox: the Orange Box highlight on every focused item but the game mode
			// carousel; the Orange Box menus had no glow
			bDrawGlow = !IsOrangeBoxMenuStyle();
			bAnimateGlow = bDrawGlow;
			if ( bSwarmLook )
			{
				bDrawCursor = ( m_nStyle == BUTTON_SIMPLE || m_nStyle == BUTTON_DROPDOWN || m_nStyle == BUTTON_DIALOG || m_nStyle == BUTTON_RED );
			}
			else
			{
				bDrawCursor = ( m_nStyle != BUTTON_GAMEMODE );
			}
			break;
	}

	wchar_t szUnicode[512];
	GetText( szUnicode, sizeof( szUnicode ) );
	int len = V_wcslen( szUnicode );
	if ( m_bAllCaps )
	{
		for ( int i = 0; i < len; i++ )
			szUnicode[i] = towupper( szUnicode[i] );
	}

	int textWide, textTall;
	surface()->GetTextSize( m_hTextFont, szUnicode, textWide, textTall );
	const int nTracking = IsOrangeBoxMenuStyle() ? OrangeBoxTracking() : 0;
	if ( nTracking && len > 1 )
		textWide += nTracking * ( len - 1 );

	textWide = clamp( textWide, 0, wide - m_textInsetX * 2 );
	textTall = clamp( textTall, 0, tall - m_textInsetX * 2 );

	int textInsetX = m_textInsetX;
	if ( bSwarmLook && m_nStyle == BUTTON_DIALOG )
	{
		// dialog buttons are centered
		textInsetX = ( wide - textWide ) / 2;
	}

	if ( FlyoutMenu::GetActiveMenu() && FlyoutMenu::GetActiveMenu()->GetNavFrom() != this )
	{
		bDrawCursor = false;
	}

	Color blotchColor = pScheme->GetColor("HybridButton.BlotchColor", Color(137, 82, 6, 255));
	Color blotchColor2 = pScheme->GetColor("HybridButton.BlotchColor2", Color(83, 63, 6, 255));
	Color borderColor = pScheme->GetColor("HybridButton.BorderColor", Color(195, 123, 6, 255));

	if ( bDrawCursor && bSwarmLook )
	{
		// Alien Swarm's cursor (gameui/swarm/vhybridbutton.cpp)
		Color swarmBlotch = pScheme->GetColor( "HybridButton.BlotchColor", Color( 0, 0, 0, 255 ) );
		Color swarmBorder = pScheme->GetColor( "HybridButton.BorderColor", Color( 0, 0, 0, 255 ) );

		// draw backing rectangle
		if ( curState == Open )
		{
			surface()->DrawSetColor( Color( 0, 0, 0, 255 ) );
			surface()->DrawFilledRectFade( x, y, x+wide, y+tall, 0, 255, true );
		}

		// draw blotch
		surface()->DrawSetColor( swarmBlotch );
		if ( m_nStyle == BUTTON_DIALOG )
		{
			int blotchWide = textWide;
			int blotchX = x + textInsetX;
			surface()->DrawFilledRectFade( blotchX, y, blotchX + 0.50f * blotchWide, y+tall, 0, 150, true );
			surface()->DrawFilledRectFade( blotchX + 0.50f * blotchWide, y, blotchX + blotchWide, y+tall, 150, 0, true );
		}
		else
		{
			int blotchWide = textWide + vgui::scheme()->GetProportionalScaledValueEx( GetScheme(), 40 );
			int blotchX = x + textInsetX;
			surface()->DrawFilledRectFade( blotchX, y, blotchX + 0.25f * blotchWide, y+tall, 0, 150, true );
			surface()->DrawFilledRectFade( blotchX + 0.25f * blotchWide, y, blotchX + blotchWide, y+tall, 150, 0, true );
		}

		// draw border lines
		surface()->DrawSetColor( swarmBorder );
		if ( curState == Open )
		{
			FlyoutMenu *pActiveFlyout = FlyoutMenu::GetActiveMenu();
			BaseModHybridButton *button = dynamic_cast< BaseModHybridButton* >( pActiveFlyout ? pActiveFlyout->GetNavFrom() : NULL );
			if ( pActiveFlyout && pActiveFlyout->GetOriginalTall() == 0 && button && button == this )
			{
				surface()->DrawFilledRectFade( x, y, x + wide, y+2, 255, 0, true );
			}
			else
			{
				// the border lines end at the beginning of the flyout
				// the flyout will draw to complete the look
				surface()->DrawFilledRectFade( x, y, x + wide, y+2, 0, 255, true );
				surface()->DrawFilledRectFade( x, y+tall-2, x + wide, y+tall, 0, 255, true );
			}
		}
		else
		{
			// top and bottom border lines
			surface()->DrawFilledRectFade( x, y, x + 0.5f * wide, y+2, 0, 255, true );
			surface()->DrawFilledRectFade( x + 0.5f * wide, y, x + wide, y+2, 255, 0, true );
			surface()->DrawFilledRectFade( x, y+tall-2, x + 0.5f * wide, y+tall, 0, 255, true );
			surface()->DrawFilledRectFade( x + 0.5f * wide, y+tall-2, x + wide, y+tall, 255, 0, true );
		}
	}
	else if ( bDrawCursor )
	{
		// Srcbox: the Orange Box highlight, over the whole button
		DrawOrangeBoxHighlight( GetWide(), GetTall() );
	}

	// Orange Box menu items have their text centred in the button
	const int nTextY = IsOrangeBoxMenuStyle() ? ( GetTall() - surface()->GetFontTall( m_hTextFont ) ) / 2 + m_textInsetY : y + m_textInsetY;

	// assume drawn, unless otherwise shortened with ellipsis
	int iLabelCharsDrawn = len;

	// draw the text
	if ( bDrawText )
	{
		int availableWidth = GetWide() - x - textInsetX;

		if ( m_bShowDropDownIndicator )
		{
			int textTall_ = vgui::surface()->GetFontTall( m_hTextFont );
			int textLen = 0;

			int len_ = wcslen( szUnicode );
			for ( int i=0;i<len_;i++ )
			{
				textLen += vgui::surface()->GetCharacterWidth( m_hTextFont, szUnicode[i] );
			}

			int imageX = x + textInsetX + textLen + m_iSelectedArrowSize / 2;
			int imageY = y + ( textTall_ - m_iSelectedArrowSize ) / 2 + 10;

			if ( ( imageX + m_iSelectedArrowSize ) > GetWide() )
			{
				imageX = GetWide() - m_iSelectedArrowSize;
			}

			if ( curState == Open || m_bOverrideDropDownIndicator )
			{
				vgui::surface()->DrawSetTexture( m_iSelectedArrow );
			}
			else
			{
				vgui::surface()->DrawSetTexture( m_iUnselectedArrow );
			}

			vgui::surface()->DrawSetColor( col );
			vgui::surface()->DrawTexturedRect( imageX, imageY, imageX + m_iSelectedArrowSize, imageY + m_iSelectedArrowSize + 10 );
			vgui::surface()->DrawSetTexture( 0 );

			availableWidth -= m_iSelectedArrowSize * 2;
		}

		vgui::surface()->DrawSetTextFont( m_hTextFont );
		// the plus 5 here is to fix centering the text, akin to orangebox itself. asw doesn't do this, and its retarded.
		vgui::surface()->DrawSetTextPos( x + textInsetX, nTextY );
		vgui::surface()->DrawSetTextColor( col );

		if ( textWide > availableWidth )
		{
			// length of 3 dots
			int ellipsesLen = 3 * vgui::surface()->GetCharacterWidth( m_hTextFont, L'.' );

			availableWidth -= ellipsesLen;

			iLabelCharsDrawn = 0;
			int charX = x + textInsetX;
			for ( int i=0; i < len; i++ )
			{
				vgui::surface()->DrawUnicodeChar( szUnicode[i] );
				iLabelCharsDrawn++;

				charX += vgui::surface()->GetCharacterWidth( m_hTextFont, szUnicode[i] );
				if ( charX >= ( x + textInsetX + availableWidth ) )
					break;
			}
			
			vgui::surface()->DrawPrintText( L"...", 3 );
		}
		else
		{
			if ( nTracking )
			{
				// letter by letter, spaced out
				int charX = x + textInsetX;
				for ( int i = 0; i < len; i++ )
				{
					vgui::surface()->DrawSetTextPos( charX, nTextY );
					vgui::surface()->DrawUnicodeChar( szUnicode[i] );
					charX += vgui::surface()->GetCharacterWidth( m_hTextFont, szUnicode[i] ) + nTracking;
				}
			}
			else
			{
				vgui::surface()->DrawUnicodeString( szUnicode );
			}
		}
	}
	else if ( GetCurrentState() == Disabled || GetCurrentState() == FocusDisabled )
	{
		Color textcol = col;
		textcol[ 3 ] = 64;

		vgui::surface()->DrawSetTextFont( m_hTextFont );
		vgui::surface()->DrawSetTextPos( x + textInsetX, y + m_textInsetY + 5 );
		vgui::surface()->DrawSetTextColor( textcol );
		vgui::surface()->DrawPrintText( szUnicode, len );
	}

	// draw the help text
	if ( m_nStyle == BUTTON_GAMEMODE && curState != Open )
	{
		const char *pHelpText = GetHelpText( IsEnabled() );
		wchar_t szHelpUnicode[512];
		wchar_t *pUnicodeString;
		if ( pHelpText && pHelpText[0] == '#' )
		{
			pUnicodeString = g_pVGuiLocalize->Find( pHelpText );
		}
		else
		{
			g_pVGuiLocalize->ConvertANSIToUnicode( pHelpText, szHelpUnicode, sizeof( szHelpUnicode ) );
			pUnicodeString = szHelpUnicode;
		}	
		vgui::surface()->DrawSetTextFont( m_hHintTextFont );
		vgui::surface()->DrawSetTextPos( x + textInsetX, y + m_textInsetY + m_nTextFontHeight );
		vgui::surface()->DrawSetTextColor( col );
		vgui::surface()->DrawPrintText( pUnicodeString, wcslen( pUnicodeString ) );
	}

	// draw the pulsing glow
	if ( bDrawGlow )
	{
		if ( !bDrawText )
		{
			vgui::surface()->DrawSetTextColor( col );
		}
		else
		{
			int alpha = bAnimateGlow ? 60.0f + 30.0f * sin( Plat_FloatTime() * 4.0f ) : 30;
			Color glowColor( 255, 255, 255, alpha );
			vgui::surface()->DrawSetTextColor( glowColor );
		}
		vgui::surface()->DrawSetTextFont( m_hTextBlurFont );
		vgui::surface()->DrawSetTextPos( x + textInsetX, nTextY );

		int glowX = x + textInsetX;
		for ( int i=0; i<len && i < iLabelCharsDrawn; i++ )
		{
			if ( nTracking )
			{
				// keep the glow on the letter-spaced text
				vgui::surface()->DrawSetTextPos( glowX, nTextY );
				glowX += vgui::surface()->GetCharacterWidth( m_hTextFont, szUnicode[i] ) + nTracking;
			}
			vgui::surface()->DrawUnicodeChar( szUnicode[i] );
		}
	}

	if ( m_nStyle == BUTTON_DROPDOWN && curState != Open )
	{
		// draw right justified item
		wchar_t *pUnicodeString = g_pVGuiLocalize->Find( m_DropDownSelection.String() );
		if ( !pUnicodeString )
		{
			g_pVGuiLocalize->ConvertANSIToUnicode( m_DropDownSelection.String(), szUnicode, sizeof( szUnicode ) );
			pUnicodeString = szUnicode;
		}
		int len_ = V_wcslen( pUnicodeString );

		int selTextWide, selTextTall;
		surface()->GetTextSize( m_hSelectionFont, pUnicodeString, selTextWide, selTextTall );

		// horizontal right justify
		int xx = wide - selTextWide - textInsetX;
		// vertical center within
		int yy = ( tall - selTextTall )/2;

		// draw the drop down selection text
		vgui::surface()->DrawSetTextFont( m_hSelectionFont );
		vgui::surface()->DrawSetTextPos( xx, yy );
		vgui::surface()->DrawSetTextColor( col );
		vgui::surface()->DrawPrintText( pUnicodeString, len_ );

		if ( bDrawGlow )
		{
			int alpha = bAnimateGlow ? 60.0f + 30.0f * sin( Plat_FloatTime() * 4.0f ) : 30;
			vgui::surface()->DrawSetTextColor( Color( 255, 255, 255, alpha ) );
			vgui::surface()->DrawSetTextFont( m_hSelectionBlurFont );
			vgui::surface()->DrawSetTextPos( xx, yy );
			vgui::surface()->DrawPrintText( pUnicodeString, len_ );
		}
	}
}

void BaseModHybridButton::Paint()
{
	// bypass ALL of CA's inept broken drawing code
	// not using VGUI controls, to much draw state is misconfigured by CA to salvage
	PaintButtonEx();

	// only do the forced selection for a single frame.
	m_isNavigateTo = false;
}

void BaseModHybridButton::ApplySettings( KeyValues * inResourceData )
{
	BaseClass::ApplySettings( inResourceData );

	// (this used to force 339 x 32 pixels whatever the resolution: proportional fonts
	// at 1080p are taller than 32 px, so the text was cut off at the bottom and long
	// labels at the right; sizes are fixed up at the end of this function instead)

	vgui::IScheme *scheme = vgui::scheme()->GetIScheme( GetScheme() );
	if( !scheme )
		return;

	char keyString[128];

	// if a style is specified attempt to load values in from the SCHEME file
	const char *style = inResourceData->GetString( "Style", NULL );
	if ( !style )
	{
		style = "DefaultButton";
	}

	// this is a total bypass of the CA look
	m_nStyle = BUTTON_SIMPLE;
	V_snprintf( keyString, sizeof( keyString ), "%s.Style", style );
	const char *pFormatString = scheme->GetResourceString( keyString );
	m_bSchemeStyle = ( pFormatString && pFormatString[0] );
	if ( m_bSchemeStyle )
	{
		m_nStyle = (ButtonStyle_t)atoi( pFormatString );
	}

	// "AllCaps" (Alien Swarm's Label has it, this vgui doesn't): upper case when drawn
	m_bAllCaps = inResourceData->GetBool( "AllCaps", false ) || inResourceData->GetBool( "allcaps", false );
	if ( m_nStyle == BUTTON_MAINMENU )
	{
		m_hTextFont = scheme->GetFont( "MainBold", true );
		m_hTextBlurFont = scheme->GetFont( "MainBoldBlur", true );
	}
	else if ( m_nStyle == BUTTON_FLYOUTITEM )
	{
		m_hTextFont = scheme->GetFont( "DefaultMedium", true );
		m_hTextBlurFont = scheme->GetFont( "DefaultMediumBlur", true );
	}
	else if ( m_nStyle == BUTTON_DROPDOWN )
	{
		// Srcbox: Alien Swarm's DefaultBold (Neo Sans 16) under its own name in this scheme
		m_hTextFont = scheme->GetFont( "SwarmOptionBold", true );
		m_hTextBlurFont = scheme->GetFont( "SwarmOptionBoldBlur", true );
		if ( m_hTextFont == vgui::INVALID_FONT )
		{
			m_hTextFont = scheme->GetFont( "DefaultBold", true );
			m_hTextBlurFont = scheme->GetFont( "DefaultBoldBlur", true );
		}
		m_hSelectionFont = scheme->GetFont( "DefaultMedium", true );
		m_hSelectionBlurFont = scheme->GetFont( "DefaultMediumBlur", true );
		m_nWideAtOpen = vgui::scheme()->GetProportionalScaledValue( inResourceData->GetInt( "wideatopen", 0 ) );
	}
	else if ( m_nStyle == BUTTON_DIALOG || ( m_nStyle == BUTTON_SIMPLE && m_bSchemeStyle ) )
	{
		m_hTextFont = scheme->GetFont( "SwarmOptionBold", true );
		m_hTextBlurFont = scheme->GetFont( "SwarmOptionBoldBlur", true );
		if ( m_hTextFont == vgui::INVALID_FONT )
		{
			m_hTextFont = scheme->GetFont( "DefaultBold", true );
			m_hTextBlurFont = scheme->GetFont( "DefaultBoldBlur", true );
		}
	}
	else if ( m_nStyle == BUTTON_RED || m_nStyle == BUTTON_REDMAIN )
	{
		m_hTextFont = scheme->GetFont( "DefaultBold", true );
		m_hTextBlurFont = scheme->GetFont( "DefaultBoldBlur", true );
	}
	else if ( m_nStyle == BUTTON_SMALL )
	{
		m_hTextFont = scheme->GetFont( "DefaultVerySmall", true );
		m_hTextBlurFont = scheme->GetFont( "DefaultVerySmall", true );
	}
	else if ( m_nStyle == BUTTON_MEDIUM )
	{
		m_hTextFont = scheme->GetFont( "DefaultMedium", true );
		m_hTextBlurFont = scheme->GetFont( "DefaultMediumBlur", true );
	}
	else if ( m_nStyle == BUTTON_GAMEMODE )
	{
		m_hTextFont = scheme->GetFont( "MainBold", true );
		m_hTextBlurFont = scheme->GetFont( "MainBoldBlur", true );
		m_hHintTextFont = scheme->GetFont( "Default", true );
		m_nWideAtOpen = vgui::scheme()->GetProportionalScaledValue( inResourceData->GetInt( "wideatopen", 0 ) );
	}
	else if ( m_nStyle == BUTTON_MAINMENUSMALL )
	{
		m_hTextFont = scheme->GetFont( "DefaultBold", true );
		m_hTextBlurFont = scheme->GetFont( "DefaultBoldBlur", true );
	}
	else if ( m_nStyle == BUTTON_ALIENSWARMMENUBUTTON || m_nStyle == BUTTON_ALIENSWARMMENUBUTTONSMALL )
	{
		// Orange Box menu font (resource/SwarmSchemeNew.res), else the old ones
		m_hTextFont = scheme->GetFont( "MenuButtonOB", true );
		if ( m_hTextFont == vgui::INVALID_FONT )
			m_hTextFont = scheme->GetFont( m_nStyle == BUTTON_ALIENSWARMMENUBUTTON ? "DefaultBold" : "DefaultMedium", true );
		m_hTextBlurFont = m_hTextFont;
	}
	else if ( m_nStyle == BUTTON_ALIENSWARMDEFAULT )
	{
		m_hTextFont = scheme->GetFont( "Default", true );
		m_hTextBlurFont = scheme->GetFont( "DefaultBlur", true );
	}
	else
	{
		m_nStyle = BUTTON_SIMPLE;
		m_hTextFont = scheme->GetFont( "DefaultBold", true );
		m_hTextBlurFont = scheme->GetFont( "DefaultBoldBlur", true );
	}
	
	m_nTextFontHeight = vgui::surface()->GetFontTall( m_hTextFont );
	m_nHintTextFontHeight = vgui::surface()->GetFontTall( m_hHintTextFont );

	// tooltips
	const char* enabledToolTipText = inResourceData->GetString( "tooltiptext" , NULL );
	if ( enabledToolTipText )
	{
		SetHelpText( enabledToolTipText, true );
	}

	const char* disabledToolTipText = inResourceData->GetString( "disabled_tooltiptext", NULL );
	if ( disabledToolTipText )
	{
		SetHelpText( disabledToolTipText, false );
	}

	//vgui's standard handling of the tabPosition tag doesn't properly navigate for the X360
	if ( inResourceData->GetInt( "tabPosition", 0 ) == 1 )
	{
		NavigateTo();
	}

	SetFont( m_hTextFont );

	//Get the x text inset
	V_snprintf( keyString, sizeof( keyString ), "%s.%s", style, "TextInsetX" );
	const char *result = scheme->GetResourceString( keyString );
	if ( *result != 0 )
	{
		m_textInsetX = atoi( result );
		m_textInsetX = vgui::scheme()->GetProportionalScaledValueEx( GetScheme(), m_textInsetX );
	}

	//Get the y text inset
	V_snprintf( keyString, sizeof( keyString ), "%s.%s", style, "TextInsetY" );
	result = scheme->GetResourceString( keyString );
	if( *result != 0 )
	{
		m_textInsetY = atoi( result );
		m_textInsetY = vgui::scheme()->GetProportionalScaledValueEx( GetScheme(), m_textInsetY );
	}

	//0 = press and release
	//1 = press
	//2 = release
	int activationType = inResourceData->GetInt( "ActivationType", 1 );
	clamp( activationType, 0, 2 );
	SetButtonActivationType( static_cast< vgui::Button::ActivationType_t >( activationType ) );

	int x, y, wide, tall;
	GetBounds( x, y, wide, tall );
	m_originalTall = tall;

	if ( m_nStyle == BUTTON_GAMEMODE )
	{
		// needs to be exact height sized
		tall = m_nTextFontHeight + m_nHintTextFontHeight;
		SetSize( wide, tall );
		m_originalTall = m_nTextFontHeight;
	}

	m_bIgnoreButtonA = ( inResourceData->GetInt( "IgnoreButtonA", 0 ) != 0 );

	m_bShowDropDownIndicator = ( inResourceData->GetInt( "ShowDropDownIndicator", 0 ) != 0 );
	m_bOverrideDropDownIndicator = false;

	m_iSelectedArrowSize = vgui::scheme()->GetProportionalScaledValue( inResourceData->GetInt( "DropDownIndicatorSize", 8 ) );

	// Srcbox: make sure the text fits
	{
		wchar_t szText[512];
		GetText( szText, sizeof( szText ) );
		int textWide = 0, textTall = 0;
		surface()->GetTextSize( m_hTextFont, szText, textWide, textTall );
		const int nTextLen = V_wcslen( szText );
		if ( IsOrangeBoxMenuStyle() && nTextLen > 1 )
			textWide += OrangeBoxTracking() * ( nTextLen - 1 );
		const int nFontTall = surface()->GetFontTall( m_hTextFont );

		GetBounds( x, y, wide, tall );
		if ( IsOrangeBoxMenuStyle() )
		{
			// Orange Box rows: 47 px tall at 1080 for 27 px text, text 17 px in, the
			// highlight fading out ~560 px from the left
			const float flScale = OrangeBoxScale();
			if ( m_textInsetX <= 0 )
				m_textInsetX = (int)( 17 * flScale + 0.5f );
			tall = nFontTall + (int)( 20 * flScale + 0.5f );
			wide = MAX( wide, MAX( (int)( 560 * flScale ), textWide + m_textInsetX * 2 ) );
			m_textInsetY = 0;
		}
		else if ( m_nStyle != BUTTON_GAMEMODE )
		{
			tall = MAX( tall, nFontTall + m_textInsetY * 2 + 2 );
			wide = MAX( wide, textWide + m_textInsetX * 2 );
		}
		SetSize( wide, tall );
		if ( m_nStyle != BUTTON_GAMEMODE )
			m_originalTall = tall;
	}
}


void BaseModHybridButton::ApplySchemeSettings( vgui::IScheme *pScheme )
{
	BaseClass::ApplySchemeSettings( pScheme );

	SetReleasedSound( CBaseModPanel::GetSingleton().GetUISoundName( UISOUND_ACCEPT ) );

	const char *pImageName;

	// use find or create pattern, avoid pointless redundant i/o
	pImageName = "vgui/icon_arrow_down";
	m_iSelectedArrow = vgui::surface()->DrawGetTextureId( pImageName );
	if ( m_iSelectedArrow == -1 )
	{
		m_iSelectedArrow = vgui::surface()->CreateNewTextureID();
		vgui::surface()->DrawSetTextureFile( m_iSelectedArrow, pImageName, true, false );	
	}

	// use find or create pattern, avoid pointles redundant i/o
	pImageName = "vgui/icon_arrow";
	m_iUnselectedArrow = vgui::surface()->DrawGetTextureId( pImageName );
	if ( m_iUnselectedArrow == -1 )
	{
		m_iUnselectedArrow = vgui::surface()->CreateNewTextureID();
		vgui::surface()->DrawSetTextureFile( m_iUnselectedArrow, pImageName, true, false );	
	}
}

void BaseModHybridButton::OnKeyCodePressed( vgui::KeyCode code )
{
	KeyCode localCode = GetBaseButtonCode( code );

	if ( ( localCode == KEY_XBUTTON_A ) )
	{
		if ( m_bIgnoreButtonA )
		{
			// Don't swallow the a key... our parent wants it
			CallParentFunction(new KeyValues("KeyCodePressed", "code", code));
			return;
		}

		if ( !IsEnabled() )
		{
			CBaseModPanel::GetSingleton().PlayUISound( UISOUND_INVALID );
			return;
		}
	}

	BaseClass::OnKeyCodePressed( code );
}

void BaseModHybridButton::OnKeyCodeReleased( KeyCode keycode )
{
	//at some point vgui_controls/button.cpp got a 360 only change to never set the armed state to false ever. Too late to fix the logic behind that, just roll with it on PC
	//fixes bug where menu items de-highlight after letting go of the arrow key used to navigate to it
	bool bOldArmedState = IsArmed();

	BaseClass::OnKeyCodeReleased( keycode );

	if( bOldArmedState && !IsArmed() )
		SetArmed( true );
}

void BaseModHybridButton::SetDropdownSelection( const char *pText )
{
	m_DropDownSelection = pText;
}

void BaseModHybridButton::EnableDropdownSelection( bool bEnable )
{
	m_bDropDownSelection = bEnable;
}

void BaseModHybridButton::OnCursorEntered()
{
	BaseClass::OnCursorEntered();
	if ( !m_isOpen )
	{
		if ( IsEnabled() && !HasFocus() )
		{
			CBaseModPanel::GetSingleton().PlayUISound( UISOUND_FOCUS );
		}

		if( GetParent() )
		{
			GetParent()->NavigateToChild( this );
		}
		else
		{
			NavigateTo();
		}
	}
}

void BaseModHybridButton::OnCursorExited()
{
	// This is a hack for now, we shouldn't close if the cursor goes to the flyout of this item...
	// Maybe have VFloutMenu check the m_navFrom and it's one of these, keep the SetClosedState...
	BaseClass::OnCursorExited();

//	SetClosed();
}

// Message targets that the button has been pressed
void BaseModHybridButton::FireActionSignal( void )
{
	BaseClass::FireActionSignal();

	PostMessageToAllSiblingsOfType< BaseModHybridButton >( new KeyValues( "OnSiblingHybridButtonOpened" ) );
}


Panel* BaseModHybridButton::NavigateUp()
{
	Panel *target = BaseClass::NavigateUp();
	if ( !target && 
		(dynamic_cast< DropDownMenu * >( GetParent() ) || dynamic_cast< SliderControl * >( GetParent() )) )
	{
		target = GetParent()->NavigateUp();
	}

	return target;
}

Panel* BaseModHybridButton::NavigateDown()
{
	Panel *target = BaseClass::NavigateDown();
	if ( !target && 
		(dynamic_cast< DropDownMenu * >( GetParent() ) || dynamic_cast< SliderControl * >( GetParent() )) )
	{
		target = GetParent()->NavigateDown();
	}
	return target;
}

Panel* BaseModHybridButton::NavigateLeft()
{
	Panel *target = BaseClass::NavigateLeft();
	if ( !target && 
		dynamic_cast< DropDownMenu * >( GetParent() ) )
	{
		target = GetParent()->NavigateLeft();
	}
	return target;
}

Panel* BaseModHybridButton::NavigateRight()
{
	Panel *target = BaseClass::NavigateRight();
	if ( !target && 
		dynamic_cast< DropDownMenu * >( GetParent() ) )
	{
		target = GetParent()->NavigateRight();
	}
	return target;
}
