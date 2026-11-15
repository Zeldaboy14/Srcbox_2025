--[[
	gmod_gamesetup (GMod 9 map entity)

	"GamemodeScript" picks the GMod 9 gamemode script the map runs, relative to
	lua/gamemodes/ (e.g. "melonracer/init.lua"). The gmod9 gamemode runs it.
--]]

ENT.Base = "prop_scripted"

function ENT:KeyValue(key, value)
	if (string.lower(key) == "gamemodescript") then
		self.GamemodeScript = value
	end
end

function ENT:Initialize()
	if (CLIENT) then
		return
	end

	self:AddEffects(32)	-- EF_NODRAW

	if (GM9 and GM9.RunGamemodeScript and self.GamemodeScript and self.GamemodeScript ~= "") then
		GM9.RunGamemodeScript(self.GamemodeScript)
	end
end
