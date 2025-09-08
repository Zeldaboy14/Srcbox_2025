//The following include files are necessary to allow your MyPanel.cpp to compile.
#include "cbase.h"
#include "MountGames.h"
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

//IGameMountPanel* gamemountpanel;

extern IFileSystem* filesystem;

using namespace vgui;

class CGameMountPanel : public vgui::Frame
{
    DECLARE_CLASS_SIMPLE(CGameMountPanel, vgui::Frame);

public:
    CGameMountPanel(vgui::VPANEL parent);
    ~CGameMountPanel() {}

protected:
    virtual void OnTick();
    virtual void OnCommand(const char* pcCommand) override;

private:
    PropertySheet* m_pTabPanel;
    ListPanel* m_pBrowseAllList;
    Button* m_pMountButton;
    vgui::ListPanel* m_pGameList; // List of games to mount
};

CGameMountPanel::CGameMountPanel(vgui::VPANEL parent)
    : BaseClass(nullptr, "GameMountPanel")
{
    SetParent(parent);

    SetKeyBoardInputEnabled(true);
    SetMouseInputEnabled(true);

    SetProportional(false);
    SetTitleBarVisible(true);
    SetSizeable(false);
    SetMoveable(true);

    SetScheme(vgui::scheme()->LoadSchemeFromFile("resource/SourceScheme.res", "SourceScheme"));

    LoadControlSettings("resource/UI/mountgames_srcbox.res");

    // Create the game list
    m_pGameList = new vgui::ListPanel(this, "GameList");
    m_pGameList->SetBounds(20, 40, 360, 180);
    //m_pGameList->AddColumnHeader(0, "Game", "Gam2", 100, 100, 100);
    //m_pGameList->AddItem(new KeyValues("Game", "name", "Counter-Strike: Source"));
    //m_pGameList->AddItem(new KeyValues("Game", "name", "Half-Life 2: Episode 2"));
    KeyValues* kvCSS = new KeyValues("Game");
    kvCSS->SetString("name", "Counter-Strike: Source");
    m_pGameList->AddItem(kvCSS, 0, false, true);

    KeyValues* kvEP2 = new KeyValues("Game");
    kvEP2->SetString("name", "Half-Life 2: Episode 2");
    m_pGameList->AddItem(kvEP2, 0, false, true);

    // Create the mount button
    m_pMountButton = new vgui::Button(this, "MountButton", "Mount Game", this, "MountGame");
    m_pMountButton->SetBounds(150, 240, 100, 30);

    DevMsg("MountGames has been constructed\n");
    SetVisible(false);
}

ConVar srcbox_mount("srcbox_map_menu", "0", FCVAR_CLIENTDLL, "Sets the state of myPanel <state>");

void CGameMountPanel::OnCommand(const char* command)
{
    if (!Q_stricmp(command, "MountGame"))
    {
        int selectedItemID = m_pGameList->GetSelectedItem(0);
        if (selectedItemID != -1)
        {
            KeyValues* kv = m_pGameList->GetItem(selectedItemID);
            const char* gameName = kv->GetString("name");

            if (!Q_stricmp(gameName, "Counter-Strike: Source"))
            {
                filesystem->AddSearchPath("G:\Steam\steamapps\common\Counter-Strike Source", "GAME");
            }
            else if (!Q_stricmp(gameName, "Half-Life 2: Episode 2"))
            {
                filesystem->AddSearchPath("G:\Steam\steamapps\common\Half-Life 2\ep2", "GAME");
            }
            else if (FStrEq(command, "turnoff"))
            {
                srcbox_mount.SetValue(0);
            }
        }
    }
    else
    {
        BaseClass::OnCommand(command);
    }
}

// Class for managing panel instance
class CGameMountPanelInterface : public IGameMountPanel
{
private:
    CGameMountPanel* m_pPanel;

public:
    CGameMountPanelInterface()
        : m_pPanel(nullptr) {}

    void Create(vgui::VPANEL parent)
    {
        if (!m_pPanel)
        {
            m_pPanel = new CGameMountPanel(parent);
        }
    }

    void Destroy()
    {
        if (m_pPanel)
        {
            m_pPanel->SetParent(nullptr);
            delete m_pPanel;
            m_pPanel = nullptr;
        }
    }

    void Activate()
    {
        if (m_pPanel)
        {
            m_pPanel->Activate();
        }
    }
 };

static CGameMountPanelInterface g_GameMountPanel;
IGameMountPanel* gamemountpanel = (IGameMountPanel*)&g_GameMountPanel;

void CGameMountPanel::OnTick()
{
    BaseClass::OnTick();
    SetVisible(srcbox_mount.GetBool());  // Toggle visibility based on ConVar
}

CON_COMMAND(srcbox_mount_menu, "Panel for mounting games")
{
    srcbox_mount.SetValue(!srcbox_mount.GetBool());
    gamemountpanel->Activate();
};