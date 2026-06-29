//The following include files are necessary to allow your MyPanel.cpp to compile.
#include "cbase.h"
#include "vgui_friends.h"
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
#include "vgui_avatarimage.h"
#include "ienginevgui.h"
//#include "../tf/clientmode_tf.h"
//#include "../tf/tf_partyclient.h"
//#include "../tf/tf_party.h"

#include <stdlib.h>

using namespace vgui;

void LoadTrackerNames()
{
	FileHandle_t f = filesystem->Open("resource/platform/Friends/trackerui_english.txt", "r", "MOD");
	if (!f)
	{
		Msg("Could not open trackerui_english.txt\n");
		return;
	}

	int fileSize = filesystem->Size(f);
	if (fileSize <= 0)
	{
		filesystem->Close(f);
		return;
	}

	char* buffer = new char[fileSize + 1];
	filesystem->Read(buffer, fileSize, f);
	buffer[fileSize] = '\0'; // null-terminate
	filesystem->Close(f);

	Msg("trackerui file loaded:\n%s\n", buffer);

	// TODO: parse buffer line by line
	// Each line could be a tracker name
	char* line = strtok(buffer, "\n");
	while (line)
	{
		Msg("Tracker: %s\n", line);
		// Store in vector, hash, etc.
		line = strtok(nullptr, "\n");
	}

	delete[] buffer;
}

bool BSteamIDIsPlayingOnline(const CSteamID& steamID)
{
	if (!steamapicontext || !steamapicontext->SteamFriends())
		return false;

	FriendGameInfo_t gameInfo;
	if (steamapicontext->SteamFriends()->GetFriendGamePlayed(steamID, &gameInfo))
	{
		if (gameInfo.m_gameID.AppID() == (uint32)engine->GetAppID())
		{
			return true;
		}
	}

	return false;
}

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

class CSteamChat : public vgui::Frame
{
	DECLARE_CLASS_SIMPLE(CSteamChat, vgui::Frame);

public:
	CSteamChat(vgui::VPANEL parent);
	~CSteamChat() {}
	const CSteamID& GetFriendSteamID() const { return m_steamID; }
	//MESSAGE_FUNC(DoSendMessage, "Context_SendMessage");

protected:
	virtual void OnTick();
	virtual void OnCommand(const char* pcCommand) override;

private:
	void UpdateControls();
	CSteamID m_steamID;
	CAvatarImagePanel* m_pAvatar;
	vgui::Label* m_pStatusLabel;
	Button* m_pInteractButton;
};



CSteamChat::CSteamChat(vgui::VPANEL parent)
	: BaseClass(nullptr, "Frame")
{
	SetParent(parent);

	SetKeyBoardInputEnabled(true);
	SetMouseInputEnabled(true);

	SetProportional(false);
	SetTitleBarVisible(true);
	SetSizeable(false);
	SetMoveable(true);
	//reminder! we need to set this dynamically!!!!

	SetTitle("#TrackerUI_Friends_OfflineTitle", false);

	SetScheme(vgui::scheme()->LoadSchemeFromFile("resource/SourceScheme.res", "SourceScheme"));

	LoadControlSettings("resource/platform/Friends/TrackerDialog.res");

	m_pAvatar = new CAvatarImagePanel(this, "avatar");
	m_pAvatar->SetShouldScaleImage(true);
	m_pAvatar->SetShouldDrawFriendIcon(false);

	m_pStatusLabel = new Label(this, "StatusLabel", (const char*)NULL);
	m_pStatusLabel->AddActionSignalTarget(this);

	m_pInteractButton = new Button(this, "InteractButton", (const char*)NULL);
	m_pInteractButton->SetMouseClickEnabled(MOUSE_RIGHT, true);

	SetVisible(false);
}

void CSteamChat::OnCommand(const char* pcCommand)
{
}

void CSteamChat::UpdateControls()
{
/*	const Color& colorInTF2 = vgui::scheme()->GetIScheme(GetScheme())->GetColor("CreditsGreen", Color(255, 255, 255, 255));
	const Color& colorOnline = vgui::scheme()->GetIScheme(GetScheme())->GetColor("ProgressBarBlue", Color(255, 255, 255, 255));
	const Color& colorOther = vgui::scheme()->GetIScheme(GetScheme())->GetColor("QuestMap_InactiveGrey", Color(255, 255, 255, 255));

	CUtlString strName = "...";
	wchar_t wzRichPresenceBuf[256] = { 0 };
	wchar_t* pwzStatus = nullptr;


	// Setup controls
	if (m_steamID.IsValid() && steamapicontext && steamapicontext->SteamFriends())
	{
		ISteamFriends* pSteamFriends = steamapicontext->SteamFriends();
		strName = pSteamFriends->GetFriendPersonaName(m_steamID);

		// Playing TF2 is sorted to the top
		FriendGameInfo_t gameInfo;
		if (pSteamFriends->GetFriendGamePlayed(m_steamID, &gameInfo))
		{
			// If the friend is playing TF2, say so.  Other games we'll show as "Playing other game"
			if (gameInfo.m_gameID.AppID() == (uint32)engine->GetAppID())
			{
				const char* pszRichState = pSteamFriends->GetFriendRichPresence(m_steamID, "state");
				const char* pszRichMatchGroupLoc = pSteamFriends->GetFriendRichPresence(m_steamID, "matchgrouploc");
				const char* pszRichMap = pSteamFriends->GetFriendRichPresence(m_steamID, "currentmap");
				// If they have at least state, see if we can build a status from the keys
				if (pszRichState && pszRichState[0] &&
					GetClientModeTFNormal()->BuildRichPresenceStatus(wzRichPresenceBuf, pszRichState,
						pszRichMatchGroupLoc, pszRichMap))
				{
					pwzStatus = wzRichPresenceBuf;
				}
				else
				{
					// Show generic
					pwzStatus = g_pVGuiLocalize->Find("#TF_Friends_PlayingTF2");
				}
				m_pStatusLabel->SetFgColor(colorInTF2);
				m_pInteractButton->SetVisible(true);
			}
			else
			{
				pwzStatus = g_pVGuiLocalize->Find("#TF_Friends_PlayingOther");
				m_pStatusLabel->SetFgColor(colorOnline);
				m_pInteractButton->SetVisible(true);
			}
		}
		else
		{
			EPersonaState eState = pSteamFriends->GetFriendPersonaState(m_steamID);

			// Set label text
			switch (eState)
			{
			case k_EPersonaStateOffline: pwzStatus = g_pVGuiLocalize->Find("#TF_Friends_Offline"); break;
			case k_EPersonaStateOnline: pwzStatus = g_pVGuiLocalize->Find("#TF_Friends_Online"); break;
			case k_EPersonaStateBusy: pwzStatus = g_pVGuiLocalize->Find("#TF_Friends_Busy"); break;
			case k_EPersonaStateAway: pwzStatus = g_pVGuiLocalize->Find("#TF_Friends_Away"); break;
			case k_EPersonaStateSnooze: pwzStatus = g_pVGuiLocalize->Find("#TF_Friends_Snooze"); break;
			}

			// Extra control settings
			switch (eState)
			{
			case k_EPersonaStateOffline:
				m_pStatusLabel->SetFgColor(colorOther);
				break;
			default:
				m_pStatusLabel->SetFgColor(colorOnline);
			}

			m_pInteractButton->SetVisible(eState != k_EPersonaStateOffline);
		}
	}

	SetDialogVariable("name", strName);
	SetDialogVariable("status", pwzStatus);*/
}

// Class for managing panel instance
class CSteamChatInterface : public ISteamChat
{
private:
	CSteamChat* m_pPanel;

public:
	CSteamChatInterface()
		: m_pPanel(nullptr) {}

	void Create(vgui::VPANEL parent) override
	{
		if (!m_pPanel)
		{
			m_pPanel = new CSteamChat(parent);
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

static CSteamChatInterface g_SteamChat;
ISteamChat* steamchat = (ISteamChat*)&g_SteamChat;

void CSteamChat::OnTick()
{
	BaseClass::OnTick();
}

CON_COMMAND(OpenFriendsDialog, "Toggle the panel for Singleplayer")
{
	steamchat->Activate();
};

































