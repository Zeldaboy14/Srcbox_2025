AddCSLuaFile("cl_test_vgui.lua")
AddCSLuaFile("cl_init.lua")
include("shared.lua")

DEFINE_BASECLASS( "gamemode_base" )

-- GMod-style gamemode: it uses its own hands (c_arms), not the default hands view model
function GM:PlayerHandsModel(ply)
	return ""
end

include("mysqloo_tests/autorun/server/init.lua")
