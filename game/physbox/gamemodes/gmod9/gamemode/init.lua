--[[
	GMod 9 compatibility gamemode (server)

	Turns our gamemode hooks into GMod 9's events (eventPlayerSpawn,
	gamerulesThink, ...) for the running GMod 9 script. The API and loader
	live in sv_gm9.lua.
--]]

Include("shared.lua")

GM9 = GM9 or {}
GM9.Folder = GM.Folder
Include("sv_gm9.lua")

Networks.AddNetworkString("gm9_hud")

function GM:Initialize()
	-- Map entities can get here first (gmod_gamesetup runs the script while the map spawns)
	GM9.EnsureEnvironment()

	if (self.BaseClass and self.BaseClass.Initialize) then
		self.BaseClass.Initialize(self)
	end
end

function GM:Think()
	GM9.Think()
end

-- gmod_player_start entities register themselves in GM9.Spawns
function GM:PlayerEntSelectSpawnPoint(client)
	local spawns = {}
	for _, spawn in ipairs(GM9.Spawns or {}) do
		if (spawn ~= NULL and spawn:IsValid()) then
			table.insert(spawns, spawn)
		end
	end

	if (#spawns > 0) then
		return spawns[math.random(#spawns)]
	end
end

function GM:PlayerInitialSpawn(client)
	local i = client:GetEntityIndex()

	-- GMod 9 asked the script which team a new player starts on
	GM9.Call("PickDefaultSpawnTeam", i)
	GM9.Call("eventPlayerInitialSpawn", i)

	-- "Active" means fully in game; give the client a moment so it receives HUD messages
	GM9.Defer(1, function()
		local p = GM9.Ply(i)
		if (p) then
			GM9.Call("eventPlayerActive", p:GetPlayerName(), i, p:GetSteamId())
		end
	end)
end

function GM:PlayerSpawn(client)
	local i = client:GetEntityIndex()

	GM9.Spectate[i] = nil
	GM9.Call("PlayerSpawnChooseModel", i)
	GM9.Call("eventPlayerSpawn", i)
end

function GM:GiveDefaultItems(client)
	if (GM9.HasFunction("GiveDefaultItems")) then
		GM9.Call("GiveDefaultItems", client:GetEntityIndex())
		return
	end

	if (self.BaseClass and self.BaseClass.GiveDefaultItems) then
		self.BaseClass.GiveDefaultItems(self, client)
	end
end

function GM:PlayerKilled(victim, info)
	local attacker = info and info:GetAttacker()
	local attackerIndex = GM9.Idx(attacker)

	local weapon = ""
	local inflictor = info and info:GetInflictor()
	if (inflictor ~= nil and inflictor ~= NULL and inflictor:IsValid()) then
		weapon = inflictor:GetClass()
	end

	GM9.Call("eventPlayerKilled", victim:GetEntityIndex(), attackerIndex, weapon)
end

function GM:ClientDisconnected(client)
	local i = client:GetEntityIndex()

	GM9.Call("eventPlayerDisconnect", client:GetPlayerName(), i, "", client:GetSteamId(), "Disconnect")

	GM9.Spectate[i] = nil
	GM9.ButtonState[i] = nil
end
