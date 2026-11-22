--[[
	Srcbox: functions from the Garry's Mod wiki that srcbox_glua_wiki_audit found missing and that
	can be built in Lua from what the game has ( the "quick wins" ). Each one is only set when the
	game doesn't already have it, so engine bindings added later take over.
--]]

local function setDefault(tableValue, key, value)
	if (tableValue and tableValue[key] == nil and value ~= nil) then
		tableValue[key] = value
	end
end

local ENTITY = FindMetaTable("Entity")
local PLAYER = FindMetaTable("Player")
local WEAPON = FindMetaTable("Weapon")
local VECTOR = FindMetaTable("Vector")
local ANGLE = FindMetaTable("Angle")
local COLOR = FindMetaTable("Color")
local MATRIX = FindMetaTable("VMatrix")
local DAMAGE = FindMetaTable("CTakeDamageInfo")
local EFFECTDATA = FindMetaTable("CEffectData")
local CONVAR = FindMetaTable("ConVar")
local MOVEDATA = FindMetaTable("CMoveData")
local MATERIAL = FindMetaTable("IMaterial")

-- Values kept for objects that have nowhere of their own to keep them ( weak: forgotten with them )
local extra = setmetatable({}, { __mode = "k" })
local function extraOf(object)
	local data = extra[object]
	if (not data) then
		data = {}
		extra[object] = data
	end
	return data
end

--[[
	util
--]]
if (util) then
	-- Like GMod: line breaks every 76 characters ( RFC 2045 ) unless inline
	setDefault(util, "Base64Encode", function(data, inline)
		return Serializers.Base64Encode(tostring(data or ""), inline and true or false)
	end)

	setDefault(util, "Base64Decode", function(data)
		return Serializers.Base64Decode((tostring(data or ""):gsub("%s", "")))
	end)

	-- SHA-1 and SHA-256 ( pure Lua, 32-bit words in Lua 5.4 integers )
	local function preprocess(message)
		local length = #message
		message = message .. "\128" .. ("\0"):rep((55 - length) % 64) .. string.pack(">I8", length * 8)
		return message
	end

	local function rotl(x, n) return ((x << n) | (x >> (32 - n))) & 0xffffffff end
	local function rotr(x, n) return ((x >> n) | (x << (32 - n))) & 0xffffffff end

	setDefault(util, "SHA1", function(text)
		local message = preprocess(tostring(text or ""))
		local h0, h1, h2, h3, h4 = 0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0
		local w = {}

		for chunk = 1, #message, 64 do
			for i = 0, 15 do
				w[i] = string.unpack(">I4", message, chunk + i * 4)
			end
			for i = 16, 79 do
				w[i] = rotl(w[i - 3] ~ w[i - 8] ~ w[i - 14] ~ w[i - 16], 1)
			end

			local a, b, c, d, e = h0, h1, h2, h3, h4
			for i = 0, 79 do
				local f, k
				if (i < 20) then
					f, k = (b & c) | ((~b) & d), 0x5A827999
				elseif (i < 40) then
					f, k = b ~ c ~ d, 0x6ED9EBA1
				elseif (i < 60) then
					f, k = (b & c) | (b & d) | (c & d), 0x8F1BBCDC
				else
					f, k = b ~ c ~ d, 0xCA62C1D6
				end
				local temp = (rotl(a, 5) + (f & 0xffffffff) + e + k + w[i]) & 0xffffffff
				e, d, c, b, a = d, c, rotl(b, 30), a, temp
			end

			h0 = (h0 + a) & 0xffffffff
			h1 = (h1 + b) & 0xffffffff
			h2 = (h2 + c) & 0xffffffff
			h3 = (h3 + d) & 0xffffffff
			h4 = (h4 + e) & 0xffffffff
		end

		return string.format("%08x%08x%08x%08x%08x", h0, h1, h2, h3, h4)
	end)

	local K256 = {
		0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
		0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
		0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
		0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
		0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
		0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
		0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
		0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
	}

	setDefault(util, "SHA256", function(text)
		local message = preprocess(tostring(text or ""))
		local h = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 }
		local w = {}

		for chunk = 1, #message, 64 do
			for i = 0, 15 do
				w[i] = string.unpack(">I4", message, chunk + i * 4)
			end
			for i = 16, 63 do
				local s0 = rotr(w[i - 15], 7) ~ rotr(w[i - 15], 18) ~ (w[i - 15] >> 3)
				local s1 = rotr(w[i - 2], 17) ~ rotr(w[i - 2], 19) ~ (w[i - 2] >> 10)
				w[i] = (w[i - 16] + s0 + w[i - 7] + s1) & 0xffffffff
			end

			local a, b, c, d, e, f, g, hh = h[1], h[2], h[3], h[4], h[5], h[6], h[7], h[8]
			for i = 0, 63 do
				local S1 = rotr(e, 6) ~ rotr(e, 11) ~ rotr(e, 25)
				local ch = (e & f) ~ ((~e) & g & 0xffffffff)
				local temp1 = (hh + S1 + ch + K256[i + 1] + w[i]) & 0xffffffff
				local S0 = rotr(a, 2) ~ rotr(a, 13) ~ rotr(a, 22)
				local maj = (a & b) ~ (a & c) ~ (b & c)
				local temp2 = (S0 + maj) & 0xffffffff
				hh, g, f, e, d, c, b, a = g, f, e, (d + temp1) & 0xffffffff, c, b, a, (temp1 + temp2) & 0xffffffff
			end

			h[1] = (h[1] + a) & 0xffffffff
			h[2] = (h[2] + b) & 0xffffffff
			h[3] = (h[3] + c) & 0xffffffff
			h[4] = (h[4] + d) & 0xffffffff
			h[5] = (h[5] + e) & 0xffffffff
			h[6] = (h[6] + f) & 0xffffffff
			h[7] = (h[7] + g) & 0xffffffff
			h[8] = (h[8] + hh) & 0xffffffff
		end

		return string.format(("%08x"):rep(8), table.unpack(h))
	end)

	setDefault(util, "SteamIDFrom64", function(steamID64)
		local id = math.tointeger(tonumber(steamID64))
		if (not id) then
			return "STEAM_0:0:0"
		end
		local account = id - 76561197960265728
		if (account < 0) then
			return "STEAM_0:0:0"
		end
		return string.format("STEAM_0:%d:%d", account % 2, account // 2)
	end)

	setDefault(util, "IsBoxIntersectingBox", function(min1, max1, min2, max2)
		return min1.x <= max2.x and max1.x >= min2.x
			and min1.y <= max2.y and max1.y >= min2.y
			and min1.z <= max2.z and max1.z >= min2.z
	end)

	setDefault(util, "IsOBBIntersectingOBB", Utilities and Utilities.IsObbIntersectingObb)

	-- Moller-Trumbore; the hit position, or nil
	setDefault(util, "IntersectRayWithTriangle", function(origin, direction, p1, p2, p3)
		local edge1, edge2 = p2 - p1, p3 - p1
		local h = direction:Cross(edge2)
		local a = edge1:Dot(h)
		if (math.abs(a) < 1e-8) then
			return nil
		end
		local f = 1 / a
		local s = origin - p1
		local u = f * s:Dot(h)
		if (u < 0 or u > 1) then
			return nil
		end
		local q = s:Cross(edge1)
		local v = f * direction:Dot(q)
		if (v < 0 or u + v > 1) then
			return nil
		end
		local t = f * edge2:Dot(q)
		if (t < 0 or t > 1) then
			return nil
		end
		return origin + direction * t
	end)

	-- Closest approach of two segments: intersecting ( within a hair ), and how far along each
	setDefault(util, "IsRayIntersectingRay", function(start1, end1, start2, end2)
		local d1, d2 = end1 - start1, end2 - start2
		local r = start1 - start2
		local a, e, f = d1:Dot(d1), d2:Dot(d2), d2:Dot(r)
		local c, b = d1:Dot(r), d1:Dot(d2)
		local denom = a * e - b * b
		local s = (denom ~= 0) and math.Clamp((b * f - c * e) / denom, 0, 1) or 0
		local t = (e ~= 0) and math.Clamp((b * s + f) / e, 0, 1) or 0
		local p1, p2 = start1 + d1 * s, start2 + d2 * t
		return p1:DistToSqr(p2) < 0.0001, s, t
	end)

	local lastCycle
	setDefault(util, "TimerCycle", function()
		local now = SysTime()
		local elapsed = lastCycle and (now - lastCycle) * 1000 or 0
		lastCycle = now
		return elapsed
	end)

	setDefault(util, "ScreenShake", Utilities and Utilities.ShakeScreen)

	setDefault(util, "TraceEntityHull", function(traceData, entity)
		local data = table.Copy(traceData)
		data.mins = entity:OBBMins()
		data.maxs = entity:OBBMaxs()
		data.filter = data.filter or entity
		return util.TraceHull(data)
	end)

	setDefault(util, "IsModelLoaded", function(model)
		return util.IsValidModel(model)
	end)

	-- Surface properties ( physics materials )
	local function surfaceProps()
		local ok, props = pcall(PhysicsSurfaceProperties.GetGlobal)
		return ok and props or nil
	end

	setDefault(util, "GetSurfaceIndex", function(name)
		local props = surfaceProps()
		return props and props:GetSurfaceIndex(name) or -1
	end)

	setDefault(util, "GetSurfacePropName", function(index)
		local props = surfaceProps()
		return props and props:GetPropName(index) or ""
	end)

	setDefault(util, "GetSurfaceData", function(index)
		local props = surfaceProps()
		return props and props:GetSurfaceData(index) or nil
	end)

	-- Activity / anim event names come from the ACT_ / AE_ enumerations
	local function nameLookup(prefix)
		local byName, byId
		return function(name)
			return rawget(_G, name)
		end, function(id)
			if (not byId) then
				byId = {}
				for key, value in pairs(_G) do
					if (type(key) == "string" and key:sub(1, #prefix) == prefix and type(value) == "number" and byId[value] == nil) then
						byId[value] = key
					end
				end
			end
			return byId[id] or ""
		end
	end

	local actId, actName = nameLookup("ACT_")
	setDefault(util, "GetActivityIDByName", function(name) return actId(name) or -1 end)
	setDefault(util, "GetActivityNameByID", actName)

	local eventId, eventName = nameLookup("AE_")
	setDefault(util, "GetAnimEventIDByName", function(name) return eventId(name) or -1 end)
	setDefault(util, "GetAnimEventNameByID", eventName)

	-- KeyValues to an ordered list: { { Key = ..., Value = ... }, ... }, nested the same way
	setDefault(util, "KeyValuesToTablePreserveOrder", function(text, usesEscapes, preserveKeyCase)
		local tokens = {}
		local i, length = 1, #text

		while (i <= length) do
			local char = text:sub(i, i)
			if (char:match("%s")) then
				i = i + 1
			elseif (text:sub(i, i + 1) == "//") then
				local lineEnd = text:find("\n", i, true) or length
				i = lineEnd + 1
			elseif (char == "{" or char == "}") then
				table.insert(tokens, char)
				i = i + 1
			elseif (char == "\"") then
				local value, j = {}, i + 1
				while (j <= length) do
					local c = text:sub(j, j)
					if (c == "\\" and usesEscapes) then
						local nextChar = text:sub(j + 1, j + 1)
						table.insert(value, (nextChar == "n") and "\n" or (nextChar == "t") and "\t" or nextChar)
						j = j + 2
					elseif (c == "\"") then
						break
					else
						table.insert(value, c)
						j = j + 1
					end
				end
				table.insert(tokens, { table.concat(value) })
				i = j + 1
			else
				local j = i
				while (j <= length and not text:sub(j, j):match("[%s{}\"]")) do
					j = j + 1
				end
				table.insert(tokens, { text:sub(i, j - 1) })
				i = j
			end
		end

		local position = 1
		local function parseBlock()
			local list = {}
			while (position <= #tokens) do
				local token = tokens[position]
				if (token == "}") then
					position = position + 1
					break
				end
				local key = token[1] or ""
				if (not preserveKeyCase) then
					key = key:lower()
				end
				position = position + 1
				local value = tokens[position]
				if (value == "{") then
					position = position + 1
					table.insert(list, { Key = key, Value = parseBlock() })
				elseif (type(value) == "table") then
					position = position + 1
					local raw = value[1]
					table.insert(list, { Key = key, Value = tonumber(raw) or raw })
				else
					break
				end
			end
			return list
		end

		local root = parseBlock()
		-- GMod returns the contents of the first ( root ) block
		if (#root == 1 and type(root[1].Value) == "table") then
			return root[1].Value
		end
		return root
	end)

	if (SERVER and util.BlastDamage) then
		setDefault(util, "BlastDamageInfo", function(damageInfo, position, radius)
			util.BlastDamage(damageInfo:GetInflictor(), damageInfo:GetAttacker(), position, radius, damageInfo:GetDamage())
		end)
	end
end

--[[
	Colors
--]]
if (COLOR) then
	local function hsv(color) return ColorToHSV(color) end
	local function hsl(color) return ColorToHSL(color) end

	local function hwbFromHsv(h, s, v)
		return h, (1 - s) * v, 1 - v
	end

	local function keepAlpha(color, newColor)
		color.r, color.g, color.b = newColor.r, newColor.g, newColor.b
	end

	setDefault(COLOR, "GetHue", function(self) return (hsv(self)) end)
	setDefault(COLOR, "GetSaturation", function(self) return select(2, hsv(self)) end)
	setDefault(COLOR, "GetBrightness", function(self) return select(3, hsv(self)) end)
	setDefault(COLOR, "GetLightness", function(self) return select(3, hsl(self)) end)
	setDefault(COLOR, "GetWhiteness", function(self) return select(2, hwbFromHsv(hsv(self))) end)
	setDefault(COLOR, "GetBlackness", function(self) return select(3, hwbFromHsv(hsv(self))) end)

	setDefault(COLOR, "ToHWB", function(self) return hwbFromHsv(hsv(self)) end)

	setDefault(COLOR, "SetHue", function(self, hue)
		local _, s, v = hsv(self)
		keepAlpha(self, HSVToColor(hue % 360, s, v))
	end)

	setDefault(COLOR, "SetSaturation", function(self, saturation)
		local h, _, v = hsv(self)
		keepAlpha(self, HSVToColor(h, math.Clamp(saturation, 0, 1), v))
	end)

	setDefault(COLOR, "SetBrightness", function(self, brightness)
		local h, s = hsv(self)
		keepAlpha(self, HSVToColor(h, s, math.Clamp(brightness, 0, 1)))
	end)

	setDefault(COLOR, "SetLightness", function(self, lightness)
		local h, s = hsl(self)
		keepAlpha(self, HSLToColor(h, s, math.Clamp(lightness, 0, 1)))
	end)

	local function hwbToColor(hue, whiteness, blackness, alpha)
		local total = whiteness + blackness
		if (total >= 1) then
			local gray = math.Round(whiteness / total * 255)
			return Color(gray, gray, gray, alpha or 255)
		end
		local value = 1 - blackness
		local saturation = (value > 0) and (1 - whiteness / value) or 0
		local color = HSVToColor(hue % 360, saturation, value)
		color.a = alpha or 255
		return color
	end

	setDefault(COLOR, "SetWhiteness", function(self, whiteness)
		local h, _, b = self:ToHWB()
		keepAlpha(self, hwbToColor(h, math.Clamp(whiteness, 0, 1), b))
	end)

	setDefault(COLOR, "SetBlackness", function(self, blackness)
		local h, w = self:ToHWB()
		keepAlpha(self, hwbToColor(h, w, math.Clamp(blackness, 0, 1)))
	end)

	setDefault(COLOR, "AddHue", function(self, amount) self:SetHue(self:GetHue() + amount) end)
	setDefault(COLOR, "AddSaturation", function(self, amount) self:SetSaturation(self:GetSaturation() + amount) end)
	setDefault(COLOR, "AddBrightness", function(self, amount) self:SetBrightness(self:GetBrightness() + amount) end)
	setDefault(COLOR, "AddLightness", function(self, amount) self:SetLightness(self:GetLightness() + amount) end)
	setDefault(COLOR, "AddWhiteness", function(self, amount) self:SetWhiteness(self:GetWhiteness() + amount) end)
	setDefault(COLOR, "AddBlackness", function(self, amount) self:SetBlackness(self:GetBlackness() + amount) end)

	setDefault(COLOR, "ToHex", function(self, omitAlpha)
		if (omitAlpha or self.a == 255) then
			return string.format("#%02x%02x%02x", self.r, self.g, self.b)
		end
		return string.format("#%02x%02x%02x%02x", self.r, self.g, self.b, self.a)
	end)

	if (HWBToColor == nil) then
		HWBToColor = function(hue, whiteness, blackness) return hwbToColor(hue, whiteness, blackness) end
	end
end

if (HexToColor == nil) then
	function HexToColor(hex)
		hex = tostring(hex or ""):gsub("^#", "")
		if (#hex == 3 or #hex == 4) then
			hex = hex:gsub(".", "%0%0")
		end
		local r = tonumber(hex:sub(1, 2), 16) or 255
		local g = tonumber(hex:sub(3, 4), 16) or 255
		local b = tonumber(hex:sub(5, 6), 16) or 255
		local a = (#hex >= 8) and (tonumber(hex:sub(7, 8), 16) or 255) or 255
		return Color(r, g, b, a)
	end
end

--[[
	bit ( LuaJIT's: 32-bit, signed results )
--]]
if (bit) then
	local function tobit(x)
		x = math.tointeger(x) or math.floor(x)
		x = x & 0xffffffff
		if (x >= 0x80000000) then
			x = x - 0x100000000
		end
		return x
	end

	setDefault(bit, "tobit", tobit)
	setDefault(bit, "arshift", function(x, n)
		return tobit(tobit(x) // (1 << (n & 31)))	-- floor division keeps the sign
	end)
	setDefault(bit, "rol", function(x, n)
		x, n = tobit(x) & 0xffffffff, n & 31
		return tobit((x << n) | (x >> (32 - n)))
	end)
	setDefault(bit, "ror", function(x, n)
		x, n = tobit(x) & 0xffffffff, n & 31
		return tobit((x >> n) | (x << (32 - n)))
	end)
	setDefault(bit, "bswap", function(x)
		x = tobit(x) & 0xffffffff
		return tobit(((x & 0xff) << 24) | ((x & 0xff00) << 8) | ((x >> 8) & 0xff00) | (x >> 24))
	end)
	setDefault(bit, "tohex", function(x, n)
		n = n or 8
		local upper = n < 0
		n = math.abs(n)
		local text = string.format(upper and "%08X" or "%08x", tobit(x) & 0xffffffff)
		return text:sub(-n)
	end)
end

--[[
	utf8 ( GMod's extras over Lua's )
--]]
if (utf8) then
	setDefault(utf8, "GetChar", function(text, index)
		local length = utf8.len(text) or 0
		if (index < 0) then
			index = length + index + 1
		end
		if (index < 1 or index > length) then
			return ""
		end
		local from = utf8.offset(text, index)
		local to = utf8.offset(text, index + 1)
		return text:sub(from, (to or #text + 1) - 1)
	end)

	setDefault(utf8, "sub", function(text, first, last)
		local length = utf8.len(text) or #text
		first = first or 1
		last = last or -1
		if (first < 0) then first = math.max(length + first + 1, 1) end
		if (last < 0) then last = length + last + 1 end
		if (first > last or first > length) then
			return ""
		end
		local from = utf8.offset(text, first)
		local to = utf8.offset(text, last + 1)
		return text:sub(from, (to or #text + 1) - 1)
	end)
end

--[[
	net
--]]
if (net) then
	local READER = FindMetaTable("MessageReader")

	-- A Read-prefixed helper on the reader, so net forwards it to the message being read
	if (READER and READER.ReadSrcboxBytesLeft == nil) then
		READER.ReadSrcboxBytesLeft = function(reader)
			return reader:GetNumBytesLeft(), reader:GetNumBitsLeft()
		end
	end

	setDefault(net, "BytesLeft", function()
		local ok, bytes, bits = pcall(function() return net.ReadSrcboxBytesLeft() end)
		if (ok) then
			return bytes, bits
		end
		return nil
	end)

	setDefault(net, "Abort", Networks and Networks.Cancel)
	setDefault(net, "WriteNormal", function(normal) net.WriteVectorNormal(normal) end)
	setDefault(net, "ReadNormal", function() return net.ReadVectorNormal() end)

	-- 64-bit unsigned numbers, as decimal strings like GMod
	local function parseUnsigned(text)
		local value = 0
		for digit in tostring(text or "0"):gmatch("%d") do
			value = value * 10 + tonumber(digit)
		end
		return value
	end

	local function unsignedToString(value)
		if (value >= 0) then
			return tostring(value)
		end
		local quotient = (value >> 1) // 5
		return tostring(quotient) .. tostring(value - quotient * 10)
	end

	setDefault(net, "WriteUInt64", function(value)
		local number = parseUnsigned(value)
		Networks.WriteBitLong(number & 0xffffffff, 32, false)
		Networks.WriteBitLong((number >> 32) & 0xffffffff, 32, false)
	end)

	setDefault(net, "ReadUInt64", function()
		local low = math.tointeger(Networks.ReadBitLong(32, false)) & 0xffffffff
		local high = math.tointeger(Networks.ReadBitLong(32, false)) & 0xffffffff
		return unsignedToString((high << 32) | low)
	end)

	if (SERVER) then
		local function playersInPvs(position)
			local list = {}
			for _, entity in ipairs(Entities.GetInPvs(position)) do
				if (entity:IsPlayer() and not entity:IsBot()) then
					table.insert(list, entity)
				end
			end
			return list
		end

		setDefault(net, "SendPVS", function(position)
			net.Send(playersInPvs(position))
		end)

		-- Hearing range isn't tracked separately here: the PVS is the closest there is
		setDefault(net, "SendPAS", function(position)
			net.Send(playersInPvs(position))
		end)
	end
end

--[[
	Lua 5.1 functions older addons still call ( this game runs Lua 5.4 )
--]]
if (debug) then
	local function environmentUpvalue(func)
		local index = 1
		while (true) do
			local name, value = debug.getupvalue(func, index)
			if (name == "_ENV") then
				return index, value
			elseif (name == nil) then
				return nil
			end
			index = index + 1
		end
	end

	setDefault(debug, "getfenv", function(func)
		if (type(func) ~= "function") then
			return _G
		end
		local _, environment = environmentUpvalue(func)
		return environment or _G
	end)

	setDefault(debug, "setfenv", function(func, environment)
		if (type(func) == "function") then
			local index = environmentUpvalue(func)
			if (index) then
				debug.upvaluejoin(func, index, function() return environment end, 1)
			end
		end
		return func
	end)
end

if (newproxy == nil) then
	-- A table stands in for the userdata Lua 5.1 made
	function newproxy(withMetatable)
		if (withMetatable == true) then
			return setmetatable({}, {})
		elseif (type(withMetatable) == "table" and getmetatable(withMetatable)) then
			return setmetatable({}, getmetatable(withMetatable))
		end
		return {}
	end
end

if (jit) then
	setDefault(jit, "on", function() end)
	setDefault(jit, "off", function() end)
	setDefault(jit, "flush", function() end)
end

--[[
	Globals
--]]
if (OrderVectors == nil) then
	function OrderVectors(a, b)
		local minX, maxX = math.min(a.x, b.x), math.max(a.x, b.x)
		local minY, maxY = math.min(a.y, b.y), math.max(a.y, b.y)
		local minZ, maxZ = math.min(a.z, b.z), math.max(a.z, b.z)
		a.x, a.y, a.z = minX, minY, minZ
		b.x, b.y, b.z = maxX, maxY, maxZ
	end
end

if (GetGlobal2Var == nil and GetGlobalVariable) then
	GetGlobal2Var = function(key, fallback)
		local value = GetGlobalVariable(key)
		if (value == nil) then return fallback end
		return value
	end
end

if (SetGlobal2Var == nil and SetGlobalVariable) then
	SetGlobal2Var = SetGlobalVariable
end

if (SERVER and BroadcastLua == nil) then
	function BroadcastLua(code)
		for _, client in ipairs(player.GetHumans and player.GetHumans() or player.GetAll()) do
			if (client.SendLua) then
				client:SendLua(code)
			end
		end
	end
end

if (BuildNetworkedVarsTable == nil) then
	function BuildNetworkedVarsTable()
		local result = {}
		for _, entity in ipairs(ents.GetAll()) do
			local variables = entity.__networkedVariables
			if (variables and next(variables)) then
				result[entity] = table.Copy(variables)
			end
		end
		return result
	end
end

--[[
	Small library additions
--]]
if (timer and timer.IsPaused == nil) then
	-- The timer module keeps paused-ness to itself; remember it from Pause / UnPause / Toggle
	local paused = {}
	local pause, unpause, toggle = timer.Pause, timer.UnPause, timer.Toggle

	if (pause) then
		timer.Pause = function(id) paused[id] = true return pause(id) end
	end
	if (unpause) then
		timer.UnPause = function(id) paused[id] = nil return unpause(id) end
	end
	if (toggle) then
		timer.Toggle = function(id) paused[id] = not paused[id] or nil return toggle(id) end
	end

	timer.IsPaused = function(id)
		return paused[id] == true
	end
end

if (concommand and concommand.GetTable == nil and concommand.Add) then
	local commands, completions = {}, {}
	local add, remove = concommand.Add, concommand.Remove

	concommand.Add = function(name, callback, autoComplete, ...)
		commands[tostring(name):lower()] = callback
		completions[tostring(name):lower()] = autoComplete
		return add(name, callback, autoComplete, ...)
	end

	if (remove) then
		concommand.Remove = function(name, ...)
			commands[tostring(name):lower()] = nil
			completions[tostring(name):lower()] = nil
			return remove(name, ...)
		end
	end

	concommand.GetTable = function()
		return commands, completions
	end

	setDefault(concommand, "Run", function(client, command, arguments, argumentString)
		local callback = commands[tostring(command):lower()]
		if (not callback) then
			return false
		end
		callback(client, command, arguments or {}, argumentString or table.concat(arguments or {}, " "))
		return true
	end)

	setDefault(concommand, "AutoComplete", function(command, argumentString, arguments)
		local autoComplete = completions[tostring(command):lower()]
		if (isfunction(autoComplete)) then
			return autoComplete(command, argumentString, arguments)
		end
	end)
end

if (game) then
	setDefault(game, "GetTimeScale", function()
		local conVar = GetConVar("host_timescale")
		return conVar and conVar:GetFloat() or 1
	end)

	setDefault(game, "GetSkillLevel", function()
		local conVar = GetConVar("skill")
		return conVar and conVar:GetInt() or 1
	end)

	setDefault(game, "GetMapVersion", function() return 0 end)
	setDefault(game, "GetMapChangeCount", function() return 0 end)

	if (SERVER) then
		setDefault(game, "SetSkillLevel", function(level)
			RunConsoleCommand("skill", tostring(level))
		end)

		setDefault(game, "KickID", function(id, reason)
			RunConsoleCommand("kickid", tostring(id), tostring(reason or "Kicked"))
		end)

		setDefault(game, "GetIPAddress", function()
			local conVar = GetConVar("ip")
			local port = GetConVar("hostport")
			return (conVar and conVar:GetString() or "0.0.0.0") .. ":" .. (port and port:GetString() or "27015")
		end)

		setDefault(game, "RemoveRagdolls", function()
			for _, entity in ipairs(ents.FindByClass("prop_ragdoll")) do
				if (not entity:CreatedByMap()) then
					entity:Remove()
				end
			end
		end)

		setDefault(game, "SetTimeScale", function(scale)
			RunConsoleCommand("host_timescale", tostring(scale))
		end)
	end
end

if (physenv) then
	setDefault(physenv, "GetLastSimulationTime", function() return 0 end)
	setDefault(physenv, "GetTimeScale", function()
		local conVar = GetConVar("phys_timescale")
		return conVar and conVar:GetFloat() or 1
	end)
	if (SERVER) then
		setDefault(physenv, "SetTimeScale", function(scale)
			RunConsoleCommand("phys_timescale", tostring(scale))
		end)
	end
end

if (ents) then
	setDefault(ents, "FindByModel", function(model)
		local found = {}
		model = tostring(model or ""):lower()
		for _, entity in ipairs(ents.GetAll()) do
			if ((entity:GetModel() or ""):lower() == model) then
				table.insert(found, entity)
			end
		end
		return found
	end)

	setDefault(ents, "FindInCone", function(origin, normal, range, angleCos)
		local found = {}
		for _, entity in ipairs(ents.FindInSphere(origin, range)) do
			local direction = entity:WorldSpaceCenter() - origin
			direction:Normalize()
			if (direction:Dot(normal) >= angleCos) then
				table.insert(found, entity)
			end
		end
		return found
	end)

	setDefault(ents, "FindByName", function(name)
		local found = {}
		for _, entity in ipairs(ents.GetAll()) do
			if (entity.GetName and entity:GetName() == name) then
				table.insert(found, entity)
			end
		end
		return found
	end)
end

if (engine) then
	setDefault(engine, "AbsoluteFrameTime", Engines and Engines.GetAbsoluteFrameTime)
	if (CLIENT) then
		setDefault(engine, "ServerFrameTime", function() return FrameTime(), 0 end)
	else
		setDefault(engine, "CloseServer", function() RunConsoleCommand("disconnect") end)
	end
end

if (system) then
	setDefault(system, "BatteryPower", function() return 255 end)	-- 255: on mains power
end

if (sql) then
	setDefault(sql, "QueryTyped", function(query)
		return sql.Query(query)
	end)
end

--[[
	Object methods
--]]
if (VECTOR) then
	setDefault(VECTOR, "DotProduct", VECTOR.Dot)
	setDefault(VECTOR, "GetNormal", VECTOR.GetNormalized)
	setDefault(VECTOR, "Distance2DSqr", function(self, other)
		local dx, dy = self.x - other.x, self.y - other.y
		return dx * dx + dy * dy
	end)
end

if (ANGLE) then
	setDefault(ANGLE, "Random", function(self, minimum, maximum)
		minimum, maximum = minimum or -180, maximum or 180
		self.p = math.Rand(minimum, maximum)
		self.y = math.Rand(minimum, maximum)
		self.r = math.Rand(minimum, maximum)
		return self
	end)
end

if (MATRIX) then
	local function eachField(fn)
		for row = 1, 4 do
			for column = 1, 4 do
				fn(row, column)
			end
		end
	end

	setDefault(MATRIX, "Add", function(self, other)
		eachField(function(r, c) self:SetField(r, c, self:GetField(r, c) + other:GetField(r, c)) end)
		return self
	end)

	setDefault(MATRIX, "Sub", function(self, other)
		eachField(function(r, c) self:SetField(r, c, self:GetField(r, c) - other:GetField(r, c)) end)
		return self
	end)

	setDefault(MATRIX, "Mul", function(self, other)
		local result = {}
		eachField(function(r, c)
			local sum = 0
			for k = 1, 4 do
				sum = sum + self:GetField(r, k) * other:GetField(k, c)
			end
			result[r * 4 + c] = sum
		end)
		eachField(function(r, c) self:SetField(r, c, result[r * 4 + c]) end)
		return self
	end)

	setDefault(MATRIX, "GetTransposed", function(self)
		local copy = Matrix()
		eachField(function(r, c) copy:SetField(c, r, self:GetField(r, c)) end)
		return copy
	end)
end

if (DAMAGE) then
	setDefault(DAMAGE, "IsBulletDamage", function(self) return bit.band(self:GetDamageType(), DMG_BULLET) ~= 0 end)
	setDefault(DAMAGE, "IsExplosionDamage", function(self) return bit.band(self:GetDamageType(), DMG_BLAST) ~= 0 end)
	setDefault(DAMAGE, "IsFallDamage", function(self) return bit.band(self:GetDamageType(), DMG_FALL) ~= 0 end)
	setDefault(DAMAGE, "SetBaseDamage", function(self, damage) extraOf(self).baseDamage = damage end)
	setDefault(DAMAGE, "GetDamageBonus", function(self) return extraOf(self).bonus or 0 end)
	setDefault(DAMAGE, "SetDamageBonus", function(self, bonus) extraOf(self).bonus = bonus end)
end

if (EFFECTDATA) then
	setDefault(EFFECTDATA, "GetSurfaceProp", function(self) return extraOf(self).surfaceProp or 0 end)
	setDefault(EFFECTDATA, "SetSurfaceProp", function(self, prop) extraOf(self).surfaceProp = prop end)
end

if (CONVAR) then
	setDefault(CONVAR, "GetFlags", function(self)
		local flags = 0
		for bitIndex = 0, 31 do
			local flag = 1 << bitIndex
			if (self:IsFlagSet(flag)) then
				flags = flags | flag
			end
		end
		return flags
	end)
end

if (MOVEDATA) then
	setDefault(MOVEDATA, "RemoveKey", function(self, key)
		self:SetButtons(bit.band(self:GetButtons(), bit.bnot(key)))
	end)
end

if (MATERIAL) then
	setDefault(MATERIAL, "GetVectorLinear", function(self, name) return self:GetVector(name) end)
end

-- CSoundPatch is the compat layer's Lua object ( sh_init.lua ); making one plays nothing
local soundPatchProbe = CreateSound and select(2, pcall(CreateSound, nil, ""))
local SOUNDPATCH = type(soundPatchProbe) == "table" and getmetatable(soundPatchProbe)
if (SOUNDPATCH and type(SOUNDPATCH.__index) == "table") then
	setDefault(SOUNDPATCH.__index, "GetSoundName", function(self) return self._sound end)
end

--[[
	Entity
--]]
if (ENTITY) then
	-- NW2 is the same networked-variable store here
	setDefault(ENTITY, "GetNW2Var", function(self, key, fallback) return self:GetNetworkedVariable(key, fallback) end)
	setDefault(ENTITY, "SetNW2Var", function(self, key, value) return self:SetNetworkedVariable(key, value) end)
	setDefault(ENTITY, "GetNetworkedVar", ENTITY.GetNW2Var)
	setDefault(ENTITY, "SetNetworkedVar", ENTITY.SetNW2Var)
	setDefault(ENTITY, "GetNetworked2Var", ENTITY.GetNW2Var)
	setDefault(ENTITY, "SetNetworked2Var", ENTITY.SetNW2Var)
	setDefault(ENTITY, "SetNetworkedNumber", ENTITY.SetNW2Var)

	local function variableTable(self)
		local result = {}
		for key, value in pairs(self.__networkedVariables or {}) do
			result[key] = { value = value, type = type(value) }
		end
		return result
	end

	setDefault(ENTITY, "GetNWVarTable", function(self) return table.Copy(self.__networkedVariables or {}) end)
	setDefault(ENTITY, "GetNW2VarTable", variableTable)
	setDefault(ENTITY, "GetNetworkedVarTable", ENTITY.GetNWVarTable)
	setDefault(ENTITY, "GetNetworked2VarTable", variableTable)

	setDefault(ENTITY, "GetNWVarProxy", function(self, key)
		return self.__networkedVarCallbacks and self.__networkedVarCallbacks[key]
	end)
	setDefault(ENTITY, "GetNetworkedVarProxy", ENTITY.GetNWVarProxy)

	setDefault(ENTITY, "IsScripted", function(self)
		local entityTable = self:GetTable()
		return type(entityTable) == "table" and (rawget(entityTable, "Base") ~= nil or rawget(entityTable, "Type") ~= nil)
	end)

	setDefault(ENTITY, "GetBloodColor", ENTITY.GetBloodColorType)

	-- Line of sight: GMod traces eye to target with MASK_BLOCKLOS, ignoring both
	local function clearLine(self, target)
		local from = self.EyePos and self:EyePos() or self:WorldSpaceCenter()
		local trace = util.TraceLine({
			start = from,
			endpos = target,
			filter = self,
			mask = MASK_BLOCKLOS or MASK_SOLID_BRUSHONLY,
		})
		return trace
	end

	setDefault(ENTITY, "Visible", function(self, other)
		if (not IsValid(other)) then return false end
		local trace = clearLine(self, other:WorldSpaceCenter())
		return not trace.Hit or trace.Entity == other
	end)

	setDefault(ENTITY, "VisibleVec", function(self, position)
		return not clearLine(self, position).Hit
	end)

	setDefault(ENTITY, "IsLineOfSightClear", function(self, target)
		local position = isvector(target) and target or (IsValid(target) and target:WorldSpaceCenter())
		if (not position) then return false end
		local trace = clearLine(self, position)
		return not trace.Hit or (not isvector(target) and trace.Entity == target)
	end)

	setDefault(ENTITY, "GetModelRadius", function(self)
		local mins, maxs = self:GetModelBounds()
		if (not mins) then return 0 end
		return (maxs - mins):Length() / 2
	end)

	setDefault(ENTITY, "GetModelRenderBounds", function(self)
		return self:GetModelBounds()
	end)

	setDefault(ENTITY, "GetRotatedAABB", function(self, mins, maxs)
		local angles = self:GetAngles()
		local resultMin = Vector(math.huge, math.huge, math.huge)
		local resultMax = Vector(-math.huge, -math.huge, -math.huge)
		for _, x in ipairs({ mins.x, maxs.x }) do
			for _, y in ipairs({ mins.y, maxs.y }) do
				for _, z in ipairs({ mins.z, maxs.z }) do
					local corner = Vector(x, y, z)
					corner:Rotate(angles)
					resultMin.x, resultMin.y, resultMin.z = math.min(resultMin.x, corner.x), math.min(resultMin.y, corner.y), math.min(resultMin.z, corner.z)
					resultMax.x, resultMax.y, resultMax.z = math.max(resultMax.x, corner.x), math.max(resultMax.y, corner.y), math.max(resultMax.z, corner.z)
				end
			end
		end
		return resultMin, resultMax
	end)

	setDefault(ENTITY, "GetSurroundingBounds", function(self)
		return self:WorldSpaceAABB()
	end)

	local function transformOf(entity)
		local matrix = Matrix()
		matrix:SetTranslation(entity:GetPos())
		matrix:SetAngles(entity:GetAngles())
		return matrix
	end

	setDefault(ENTITY, "GetWorldTransformMatrix", transformOf)
	setDefault(ENTITY, "GetParentWorldTransformMatrix", function(self)
		local parent = self:GetParent()
		return IsValid(parent) and transformOf(parent) or Matrix()
	end)

	setDefault(ENTITY, "SetCollisionBoundsWS", function(self, mins, maxs)
		local position = self:GetPos()
		self:SetCollisionBounds(mins - position, maxs - position)
	end)

	-- Sounds: stopping one is emitting it with SND_STOP ( 4 )
	setDefault(ENTITY, "StopSound", function(self, soundName)
		self:EmitSound(soundName, 75, 100, 0, CHAN_AUTO, 4)
	end)

	setDefault(ENTITY, "StartLoopingSound", function(self, soundName)
		local patch = CreateSound(self, soundName)
		if (not patch) then return -1 end
		patch:Play()
		local data = extraOf(self)
		data.loopingSounds = data.loopingSounds or {}
		local id = #data.loopingSounds + 1
		data.loopingSounds[id] = patch
		return id
	end)

	setDefault(ENTITY, "StopLoopingSound", function(self, id)
		local sounds = extraOf(self).loopingSounds
		if (sounds and sounds[id]) then
			sounds[id]:Stop()
			sounds[id] = nil
		end
	end)

	setDefault(ENTITY, "GetParentPhysNum", function(self) return extraOf(self).parentPhysNum or 0 end)
	setDefault(ENTITY, "SetParentPhysNum", function(self, number) extraOf(self).parentPhysNum = number end)

	setDefault(ENTITY, "GetShouldServerRagdoll", function(self) return extraOf(self).serverRagdoll == true end)
	setDefault(ENTITY, "SetShouldServerRagdoll", function(self, should) extraOf(self).serverRagdoll = should and true or nil end)

	setDefault(ENTITY, "GetCustomCollisionCheck", function(self) return extraOf(self).customCollision == true end)
	setDefault(ENTITY, "SetCustomCollisionCheck", function(self, enable) extraOf(self).customCollision = enable and true or nil end)

	if (SERVER) then
		-- Dissolve: an env_entity_dissolver aimed at it by name, like the combine ball's
		setDefault(ENTITY, "Dissolve", function(self, dissolveType, magnitude, origin)
			local name = self:GetName()
			if (name == nil or name == "") then
				name = "srcbox_dissolve_" .. self:EntIndex()
				self:SetName(name)
			end
			local dissolver = ents.Create("env_entity_dissolver")
			if (not IsValid(dissolver)) then return end
			dissolver:SetPos(origin or self:GetPos())
			dissolver:SetKeyValue("dissolvetype", tostring(dissolveType or 0))
			dissolver:SetKeyValue("magnitude", tostring(magnitude or 0))
			dissolver:Spawn()
			dissolver:Fire("Dissolve", name, 0)
			dissolver:Fire("Kill", "", 0.1)
		end)
	end
end

--[[
	Player
--]]
if (PLAYER) then
	setDefault(PLAYER, "GetPreviousWeapon", PLAYER.GetLastWeapon)
	setDefault(PLAYER, "GetCurrentViewOffset", PLAYER.GetViewOffset)
	setDefault(PLAYER, "SetCurrentViewOffset", PLAYER.SetViewOffset)
	setDefault(PLAYER, "GetEntityInUse", PLAYER.GetUseEntity)

	setDefault(PLAYER, "IsSprinting", function(self)
		return self:KeyDown(IN_SPEED) and self:GetVelocity():Length2DSqr() > 1
	end)

	setDefault(PLAYER, "IsWalking", function(self)
		return self:KeyDown(IN_WALK)
	end)

	setDefault(PLAYER, "GetPlayerInfo", function(self)
		local ok, info = pcall(Engines.GetPlayerInfo, self:EntIndex())
		return ok and info or {}
	end)

	setDefault(PLAYER, "IsConnected", function(self) return IsValid(self) end)
	setDefault(PLAYER, "IsTimingOut", function() return false end)
	setDefault(PLAYER, "GetTimeoutSeconds", function() return 0 end)
	setDefault(PLAYER, "PacketLoss", function() return 0 end)

	local function flagPair(getName, setName, key, default)
		setDefault(PLAYER, getName, function(self)
			local value = extraOf(self)[key]
			if (value == nil) then return default end
			return value
		end)
		setDefault(PLAYER, setName, function(self, value) extraOf(self)[key] = value end)
	end

	flagPair("GetAllowFullRotation", "SetAllowFullRotation", "fullRotation", false)
	flagPair("GetCanWalk", "SetCanWalk", "canWalk", true)
	flagPair("GetSuppressPickupNotices", "SetSuppressPickupNotices", "suppressPickups", false)
	flagPair("GetPreferredCarryAngles", "SetPreferredCarryAngles", "carryAngles", nil)

	-- Render angles: what was set, or where the player looks
	setDefault(PLAYER, "GetRenderAngles", function(self)
		return extraOf(self).renderAngles or Angle(0, self:EyeAngles().y, 0)
	end)
	setDefault(PLAYER, "SetRenderAngles", function(self, angles) extraOf(self).renderAngles = angles end)

	setDefault(PLAYER, "GetStepSize", function(self)
		local value = extraOf(self).stepSize
		if (value) then return value end
		local conVar = GetConVar("sv_stepsize")
		return conVar and conVar:GetFloat() or 18
	end)
	setDefault(PLAYER, "SetStepSize", function(self, size)
		extraOf(self).stepSize = size
		pcall(self.SetSaveValue, self, "m_flStepSize", size)
	end)

	setDefault(PLAYER, "IsWorldClicking", function() return false end)
	setDefault(PLAYER, "IsWorldClickingDisabled", function(self) return extraOf(self).noWorldClick == true end)
	setDefault(PLAYER, "DisableWorldClicking", function(self, disable) extraOf(self).noWorldClick = disable and true or nil end)
	setDefault(PLAYER, "OwnerSteamID64", function(self) return self:SteamID64() end)
	setDefault(PLAYER, "GetFriendStatus", function() return "none" end)
	setDefault(PLAYER, "IsVoiceAudible", function() return true end)

	if (SERVER) then
		setDefault(PLAYER, "SetActiveWeapon", PLAYER.SwitchWeapon)
		setDefault(PLAYER, "PickupWeapon", PLAYER.BumpWeapon)

		setDefault(PLAYER, "DropNamedWeapon", function(self, className)
			local weapon = self:GetWeapon(className)
			if (IsValid(weapon)) then
				self:DropWeapon(weapon)
			end
		end)

		setDefault(PLAYER, "SetFrags", function(self, frags)
			self:AddFrags(frags - self:Frags())
		end)

		setDefault(PLAYER, "SetDeaths", function(self, deaths)
			self:AddDeaths(deaths - self:Deaths())
		end)

		-- ScreenFade: GMod's SCREENFADE flags match the engine's FFADE ones
		setDefault(PLAYER, "ScreenFade", function(self, flags, color, fadeTime, holdTime)
			color = color or Color(0, 0, 0, 255)
			self:SendLua(string.format("Srcbox_ScreenFade(%d,%d,%d,%d,%d,%f,%f)",
				flags or 0, color.r, color.g, color.b, color.a, fadeTime or 0, holdTime or 0))
		end)

		setDefault(PLAYER, "StartSprinting", function() end)
		setDefault(PLAYER, "StopSprinting", function() end)
		setDefault(PLAYER, "StartWalking", function() end)
		setDefault(PLAYER, "StopWalking", function() end)
		setDefault(PLAYER, "AllowImmediateDecalPainting", function() end)

		setDefault(PLAYER, "SetNoTarget", function(self, noTarget)
			if (noTarget) then
				self:AddFlags(FL_NOTARGET)
			else
				self:RemoveFlags(FL_NOTARGET)
			end
		end)

		setDefault(PLAYER, "SetLaggedMovementValue", function(self, value)
			self:SetSaveValue("m_flLaggedMovementValue", value)
		end)

		setDefault(PLAYER, "LastHitGroup", function(self)
			local ok, value = pcall(self.GetInternalVariable, self, "m_LastHitGroup")
			return ok and value or 0
		end)

		setDefault(PLAYER, "SetLastHitGroup", function(self, hitGroup)
			pcall(self.SetSaveValue, self, "m_LastHitGroup", hitGroup)
		end)

		setDefault(PLAYER, "Say", function(self, text, teamOnly)
			self:ConCommand((teamOnly and "say_team " or "say ") .. tostring(text))
		end)

		setDefault(PLAYER, "Kick", function(self, reason)
			RunConsoleCommand("kickid", tostring(self:UserID()), tostring(reason or "Kicked"))
		end)

		setDefault(PLAYER, "Ban", function(self, minutes, kick)
			RunConsoleCommand("banid", tostring(minutes or 0), tostring(self:UserID()))
			if (kick) then
				self:Kick("Banned")
			end
		end)

		local connectedAt = setmetatable({}, { __mode = "k" })
		hook.Add("PlayerInitialSpawn", "Srcbox_TimeConnected", function(client)
			connectedAt[client] = CurTime()
		end)

		setDefault(PLAYER, "TimeConnected", function(self)
			return CurTime() - (connectedAt[self] or 0)
		end)

		setDefault(PLAYER, "SprintEnable", function() end)
		setDefault(PLAYER, "SprintDisable", function() end)
		setDefault(PLAYER, "StopZooming", function(self) self:SetFOV(0, 0) end)
		setDefault(PLAYER, "SetAllowWeaponsInVehicle", function(self, allow) extraOf(self).weaponsInVehicle = allow end)
	end
end

--[[
	Weapon
--]]
if (WEAPON) then
	setDefault(WEAPON, "GetSlotPos", WEAPON.GetSlotPosition)
	setDefault(WEAPON, "GetWeaponWorldModel", WEAPON.GetWorldModel)
	setDefault(WEAPON, "LastShootTime", function(self) return extraOf(self).lastShoot or 0 end)
	setDefault(WEAPON, "SetLastShootTime", function(self, time) extraOf(self).lastShoot = time or CurTime() end)
	setDefault(WEAPON, "GetDeploySpeed", function(self)
		local value = extraOf(self).deploySpeed
		if (value) then return value end
		local conVar = GetConVar("sv_defaultdeployspeed")
		return conVar and conVar:GetFloat() or 1
	end)
	setDefault(WEAPON, "SetDeploySpeed", function(self, speed) extraOf(self).deploySpeed = speed end)

	-- CallOnClient: run one of the weapon's functions on its owner's client
	local CALL_MESSAGE = "Srcbox_WeaponCallOnClient"
	if (SERVER) then
		util.AddNetworkString(CALL_MESSAGE)
		setDefault(WEAPON, "CallOnClient", function(self, functionName, argument)
			local owner = self:GetOwner()
			if (not IsValid(owner) or not owner:IsPlayer()) then return end
			net.Start(CALL_MESSAGE)
			net.WriteEntity(self)
			net.WriteString(tostring(functionName))
			net.WriteString(tostring(argument or ""))
			net.Send(owner)
		end)
	else
		net.Receive(CALL_MESSAGE, function()
			local weapon = net.ReadEntity()
			local functionName = net.ReadString()
			local argument = net.ReadString()
			if (IsValid(weapon) and isfunction(weapon[functionName])) then
				weapon[functionName](weapon, argument)
			end
		end)
	end
end

--[[
	Panels ( client )
--]]
if (CLIENT) then
	local PANEL = FindMetaTable("Panel")
	local TEXT_ENTRY = FindMetaTable("TextEntry")

	-- Fades ( Player:ScreenFade sends these ); a full-screen rectangle drawn after the HUD
	local fade
	function Srcbox_ScreenFade(flags, r, g, b, a, fadeTime, holdTime)
		fade = { flags = flags, color = Color(r, g, b, a), start = CurTime(), fadeTime = fadeTime, holdTime = holdTime }
	end

	hook.Add("HUDPaint", "Srcbox_ScreenFade", function()
		if (not fade) then return end
		local elapsed = CurTime() - fade.start
		local fadeIn = bit.band(fade.flags, 1) ~= 0	-- SCREENFADE.IN: from the colour to clear
		local stayOut = bit.band(fade.flags, 8) ~= 0	-- SCREENFADE.STAYOUT
		local fraction
		if (fadeIn) then
			fraction = (elapsed < fade.holdTime) and 1 or 1 - math.Clamp((elapsed - fade.holdTime) / math.max(fade.fadeTime, 0.001), 0, 1)
		else
			fraction = math.Clamp(elapsed / math.max(fade.fadeTime, 0.001), 0, 1)
			if (not stayOut and elapsed > fade.fadeTime + fade.holdTime) then
				fraction = 0
			end
		end
		if (fraction <= 0 and (fadeIn or not stayOut)) then
			fade = nil
			return
		end
		surface.SetDrawColor(fade.color.r, fade.color.g, fade.color.b, fade.color.a * fraction)
		surface.DrawRect(0, 0, ScrW(), ScrH())
	end)

	if (PANEL) then
		setDefault(PANEL, "IsKeyboardInputEnabled", PANEL.IsKeyBoardInputEnabled)
		setDefault(PANEL, "GetBGColor", function(self) return extraOf(self).bgColor or (self.GetBackgroundColor and self:GetBackgroundColor()) end)
		setDefault(PANEL, "GetFGColor", function(self) return extraOf(self).fgColor or (self.GetForegroundColor and self:GetForegroundColor()) end)
		setDefault(PANEL, "SetPopupStayAtBack", function() end)
		setDefault(PANEL, "SetRenderInScreenshots", function() end)
		setDefault(PANEL, "FocusPrevious", function() end)
		setDefault(PANEL, "SetDrawLanguageID", function() end)
	end

	-- cam.Start3D2D: surface drawing outside of panel painting goes straight through the current
	-- matrices, so a model matrix that puts the 2D plane in the world ( y flipped, like the screen's )
	-- is all it takes. Screen clipping makes no sense there, so it's off until End3D2D.
	if (cam and cam.Start3D2D == nil and cam.PushModelMatrix) then
		local clippingStack = {}

		cam.Start3D2D = function(position, angles, scale)
			local matrix = Matrix()
			matrix:SetTranslation(position)
			matrix:SetAngles(angles)
			matrix:Scale(Vector(scale, -scale, 1))
			cam.PushModelMatrix(matrix, true)
			table.insert(clippingStack, Surfaces.DisableClipping(true))
		end

		cam.End3D2D = function()
			cam.PopModelMatrix()
			local wasDisabled = table.remove(clippingStack)
			Surfaces.DisableClipping(wasDisabled == true)
		end
	end

	-- GMod keeps text entry methods on Panel; here they're the TextEntry class's, under other names
	if (TEXT_ENTRY) then
		local textEntryNames = {
			SetMaximumCharCount = "SetMaximumCharacterCount",
			GetMaximumCharCount = "GetMaximumCharacterCount",
			GetNumLines = "GetNumberOfLines",
			SelectAll = "SelectAllText",
			SelectAllOnFocus = "SelectAllOnFocusAlways",
			SetDrawLanguageIDAtLeft = "SetDrawLanguageIdAtLeft",
			SetVerticalScrollbarEnabled = "SetVerticalScrollbar",
		}

		for gmodName, nativeName in pairs(textEntryNames) do
			local native = TEXT_ENTRY[nativeName]
			if (native) then
				setDefault(TEXT_ENTRY, gmodName, native)
				setDefault(PANEL, gmodName, function(self, ...)
					local method = self[nativeName]
					if (method) then
						return method(self, ...)
					end
				end)
			end
		end
	end

	if (PANEL) then
		setDefault(PANEL, "Valid", function(self) return IsValid(self) end)
	end
end
