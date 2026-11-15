--[[
	GMod 9 compatibility layer (server)

	Runs Garry's Mod 9 Lua gamemodes (2005, Lua 5.0, the "_Function" API) on
	this engine's Lua. GMod 9 scripts run in their own environment table so the
	globals they define (Msg, Timer, IsPlayer, ...) can't clobber ours; anything
	they don't define falls through to our globals.

	Layout (inside this gamemode):
		gm9lua/includes/*.lua		GMod 9's own includes (defines, timers, ...)
		gm9lua/gamemodes/<name>/	GMod 9 gamemode scripts
	A map's gmod_gamesetup entity picks the script ("melonracer/init.lua"),
	exactly like GMod 9 did.

	GMod 9 identifies players and entities by entity index (0 = none); this
	layer converts between indices and our entity objects.
--]]

GM9 = GM9 or {}
local GM9 = GM9

GM9.Folder = GM9.Folder or (GM and GM.Folder) or "gamemodes\\gmod9\\"

local FL_FROZEN = 64	-- HL2MP's const.h: FL_FROZEN is (1<<6)

-- GMod 9 (2005 engine) observer modes -> this engine's OBS_MODE_* (shareddefs.h:
-- NONE, DEATHCAM, FREEZECAM, FIXED, IN_EYE, CHASE, POI, ROAMING)
local OBS_FROM_GM9 = { [0] = 0, [1] = 1, [2] = 3, [3] = 4, [4] = 5, [5] = 7 }

-- Console + data/gm9_log.txt (the console isn't always easy to get at), cleared each map
local logStarted = false
function GM9.Log(message)
	message = "[GMod9] " .. tostring(message)
	print(message)
	if (LuaLogToFile) then
		pcall(LuaLogToFile, "%s", message)
	end

	local ok = pcall(function()
		local handle = Files.Open("gm9_log.txt", logStarted and "a" or "w", "DATA")
		Files.Write(string.format("%.2f %s\n", Engines.GetCurrentTime(), message), handle)
		Files.Close(handle)
	end)
	logStarted = logStarted or ok
end

-- Missing-API warnings are logged once per name
local warned = {}
local function WarnOnce(key, message)
	if (warned[key]) then
		return
	end
	warned[key] = true
	GM9.Log(message)
end

--[[
	Index <-> object helpers
--]]

local function IsValidObject(e)
	return e ~= nil and e ~= NULL and e:IsValid()
end

local function MaxPlayers()
	return Engines.GetMaxClients()
end

local function Ply(i)
	if (type(i) ~= "number" or i < 1 or i > MaxPlayers()) then
		return nil
	end

	local p = Players.FindByIndex(i)
	if (IsValidObject(p)) then
		return p
	end
end

local function Ent(i)
	if (type(i) ~= "number" or i <= 0) then
		return nil
	end

	local e = Entities.FindByIndex(i)
	if (IsValidObject(e)) then
		return e
	end
end

local function Idx(e)
	if (IsValidObject(e)) then
		return e:GetEntityIndex()
	end
	return 0
end

GM9.Ply = Ply
GM9.Ent = Ent
GM9.Idx = Idx

-- GMod 9 vectors are plain {x, y, z} tables (vector3)
local function ToVector(t)
	if (type(t) ~= "table") then
		return t
	end
	return Vectors.Create(t.x or 0, t.y or 0, t.z or 0)	-- there is no global Vector() here
end

local function ToVector3(v)
	if (v == nil) then
		return nil
	end
	return { x = v.x, y = v.y, z = v.z }
end

-- 5.0 printed integral numbers without ".0"; keep values we hand back integral
local function Int(n)
	if (type(n) == "number") then
		return math.tointeger(n) or n
	end
	return n
end

--[[
	Lua 5.0 -> 5.4 source fixes, applied when a GMod 9 script is loaded
--]]

local function Lua50To54(source)
	-- "for k, v in tbl do" iterated a table directly in 5.0; 5.4 needs pairs()
	source = source:gsub("(for%s+[%w_]+%s*,?%s*[%w_]*%s+in%s+)([^\n]-)(%s+do[%s;])", function(head, expr, tail)
		local trimmed = expr:match("^%s*(.-)%s*$")
		if (trimmed:find("%(")) then
			return head .. expr .. tail	-- already a call: pairs(t), string.gfind(...)
		end
		return head .. "pairs(" .. trimmed .. ")" .. tail
	end)

	-- Vararg functions got an implicit "arg" table in 5.0
	source = source:gsub("(function%s*[%w_%.:]*%s*%(([^%)]-)%.%.%.%s*%))", "%1 local arg = table.pack(...);")

	return source
end

--[[
	The GMod 9 environment
--]]

local env

local function BuildShims(e)
	local tableShim = setmetatable({
		getn = function(t)
			return t.n or #t
		end,
		setn = function(t, n)
			t.n = n
		end,
		foreach = function(t, f)
			for k, v in pairs(t) do
				local r = f(k, v)
				if (r ~= nil) then
					return r
				end
			end
		end,
		foreachi = function(t, f)
			for i = 1, (t.n or #t) do
				local r = f(i, t[i])
				if (r ~= nil) then
					return r
				end
			end
		end,
	}, { __index = table })

	e.table = tableShim
	e.math = setmetatable({
		mod = math.fmod,
		pow = function(a, b)
			return a ^ b
		end,
		log10 = function(x)
			return math.log(x, 10)
		end,
	}, { __index = math })
	e.string = setmetatable({ gfind = string.gmatch }, { __index = string })

	e.unpack = function(t, i, j)
		return table.unpack(t, i or 1, j or t.n or #t)
	end
	e.getn = tableShim.getn
	e.foreach = tableShim.foreach
	e.foreachi = tableShim.foreachi
	e.loadstring = function(source, name)
		return load(Lua50To54(source), name, "t", env)
	end
	e.dofile = function(path)
		return GM9.OpenScript(path)
	end
	e.tostring = function(v)
		if (math.type(v) == "float" and v == math.floor(v) and math.abs(v) < 2 ^ 53) then
			return string.format("%d", v)
		end
		return tostring(v)
	end

	e.vector3 = function(x, y, z)
		return { x = x or 0, y = y or 0, z = z or 0 }
	end
end

local function ReadScript(relative)
	relative = relative:gsub("\\", "/")

	local candidates = {
		GM9.Folder .. "gm9lua/" .. relative,
		"gamemodes/gmod9/gm9lua/" .. relative,
	}

	for _, path in ipairs(candidates) do
		if (Files.FileExists(path, "GAME")) then
			local size = Files.Size(path, "GAME")
			local handle = Files.Open(path, "rb", "GAME")
			local _, data = Files.Read(size, handle)
			Files.Close(handle)
			if (data) then
				return data, path
			end
		end
	end
end

function GM9.OpenScript(relative)
	local source, path = ReadScript(relative)
	if (not source) then
		GM9.Log("_OpenScript: can't find " .. relative)
		return false
	end

	local chunk, err = load(Lua50To54(source), "@gm9/" .. relative, "t", env)
	if (not chunk) then
		GM9.Log("Error compiling " .. relative .. ": " .. tostring(err))
		return false
	end

	local ok, runErr = pcall(chunk)
	if (not ok) then
		GM9.Log("Error running " .. relative .. ": " .. tostring(runErr))
		return false
	end

	return true
end

-- Calls a GMod 9 global function (event) if the script defined it
function GM9.Call(name, ...)
	if (not env) then
		return nil
	end

	local f = rawget(env, name)
	if (type(f) ~= "function") then
		return nil
	end

	local ok, a, b = pcall(f, ...)
	if (not ok) then
		GM9.Log("Error in " .. name .. ": " .. tostring(a))
		return nil
	end
	return a, b
end

function GM9.HasFunction(name)
	return env ~= nil and type(rawget(env, name)) == "function"
end

--[[
	Server state the API keeps
--]]

GM9.Spectate = {}		-- [playerIndex] = { mode = OBS_MODE_*, target = entity }
GM9.Tracked = {}		-- [entityIndex] = entity, physics props _EntCreate made (for eventPropBreak)
GM9.Removing = {}		-- [entityIndex] = true when removed through _EntRemove
GM9.ChaseDistance = {}	-- [playerIndex] = distance (stored; not applied yet)
GM9.Spawns = GM9.Spawns or {}	-- gmod_player_start entities
GM9.ButtonState = GM9.ButtonState or {}

--[[
	The _ API (only what's needed so far; anything else warns once and returns nil)
--]]

local api = {}

function api._OpenScript(path)
	return GM9.OpenScript(path)
end

function api._Msg(text)
	text = tostring(text):gsub("\n$", "")
	print(text)
end

function api._PrintMessage(playerIndex, printType, text)
	local p = Ply(playerIndex)
	if (p) then
		p:ShowMessage(tostring(text))
	else
		api._Msg(text)
	end
end

function api._MaxPlayers()
	return Int(MaxPlayers())
end

function api._CurTime()
	return Engines.GetCurrentTime()
end

-- Players

function api._PlayerInfo(i, what)
	local p = Ply(i)

	if (what == "connected") then
		return p ~= nil
	end
	if (not p) then
		return nil
	end

	if (what == "name") then
		return p:GetPlayerName()
	elseif (what == "team") then
		return Int(p:GetTeamNumber())
	elseif (what == "alive") then
		return p:IsAlive()
	elseif (what == "kills") then
		return Int(p:GetFrags())
	elseif (what == "deaths") then
		return Int(p:GetDeaths())
	elseif (what == "health") then
		return Int(p:GetHealth())
	elseif (what == "armor") then
		return Int(p:GetArmor())
	elseif (what == "ping") then
		return Int(p:GetPing())
	elseif (what == "networkid") then
		return p:GetSteamId()
	elseif (what == "entindex") then
		return Int(i)
	elseif (what == "weapon") then
		local w = p:GetActiveWeapon()
		return IsValidObject(w) and w:GetClassName() or ""
	elseif (what == "model") then
		return ""
	end

	WarnOnce("PlayerInfo:" .. tostring(what), "_PlayerInfo: unknown field '" .. tostring(what) .. "'")
	return nil
end

function api._PlayerIsKeyDown(i, key)
	local p = Ply(i)
	return p ~= nil and p:IsKeyDown(key)
end

-- GMod 9's "shoot angle" is the aim direction vector
function api._PlayerGetShootAng(i)
	local p = Ply(i)
	if (p) then
		return ToVector3(p:GetAimVector())
	end
end

function api._PlayerGetShootPos(i)
	local p = Ply(i)
	if (p) then
		return ToVector3(p:GetWeaponShootPosition())
	end
end

function api._PlayerChangeTeam(i, team)
	local p = Ply(i)
	if (not p) then
		return
	end

	-- HL2MP only has teams up to 3 (Combine/Rebels); GMod 9 had Blue/Yellow/Green/Red (2-5)
	if (team > 3) then
		WarnOnce("team" .. team, "_PlayerChangeTeam: team " .. team .. " doesn't exist here, using team 3")
		team = 3
	end

	GM9.Spectate[i] = nil
	if (GM9.Debug) then
		GM9.Log("_PlayerChangeTeam(" .. i .. ", " .. team .. ")  from team " .. tostring(p:GetTeamNumber()))
	end

	-- Joining a playing team has to go through HL2MP's jointeam handling (Player:JoinTeam,
	-- server C++): it also takes the player out of HL2MP's observer state, which a bare
	-- ChangeTeam doesn't, so the player would stay a spectator after respawning.
	if (team ~= 1 and p.JoinTeam and p:JoinTeam(team)) then
		return
	end
	p:ChangeTeam(team)
end

function api._PlayerAddScore(i, amount)
	local p = Ply(i)
	if (p) then
		p:IncrementFragCount(amount or 1)
	end
end

function api._PlayerAddDeath(i, amount)
	local p = Ply(i)
	if (p) then
		p:IncrementDeathCount(amount or 1)
	end
end

function api._PlayerFreeze(i, freeze)
	local p = Ply(i)
	if (not p) then
		return
	end

	if (freeze) then
		p:AddFlag(FL_FROZEN)
	else
		p:RemoveFlag(FL_FROZEN)
	end
end

function api._PlayerKill(i)
	local p = Ply(i)
	if (p) then
		p:Kill()
	end
end

function api._PlayerSetModel(i, model)
	local p = Ply(i)
	if (p) then
		api._EntPrecacheModel(model)
		p:SetModel(model)
	end
end

function api._PlayerSpectatorStart(i, gm9Mode)
	local p = Ply(i)
	if (not p) then
		return
	end

	local mode = OBS_FROM_GM9[gm9Mode] or gm9Mode
	GM9.SetObserverMode(p, mode)

	if (GM9.Debug) then
		GM9.Log("_PlayerSpectatorStart(" .. i .. ", " .. tostring(gm9Mode) .. ")  observer now: " .. tostring(p:IsObserver()))
	end

	GM9.Spectate[i] = GM9.Spectate[i] or {}
	GM9.Spectate[i].mode = mode
end

function api._PlayerSpectatorTarget(i, target)
	local p = Ply(i)
	local e = Ent(target)
	if (not p or not e) then
		return
	end

	p:SetObserverTarget(e)

	GM9.Spectate[i] = GM9.Spectate[i] or {}
	GM9.Spectate[i].target = e
end

-- Puts a player into an observer mode so it stays there.
-- Player:ForceStartObserverMode (server C++) goes through the engine's normal
-- SetObserverMode, and also gets past HL2MP refusing a living player who didn't ask
-- to spectate. The Lua SetObserverMode binding is the engine's *temporary*
-- ForceObserverMode, which CheckObserverSettings switches back to the previous mode
-- (that's what made the camera jump into the melon now and then).
function GM9.SetObserverMode(p, mode)
	if (p.ForceStartObserverMode) then
		p:ForceStartObserverMode(mode)
	elseif (p:IsObserver()) then
		p:SetObserverMode(mode)
	else
		p:StartObserverMode(mode)
	end

	-- GMod 9 had no spectator bars; starting observer mode shows the 2013 spectator GUI
	if (p.ShowViewPortPanel) then
		p:ShowViewPortPanel("specgui", false)
	end
end

-- TODO: needs a C++ hook into the chase camera; the default chase distance is used for now
function api._PlayerSetChaseCamDistance(i, distance)
	GM9.ChaseDistance[i] = distance
end

-- Entities

function api._EntPrecacheModel(model)
	local allowed = _R.Entity.IsPrecacheAllowed()
	_R.Entity.SetAllowPrecache(true)
	local index = _R.Entity.PrecacheModel(model)
	_R.Entity.SetAllowPrecache(allowed)
	return index
end

function api._EntCreate(className)
	local e = Entities.CreateByName(className)
	if (not IsValidObject(e)) then
		return 0
	end

	local index = e:GetEntityIndex()
	if (className == "prop_physics" or className == "physics_prop" or className == "prop_physics_multiplayer") then
		GM9.Tracked[index] = e
	end
	return Int(index)
end

function api._EntSpawn(i)
	local p = Ply(i)
	if (p) then
		-- Respawning a player
		if (GM9.Debug) then
			GM9.Log("_EntSpawn(player " .. i .. ")  team " .. tostring(p:GetTeamNumber()))
		end
		GM9.Spectate[i] = nil
		if (p:IsObserver()) then
			p:StopObserverMode()
		end
		p:ForceRespawn()
		return
	end

	local e = Ent(i)
	if (e) then
		e:Spawn()
	end
end

function api._EntActivate(i)
	local e = Ent(i)
	if (e) then
		e:Activate()
	end
end

function api._EntRemove(i)
	local e = Ent(i)
	if (e) then
		GM9.Removing[i] = true
		e:Remove()
	end
end

function api._EntSetModel(i, model)
	local e = Ent(i)
	if (e) then
		api._EntPrecacheModel(model)
		e:SetModel(model)
	end
end

function api._EntSetPos(i, pos)
	local e = Ent(i)
	if (e) then
		e:SetPosition(ToVector(pos))
	end
end

function api._EntGetPos(i)
	local e = Ent(i)
	if (e) then
		return ToVector3(e:GetPosition())
	end
	return { x = 0, y = 0, z = 0 }
end

function api._EntFire(i, input, value, delay)
	local e = Ent(i)
	if (e) then
		e:Fire(input, value ~= nil and tostring(value) or "", delay or 0)

		-- A melon that "break" doesn't remove stays in the race as a stray prop; make sure it goes
		if (type(input) == "string" and input:lower() == "break" and GM9.Tracked[i]) then
			GM9.Defer((tonumber(delay) or 0) + 0.2, function()
				if (IsValidObject(e) and GM9.Tracked[i] == e) then
					GM9.Log("break didn't remove prop " .. i .. " (" .. tostring(e:GetClass()) .. "); removing it")
					e:Remove()
				end
			end)
		end
	end
end

function api._EntSetKeyValue(i, key, value)
	local e = Ent(i)
	if (e) then
		if (type(value) == "table") then
			value = value.x .. " " .. value.y .. " " .. value.z
		end
		e:KeyValue(key, tostring(value))
	end
end

function api._EntGetType(i)
	local e = Ent(i)
	return e and e:GetClass() or ""
end

-- Physics

api._phys = {}

local function Phys(i)
	local e = Ent(i)
	if (e) then
		local phys = e:GetPhysicsObject()
		if (phys and phys ~= NULL and (phys.IsValid == nil or phys:IsValid())) then
			return phys
		end
	end
end

function api._phys.ApplyForceCenter(i, force)
	local phys = Phys(i)
	if (phys) then
		phys:ApplyForceCenter(ToVector(force))
	end
end

function api._phys.EnableMotion(i, enable)
	local phys = Phys(i)
	if (phys) then
		phys:EnableMotion(enable and true or false)
		if (enable) then
			phys:Wake()
		end
	end
end

function api._phys.Wake(i)
	local phys = Phys(i)
	if (phys) then
		phys:Wake()
	end
end

function api._phys.Sleep(i)
	local phys = Phys(i)
	if (phys) then
		phys:Sleep()
	end
end

function api._phys.EnableGravity(i, enable)
	local phys = Phys(i)
	if (phys) then
		phys:EnableGravity(enable and true or false)
	end
end

function api._phys.EnableCollisions(i, enable)
	local phys = Phys(i)
	if (phys) then
		phys:EnableCollisions(enable and true or false)
	end
end

function api._phys.SetMass(i, mass)
	local phys = Phys(i)
	if (phys) then
		phys:SetMass(mass)
	end
end

function api._phys.GetMass(i)
	local phys = Phys(i)
	return phys and phys:GetMass() or 0
end

function api._phys.IsAsleep(i)
	local phys = Phys(i)
	return phys ~= nil and phys:IsAsleep()
end

function api._phys.HasPhysics(i)
	return Phys(i) ~= nil
end

-- Sound: played on every client as a UI sound (GMod 9's _PlaySound was global)
function api._PlaySound(sound)
	GM9.SendHud(0, { op = "sound", sound = sound })
end

--[[
	_GModText / _GModRect: build an element, then Send / SendAnimate / Hide it.
	The client (cl_gm9_hud.lua) draws them.
--]]

local building = { text = {}, rect = {} }

local function Start(kind, first)
	building[kind] = { first = first }
end

local function SetField(kind, key, value)
	building[kind][key] = value
end

-- playerIndex 0 = everyone
function GM9.SendHud(playerIndex, message)
	Networks.Start("gm9_hud")
	Networks.WriteTable(message)

	if (playerIndex == 0 or playerIndex == nil) then
		Networks.Broadcast()
		return
	end

	local p = Ply(playerIndex)
	if (p) then
		Networks.Send(p)
	else
		Networks.Cancel()
	end
end

local function Send(kind, playerIndex, index, op, extra)
	local message = { kind = kind, op = op, index = index }
	for k, v in pairs(building[kind]) do
		message[k] = v
	end
	if (extra) then
		for k, v in pairs(extra) do
			message[k] = v
		end
	end
	GM9.SendHud(playerIndex, message)
end

function api._GModText_Start(font)
	Start("text")
	SetField("text", "font", font or "Default")
end

function api._GModText_SetColor(r, g, b, a)
	SetField("text", "color", { r, g, b, a or 255 })
end

function api._GModText_SetTime(life, fadeIn, fadeOut)
	SetField("text", "time", { life or 0, fadeIn or 0, fadeOut or 0 })
end

function api._GModText_SetPos(x, y)
	SetField("text", "pos", { x, y })
end

function api._GModText_SetText(text)
	SetField("text", "text", tostring(text))
end

function api._GModText_SetDelay(delay)
	SetField("text", "delay", delay)
end

function api._GModText_SetEntity(i)
	SetField("text", "entity", i)
end

function api._GModText_SetEntityOffset(offset)
	SetField("text", "entityOffset", { offset.x, offset.y, offset.z })
end

function api._GModText_Send(playerIndex, index)
	Send("text", playerIndex, index, "send")
end

function api._GModText_SendAnimate(playerIndex, index, duration, ease)
	Send("text", playerIndex, index, "animate", { duration = duration, ease = ease })
end

function api._GModText_Hide(playerIndex, index, fadeTime, delay)
	GM9.SendHud(playerIndex, { kind = "text", op = "hide", index = index, fade = fadeTime or 0, delay = delay or 0 })
end

function api._GModRect_Start(material)
	Start("rect")
	SetField("rect", "material", material or "")
end

function api._GModRect_SetPos(x, y, w, h)
	SetField("rect", "pos", { x, y, w, h })
end

function api._GModRect_SetColor(r, g, b, a)
	SetField("rect", "color", { r, g, b, a or 255 })
end

function api._GModRect_SetTime(life, fadeIn, fadeOut)
	SetField("rect", "time", { life or 0, fadeIn or 0, fadeOut or 0 })
end

function api._GModRect_SetDelay(delay)
	SetField("rect", "delay", delay)
end

function api._GModRect_Send(playerIndex, index)
	Send("rect", playerIndex, index, "send")
end

function api._GModRect_SendAnimate(playerIndex, index, duration, ease)
	Send("rect", playerIndex, index, "animate", { duration = duration, ease = ease })
end

function api._GModRect_Hide(playerIndex, index, fadeTime, delay)
	GM9.SendHud(playerIndex, { kind = "rect", op = "hide", index = index, fade = fadeTime or 0, delay = delay or 0 })
end

--[[
	Environment setup and loading
--]]

function GM9.CreateEnvironment()
	env = {}

	BuildShims(env)
	for name, f in pairs(api) do
		env[name] = f
	end

	setmetatable(env, {
		__index = function(_, key)
			local value = _G[key]
			if (value ~= nil) then
				return value
			end

			-- An engine function we don't have yet: warn once, do nothing
			if (type(key) == "string" and key:sub(1, 1) == "_" and key:sub(2, 2):match("%u")) then
				WarnOnce(key, key .. " isn't implemented yet")
				return function() end
			end
		end,
	})

	GM9.Env = env

	-- What GMod 9's init/init.lua opens on startup
	for _, include in ipairs({ "defines", "concommands", "backcompat", "vector3", "luathink",
		"player", "misc", "events", "timers", "eventhook" }) do
		GM9.OpenScript("includes/" .. include .. ".lua")
	end

	-- Track key presses per player for eventKeyPressed/eventKeyReleased
	GM9.Keys = {}
	for name, value in pairs(env) do
		if (type(name) == "string" and name:match("^IN_") and type(value) == "number") then
			table.insert(GM9.Keys, value)
		end
	end
	GM9.ButtonState = {}
end

function GM9.EnsureEnvironment()
	if (not GM9.Env) then
		GM9.CreateEnvironment()
	end
end

-- Called by gmod_gamesetup (GamemodeScript keyvalue, e.g. "melonracer/init.lua")
function GM9.RunGamemodeScript(script)
	GM9.EnsureEnvironment()
	if (GM9.ScriptLoaded) then
		return
	end
	GM9.ScriptLoaded = script

	GM9.Log("Running gamemode script " .. script)
	GM9.OpenScript("gamemodes/" .. script)
	GM9.Call("gamerulesStartMap")
end

--[[
	Deferred calls (GMod 9 events that need the client to be ready)
--]]

GM9.Deferred = {}

function GM9.Defer(delay, f)
	table.insert(GM9.Deferred, { time = Engines.GetCurrentTime() + delay, func = f })
end

local function RunDeferred()
	local now = Engines.GetCurrentTime()
	for i = #GM9.Deferred, 1, -1 do
		local d = GM9.Deferred[i]
		if (d.time <= now) then
			table.remove(GM9.Deferred, i)
			local ok, err = pcall(d.func)
			if (not ok) then
				GM9.Log(tostring(err))
			end
		end
	end
end

--[[
	Per-frame work
--]]

local function CheckKeys()
	for _, p in ipairs(Players.GetAll()) do
		if (IsValidObject(p)) then
			local i = p:GetEntityIndex()
			local state = GM9.ButtonState[i] or {}
			GM9.ButtonState[i] = state

			for _, key in ipairs(GM9.Keys) do
				local down = p:IsKeyDown(key)
				if (down and not state[key]) then
					if (GM9.Debug) then
						GM9.Log("eventKeyPressed(" .. i .. ", " .. key .. ")  team " .. tostring(p:GetTeamNumber()))
					end
					GM9.Call("eventKeyPressed", i, key)
				elseif (not down and state[key]) then
					GM9.Call("eventKeyReleased", i, key)
				end
				state[key] = down
			end
		end
	end
end

-- Keep players on the observer mode/target the script set (the observer
-- buttons would otherwise cycle targets or switch to roaming)
local function KeepSpectating()
	for i, want in pairs(GM9.Spectate) do
		local p = Ply(i)
		if (not p) then
			GM9.Spectate[i] = nil
		elseif (p:IsObserver()) then
			if (want.mode and p:GetObserverMode() ~= want.mode) then
				GM9.SetObserverMode(p, want.mode)
			end
			if (IsValidObject(want.target) and Idx(p:GetObserverTarget()) ~= Idx(want.target)) then
				p:SetObserverTarget(want.target)
			end
		end
	end
end

-- GMod 9's eventPropBreak: a tracked physics prop disappeared without _EntRemove.
-- A prop whose physics fell out of the world (the engine then ignores its position and
-- it hangs where it was) is broken too, so the gamemode respawns it like GMod 9 did.
local WORLD_BOTTOM = -16384

local function CheckBrokenProps()
	for i, e in pairs(GM9.Tracked) do
		if (IsValidObject(e)) then
			local phys = e:GetPhysicsObject()
			if (phys and phys ~= NULL and phys:IsValid()) then
				local pos = phys:GetPosition()
				if (pos and pos.z < WORLD_BOTTOM) then
					GM9.Log("prop " .. i .. " fell out of the world at " .. math.floor(pos.x) .. " " .. math.floor(pos.y) .. "; breaking it")
					e:Remove()
				end
			end
		end

		if (not IsValidObject(e)) then
			GM9.Tracked[i] = nil
			if (GM9.Removing[i]) then
				GM9.Removing[i] = nil
			else
				GM9.Call("eventPropBreak", 0, i)
			end
		end
	end
end

GM9.Debug = true	-- logs key presses, team changes and respawns to data/gm9_log.txt

-- Debug: log a tracked prop that is barely moving (where a melon gets stuck), once a second
local stuckSamples = {}
local function TraceStuckProps()
	local now = Engines.GetCurrentTime()
	for i, e in pairs(GM9.Tracked) do
		if (IsValidObject(e)) then
			local phys = e:GetPhysicsObject()
			if (phys and phys ~= NULL and phys:IsValid()) then
				local pos = phys:GetPosition()
				local last = stuckSamples[i]
				if (not last or now - last.time >= 1) then
					if (last) then
						local dx, dy, dz = pos.x - last.x, pos.y - last.y, pos.z - last.z
						local moved = math.sqrt(dx * dx + dy * dy + dz * dz)
						if (moved < 24) then
							GM9.Log(string.format("prop %d slow: at %.1f %.1f %.1f, moved %.1f in 1s", i, pos.x, pos.y, pos.z, moved))
						end
					end
					stuckSamples[i] = { time = now, x = pos.x, y = pos.y, z = pos.z }
				end
			end
		else
			stuckSamples[i] = nil
		end
	end
end

function GM9.Think()
	if (not env) then
		return
	end

	if (GM9.Debug and not GM9.LoggedThink) then
		GM9.LoggedThink = true
		local count = 0
		for _ in pairs(Players.GetAll()) do
			count = count + 1
		end
		GM9.Log("Think running; " .. count .. " player(s), " .. #GM9.Keys .. " keys watched")
	end

	RunDeferred()
	CheckKeys()
	CheckBrokenProps()
	if (GM9.Debug) then
		TraceStuckProps()
	end
	GM9.Call("DoLuaThinkFunctions")	-- runs GMod 9's timers
	GM9.Call("gamerulesThink")
	KeepSpectating()
end
