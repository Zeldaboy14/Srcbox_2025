//The following include files are necessary to allow your MyPanel.cpp to compile.
#include "cbase.h"
#include "vgui_savenquit.h"
#include <vgui/IVGui.h>
#include <vgui_controls/Frame.h>
#include <vgui_controls/Button.h>
#include <vgui_controls/PropertySheet.h>
#include <vgui_controls/ListPanel.h>
#include <filesystem.h>
#include <KeyValues.h>
#include "ienginevgui.h"
#include <vgui_controls/PanelListPanel.h>
#include <vgui_controls/ImagePanel.h>
#include "vgui_controls/Menu.h"
#include <vgui_controls/TextEntry.h>

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

class CSaveBeforeQuitQueryDialog : public vgui::Frame
{
	DECLARE_CLASS_SIMPLE(CSaveBeforeQuitQueryDialog, vgui::Frame);

public:
	CSaveBeforeQuitQueryDialog(vgui::VPANEL parent);
	~CSaveBeforeQuitQueryDialog() {}

protected:
	virtual void OnTick();
	virtual void OnCommand(const char* pcCommand) override;

private:
	vgui::Frame* m_pQuitPanel;
	vgui::Label* m_pQuitConfirmText;
	Button* m_pCancelButton;
	Button* m_pQuitButton;
};

ConVar cancel("", "0", FCVAR_CLIENTDLL, "");

CSaveBeforeQuitQueryDialog::CSaveBeforeQuitQueryDialog(vgui::VPANEL parent)
	: BaseClass(nullptr, "SaveBeforeQuitQueryDialog")
{
	SetParent(parent);

	SetKeyBoardInputEnabled(true);
	SetMouseInputEnabled(true);

	SetProportional(false);
	SetTitleBarVisible(true);
	SetSizeable(false);
	SetMinimizeButtonVisible(false);
	SetMaximizeButtonVisible(false);
	SetCloseButtonVisible(false);
	SetMoveable(true);
	SetTabPosition(2);
	PaintBackground();
	MoveToCenterOfScreen();

	SetTitle("#GameUI_Quit", false);

	SetScheme(vgui::scheme()->LoadSchemeFromFile("resource/SourceScheme.res", "SourceScheme"));

	LoadControlSettings("resource/savebeforequitdialog.res");

	SetVisible(false);
}

// Class for managing panel instance
class CSaveBeforeQuitQueryDialogInterface : public ISaveBeforeQuitDialog
{
private:
	CSaveBeforeQuitQueryDialog* m_pPanel;

public:
	CSaveBeforeQuitQueryDialogInterface()
		: m_pPanel(nullptr) {}

	void Create(vgui::VPANEL parent) override
	{
		if (!m_pPanel)
		{
			m_pPanel = new CSaveBeforeQuitQueryDialog(parent);
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

static CSaveBeforeQuitQueryDialogInterface g_SavebeforeQuit;
ISaveBeforeQuitDialog* savebeforequitdialog = (ISaveBeforeQuitDialog*)&g_SavebeforeQuit;

class CReturntoMainMenuDialog : public vgui::Frame
{
	DECLARE_CLASS_SIMPLE(CReturntoMainMenuDialog, vgui::Frame);

public:
	CReturntoMainMenuDialog(vgui::VPANEL parent);
	~CReturntoMainMenuDialog() {}

protected:
	virtual void OnTick();
	virtual void OnCommand(const char* pcCommand) override;

private:
	vgui::Frame* m_pQuitPanel;
	vgui::Label* m_pQuitConfirmText;
	Button* m_pCancelButton;
	Button* m_pQuitButton;
};

CReturntoMainMenuDialog::CReturntoMainMenuDialog(vgui::VPANEL parent)
	: BaseClass(nullptr, "")
{
	SetParent(parent);

	SetKeyBoardInputEnabled(true);
	SetMouseInputEnabled(true);

	SetProportional(false);
	SetTitleBarVisible(true);
	SetSizeable(false);
	SetMinimizeButtonVisible(false);
	SetMaximizeButtonVisible(false);
	SetCloseButtonVisible(false);
	SetMoveable(true);
	SetTabPosition(2);
	PaintBackground();
	MoveToCenterOfScreen();

	SetScheme(vgui::scheme()->LoadSchemeFromFile("resource/SourceScheme.res", "SourceScheme"));

	SetTitle("#GameUI_GameMenu_ReturnToMainMenu", false);
	m_pQuitPanel = new vgui::Frame(this, "QuitQueryDialog");
	SetBounds(656, 384, 288, 132);
	SetScheme(vgui::scheme()->LoadSchemeFromFile("resource/SourceScheme.res", "SourceScheme"));

	m_pQuitConfirmText = new vgui::Label(this, "", "#GameUI_MainMenuWarning");
	m_pQuitConfirmText->SetBounds(54, 35, 190, 32);

	m_pQuitButton = new Button(this, "", "#GameUI_OK");
	m_pQuitButton->SetCommand("OK");
	m_pQuitButton->SetTabPosition(1);
	m_pQuitButton->SetBounds(78, 85, 64, 26);
	m_pQuitButton->SetVisible(true);

	m_pCancelButton = new Button(this, "", "#QueryBox_Cancel");
	m_pCancelButton->SetCommand("Cancel");
	m_pCancelButton->SetBounds(158, 85, 64, 26);
	m_pCancelButton->SetVisible(true);

	SetVisible(false);
}

// Class for managing panel instance
class CReturntoMainMenuInterface : public IReturntoMainMenuDialog
{
private:
	CReturntoMainMenuDialog* m_pPanel;

public:
	CReturntoMainMenuInterface()
		: m_pPanel(nullptr) {
	}

	void Create(vgui::VPANEL parent) override
	{
		if (!m_pPanel)
		{
			m_pPanel = new CReturntoMainMenuDialog(parent);
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

static CReturntoMainMenuInterface g_ReturntoMainMenu;
IReturntoMainMenuDialog* returntomainmenudialog = (IReturntoMainMenuDialog*)&g_ReturntoMainMenu;

class CQuitQueryBoxDialog : public vgui::Frame
{
	DECLARE_CLASS_SIMPLE(CQuitQueryBoxDialog, vgui::Frame);

public:
	CQuitQueryBoxDialog(vgui::VPANEL parent);
	~CQuitQueryBoxDialog() {}

protected:
	virtual void OnTick();
	virtual void OnCommand(const char* pcCommand) override;

private:
	void ImagePanel();
	vgui::Frame* m_pQuitPanel;
	vgui::Label* m_pQuitConfirmText;
	Button* m_pCancelButton;
	Button* m_pQuitButton;
};

CQuitQueryBoxDialog::CQuitQueryBoxDialog(vgui::VPANEL parent)
	: BaseClass(nullptr, "QuitQueryDialog")
{

	SetParent(parent);

	SetKeyBoardInputEnabled(true);
	SetMouseInputEnabled(true);

	SetProportional(false);
	SetTitleBarVisible(true);
	SetSizeable(false);
	SetMinimizeButtonVisible(false);
	SetMaximizeButtonVisible(false);
	SetCloseButtonVisible(false);
	SetMoveable(true);
	SetTabPosition(2);
	PaintBackground();
	MoveToCenterOfScreen();

	/*vgui::ImagePanel* pGameBackground = new vgui::ImagePanel(this, "Back");
	pGameBackground->SetImage("../vgui/appchooser/background_hl2");
	pGameBackground->SetPos(0, 0);
	pGameBackground->SetSize(1600, 900);
	pGameBackground->SetShouldScaleImage(1);
	pGameBackground->SetScaleAmount(1);*/
	//GetWindowPriority();
	SetTitle("#GameUI_QuitConfirmationTitle", false);

	m_pQuitPanel = new vgui::Frame(this, "QuitQueryDialog");
	SetBounds(655, 392, 290, 116);
	SetScheme(vgui::scheme()->LoadSchemeFromFile("resource/SourceScheme.res", "SourceScheme"));

	m_pQuitConfirmText = new vgui::Label(this, "", "#GameUI_QuitConfirmationText");
	m_pQuitConfirmText->SetBounds(54, 31, 190, 16);

	m_pQuitButton = new Button(this, "", "#GameUI_Quit");
	m_pQuitButton->SetCommand("OK");
	m_pQuitButton->SetTabPosition(1);
	m_pQuitButton->SetBounds(73, 69, 75, 26);
	m_pQuitButton->SetVisible(true);

	m_pCancelButton = new Button(this, "", "#QueryBox_Cancel");
	m_pCancelButton->SetCommand("OnCancel");
	//m_pCancelButton->SetTabPosition(2);
	m_pCancelButton->SetBounds(165, 69, 64, 26); // Adjust position and size
	m_pCancelButton->SetVisible(true);

	SetVisible(false);
}

// Class for managing panel instance
class CQuitQueryBoxDialogInterface : public IQuitQueryBoxDialog
{
private:
	CQuitQueryBoxDialog* m_pPanel;

public:
	CQuitQueryBoxDialogInterface()
		: m_pPanel(nullptr) {
	}

	void Create(vgui::VPANEL parent) override
	{
		if (!m_pPanel)
		{
			m_pPanel = new CQuitQueryBoxDialog(parent);
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

static CQuitQueryBoxDialogInterface g_QuitQueryBoxDialog;
IQuitQueryBoxDialog* quitqueryboxdialog = (IQuitQueryBoxDialog*)&g_QuitQueryBoxDialog;

CON_COMMAND(OpenSaveBeforeQuitDialog, "")
{
	savebeforequitdialog->Activate();
};

CON_COMMAND(OpenReturnToMainMenuDialog, "")
{
	returntomainmenudialog->Activate();
};

CON_COMMAND(OpenQuitDialog, "")
{
	quitqueryboxdialog->Activate();
};

void CSaveBeforeQuitQueryDialog::OnTick()
{
	BaseClass::OnTick();
	SetVisible(cancel.GetBool());
}

void CSaveBeforeQuitQueryDialog::OnCommand(const char* pcCommand)
{
	if (FStrEq(pcCommand, "Cancel"))
	{
		SetVisible(false);
		engine->ClientCmd("OpenInGameMainMenu");
	}

	if (FStrEq(pcCommand, "Quit"))
	{
		engine->ClientCmd("disconnect");
	}
}

void CQuitQueryBoxDialog::OnTick()
{
	BaseClass::OnTick();
	SetVisible(cancel.GetBool());
}

void CReturntoMainMenuDialog::OnCommand(const char* pcCommand)
{
	if (FStrEq(pcCommand, "Cancel"))
	{
		SetVisible(false);
		engine->ClientCmd("OpenInGameMainMenu");
	}

	if (FStrEq(pcCommand, "Ok"))
	{
		SetVisible(false);
		engine->ClientCmd("disconnect");
	}
}

void CReturntoMainMenuDialog::OnTick()
{
	BaseClass::OnTick();
	SetVisible(cancel.GetBool());
}

void CQuitQueryBoxDialog::OnCommand(const char* pcCommand)
{
	if (FStrEq(pcCommand, "OnCancel"))
	{
		engine->ClientCmd("OpenMainMenu");
		Close();
	}

	if (FStrEq(pcCommand, "OK"))
	{
		SetVisible(false);
		engine->ClientCmd("quit");
	}
}

































