--[[
	Srcbox: client-side g_SkyPaint.

	In GMod the client env_skypaint entity sets g_SkyPaint, and the SkyPaint
	material proxy reads it. Srcbox's env_skypaint is a C++ entity and the
	proxy is native, so this table stands in for it with the same API
	(Get/Set per field plus GetDT*). Sky addons such as StormFox write to it.

	Until something calls a setter, SrcboxUntouched stays true and the proxy
	uses the map's own env_skypaint instead.

	Vector/Angle don't exist yet when auto_load runs, so values are kept as
	plain numbers and only turned into Vector/Angle when asked for.
--]]

if (not CLIENT) then
	return
end

local SKY = { SrcboxUntouched = true }

-- DT slots, matching GMod's env_skypaint NetworkVar layout
local vectors = {
	[0] = { 0.2, 0.5, 1.0 },	-- TopColor
	[1] = { 0.8, 1.0, 1.0 },	-- BottomColor
	[2] = { 0.4, 0.0, 0.01 },	-- SunNormal
	[3] = { 0.2, 0.1, 0.0 },	-- SunColor
	[4] = { 1.0, 0.2, 0.0 },	-- DuskColor
}
local floats = { [0] = 1.0, 0.66, 1.0, 1.0, 2.0 }	-- FadeBias, HDRScale, DuskScale, DuskIntensity, SunSize
local drawStars = true
local starLayers = 1
local starTexture = "skybox/starfield"
local starScale, starFade, starSpeed = 0.5, 1.5, 0.01

local function toVector(t)
	return Vector(t[1], t[2], t[3])
end

local function fromVector(v)
	if (type(v) == "table" and v[1] ~= nil) then
		return { tonumber(v[1]) or 0, tonumber(v[2]) or 0, tonumber(v[3]) or 0 }
	end
	return { tonumber(v.x) or 0, tonumber(v.y) or 0, tonumber(v.z) or 0 }
end

local function touched(self)
	self.SrcboxUntouched = false
end

local vectorNames = { TopColor = 0, BottomColor = 1, SunNormal = 2, SunColor = 3, DuskColor = 4 }
for name, i in pairs(vectorNames) do
	SKY["Get" .. name] = function(self) return toVector(vectors[i]) end
	SKY["Set" .. name] = function(self, v) vectors[i] = fromVector(v); touched(self) end
end

local floatNames = { FadeBias = 0, HDRScale = 1, DuskScale = 2, DuskIntensity = 3, SunSize = 4 }
for name, i in pairs(floatNames) do
	SKY["Get" .. name] = function(self) return floats[i] end
	SKY["Set" .. name] = function(self, v) floats[i] = tonumber(v) or floats[i]; touched(self) end
end

function SKY:GetDrawStars() return drawStars end
function SKY:SetDrawStars(b) drawStars = b and true or false; touched(self) end
function SKY:GetStarLayers() return starLayers end
function SKY:SetStarLayers(n) starLayers = tonumber(n) or starLayers; touched(self) end
function SKY:GetStarTexture() return starTexture end
function SKY:SetStarTexture(s) starTexture = tostring(s); touched(self) end
function SKY:GetStarScale() return starScale end
function SKY:SetStarScale(n) starScale = tonumber(n) or starScale; touched(self) end
function SKY:GetStarFade() return starFade end
function SKY:SetStarFade(n) starFade = tonumber(n) or starFade; touched(self) end
function SKY:GetStarSpeed() return starSpeed end
function SKY:SetStarSpeed(n) starSpeed = tonumber(n) or starSpeed; touched(self) end

function SKY:GetDTVector(i) return toVector(vectors[i] or { 0, 0, 0 }) end
function SKY:GetDTFloat(i) return floats[i] or 0 end
function SKY:GetDTBool(i) if (i == 0) then return drawStars end return false end
function SKY:GetDTInt(i) if (i == 0) then return starLayers end return 0 end
function SKY:GetDTAngle(i)
	if (i == 0) then return Angle(starScale, starFade, starSpeed) end
	return Angle(0, 0, 0)
end
function SKY:GetDTString(i) if (i == 0) then return starTexture end return "" end
function SKY:SetDTVector(i, v) if (vectors[i]) then vectors[i] = fromVector(v); touched(self) end end
function SKY:SetDTFloat(i, v) if (floats[i]) then floats[i] = tonumber(v) or floats[i]; touched(self) end end

function SKY:IsValid() return true end
function SKY:GetClass() return "env_skypaint" end

if (g_SkyPaint == nil) then
	g_SkyPaint = SKY
end
