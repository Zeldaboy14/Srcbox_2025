//The following include files are necessary to allow your MyPanel.cpp to compile.
#include "cbase.h"
#include "MyPanel.h"
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

using namespace vgui;

#define GameUI_GameMenu_SinglePlayer "Play Singleplayer"

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
	ListPanel* m_pBrowseAllList;
	Button* m_pPlayButton;
	PanelListPanel* m_pBrowseAllPanel; // For "Browse All" tab
	PanelListPanel* m_pGameMapPanel;  // For "Game" tab to hold icons

	vgui::Button* m_pSelectedButton = nullptr;
	char m_SelectedMap[MAX_PATH];

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
	SetSizeable(false);
	SetMoveable(true);
	SetTitle("Play Singleplayer", false);

	SetScheme(vgui::scheme()->LoadSchemeFromFile("resource/SourceScheme.res", "SourceScheme"));

	LoadControlSettings("resource/UI/singleplayer_srcbox.res");

	// Create PropertySheet (Tab Panel)
	m_pTabPanel = new PropertySheet(this, "TabPanel");
	m_pTabPanel->SetBounds(10, 40, 456, 714); // Adjust to fit your window dimensions

	// Placeholder for "Game" Tab
	//Panel* pGamePanel = new Panel(m_pTabPanel, "GamePanel");
	//pGamePanel->SetPaintBackgroundEnabled(true);
	//pGamePanel->SetBgColor(Color(50, 50, 50, 255)); // Background color for the "Game" tab

	// Container for map icons in the Game tab
	//m_pGameMapPanel = new PanelListPanel(pGamePanel, "GameMapPanel");
	//m_pGameMapPanel->SetBounds(10, 10, 430, 700); // Adjust to fit your window dimensions
	//m_pGameMapPanel->SetFirstColumnWidth(200); // Layout adjustments

	// Add the Game tab to the PropertySheet
	//m_pTabPanel->AddPage(pGamePanel, "Game");

	// "Game" Tab
	//PopulateGameTab();
	//m_pTabPanel->AddPage(m_pGameMapPanel, "Game");
	// disabled for now

	// "Browse All" Tab
	m_pBrowseAllList = new ListPanel(m_pTabPanel, "BrowseAllList");
	m_pBrowseAllList->SetPaintBackgroundEnabled(true);
	m_pBrowseAllList->SetBgColor(Color(50, 50, 50, 255));

	// Define the columns for the table
	m_pBrowseAllList->AddColumnHeader(0, "MapName", "Map Name", 350, ListPanel::COLUMN_FIXEDSIZE);
	m_pBrowseAllList->AddColumnHeader(1, "Game", "Game", 100, ListPanel::COLUMN_FIXEDSIZE);

	m_pTabPanel->AddPage(m_pBrowseAllList, "Browse All");

	// Populate "Browse All" Tab
	PopulateBrowseAll();


	// Add the Play button
	m_pPlayButton = new Button(this, "PlayButton", "Play");
	m_pPlayButton->SetCommand("play_selected_map");
	m_pPlayButton->SetBounds(300, 760, 64, 24); // Adjust position and size
	m_pPlayButton->SetVisible(true);

	SetVisible(false);
}

void CMyPanel::PopulateBrowseAll()
{
	m_pBrowseAllList->RemoveAll(); // Clear any existing entries

	// Define prefix categories
	struct GameCategory
	{
		const char* gameName;
		const char* prefixes[30]; // Up to 10 prefixes per category (adjust as needed)
	};

	GameCategory categories[] = {
		{ "Gmod", { "gm_", nullptr } },
		{ "TF2", { "ctf_", "koth_", "cp_", "pl_", "mvm_", "pass_", "pd_", "sd_", "tc_", "vsh_", "zi_", "tr_", nullptr } },
		{ "CS:S", { "de_", "cs_", nullptr } },
		{ "HL2 Beta", { "d1_garage_", nullptr } },
		{ "HL:S", { "t0", nullptr } },
		{ "HL2", { "d1_trainstation", "d2_", "c17_", "ep1_", "ep1_", nullptr } },
		{ "HL2:DM", { "dm_", nullptr } },
		{ "L4D", { "l4d_", nullptr } },
		{ "L4D2", { "c1m1_hotel", "c1m2_streets", "c1m3_mall", "c2m1_highway", nullptr } },
		{ "Unknown Game", { nullptr } } // Default fallback
	};

	// Scan maps directory
	const char* mapsDir = "maps/*.bsp";
	FileFindHandle_t findHandle;
	const char* fileName = g_pFullFileSystem->FindFirst(mapsDir, &findHandle);

	while (fileName)
	{
		if (strstr(fileName, ".bsp"))
		{
			// Remove the ".bsp" extension
			char mapName[MAX_PATH];
			Q_strncpy(mapName, fileName, sizeof(mapName));
			mapName[strlen(mapName) - 4] = '\0';

			// Determine the game category based on prefixes
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
				//if (strcmp(detectedGame, "Unknown Game") != 0)
					//break; // Stop searching if we found a match
			}

			// Create a row for the ListPanel
			KeyValues* kv = new KeyValues("MapData");
			kv->SetString("MapName", mapName);
			kv->SetString("Game", detectedGame);

			m_pBrowseAllList->AddItem(kv, 0, false, false); // Add the map as a new row
			kv->deleteThis(); // Clean up
		}

		fileName = g_pFullFileSystem->FindNext(findHandle);
	}

	g_pFullFileSystem->FindClose(findHandle);
}

// Declare a member variable to store the selected map
char m_SelectedMap[MAX_PATH] = "";

void CMyPanel::PopulateGameTab()
{
    if (m_pGameMapPanel)
    {
        m_pGameMapPanel->DeleteAllItems();
    }

    const int itemWidth = 150;
    const int itemHeight = 40;
    const int columns = 3;
    const int spacing = 15;

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

            // Create a button for each map
           // Button* pMapButton = new Button(m_pGameMapPanel, mapName, mapName);
            //pMapButton->SetBounds(x, y, itemWidth, itemHeight);

			//Panel* pMapContainer = new Panel(m_pGameMapPanel, mapName);

			// Create a container panel for gm_construct
			//vgui::Panel* pMapContainer = new vgui::Panel(m_pGameMapPanel, "gm_construct");
			//pMapContainer->SetSize(200, 120);

			// Create an image panel for the map
			//vgui::ImagePanel* pImagePanel = new vgui::ImagePanel(pMapContainer, "MapThumbnail");
			//pImagePanel->SetBounds(5, 5, 190, 100);
			//pImagePanel->SetImage("maps/gm_construct"); // Set the correct image

			// Create a button overlay for selection
			//vgui::Button* pMapButton = new vgui::Button(pMapContainer, "MapButton", "gm_construct", this);
			//pMapButton->SetBounds(0, 0, 200, 120);
			//pMapButton->SetCommand(new KeyValues("SelectMap", "map", "gm_construct"));

            //m_pGameMapPanel->AddItem(nullptr, pMapButton);

            // Layout handling
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


// Start Game icon stuff

void CMyPanel::CreateGameIcon(const char* mapName, const char* imagePath, const char* command)
{
	// Create a Button that acts as the clickable area
	vgui::Button* pImageButton = new vgui::Button(m_pGameMapPanel, mapName, "");
	//pImageButton->SetSize(160, 90); // Set the size of the button
	pImageButton->SetCommand(command);
	pImageButton->AddActionSignalTarget(this);

	// Add an ImagePanel inside the button to display the image
	vgui::ImagePanel* pImage = new vgui::ImagePanel(pImageButton, "GameImage");
	pImage->SetImage(imagePath);
	pImage->SetShouldScaleImage(true);
	//pImage->SetSize(160, 90); // Set the image size same as button
}



void CMyPanel::PlaySelectedMap()
{
	// Get the selected item from the list
	int selectedItemID = m_pBrowseAllList->GetSelectedItem(0); // Get the first selected item
	if (selectedItemID == -1)
	{
		// No map selected
		Msg("No map selected.\n");
		return;
	}

	// Retrieve the map name from the selected item
	KeyValues* kv = m_pBrowseAllList->GetItem(selectedItemID);
	const char* mapName = kv->GetString("MapName");

	// Construct the map load command
	char command[256];
	Q_snprintf(command, sizeof(command), "map %s\n", mapName);

	// Execute the command to load the map
	engine->ClientCmd(command);
}

ConVar srcbox_map_menu("srcbox_map_menu", "0", FCVAR_CLIENTDLL, "Sets the state of myPanel <state>");

// Handle map selection
void CMyPanel::OnCommand(const char* pcCommand)
{
	BaseClass::OnCommand(pcCommand);

	if (FStrEq(pcCommand, "play_selected_map"))
	{
		PlaySelectedMap(); // Handle the Play button command
	}
	else if (Q_strnicmp(pcCommand, "SelectMap", 9) == 0) // Check if it's a SelectMap command
	{
		const char* selectedMap = pcCommand + 10; // Extract the map name from the command
		if (selectedMap && selectedMap[0])
		{
			Q_strncpy(m_SelectedMap, selectedMap, sizeof(m_SelectedMap));
			Msg("Selected Map: %s\n", m_SelectedMap);
		}
	}
	else if (FStrEq(pcCommand, "StartGame"))
	{
		if (m_SelectedMap[0])
		{
			char commandString[128];
			Q_snprintf(commandString, sizeof(commandString), "map %s", m_SelectedMap);
			engine->ClientCmd(commandString);  // Load the selected map
		}
	}
	else if (FStrEq(pcCommand, "turnoff"))
	{
		srcbox_map_menu.SetValue(0);
	}
}

// Class for managing panel instance
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
	SetVisible(srcbox_map_menu.GetBool());
}

CON_COMMAND(srcbox_singleplayer, "Toggle the panel for Singleplayer")
{
	srcbox_map_menu.SetValue(!srcbox_map_menu.GetBool());
	mypanel->Activate();
};

































