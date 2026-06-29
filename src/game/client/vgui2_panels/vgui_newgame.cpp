//The following include files are necessary to allow your MyPanel.cpp to compile.
#include "cbase.h"
#include "vgui_newgame.h"
#include <vgui/IVGui.h>
#include <vgui_controls/Frame.h>
#include <vgui_controls/Button.h>
#include <vgui_controls/PropertySheet.h>
#include <vgui_controls/ListPanel.h>
#include <vgui_controls/ComboBox.h>
#include <vgui/ISurface.h>
#include <filesystem.h>
#include <KeyValues.h>
#include "ienginevgui.h"
#include <vgui_controls/PanelListPanel.h>
#include <vgui_controls/ImagePanel.h>
#include "vgui_controls/Menu.h"

#include <stdlib.h>

using namespace vgui;

//-----------------------------------------------------------------------------
// Used by the autocompletion system
//-----------------------------------------------------------------------------
class CNonFocusableMenu : public Menu
{
	DECLARE_CLASS_SIMPLE(CNonFocusableMenu, Menu);

public:
	CNonFocusableMenu(Panel *parent, const char *panelName)
		: BaseClass(parent, panelName),
		m_pFocus(0)
	{
	}

	void SetFocusPanel(Panel *panel)
	{
		m_pFocus = panel;
	}

	VPANEL GetCurrentKeyFocus()
	{
		if (!m_pFocus)
			return GetVPanel();

		return m_pFocus->GetVPanel();
	}

private:
	Panel		*m_pFocus;
};

class CNewGameDialog : public vgui::Frame
{
	DECLARE_CLASS_SIMPLE(CNewGameDialog, vgui::Frame);

public:
	CNewGameDialog(vgui::VPANEL parent);
	~CNewGameDialog() {}

protected:
	virtual void OnTick();
	virtual void OnCommand(const char* pcCommand) override;

private:
	void ImagePanel();
	Button* Next;
	Button* Prev;
	Button* Play;
	Button* Close;
	Button* Cancel;
};

ConVar close("close", "0", FCVAR_CLIENTDLL, "");

CNewGameDialog::CNewGameDialog(vgui::VPANEL parent)
	: BaseClass(nullptr, "NewGameDialog")
{
	SetParent(parent);

	SetKeyBoardInputEnabled(true);
	SetMouseInputEnabled(true);

	SetProportional(false);
	SetTitleBarVisible(true);
	SetSizeable(false);
	SetMoveable(true);
	SetTitle("#GameUI_NewGame", false);

	SetScheme(vgui::scheme()->LoadSchemeFromFile("resource/SourceScheme.res", "SourceScheme"));

	LoadControlSettings("resource/newgamedialog.res");

	int w, h;
	GetSize(w, h);

	int sw, sh;
	vgui::surface()->GetScreenSize(sw, sh);

	SetPos((sw - w) / 2, (sh - h) / 2);
	//ImagePanel();

	SetVisible(false);
}

void CNewGameDialog::ImagePanel()
{
	LoadControlSettings("resource/newgamechapterpanel.res");
	SetVisible(true);
}

// Class for managing panel instance
class CNewGameDialogInterface : public INewGameDialog
{
private:
	CNewGameDialog* m_pPanel;

public:
	CNewGameDialogInterface()
		: m_pPanel(nullptr) {}

	void Create(vgui::VPANEL parent) override
	{
		if (!m_pPanel)
		{
			m_pPanel = new CNewGameDialog(parent);
		}
	}

	void Destroy() override
	{
		if (m_pPanel)
		{
			m_pPanel->SetParent(nullptr);
			delete m_pPanel;
			m_pPanel = nullptr;
		}
	}

	void Activate() override
	{
		if (m_pPanel)
		{
			m_pPanel->Activate();
		}
	}
};

static CNewGameDialogInterface g_NewGameDialog;
INewGameDialog* newgamedialog = (INewGameDialog*)&g_NewGameDialog;

void CNewGameDialog::OnTick()
{
	BaseClass::OnTick();
	SetVisible(close.GetBool());
}

CON_COMMAND(OpenNewGameDialog, "")
{
	newgamedialog->Activate();
};

void CNewGameDialog::OnCommand(const char* pcCommand)
{
	if (FStrEq(pcCommand, "Close"))
	{
		SetVisible(false);
	}
}

































