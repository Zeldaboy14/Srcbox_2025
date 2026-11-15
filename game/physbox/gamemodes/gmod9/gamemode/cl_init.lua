--[[
	GMod 9 compatibility gamemode (client)

	GMod 9 ran no Lua on the client; everything it showed came from the server
	as _GModText / _GModRect elements, drawn here.
--]]

Include("shared.lua")
Include("cl_gm9_hud.lua")

function GM:HudDraw()
	GM9HUD.Draw()

	if (self.BaseClass and self.BaseClass.HudDraw) then
		self.BaseClass.HudDraw(self)
	end
end

-- While observing, the engine turns attack / attack2 / jump into spectator commands
-- (spec_next, spec_prev, spec_mode; ClientModeShared::HandleSpectatorKeyInput) and never
-- sends them to the server. GMod 9 gamemodes use them while spectating (melonracer: attack
-- to join, attack2 for the camera), so let them through as normal buttons.
local spectatorKeys = { ["+attack"] = true, ["+attack2"] = true, ["+jump"] = true }

function GM:KeyInput(down, keyNumber, currentBinding)
	if (currentBinding and spectatorKeys[currentBinding]) then
		local localPlayer = Players.GetLocalPlayer()
		if (localPlayer ~= nil and localPlayer ~= NULL and localPlayer:IsValid() and localPlayer:GetObserverMode() > 1) then
			return true	-- handle it as a normal key; skips the spectator handling
		end
	end

	if (self.BaseClass and self.BaseClass.KeyInput) then
		return self.BaseClass.KeyInput(self, down, keyNumber, currentBinding)
	end
end
