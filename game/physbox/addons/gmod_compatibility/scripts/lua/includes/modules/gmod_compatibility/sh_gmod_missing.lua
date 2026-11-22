--[[
	Srcbox: GMod library functions and globals that srcbox_glua_audit found missing in both
	realms. The engine ones ( util.BlastDamage, util.SpriteTrail, ParticleEffect, game.CleanUpMap,
	DynamicLight ) are C++ in game/shared/lutil_shared.cpp; the rest are Lua here. Each is only
	set when the game doesn't already have it.
--]]

local function setDefault(tableValue, key, value)
	if (tableValue and tableValue[key] == nil and value ~= nil) then
		tableValue[key] = value
	end
end

--[[
	Globals
--]]
DamageInfo = DamageInfo or function()
	return TakeDamageInfos.Create()
end

ParticleEffect = ParticleEffect or Utilities.ParticleEffect

-- GMod's Error( ... ): an error built from its arguments
Error = Error or function(...)
	local parts = {}
	for index = 1, select("#", ...) do
		parts[index] = tostring(select(index, ...))
	end
	error(table.concat(parts), 2)
end

if (SERVER) then
	-- Lag compensation helper for prediction; nothing to suppress here
	SuppressHostEvents = SuppressHostEvents or function() end
end

if (CLIENT) then
	DynamicLight = DynamicLight or Utilities.DynamicLight

	-- notification.AddLegacy / AddProgress put their panels on this
	GetOverlayPanel = GetOverlayPanel or function()
		return vgui.GetWorldPanel()
	end
end

--[[
	game / engine
--]]
if (SERVER) then
	setDefault(game, "CleanUpMap", Utilities.CleanUpMap)
end

setDefault(engine, "GetDemoPlaybackTick", function() return 0 end)
setDefault(engine, "GetDemoPlaybackTotalTicks", function() return 0 end)
setDefault(engine, "VideoSettings", function() return nil end)

if (video) then
	setDefault(video, "Record", function() return nil, "Video recording isn't supported" end)
end

if (system) then
	setDefault(system, "FlashWindow", function() end)
end

--[[
	util
--]]
setDefault(util, "IntersectRayWithOBB", util.IntersectRayWithObb)
setDefault(util, "FilterText", function(text) return text end)

setDefault(util, "IsPlayerSpeaking", function(client)
	return IsValid(client) and client.IsSpeaking and client:IsSpeaking() or false
end)

-- "STEAM_0:Y:Z" -> the 64-bit SteamID as a string
setDefault(util, "SteamIDTo64", function(steamID)
	local y, z = tostring(steamID or ""):match("^STEAM_%d+:(%d):(%d+)$")

	if (not y) then
		return "0"
	end

	return tostring(76561197960265728 + tonumber(z) * 2 + tonumber(y))
end)

-- distance from the line, the nearest point on it, and how far along the line that point is
setDefault(util, "DistanceToLine", function(lineStart, lineEnd, point)
	local direction = lineEnd - lineStart
	local length = direction:Length()

	if (length <= 0) then
		return point:Distance(lineStart), lineStart, 0
	end

	direction = direction / length
	local along = math.Clamp((point - lineStart):Dot(direction), 0, length)
	local nearest = lineStart + direction * along

	return point:Distance(nearest), nearest, along
end)

-- the direction through a point on screen, for a camera at these angles and field of view
setDefault(util, "AimVector", function(angles, fov, x, y, width, height)
	local distance = 4 * height / (6 * math.tan(0.5 * math.rad(fov)))
	local forward, right, up = angles:Forward(), angles:Right(), angles:Up()

	return (forward * distance + right * (x - 0.5 * width) + up * (0.5 * height - y)):GetNormalized()
end)

-- a table as KeyValues text ( the format util.KeyValuesToTable reads )
setDefault(util, "TableToKeyValues", function(data, rootName)
	local lines = {}

	local function write(name, value, indent)
		local pad = string.rep("\t", indent)

		if (istable(value)) then
			lines[#lines + 1] = pad .. "\"" .. tostring(name) .. "\""
			lines[#lines + 1] = pad .. "{"

			for key, inner in pairs(value) do
				write(key, inner, indent + 1)
			end

			lines[#lines + 1] = pad .. "}"
		else
			lines[#lines + 1] = pad .. "\"" .. tostring(name) .. "\"\t\t\"" .. tostring(value):gsub("\"", "\\\"") .. "\""
		end
	end

	write(rootName or "TableToKeyValues", data or {}, 0)
	return table.concat(lines, "\n")
end)

if (SERVER) then
	setDefault(util, "BlastDamage", Utilities.BlastDamage)
	setDefault(util, "SpriteTrail", Utilities.SpriteTrail)
end

--[[
	render ( client )
--]]
if (CLIENT) then
	-- render.Model( { model, pos, angle }, entity ): draws a model where asked, with a reused
	-- clientside model when no entity is given
	local cachedModels = {}

	setDefault(render, "Model", function(settings, entity)
		if (not istable(settings) or not settings.model) then
			return
		end

		if (not IsValid(entity)) then
			entity = cachedModels[settings.model]

			if (not IsValid(entity)) then
				entity = ClientsideModel(settings.model, RENDERGROUP_OTHER)

				if (not IsValid(entity)) then
					return
				end

				entity:SetNoDraw(true)
				cachedModels[settings.model] = entity
			end
		end

		if (entity:GetModel() ~= settings.model) then
			entity:SetModel(settings.model)
		end

		entity:SetPos(settings.pos or vector_origin)
		entity:SetAngles(settings.angle or angle_zero)
		entity:SetupBones()
		entity:DrawModel()
	end)

	setDefault(render, "ClearRenderTarget", function(target, color)
		color = color or Color(0, 0, 0, 0)
		render.PushRenderTarget(target)
		render.Clear(color.r, color.g, color.b, color.a or 255, true, true)
		render.PopRenderTarget()
	end)

	setDefault(render, "ComputeLighting", function(position)
		return render.GetLightColor(position)
	end)

	-- These have nothing to do here
	setDefault(render, "Spin", function() end)
	setDefault(render, "BindLocalCubemap", function() end)
	setDefault(render, "RenderHUD", function() end)
end
