"GameMenu" [$WIN32]
{
	"1"
	{
		"label" "#GameUI_GameMenu_ResumeGame"
		"command" "ResumeGame"
		"OnlyInGame" "1"
	}
	"2"
	{
		"label" "#GameUI_GameMenu_Disconnect"
		"command" "Disconnect"
		"OnlyInGame" "1"
	} 
	
	"3"
	{
		"label" "#GameUI_GameMenu_SinglePlayer"
		"command" "engine srcbox_singleplayer"
		"notmulti" "1"
	}
	
	"4"
	{
		"label" "------------------------"
		"OnlyInGame" "1"
	}
	"5"
	{
		"label" "Find Multiplayer Servers"
		"command" "OpenServerBrowser"
	}
	"6"
	{
		"label" "#GameUI_GameMenu_LoadGame"
		"command" "OpenLoadGameDialog"
	}
	
	"7"
	{
		"label" "------------------------"
	}

	"8"
	{
		"label" "#GameUI_GameMenu_GameMount"
		"command" "engine srcbox_mount_menu"
	}
	
	"8"
	{
		"label" "#GameUI_GameMenu_AdvancedOptions"
		"command" "engine srcbox_advancedoptions"
	}
	
	"12"
	{
		"label" "#GameUI_GameMenu_Options"
		"command" "OpenOptionsDialog"
	}
	
	"13"
	{
		"label" "------------------------"
	}
	
	"14"
	{
		"label" "#GameUI_GameMenu_Quit"
		"command" "Quit"
	}
}
