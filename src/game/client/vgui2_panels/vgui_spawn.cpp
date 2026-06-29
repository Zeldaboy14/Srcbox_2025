#include "cbase.h"
#include "vgui_spawn.h"
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
#include "vgui/ISurface.h"

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

class CSpawnMenu : public vgui::Frame
{
	DECLARE_CLASS_SIMPLE(CSpawnMenu, vgui::Frame);

public:
	CSpawnMenu(vgui::VPANEL parent);
	~CSpawnMenu() {}

protected:
	virtual void OnTick();
	virtual void OnCommand(const char* pcCommand) override;

private:
	void ImagePanel();
	void Paint();
	Button* Next;
};

ConVar spawnmenu("menu", "0", FCVAR_CLIENTDLL, "");

CSpawnMenu::CSpawnMenu(vgui::VPANEL parent)
	: BaseClass(nullptr, "SpawnMenu")
{
	SetParent(parent);

	SetKeyBoardInputEnabled(true);
	SetMouseInputEnabled(true);

	SetProportional(true);
	SetTitleBarVisible(true);
	SetSizeable(false);
	SetMoveable(false);
	SetTitle("#GameUI_NewGame", false);

	SetScheme(vgui::scheme()->LoadSchemeFromFile("resource/SourceScheme.res", "SourceScheme"));

	//LoadControlSettings("resource/newgamedialog.res");
	//ImagePanel();

	SetVisible(false);
}

void CSpawnMenu::Paint(void)
{
	BaseClass::Paint();
	surface()->DrawSetColor(255, 0, 0, 255); //RGBA
	surface()->DrawFilledRect(0, 0, 20, 20); //x0,y0,x1,y1
}

void CSpawnMenu::ImagePanel()
{
	LoadControlSettings("resource/newgamechapterpanel.res");
	SetVisible(true);
}

// Class for managing panel instance
class CSpawnMenuInterface : public ISpawnMenu
{
private:
	CSpawnMenu* m_pPanel;

public:
	CSpawnMenuInterface()
		: m_pPanel(nullptr) {}

	void Create(vgui::VPANEL parent) override
	{
		if (!m_pPanel)
		{
			m_pPanel = new CSpawnMenu(parent);
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

static CSpawnMenuInterface g_CSpawnMenuDialog;
ISpawnMenu* spawnmenudialog = (ISpawnMenu*)&g_CSpawnMenuDialog;

void CSpawnMenu::OnTick()
{
	BaseClass::OnTick();
	SetVisible(spawnmenu.GetBool());
}

CON_COMMAND(OpenSpawnMenu, "menu")
{
	spawnmenudialog->Activate();
};

void CSpawnMenu::OnCommand(const char* pcCommand)
{
	/*if (stricmp(pcCommand, "menu"))
	{
		//spawnmenudialog->Activate();
		//SetVisible(cl_spawnmenu.GetBool());
	}
	if (FStrEq(pcCommand, "Close"))
	{
		SetVisible(false);
	}*/
}

































