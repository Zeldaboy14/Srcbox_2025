--[[
	gmod_player_start (GMod 9 map entity)

	A spawn point; the gmod9 gamemode picks from these (GM:PlayerEntSelectSpawnPoint).
	The team spawnflags (1 Blue, 2 Yellow, 4 Green, 8 Red) aren't used yet.
--]]

ENT.Base = "prop_scripted"

function ENT:Initialize()
	if (CLIENT) then
		return
	end

	self:AddEffects(32)	-- EF_NODRAW

	if (GM9) then
		GM9.Spawns = GM9.Spawns or {}
		table.insert(GM9.Spawns, self)
	end
end
