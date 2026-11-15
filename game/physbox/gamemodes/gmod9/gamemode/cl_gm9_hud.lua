--[[
	GMod 9 compatibility: _GModText / _GModRect renderer (client)

	The server sends each element with Networks ("gm9_hud"); this keeps them by
	kind + index and draws them in GM:HudDraw.

	GMod 9 semantics:
		positions are fractions of the screen; text x/y of -1 means centred
		SetTime(life, fadeIn, fadeOut): fade in, stay for life, fade out
		SendAnimate(player, index, duration, ease) tweens an existing element
			to the newly set position/colour
		Hide(player, index, fadeTime, delay)
	Fonts come from GMod 9's ClientScheme (content/resource/gmod9_clientscheme.res).
--]]

GM9HUD = GM9HUD or {}
local HUD = GM9HUD

HUD.Elements = { text = {}, rect = {} }

local function Now()
	return Engines.GetCurrentTime()
end

--[[
	Fonts and textures
--]]

local fontCache = {}
local gm9Scheme, clientScheme

local function IsUsableFont(font)
	return font ~= nil and Surfaces.GetFontTall(font) > 0
end

local function GetFont(name)
	if (fontCache[name]) then
		return fontCache[name]
	end

	if (gm9Scheme == nil) then
		local ok, scheme = pcall(Schemes.LoadSchemeFromFile, "resource/gmod9_clientscheme.res", "GMod9Scheme")
		gm9Scheme = ok and scheme or false
	end
	if (clientScheme == nil) then
		local ok, scheme = pcall(Schemes.GetScheme, "ClientScheme")
		clientScheme = ok and scheme or false
	end

	local font
	if (gm9Scheme) then
		font = gm9Scheme:GetFont(name)
	end
	if (not IsUsableFont(font) and clientScheme) then
		font = clientScheme:GetFont(name)
		if (not IsUsableFont(font)) then
			font = clientScheme:GetFont("Default")
		end
	end

	fontCache[name] = font
	return font
end

local textureCache = {}

local function GetTexture(material)
	local id = textureCache[material]
	if (not id) then
		id = Surfaces.CreateNewTextureID()
		Surfaces.DrawSetTextureFile(id, material, 1, false)
		textureCache[material] = id
	end
	return id
end

--[[
	Element state
--]]

local function Copy(t)
	if (type(t) ~= "table") then
		return t
	end
	local c = {}
	for k, v in pairs(t) do
		c[k] = v
	end
	return c
end

local function NewElement(message)
	local time = message.time or { 0, 0, 0 }
	return {
		font = message.font,
		text = message.text or "",
		material = message.material or "",
		color = Copy(message.color) or { 255, 255, 255, 255 },
		pos = Copy(message.pos) or { 0, 0, 0, 0 },
		entity = message.entity,
		entityOffset = message.entityOffset,
		start = Now() + (message.delay or 0),
		life = time[1] or 0,
		fadeIn = time[2] or 0,
		fadeOut = time[3] or 0,
	}
end

local function Ease(p, ease)
	ease = ease or 0
	-- ease 0 = linear; higher = stronger slow-down towards the end
	return 1 - (1 - p) ^ (1 + ease * 3)
end

local function Lerp(a, b, t)
	-- -1 is "centred" for text; don't tween through it
	if (a == -1 or b == -1) then
		return t >= 1 and b or a
	end
	return a + (b - a) * t
end

local function LerpList(from, to, t)
	local out = {}
	for i = 1, math.max(#from, #to) do
		local a, b = from[i], to[i]
		if (a == nil) then
			out[i] = b
		elseif (b == nil) then
			out[i] = a
		else
			out[i] = Lerp(a, b, t)
		end
	end
	return out
end

-- Current position/colour, advancing any animation
local function Current(el, now)
	local anim = el.anim
	if (not anim or now < anim.start) then
		return el.pos, el.color
	end

	if (not anim.fromPos) then
		anim.fromPos = Copy(el.pos)
		anim.fromColor = Copy(el.color)
	end

	local p = anim.duration > 0 and math.min((now - anim.start) / anim.duration, 1) or 1
	local t = Ease(p, anim.ease)
	local pos = anim.toPos and LerpList(anim.fromPos, anim.toPos, t) or el.pos
	local color = anim.toColor and LerpList(anim.fromColor, anim.toColor, t) or el.color

	if (p >= 1) then
		el.pos, el.color = pos, color
		if (anim.text) then
			el.text = anim.text
		end
		el.anim = nil
	end

	return pos, color
end

-- 0..1 fade, or nil when the element is finished
local function Alpha(el, now)
	local t = now - el.start
	if (t < 0) then
		return 0
	end

	local alpha
	if (el.fadeIn > 0 and t < el.fadeIn) then
		alpha = t / el.fadeIn
	elseif (t < el.fadeIn + el.life) then
		alpha = 1
	elseif (el.fadeOut > 0 and t < el.fadeIn + el.life + el.fadeOut) then
		alpha = 1 - (t - el.fadeIn - el.life) / el.fadeOut
	elseif (el.anim) then
		alpha = 0	-- keep it while an animation is still running
	else
		return nil
	end

	if (el.hideStart and now >= el.hideStart) then
		if (el.hideFade <= 0 or now >= el.hideStart + el.hideFade) then
			return nil
		end
		alpha = alpha * (1 - (now - el.hideStart) / el.hideFade)
	end

	return alpha
end

--[[
	Messages from the server
--]]

local fallbackSounds = {
	["hl1/fvox/bell.wav"] = "buttons/bell1.wav",
}

function HUD.Receive(message)
	if (message.op == "sound") then
		local sound = message.sound or ""
		if (not Files.FileExists("sound/" .. sound, "GAME") and fallbackSounds[sound]) then
			sound = fallbackSounds[sound]
		end
		Surfaces.PlaySound(sound)
		return
	end

	local list = HUD.Elements[message.kind]
	if (not list or message.index == nil) then
		return
	end

	if (message.op == "send") then
		list[message.index] = NewElement(message)
	elseif (message.op == "animate") then
		local el = list[message.index]
		if (el) then
			el.anim = {
				start = Now() + (message.delay or 0),
				duration = message.duration or 0,
				ease = message.ease,
				toPos = Copy(message.pos),
				toColor = Copy(message.color),
				text = message.text,
			}
		end
	elseif (message.op == "hide") then
		local el = list[message.index]
		if (el) then
			el.hideStart = Now() + (message.delay or 0)
			el.hideFade = message.fade or 0
		end
	end
end

Networks.Receive("gm9_hud", function(length, sender)
	HUD.Receive(Networks.ReadTable())
end)

--[[
	Drawing
--]]

local entityLookup

local function FindEntity(index)
	if (not entityLookup) then
		entityLookup = {}
		for _, e in ipairs(Entities.GetAll()) do
			if (e ~= NULL and e:IsValid()) then
				entityLookup[e:GetEntityIndex()] = e
			end
		end
	end
	return entityLookup[index]
end

local function DrawRect(el, pos, color, alpha, w, h)
	local x, y = pos[1] * w, pos[2] * h
	local rw, rh = (pos[3] or 0) * w, (pos[4] or 0) * h
	if (rw <= 0 or rh <= 0) then
		return
	end

	Surfaces.DrawSetColor(color[1], color[2], color[3], (color[4] or 255) * alpha)
	if (el.material == "") then
		Surfaces.DrawFilledRectangle(x, y, rw, rh)
	else
		Surfaces.DrawSetTexture(GetTexture(el.material))
		Surfaces.DrawTexturedRectangle(x, y, rw, rh)
	end
end

local function DrawText(el, pos, color, alpha, w, h)
	local font = GetFont(el.font or "Default")
	if (not font) then
		return
	end

	local lines = {}
	for line in (el.text .. "\n"):gmatch("(.-)\n") do
		table.insert(lines, line)
	end

	local lineTall = Surfaces.GetFontTall(font)
	local blockTall = lineTall * #lines

	local centreX, top
	if (el.entity) then
		local e = FindEntity(el.entity)
		if (not e) then
			return
		end
		local offset = el.entityOffset or { 0, 0, 0 }
		local screen = (e:GetPosition() + Vectors.Create(offset[1], offset[2], offset[3])):ToScreen()
		if (not screen.visible) then
			return
		end
		centreX = screen.x
		top = screen.y - blockTall / 2
	else
		if (pos[1] == -1) then
			centreX = w / 2
		end
		top = (pos[2] == -1) and (h - blockTall) / 2 or pos[2] * h
	end

	Surfaces.DrawSetTextFont(font)
	Surfaces.DrawSetTextColor(color[1], color[2], color[3], (color[4] or 255) * alpha)

	for i, line in ipairs(lines) do
		local x
		if (centreX) then
			local lineWide = Surfaces.GetTextSize(font, line)
			x = centreX - lineWide / 2
		else
			x = pos[1] * w
		end
		Surfaces.DrawSetTextPosition(x, top + (i - 1) * lineTall)
		Surfaces.DrawPrintText(line)
	end
end

local function DrawList(list, drawFunc, now, w, h)
	local indices = {}
	for index in pairs(list) do
		table.insert(indices, index)
	end
	table.sort(indices)

	for _, index in ipairs(indices) do
		local el = list[index]
		local pos, color = Current(el, now)
		local alpha = Alpha(el, now)

		if (alpha == nil) then
			list[index] = nil
		elseif (alpha > 0) then
			-- One bad element shouldn't stop the rest of the HUD drawing
			local ok, err = pcall(drawFunc, el, pos, color, alpha, w, h)
			if (not ok) then
				print("[GMod9] HUD element " .. tostring(index) .. ": " .. tostring(err))
				list[index] = nil
			end
		end
	end
end

function HUD.Draw()
	local now = Now()
	local w, h = Surfaces.GetScreenSize()
	entityLookup = nil

	DrawList(HUD.Elements.rect, DrawRect, now, w, h)
	DrawList(HUD.Elements.text, DrawText, now, w, h)
end
