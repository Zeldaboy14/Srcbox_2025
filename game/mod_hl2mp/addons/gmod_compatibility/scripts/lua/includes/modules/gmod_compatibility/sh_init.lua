--[[
	Note that most code here belongs to Facepunch Studios. It has been
	modified and adapted to provide compatibility with Experiment.
--]]

Include("sh_enumerations.lua")
Include("sh_file.lua")

--[[
	All our libraries are plural, while with Garry's Mod they vary between plural and singular.
--]]

bit = require("bitwise")
gamemode = require("gamemodes")
hook = require("hooks")
timer = require("timers")
net = Networks

-- weapons = require("scripted_weapons")
-- scripted_ents = require("scripted_entities") -- Gmods scripted_ents is compatible with our ScriptedEntities

--[[
	Renames and other polyfills
--]]
DeriveGamemode = InheritGamemode

include = Include
Msg = PrintMessage
MsgN = PrintMessageLine

OriginalPrintMessage = PrintMessage
PrintMessage = Players.ClientPrintToAll

function GetConVar_Internal(name)
	local consoleVariable = ConsoleVariables.Get(name)

	if (not IsValid(consoleVariable)) then
		-- debug.PrintError("GetConVar: Couldn't find convar '" .. name .. "'")
		return nil
	end

	return consoleVariable
end

-- Lua 5.1's getfenv/setfenv, which GMod (LuaJIT) still has and Lua 5.4 dropped.
-- A function's environment is its _ENV upvalue; functions without one use _G.
if (not getfenv) then
	local function findEnvironment(func)
		local index = 1

		while (true) do
			local name, value = debug.getupvalue(func, index)

			if (not name) then
				return nil
			end

			if (name == "_ENV") then
				return value, index
			end

			index = index + 1
		end
	end

	local function resolveFunction(target, level)
		if (target == nil) then
			target = 1
		end

		if (type(target) == "number") then
			if (target == 0) then
				return nil
			end

			local info = debug.getinfo(target + level, "f")

			if (not info) then
				error("bad argument #1 (invalid level)", level + 1)
			end

			return info.func
		end

		return target
	end

	function getfenv(target)
		local func = resolveFunction(target, 2)

		if (type(func) ~= "function") then
			return _G
		end

		return (findEnvironment(func)) or _G
	end

	function setfenv(target, environment)
		local func = resolveFunction(target, 2)

		if (type(func) ~= "function") then
			return func
		end

		local _, index = findEnvironment(func)

		if (index) then
			-- Give only this function a new _ENV (setupvalue would change it for
			-- every function sharing the same upvalue)
			local holder = function() return environment end
			debug.upvaluejoin(func, index, holder, 1)
		end

		return func
	end
end

-- GMod: calls func with the arguments, reports errors without halting, returns success
function ProtectedCall(func, ...)
	local results = table.pack(xpcall(func, debug.traceback, ...))

	if (not results[1]) then
		ErrorNoHalt(tostring(results[2]) .. "\n")
	end

	return results[1]
end

-- GMod loads the usermessage module on the server too (Hook does nothing there);
-- shared addon files call usermessage.Hook without checking the realm
if (SERVER and rawget(_G, "usermessage") == nil) then
	usermessage = {
		Hook = function() end,
		GetTable = function() return {} end,
		IncomingMessage = function() end,
	}
end

-- GMod's client 'usermessage' library, on top of Experiment's UserMessages
if (CLIENT and rawget(_G, "usermessage") == nil and rawget(_G, "UserMessages") ~= nil) then
	usermessage = {
		Hook = function(messageName, func, ...)
			UserMessages.Hook(messageName, func, ...)
		end,
		GetTable = function()
			return UserMessages.Hooks or {}
		end,
		IncomingMessage = function(messageName, message)
			UserMessages.OnMessageReceived(messageName, message)
		end,
	}
end

--[[
	GMod's CreateSound / CSoundPatch, on top of Entity:EmitSound.
	A patch plays on CHAN_STATIC; volume/pitch changes are sent to the playing
	sound with SND_CHANGE_VOL/SND_CHANGE_PITCH, and ramps (ChangeVolume/
	ChangePitch with a delta, FadeOut) are stepped from a Think hook.
--]]
do
	local SND_CHANGE_VOL = 1
	local SND_CHANGE_PITCH = 2
	local SND_STOP = 4
	local PATCH_CHANNEL = 6 -- CHAN_STATIC
	local STEP_INTERVAL = 0.05

	local SOUNDPATCH = {}
	SOUNDPATCH.__index = SOUNDPATCH
	SOUNDPATCH.MetaName = "CSoundPatch"

	local ramping = {}

	local function emit(patch, flags)
		local entity = patch._entity

		if (not entity or not entity.EmitSound) then
			return
		end

		pcall(entity.EmitSound, entity, patch._sound, patch._level, patch._pitch, patch._volume,
			PATCH_CHANNEL, flags or 0, patch._dsp, patch._filter)
	end

	function SOUNDPATCH:__tostring()
		return "CSoundPatch [" .. tostring(self._sound) .. "]"
	end

	function SOUNDPATCH:Play()
		if (self._playing) then
			return
		end

		self._playing = true
		emit(self, 0)
	end

	function SOUNDPATCH:PlayEx(volume, pitch)
		self._volume = math.Clamp(tonumber(volume) or self._volume, 0, 1)
		self._pitch = math.Clamp(tonumber(pitch) or self._pitch, 0, 255)

		if (self._playing) then
			emit(self, SND_CHANGE_VOL + SND_CHANGE_PITCH)
		else
			self:Play()
		end
	end

	function SOUNDPATCH:Stop()
		ramping[self] = nil

		if (not self._playing) then
			return
		end

		self._playing = false
		emit(self, SND_STOP)
	end

	function SOUNDPATCH:IsPlaying()
		return self._playing
	end

	local function startRamp(patch, field, target, seconds, stopAtEnd)
		local ramp = ramping[patch] or {}
		ramp[field] = {
			from = patch[field],
			to = target,
			start = RealTime(),
			duration = seconds,
			stopAtEnd = stopAtEnd,
		}
		ramping[patch] = ramp
	end

	function SOUNDPATCH:ChangeVolume(volume, delta)
		volume = math.Clamp(tonumber(volume) or 0, 0, 1)

		if (delta and delta > 0 and self._playing) then
			startRamp(self, "_volume", volume, delta)
			return
		end

		if (ramping[self]) then ramping[self]._volume = nil end
		self._volume = volume

		if (self._playing) then
			emit(self, SND_CHANGE_VOL)
		end
	end

	function SOUNDPATCH:ChangePitch(pitch, delta)
		pitch = math.Clamp(tonumber(pitch) or 100, 0, 255)

		if (delta and delta > 0 and self._playing) then
			startRamp(self, "_pitch", pitch, delta)
			return
		end

		if (ramping[self]) then ramping[self]._pitch = nil end
		self._pitch = pitch

		if (self._playing) then
			emit(self, SND_CHANGE_PITCH)
		end
	end

	function SOUNDPATCH:FadeOut(seconds)
		if (not self._playing) then
			return
		end

		if (not seconds or seconds <= 0) then
			self:Stop()
			return
		end

		startRamp(self, "_volume", 0, seconds, true)
	end

	function SOUNDPATCH:GetVolume() return self._volume end
	function SOUNDPATCH:GetPitch() return self._pitch end
	function SOUNDPATCH:GetSoundLevel() return self._level end
	function SOUNDPATCH:SetSoundLevel(level) self._level = tonumber(level) or self._level end
	function SOUNDPATCH:GetDSP() return self._dsp end
	function SOUNDPATCH:SetDSP(dsp) self._dsp = tonumber(dsp) or 0 end

	function CreateSound(entity, soundName, filter)
		return setmetatable({
			_entity = entity,
			_sound = soundName,
			_filter = filter,
			_volume = 1,
			_pitch = 100,
			_level = 75, -- SNDLVL_NORM
			_dsp = 0,
			_playing = false,
		}, SOUNDPATCH)
	end

	local nextStep = 0

	hook.Add("Think", "GModCompatibility_SoundPatchRamps", function()
		local now = RealTime()

		if (now < nextStep or next(ramping) == nil) then
			return
		end

		nextStep = now + STEP_INTERVAL

		for patch, ramp in pairs(ramping) do
			local flags = 0
			local stop = false

			for field, info in pairs(ramp) do
				local fraction = math.Clamp((now - info.start) / info.duration, 0, 1)
				patch[field] = info.from + (info.to - info.from) * fraction
				flags = flags + (field == "_volume" and SND_CHANGE_VOL or SND_CHANGE_PITCH)

				if (fraction >= 1) then
					ramp[field] = nil
					stop = stop or info.stopAtEnd
				end
			end

			if (stop) then
				patch:Stop()
			else
				if (flags > 0 and patch._playing) then
					emit(patch, flags)
				end

				if (next(ramp) == nil) then
					ramping[patch] = nil
				end
			end
		end
	end)
end

-- GMod engine function: true if a console variable with this name exists
function ConVarExists(name)
	if (type(name) ~= "string") then
		return false
	end

	return GetConVar_Internal(name) ~= nil
end

-- Before Lua 5.3 math.random(1, #emptyTable) would return 1, but it errors now
local originalMathRandom = math.random

function math.random(min, max)
	if (not min and not max) then
		return originalMathRandom()
	end

	-- GMod's LuaJIT floors fractional bounds; Lua 5.4 errors on them
	-- ("number has no integer representation")
	min = min and math.floor(tonumber(min) or 0)
	max = max and math.floor(tonumber(max) or 0)

	if (not max) then
		max = min
		min = 1
	end

	if (max < min) then
		return originalMathRandom(max, min)
	end

	return originalMathRandom(min, max)
end

-- Garry's Mod has utf8.force built into its engine (markup.Parse uses it), Lua 5.4's utf8 library doesn't.
-- Returns the string with every invalid UTF-8 byte replaced by U+FFFD, like GMod does.
if (utf8 and not utf8.force) then
	function utf8.force(str)
		str = tostring(str)

		local parts = {}
		local position = 1

		while (true) do
			local length, invalidPosition = utf8.len(str, position)

			if (length) then
				parts[#parts + 1] = str:sub(position)
				break
			end

			parts[#parts + 1] = str:sub(position, invalidPosition - 1)
			parts[#parts + 1] = "\239\191\189" -- U+FFFD replacement character
			position = invalidPosition + 1
		end

		return table.concat(parts)
	end
end

function math.pow(base, exponent)
	return base ^ exponent
end

-- Since Lua 5.3 non-integers give an error
local originalRandomSeed = math.randomseed

function math.randomseed(seed)
	originalRandomSeed(math.floor(seed))
end

timer.RepsLeft = Timers.RepetitionsLeft

util = Utilities

-- Srcbox: pixel visibility queries live in Renders (client)
if (CLIENT and Renders) then
	util.GetPixelVisibleHandle = util.GetPixelVisibleHandle or Renders.GetPixelVisibleHandle
	util.PixelVisible = util.PixelVisible or Renders.PixelVisible
end

util.PrecacheModel = _R.Entity.PrecacheModel
util.PrecacheSound = _R.Entity.PrecacheSound

-- TODO: This is no help in fixing the crash in _R.Entity.PrecacheModel
-- util.PrecacheModel("models/f_anm.mdl")
-- util.PrecacheModel("models/m_anm.mdl")
-- util.PrecacheModel("models/z_anm.mdl")
-- util.PrecacheModel("models/humans/female_gestures.mdl")
-- util.PrecacheModel("models/humans/female_ss.mdl")
-- util.PrecacheModel("models/humans/female_postures.mdl")
-- util.PrecacheModel("models/humans/female_shared.mdl")
-- util.PrecacheModel("models/humans/male_gestures.mdl")
-- util.PrecacheModel("models/humans/male_ss.mdl")
-- util.PrecacheModel("models/humans/male_postures.mdl")
-- util.PrecacheModel("models/humans/male_shared.mdl")

util.AddNetworkString = Networks.AddNetworkString
util.NetworkIDToString = Networks.NetworkIdToString
util.NetworkStringToID = Networks.NetworkStringToId

util.TraceLine = Traces.TraceLine
util.TraceHull = Traces.TraceHull
util.TraceEntity = Traces.TraceEntity
util.PointContents = Traces.PointContents

util.Compress = Serializers.LzmaCompress
util.Decompress = Serializers.LzmaDecompress

util.JSONToTable = function(json)
	-- wraped in a pcall, because gmod returns nothing on failure (without error)
	local success, result = pcall(Json.Decode, json)

	if (not success) then
		return
	end

	return result
end

util.TableToJSON = function(table)
	return Json.Encode(table)
end
--[[
	GMod's util.TableToJSON / util.JSONToTable.
	- Sequential tables become arrays, everything else an object (number keys are
	  written as strings and turned back into numbers when decoding).
	- Vectors are written as "[x y z]", Angles as "{p y r}", Colors as {r,g,b,a}.
	- Values JSON can't hold (functions, entities, ...) are left out.
--]]
do
	local escapes = {
		["\\"] = "\\\\", ["\""] = "\\\"", ["\b"] = "\\b", ["\f"] = "\\f",
		["\n"] = "\\n", ["\r"] = "\\r", ["\t"] = "\\t",
	}

	local function encodeString(value)
		return "\"" .. value:gsub("[%c\\\"]", function(c)
			return escapes[c] or string.format("\\u%04x", c:byte())
		end) .. "\""
	end

	local function encodeNumber(value)
		if (value ~= value or value == math.huge or value == -math.huge) then
			return "0"
		end

		if (math.type(value) == "integer") then
			return tostring(value)
		end

		if (value == math.floor(value) and math.abs(value) < 1e15) then
			return string.format("%d", value)
		end

		return string.format("%.14g", value)
	end

	local function formatNumber(value)
		return encodeNumber(value)
	end

	local encodeValue

	local function encodeTable(value, pretty, depth, seen)
		if (seen[value]) then
			return nil -- circular reference: leave it out
		end

		seen[value] = true

		local count = 0
		for _ in pairs(value) do
			count = count + 1
		end

		local isArray = count > 0 and count == #value
		local parts = {}
		local indent = pretty and ("\n" .. string.rep("\t", depth + 1)) or ""
		local closingIndent = pretty and ("\n" .. string.rep("\t", depth)) or ""
		local separator = pretty and ": " or ":"

		if (isArray) then
			for i = 1, count do
				local encoded = encodeValue(value[i], pretty, depth + 1, seen)
				parts[#parts + 1] = indent .. (encoded or "null")
			end

			seen[value] = nil

			if (#parts == 0) then
				return "[]"
			end

			return "[" .. table.concat(parts, ",") .. closingIndent .. "]"
		end

		-- Stable output: sort the keys
		local keys = {}
		for key in pairs(value) do
			local keyType = type(key)

			if (keyType == "string" or keyType == "number") then
				keys[#keys + 1] = key
			end
		end

		table.sort(keys, function(a, b)
			return tostring(a) < tostring(b)
		end)

		for _, key in ipairs(keys) do
			local encoded = encodeValue(value[key], pretty, depth + 1, seen)

			if (encoded ~= nil) then
				local keyString = type(key) == "number" and formatNumber(key) or key
				parts[#parts + 1] = indent .. encodeString(keyString) .. separator .. encoded
			end
		end

		seen[value] = nil

		if (#parts == 0) then
			return count == 0 and "[]" or "{}"
		end

		return "{" .. table.concat(parts, ",") .. closingIndent .. "}"
	end

	encodeValue = function(value, pretty, depth, seen)
		if (IsColor(value)) then
			return encodeTable({ r = value.r, g = value.g, b = value.b, a = value.a }, pretty, depth, seen)
		elseif (isvector(value)) then
			return string.format("\"[%s %s %s]\"", formatNumber(value.x), formatNumber(value.y), formatNumber(value.z))
		elseif (isangle(value)) then
			return string.format("\"{%s %s %s}\"", formatNumber(value.p or value.x), formatNumber(value.y), formatNumber(value.r or value.z))
		end

		local valueType = type(value)

		if (valueType == "string") then
			return encodeString(value)
		elseif (valueType == "number") then
			return encodeNumber(value)
		elseif (valueType == "boolean") then
			return value and "true" or "false"
		elseif (valueType == "table") then
			return encodeTable(value, pretty, depth, seen)
		end

		return nil
	end

	util.TableToJSON = function(value, prettyPrint)
		if (type(value) ~= "table" and not IsColor(value)) then
			return nil
		end

		return encodeValue(value, prettyPrint and true or false, 0, {})
	end

	local numberPattern = "(%-?[%d%.]+[eE]?[%-%+]?%d*)"
	local vectorPattern = "^%[" .. numberPattern .. " " .. numberPattern .. " " .. numberPattern .. "%]$"
	local anglePattern = "^{" .. numberPattern .. " " .. numberPattern .. " " .. numberPattern .. "}$"

	local function convert(value, ignoreConversions)
		if (type(value) == "string") then
			if (ignoreConversions) then
				return value
			end

			local x, y, z = value:match(vectorPattern)
			if (x) then
				return Vector(tonumber(x), tonumber(y), tonumber(z))
			end

			local p, yaw, r = value:match(anglePattern)
			if (p) then
				return Angle(tonumber(p), tonumber(yaw), tonumber(r))
			end

			return value
		end

		if (type(value) ~= "table") then
			return value
		end

		local result = {}

		for key, child in pairs(value) do
			if (type(key) == "string") then
				key = tonumber(key) or key
			end

			result[key] = convert(child, ignoreConversions)
		end

		return result
	end

	util.JSONToTable = function(json, ignoreLimits, ignoreConversions)
		if (type(json) ~= "string") then
			return nil
		end

		local success, result = pcall(Json.Decode, json)

		if (not success or type(result) ~= "table") then
			return nil
		end

		return convert(result, ignoreConversions)
	end
end

util.IsValidProp = util.IsValidPhysicsProp
-- Srcbox: GMod's util.MD5 (hex digest), pure Lua on 5.4 integer bit operations
if (not util.MD5) then
	local K = {}
	for i = 0, 63 do
		K[i] = math.floor(math.abs(math.sin(i + 1)) * 4294967296) & 0xFFFFFFFF
	end

	local S = {
		7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
		5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20,
		4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
		6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21,
	}

	local function rotl(x, n)
		x = x & 0xFFFFFFFF
		return ((x << n) | (x >> (32 - n))) & 0xFFFFFFFF
	end

	function util.MD5(data)
		data = tostring(data)
		local length = #data
		data = data .. "\128" .. string.rep("\0", (55 - length) % 64) .. string.pack("<I8", (length * 8) & 0xFFFFFFFFFFFFFFFF)

		local a0, b0, c0, d0 = 0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476

		for chunk = 1, #data, 64 do
			local M = { string.unpack("<" .. string.rep("I4", 16), data, chunk) }
			local A, B, C, D = a0, b0, c0, d0

			for i = 0, 63 do
				local F, g
				if (i < 16) then
					F = (B & C) | (~B & D); g = i
				elseif (i < 32) then
					F = (D & B) | (~D & C); g = (5 * i + 1) % 16
				elseif (i < 48) then
					F = B ~ C ~ D; g = (3 * i + 5) % 16
				else
					F = C ~ (B | ~D); g = (7 * i) % 16
				end

				F = (F + A + K[i] + M[g + 1]) & 0xFFFFFFFF
				A = D
				D = C
				C = B
				B = (B + rotl(F, S[i + 1])) & 0xFFFFFFFF
			end

			a0 = (a0 + A) & 0xFFFFFFFF
			b0 = (b0 + B) & 0xFFFFFFFF
			c0 = (c0 + C) & 0xFFFFFFFF
			d0 = (d0 + D) & 0xFFFFFFFF
		end

		local digest = string.pack("<I4I4I4I4", a0, b0, c0, d0)
		return (digest:gsub(".", function(c) return string.format("%02x", c:byte()) end))
	end
end

-- Srcbox: GMod's util.KeyValuesToTable( text, usesEscapeSequences, preserveKeyCase )
-- Parses Valve KeyValues text; keys are lowercased unless preserveKeyCase, numeric
-- values become numbers, nested blocks become tables. Returns the root block's contents.
if (not util.KeyValuesToTable) then
	local function tokenize(text, escapes)
		local tokens = {}
		local position, length = 1, #text

		while (position <= length) do
			local c = text:sub(position, position)

			if (c:match("%s")) then
				position = position + 1
			elseif (text:sub(position, position + 1) == "//") then
				local lineEnd = text:find("\n", position, true)
				position = lineEnd and (lineEnd + 1) or (length + 1)
			elseif (c == "{" or c == "}") then
				tokens[#tokens + 1] = { type = c }
				position = position + 1
			elseif (c == "[") then
				-- conditional like [$WIN32]; ignored
				local close = text:find("]", position, true)
				position = close and (close + 1) or (length + 1)
			elseif (c == '"') then
				local buffer = {}
				position = position + 1

				while (position <= length) do
					local ch = text:sub(position, position)

					if (escapes and ch == "\\") then
						local nextChar = text:sub(position + 1, position + 1)
						buffer[#buffer + 1] = (nextChar == "n" and "\n") or (nextChar == "t" and "\t") or nextChar
						position = position + 2
					elseif (ch == '"') then
						position = position + 1
						break
					else
						buffer[#buffer + 1] = ch
						position = position + 1
					end
				end

				tokens[#tokens + 1] = { type = "string", value = table.concat(buffer) }
			else
				local wordEnd = position

				while (wordEnd <= length and not text:sub(wordEnd, wordEnd):match('[%s{}"]')) do
					wordEnd = wordEnd + 1
				end

				tokens[#tokens + 1] = { type = "string", value = text:sub(position, wordEnd - 1) }
				position = wordEnd
			end
		end

		return tokens
	end

	local function parseBlock(tokens, index, preserveCase)
		local result = {}

		while (tokens[index] and tokens[index].type ~= "}") do
			local keyToken = tokens[index]

			if (keyToken.type ~= "string") then
				index = index + 1
			else
				local key = preserveCase and keyToken.value or keyToken.value:lower()
				local valueToken = tokens[index + 1]

				if (not valueToken) then
					break
				elseif (valueToken.type == "{") then
					result[key], index = parseBlock(tokens, index + 2, preserveCase)
					index = index + 1 -- skip "}"
				elseif (valueToken.type == "string") then
					result[key] = tonumber(valueToken.value) or valueToken.value
					index = index + 2
				else
					index = index + 1
				end
			end
		end

		return result, index
	end

	function util.KeyValuesToTable(text, usesEscapeSequences, preserveKeyCase)
		if (type(text) ~= "string") then
			return {}
		end

		local tokens = tokenize(text, usesEscapeSequences)
		local root = parseBlock(tokens, 1, preserveKeyCase)

		-- GMod returns the contents of the first (root) block
		local _, firstValue = next(root)
		if (type(firstValue) == "table" and next(root, (next(root))) == nil) then
			return firstValue
		end

		return root
	end
end

util.CRC = function(unencoded)
	return tostring(Serializers.Crc32(unencoded))
end

util.SharedRandom = Randoms.SharedRandomFloat

ents = {
	Create = Entities.CreateByName,
	-- TODO: Probably not correct, since CreateClientProp creates an ent with physics
	CreateClientProp = Entities.CreateClientEntity,

	GetAll = Entities.GetAll,
	GetCount = Entities.GetCount,
	GetEdictCount = Entities.GetEdictCount,
	FindAlongRay = Entities.GetAlongRay,
	FindInBox = Entities.GetInBox,
	FindInSphere = Entities.GetInSphere,
	FindByClass = Entities.GetByClass,
	-- GMod: ents.GetByIndex( index ) == Entity( index ) (used by HAT's hat_select)
	GetByIndex = Entities.Find,

	FindInPVS = function(viewOrigin)
		if (type(viewOrigin) == "Entity") then
			viewOrigin = viewOrigin:GetPosition()
		elseif (type(viewOrigin) == "Player") then
			local viewEntity = viewOrigin:GetViewEntity()

			if (IsValid(viewEntity)) then
				viewOrigin = viewEntity:GetPosition()
			else
				viewOrigin = viewOrigin:GetPosition()
			end
		end

		return Entities.GetInPvs(viewOrigin)
	end,
}

player = Players
player.GetByID = Players.FindByIndex
player.GetBySteamID = Players.FindBySteamId
player.GetBySteamID64 = Players.FindBySteamId64
player.GetByUniqueID = Players.FindByUniqueID
player.GetHumans = Players.GetAllHumans
player.GetBots = Players.GetAllBots

-- We don't have LuaJIT. arch follows the build (GMod's 64-bit branch reports
-- "x64"; addons such as G64 refuse to run when they see "x86").
local is64Bit = string.packsize("T") == 8

jit = {
	opt = function() end,
	status = function() return false end,
	version = function() return "Lua 5.4" end,
	versionnum = 0,
	arch = is64Bit and "x64" or "x86",
	os = "Windows",
}

-- GMod's branch name; the 64-bit build is "x86-64"
BRANCH = BRANCH or (is64Bit and "x86-64" or "unknown")

-- GMod's name of the active gamemode (its folder). Addons branch on it, e.g. the
-- playermodel selector only keeps the sandbox desktop window settings (width,
-- height) when GAMEMODE_NAME == "sandbox"; without it the context menu errors.
if (GAMEMODE_NAME == nil) then
	local gamemodeConVar = GetConVar_Internal("gamemode")

	if (gamemodeConVar) then
		GAMEMODE_NAME = gamemodeConVar:GetString()
	end

	-- gmod_minimal is Srcbox's thin wrapper around sandbox; addons should see it as sandbox
	-- (e.g. the playermodel selector only edits the existing sandbox window entry then).
	if (GAMEMODE_NAME == "gmod_minimal") then
		GAMEMODE_NAME = "sandbox"
	end
end

-- Desktop window entries without a size (addons that replace the entry outside sandbox only set
-- title/icon/init) made the context menu's DoClick call SetSize(nil). Give them sandbox's default.
if (CLIENT) then
	hook.Add("ContextMenuOpen", "GModCompatibility.DesktopWindowDefaults", function()
		local desktopWindows = list.GetForEdit and list.GetForEdit("DesktopWindows")

		if (not istable(desktopWindows)) then
			return
		end

		for _, window in pairs(desktopWindows) do
			if (istable(window)) then
				window.width = window.width or 960
				window.height = window.height or 700
			end
		end
	end)
end

local registry = debug.getregistry()
function FindMetaTable(name)
	if (name == "Vehicle") then
		-- We don't have vehicles in Experiment, so lets not waste time on it
		return {}
	elseif (name == "NPC" or name == "NextBot") then
		-- Experiment has no metatable of its own for these. Addons add methods
		-- to them (CPTBase: NPC_Meta:CPT_UpdateNPCFaction), so they must exist and
		-- stay the same table. NPC entities look in the "NPC" one (Entity __index,
		-- lbaseentity_shared.cpp); anything else falls through to Entity's.
		local meta = registry[name]
		if (not meta) then
			meta = { MetaName = name }
			setmetatable(meta, { __index = registry.Entity })
			registry[name] = meta
		end
		return meta
	elseif (name == "IMaterial") then
		name = "Material"
	elseif (name == "ITexture") then
		name = "Texture"
	elseif (name == "IGModAudioChannel") then
		name = "AudioChannel"
	elseif (name == "CEffectData") then
		name = "EffectData"
	elseif (name == "CMoveData") then
		name = "MoveData"
	elseif (name == "CRecipientFilter") then
		name = "RecipientFilter"
	elseif (name == "CTakeDamageInfo") then
		name = "TakeDamageInfo"
	elseif (name == "CUserCmd") then
		name = "UserCommand"
	elseif (name == "bf_read") then
		name = "UserMessageReader"
	elseif (name == "VMatrix") then
		name = "Matrix"
	elseif (name == "PhysObj") then
		name = "PhysicsObject"
		-- elseif (name == "PhysCollide") then
		-- 	name = "PhysicsCollide"
	end

	return registry[name]
end

function RegisterMetaTable(name, table)
	-- TODO: What should MetaName and MetaID be set to to match GMod behavior?
	table.MetaName = name
	table.MetaID = name
	registry[name] = table
end

-- TODO: Actually implement SQLite
sql = {
	Begin = function() end,
	Commit = function() end,
	IndexExists = function() end,
	LastError = function() end,
	Query = function() end,
	QueryRow = function() end,
	QueryValue = function() end,
	SQLStr = function() end,
	TableExists = function() end,
}

cvars = ConsoleVariables
engine = Engines
input = Inputs
render = Renders
resource = Resources
surface = Surfaces
system = Systems
vgui = Panels
concommand = ConsoleCommands
physenv = PhysicsEnvironments
gameevent = GameEvents
sound = Sounds
debugoverlay = DebugOverlays

Angle = Angles.Create
Color = Colors.Create
ClientsideModel = Entities.CreateClientEntity
Entity = Entities.Find
Vector = Vectors.Create
-- GMod: Matrix() is the identity matrix; Matrix(tbl) takes 4 rows, Matrix(m) copies
Matrix = function(...)
	if (select("#", ...) == 0) then
		return Matrixes.CreateIdentityMatrix()
	end

	return Matrixes.Create(...)
end
HSVToColor = Colors.HsvToColor
ColorToHSV = Colors.ColorToHsv
ColorToHSL = Colors.ColorToHsl
HslToColor = Colors.HslToColor

SetClipboardText = Systems.SetClipboardText

RealFrameTime = Engines.GetAbsoluteFrameTime
CurTime = Engines.GetCurrentTime
UnPredictedCurTime = Engines.GetCurrentTime -- TODO: Use actually un-predicted time
SysTime = Engines.GetSystemTime
VGUIFrameTime = Engines.GetSystemTime
RealTime = Engines.GetRealTime
FrameNumber = Engines.GetFrameCount
FrameTime = Engines.GetFrameTime
engine.TickCount = Engines.GetTickCount
engine.TickInterval = Engines.GetIntervalPerTick
SoundDuration = Engines.GetSoundDuration
GetHostName = Engines.GetServerName
Player = Players.FindByUserId
EmitSound = Sounds.Play

PrecacheParticleSystem = ParticleSystems.Precache
GetPredictionPlayer = Predictions.GetPredictionPlayer
IsFirstTimePredicted = Predictions.IsFirstTimePredicted
LerpAngle = Angles.Lerp
LerpVector = Vectors.Lerp

RecipientFilter = RecipientFilters.Create
EffectData = Effects.Create
util.Effect = Effects.Dispatch

system.AppTime = Systems.GetSecondsSinceAppActive
system.IsOSX = Systems.IsOsx
system.SteamTime = Systems.GetSteamServerRealTime
system.UpTime = Systems.GetSecondsSinceComputerActive

if (SERVER) then
	ai = EnemyAi
	ai.GetScheduleID = EnemyAi.GetScheduleId
	ai.GetTaskID = EnemyAi.GetTaskId
end

local function convertPlaySoundFlags(flagsAsString)
	local flags = _E.PLAY_SOUND_FLAG.STREAM_BLOCK

	if (not flagsAsString or type(flagsAsString) ~= "string") then
		return flags
	end

	for flag in flagsAsString:gmatch("%S+") do
		if (flag == "3d") then
			flags = bit.bor(flags, _E.PLAY_SOUND_FLAG.SAMPLE_3D)
		elseif (flag == "mono") then
			flags = bit.bor(flags, _E.PLAY_SOUND_FLAG.SAMPLE_MONO)
		elseif (flag == "noplay") then
			flags = bit.bor(flags, _E.PLAY_SOUND_FLAG.DONT_PLAY)
		elseif (flag == "noblock") then
			-- Remove the STREAM_BLOCK flag
			flags = bit.band(flags, bit.bnot(_E.PLAY_SOUND_FLAG.STREAM_BLOCK))
		end
	end
end

sound.PlayURL = function(path, flagsAsString, callback)
	return Sounds.PlayUrl(path, convertPlaySoundFlags(flagsAsString), callback)
end
local oldPlayFile = Sounds.PlayFile
sound.PlayFile = function(path, flagsAsString, callback)
	return oldPlayFile(path, convertPlaySoundFlags(flagsAsString), callback)
end

local originalConCommandAdd = concommand.Add

function concommand.Add(command, callback, autoCompleteHandler, helpText, flags)
	if (istable(flags)) then
		if (#flags == 0) then
			flags = 0
		elseif (#flags == 1) then
			flags = flags[1]
		else
			flags = bit.bor(table.unpack(flags))
		end
	end

	return originalConCommandAdd(command, callback, helpText, flags)
end

engine.ActiveGamemode = function()
	return Gamemodes.GetActiveName()
end

engine.GetGames = function()
	local games = Engines.GetMountableGames()
	local result = {}

	for i, game in ipairs(games) do
		result[i] = {
			depot = game.appId,
			title = game.name,
			folder = game.directoryName,

			owned = game.isOwned,
			mounted = game.isMounted,
			installed = game.isInstalled,
		}
	end

	return result
end

engine.GetAddons = function() return {} end    -- TODO: Implement with our addon system
engine.GetGamemodes = function() return {} end -- TODO: Implement with our gamemode system
engine.GetUserContent = function() return {} end

gmod = {
	GetGamemode = function()
		return _G.GAMEMODE
	end,
}

language = {
	GetPhrase = function(phrase)
		return Localizations.Find(phrase)
	end,

	Add = function(key, value)
		Localizations.AddString(key, value)
	end,
}

function MsgAll(...)
	local message = table.concat({ ... }, " ")
	Players.ShowMessageToAll(message)
end

local basePath = "resource/localization/en/"
local languageFiles = file.Find(basePath .. "*.properties", "GAME")

for _, languageFile in ipairs(languageFiles) do
	local fileContent = file.Read(basePath .. languageFile, "GAME")

	if (fileContent == nil) then
		continue
	end

	for line in fileContent:gmatch("[^\r\n]+") do
		-- Skip comment lines or empty lines
		if (line:match("^%s*#") or line:match("^%s*$")) then
			continue
		end

		local key, value = line:match("([^=]+)=(.*)")
		value = value:gsub("\\(.)", "%1")

		language.Add(key, value)
	end
end

function physenv.AddSurfaceData(surfaceDataKeyValuesString)
	-- ParseSurfaceData expects a unique file name, so we generate one
	local fakeFileName = "physenv.AddSurfaceData" .. tostring(os.time()) .. ".txt"

	return PhysicsSurfaceProperties.ParseSurfaceData(fakeFileName, surfaceDataKeyValuesString)
end

local MESSAGE_READER_META = FindMetaTable("MessageReader")

function MESSAGE_READER_META:ReadInt(bitCount)
	return self:ReadBitLong(bitCount, true)
end

function MESSAGE_READER_META:ReadUInt(bitCount)
	return self:ReadBitLong(bitCount, false)
end

local MESSAGE_WRITER_META = FindMetaTable("MessageWriter")

function MESSAGE_WRITER_META:WriteInt(value, bitCount)
	return self:WriteBitLong(value, bitCount, true)
end

function MESSAGE_WRITER_META:WriteUInt(value, bitCount)
	return self:WriteBitLong(value, bitCount, false)
end

umsg = require("UserMessages")
umsg.Angle = umsg.WriteAngle or umsg.WriteAngles
umsg.End = umsg.End or umsg.MessageEnd
umsg.Bool = umsg.WriteBool
umsg.Char = umsg.WriteChar
umsg.Entity = umsg.WriteEntity
umsg.Float = umsg.WriteFloat
umsg.Long = umsg.WriteLong
umsg.Short = umsg.WriteShort
umsg.String = umsg.WriteString
umsg.Vector = umsg.WriteVector
umsg.VectorNormal = umsg.WriteNormal
umsg.Send = umsg.MessageEnd

unpack = table.unpack

-- Have table.insert return the position where the value was inserted
table._OriginalInsert = table._OriginalInsert or table.insert

---@overload fun(list: table, value: any)
---@param list table
---@param positionOrValue integer|any
---@param value? any|nil
---@return integer
---@diagnostic disable-next-line: duplicate-set-field
function table.insert(list, positionOrValue, value)
	if (not value) then
		table._OriginalInsert(list, positionOrValue)
		return #list
	end

	table._OriginalInsert(list, positionOrValue, value)

	return positionOrValue
end

CreateConVar = function(name, value, flags, helpText, min, max)
	if (istable(flags)) then
		if (#flags == 0) then
			flags = 0
		elseif (#flags == 1) then
			flags = flags[1]
		else
			flags = bit.bor(table.unpack(flags))
		end
	end

	return ConsoleVariables.Create(name, value, flags, helpText, min, max)
end

AddCSLuaFile = SendFile or function() end

TauntCamera = function()
	return {
		ShouldDrawLocalPlayer = function() return false end,
		CreateMove = function() end,
		CalcView = function() end,
	}
end

-- We don't use the workshop
WorkshopFileBase = function(namespace, requiredTags)
	return {}
end

HTTP = function(httpRequest)
	local method = httpRequest.method:upper()
	local body = httpRequest.body
	local parameters = httpRequest.parameters
	local headers = httpRequest.headers
	local contentType = httpRequest.contentType
	local timeout = httpRequest.timeout

	local successCallback = httpRequest.success
	local failureCallback = httpRequest.failed

	if (parameters) then
		parameters = KeyValues.CreateFromTable("parameters", parameters)
	end

	if (headers) then
		headers = KeyValues.CreateFromTable("headers", headers)
	end

	return WebConnections.RequestHttpMethod(
		httpRequest.url,
		method,
		body,
		parameters,
		headers,
		contentType,
		timeout,
		successCallback,
		failureCallback)
end

game = {
	IsDedicated = function()
		return Engines.IsDedicatedServer()
	end,

	SinglePlayer = function()
		return Engines.GetMaxClients() == 1
	end,

	MaxPlayers = Engines.GetMaxClients,

	ConsoleCommand = RunConsoleCommand,

	AddParticles = function(filePath)
		-- Remove particles/ from the start of the file path
		filePath = filePath:sub(11)

		return ParticleSystems.ReadConfigFile("particles/" .. tostring(filePath))
	end,

	-- GMod returns just the map name; the client's engine gives "maps/<name>.bsp"
	GetMap = function()
		local levelName = tostring(Engines.GetLevelName() or ""):gsub("\\", "/")
		levelName = levelName:match("([^/]+)$") or levelName
		return (levelName:gsub("%.bsp$", ""))
	end,

	-- Newer sandbox code (spawnmenu/toolpanel.lua) uses this; the world is always entity index 0
	GetWorld = function()
		return Entity(0)
	end,

	-- GMod: registers a decal name for util.Decal, backed by one or more materials
	-- (one is picked at random each time)
	AddDecal = function(decalName, materials)
		if (type(decalName) ~= "string") then
			return
		end

		if (type(materials) == "string") then
			materials = { materials }
		end

		if (type(materials) ~= "table" or #materials == 0) then
			return
		end

		local list = {}

		for _, material in ipairs(materials) do
			if (type(material) == "string" and material ~= "") then
				list[#list + 1] = material
			end
		end

		if (#list > 0) then
			GModCompatibilityCustomDecals[decalName:lower()] = list
		end
	end,
}

GModCompatibilityCustomDecals = GModCompatibilityCustomDecals or {}

do
	-- util is the native Utilities table, so keep the native decal functions before
	-- the GMod ones replace them
	local nativeDecal = rawget(Utilities, "_SrcboxDecal") or Utilities.Decal
	local nativeDecalMaterial = rawget(Utilities, "_SrcboxDecalMaterial") or Utilities.DecalMaterial
	Utilities._SrcboxDecal = nativeDecal
	Utilities._SrcboxDecalMaterial = nativeDecalMaterial
	nativeDecal = nativeDecal or function() end
	nativeDecalMaterial = nativeDecalMaterial or function() end

	-- util.Decal's filter can be an entity or a table of them; we can ignore one
	local function firstEntity(filter)
		if (type(filter) == "table") then
			for _, entity in ipairs(filter) do
				if (IsValid(entity)) then
					return entity
				end
			end

			return nil
		end

		if (filter ~= nil and IsValid(filter)) then
			return filter
		end

		return nil
	end

	local function customDecalMaterial(name)
		local materials = GModCompatibilityCustomDecals[tostring(name):lower()]

		if (materials) then
			return materials[math.random(1, #materials)]
		end
	end

	util.DecalMaterial = function(name)
		return customDecalMaterial(name) or name
	end

	util.Decal = function(name, startPos, endPos, filter)
		if (type(name) ~= "string" or not isvector(startPos) or not isvector(endPos)) then
			return
		end

		local ignore = firstEntity(filter)
		local material = customDecalMaterial(name)

		if (material) then
			nativeDecalMaterial(material, startPos, endPos, ignore)
		elseif (nativeDecal) then
			nativeDecal(name, startPos, endPos, ignore)
		end
	end

	-- Size/color can't be controlled with engine decals; the material is placed as is
	util.DecalEx = function(material, entity, position, normal, color, width, height)
		if (not isvector(position) or not isvector(normal)) then
			return
		end

		local materialName = type(material) == "string" and material or (material and material.GetName and material:GetName())

		if (not materialName) then
			return
		end

		nativeDecalMaterial(materialName, position + normal * 4, position - normal * 4)
	end
end

BOUNDS_COLLISION = BOUNDS_COLLISION or 0
BOUNDS_HITBOXES = BOUNDS_HITBOXES or 2

debugoverlay.Axis = function(origin, angle, size, lifetime, ignoreZ)
	DebugOverlays.AddLine(origin, origin + angle:Forward() * size, lifetime, Color(0, 255, 0), ignoreZ)
	DebugOverlays.AddLine(origin, origin + angle:Right() * size, lifetime, Color(255, 0, 0), ignoreZ)
	DebugOverlays.AddLine(origin, origin + angle:Up() * size, lifetime, Color(0, 0, 255), ignoreZ)
end
debugoverlay.Box = function(origin, mins, maxs, lifetime, color)
	DebugOverlays.AddBox(origin, mins, maxs, Angle(), color, lifetime)
end
debugoverlay.BoxAngles = DebugOverlays.AddBox
debugoverlay.Cross = function(origin, size, lifetime, color, ignoreZ)
	DebugOverlays.AddLine(origin + Vector(size, 0, 0), origin - Vector(size, 0, 0), lifetime, color, ignoreZ)
	DebugOverlays.AddLine(origin + Vector(0, size, 0), origin - Vector(0, size, 0), lifetime, color, ignoreZ)
	DebugOverlays.AddLine(origin + Vector(0, 0, size), origin - Vector(0, 0, size), lifetime, color, ignoreZ)
end
debugoverlay.EntityTextAtPosition = DebugOverlays.AddEntityTextOverlay
debugoverlay.Grid = DebugOverlays.AddGrid
debugoverlay.Line = DebugOverlays.AddLine
debugoverlay.ScreenText = DebugOverlays.AddScreenText
-- debugoverlay.Sphere = DebugOverlays.AddSphere -- not yet implemented
debugoverlay.SweptBox = DebugOverlays.AddSweptBox
debugoverlay.Text = DebugOverlays.AddText
debugoverlay.Triangle = DebugOverlays.AddTriangle

local MATRIX_META = FindMetaTable("Matrix")

--[[
	GMod's VMatrix API on top of the native Matrix.
	GMod's methods change the matrix in place, so the Srcbox ones that return a
	new matrix (Scale) are wrapped here.
--]]
do
	local M = MATRIX_META
	local nativeScale = rawget(M, "_SrcboxScale") or M.Scale
	local nativeMul = rawget(M, "_SrcboxMul") or M.__mul
	M._SrcboxScale = nativeScale
	M._SrcboxMul = nativeMul

	local function rotationMatrix(ang)
		local rotation = Matrixes.CreateIdentityMatrix()
		rotation:SetupMatrixForOriginAndAngles(Vector(0, 0, 0), ang)
		return rotation
	end

	-- GMod's Translate is M = M * T(v) (moves along the matrix's own axes)
	M.Translate = M.PreTranslate

	function M:Set(other)
		self:CopyFrom(other)
	end

	function M:ToTable()
		local result = {}

		for row = 1, 4 do
			result[row] = {
				self:GetField(row, 1),
				self:GetField(row, 2),
				self:GetField(row, 3),
				self:GetField(row, 4),
			}
		end

		return result
	end

	function M:Unpack()
		return self:GetField(1, 1), self:GetField(1, 2), self:GetField(1, 3), self:GetField(1, 4),
			self:GetField(2, 1), self:GetField(2, 2), self:GetField(2, 3), self:GetField(2, 4),
			self:GetField(3, 1), self:GetField(3, 2), self:GetField(3, 3), self:GetField(3, 4),
			self:GetField(4, 1), self:GetField(4, 2), self:GetField(4, 3), self:GetField(4, 4)
	end

	function M:SetUnpacked(...)
		local values = { ... }

		for i = 1, 16 do
			local row = math.floor((i - 1) / 4) + 1
			local column = (i - 1) % 4 + 1
			self:SetField(row, column, values[i] or 0)
		end
	end

	function M:Zero()
		for row = 1, 4 do
			for column = 1, 4 do
				self:SetField(row, column, 0)
			end
		end
	end

	function M:IsZero()
		for row = 1, 4 do
			for column = 1, 4 do
				if (self:GetField(row, column) ~= 0) then
					return false
				end
			end
		end

		return true
	end

	function M:GetRight()
		return self:GetLeft() * -1
	end

	function M:SetRight(right)
		self:SetLeft(right * -1)
	end

	function M:GetAngles()
		local forward = Vector(self:GetField(1, 1), self:GetField(2, 1), self:GetField(3, 1))
		local up = Vector(self:GetField(1, 3), self:GetField(2, 3), self:GetField(3, 3))
		forward:NormalizeInPlace()
		up:NormalizeInPlace()

		return forward:AngleWithUp(up)
	end

	function M:SetAngles(ang)
		local rotation = rotationMatrix(ang)

		for row = 1, 3 do
			for column = 1, 3 do
				self:SetField(row, column, rotation:GetField(row, column))
			end
		end
	end

	function M:Rotate(ang)
		self:CopyFrom(nativeMul(self, rotationMatrix(ang)))
	end

	function M:Scale(scale)
		self:CopyFrom(nativeScale(self, scale))
		return self
	end

	function M:ScaleTranslation(scale)
		self:SetTranslation(self:GetTranslation() * scale)
	end

	function M:SetScale(scale)
		local axes = { scale.x, scale.y, scale.z }

		for column = 1, 3 do
			local x, y, z = self:GetField(1, column), self:GetField(2, column), self:GetField(3, column)
			local length = math.sqrt(x * x + y * y + z * z)

			if (length > 0) then
				local factor = axes[column] / length
				self:SetField(1, column, x * factor)
				self:SetField(2, column, y * factor)
				self:SetField(3, column, z * factor)
			end
		end
	end

	function M:Invert()
		local inverse = Matrixes.CreateIdentityMatrix()

		if (not self:InverseGeneral(inverse)) then
			return false
		end

		self:CopyFrom(inverse)
		return true
	end

	function M:GetInverse()
		local inverse = Matrixes.CreateIdentityMatrix()

		if (not self:InverseGeneral(inverse)) then
			return nil
		end

		return inverse
	end

	function M:InvertTR()
		self:CopyFrom(self:InverseTranslation())
	end

	function M:GetInverseTR()
		return self:InverseTranslation()
	end

	-- GMod: matrix * vector transforms the vector
	M.__mul = function(a, b)
		if (isvector(b)) then
			return a:MultiplyByVector4x3(b)
		end

		return nativeMul(a, b)
	end
end

local EFFECT_DATA_META = FindMetaTable("EffectData")
EFFECT_DATA_META.GetEntIndex = EFFECT_DATA_META.GetEntityIndex
EFFECT_DATA_META.SetEntIndex = EFFECT_DATA_META.SetEntityIndex
EFFECT_DATA_META.GetAttachment = EFFECT_DATA_META.GetAttachmentIndex
EFFECT_DATA_META.SetAttachment = EFFECT_DATA_META.SetAttachmentIndex

local USER_COMMAND_META = FindMetaTable("UserCommand")

function USER_COMMAND_META:IsForced()
	return not self:IsNew()
end

local VECTOR_META = FindMetaTable("Vector")
VECTOR_META.AngleEx = VECTOR_META.AngleWithUp
VECTOR_META.Distance = VECTOR_META.DistanceTo
VECTOR_META.DistToSqr = VECTOR_META.DistanceToAsSqr
VECTOR_META.Distance2D = VECTOR_META.DistanceTo2D
VECTOR_META.Distance2DSqr = VECTOR_META.DistanceToAsSqr2D
VECTOR_META.Div = VECTOR_META.Divide
VECTOR_META.Mul = VECTOR_META.Scale
VECTOR_META.Normalize = VECTOR_META.NormalizeInPlace
VECTOR_META.SetUnpacked = VECTOR_META.Initialize

function VECTOR_META:Set(vectorToCopy)
	self:Initialize(vectorToCopy.x, vectorToCopy.y, vectorToCopy.z)
end

function VECTOR_META:GetNormalized()
	local copy = Vector(self)
	copy:NormalizeInPlace()
	return copy
end

local ANGLE_META = FindMetaTable("Angle")
ANGLE_META.Div = ANGLE_META.Divide
ANGLE_META.Mul = ANGLE_META.Scale
ANGLE_META.SetUnpacked = ANGLE_META.Initialize

function ANGLE_META:Set(angleToCopy)
	self:Initialize(angleToCopy.p, angleToCopy.y, angleToCopy.r)
end

local ENTITY_META = FindMetaTable("Entity")
ENTITY_META.EntIndex = ENTITY_META.GetEntityIndex
ENTITY_META.Health = ENTITY_META.GetHealth
ENTITY_META.GetPos = ENTITY_META.GetPosition
ENTITY_META.SetPos = ENTITY_META.SetPosition
ENTITY_META.EyePos = ENTITY_META.GetEyePosition
ENTITY_META.EyeAngles = ENTITY_META.GetEyeAngles
-- Keep the native Entity:GetModel (works on every entity) when the build has it
ENTITY_META.GetModel = rawget(ENTITY_META, "GetModel") or ENTITY_META.GetModelName
-- Animating entities have these natively; GMod lets any entity call them
ENTITY_META.SetModelScale = rawget(ENTITY_META, "SetModelScale") or function() end
ENTITY_META.GetModelScale = rawget(ENTITY_META, "GetModelScale") or function() return 1 end
ENTITY_META.GetTable = ENTITY_META.GetRefTable
ENTITY_META.NearestPoint = ENTITY_META.CalculateNearestPoint
ENTITY_META.OBBCenter = ENTITY_META.GetOBBCenter
ENTITY_META.OBBMaxs = ENTITY_META.GetOBBMaxs
ENTITY_META.OBBMins = ENTITY_META.GetOBBMins
ENTITY_META.LocalToWorld = ENTITY_META.EntityToWorldSpace
ENTITY_META.SkinCount = ENTITY_META.GetSkinCount
ENTITY_META.Alive = ENTITY_META.IsAlive
ENTITY_META.IsNPC = ENTITY_META.IsNpc
ENTITY_META.Widget = false
ENTITY_META.AddFlags = ENTITY_META.AddFlag
ENTITY_META.RemoveFlags = ENTITY_META.RemoveFlag
ENTITY_META.GetFlexNum = ENTITY_META.GetFlexCount
ENTITY_META.SetOwner = ENTITY_META.SetOwnerEntity
ENTITY_META.GetOwner = ENTITY_META.GetOwnerEntity
ENTITY_META.DeleteOnRemove = ENTITY_META.AddDeleteOnRemove
ENTITY_META.DontDeleteOnRemove = ENTITY_META.RemoveDeleteOnRemove
ENTITY_META.GetFlexIDByName = ENTITY_META.GetFlexIdByName
ENTITY_META.GetNumBodyGroups = ENTITY_META.GetBodyGroupsCount
ENTITY_META.WaterLevel = ENTITY_META.GetWaterLevel
ENTITY_META.SetMaterial = ENTITY_META.SetMaterialOverride
ENTITY_META.GetMaterial = ENTITY_META.GetMaterialOverride
ENTITY_META.SetSubMaterial = ENTITY_META.SetSubMaterialOverride
ENTITY_META.GetSubMaterial = ENTITY_META.GetSubMaterialOverride
ENTITY_META.GetAbsVelocity = ENTITY_META.GetLocalVelocity
ENTITY_META.SetAbsVelocity = ENTITY_META.SetLocalVelocity
ENTITY_META.Team = ENTITY_META.GetTeamNumber
ENTITY_META.GetNoCollideWithTeammates = ENTITY_META.GetNoCollidingWithTeammates
ENTITY_META.SetNoCollideWithTeammates = ENTITY_META.SetNoCollidingWithTeammates
ENTITY_META.SetColor = ENTITY_META.SetRenderColor
ENTITY_META.GetColor = ENTITY_META.GetRenderColor
ENTITY_META.GetSequenceList = ENTITY_META.GetSequences
ENTITY_META.NextThink = ENTITY_META.SetNextThink
ENTITY_META.PhysicsDestroy = ENTITY_META.DestroyPhysicsObject

function ENTITY_META:SetColor4Part(r, g, b, a)
	self:SetRenderColor(Color(r, g, b, a))
end

function ENTITY_META:GetColor4Part()
	local color = self:GetRenderColor()

	return color.r, color.g, color.b, color.a
end

function ENTITY_META:GetForward()
	local forward, _, _ = self:GetVectors()
	return forward
end

function ENTITY_META:GetRight()
	local _, right, _ = self:GetVectors()
	return right
end

function ENTITY_META:GetUp()
	local _, _, up = self:GetVectors()
	return up
end

function ENTITY_META:GetDTAngle(index)
	return self:GetNetworkDataValue(_E.NETWORK_VARIABLE_TYPE.ANGLE, index)
end

function ENTITY_META:GetDTBool(index)
	return self:GetNetworkDataValue(_E.NETWORK_VARIABLE_TYPE.BOOLEAN, index)
end

function ENTITY_META:GetDTEntity(index)
	return self:GetNetworkDataValue(_E.NETWORK_VARIABLE_TYPE.ENTITY, index)
end

function ENTITY_META:GetDTFloat(index)
	return self:GetNetworkDataValue(_E.NETWORK_VARIABLE_TYPE.FLOAT, index)
end

function ENTITY_META:GetDTInt(index)
	return self:GetNetworkDataValue(_E.NETWORK_VARIABLE_TYPE.INTEGER, index)
end

function ENTITY_META:GetDTString(index)
	return self:GetNetworkDataValue(_E.NETWORK_VARIABLE_TYPE.STRING, index)
end

function ENTITY_META:GetDTVector(index)
	return self:GetNetworkDataValue(_E.NETWORK_VARIABLE_TYPE.VECTOR, index)
end

function ENTITY_META:SetDTAngle(index, value)
	self:SetNetworkDataValue(_E.NETWORK_VARIABLE_TYPE.ANGLE, index, value)
end

function ENTITY_META:SetDTBool(index, value)
	self:SetNetworkDataValue(_E.NETWORK_VARIABLE_TYPE.BOOLEAN, index, value)
end

function ENTITY_META:SetDTEntity(index, value)
	self:SetNetworkDataValue(_E.NETWORK_VARIABLE_TYPE.ENTITY, index, value)
end

function ENTITY_META:SetDTFloat(index, value)
	self:SetNetworkDataValue(_E.NETWORK_VARIABLE_TYPE.FLOAT, index, value)
end

function ENTITY_META:SetDTInt(index, value)
	self:SetNetworkDataValue(_E.NETWORK_VARIABLE_TYPE.INTEGER, index, value)
end

function ENTITY_META:SetDTString(index, value)
	self:SetNetworkDataValue(_E.NETWORK_VARIABLE_TYPE.STRING, index, value)
end

function ENTITY_META:SetDTVector(index, value)
	self:SetNetworkDataValue(_E.NETWORK_VARIABLE_TYPE.VECTOR, index, value)
end

function ENTITY_META:IsNextBot()
	return false -- TODO: Implement (low priority)
end

function ENTITY_META:SetSpawnEffect(effect)
	-- TODO: Implement
	self.__spawnEffect = effect
end

function ENTITY_META:GetSpawnEffect()
	-- TODO: Implement
	return self.__spawnEffect
end

function ENTITY_META:SetPersistent(persistent)
	-- TODO: Implement
	self.__persistent = persistent
end

function ENTITY_META:GetPersistent()
	-- TODO: Implement
	return self.__persistent
end

function ENTITY_META:SetNoDraw(bBool)
	if (bBool) then
		self:AddEffects(_E.ENTITY_EFFECT.NO_DRAW)
	else
		self:RemoveEffects(_E.ENTITY_EFFECT.NO_DRAW)
	end
end

function ENTITY_META:GetNoDraw()
	return self:IsEffectActive(_E.ENTITY_EFFECT.NO_DRAW)
end

function ENTITY_META:IsFlagSet(flag)
	return self:GetFlags() & flag ~= 0
end

function ENTITY_META:IsOnGround()
	return self:IsFlagSet(_E.ENGINE_FLAG.ON_GROUND)
end

ENTITY_META.OnGround = ENTITY_META.IsOnGround

-- TODO: Are GetPhysicsObjectNum and GetPhysicsObjectCount correctly implemented?
function ENTITY_META:GetPhysicsObjectCount()
	return select(1, self:GetPhysicsObjects())
end

function ENTITY_META:GetPhysicsObjectNum(physicsObjectNumber)
	local physicsObjects = select(2, self:GetPhysicsObjects())

	return physicsObjects[physicsObjectNumber + 1]
end

--[[
	We implement SetNotSolid and DrawShadow using engine flags/effects, but I'm not sure
	if that's the way gmod does it. If it isn't, these flags may be unexpectedly set by
	some scripts.
	TODO: Check if DrawShadow/SetNotSolid on gmod, causes any flags to be set.
--]]
function ENTITY_META:SetNotSolid(bBool)
	if (bBool) then
		self:AddSolidFlags(SOLID_NONE)
	else
		print("ENTITY_META:SetNotSolid - Not sure if SOLID_VPHYSICS is correct for SetNotSolid(false)")
		self:RemoveSolidFlags(SOLID_VPHYSICS)
	end
end

function ENTITY_META:DrawShadow(bBool)
	if (bBool) then
		self:RemoveEffects(_E.ENTITY_EFFECT.NO_SHADOW)
	else
		self:AddEffects(_E.ENTITY_EFFECT.NO_SHADOW)
	end
end

-- TODO: Actually implement these bone manipulation functions
-- TODO: We should probably override SetupBones, call the baseclass, and then apply our manipulations?
-- TODO: Or we need to use a function from bone_setup.h. I'm not sure which one.
function ENTITY_META:GetManipulateBoneAngles()
	return Angle()
end

function ENTITY_META:GetManipulateBoneJiggle()
	return 0
end

function ENTITY_META:GetManipulateBonePosition()
	return Vector()
end

function ENTITY_META:GetManipulateBoneScale()
	return Vector(1, 1, 1)
end

function ENTITY_META:ManipulateBoneAngles(boneID, angle, isNetworked)
end

function ENTITY_META:ManipulateBoneJiggle(boneID, jiggle)
end

function ENTITY_META:ManipulateBonePosition(boneID, position, isNetworked)
end

function ENTITY_META:ManipulateBoneScale(boneID, scale, isNetworked)
end

function ENTITY_META:HasBoneManipulations()
	return false
end

if (SERVER) then
	function ENTITY_META:FrameAdvance()
		-- TODO: Should this be implemented server-side? I can't find it in the engine.
	end

	function ENTITY_META:IsPlayerHolding()
		return self:GetPhysicsObject():GetGameFlags() == _E.FVPHYSICS.PLAYER_HELD
	end
end

local PHYSICS_OBJECT_META = FindMetaTable("PhysicsObject")
PHYSICS_OBJECT_META.SetAngleVelocity = PHYSICS_OBJECT_META.SetAngularVelocity
PHYSICS_OBJECT_META.SetAngularVelocityInstantaneous = PHYSICS_OBJECT_META.SetAngularVelocityInstantaneous
PHYSICS_OBJECT_META.GetAABB = PHYSICS_OBJECT_META.GetAabb

function PHYSICS_OBJECT_META:GetPos()
	return select(1, self:GetPosition())
end

function PHYSICS_OBJECT_META:GetAngles()
	return select(2, self:GetPosition())
end

-- GMod's PhysObj:SetPos / SetAngles (the engine binding is PhysicsObject:SetPosition(pos, ang, teleport)).
-- Without these, HAT silently skipped posing props (gQuery ignores missing methods).
if (PHYSICS_OBJECT_META.SetPosition) then
	function PHYSICS_OBJECT_META:SetPos(position, teleport)
		local _, angles = self:GetPosition()
		self:SetPosition(position, angles, teleport or false)
	end

	function PHYSICS_OBJECT_META:SetAngles(angles)
		local position = self:GetPosition()
		self:SetPosition(position, angles, false)
	end
end

local WEAPON_META = FindMetaTable("Weapon")
-- GMod's (engine) SetHoldType stores the hold type and calls the SWEP's SetWeaponHoldType, which builds the
-- ActivityTranslate table from weapon_base/sh_anim.lua. SetAnimationPrefix alone does nothing (its C++ body is stubbed).
function WEAPON_META:SetHoldType(holdType)
	self.m_strGModHoldType = holdType

	if (self.SetAnimationPrefix) then
		self:SetAnimationPrefix(holdType)
	end

	if (isfunction(self.SetWeaponHoldType)) then
		self:SetWeaponHoldType(holdType)
	end
end

function WEAPON_META:GetHoldType()
	return self.m_strGModHoldType or self.HoldType or "normal"
end
WEAPON_META.Ammo1 = WEAPON_META.GetPrimaryAmmoCount
WEAPON_META.Ammo2 = WEAPON_META.GetSecondaryAmmoCount
WEAPON_META.SendWeaponAnim = WEAPON_META.SendWeaponAnimation

local PLAYER_META = FindMetaTable("Player")

-- GMod's Kinect API. There is no motion sensor support, so report it as unavailable. Without this,
-- properties/kinect_controller.lua's Filter errored and aborted building the right-click menu before
-- menu:Open() ran, leaving the half-built menu in the top-left corner.
motionsensor = motionsensor or {}
motionsensor.IsAvailable = motionsensor.IsAvailable or function() return false end
motionsensor.IsActive = motionsensor.IsActive or function() return false end
motionsensor.Start = motionsensor.Start or function() return false end
motionsensor.Stop = motionsensor.Stop or function() end
motionsensor.GetSkeleton = motionsensor.GetSkeleton or function() return nil end
motionsensor.GetColourMaterial = motionsensor.GetColourMaterial or function() return nil end

-- GMod's drive system (properties/drive.lua, includes/modules/drive.lua). The engine side of
-- driving isn't implemented, so this only tracks state per realm so the scripts don't error.
if (not PLAYER_META.GetDrivingEntity) then
	function PLAYER_META:GetDrivingEntity()
		local ent = rawget(self:GetTable() or {}, "__gmodDrivingEntity")
		return IsValid(ent) and ent or NULL
	end

	function PLAYER_META:GetDrivingMode()
		return rawget(self:GetTable() or {}, "__gmodDrivingMode") or 0
	end

	function PLAYER_META:IsDrivingEntity(ent)
		local driving = self:GetDrivingEntity()

		if (ent == nil) then
			return IsValid(driving)
		end

		return IsValid(driving) and driving == ent
	end

	function PLAYER_META:SetDrivingEntity(ent, mode)
		local tab = self:GetTable()

		if (not tab) then
			return
		end

		if (IsValid(ent)) then
			tab.__gmodDrivingEntity = ent
			tab.__gmodDrivingMode = mode or 0
		else
			tab.__gmodDrivingEntity = nil
			tab.__gmodDrivingMode = nil
		end
	end
end
PLAYER_META.GetShootPos = PLAYER_META.GetWeaponShootPosition
PLAYER_META.UserID = PLAYER_META.GetUserId
PLAYER_META.AccountID = PLAYER_META.GetAccountId
PLAYER_META.SteamID = PLAYER_META.GetSteamId
PLAYER_META.SteamID64 = PLAYER_META.GetSteamId64
PLAYER_META.UniqueID = PLAYER_META.GetUniqueId
PLAYER_META.InVehicle = PLAYER_META.IsInVehicle
PLAYER_META.GetVehicle = PLAYER_META.GetVehicleEntity
PLAYER_META.AnimResetGestureSlot = PLAYER_META.AnimationResetGestureSlot
PLAYER_META.AnimRestartGesture = PLAYER_META.AnimationRestartGesture
PLAYER_META.AnimRestartMainSequence = PLAYER_META.AnimationRestartMainSequence
PLAYER_META.AnimSetGestureSequence = PLAYER_META.AnimationSetGestureSequence
PLAYER_META.AnimSetGestureWeight = PLAYER_META.AnimationSetGestureWeight
PLAYER_META.GetName = PLAYER_META.GetPlayerName
PLAYER_META.Name = PLAYER_META.GetPlayerName
PLAYER_META.Nick = PLAYER_META.GetPlayerName
PLAYER_META.SetSlowWalkSpeed = PLAYER_META.SetWalkSpeed
PLAYER_META.GetSlowWalkSpeed = PLAYER_META.GetWalkSpeed
PLAYER_META.SetWalkSpeed = PLAYER_META.SetNormalSpeed
PLAYER_META.GetWalkSpeed = PLAYER_META.GetNormalSpeed
PLAYER_META.GetCrouchedWalkSpeed = PLAYER_META.GetCrouchWalkFraction
PLAYER_META.SetCrouchedWalkSpeed = PLAYER_META.SetCrouchWalkFraction
PLAYER_META.Ping = PLAYER_META.GetPing
PLAYER_META.KeyDown = PLAYER_META.IsKeyDown
PLAYER_META.KeyDownLast = PLAYER_META.WasKeyDown
PLAYER_META.KeyPressed = PLAYER_META.WasKeyPressed
PLAYER_META.KeyReleased = PLAYER_META.WasKeyReleased
PLAYER_META.GetFOV = PLAYER_META.GetFov
PLAYER_META.SetFOV = PLAYER_META.SetFov
PLAYER_META.SetUnDuckSpeed = PLAYER_META.SetUnDuckFraction
PLAYER_META.GetUnDuckSpeed = PLAYER_META.GetUnDuckFraction
PLAYER_META.SetDSP = PLAYER_META.SetDsp
PLAYER_META.ShouldDropWeapon = PLAYER_META.SetDropActiveWeaponOnDeath
PLAYER_META.Armor = PLAYER_META.GetArmor
PLAYER_META.ConCommand = PLAYER_META.RunConsoleCommand
PLAYER_META.Frags = PLAYER_META.GetFrags
PLAYER_META.Deaths = PLAYER_META.GetDeaths

function PLAYER_META:LagCompensation(shouldStart)
	if (shouldStart) then
		self:StartLagCompensation()
	else
		self:FinishLagCompensation()
	end
end

function PLAYER_META:Crouching()
	return self:IsFlagSet(_E.ENGINE_FLAG.DUCKING)
end

function PLAYER_META:GetInfo(consoleVariableName)
	return engine.GetClientConsoleVariableValue(self, consoleVariableName)
end

function PLAYER_META:GetInfoNum(consoleVariableName, default)
	return engine.GetClientConsoleVariableValueAsNumber(self, consoleVariableName) or default
end

function PLAYER_META:PrintMessage(type, message)
	Players.ClientPrint(self, type, message)
end

function PLAYER_META:IsDrivingEntity()
	return false -- TODO: implement this
end

function PLAYER_META:IsPlayingTaunt()
	return false -- TODO: implement this
end

function PLAYER_META:IsTyping()
	return false -- TODO: implement this
end

function PLAYER_META:GetCanZoom()
	return self.__canZoom or false
end

function PLAYER_META:SetCanZoom(canZoom)
	self.__canZoom = canZoom
end

-- TODO: Implement these correctly (I'm not sure what they do atm)
function PLAYER_META:SetHoveredWidget(widget)
	self.__hoveredWidget = widget
end

function PLAYER_META:GetHoveredWidget()
	return self.__hoveredWidget
end

function PLAYER_META:SetPressedWidget(widget)
	self.__pressedWidget = widget
end

function PLAYER_META:GetPressedWidget()
	return self.__pressedWidget
end

function PLAYER_META:FlashlightIsOn()
	return self:IsFlashlightOn()
end

function PLAYER_META:SetPlayerColor(color)
	-- TODO: Implement this
end

function PLAYER_META:GetPlayerColor()
	-- TODO: Implement this
	return Color(255, 255, 255)
end

function PLAYER_META:SetWeaponColor(color)
	-- TODO: Implement this
end

function PLAYER_META:GetWeaponColor()
	-- TODO: Implement this
	return Color(255, 255, 255)
end

if (SERVER) then
	function PLAYER_META:SelectWeapon(weaponClass)
		self:SwitchWeapon(self:GetWeapon(weaponClass))
	end

	function PLAYER_META:StripAmmo()
		self:RemoveAllAmmo()
	end

	function PLAYER_META:StripWeapon(weaponClass)
		self:RemoveWeapon(self:GetWeapon(weaponClass))
	end

	function PLAYER_META:StripWeapons()
		local shouldRemoveSuit = false
		-- shouldRemoveSuit is false by default, but here for clarity.
		-- TODO: What does Garry's Mod do with this?
		self:RemoveAllItems(shouldRemoveSuit)
	end

	util.AddNetworkString("__PlayerLuaRun")
	function PLAYER_META:SendLua(lua)
		net.Start("__PlayerLuaRun")
		net.WriteString(lua)
		net.Send(self)
	end

	PLAYER_META.Spectate = PLAYER_META.SetObserverMode
	PLAYER_META.SpectateEntity = PLAYER_META.SetObserverTarget

	function PLAYER_META:UnSpectate()
		self:SetObserverMode(OBS_MODE_NONE)
	end

	function PLAYER_META:CrosshairDisable()
		self:ShowCrosshair(false)
	end

	function PLAYER_META:CrosshairEnable()
		self:ShowCrosshair(true)
	end

	function PLAYER_META:Flashlight(isOn)
		if (isOn) then
			self:TurnFlashlightOn()
		else
			self:TurnFlashlightOff()
		end
	end
else
	net.Receive("__PlayerLuaRun", function()
		local lua = net.ReadString()
		RunString(lua)
	end)
end

function PLAYER_META:SetClassID(id)
	self:SetNWInt("__classId", id)
end

function PLAYER_META:GetClassID()
	return self:GetNWInt("__classId", 0)
end

function PLAYER_META:IsListenServerHost()
	if (CLIENT) then
		if (Engines.GetMaxClients() == 1) then
			-- Singleplayer always returns true
			return true
		else
			return self == Players.FindByIndex(1)
		end
	end

	return self == Engines.GetListenServerHost()
end

if (CLIENT) then
	-- TODO: Implement this properly (possibly in CHLClient::ClientAdjustStartSoundParams)
	function PLAYER_META:GetVoiceVolumeScale()
		return self.__voiceVolumeScale or 1.0
	end

	-- TODO: Implement this properly
	function PLAYER_META:SetVoiceVolumeScale(scale)
		self.__voiceVolumeScale = scale
	end
end

local MOVE_DATA_META = FindMetaTable("MoveData")
MOVE_DATA_META.KeyDown = MOVE_DATA_META.IsKeyDown
MOVE_DATA_META.KeyPressed = MOVE_DATA_META.WasKeyPressed
MOVE_DATA_META.KeyReleased = MOVE_DATA_META.WasKeyReleased
MOVE_DATA_META.KeyWasDown = MOVE_DATA_META.WasKeyDown
MOVE_DATA_META.GetAbsMoveAngles = MOVE_DATA_META.GetAbsoluteMoveAngles
MOVE_DATA_META.SetAbsMoveAngles = MOVE_DATA_META.SetAbsoluteMoveAngles

local CONSOLE_VARIABLE_META = FindMetaTable("ConsoleVariable")
CONSOLE_VARIABLE_META.GetInt = CONSOLE_VARIABLE_META.GetInteger
CONSOLE_VARIABLE_META.SetInt = CONSOLE_VARIABLE_META.SetValue
CONSOLE_VARIABLE_META.SetFloat = CONSOLE_VARIABLE_META.SetValue
CONSOLE_VARIABLE_META.SetBool = CONSOLE_VARIABLE_META.SetValue
CONSOLE_VARIABLE_META.SetString = CONSOLE_VARIABLE_META.SetValue

local TEXTURE_META = FindMetaTable("Texture")

function TEXTURE_META:Width()
	return self:GetActualWidth()
end

function TEXTURE_META:Height()
	return self:GetActualHeight()
end

local MATERIAL_META = FindMetaTable("Material")
MATERIAL_META.GetShader = MATERIAL_META.GetShaderName
MATERIAL_META.IsError = MATERIAL_META.IsErrorMaterial
MATERIAL_META.Recompute = MATERIAL_META.RecomputeStateSnapshots
MATERIAL_META.SetInt = MATERIAL_META.SetInteger
MATERIAL_META.GetInt = MATERIAL_META.GetInteger

-- GMod's Material:SetString parses numbers/vectors (halo.lua does SetString("$color", "1 1 1") and
-- SetString("$alpha", "1")). The engine's SetStringValue turns the variable into a string type, which
-- shaders then read as 0, so route numeric strings to SetFloat/SetVector instead.
do
	local originalSetString = MATERIAL_META.SetString

	function MATERIAL_META:SetString(name, value)
		if (type(value) == "string") then
			local trimmed = value:gsub("^%s*[%[{]?%s*", ""):gsub("%s*[%]}]?%s*$", "")
			local numbers = {}

			for part in trimmed:gmatch("%S+") do
				numbers[#numbers + 1] = tonumber(part)

				if (numbers[#numbers] == nil) then
					numbers = nil
					break
				end
			end

			if (numbers and #numbers == 1) then
				return self:SetFloat(name, numbers[1])
			elseif (numbers and #numbers == 3) then
				return self:SetVector(name, Vector(numbers[1], numbers[2], numbers[3]))
			end
		end

		return originalSetString(self, name, value)
	end
end

function MATERIAL_META:Width()
	local baseTexture = self:GetTexture("$basetexture")

	if (baseTexture) then
		return baseTexture:GetActualWidth()
	end

	return 0
end

function MATERIAL_META:Height()
	local baseTexture = self:GetTexture("$basetexture")

	if (baseTexture) then
		return baseTexture:GetActualHeight()
	end

	return 0
end

if (SERVER) then
	local RECIPIENT_FILTER_META = FindMetaTable("RecipientFilter")
	RECIPIENT_FILTER_META.AddPlayer = RECIPIENT_FILTER_META.AddRecipient
	RECIPIENT_FILTER_META.AddPlayers = RECIPIENT_FILTER_META.CopyFrom
	RECIPIENT_FILTER_META.GetCount = RECIPIENT_FILTER_META.GetRecipientCount
	RECIPIENT_FILTER_META.GetPlayers = RECIPIENT_FILTER_META.GetRecipients
	RECIPIENT_FILTER_META.RemovePlayer = RECIPIENT_FILTER_META.RemoveRecipient
	RECIPIENT_FILTER_META.RemoveAllPlayers = RECIPIENT_FILTER_META.RemoveAllRecipients
end

--[[
	We don't use achievements:
--]]
achievements = {
	BalloonPopped = function() end,
	Count = function() return 0 end,
	EatBall = function() end,
	GetCount = function() return 0 end,
	GetDesc = function() return "" end,
	GetGoal = function() return 0 end,
	GetName = function() return "" end,
	IncBaddies = function() end,
	IncBystander = function() end,
	IncGoodies = function() end,
	IsAchieved = function() return false end,
	Remover = function() end,
	SpawnedNPC = function() end,
	SpawnedProp = function() end,
	SpawnedRagdoll = function() end,
	SpawnMenuOpen = function() end,
}

Material = function(name)
	return Materials.Find(name)
end

if (SERVER) then
	resource.AddWorkshop = function() end -- TODO: Implement (low priority)
else
	-- Empty functions so we can easily add resource files without errors in shared code
	resource = {}
	resource.AddFile = function() end
	resource.AddWorkshop = function() end
	resource.AddSingleFile = function() end

	-- Returns whether the currently focused panel is a child of the given one.
	function vgui.FocusedHasParent(panel)
		local focusedPanel = Inputs.GetFocus()

		if (not IsValid(focusedPanel)) then
			return false
		end

		while (IsValid(focusedPanel)) do
			if (focusedPanel == panel) then
				return true
			end

			focusedPanel = focusedPanel:GetParent()
		end

		return false
	end

	vgui.GetWorldPanel = Panels.GetClientLuaRootPanel
	GetHUDPanel = Panels.GetClientLuaRootHudPanel

	vgui.GetKeyboardFocus = Inputs.GetFocus
	vgui.GetHoveredPanel = Inputs.GetMouseOver
	vgui.CursorVisible = Surfaces.IsCursorVisible

	gui = {
		MouseX = function()
			local x, y = Inputs.GetCursorPosition()
			return x
		end,

		MouseY = function()
			local x, y = Inputs.GetCursorPosition()
			return y
		end,

		MousePos = function()
			return Inputs.GetCursorPosition()
		end,

		OpenURL = function(url)
			if (RenderExtensions and RenderExtensions.OpenURL) then
				RenderExtensions.OpenURL(tostring(url))
			else
				print("gui.OpenURL: " .. tostring(url))
			end
		end,

		ShowConsole = function()
			RunConsoleCommand("showconsole")
		end,

		ActivateGameUI = function()
			RunConsoleCommand("gameui_activate")
		end,

		HideGameUI = function()
			RunConsoleCommand("gameui_hide")
		end,

		EnableScreenClicker = function(visible)
			Surfaces.SetCursorAlwaysVisible(visible)
		end,

		SetMousePos = input.SetCursorPosition,
		ScreenToVector = input.ScreenToWorld,
		AimToVector = input.AimToVector,
		IsConsoleVisible = Engines.IsConsoleVisible,
		IsGameUIVisible = EngineVgui.IsGameUiVisible,
	}

	cam = {
		Start3D = Renders.PushView3D,
		Start2D = Renders.PushView2D,
		End3D = Renders.PopView3D,
		End2D = Renders.PopView2D,

		IgnoreZ = function(bBool)
			if (bBool) then
				Renders.DepthRange(0, 0.01)
			else
				Renders.DepthRange(0, 1)
			end
		end,

		GetModelMatrix = Renders.GetModelMatrix,
		PushModelMatrix = Renders.PushModelMatrix,
		PopModelMatrix = Renders.PopModelMatrix,
	}

	render.SetModelLighting = Renders.SetAmbientLightCube
	render.ResetModelLighting = Renders.ResetAmbientLightCube
	render.PushFilterMin = Renders.PushFilterMinification
	render.PopFilterMin = Renders.PopFilterMinification
	render.PushFilterMag = Renders.PushFilterMagnification
	render.PopFilterMag = Renders.PopFilterMagnification
	render.SetScissorRect = Renders.SetScissorRectangle
	render.SetWriteDepthToDestAlpha = Renders.SetWriteDepthToDestinationAlpha
	render.EnableClipping = Renders.SetClippingEnabled

	function render.Clear(r, g, b, a, clearDepth, clearStencil)
		-- The clear colour has to be set before the clear happens
		Renders.ClearColor(Color(r, g, b, a))
		Renders.ClearBuffers(true, clearDepth or false, clearStencil or false)
	end

	function render.ClearDepth(clearStencil)
		Renders.ClearBuffers(false, true, clearStencil or false)
	end

	function render.ClearStencil()
		Renders.ClearBuffers(false, false, true)
	end

	input.SetCursorPos = input.SetCursorPosition
	input.GetCursorPos = input.GetCursorPosition
	input.IsButtonDown = input.IsKeyDown
	input.StartKeyTrapping = engine.StartKeyTrapMode
	input.IsKeyTrapping = engine.IsKeyTrapping
	input.CheckKeyTrapping = engine.CheckKeyTrapping
	input.GetKeyName = input.KeyCodeToDisplayString

	function input.IsShiftDown()
		return input.IsKeyDown(KEY_LSHIFT) or input.IsKeyDown(KEY_RSHIFT)
	end

	function input.IsControlDown()
		return input.IsKeyDown(KEY_LCONTROL) or input.IsKeyDown(KEY_RCONTROL)
	end

	CreateMaterial = Materials.Create
	DisableClipping = Surfaces.DisableClipping
	GetViewEntity = Renders.GetViewEntity

	local MODEL_IMAGE_PANEL_META = FindMetaTable("ModelImagePanel")
	MODEL_IMAGE_PANEL_META._OriginalRebuildSpawnIcon = MODEL_IMAGE_PANEL_META._OriginalRebuildSpawnIcon or
		MODEL_IMAGE_PANEL_META.RebuildSpawnIcon
	registry.ModelImage = MODEL_IMAGE_PANEL_META

	MODEL_IMAGE_PANEL_META.SetSpawnIcon = MODEL_IMAGE_PANEL_META.SetModelImage

	function MODEL_IMAGE_PANEL_META:RebuildSpawnIcon()
		local ent = ClientsideModel(self:GetModel(), RENDERGROUP_OTHER)
		local result = PositionSpawnIcon(ent, vector_origin)
		ent:Remove()

		self:_OriginalRebuildSpawnIcon({
			origin = result.origin,
			angles = result.angles,
			fieldOfView = result.fov,
			zNear = result.znear,
			zFar = result.zfar,
		})
	end

	function MODEL_IMAGE_PANEL_META:RebuildSpawnIconEx(tab)
		self:_OriginalRebuildSpawnIcon({
			origin = tab.cam_pos or tab.origin or Vector(0, 0, 0),
			angles = tab.cam_ang or tab.angles or Angle(0, 0, 0),
			fieldOfView = tab.cam_fov or tab.fieldOfView or 90,
			zNear = tab.nearz or tab.zNear or 1,
			zFar = tab.farz or tab.zFar or 1000,
		})
	end

	local PANEL_META = FindMetaTable("Panel")
	PANEL_META.GetPos = PANEL_META.GetPosition
	PANEL_META.SetPos = PANEL_META.SetPosition
	PANEL_META.GetZPos = PANEL_META.GetZIndex
	PANEL_META.SetZPos = PANEL_META.SetZIndex
	PANEL_META.ParentToHUD = PANEL_META.ParentToHud
	PANEL_META._OriginalSetCursor = PANEL_META._OriginalSetCursor or PANEL_META.SetCursor
	PANEL_META._OriginalGetParent = PANEL_META._OriginalGetParent or PANEL_META.GetParent

	PANEL_META.CursorPos = PANEL_META.GetLocalCursorPosition
	PANEL_META.Remove = PANEL_META.MarkForDeletion
	PANEL_META.GetTable = PANEL_META.GetRefTable
	PANEL_META.Dock = PANEL_META.SetDock
	PANEL_META.DockMargin = PANEL_META.SetDockMargin
	PANEL_META.DockPadding = PANEL_META.SetDockPadding
	PANEL_META.ChildCount = PANEL_META.GetChildCount
	PANEL_META.ChildrenSize = PANEL_META.GetChildrenSize
	PANEL_META.NoClipping = PANEL_META.SetPaintClippingEnabled

	-- Deprecated:
	PANEL_META.SetKeyBoardInputEnabled = PANEL_META.SetKeyboardInputEnabled

	function PANEL_META:SizeToContents(sizeWidth, sizeHeight)
		-- For some reason DTree_Node uses SizeToContents on a DListLayout, which doesn't have a SizeToContents function.
		-- :/
		self:SizeToChildren(sizeWidth, sizeHeight)
	end

	function PANEL_META:DrawFilledRect()
		local width, height = self:GetSize()
		surface.DrawRect(0, 0, width, height)
	end

	function PANEL_META:DrawOutlinedRect()
		local width, height = self:GetSize()
		surface.DrawOutlinedRect(0, 0, width, height)
	end

	function PANEL_META:DrawTexturedRect()
		local width, height = self:GetSize()
		surface.DrawTexturedRect(0, 0, width, height)
	end

	function PANEL_META:MouseCapture(doCapture)
		if (doCapture) then
			input.SetMouseCapture(self)
		else
			input.SetMouseCapture(nil)
		end
	end

	function PANEL_META:GetParent()
		local parent = self:_OriginalGetParent()

		if (not IsValid(parent)) then
			return nil
		end

		return parent
	end

	-- Change casing and add functionality to pass individual color components.
	function PANEL_META:SetBGColor(rOrColor, g, b, a)
		-- Color objects aren't plain tables (type() is "Color"), so test for numbers instead of istable
		if (type(rOrColor) ~= "number") then
			self:SetBackgroundColor(rOrColor)
		else
			self:SetBackgroundColor(Colors.Create(rOrColor, g, b, a))
		end
	end

	-- Change casing and add functionality to pass individual color components.
	function PANEL_META:SetFGColor(rOrColor, g, b, a)
		-- Color objects aren't plain tables (type() is "Color"), so test for numbers instead of istable
		if (type(rOrColor) ~= "number") then
			self:SetForegroundColor(rOrColor)
		else
			self:SetForegroundColor(Colors.Create(rOrColor, g, b, a))
		end
	end

	local LABEL_PANEL_META = FindMetaTable("Label")
	LABEL_PANEL_META._OriginalSetFont = LABEL_PANEL_META._OriginalSetFont or LABEL_PANEL_META.SetFont
	LABEL_PANEL_META._OriginalGetFont = LABEL_PANEL_META._OriginalGetFont or LABEL_PANEL_META.GetFont
	LABEL_PANEL_META.SetTextColor = LABEL_PANEL_META.SetForegroundColor
	LABEL_PANEL_META.GetTextColor = LABEL_PANEL_META.GetForegroundColor
	LABEL_PANEL_META._OriginalSetContentAlignment = LABEL_PANEL_META._OriginalSetContentAlignment or
		LABEL_PANEL_META.SetContentAlignment

	LABEL_PANEL_META.GetTextSize = LABEL_PANEL_META.GetContentSize

	function LABEL_PANEL_META:SetFontInternal(font)
		self:SetFontByName(font)
	end

	function LABEL_PANEL_META:SetFont(font)
		self:SetFontByName(font)
	end

	function LABEL_PANEL_META:GetFont()
		return self:GetFontName()
	end

	-- TODO: Actually draw the drop shadow
	function LABEL_PANEL_META:SetExpensiveShadow(distance, color)
		self._expensiveShadow = { distance = distance, color = color }
	end

	--- Set content alignment based on numpad keys
	local NUMPAD_TO_ALIGNMENT_MAP = {
		[7] = 0,
		[8] = 1,
		[9] = 2,

		[4] = 3,
		[5] = 4,
		[6] = 5,

		[1] = 6,
		[2] = 7,
		[3] = 8,
	}
	function LABEL_PANEL_META:SetContentAlignment(numpadAlignment)
		local alignment = NUMPAD_TO_ALIGNMENT_MAP[numpadAlignment]

		if (not alignment) then
			error("attempt to set invalid content alignment \"" .. tostring(numpadAlignment) .. "\"", 2)
		end

		self:_OriginalSetContentAlignment(alignment)
	end

	local HTML_PANEL_META = FindMetaTable("Html")
	HTML_PANEL_META.OpenURL = HTML_PANEL_META.OpenUrl
	HTML_PANEL_META.SetHTML = HTML_PANEL_META.SetHtml
	HTML_PANEL_META.NewObject = HTML_PANEL_META.AddJavascriptObject
	HTML_PANEL_META.NewObjectCallback = HTML_PANEL_META.AddJavascriptObjectCallback

	local TEXT_ENTRY_PANEL_META = FindMetaTable("TextEntry")
	TEXT_ENTRY_PANEL_META._OriginalSetFont = TEXT_ENTRY_PANEL_META._OriginalSetFont or TEXT_ENTRY_PANEL_META.SetFont
	TEXT_ENTRY_PANEL_META._OriginalGetFont = TEXT_ENTRY_PANEL_META._OriginalGetFont or TEXT_ENTRY_PANEL_META.GetFont
	TEXT_ENTRY_PANEL_META.DrawTextEntryText = TEXT_ENTRY_PANEL_META.PaintText
	TEXT_ENTRY_PANEL_META.GetCaretPos = TEXT_ENTRY_PANEL_META.GetCursorPosition
	TEXT_ENTRY_PANEL_META.SetCaretPos = TEXT_ENTRY_PANEL_META.SetCursorPosition

	local FRAME_PANEL_META = FindMetaTable("Frame")
	FRAME_PANEL_META.SetDraggable = FRAME_PANEL_META.SetMoveable
	FRAME_PANEL_META.GetDraggable = FRAME_PANEL_META.GetMoveable

	function TEXT_ENTRY_PANEL_META:SetFontInternal(font)
		self:SetFontByName(font)
	end

	function TEXT_ENTRY_PANEL_META:SetFont(font)
		self:SetFontByName(font)
	end

	function TEXT_ENTRY_PANEL_META:GetFont()
		return self:GetFontName()
	end

	local CHECK_BUTTON_PANEL_META = FindMetaTable("CheckButton")
	CHECK_BUTTON_PANEL_META.GetValue = CHECK_BUTTON_PANEL_META.IsSelected

	function PANEL_META:GetContentSize()
		local className = self:GetClassName()

		if (className == "LLabel") then
			return LABEL_PANEL_META.GetContentSize(self)
		end

		error("attempt to get content size of unsupported panel class \"" .. tostring(className) .. "\"", 2)
	end

	local EDITABLEPANEL_PANEL_META = FindMetaTable("EditablePanel")
	function PANEL_META:IsModal()
		local className = self:GetClassName()

		if (className == "LEditablePanel") then
			return EDITABLEPANEL_PANEL_META.IsModal(self)
		end
	end

	-- GMod uses this for menus/tooltips that must render above everything else. VGUI has no
	-- separate "on top" layer, so bring the panel to the front of its siblings instead.
	function PANEL_META:SetDrawOnTop(isOnTop)
		self.__gmodDrawOnTop = isOnTop and true or false

		if (isOnTop and self.MoveToFront) then
			self:MoveToFront()
		end
	end

	function PANEL_META:GetDrawOnTop()
		return self.__gmodDrawOnTop or false
	end

	-- Maps cursor strings to cursor codes.
	local cursorMap = {
		-- ["user"] = CURSOR_CODE.USER,
		["none"] = CURSOR_CODE.NONE,
		["arrow"] = CURSOR_CODE.ARROW,
		["beam"] = CURSOR_CODE.I_BEAM,
		["hourglass"] = CURSOR_CODE.HOURGLASS,
		["waitarrow"] = CURSOR_CODE.WAIT_ARROW,
		["crosshair"] = CURSOR_CODE.CROSSHAIR,
		["up"] = CURSOR_CODE.UP,
		["sizenwse"] = CURSOR_CODE.SIZE_NWSE,
		["sizenesw"] = CURSOR_CODE.SIZE_NESW,
		["sizewe"] = CURSOR_CODE.SIZE_WE,
		["sizens"] = CURSOR_CODE.SIZE_NS,
		["sizeall"] = CURSOR_CODE.SIZE_ALL,
		["no"] = CURSOR_CODE.NO,
		["hand"] = CURSOR_CODE.HAND,
		["blank"] = CURSOR_CODE.BLANK,
		-- ["last"] = CURSOR_CODE.LAST,
		-- ["alwaysvisiblepush"] = CURSOR_CODE.ALWAYS_VISIBLE_PUSH,
		-- ["alwaysvisiblepop"] = CURSOR_CODE.ALWAYS_VISIBLE_POP,
	}

	function PANEL_META:SetCursor(cursor)
		local cursorCode = cursorMap[cursor]

		if (not cursorCode) then
			cursorCode = CURSOR_CODE.NONE
		end

		self:_OriginalSetCursor(cursorCode)
	end

	ScrW = Utilities.GetScreenWidth
	ScrH = Utilities.GetScreenHeight

	Utilities.IsSkyboxVisibleFromPoint = Engines.IsSkyboxVisibleFromPoint

	GetRenderTargetEx = render.CreateRenderTargetTextureEx
	GetRenderTarget = function(name, width, height)
		return Renders.CreateRenderTargetTextureEx(name, width, height)
	end
	EyePos = render.MainViewOrigin
	EyeAngles = render.MainViewAngles
	EyeVector = render.MainViewForward
	-- Newer GMod (properties.lua, etc.) uses the MainEye* names for the main view
	MainEyePos = MainEyePos or render.MainViewOrigin
	MainEyeAngles = MainEyeAngles or render.MainViewAngles
	MainEyeVector = MainEyeVector or render.MainViewForward

	-- GMod: while the cursor is over a world-clicker panel (the context menu), the local
	-- player's aim follows the mouse. properties.lua relies on this for the hover halo.
	do
		local playerMeta = FindMetaTable("Player")
		local originalGetAimVector = playerMeta and playerMeta.GetAimVector

		if (originalGetAimVector and not playerMeta.__gmodWorldClickerAim) then
			playerMeta.__gmodWorldClickerAim = true

			function playerMeta:GetAimVector(...)
				if (self == LocalPlayer() and gui and gui.ScreenToVector and vgui.GetHoveredPanel) then
					local hovered = vgui.GetHoveredPanel()

					if (IsValid(hovered) and hovered.IsWorldClicker and hovered:IsWorldClicker()) then
						local x, y = input.GetCursorPos()
						local direction = gui.ScreenToVector(x, y)

						if (direction) then
							return direction
						end
					end
				end

				return originalGetAimVector(self, ...)
			end
		end
	end

	LocalPlayer = Players.GetLocalPlayer

	surface.SetDrawColor = Surfaces.DrawSetColor
	surface.DrawRect = Surfaces.DrawFilledRectangle
	surface.DrawPoly = Surfaces.DrawTexturedPolygon
	surface.SetAlphaMultiplier = Surfaces.DrawSetAlphaMultiplier
	surface.GetAlphaMultiplier = surface.GetAlphaMultiplier or Surfaces.DrawGetAlphaMultiplier

	-- GMod: surface.DrawCircle(x, y, radius, r, g, b, a) or (x, y, radius, color);
	-- an outlined circle in the given colour (the current draw colour if none)
	surface.DrawCircle = function(x, y, radius, r, g, b, a)
		if (r ~= nil and type(r) ~= "number") then
			r, g, b, a = r.r, r.g, r.b, r.a
		end

		if (r ~= nil) then
			surface.SetDrawColor(r, g or 255, b or 255, a or 255)
		end

		radius = tonumber(radius) or 0

		if (radius <= 0) then
			return
		end

		local segments = math.max(8, math.min(64, math.floor(radius / 2)))
		Surfaces.DrawOutlinedCircle(x, y, radius, segments)
	end

	-- GMod's old surface.CreateFont argument order
	surface.CreateLegacyFont = surface.CreateLegacyFont or function(font, size, weight, antialias, additive, name, shadow, outline, blurSize)
		surface.CreateFont(name, {
			font = font,
			size = size,
			weight = weight,
			antialias = antialias,
			additive = additive,
			shadow = shadow,
			outline = outline,
			blursize = blurSize,
		})
	end

	local textureMap = {}

	surface.GetTextureID = function(name)
		if (not file.Exists("materials/" .. name .. ".vmt", "GAME") and not file.Exists("materials/" .. name, "GAME")) then
			name = "" .. name
		end

		if (not textureMap[name]) then
			textureMap[name] = surface.CreateNewTextureID()
			surface.DrawSetTextureFile(textureMap[name], name)
		end

		return textureMap[name]
	end

	surface.GetTextureNameByID = function(id)
		for name, textureID in pairs(textureMap) do
			if (textureID == id) then
				return name
			end
		end
	end

	local function getAppropriateBaseParent()
		if (GAMEUI) then
			return Panels.GetGameUIPanel()
		end

		return Panels.GetClientLuaRootPanel()
	end

	--[[
		We disable our own Panels.Create and Panels.Register logic, in favor of the GMod one.
		This Panels.Create will be used as vgui.CreateX by gmod to create internal panels.
	--]]
	function Panels.Create(panelClassName, parentPanel, name)
		parentPanel = parentPanel or getAppropriateBaseParent()

		if (panelClassName == "ModelImage") then
			panelClassName = "ModelImagePanel"
		end

		if (not Panels[panelClassName]) then
			error("attempt to create non-existing panel class \"" .. tostring(panelClassName) .. "\"", 2)
		end

		return Panels[panelClassName](parentPanel, name or panelClassName)
	end

	surface.SetDrawColor = Surfaces.DrawSetColor
	surface.DrawRect = Surfaces.DrawFilledRectangle
	surface.DrawOutlinedRect = Surfaces.DrawOutlinedRectangle
	surface.DrawTexturedRect = Surfaces.DrawTexturedRectangle
	-- TODO: surface.DrawTexturedRectRotated = Surfaces.DrawTexturedRotatedRectangle
	surface.DrawTexturedRectUV = Surfaces.DrawTexturedSubRectangle
	surface.GetTextPos = Surfaces.DrawGetTextPosition
	surface.SetTextPos = Surfaces.DrawSetTextPosition
	surface.SetTextColor = Surfaces.DrawSetTextColor
	surface.DrawText = Surfaces.DrawPrintText
	surface.SetTexture = Surfaces.DrawSetTexture

	local currentFont
	surface.SetFont = function(font)
		currentFont = font
		surface.DrawSetTextFont(font)
	end

	local oldTextSize = Surfaces.GetTextSize

	surface.GetTextSize = function(text)
		return oldTextSize(currentFont, text)
	end

	surface.SetMaterial = function(material)
		local name = material:GetString("$basetexture")

		if (not textureMap[name]) then
			textureMap[name] = Surfaces.CreateNewTextureID(true)
			Surfaces.DrawSetTextureMaterial(textureMap[name], material)
		end

		Surfaces.DrawSetTexture(textureMap[name])
	end

	-- TODO: Implement
	surface.DrawTexturedRectRotated = function(x, y, w, h, rotation)
		-- See src/game/client/hl2/hud_zoom.cpp for an example of how to possibly implement this:
		--[[
			// draw the darkened edges, with a rotated texture in the four corners
			CMatRenderContextPtr pRenderContext( materials );
			pRenderContext->Bind( m_ZoomMaterial );
			IMesh *pMesh = pRenderContext->GetDynamicMesh( true, NULL, NULL, NULL );

			float x0 = 0.0f, x1 = fX, x2 = wide;
			float y0 = 0.0f, y1 = fY, y2 = tall;

			float uv1 = 1.0f - (1.0f / 255.0f);
			float uv2 = 0.0f + (1.0f / 255.0f);

			struct coord_t
			{
				float x, y;
				float u, v;
			};
			coord_t coords[16] =
			{
				// top-left
				{ x0, y0, uv1, uv2 },
				{ x1, y0, uv2, uv2 },
				{ x1, y1, uv2, uv1 },
				{ x0, y1, uv1, uv1 },

				// top-right
				{ x1, y0, uv2, uv2 },
				{ x2, y0, uv1, uv2 },
				{ x2, y1, uv1, uv1 },
				{ x1, y1, uv2, uv1 },

				// bottom-right
				{ x1, y1, uv2, uv1 },
				{ x2, y1, uv1, uv1 },
				{ x2, y2, uv1, uv2 },
				{ x1, y2, uv2, uv2 },

				// bottom-left
				{ x0, y1, uv1, uv1 },
				{ x1, y1, uv2, uv1 },
				{ x1, y2, uv2, uv2 },
				{ x0, y2, uv1, uv2 },
			};

			CMeshBuilder meshBuilder;
			meshBuilder.Begin( pMesh, MATERIAL_QUADS, 4 );

			for (int i = 0; i < 16; i++)
			{
				meshBuilder.Color4f( 0.0, 0.0, 0.0, alpha );
				meshBuilder.TexCoord2f( 0, coords[i].u, coords[i].v );
				meshBuilder.Position3f( coords[i].x, coords[i].y, 0.0f );
				meshBuilder.AdvanceVertex();
			}

			meshBuilder.End();
			pMesh->Draw();
		--]]
	end
end

function baseclassGetCompatibility(name)
	if (name:sub(1, 9) == "gamemode_") then
		name = name:sub(10)

		return Gamemodes.Get(name)
	end

	return baseclass.Get(name)
end

if (CLIENT) then
	chat = Chats

	chat.GetChatBoxPos = Chats.GetChatBoxPosition
	chat.Open = Chats.StartMessageMode
	chat.Close = Chats.StopMessageMode

	ProjectedTexture = ProjectedTextures.Create

	local PROJECTED_TEXTURE_META = FindMetaTable("ProjectedTexture")
	PROJECTED_TEXTURE_META.GetPos = PROJECTED_TEXTURE_META.GetPosition
	PROJECTED_TEXTURE_META.SetPos = PROJECTED_TEXTURE_META.SetPosition
	PROJECTED_TEXTURE_META.GetFOV = PROJECTED_TEXTURE_META.GetFov
	PROJECTED_TEXTURE_META.SetFOV = PROJECTED_TEXTURE_META.SetFov
	PROJECTED_TEXTURE_META.GetHorizontalFOV = PROJECTED_TEXTURE_META.GetHorizontalFov
	PROJECTED_TEXTURE_META.SetHorizontalFOV = PROJECTED_TEXTURE_META.SetHorizontalFov
	PROJECTED_TEXTURE_META.GetVerticalFOV = PROJECTED_TEXTURE_META.GetVerticalFov
	PROJECTED_TEXTURE_META.SetVerticalFOV = PROJECTED_TEXTURE_META.SetVerticalFov

	local AUDIO_CHANNEL_META = FindMetaTable("AudioChannel")
	AUDIO_CHANNEL_META.FFT = AUDIO_CHANNEL_META.GetFft
	AUDIO_CHANNEL_META.Is3D = AUDIO_CHANNEL_META.Is3d
	AUDIO_CHANNEL_META.Set3DCone = AUDIO_CHANNEL_META.Set3dCone
	AUDIO_CHANNEL_META.Get3DCone = AUDIO_CHANNEL_META.Get3dCone
	AUDIO_CHANNEL_META.Set3DEnabled = AUDIO_CHANNEL_META.Set3dEnabled
	AUDIO_CHANNEL_META.Get3DEnabled = AUDIO_CHANNEL_META.Get3dEnabled
	AUDIO_CHANNEL_META.Set3DFadeDistance = AUDIO_CHANNEL_META.Set3dFadeDistance
	AUDIO_CHANNEL_META.Get3DFadeDistance = AUDIO_CHANNEL_META.Get3dFadeDistance
	AUDIO_CHANNEL_META.GetPos = AUDIO_CHANNEL_META.GetPosition
	AUDIO_CHANNEL_META.SetPos = AUDIO_CHANNEL_META.SetPosition
	AUDIO_CHANNEL_META.GetTagsHTTP = AUDIO_CHANNEL_META.GetTagsOfHttp
	AUDIO_CHANNEL_META.GetTagsID3 = AUDIO_CHANNEL_META.GetTagsOfId3
	AUDIO_CHANNEL_META.GetTagsMeta = AUDIO_CHANNEL_META.GetTagsOfMeta
	AUDIO_CHANNEL_META.GetTagsOGG = AUDIO_CHANNEL_META.GetTagsOfOgg
	AUDIO_CHANNEL_META.GetTagsVendor = AUDIO_CHANNEL_META.GetTagsOfVendor

	matproxy = {
		Add = function(name, data) end -- TODO: Implement
	}

	spawnmenu = {
		PopulateFromTextFiles = function(callback)
			local spawnlists = file.Find("settings/spawnlist/*.txt", "GAME")

			for _, spawnlistFileName in ipairs(spawnlists) do
				local spawnlistKeyValues = KeyValues.Create(spawnlistFileName)
				spawnlistKeyValues:LoadFromFile("settings/spawnlist/" .. spawnlistFileName,
					"GAME")
				local spawnlist = spawnlistKeyValues:ToTable()

				-- KeyValues:ToTable() returns every value as a string, but sandbox compares ids as numbers
				-- (AddPropsOfParent( ..., 0 ) uses parentid != Info.parentid), so "0" never matched 0 and
				-- no spawnlist was added to the tree. GMod's engine passes numbers and a numeric contents list.
				local contents = {}

				if (istable(spawnlist.contents)) then
					local keys = {}

					for key in pairs(spawnlist.contents) do
						keys[#keys + 1] = key
					end

					table.sort(keys, function(a, b)
						local numberA, numberB = tonumber(a), tonumber(b)

						if (numberA and numberB) then
							return numberA < numberB
						end

						return tostring(a) < tostring(b)
					end)

					for _, key in ipairs(keys) do
						local object = spawnlist.contents[key]

						-- Same string problem per entry: SpawnIcon compares skin as a number (body stays a
						-- bodygroup string like "000000000", as in GMod)
						if (istable(object)) then
							for _, numberKey in ipairs({ "skin", "wide", "tall" }) do
								if (object[numberKey] ~= nil) then
									object[numberKey] = tonumber(object[numberKey]) or object[numberKey]
								end
							end
						end

						contents[#contents + 1] = object
					end
				end

				callback(
					spawnlistFileName,
					spawnlist.name,
					contents,
					spawnlist.icon,
					tonumber(spawnlist.id) or spawnlist.id,
					tonumber(spawnlist.parentid) or 0,
					spawnlist.needsapp ~= "" and spawnlist.needsapp or nil
				)
			end
		end,

		DoSaveToTextFiles = function(props)
			for filename, data in pairs(props) do
				file.Write("settings/spawnlist/" .. filename, data.contents)
			end
		end,
	}

	-- Newer GMod's spawnmenu module calls the engine function SaveToTextFiles
	spawnmenu.SaveToTextFiles = spawnmenu.DoSaveToTextFiles

	function LoadPresets()
		local loadedPresets = {}
		local _, directories = file.Find("settings/presets/*", "GAME")

		for _, directory in ipairs(directories) do
			local presetFiles = file.Find("settings/presets/" .. directory .. "/*.txt", "GAME")

			loadedPresets[directory] = {}

			for _, presetFileName in ipairs(presetFiles) do
				local presetKeyValues = KeyValues.Create(presetFileName)
				presetKeyValues:LoadFromFile(
					"settings/presets/" .. directory .. "/" .. presetFileName,
					"GAME")
				local preset = presetKeyValues:ToTable()

				loadedPresets[directory][presetKeyValues:GetName()] = preset
			end
		end

		return loadedPresets
	end
end

--[[
	Other shared functions.
--]]
unpack = unpack or table.unpack

MsgC = function(...)
	local currentColor = debug.GetRealmColor()

	for k, stringOrColor in ipairs({ ... }) do
		if (IsColor(stringOrColor)) then
			currentColor = stringOrColor
		else
			debug.PrintColorMessage(currentColor, tostring(stringOrColor))
		end
	end

	debug.PrintColorMessage(currentColor, "\n")
end

PrintTable = table.Print

ErrorNoHalt = function(...)
	Msg(...)
end

ErrorNoHaltWithStack = function(...)
	Msg(debug.traceback(table.concat({ ... }, " "), 2))
end

--[[
  sbox_ convars that aren't normally defined in Lua
  TODO: Read these from the gamemodes/gmname/gmname.txt keyvalues file
--]]
local gmod_language = CreateConVar("gmod_language", "en",
	{ FCVAR_ARCHIVE, FCVAR_NOTIFY, FCVAR_REPLICATED, FCVAR_SERVER_CAN_EXECUTE },
	"Language to use in GMod")

local sbox_godmode = CreateConVar("sbox_godmode", "0",
	{ FCVAR_ARCHIVE, FCVAR_NOTIFY, FCVAR_REPLICATED, FCVAR_SERVER_CAN_EXECUTE },
	"If enabled, all players will be invincible")
local sbox_maxballoons = CreateConVar("sbox_maxballoons", "100",
	{ FCVAR_ARCHIVE, FCVAR_NOTIFY, FCVAR_REPLICATED, FCVAR_SERVER_CAN_EXECUTE },
	"Maximum balloons a single player can create")
local sbox_maxbuttons = CreateConVar("sbox_maxbuttons", "50",
	{ FCVAR_ARCHIVE, FCVAR_NOTIFY, FCVAR_REPLICATED, FCVAR_SERVER_CAN_EXECUTE },
	"Maximum buttons a single player can create")
local sbox_maxcameras = CreateConVar("sbox_maxcameras", "10",
	{ FCVAR_ARCHIVE, FCVAR_NOTIFY, FCVAR_REPLICATED, FCVAR_SERVER_CAN_EXECUTE },
	"Maximum cameras a single player can create")
local sbox_maxdynamite = CreateConVar("sbox_maxdynamite", "10",
	{ FCVAR_ARCHIVE, FCVAR_NOTIFY, FCVAR_REPLICATED, FCVAR_SERVER_CAN_EXECUTE },
	"Maximum dynamites a single player can create")
local sbox_maxeffects = CreateConVar("sbox_maxeffects", "200",
	{ FCVAR_ARCHIVE, FCVAR_NOTIFY, FCVAR_REPLICATED, FCVAR_SERVER_CAN_EXECUTE },
	"Maximum effect props a single player can create")
local sbox_maxemitters = CreateConVar("sbox_maxemitters", "20",
	{ FCVAR_ARCHIVE, FCVAR_NOTIFY, FCVAR_REPLICATED, FCVAR_SERVER_CAN_EXECUTE },
	"Maximum emitters a single player can create")
local sbox_maxhoverballs = CreateConVar("sbox_maxhoverballs", "50",
	{ FCVAR_ARCHIVE, FCVAR_NOTIFY, FCVAR_REPLICATED, FCVAR_SERVER_CAN_EXECUTE },
	"Maximum hoverballs a single player can create")
local sbox_maxlamps = CreateConVar("sbox_maxlamps", "3",
	{ FCVAR_ARCHIVE, FCVAR_NOTIFY, FCVAR_REPLICATED, FCVAR_SERVER_CAN_EXECUTE },
	"Maximum lamps a single player can create")
local sbox_maxlights = CreateConVar("sbox_maxlights", "5",
	{ FCVAR_ARCHIVE, FCVAR_NOTIFY, FCVAR_REPLICATED, FCVAR_SERVER_CAN_EXECUTE },
	"Maximum lights a single player can create")
local sbox_maxnpcs = CreateConVar("sbox_maxnpcs", "10",
	{ FCVAR_ARCHIVE, FCVAR_NOTIFY, FCVAR_REPLICATED, FCVAR_SERVER_CAN_EXECUTE },
	"Maximum NPCs a single player can create")
local sbox_maxprops = CreateConVar("sbox_maxprops", "200",
	{ FCVAR_ARCHIVE, FCVAR_NOTIFY, FCVAR_REPLICATED, FCVAR_SERVER_CAN_EXECUTE },
	"Maximum props a single player can create")
local sbox_maxragdolls = CreateConVar("sbox_maxragdolls", "10",
	{ FCVAR_ARCHIVE, FCVAR_NOTIFY, FCVAR_REPLICATED, FCVAR_SERVER_CAN_EXECUTE },
	"Maximum ragdolls a single player can create")
local sbox_maxsents = CreateConVar("sbox_maxsents", "100",
	{ FCVAR_ARCHIVE, FCVAR_NOTIFY, FCVAR_REPLICATED, FCVAR_SERVER_CAN_EXECUTE },
	"Maximum entities a single player can create")
local sbox_maxthrusters = CreateConVar("sbox_maxthrusters", "50",
	{ FCVAR_ARCHIVE, FCVAR_NOTIFY, FCVAR_REPLICATED, FCVAR_SERVER_CAN_EXECUTE },
	"Maximum thrusters a single player can create")
local sbox_maxvehicles = CreateConVar("sbox_maxvehicles", "4",
	{ FCVAR_ARCHIVE, FCVAR_NOTIFY, FCVAR_REPLICATED, FCVAR_SERVER_CAN_EXECUTE },
	"Maximum vehicles a single player can create")
local sbox_maxwheels = CreateConVar("sbox_maxwheels", "50",
	{ FCVAR_ARCHIVE, FCVAR_NOTIFY, FCVAR_REPLICATED, FCVAR_SERVER_CAN_EXECUTE },
	"Maximum wheels a single player can create")
local sbox_noclip = CreateConVar("sbox_noclip", "1",
	{ FCVAR_ARCHIVE, FCVAR_NOTIFY, FCVAR_REPLICATED, FCVAR_SERVER_CAN_EXECUTE },
	"If enabled, players will be able to use noclip")
local sbox_persist = CreateConVar("sbox_persist", "1",
	{ FCVAR_ARCHIVE, FCVAR_NOTIFY, FCVAR_REPLICATED, FCVAR_SERVER_CAN_EXECUTE },
	"If not empty, enables 'Make Persistent' option when you right click on props while holding C, allowing you to save them across")
local sbox_playershurtplayers = CreateConVar("sbox_playershurtplayers", "1",
	{ FCVAR_ARCHIVE, FCVAR_NOTIFY, FCVAR_REPLICATED, FCVAR_SERVER_CAN_EXECUTE },
	"If enabled, players will be able to hurt each other")
local sbox_weapons = CreateConVar("sbox_weapons", "1",
	{ FCVAR_ARCHIVE, FCVAR_NOTIFY, FCVAR_REPLICATED, FCVAR_SERVER_CAN_EXECUTE },
	"If enabled, each player will receive default Half-Life 2 weapons on each spawn")

local sv_defaultdeployspeed = CreateConVar("sv_defaultdeployspeed", "4",
	{ FCVAR_ARCHIVE, FCVAR_NOTIFY, FCVAR_REPLICATED, FCVAR_SERVER_CAN_EXECUTE },
	"Default deploy speed for weapons")

--[[
	Now that the compatibility libraries have been loaded, we can start changing hooks
	and adding commands
--]]
if (CLIENT) then
	ConsoleCommands.Add("+menu", function(client, command, arguments)
		hook.Run("OnSpawnMenuOpen")
	end)

	ConsoleCommands.Add("-menu", function(client, command, arguments)
		hook.Run("OnSpawnMenuClose")
	end)

	ConsoleCommands.Add("+menu_context", function(client, command, arguments)
		hook.Run("OnContextMenuOpen")
	end)

	ConsoleCommands.Add("-menu_context", function(client, command, arguments)
		hook.Run("OnContextMenuClose")
	end)

	hook.Add("LevelInitPostEntity", "GModCompatibility.CallInitPostEntityHook", function()
		hook.Run("InitPostEntity")
	end)

	hook.Add("HudDraw", "GModCompatibility.CallHUDPaintHook", function()
		hook.Run("HUDPaint")
	end)

	hook.Add("HudShouldDraw", "GModCompatibility.CallHUDShouldDrawHook", function()
		hook.Run("HUDShouldDraw")
	end)

	hook.Add("ScoreboardDraw", "GModCompatibility.CallHUDDrawScoreBoardHook", function()
		hook.Run("HUDDrawScoreBoard")
	end)
else
	hook.Add("ServerActivate", "GModCompatibility.CallInitPostEntityHook", function()
		hook.Run("InitPostEntity")
	end)

	hook.Add("ClientConnected", "GModCompatibility.CallPlayerConnectHook", function(name, ip)
		hook.Run("PlayerConnect", name, ip)
	end)
end

hook.Add("CanPhysgunPickup", "GModCompatibility.CallCanPhysgunPickupHook", function(client, entity)
	return hook.Run("PhysgunPickup", client, entity)
end)

hook.Add("Initialize", "GModCompatibility.CallInitializeHooks", function()
	hook.Run("CreateTeams")
	hook.Run("PreGamemodeLoaded")
	hook.Run("OnGamemodeLoaded")
	hook.Run("PostGamemodeLoaded")
end)

-- Also register gmod weapons and entities in our system
hook.Add("PreRegisterSWEP", "GModCompatibility.RegisterSWEP", function(weaponTable, className)
	weaponTable.Base = weaponTable.Base or "weapon_base"
	ScriptedWeapons.Register(weaponTable, className)
end)
hook.Add("PreRegisterSENT", "GModCompatibility.RegisterSENT", function(entityTable, className)
	entityTable.Base = entityTable.Base or "base_entity"
	ScriptedEntities.Register(entityTable, className)
end)

hook.Add("PostEntitiesLoaded", "GModCompatibility.CallOnLoaded", function()
	scripted_ents.OnLoaded()
	weapons.OnLoaded()
end)

hook.Add("PreEntityInitialize", "GModCompatibility.CallSetupDataTables", function(entity)
	if (entity.InstallDataTable) then
		entity:InstallDataTable()
	end

	if (entity.SetupDataTables) then
		entity:SetupDataTables()
	end
end)

--[[
	GModCompatibility shims: functions newer Garry's Mod code calls that Experiment doesn't provide
	(found by scanning garrysmod/lua and the base/sandbox gamemodes against Experiment's bindings).
--]]

-- Removed in Lua 5.4; math.atan takes (y, x) now
math.atan2 = math.atan2 or function(y, x)
	return math.atan(y, x or 1)
end

-- GMod's special teams are TEAM_UNASSIGNED = 1001 / TEAM_SPECTATOR = 1002, the engine's are 0 / 1
-- (shareddefs.h). The base gamemode does pl:SetTeam( TEAM_UNASSIGNED ) on join, which the engine
-- rejected ("CBasePlayer::ChangeTeam( 1001 ) - invalid team index"). Translate both ways.
do
	local toEngineTeam = {
		[TEAM_UNASSIGNED] = 0,
		[TEAM_SPECTATOR] = 1,
	}
	local toGModTeam = {
		[0] = TEAM_UNASSIGNED,
		[1] = TEAM_SPECTATOR,
	}

	local playerMeta = FindMetaTable("Player")
	local entityMeta = FindMetaTable("Entity")

	if (playerMeta and playerMeta.SetTeam) then
		local engineSetTeam = playerMeta.SetTeam

		playerMeta.SetTeam = function(self, teamId)
			return engineSetTeam(self, toEngineTeam[teamId] or teamId)
		end
	end

	if (entityMeta and entityMeta.Team) then
		local engineTeam = entityMeta.Team

		entityMeta.Team = function(self)
			local teamId = engineTeam(self)

			return toGModTeam[teamId] or teamId
		end
	end
end

-- Vector:Unpack()/Angle:Unpack() (used by GMod's extensions/vector.lua, e.g. Vector:ToColor)
do
	local vectorMeta = FindMetaTable("Vector")
	local angleMeta = FindMetaTable("Angle")

	if (vectorMeta and not vectorMeta.Unpack) then
		vectorMeta.Unpack = function(self) return self.x, self.y, self.z end
	end

	if (angleMeta and not angleMeta.Unpack) then
		angleMeta.Unpack = function(self) return self.p, self.y, self.r end
	end
end

-- net.WritePlayer/ReadPlayer are just entities on the wire
do
	local writerMeta = FindMetaTable("MessageWriter")
	local readerMeta = FindMetaTable("MessageReader")

	if (writerMeta and not writerMeta.WritePlayer) then
		writerMeta.WritePlayer = writerMeta.WriteEntity
	end

	if (readerMeta and not readerMeta.ReadPlayer) then
		readerMeta.ReadPlayer = readerMeta.ReadEntity
	end
end

if (SERVER) then
	-- GMod returns every entity with the name; Experiment's binding finds one at a time
	ents.FindByName = ents.FindByName or function(name)
		local found = {}
		local entity = Entities.FindByName(name)

		while (IsValid(entity)) do
			found[#found + 1] = entity
			entity = Entities.FindByName(name, entity)
		end

		return found
	end
end

if (CLIENT) then
	-- DModelPanel:DrawModel asks for the current scissor rect; Experiment can't query it,
	-- so report it as disabled (the model is drawn unclipped instead of erroring every frame)
	surface.GetScissorRect = surface.GetScissorRect or function()
		return false, 0, 0, 0, 0
	end

	-- Only used by derma to refresh every panel after a skin reload
	vgui.GetAll = vgui.GetAll or function()
		return {}
	end

	-- cam.Start/cam.End take a table describing a 2D or 3D view
	local camStartStack = {}

	cam.Start = cam.Start or function(data)
		data = data or {}

		if (data.type == "2D") then
			cam.Start2D()
		else
			cam.Start3D(data.origin, data.angles, data.fov, data.x, data.y, data.w, data.h, data.znear, data.zfar)
		end

		camStartStack[#camStartStack + 1] = data.type
	end

	cam.End = cam.End or function()
		local viewType = table.remove(camStartStack)

		if (viewType == "2D") then
			cam.End2D()
		else
			cam.End3D()
		end
	end
end

if (CLIENT) then
	--[[
		GMod-style named fonts: surface.CreateFont(name, data).
		surface is Experiment's Surfaces, whose CreateFont() takes no arguments, so every GMod font
		(DermaDefault, DermaLarge, all spawnmenu fonts...) became an empty font and its name was lost.
		Name lookups then fell through to ClientScheme, which doesn't have them, so text measured 0x0.
	--]]
	GModCompatibilityFonts = GModCompatibilityFonts or {}

	local FONT_FLAGS = {
		italic = 0x001,
		underline = 0x002,
		strikeout = 0x004,
		symbol = 0x008,
		rotary = 0x040,
		shadow = 0x080,
		additive = 0x100,
		outline = 0x200,
	}
	local FONT_FLAG_ANTIALIAS = 0x010
	local FONT_FLAG_GAUSSIANBLUR = 0x020

	local originalCreateFont = Surfaces.CreateFont

	function Surfaces.CreateFont(name, data)
		-- Experiment's own calls: no name, just a new font container
		if (type(name) ~= "string") then
			return originalCreateFont()
		end

		data = data or {}

		local flags = 0

		if (data.antialias ~= false) then
			flags = flags | FONT_FLAG_ANTIALIAS
		end

		if ((data.blursize or 0) > 0) then
			flags = flags | FONT_FLAG_GAUSSIANBLUR
		end

		for key, flag in pairs(FONT_FLAGS) do
			if (data[key]) then
				flags = flags | flag
			end
		end

		-- Recreating a font with the same name reuses its container, like GMod does
		local container = GModCompatibilityFonts[name] or originalCreateFont()

		Surfaces.SetFontGlyphSet(
			container,
			data.font or "Arial",
			math.floor(data.size or 13),
			math.floor(data.weight or 500),
			math.floor(data.blursize or 0),
			math.floor(data.scanlines or 0),
			flags
		)

		GModCompatibilityFonts[name] = container
	end

	-- Every name lookup (surface.SetFont, GetTextSize, GetFontTall, ...) goes through ResolveFont
	local originalResolveFont = Surfaces.ResolveFont

	function Surfaces.ResolveFont(font, scheme)
		if (type(font) == "string" and GModCompatibilityFonts[font]) then
			return GModCompatibilityFonts[font].font
		end

		return originalResolveFont(font, scheme)
	end

	-- Labels and text entries set fonts by scheme name in C++, which doesn't know GMod fonts either
	for _, metaName in ipairs({ "Label", "TextEntry" }) do
		local meta = FindMetaTable(metaName)

		if (meta and meta._OriginalSetFont) then
			local schemeSetFont = meta.SetFont
			local schemeGetFont = meta.GetFont

			local function setFont(self, font)
				local container = type(font) == "string" and GModCompatibilityFonts[font]

				if (container) then
					self._gmodFontName = font
					return self:_OriginalSetFont(container.font)
				end

				self._gmodFontName = nil
				return schemeSetFont(self, font)
			end

			meta.SetFont = setFont
			meta.SetFontInternal = setFont

			meta.GetFont = function(self)
				return self._gmodFontName or schemeGetFont(self)
			end
		end
	end
end

-- No Steam Workshop integration: answer like GMod does when nothing is available, instead of erroring
steamworks = steamworks or {
	GetList = function(type, tags, offset, numRetrieve, days, userID, callback)
		if (isfunction(callback)) then
			callback({ numresults = 0, totalresults = 0, results = {} })
		end
	end,
	FileInfo = function(workshopId, callback)
		if (isfunction(callback)) then
			callback(nil)
		end
	end,
	GetDownloadedItems = function()
		return {}
	end,
	IsSubscribed = function()
		return false
	end,
	ShouldMountAddon = function()
		return false
	end,
	RequestPlayerInfo = function() end,
	FileUserInfo = function() end,
	Download = function() end,
	DownloadUGC = function() end,
	SetFavorite = function() end,
	ViewFile = function() end,
	Vote = function() end,
}

-- Setup the Garry's Mod lua include path so Include can find scripts
-- No need to prefix search paths here, since Include already finds scripts using
-- the configured search paths.
package.IncludePath = "lua/;" .. package.IncludePath

-- Setup to search for modules in all search paths
local searchPathsString = Files.GetSearchPath("GAME")
local searchPaths = {}

for searchPath in searchPathsString:gmatch("([^;]+)") do
	searchPaths[#searchPaths + 1] = searchPath

	if (searchPath:find("%.bsp$") or searchPath:find("%.vpk$")) then
		continue
	end

	if (searchPath:sub(-1) ~= "/" and searchPath:sub(-1) ~= "\\") then
		searchPath = searchPath .. "/"
	end

	-- Prepend the searchpath to the lua dirs
	package.path = searchPath .. "lua/includes/modules/?.lua;" .. package.path

	if (Systems.IsWindows()) then
		package.cpath = searchPath .. "lua/bin/?.dll;" .. package.cpath
	elseif (Systems.IsLinux()) then
		package.cpath = searchPath .. "lua/bin/?.so;" .. package.cpath
	end
end

-- Now that the entire compatibility setup is done, we can execute the scripts in 'autorun' (shared), 'autorun/client' and 'autorun/server'.
local function includeFolder(folder)
	local files = file.Find(folder .. "/*.lua", "GAME")

	-- GMod runs autorun files in sorted order (addons rely on it, e.g. CPTBase's
	-- '!cpt_base.lua' must run first). file.Find here returns search-path order,
	-- and the same name can come from several paths, so sort and dedupe.
	local seen = {}
	local sorted = {}
	for _, fileName in ipairs(files) do
		local key = fileName:lower()
		if (not seen[key]) then
			seen[key] = true
			sorted[#sorted + 1] = fileName
		end
	end
	table.sort(sorted, function(a, b) return a:lower() < b:lower() end)

	for _, fileName in ipairs(sorted) do
		include(folder .. "/" .. fileName)
	end
end

-- We implement this ourselves, because the gmod util/color.lua messes up the metatable
include("sh_color.lua")

-- GMod colors are plain tables, so addons check type(col) == "table" (StormFox's
-- weather lerp, color arguments, ...). Ours are userdata, so report them as tables.
-- Experiment's own sh_init.lua keeps the original type() (it captures it locally).
do
	local COLOR_META = FindMetaTable("Color")
	local originalType = rawget(_G, "_SrcboxOriginalType") or type
	_SrcboxOriginalType = originalType

	function type(value)
		local valueType = originalType(value)

		if (valueType == "Color") then
			return "table"
		end

		return valueType
	end

	-- ...and stores its own fields on it (weapons.lua's TableInherit sets
	-- BaseClass on colors in weapon tables). Keep anything that isn't r/g/b/a
	-- beside the color instead of erroring with "read-only table".
	do
		local nativeIndex = rawget(COLOR_META, "_SrcboxIndex") or COLOR_META.__index
		local nativeNewIndex = rawget(COLOR_META, "_SrcboxNewIndex") or COLOR_META.__newindex
		COLOR_META._SrcboxIndex = nativeIndex
		COLOR_META._SrcboxNewIndex = nativeNewIndex

		local extraFields = setmetatable({}, { __mode = "k" })
		local colorFields = { r = true, g = true, b = true, a = true, [1] = true, [2] = true, [3] = true, [4] = true }

		COLOR_META.__newindex = function(color, key, value)
			if (colorFields[key]) then
				return nativeNewIndex(color, key, value)
			end

			local fields = extraFields[color]

			if (not fields) then
				fields = {}
				extraFields[color] = fields
			end

			fields[key] = value
		end

		COLOR_META.__index = function(color, key)
			local fields = extraFields[color]

			if (fields) then
				local value = fields[key]

				if (value ~= nil) then
					return value
				end
			end

			if (type(nativeIndex) == "function") then
				return nativeIndex(color, key)
			end

			return nativeIndex[key]
		end
	end

	-- Code that then treats the color as a table iterates it
	COLOR_META.__pairs = function(color)
		local fields = { "r", "g", "b", "a" }
		local index = 0

		return function()
			index = index + 1
			local key = fields[index]

			if (key) then
				return key, color[key]
			end
		end, color, nil
	end
end

--[[
	Load the Garry's Mod init scripts and panels
--]]

-- Let's setup a temporary filter so we don't load the extensions and modules we implemented ourselves.
local filter = {
	require = {
		["concommand"] = true,
		["hook"] = true,
		["gamemode"] = true,
		-- ["weapons"] = true,
		-- ["scripted_ents"] = true,
		["usermessage"] = true,
	},
	include = {
		["extensions/net.lua"] = true, -- We implement networking using luasocket
		["extensions/file.lua"] = true, -- Our filesystem works slightly different
		["util/color.lua"] = true, -- In contrast with gmod, we should properly get metatables everywhere (so don't need this hack util)
	},
}

local originalRequire = require
local originalInclude = Include

-- Load each Garry's Mod module/file in isolation. Without this, a single failure (for example
-- a constant a newer GMod update started using) aborts the rest of includes/init.lua, and every
-- library after it (math/util/entity extensions, vgui.Register, spawnmenu, search, ...) goes missing.
local function reportLoadError(kind, name, err)
	ErrorNoHalt("[GModCompatibility] Failed to " .. kind .. " '" .. tostring(name) .. "': " .. tostring(err) .. "\n")
end

local function protectedLoad(kind, loader, name)
	local results = table.pack(xpcall(loader, debug.traceback, name))

	if (not results[1]) then
		reportLoadError(kind, name, results[2])
		return
	end

	return table.unpack(results, 2, results.n)
end

require = function(name)
	if (not filter.require[name]) then
		return protectedLoad("require", originalRequire, name)
	end
end

include = function(name)
	if (not filter.include[name]) then
		return protectedLoad("include", originalInclude, name)
	end
end

-- Newer GMod util.lua redefines isentity() to check a GMod-only metatable field (MetaName == "Entity"),
-- which Experiment's entities don't have, so every real entity failed it (e.g. ent:SetCreator(ply) in gm_spawn).
local experimentIsEntity = isentity

include("includes/init.lua")

include = originalInclude

-- table.Copy on a color (now reported as a table) has to make a new color, not a
-- table carrying the userdata metatable
if (table.Copy and not table._SrcboxOriginalCopy) then
	table._SrcboxOriginalCopy = table.Copy

	function table.Copy(value, lookupTable)
		if (IsColor(value)) then
			return Color(value.r, value.g, value.b, value.a)
		end

		return table._SrcboxOriginalCopy(value, lookupTable)
	end
end

-- Keep GMod's own versions of these modules out for good. Our replacements are
-- already the globals; if an addon later called require("hook"), GMod's hook.lua
-- would load and swap out the global 'hook' table, silently dropping every hook
-- registered before it (gamemode, spawn menu, ...).
local keepOurModule = {
	["hook"] = true,
	["concommand"] = true,
	["gamemode"] = true,
	["usermessage"] = true,
}

for name in pairs(keepOurModule) do
	if (rawget(_G, name) ~= nil) then
		package.loaded[name] = rawget(_G, name)
	end
end

require = function(name, ...)
	if (keepOurModule[name] and rawget(_G, name) ~= nil) then
		return rawget(_G, name)
	end

	return originalRequire(name, ...)
end

if (experimentIsEntity) then
	isentity = experimentIsEntity
	IsEntity = experimentIsEntity
end

if (CLIENT) then
	Panels.HTML = Panels.Html

	--[[
		Load VGUI
	--]]

	include("derma/init.lua")

	-- include("includes/vgui_base.lua") -- I don't think this is actually loaded in Garry's Mod since it's missing a bunch of includes
	-- So let's just include all files inside lua/vgui/
	includeFolder("lua/vgui")

	include("cl_awesomium.lua")

	include("skins/default.lua")

	require("notification")


	--[[
		Srcbox: GMod rendering API that isn't native here (mesh, Mesh, ParticleEmitter,
		render.Draw*, the post-process textures, ...).
	--]]
	MATERIAL_POINTS = MATERIAL_POINTS or 0
	MATERIAL_LINES = MATERIAL_LINES or 1
	MATERIAL_TRIANGLES = MATERIAL_TRIANGLES or 2
	MATERIAL_TRIANGLE_STRIP = MATERIAL_TRIANGLE_STRIP or 3
	MATERIAL_LINE_STRIP = MATERIAL_LINE_STRIP or 4
	MATERIAL_LINE_LOOP = MATERIAL_LINE_LOOP or 5
	MATERIAL_POLYGON = MATERIAL_POLYGON or 6
	MATERIAL_QUADS = MATERIAL_QUADS or 7

	MATERIAL_FOG_NONE = MATERIAL_FOG_NONE or 0
	MATERIAL_FOG_LINEAR = MATERIAL_FOG_LINEAR or 1
	MATERIAL_FOG_LINEAR_BELOW_FOG_Z = MATERIAL_FOG_LINEAR_BELOW_FOG_Z or 2

	if (Meshes) then
		mesh = Meshes
		Mesh = Meshes.Create
	end

	if (ParticleEmitters) then
		ParticleEmitter = function(position, use3D)
			return ParticleEmitters.Create(position or Vector(0, 0, 0), use3D and true or false)
		end
	end

	if (RenderExtensions) then
		for name, func in pairs(RenderExtensions) do
			if (render[name] == nil) then
				render[name] = func
			end
		end

		render.ModelMaterialOverride = render.ModelMaterialOverride or RenderExtensions.MaterialOverride

		-- GMod has this on util, not render
		util.GetSunInfo = util.GetSunInfo or RenderExtensions.GetSunInfo
		render.MaterialOverrideByIndex = render.MaterialOverrideByIndex or function(index, material)
			-- Per-submaterial overrides aren't available; apply it to the whole model
			RenderExtensions.MaterialOverride(material)
		end
	end

	render.OverrideBlend = render.OverrideBlend or function() end
	render.OverrideBlendFunc = render.OverrideBlendFunc or function() end
	render.SetLightingMode = render.SetLightingMode or function() end
	render.SetLocalModelLights = render.SetLocalModelLights or function() end
	render.PushFlashlightMode = render.PushFlashlightMode or function() end
	render.PopFlashlightMode = render.PopFlashlightMode or function() end
	render.SetLightmapTexture = render.SetLightmapTexture or function() end
	render.RedownloadAllLightmaps = render.RedownloadAllLightmaps or function() end
	render.SupportsPixelShaders_1_4 = render.SupportsPixelShaders_1_4 or function() return true end
	render.SupportsPixelShaders_2_0 = render.SupportsPixelShaders_2_0 or function() return true end
	render.SupportsVertexShaders_2_0 = render.SupportsVertexShaders_2_0 or function() return true end
	render.SupportsHDR = render.SupportsHDR or function() return true end
	render.GetAmbientLightColor = render.GetAmbientLightColor or function() return Vector(0, 0, 0) end

	-- GMod's GetScreenEffectTexture defaults to texture 0
	do
		local getScreenEffectTexture = Renders.GetScreenEffectTexture

		render.GetScreenEffectTexture = function(index)
			return getScreenEffectTexture(index or 0)
		end
	end

	-- Post-process render targets GMod creates for its own effects. We make our own
	-- (created on first use) or find the engine's when it has one.
	do
		local RT_SIZE_DEFAULT = 1
		local RT_SIZE_FULL_FRAME_BUFFER = 4
		local MATERIAL_RT_DEPTH_SHARED = 0
		local MATERIAL_RT_DEPTH_NONE = 2
		local cache = {}

		local function makeTarget(name, fullFrame, divisor)
			if (cache[name]) then
				return cache[name]
			end

			local texture

			if (fullFrame) then
				texture = Renders.CreateRenderTargetTextureEx(name, ScrW(), ScrH(), RT_SIZE_FULL_FRAME_BUFFER, MATERIAL_RT_DEPTH_SHARED)
			else
				texture = Renders.CreateRenderTargetTextureEx(name, math.max(1, math.floor(ScrW() / divisor)), math.max(1, math.floor(ScrH() / divisor)), RT_SIZE_DEFAULT, MATERIAL_RT_DEPTH_NONE)
			end

			cache[name] = texture
			return texture
		end

		local function findTexture(...)
			for _, name in ipairs({ ... }) do
				local texture = Renders.FindTexture and Renders.FindTexture(name)

				if (texture) then
					return texture
				end
			end
		end

		render.GetMoBlurTex0 = render.GetMoBlurTex0 or function() return makeTarget("_rt_gmodcompat_moblur0", true) end
		render.GetMoBlurTex1 = render.GetMoBlurTex1 or function() return makeTarget("_rt_gmodcompat_moblur1", true) end
		render.GetSuperFPTex = render.GetSuperFPTex or function() return makeTarget("_rt_gmodcompat_superfp0", true) end
		render.GetSuperFPTex2 = render.GetSuperFPTex2 or function() return makeTarget("_rt_gmodcompat_superfp1", true) end
		render.GetBloomTex0 = render.GetBloomTex0 or function() return findTexture("_rt_SmallFB0") or makeTarget("_rt_gmodcompat_bloom0", false, 4) end
		render.GetBloomTex1 = render.GetBloomTex1 or function() return findTexture("_rt_SmallFB1") or makeTarget("_rt_gmodcompat_bloom1", false, 4) end
		render.GetSmallTex0 = render.GetSmallTex0 or function() return findTexture("_rt_SmallFB0") or makeTarget("_rt_gmodcompat_small0", false, 4) end
		render.GetSmallTex1 = render.GetSmallTex1 or function() return findTexture("_rt_SmallFB1") or makeTarget("_rt_gmodcompat_small1", false, 4) end
		render.GetPowerOfTwoTexture = render.GetPowerOfTwoTexture or function() return findTexture("_rt_PowerOfTwoFB") end
		render.GetRefractionTexture = render.GetRefractionTexture or function() return findTexture("_rt_WaterRefraction", "_rt_PowerOfTwoFB") end
		render.GetResolvedFullFrameDepth = render.GetResolvedFullFrameDepth or function() return findTexture("_rt_ResolvedFullFrameDepth", "_rt_FullFrameDepth") end
		render.GetFullScreenDepthTexture = render.GetFullScreenDepthTexture or function() return findTexture("_rt_FullFrameDepth", "_rt_ResolvedFullFrameDepth") end
	end

	-- Materials for the colour-only helpers (vertex coloured, unlit)
	do
		local colorMaterials = {}

		local function getColorMaterial(ignoreZ)
			local key = ignoreZ and "ignorez" or "normal"

			if (not colorMaterials[key]) then
				colorMaterials[key] = CreateMaterial("gmodcompat_color_" .. key, "UnlitGeneric", {
					["$basetexture"] = "color/white",
					["$vertexcolor"] = 1,
					["$vertexalpha"] = 1,
					["$translucent"] = 1,
					["$ignorez"] = ignoreZ and 1 or 0,
				})
			end

			return colorMaterials[key]
		end

		render.SetColorMaterial = render.SetColorMaterial or function()
			render.SetMaterial(getColorMaterial(false))
		end

		render.SetColorMaterialIgnoreZ = render.SetColorMaterialIgnoreZ or function()
			render.SetMaterial(getColorMaterial(true))
		end

		local function colorComponents(color)
			if (color == nil) then
				return 255, 255, 255, 255
			end

			return color.r or 255, color.g or 255, color.b or 255, color.a or 255
		end

		local function localToWorld(position, angles, x, y, z)
			-- Source axes: x forward, y left, z up
			return position + angles:Forward() * x - angles:Right() * y + angles:Up() * z
		end

		render.DrawLine = render.DrawLine or function(startPos, endPos, color, writeZ)
			render.SetMaterial(getColorMaterial(not writeZ))
			local r, g, b, a = colorComponents(color)

			mesh.Begin(MATERIAL_LINES, 1)
			mesh.Color(r, g, b, a)
			mesh.Position(startPos)
			mesh.AdvanceVertex()
			mesh.Color(r, g, b, a)
			mesh.Position(endPos)
			mesh.AdvanceVertex()
			mesh.End()
		end

		render.DrawQuad = render.DrawQuad or function(v1, v2, v3, v4, color)
			local r, g, b, a = colorComponents(color)

			mesh.Begin(MATERIAL_QUADS, 1)
			for i, vertex in ipairs({ v1, v2, v3, v4 }) do
				local u = (i == 2 or i == 3) and 1 or 0
				local v = (i >= 3) and 1 or 0
				mesh.Color(r, g, b, a)
				mesh.TexCoord(0, u, v)
				mesh.Position(vertex)
				mesh.AdvanceVertex()
			end
			mesh.End()
		end

		render.DrawQuadEasy = render.DrawQuadEasy or function(position, normal, width, height, color, rotation)
			local r, g, b, a = colorComponents(color)
			local angles = normal:Angle()

			if (rotation and rotation ~= 0) then
				angles:RotateAroundAxis(angles:Forward(), rotation)
			end

			local right = angles:Right() * (width * 0.5)
			local up = angles:Up() * (height * 0.5)

			mesh.Begin(MATERIAL_QUADS, 1)
			local corners = {
				{ position - right + up, 0, 0 },
				{ position + right + up, 1, 0 },
				{ position + right - up, 1, 1 },
				{ position - right - up, 0, 1 },
			}
			for _, corner in ipairs(corners) do
				mesh.Color(r, g, b, a)
				mesh.TexCoord(0, corner[2], corner[3])
				mesh.Normal(normal)
				mesh.Position(corner[1])
				mesh.AdvanceVertex()
			end
			mesh.End()
		end

		local function boxCorners(position, angles, mins, maxs)
			local corners = {}

			for i = 0, 7 do
				local x = (i % 2 == 0) and mins.x or maxs.x
				local y = (math.floor(i / 2) % 2 == 0) and mins.y or maxs.y
				local z = (math.floor(i / 4) == 0) and mins.z or maxs.z
				corners[i] = localToWorld(position, angles, x, y, z)
			end

			return corners
		end

		local boxFaces = {
			{ 0, 2, 3, 1 }, { 4, 5, 7, 6 }, -- bottom, top
			{ 0, 1, 5, 4 }, { 2, 6, 7, 3 }, -- -y, +y
			{ 0, 4, 6, 2 }, { 1, 3, 7, 5 }, -- -x, +x
		}

		render.DrawBox = render.DrawBox or function(position, angles, mins, maxs, color)
			local r, g, b, a = colorComponents(color)
			local corners = boxCorners(position, angles, mins, maxs)
			local uvs = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } }

			mesh.Begin(MATERIAL_QUADS, 6)
			for _, face in ipairs(boxFaces) do
				for i, index in ipairs(face) do
					mesh.Color(r, g, b, a)
					mesh.TexCoord(0, uvs[i][1], uvs[i][2])
					mesh.Position(corners[index])
					mesh.AdvanceVertex()
				end
			end
			mesh.End()
		end

		local boxEdges = {
			{ 0, 1 }, { 1, 3 }, { 3, 2 }, { 2, 0 },
			{ 4, 5 }, { 5, 7 }, { 7, 6 }, { 6, 4 },
			{ 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 },
		}

		render.DrawWireframeBox = render.DrawWireframeBox or function(position, angles, mins, maxs, color, writeZ)
			render.SetMaterial(getColorMaterial(not writeZ))
			local r, g, b, a = colorComponents(color)
			local corners = boxCorners(position, angles, mins, maxs)

			mesh.Begin(MATERIAL_LINES, #boxEdges)
			for _, edge in ipairs(boxEdges) do
				for _, index in ipairs(edge) do
					mesh.Color(r, g, b, a)
					mesh.Position(corners[index])
					mesh.AdvanceVertex()
				end
			end
			mesh.End()
		end

		local function spherePoint(position, radius, latitude, longitude)
			local sinLat, cosLat = math.sin(latitude), math.cos(latitude)
			return position + Vector(math.cos(longitude) * sinLat, math.sin(longitude) * sinLat, cosLat) * radius
		end

		render.DrawSphere = render.DrawSphere or function(position, radius, longitudeSteps, latitudeSteps, color)
			local r, g, b, a = colorComponents(color)
			longitudeSteps = math.max(3, math.floor(longitudeSteps or 16))
			latitudeSteps = math.max(2, math.floor(latitudeSteps or 16))

			mesh.Begin(MATERIAL_QUADS, longitudeSteps * latitudeSteps)
			for lat = 0, latitudeSteps - 1 do
				local lat1 = math.pi * lat / latitudeSteps
				local lat2 = math.pi * (lat + 1) / latitudeSteps

				for lon = 0, longitudeSteps - 1 do
					local lon1 = 2 * math.pi * lon / longitudeSteps
					local lon2 = 2 * math.pi * (lon + 1) / longitudeSteps
					local quad = {
						{ lat1, lon1 }, { lat1, lon2 }, { lat2, lon2 }, { lat2, lon1 },
					}

					for _, point in ipairs(quad) do
						local vertex = spherePoint(position, radius, point[1], point[2])
						local normal = (vertex - position)
						normal:NormalizeInPlace()
						mesh.Color(r, g, b, a)
						mesh.Normal(normal)
						mesh.TexCoord(0, point[2] / (2 * math.pi), point[1] / math.pi)
						mesh.Position(vertex)
						mesh.AdvanceVertex()
					end
				end
			end
			mesh.End()
		end

		render.DrawWireframeSphere = render.DrawWireframeSphere or function(position, radius, longitudeSteps, latitudeSteps, color, writeZ)
			render.SetMaterial(getColorMaterial(not writeZ))
			local r, g, b, a = colorComponents(color)
			longitudeSteps = math.max(3, math.floor(longitudeSteps or 16))
			latitudeSteps = math.max(2, math.floor(latitudeSteps or 16))

			mesh.Begin(MATERIAL_LINES, longitudeSteps * latitudeSteps * 2)
			for lat = 0, latitudeSteps - 1 do
				local lat1 = math.pi * lat / latitudeSteps
				local lat2 = math.pi * (lat + 1) / latitudeSteps

				for lon = 0, longitudeSteps - 1 do
					local lon1 = 2 * math.pi * lon / longitudeSteps
					local lon2 = 2 * math.pi * (lon + 1) / longitudeSteps
					local lines = {
						{ { lat1, lon1 }, { lat1, lon2 } },
						{ { lat1, lon1 }, { lat2, lon1 } },
					}

					for _, line in ipairs(lines) do
						for _, point in ipairs(line) do
							mesh.Color(r, g, b, a)
							mesh.Position(spherePoint(position, radius, point[1], point[2]))
							mesh.AdvanceVertex()
						end
					end
				end
			end
			mesh.End()
		end

		-- render.StartBeam / AddBeam / EndBeam: camera-facing strip through the points
		local beamPoints = nil
		local beamExpected = 0

		render.StartBeam = render.StartBeam or function(segments)
			beamPoints = {}
			beamExpected = math.max(0, math.floor(segments or 0))
		end

		render.AddBeam = render.AddBeam or function(position, width, textureEnd, color)
			if (not beamPoints) then
				return
			end

			beamPoints[#beamPoints + 1] = { position, width, textureEnd, color }
		end

		render.EndBeam = render.EndBeam or function()
			local points = beamPoints
			beamPoints = nil

			if (not points or #points < 2) then
				return
			end

			local eye = EyePos()

			mesh.Begin(MATERIAL_QUADS, #points - 1)
			for i = 1, #points - 1 do
				local a, b = points[i], points[i + 1]
				local direction = b[1] - a[1]
				local toEye = eye - a[1]
				local side = direction:Cross(toEye)
				side:NormalizeInPlace()

				local sideA = side * (a[2] * 0.5)
				local sideB = side * (b[2] * 0.5)
				local ar, ag, ab, aa = colorComponents(a[4])
				local br, bg, bb, ba = colorComponents(b[4])

				mesh.Color(ar, ag, ab, aa) mesh.TexCoord(0, 0, a[3]) mesh.Position(a[1] + sideA) mesh.AdvanceVertex()
				mesh.Color(br, bg, bb, ba) mesh.TexCoord(0, 0, b[3]) mesh.Position(b[1] + sideB) mesh.AdvanceVertex()
				mesh.Color(br, bg, bb, ba) mesh.TexCoord(0, 1, b[3]) mesh.Position(b[1] - sideB) mesh.AdvanceVertex()
				mesh.Color(ar, ag, ab, aa) mesh.TexCoord(0, 1, a[3]) mesh.Position(a[1] - sideA) mesh.AdvanceVertex()
			end
			mesh.End()
		end

		-- Draws a texture over (part of) the screen
		local screenMaterials = {}

		local function getScreenMaterial(texture)
			local name = texture:GetName()

			if (not screenMaterials[name]) then
				local material = CreateMaterial("gmodcompat_screen_" .. (#table.GetKeys(screenMaterials) + 1), "UnlitGeneric", {
					["$basetexture"] = name,
					["$vertexcolor"] = 1,
					["$vertexalpha"] = 1,
					["$ignorez"] = 1,
				})
				screenMaterials[name] = material
			end

			return screenMaterials[name]
		end

		render.DrawTextureToScreen = render.DrawTextureToScreen or function(texture)
			render.SetMaterial(getScreenMaterial(texture))
			render.DrawScreenQuad()
		end

		render.DrawTextureToScreenRect = render.DrawTextureToScreenRect or function(texture, x, y, width, height)
			render.SetMaterial(getScreenMaterial(texture))
			render.DrawScreenQuadEx(x, y, width, height)
		end
	end

	render.GetViewSetup = render.GetViewSetup or function()
		return {
			origin = EyePos(),
			angles = EyeAngles(),
			fov = (LocalPlayer and IsValid(LocalPlayer()) and LocalPlayer():GetFOV()) or 90,
			x = 0,
			y = 0,
			width = ScrW(),
			height = ScrH(),
			znear = 3,
			zfar = 30000,
			aspect = ScrW() / ScrH(),
		}
	end

	-- surface.DrawTexturedRectRotated: a rotated textured quad around its centre
	surface.DrawTexturedRectRotated = function(x, y, width, height, rotation)
		local radians = math.rad(rotation or 0)
		local cosine, sine = math.cos(radians), math.sin(radians)
		local halfWidth, halfHeight = width * 0.5, height * 0.5
		local corners = {
			{ -halfWidth, -halfHeight, 0, 0 },
			{ halfWidth, -halfHeight, 1, 0 },
			{ halfWidth, halfHeight, 1, 1 },
			{ -halfWidth, halfHeight, 0, 1 },
		}
		local polygon = {}

		for i, corner in ipairs(corners) do
			-- GMod rotates counter-clockwise on screen
			polygon[i] = {
				x = x + corner[1] * cosine + corner[2] * sine,
				y = y - corner[1] * sine + corner[2] * cosine,
				u = corner[3],
				v = corner[4],
			}
		end

		Surfaces.DrawTexturedPolygon(polygon)
	end

	-- GMod's engine loads lua/postprocess/*.lua by itself on the client
	-- (DrawColorModify, DrawBloom, DrawSunbeams, ... and their spawnmenu entries)
	includeFolder("lua/postprocess")

	-- These draw full-screen quads with GMod-only shaders. When the shader doesn't
	-- exist here the material falls back to an error/wireframe shader, and drawing
	-- it over the whole screen ruins the frame - so leave those effects off.
	local postProcessMaterials = {
		DrawColorModify = "pp/colour",
		DrawBloom = "pp/bloom",
		DrawSharpen = "pp/sharpen",
		DrawSobel = "pp/sobel",
		DrawSunbeams = "pp/sunbeams",
		DrawMotionBlur = "pp/motionblur",
		DrawBokehDOF = "pp/bokehblur",
		DrawTexturize = "pp/texturize",
		DrawToyTown = "pp/toytown-top",
	}

	for functionName, materialName in pairs(postProcessMaterials) do
		if (type(_G[functionName]) == "function") then
			local ok, material = pcall(Material, materialName)
			local usable = ok and material ~= nil

			if (usable and material.IsErrorMaterial and material:IsErrorMaterial()) then
				usable = false
			end

			if (usable and material.GetShaderName) then
				local shader = tostring(material:GetShaderName() or ""):lower()

				if (shader == "" or shader:find("wireframe", 1, true)) then
					usable = false
				end
			end

			if (not usable) then
				MsgN("[GModCompatibility] " .. functionName .. " disabled: its shader (" .. materialName .. ") isn't available")
				_G[functionName] = function() end
			end
		end
	end

	-- A post-process file that failed to load still shouldn't take its callers down
	for functionName in pairs(postProcessMaterials) do
		if (type(_G[functionName]) ~= "function") then
			MsgN("[GModCompatibility] " .. functionName .. " isn't available; it will do nothing")
			_G[functionName] = function() end
		end
	end

	DrawMaterialOverlay = DrawMaterialOverlay or function() end
end

if (SERVER) then
	PLAYER_META.IPAddress = PLAYER_META.GetIpAddress
	PLAYER_META.UnLock = PLAYER_META.Unlock

	-- AllowFlashlight and CanUseFlashlight override default gmod implementations, which is why its placed after include("includes/init.lua")
	-- https://github.com/Facepunch/garrysmod/blob/1ce3b6fec3417e4798ade0862540da74ce612483/garrysmod/lua/includes/extensions/player.lua#L182C70-L183C71
	function PLAYER_META:AllowFlashlight(canFlashlight)
		self:SetFlashlightEnabled(canFlashlight)
	end

	function PLAYER_META:CanUseFlashlight()
		return self:IsFlashlightEnabled()
	end
end

--[[
	Addon hooks run in isolation.

	GMod's hook.Call stops at the first hook that errors, which also skips every
	hook after it - including the gamemode's (spawn menu, HUD, ...). Addons hit
	gaps in this compatibility layer far more often than in GMod, so one broken
	addon could take down unrelated features. Each hook added from here on is
	wrapped: an error is reported (a few times, then muted) and the call moves on.
--]]
do
	local originalHookAdd = hook.Add
	local errorCounts = {}
	local MAX_REPORTS = 3

	local function reportHookError(eventName, identifier, err)
		local key = tostring(eventName) .. " / " .. tostring(identifier)
		local count = (errorCounts[key] or 0) + 1
		errorCounts[key] = count

		if (count <= MAX_REPORTS) then
			ErrorNoHalt("[GModCompatibility] Hook '" .. key .. "' failed: " .. tostring(err) .. "\n")

			if (count == MAX_REPORTS) then
				ErrorNoHalt("[GModCompatibility] Further errors from hook '" .. key .. "' are muted\n")
			end
		end
	end

	--[[
		Render state left behind by a failing hook. If an addon pushes a render
		target or starts a camera and then errors before popping it, every later
		frame keeps drawing into that state - typically a black screen. We track
		the outermost push/start calls and undo whatever a failed hook left open.
	--]]
	local openStates = {}

	local function unwindTo(depth)
		while (#openStates > depth) do
			local finish = table.remove(openStates)
			pcall(finish)
		end
	end

	if (CLIENT) then
		local insideTracked = false

		local function track(library, startName, finishName)
			local start = library and library[startName]
			local finish = library and library[finishName]

			if (type(start) ~= "function" or type(finish) ~= "function") then
				return
			end

			library[startName] = function(...)
				if (insideTracked) then
					return start(...)
				end

				insideTracked = true
				local results = table.pack(pcall(start, ...))
				insideTracked = false

				if (not results[1]) then
					error(results[2], 2)
				end

				openStates[#openStates + 1] = finish
				return table.unpack(results, 2, results.n)
			end

			library[finishName] = function(...)
				if (insideTracked) then
					return finish(...)
				end

				for index = #openStates, 1, -1 do
					if (openStates[index] == finish) then
						table.remove(openStates, index)
						break
					end
				end

				insideTracked = true
				local results = table.pack(pcall(finish, ...))
				insideTracked = false

				if (not results[1]) then
					error(results[2], 2)
				end

				return table.unpack(results, 2, results.n)
			end
		end

		-- (render targets are recovered natively at the start of each frame, see lrender.cpp)
		track(render, "PushCustomClipPlane", "PopCustomClipPlane")
		track(cam, "Start", "End")
		track(cam, "Start2D", "End2D")
		track(cam, "Start3D", "End3D")
		track(cam, "Start3D2D", "End3D2D")
		track(cam, "PushModelMatrix", "PopModelMatrix")
	end

	-- Debugging: gmodcompat_mute_event <event> skips every addon hook for that
	-- event (run again to unmute); gmodcompat_list_hooks <event> lists them.
	local mutedEvents = {}

	if (concommand and concommand.Add) then
		local prefix = CLIENT and "cl_" or ""

		concommand.Add(prefix .. "gmodcompat_mute_event", function(player, command, arguments)
			local eventName = arguments[1]

			if (not eventName) then
				print("Usage: " .. command .. " <event name>. Muted now:")
				for name in pairs(mutedEvents) do print("  " .. name) end
				return
			end

			mutedEvents[eventName] = not mutedEvents[eventName] or nil
			print("[GModCompatibility] Hooks for '" .. eventName .. "' are now " .. (mutedEvents[eventName] and "MUTED" or "active"))
		end)

		concommand.Add(prefix .. "gmodcompat_list_hooks", function(player, command, arguments)
			local hooks = hook.GetTable()
			local eventName = arguments[1]

			for name, list in pairs(hooks) do
				if (not eventName or name:lower() == eventName:lower()) then
					for identifier in pairs(list) do
						print(name .. "  ->  " .. tostring(identifier))
					end
				end
			end
		end)
	end

	hook.Add = function(eventName, identifier, func, ...)
		if (type(func) == "function") then
			local original = func

			func = function(...)
				if (mutedEvents[eventName]) then
					return
				end

				local depth = #openStates
				local results = table.pack(xpcall(original, debug.traceback, ...))

				if (not results[1]) then
					unwindTo(depth)
					reportHookError(eventName, identifier, results[2])
					return
				end

				return table.unpack(results, 2, results.n)
			end
		end

		return originalHookAdd(eventName, identifier, func, ...)
	end
end

--[[
	GMod engine convars that addons read straight away with GetConVar(...):Get*()
--]]
for name, default in pairs({
	physgun_wheelspeed = "10",
	ai_disabled = "0",
	ai_ignoreplayers = "0",
	-- Addons set these with RunConsoleCommand; GMod has them, we don't use them
	physcannon_instant = "0",
	cl_tree_sway_dir = "0",
}) do
	if (not GetConVar(name)) then
		CreateConVar(name, default, FCVAR_ARCHIVE)
	end
end

-- GMod spells the bodygroup functions "Bodygroup"; ours are "BodyGroup".
do
	local aliases = {
		GetBodygroup = "GetBodyGroup",
		SetBodygroup = "SetBodyGroup",
		GetBodygroupCount = "GetBodyGroupCount",
		GetBodygroupName = "GetBodyGroupName",
		FindBodygroupByName = "FindBodyGroupByName",
		GetNumBodyGroups = "GetBodyGroupsCount",
	}

	for _, metaName in ipairs({ "Entity", "CBaseAnimating" }) do
		local meta = FindMetaTable(metaName)

		if (meta) then
			for gmodName, ourName in pairs(aliases) do
				if (rawget(meta, gmodName) == nil and meta[ourName]) then
					meta[gmodName] = meta[ourName]
				end
			end
		end
	end
end

-- GMod's GetSpawnFlags is shared, ours only exists on the server. The client doesn't know
-- the flags, so report none (property filters call this on the client).
if (CLIENT and not ENTITY_META.GetSpawnFlags) then
	function ENTITY_META:GetSpawnFlags()
		return 0
	end
end

-- properties.OpenEntityMenu runs every property's Filter while building the menu, and it only
-- calls menu:Open() at the end. One Filter erroring (an addon property using a function we
-- lack) aborted that, leaving the half-built menu in the top-left corner. Treat an erroring
-- Filter as "not applicable" instead.
if (properties and properties.Add) then
	local function guardFilter(name, property)
		if (not istable(property) or not isfunction(property.Filter) or property._SrcboxFilterGuarded) then
			return
		end

		local originalFilter = property.Filter
		property._SrcboxFilterGuarded = true

		property.Filter = function(...)
			local success, result = pcall(originalFilter, ...)

			if (not success) then
				ErrorNoHalt("[GModCompatibility] Property '" .. tostring(name) .. "' Filter failed: " .. tostring(result) .. "\n")
				return false
			end

			return result
		end
	end

	local originalAdd = properties._SrcboxOriginalAdd or properties.Add
	properties._SrcboxOriginalAdd = originalAdd

	function properties.Add(name, property)
		guardFilter(name, property)
		return originalAdd(name, property)
	end

	if (istable(properties.List)) then
		for name, property in pairs(properties.List) do
			guardFilter(name, property)
		end
	end
end

--[[
	Load the autorun scripts for Garry's Mod addons/etc.
--]]
includeFolder("lua/autorun")

if (CLIENT) then
	includeFolder("lua/autorun/client")
else
	includeFolder("lua/autorun/server")
end

--[[
	For all search paths, ensure the entities and weapons are loaded, after all autorun scripts have been loaded.
--]]

-- Gamemode weapons and entities are added to the search path by the loader
-- This adds the lua/ folder to the search path so Experiment will load the entities and weapons from it
Files.AddSearchPath("lua/", "LUA")

-- Srcbox: mounted .gma addons live under <mod>/cache/gma_mount/. GMod's "LUA"
-- path covers their lua/ folder too, and Experiment loads scripted entities and
-- weapons (lua/entities, lua/weapons) from the "LUA" path, so add it there.
for _, searchPath in ipairs(searchPaths) do
	local normalized = searchPath:gsub("\\", "/"):lower()

	if (normalized:find("gma_mount", 1, true) and not normalized:find("%.vpk$")) then
		local directory = searchPath

		if (directory:sub(-1) ~= "/" and directory:sub(-1) ~= "\\") then
			directory = directory .. "/"
		end

		Files.AddSearchPath(directory .. "lua/", "LUA")
	end
end

-- Copy ents from our system to the GMod system, making sure their bases use the GMod base
-- We need to do this because these don't explicitly set '.Base' since they expect Garry's Mod
-- to set the default for them.
local differentBase = {
	-- Weapons
	["gmod_tool"] = "weapon_base",
	["gmod_camera"] = "weapon_base",
	["manhack_welder"] = "weapon_base",
	["weapon_fists"] = "weapon_base",
	["weapon_flechettegun"] = "weapon_base",
	["weapon_medkit"] = "weapon_base",

	-- Entities
	["base_edit"] = "base_entity",
	["edit_fog"] = false,     -- "base_edit",
	["edit_sky"] = false,     -- "base_edit",
	["base_gmodentity"] = false, -- "base_anim",
	["gmod_anchor"] = false,  -- "base_anim",
	["gmod_balloon"] = "base_entity",
	["gmod_button"] = "base_entity",
	["gmod_cameraprop"] = false, -- "base_anim",
	["gmod_dynamite"] = "base_entity",
	["gmod_emitter"] = "base_entity",
	["gmod_ghost"] = false, -- "base_anim",
	["gmod_hoverball"] = "base_entity",
	["gmod_lamp"] = "base_entity",
	["gmod_light"] = "base_entity",
	["gmod_thruster"] = "base_entity",
	["gmod_wheel"] = "base_entity",
	["gmod_winch_controller"] = false, -- "base_point",
	["sent_ball"] = "base_entity",
	["widget_base"] = "base_entity",
	["env_skypaint"] = false,   -- "base_point",
	["gmod_hands"] = false,     -- "base_anim",
	["gmod_player_start"] = false, -- "base_point",
	["lua_run"] = false,        -- "base_point",
	["prop_effect"] = false,    -- "base_anim",
	["ragdoll_motion"] = false, -- "base_anim",
}

hook.Add(
	"ScriptedEntityRegistered",
	"GModCompatibility.ScriptedEntityRegistered.SyncWithGMod",
	function(className, scriptedEntity)
		if (differentBase[className]) then
			scriptedEntity.Base = differentBase[className]
		elseif (differentBase[className] == false) then
			-- Let the gmod module set the Base
			scriptedEntity.Base = nil
		end

		scripted_ents.Register(scriptedEntity, className)
	end
)

hook.Add(
	"ScriptedWeaponRegistered",
	"GModCompatibility.ScriptedWeaponRegistered.SyncWithGMod",
	function(className, scriptedWeapon)
		if (differentBase[className]) then
			scriptedWeapon.Base = differentBase[className]
		elseif (differentBase[className] == false) then
			scriptedWeapon.Base = nil
		end

		weapons.Register(scriptedWeapon, className)
	end
)

-- GMod builds a SWEP instance from weapons.Get(class): a copy of the SWEP with its whole Base chain
-- inherited (and BaseClass set). Srcbox's InitializeRefTable only merged the SWEP's own table, so
-- anything from weapon_base (SetWeaponHoldType, ShootEffects, TakePrimaryAmmo, ...) was missing.
do
	local originalInitializeRefTable = ScriptedWeapons._SrcboxInitializeRefTable or ScriptedWeapons.InitializeRefTable
	ScriptedWeapons._SrcboxInitializeRefTable = originalInitializeRefTable

	function ScriptedWeapons.InitializeRefTable(refTable, className)
		if (weapons and weapons.Get and weapons.GetStored and weapons.GetStored(className)) then
			local success, weaponTable = pcall(weapons.Get, className)

			if (success and istable(weaponTable)) then
				table.Merge(refTable, weaponTable)
				return
			end
		end

		return originalInitializeRefTable(refTable, className)
	end
end
