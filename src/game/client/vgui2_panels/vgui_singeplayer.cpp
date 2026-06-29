//The following include files are necessary to allow your MyPanel.cpp to compile.
#include "cbase.h"
#include "vgui_singeplayer.h"
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
#include "gamestats.h"
#include <vgui_controls/PanelListPanel.h>
#include <vgui_controls/ImagePanel.h>
#include "hl2mp_gamerules.h"

//Swarm wants YOU!
#include "vgui_controls/Menu.h"

#include <stdlib.h>

extern const char* GetGameTypeID;

extern ConVar srcbox_gamemode_sandbox;
extern ConVar srcbox_gamemode_hl2;

using namespace vgui;

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

class CMyPanel : public vgui::Frame
{
	DECLARE_CLASS_SIMPLE(CMyPanel, vgui::Frame);

public:
	CMyPanel(vgui::VPANEL parent);
	~CMyPanel() {}

protected:
	virtual void OnTick();
	virtual void OnCommand(const char* pcCommand) override;

private:
	PropertySheet* m_pTabPanel;
	Panel* m_pBox;
	ListPanel* m_pBrowseAllList;
	ComboBox* m_pGamemode;
	Button* m_pPlayButton;
	PanelListPanel* m_pBrowseAllPanel; // For "Browse All" tab
	PanelListPanel* m_pGameMapPanel;  // For "Game" tab to hold icons

	void PopulateBrowseAll();
	void PopulateGameTab();
	void PlaySelectedMap(); // Function to handle playing the selected map
	void CreateGameIcon(const char* mapName, const char* imagePath, const char* command);
};



CMyPanel::CMyPanel(vgui::VPANEL parent)
	: BaseClass(nullptr, "MyPanel")
{
	SetParent(parent);

	SetKeyBoardInputEnabled(true);
	SetMouseInputEnabled(true);
	SetProportional(false);
	SetTitleBarVisible(true);
	SetMinimizeButtonVisible(false);
	SetMaximizeButtonVisible(false);
	SetCloseButtonVisible(true);
	SetSizeable(false);
	SetMoveable(true);
	SetTitle("Play Singleplayer", false);

	SetWide(472);
	SetTall(980);

	SetScheme(vgui::scheme()->LoadSchemeFromFile("resource/SourceScheme.res", "SourceScheme"));

	//LoadControlSettings("resource/UI/singleplayer_srcbox.res");

	int w, h;
	GetSize(w, h);

	int sw, sh;
	vgui::surface()->GetScreenSize(sw, sh);

	SetPos((sw - w) / 2, (sh - h) / 2);

	m_pTabPanel = new PropertySheet(this, "TabPanel");
	m_pTabPanel->SetBounds(10, 40, 456, 894);
	m_pTabPanel->SetRoundedCorners(15);
	m_pTabPanel->ShouldDrawTopLeftCornerRounded();
	m_pTabPanel->ShouldDrawTopRightCornerRounded();
	m_pTabPanel->ShouldDrawBottomLeftCornerRounded();
	m_pTabPanel->ShouldDrawBottomRightCornerRounded();

	Panel* pGamePanel = new Panel(m_pTabPanel, "GamePanel");
	pGamePanel->SetPaintBackgroundEnabled(true);
	pGamePanel->SetBgColor(Color(50, 50, 50, 255));
	m_pGameMapPanel = new PanelListPanel(pGamePanel, "GameMapPanel");
	m_pGameMapPanel->SetBounds(10, 10, 430, 700);
	m_pGameMapPanel->SetFirstColumnWidth(200);
	m_pGameMapPanel->SetRoundedCorners(15);
	m_pGameMapPanel->GetRoundedCorners();
	m_pGameMapPanel->ShouldDrawTopLeftCornerRounded();
	m_pGameMapPanel->ShouldDrawTopRightCornerRounded();
	m_pGameMapPanel->ShouldDrawBottomLeftCornerRounded();
	m_pGameMapPanel->ShouldDrawBottomRightCornerRounded();

	PopulateGameTab();
	m_pTabPanel->AddPage(m_pGameMapPanel, "Gallery");
	m_pTabPanel->SetRoundedCorners(15);

	m_pBrowseAllList = new ListPanel(m_pTabPanel, "BrowseAllList");
	m_pBrowseAllList->SetPaintBackgroundEnabled(true);
	m_pBrowseAllList->SetBgColor(Color(50, 50, 50, 255));
	m_pTabPanel->SetRoundedCorners(15);

	m_pBrowseAllList->AddColumnHeader(0, "MapName", "Map Name", 350, ListPanel::COLUMN_FIXEDSIZE);
	m_pBrowseAllList->AddColumnHeader(1, "Game", "Game", 100, ListPanel::COLUMN_FIXEDSIZE);

	m_pTabPanel->AddPage(m_pBrowseAllList, "Browse All");

	PopulateBrowseAll();

	m_pGamemode = new ComboBox(this, "Gamemode", 5, false);
	m_pGamemode->SetBounds(125, 938, 171, 24);
	m_pGamemode->SetVisible(true);
	m_pGamemode->ActivateItem(0);

	if (m_pGamemode)
	{
		m_pGamemode->AddItem("Sandbox", new KeyValues("Gamemodes", "sandbox", "sandbox"));
		m_pGamemode->AddItem("Half-Life 2", new KeyValues("Gamemodes", "hl2", "hl2"));
	}

	m_pPlayButton = new Button(this, "PlayButton", "Start Game");
	m_pPlayButton->SetCommand("play_selected_map");
	m_pPlayButton->SetBounds(332, 938, 98, 24);
	m_pPlayButton->SetVisible(true);

	//m_pGamemodeText = new Button(this, "PlayButton", "Start Game");

	SetVisible(false);
}

void CMyPanel::PopulateBrowseAll()
{
	m_pBrowseAllList->RemoveAll();

	struct GameCategory
	{
		const char* gameName;
		const char* prefixes[30];
	};

	GameCategory categories[] = {
		{ "Gmod", { "gm_", nullptr } },
		{ "TF2", { "ctf_", "koth_", "cp_", "pl_", "mvm_", "pass_", "pd_", "sd_", "tc_", "vsh_", "zi_", "tr_", nullptr } },
		{ "CS:S", { "de_", "cs_", nullptr } },
		{ "HL2 Beta", { "d1_garage_", nullptr } },
		{ "HL:S", { "t0", nullptr } },
		{ "HL2", { "d1_trainstation", "d2_", "c17_", "ep1_", "ep1_", nullptr } },
		{ "HL2:DM", { "dm_", nullptr } },
		{ "NH2", { "nh2", nullptr } },
		{ "L4D", { "l4d_", nullptr } },
		{ "L4D2", { "c1m1_hotel", "c1m2_streets", "c1m3_mall", "c2m1_highway", nullptr } },
		{ "Unknown Game", { nullptr } } 
	};

	const char* mapsDir = "maps/*.bsp";
	FileFindHandle_t findHandle;
	const char* fileName = g_pFullFileSystem->FindFirst(mapsDir, &findHandle);

	while (fileName)
	{
		if (strstr(fileName, ".bsp"))
		{
			char mapName[MAX_PATH];
			Q_strncpy(mapName, fileName, sizeof(mapName));
			mapName[strlen(mapName) - 4] = '\0';

			const char* detectedGame = "Srcbox";
			for (const auto& category : categories)
			{
				for (int i = 0; category.prefixes[i] != nullptr; ++i)
				{
					if (Q_strnicmp(mapName, category.prefixes[i], strlen(category.prefixes[i])) == 0)
					{
						detectedGame = category.gameName;
						break;
					}
				}
			}

			KeyValues* kv = new KeyValues("MapData");
			kv->SetString("MapName", mapName);
			kv->SetString("Game", detectedGame);

			m_pBrowseAllList->AddItem(kv, 0, false, false);
			kv->deleteThis();
		}

		fileName = g_pFullFileSystem->FindNext(findHandle);
	}

	g_pFullFileSystem->FindClose(findHandle);
}

void CMyPanel::PopulateGameTab()
{
	// Clear previous entries
	if (m_pGameMapPanel)
	{
		m_pGameMapPanel->DeleteAllItems();
	}

	const int itemWidth = 150;
	const int itemHeight = 200;
	const int columns = 3;
	const int spacing = 1;

	int x = 10;
	int y = 10;
	int columnIndex = 0;

	const char* mapsDir = "maps/*.bsp";
	FileFindHandle_t findHandle;
	const char* fileName = g_pFullFileSystem->FindFirst(mapsDir, &findHandle);

	while (fileName)
	{
		if (strstr(fileName, ".bsp"))
		{
			char mapName[MAX_PATH];
			Q_strncpy(mapName, fileName, sizeof(mapName));
			mapName[strlen(mapName) - 4] = '\0';

			Panel* pMapContainer = new Panel(m_pGameMapPanel, mapName);
			pMapContainer->SetBounds(x, y, itemWidth, itemHeight);
			pMapContainer->SetPaintBackgroundEnabled(true);
			pMapContainer->SetBgColor(Color(60, 60, 60, 255));

			//ImagePanel* pImagePanel = new ImagePanel(pMapContainer, "MapThumbnail");
			//pImagePanel->SetBounds(-50 , 5, itemWidth - 10, 150);
			//pImagePanel->SetImage("../maps/no_ico");
			//pImagePanel->AddActionSignalTarget(this);

			m_pGameMapPanel->AddItem(nullptr, pMapContainer);

			columnIndex++;
			if (columnIndex >= columns)
			{
				columnIndex = 0;
				x = 10;                     
				y += itemHeight + spacing; 
			}
			else
			{
				x += itemWidth + spacing;
			}
		}

		fileName = g_pFullFileSystem->FindNext(findHandle);
	}

	g_pFullFileSystem->FindClose(findHandle);
}



void CMyPanel::PlaySelectedMap()
{
	int selectedItemID = m_pBrowseAllList->GetSelectedItem(0);
	if (selectedItemID == -1)
	{
		Msg("No map selected.\n");
		return;
	}

	KeyValues* kv = m_pBrowseAllList->GetItem(selectedItemID);
	const char* mapName = kv->GetString("MapName");

	char command[256];
	Q_snprintf(command, sizeof(command), "map %s\n", mapName);

	engine->ClientCmd(command);
}

void CMyPanel::OnCommand(const char* pcCommand)
{
	BaseClass::OnCommand(pcCommand);

	int activeItem = m_pGamemode->GetActiveItem();

	KeyValues* kv = m_pGamemode->GetItemUserData(activeItem);
	const char* mode = kv ? kv->GetString("mode", "") : "";

	if (FStrEq(pcCommand, "play_selected_map"))
	{
		//if (m_pGamemode == "Sandbox")
		if (!Q_stricmp(mode, "Sandbox"))
		{
			srcbox_gamemode_sandbox.SetValue(1);
			srcbox_gamemode_hl2.SetValue(0);
		}
		else if (!Q_stricmp(mode, "Half-Life 2"))
		{
			srcbox_gamemode_sandbox.SetValue(0);
			srcbox_gamemode_hl2.SetValue(1);
		};
		PlaySelectedMap();
		//IsTeamplay() == true;
	}
}

class CMyPanelInterface : public IMyPanel
{
private:
	CMyPanel* m_pPanel;

public:
	CMyPanelInterface()
		: m_pPanel(nullptr) {}

	void Create(vgui::VPANEL parent) override
	{
		if (!m_pPanel)
		{
			m_pPanel = new CMyPanel(parent);
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

static CMyPanelInterface g_MyPanel;
IMyPanel* mypanel = (IMyPanel*)&g_MyPanel;

void CMyPanel::OnTick()
{
	BaseClass::OnTick();
}

CON_COMMAND(srcbox_singleplayer, "Toggle the panel for Singleplayer")
{
	mypanel->Activate();
};

































