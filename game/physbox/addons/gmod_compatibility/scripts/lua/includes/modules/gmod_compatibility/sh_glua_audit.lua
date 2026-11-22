--[[
	Srcbox: a check of what GMod's Lua and the Base / Sandbox gamemodes call against what this
	game actually has, run in the game ( so it sees everything the engine, the compat layer and
	GMod's own Lua set up ).

	srcbox_glua_audit      ( server )
	srcbox_glua_audit_cl   ( client )

	Writes data/srcbox_glua_audit_server.txt / _client.txt: every engine method, library function
	and global that's called somewhere but doesn't exist here, with how often it's called and one
	place it's called from. The names come from sh_glua_audit_data.lua ( generated from GMod's
	lua/ and gamemodes/; methods GMod's own Lua defines are left out, since those load with it ).
--]]

-- Loaded now, while include() can find files beside this one
local AUDIT_OK, AUDIT_DATA = pcall(include, "sh_glua_audit_data.lua")

-- Things to look methods up on. A lot of metatables find their methods through an __index
-- function, which pairs() can't list, so each name is also looked up on real objects.
local function collectSamples()
	local samples = {}

	local function add(value)
		if (value ~= nil) then
			samples[#samples + 1] = value
		end
	end

	local function try(maker)
		local ok, value = pcall(maker)
		if (ok) then
			add(value)
		end
	end

	try(function() return game.GetWorld() end)
	try(function() return Entity(0) end)
	try(function() return Vector(1, 2, 3) end)
	try(function() return Angle(1, 2, 3) end)
	try(function() return Color(1, 2, 3, 4) end)
	try(function() return DamageInfo() end)
	try(function() return EffectData() end)
	try(function() return Material("debug/debugwhite") end)
	try(function() return Material("debug/debugwhite"):GetTexture("$basetexture") end)
	try(function() return Matrix() end)
	try(function() return ConVarExists and GetConVar("sv_gravity") end)

	local client = CLIENT and LocalPlayer() or player.GetAll()[1]
	if (IsValid(client)) then
		add(client)
		try(function() return client:GetActiveWeapon() end)
		try(function() return client:GetPhysicsObject() end)
	end

	for _, pattern in ipairs({ "prop_physics", "npc_*", "prop_vehicle_*", "gmod_*", "weapon_*" }) do
		local found = ents.FindByClass(pattern)
		if (found and IsValid(found[1])) then
			add(found[1])
			try(function() return found[1]:GetPhysicsObject() end)
		end
	end

	for _, pattern in ipairs({ "prop_ragdoll", "func_*", "env_skypaint", "gmod_hands" }) do
		local found = ents.FindByClass(pattern)
		if (found and IsValid(found[1])) then
			add(found[1])
		end
	end

	-- A looping sound object ( CSoundPatch )
	try(function()
		local owner = IsValid(client) and client or game.GetWorld()
		return CreateSound(owner, "common/null.wav")
	end)

	-- A file handle
	try(function()
		local f = file.Open("srcbox_glua_audit_probe.txt", "wb", "DATA")
		if (f) then f:Close() end
		return file.Open("srcbox_glua_audit_probe.txt", "rb", "DATA")
	end)

	if (CLIENT) then
		local function panel(class)
			try(function()
				local p = vgui.Create(class)
				timer.Simple(0, function() if (IsValid(p)) then p:Remove() end end)
				return p
			end)
		end

		for _, class in ipairs({ "DPanel", "DLabel", "DTextEntry", "RichText", "DFrame", "DButton", "DImage", "DModelPanel" }) do
			panel(class)
		end

		-- A particle and its emitter
		try(function()
			local emitter = ParticleEmitter(Vector(0, 0, 0))
			if (not emitter) then return nil end
			add(emitter)
			local particle = emitter:Add("effects/spark", Vector(0, 0, 0))
			if (particle) then particle:SetDieTime(0) end
			emitter:Finish()
			return particle
		end)
	end

	return samples
end

local function collectMethods()
	local known, seen = {}, {}

	local function add(t)
		if (type(t) ~= "table" or seen[t]) then
			return
		end

		seen[t] = true

		for key, value in pairs(t) do
			if (type(key) == "string" and type(value) == "function") then
				known[key] = true
			end
		end

		local meta = getmetatable(t)
		if (meta and type(meta.__index) == "table") then
			add(meta.__index)
		end

		if (type(rawget(t, "__index")) == "table") then
			add(rawget(t, "__index"))
		end
	end

	local registryCount = 0
	for name, value in pairs(debug.getregistry()) do
		if (type(name) == "string" and type(value) == "table") then
			registryCount = registryCount + 1
			add(value)
		end
	end
	known.__srcbox_registry_count = registryCount

	-- Methods on strings
	add(string)

	-- Every metatable by name ( GMod's names are mapped by FindMetaTable )
	local metaNames = {
		"Entity", "Player", "Weapon", "NPC", "NextBot", "Vehicle", "PhysObj", "PhysicsObject", "CBaseAnimating", "CBaseFlex",
		"CExperimentPlayer", "Panel", "EditablePanel", "Label", "TextEntry", "Html", "Frame", "Button", "CheckButton",
		"ModelImagePanel", "AvatarImage", "Vector", "Angle", "Color", "Matrix", "VMatrix", "Material", "IMaterial",
		"Texture", "ITexture", "CEffectData", "EffectData", "CMoveData", "MoveData", "CUserCmd", "UserCommand",
		"CTakeDamageInfo", "TakeDamageInfo", "CRecipientFilter", "RecipientFilter", "ConVar", "ConsoleVariable",
		"CSoundPatch", "File", "FileHandle", "CLuaEmitter", "CLuaParticle", "LuaEmitter", "LuaParticle",
		"IGModAudioChannel", "AudioChannel", "ProjectedTexture", "MessageWriter", "MessageReader", "bf_read",
		"KeyValuesHandle", "NetChannelInfo", "Trace", "Mesh", "LuaMesh", "PhysicsSurfacePropertiesHandle"
	}

	local metaCount, metas = 0, {}
	for _, name in ipairs(metaNames) do
		local ok, meta = pcall(FindMetaTable, name)
		if (ok and type(meta) == "table") then
			metaCount = metaCount + 1
			metas[#metas + 1] = meta
			add(meta)
		end
	end
	known.__srcbox_meta_count = metaCount

	local samples = collectSamples()
	local sampleCount = #samples

	-- Index the metatables too, so names found through an __index function count
	for _, meta in ipairs(metas) do
		samples[#samples + 1] = meta
	end

	return function(name)
		if (name == nil) then
			return known.__srcbox_registry_count, known.__srcbox_meta_count, sampleCount
		end

		if (known[name]) then
			return true
		end

		for _, sample in ipairs(samples) do
			local ok, value = pcall(function() return sample[name] end)
			if (ok and value ~= nil) then
				known[name] = true
				return true
			end
		end

		return false
	end
end

local function runAudit(realm)
	local data = AUDIT_DATA

	if (not AUDIT_OK or type(data) ~= "table") then
		MsgN("[GLua audit] Couldn't load sh_glua_audit_data.lua: " .. tostring(data))
		return
	end

	local methods = collectMethods()
	local lines = {
		"GLua audit ( " .. realm .. " ), " .. os.date("%Y-%m-%d %H:%M"),
		"",
		"Only names GMod calls on this side are checked. Calls made only by GMod's own main menu are left out.",
		"Each section lists calls from " .. realm .. " code first, then calls from code that could run on either side",
		"( shared files with no CLIENT / SERVER check, so some of those may only ever run on the other side ).",
		""
	}
	local counts = {}

	-- Entry tail: client uses, client example, server uses, server example, either-side uses, example, menu uses
	local mine = (realm == "client") and 0 or 2

	local registryCount, metaCount, sampleCount = methods(nil)
	lines[#lines + 1] = string.format("Looked methods up on %d registry tables, %d metatables by name and %d live objects.",
		registryCount or 0, metaCount or 0, sampleCount or 0)
	lines[#lines + 1] = ""

	local function section(title, list, isMissing, label)
		local sure, either = {}, {}

		for _, entry in ipairs(list) do
			local uses, where = entry.t[1 + mine], entry.t[2 + mine]
			local anyUses, anyWhere = entry.t[5], entry.t[6]

			if ((uses > 0 or anyUses > 0) and isMissing(entry)) then
				if (uses > 0) then
					sure[#sure + 1] = { text = label(entry), uses = uses, where = where }
				else
					either[#either + 1] = { text = label(entry), uses = anyUses, where = anyWhere }
				end
			end
		end

		local function byUses(x, y) return x.uses > y.uses end
		table.sort(sure, byUses)
		table.sort(either, byUses)

		lines[#lines + 1] = string.format("== %s: %d missing from %s code, %d more from either-side code", title, #sure, realm, #either)
		for _, e in ipairs(sure) do
			lines[#lines + 1] = string.format("  %s  ( %d uses, e.g. %s )", e.text, e.uses, e.where)
		end
		if (#either > 0) then
			lines[#lines + 1] = "  -- from code that could run on either side:"
			for _, e in ipairs(either) do
				lines[#lines + 1] = string.format("  %s  ( %d uses, e.g. %s )", e.text, e.uses, e.where)
			end
		end
		lines[#lines + 1] = ""

		counts[#counts + 1] = string.format("%s %d (+%d)", title, #sure, #either)
	end

	local function tailOf(e, first)
		return { e[first], e[first + 1], e[first + 2], e[first + 3], e[first + 4], e[first + 5], e[first + 6] }
	end

	local methodList, libraryList, globalList = {}, {}, {}

	for _, e in ipairs(data.methods) do methodList[#methodList + 1] = { name = e[1], t = tailOf(e, 2) } end
	for _, e in ipairs(data.libraries) do libraryList[#libraryList + 1] = { lib = e[1], name = e[2], t = tailOf(e, 3) } end
	for _, e in ipairs(data.globals) do globalList[#globalList + 1] = { name = e[1], t = tailOf(e, 2) } end

	section("Methods", methodList, function(e) return not methods(e.name) end,
		function(e) return ":" .. e.name end)

	section("Library functions", libraryList, function(e)
		local lib = rawget(_G, e.lib)

		-- net.Write* / Read* only exist while a message is being written or read
		if (e.lib == "net" and (e.name:find("^Write") or e.name:find("^Read"))) then
			local writer = FindMetaTable("MessageWriter")
			local reader = FindMetaTable("MessageReader")
			return not ((writer and writer[e.name]) or (reader and reader[e.name]))
		end

		return type(lib) ~= "table" or lib[e.name] == nil
	end, function(e) return e.lib .. "." .. e.name end)

	section("Globals", globalList, function(e) return rawget(_G, e.name) == nil end,
		function(e) return e.name end)

	local fileName = "srcbox_glua_audit_" .. realm .. ".txt"
	file.Write(fileName, table.concat(lines, "\n"))

	MsgN("[GLua audit] " .. realm .. " missing ( + from either-side code ): " .. table.concat(counts, ", ") .. ". Full list: data/" .. fileName)
end

if (SERVER) then
	concommand.Add("srcbox_glua_audit", function(client)
		if (IsValid(client) and not client:IsAdmin()) then
			return
		end

		runAudit("server")
	end)
else
	concommand.Add("srcbox_glua_audit_cl", function()
		runAudit("client")
	end)
end

--[[
	srcbox_glua_wiki_audit ( server ) / srcbox_glua_wiki_audit_cl ( client )

	Checks every function documented on the Garry's Mod wiki ( sh_glua_wiki_data.lua, generated
	from luttje/glua-api-snippets ) in the realms the wiki says it exists in, and writes
	data/srcbox_glua_wiki_audit_<realm>.txt: a summary per library / class, then what's missing.
	Hooks ( GM:, ENTITY:, SWEP: ... ) are listed from a static guess of whether anything calls them.
--]]

local WIKI_OK, WIKI_DATA = pcall(include, "sh_glua_wiki_data.lua")

-- Where each wiki class's methods live here ( FindMetaTable maps GMod's names )
local ENTITY_METAS = { "Entity", "CBaseAnimating", "CBaseFlex" }
local CLASS_METAS = {
	Entity = ENTITY_METAS,
	CSEnt = ENTITY_METAS,
	Player = { "Player", "CExperimentPlayer", "Entity", "CBaseAnimating", "CBaseFlex" },
	Weapon = { "Weapon", "Entity", "CBaseAnimating" },
	NPC = { "NPC", "Entity", "CBaseAnimating", "CBaseFlex" },
	NextBot = { "NextBot", "Entity", "CBaseAnimating" },
	Vehicle = { "Vehicle", "Entity", "CBaseAnimating" },
	Panel = { "Panel", "EditablePanel", "Label", "TextEntry", "Html", "Frame", "Button", "CheckButton", "ModelImagePanel", "AvatarImage" },
}

local function wikiSamples()
	local samples = {}

	local function try(class, maker)
		local ok, value = pcall(maker)
		if (ok and value ~= nil) then
			samples[class] = samples[class] or {}
			table.insert(samples[class], value)
		end
	end

	try("Vector", function() return Vector(1, 2, 3) end)
	try("Angle", function() return Angle(1, 2, 3) end)
	try("Color", function() return Color(1, 2, 3, 4) end)
	try("VMatrix", function() return Matrix() end)
	try("CTakeDamageInfo", function() return DamageInfo() end)
	try("CEffectData", function() return EffectData() end)
	try("IMaterial", function() return Material("debug/debugwhite") end)
	try("ITexture", function() return Material("debug/debugwhite"):GetTexture("$basetexture") end)
	try("ConVar", function() return GetConVar("sv_gravity") end)
	try("Stack", function() return util.Stack() end)
	try("Schedule", function() return ai_schedule.New("srcbox_audit") end)
	try("Task", function() return ai_task.New() end)
	try("PathFollower", function() return Path("Follow") end)
	try("Tool", function()
		-- A tool object: the held tool gun's, or one made from the stored tool gun's list
		for _, client in ipairs(player.GetAll()) do
			local weapon = client:GetActiveWeapon()
			if (IsValid(weapon) and weapon.GetToolObject) then
				local tool = weapon:GetToolObject()
				if (tool) then return tool end
			end
		end
		local stored = weapons.GetStored and weapons.GetStored("gmod_tool")
		if (stored and stored.Tool) then
			local _, tool = next(stored.Tool)
			return tool
		end
	end)
	try("WorkshopFileBase", function() return WorkshopFileBase("srcbox_audit", {}) end)
	try("File", function()
		local f = file.Open("srcbox_glua_audit_probe.txt", "wb", "DATA")
		if (f) then f:Close() end
		return file.Open("srcbox_glua_audit_probe.txt", "rb", "DATA")
	end)

	local player1 = CLIENT and LocalPlayer() or player.GetAll()[1]
	if (IsValid(player1)) then
		try("Player", function() return player1 end)
		try("Entity", function() return player1 end)
		try("Weapon", function() local w = player1:GetActiveWeapon() return IsValid(w) and w or nil end)
		try("CSoundPatch", function() return CreateSound(player1, "common/null.wav") end)
	end

	for _, class in ipairs({ "prop_physics", "prop_ragdoll", "prop_dynamic" }) do
		local found = ents.FindByClass(class)[1]
		if (IsValid(found)) then
			try("Entity", function() return found end)
			try("PhysObj", function() local p = found:GetPhysicsObject() return IsValid(p) and p or nil end)
		end
	end

	for _, ent in ipairs(ents.GetAll()) do
		if (ent:IsNPC()) then try("NPC", function() return ent end) break end
	end
	for _, ent in ipairs(ents.GetAll()) do
		if (ent:IsVehicle()) then try("Vehicle", function() return ent end) break end
	end

	if (SERVER) then
		try("CRecipientFilter", function() return RecipientFilter() end)
	else
		try("CSEnt", function()
			local ent = ClientsideModel("models/props_junk/watermelon01.mdl")
			timer.Simple(0, function() if (IsValid(ent)) then ent:Remove() end end)
			return ent
		end)
		try("ProjectedTexture", function()
			local pt = ProjectedTexture()
			timer.Simple(0, function() if (pt and pt.Remove) then pt:Remove() end end)
			return pt
		end)
		try("MarkupObject", function() return markup.Parse("x") end)
		try("IMesh", function() return Mesh() end)
		try("CLuaEmitter", function()
			local emitter = ParticleEmitter(Vector(0, 0, 0))
			if (emitter) then
				try("CLuaParticle", function() return emitter:Add("effects/spark", Vector(0, 0, 0)) end)
				emitter:Finish()
			end
			return emitter
		end)
		try("Panel", function()
			local panel = vgui.Create("DPanel")
			timer.Simple(0, function() if (IsValid(panel)) then panel:Remove() end end)
			return panel
		end)
	end

	return samples
end

local function has(object, name)
	if (object == nil) then
		return false
	end

	local ok, value = pcall(function() return object[name] end)
	return ok and value ~= nil
end

local function methodExists(owner, name, samples)
	for _, metaName in ipairs(CLASS_METAS[owner] or { owner }) do
		local ok, meta = pcall(FindMetaTable, metaName)
		if (ok and type(meta) == "table" and has(meta, name)) then
			return true
		end
	end

	for _, sample in ipairs(samples[owner] or {}) do
		if (has(sample, name)) then
			return true
		end
	end

	return false
end

-- A Derma control's method: its own table, then its bases ( ending at the engine's panel )
local function controlMethodExists(owner, name, samples)
	if (not vgui or not vgui.GetControlTable) then
		return false, false
	end

	local control = vgui.GetControlTable(owner)
	if (not control) then
		return false, false
	end

	local seen = {}
	while (control and not seen[control]) do
		seen[control] = true
		if (rawget(control, name) ~= nil) then
			return true, true
		end

		local base = rawget(control, "Base")
		control = base and vgui.GetControlTable(base)
	end

	return methodExists("Panel", name, samples), true
end

local function libraryValue(path, name)
	local value = _G
	for part in string.gmatch(path, "[^.]+") do
		if (type(value) ~= "table") then
			return nil
		end
		value = rawget(value, part)
	end

	if (type(value) ~= "table") then
		return nil
	end

	-- net.Write* / Read* live on the message objects here
	if (path == "net" and (name:find("^Write") or name:find("^Read"))) then
		local writer, reader = FindMetaTable("MessageWriter"), FindMetaTable("MessageReader")
		return (writer and writer[name]) or (reader and reader[name]) or value[name]
	end

	local ok, result = pcall(function() return value[name] end)
	return ok and result or nil
end

local function runWikiAudit(realm)
	if (not WIKI_OK or type(WIKI_DATA) ~= "table") then
		MsgN("[GLua wiki audit] Couldn't load sh_glua_wiki_data.lua: " .. tostring(WIKI_DATA))
		return
	end

	local letter = (realm == "client") and "c" or "s"
	local samples = wikiSamples()

	local KIND_NAMES = { g = "Global functions", l = "Library functions", m = "Class methods", p = "Derma control methods" }
	local totals = {}
	local owners = {}
	local missingControls = {}

	for _, entry in ipairs(WIKI_DATA.functions) do
		local kind, owner, name, realms, flags = entry[1], entry[2], entry[3], entry[4], entry[5]

		if (realms:find(letter, 1, true) and not (kind == "p" and realm == "server")) then
			local present

			if (kind == "g") then
				present = rawget(_G, name) ~= nil
			elseif (kind == "l") then
				present = libraryValue(owner, name) ~= nil
			elseif (kind == "p" or (vgui and vgui.GetControlTable and CLIENT and vgui.GetControlTable(owner))) then
				local registered
				present, registered = controlMethodExists(owner, name, samples)
				if (not registered) then
					missingControls[owner] = true
				end
				kind = "p"
			else
				present = methodExists(owner, name, samples)
			end

			local minor = flags ~= ""
			local total = totals[kind] or { present = 0, all = 0, minorMissing = 0 }
			totals[kind] = total
			total.all = total.all + 1
			if (present) then
				total.present = total.present + 1
			elseif (minor) then
				total.minorMissing = total.minorMissing + 1
			end

			local key = (kind == "g") and "( globals )" or owner
			local info = owners[key] or { kind = kind, all = 0, missing = {} }
			owners[key] = info
			info.all = info.all + 1
			if (not present) then
				local tag = ""
				if (flags:find("d", 1, true)) then tag = tag .. " [deprecated]" end
				if (flags:find("i", 1, true)) then tag = tag .. " [internal]" end
				local separator = (kind == "g") and "" or ((kind == "l") and "." or ":")
				table.insert(info.missing, ((kind == "g") and "" or owner) .. separator .. name .. tag)
			end
		end
	end

	local lines = {
		"GLua wiki audit ( " .. realm .. " ), " .. os.date("%Y-%m-%d %H:%M"),
		"",
		"Every function documented on the Garry's Mod wiki that exists in this realm, checked against this game.",
		"[deprecated] / [internal] ones are counted but matter less. Spawn a prop, a ragdoll and an NPC first,",
		"so their methods can be checked on real objects.",
		"",
		"== Summary",
	}

	for _, kind in ipairs({ "g", "l", "m", "p" }) do
		local total = totals[kind]
		if (total) then
			table.insert(lines, string.format("  %-24s %4d of %4d present ( %d missing, %d of them deprecated / internal )",
				KIND_NAMES[kind], total.present, total.all, total.all - total.present, total.minorMissing))
		end
	end

	local ownerList = {}
	for name, info in pairs(owners) do
		table.insert(ownerList, { name = name, info = info })
	end
	table.sort(ownerList, function(a, b)
		if (#a.info.missing ~= #b.info.missing) then return #a.info.missing > #b.info.missing end
		return a.name < b.name
	end)

	table.insert(lines, "")
	table.insert(lines, "== By library / class ( missing of documented )")
	for _, item in ipairs(ownerList) do
		local missing = #item.info.missing
		if (missing > 0) then
			local note = missingControls[item.name] and "   ( control not registered )" or ""
			table.insert(lines, string.format("  %-30s %4d of %4d missing%s", item.name, missing, item.info.all, note))
		end
	end

	local complete = 0
	for _, item in ipairs(ownerList) do
		if (#item.info.missing == 0) then complete = complete + 1 end
	end
	table.insert(lines, string.format("  ( %d more libraries / classes have everything )", complete))

	table.insert(lines, "")
	table.insert(lines, "== Missing, by library / class")
	for _, item in ipairs(ownerList) do
		if (#item.info.missing > 0) then
			table.sort(item.info.missing)
			table.insert(lines, "")
			table.insert(lines, item.name .. " ( " .. #item.info.missing .. " )")
			for _, name in ipairs(item.info.missing) do
				table.insert(lines, "  " .. name)
			end
		end
	end

	-- Hooks: a static guess ( generated ) of whether the engine or GMod's / the compat Lua calls them
	local hookStats, hookMissing = {}, {}
	for _, entry in ipairs(WIKI_DATA.hooks or {}) do
		local owner, name, realms, flags, called = entry[1], entry[2], entry[3], entry[4], entry[5]
		if (realms:find(letter, 1, true)) then
			local stat = hookStats[owner] or { called = 0, all = 0 }
			hookStats[owner] = stat
			stat.all = stat.all + 1
			if (called) then
				stat.called = stat.called + 1
			else
				hookMissing[owner] = hookMissing[owner] or {}
				table.insert(hookMissing[owner], name .. ((flags:find("d", 1, true)) and " [deprecated]" or ""))
			end
		end
	end

	table.insert(lines, "")
	table.insert(lines, "== Hooks ( a guess from the source: called somewhere by the engine, GMod's Lua or the compat layer )")
	local hookOwners = {}
	for owner in pairs(hookStats) do table.insert(hookOwners, owner) end
	table.sort(hookOwners)
	for _, owner in ipairs(hookOwners) do
		local stat = hookStats[owner]
		table.insert(lines, string.format("  %-10s %4d of %4d called", owner, stat.called, stat.all))
	end
	for _, owner in ipairs(hookOwners) do
		if (hookMissing[owner]) then
			table.sort(hookMissing[owner])
			table.insert(lines, "")
			table.insert(lines, owner .. " hooks nothing calls ( " .. #hookMissing[owner] .. " )")
			for _, name in ipairs(hookMissing[owner]) do
				table.insert(lines, "  " .. owner .. ":" .. name)
			end
		end
	end

	local fileName = "srcbox_glua_wiki_audit_" .. realm .. ".txt"
	file.Write(fileName, table.concat(lines, "\n"))

	local summary = {}
	for _, kind in ipairs({ "g", "l", "m", "p" }) do
		local total = totals[kind]
		if (total) then table.insert(summary, string.format("%s %d/%d", KIND_NAMES[kind], total.present, total.all)) end
	end
	MsgN("[GLua wiki audit] " .. realm .. " present: " .. table.concat(summary, ", ") .. ". Full list: data/" .. fileName)
end

if (SERVER) then
	concommand.Add("srcbox_glua_wiki_audit", function(client)
		if (IsValid(client) and not client:IsAdmin()) then
			return
		end

		runWikiAudit("server")
	end)
else
	concommand.Add("srcbox_glua_wiki_audit_cl", function()
		runWikiAudit("client")
	end)
end
