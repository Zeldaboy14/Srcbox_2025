"Resource/UI/MainMenu.res"
{
	"MainMenu"
	{
		"ControlName"			"Frame"
		"fieldName"				"MainMenu"
		"xpos"					"0"
		"ypos"					"0"
		"zpos"					"-100"
		"wide"					"f0"
		"tall"					"f0"
		"autoResize"			"0"
		"pinCorner"				"0"
		"visible"				"1"
		"enabled"				"1"
		"tabPosition"			"0"
		"setclosebuttonvisible" "0"
		"PaintBackgroundType"	"0"
		
		"paintbackground"	"0"
		"alpha"				"255"

		"navUp"			"QuitButton"
		"navDown"		"ServerBrowserButton"

		"if_inlevel"
		{
			"navDown"	"ResumeButton"
		}
	}
	
	"LogoText"
	{
		//"ControlName"	"Label"
		"fieldName"		"LogoText"
		"xpos"			"81"
		"ypos"			"175"
		"wide"			"900"
		"tall"			"60"
		"visible"		"0"
		"proportionalToParent"	"1"
		"textAlignment"		"north-east"
		"labelText"			"HALF-LIFE'"
		"font"				"TitleFont"
		"weight"			"0"
	}
	
	"Logo"
	{
		"ControlName"	"ImagePanel"
		"fieldName"		"Logo"
		"xpos"			"74"
		"ypos"			"150"
		"zpos"			"1"
		"wide"			"256"
		"tall"			"65"
		"visible"		"1"
		"enabled"		"1"
		"image"			"resource/srcbox_logo.vmt"
		"scaleImage"	"1"
		"ProportionalToParent"	"1"
	}
	
	"BtnSingleplayer"
	{
		"ControlName"			"BaseModHybridButton"
		"fieldName"				"BtnSingleplayer"
		"xpos"					"86"
		"ypos"					"220"
		"wide"					"210"
		"tall"					"30"
		"autoResize"			"1"
		"pinCorner"				"0"
		"visible"				"1"
		"enabled"				"1"
		"tabPosition"			"0"
		"navUp"					"PnlQuickJoin"
		"navDown"				"BtnMultiplayer"
		"labelText"				"#L4D360UI_MainMenu_PlaySolo"
		"tooltiptext"			"#L4D360UI_MainMenu_PlaySolo_Tip"
		"style"					"AlienSwarmMenuButtonSmall"
		"command"				"Srcbox_Singleplayer_Menu"	
		"ActivationType"		"0"
	}
	
	"BtnNewGame"
	{
		"ControlName"			"BaseModHybridButton"
		"fieldName"				"BtnNewGame"
		"xpos"					"86"
		"ypos"					"240"
		"wide"					"180"
		"tall"					"30"
		"pinCorner"				"0"
		"visible"				"1"
		"enabled"				"1"
		"tabPosition"			"0"
		"navUp"					"BtnQuit"
		"navDown"				"BtnLoadGame"
		"labelText"				"#GameUI_GameMenu_NewGame"
		"style"					"AlienSwarmMenuButton"
		"command"				"CreateGame"	
		//"command"				"Srcbox_Singleplayer_Menu"	
		"ActivationType"		"1"
	}
	
	"BtnLoadGame"
	{
		"ControlName"			"BaseModHybridButton"
		"fieldName"				"BtnLoadGame"
		"xpos"					"86"
		"ypos"					"260"
		"wide"					"180"
		"tall"					"17"
		"autoResize"			"1"
		"pinCorner"				"0"
		"visible"				"1"
		"enabled"				"1"
		"tabPosition"			"0"
		"navUp"					"BtnNewGame"
		"navDown"				"BtnStatsAndAchievements"
		"labelText"				"#GameUI_GameMenu_LoadGame"
		"style"					"AlienSwarmMenuButton"
		"command"				"LoadGameDialog"	
		"ActivationType"		"1"
	}
		
	"BtnStatsAndAchievements"
	{
		"ControlName"			"BaseModHybridButton"
		"fieldName"				"BtnStatsAndAchievements"
		"xpos"					"86"
		"ypos"					"280"
		"wide"					"180"
		"tall"					"13"
		"autoResize"			"1"
		"pinCorner"				"0"
		"visible"				"0"		[$X360GUEST]
		"enabled"				"0"		[$X360GUEST]
		"visible"				"1"		[!$X360GUEST]		// jms: TEMP disabled
		"enabled"				"1"		[!$X360GUEST]
		"tabPosition"			"0"
		"navUp"					"BtnLoadGame"
		"navDown"				"BtnController"
		"labelText"				"#L4D360UI_MainMenu_StatsAndAchievements"
		"tooltiptext"			"#L4D360UI_MainMenu_StatsAndAchievements_Tip"	[$X360]
		"tooltiptext"			"#L4D360UI_MainMenu_PCStatsAndAchievements_Tip"	[$WIN32]
		"style"					"AlienSwarmMenuButtonSmall"
		"command"				"StatsAndAchievements"
		"ActivationType"		"1"
		"EnableCondition"		"Never" [$DEMO]
	}
	
	"BtnController"
	{
		"ControlName"			"BaseModHybridButton"
		"fieldName"				"BtnController"
		"xpos"					"86"
		"ypos"					"300"
		"wide"					"180"
		"tall"					"13"
		"autoResize"			"1"
		"pinCorner"				"0"
		"visible"				"0"		[$X360GUEST]
		"enabled"				"0"		[$X360GUEST]
		"visible"				"1"		[!$X360GUEST]		// jms: TEMP disabled
		"enabled"				"1"		[!$X360GUEST]
		"tabPosition"			"0"
		"navUp"					"BtnStatsAndAchievements"
		"navDown"				"BtnOptions"
		"labelText"				"#GameUI_GameMenu_Controller"
		"style"					"AlienSwarmMenuButtonSmall"
		"command"				"ControllerDialog"
		"ActivationType"		"1"
		"EnableCondition"		"Never" [$DEMO]
	}
	
	"BtnOptions"
	{
		"ControlName"			"BaseModHybridButton"
		"fieldName"				"BtnOptions"
		"xpos"					"86"
		"ypos"					"320"
		"wide"					"180"
		"tall"					"13"
		"autoResize"			"1"
		"pinCorner"				"0"
		"visible"				"1"
		"enabled"				"1"
		"tabPosition"			"0"
		"navUp"					"BtnStatsAndAchievements"
		"navDown"				"BtnQuit"
		"labelText"				"#L4D360UI_MainMenu_Options"
		"tooltiptext"			"#L4D360UI_MainMenu_Options_Tip"
		"style"					"AlienSwarmMenuButtonSmall"
		"command"				"OpenLegacyOptions"			[!$X360GUEST]	
		"ActivationType"		"1"
	}

	"BtnQuit"
	{
		"ControlName"			"BaseModHybridButton"
		"fieldName"				"BtnQuit"
		"xpos"					"86"
		"ypos"					"340"
		"wide"					"180"
		"tall"					"13"
		"autoResize"			"1"
		"pinCorner"				"0"
		"visible"				"1"
		"enabled"				"1"
		"tabPosition"			"0"
		"navUp"					"BtnExtras"
		"navDown"				"BtnNewGame"
		"style"					"AlienSwarmMenuButtonSmall"
		"ActivationType"		"1"
		"labelText"				"#L4D360UI_MainMenu_Quit"			[$WIN32]
		"command"				"Quit"
	}
}
