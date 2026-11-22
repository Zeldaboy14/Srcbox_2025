--[[
	Srcbox: Garry's Mod's dupe files, for the Duplicator tool ( sandbox's stools/duplicator/*.lua )
	and the spawnmenu's Dupes tab.

	In GMod these come from the engine:
	- engine.WriteDupe( compressed, picture ): saves dupes/<md5>.dupe and dupes/<md5>.jpg
	- engine.OpenDupe( file ): { data = <compressed dupe> }
	A .dupe file is "DUP3" followed by util.Compress'd JSON ( the 5 LZMA property bytes, the
	uncompressed size as 8 bytes, the LZMA stream ).

	The Dupes tab also lists the dupes in your Garry's Mod install ( the SRCBOX_GMOD search path
	the main menu adds ), named "GMOD/dupes/<file>", with their pictures.
--]]

local Files = require("Files")

local DUPE_MAGIC = "DUP3"
local GMOD_PREFIX = "GMOD/"
local GMOD_PATH = "SRCBOX_GMOD"

local function isValidHandle(handle)
	return handle ~= nil and handle ~= FILESYSTEM_INVALID_HANDLE
end

-- The whole file, binary safe ( nil if it isn't there )
local function readBinary(path, pathID)
	local ok, handle = pcall(Files.Open, path, "rb", pathID)

	if (not ok or not isValidHandle(handle)) then
		return nil
	end

	local size = Files.Size(handle) or 0
	local data = ""

	if (size > 0) then
		local _, content = Files.Read(size, handle)
		data = content or ""
	end

	Files.Close(handle)
	return data
end

local function writeBinary(path, pathID, data)
	local ok, handle = pcall(Files.Open, path, "wb", pathID)

	if (not ok or not isValidHandle(handle)) then
		return false
	end

	local written = Files.Write(data, handle)
	Files.Close(handle)

	return written == #data
end

local function fileTime(path, pathID)
	if (not Files.Time) then
		return 0
	end

	local ok, time = pcall(Files.Time, path, pathID)
	return ok and tonumber(time) or 0
end

--[[
	engine.WriteDupe / engine.OpenDupe
--]]
function engine.WriteDupe(dupe, picture)
	if (type(dupe) ~= "string" or dupe == "") then
		return false
	end

	local name = "dupes/" .. util.MD5(dupe)

	if (not writeBinary(name .. ".dupe", "MOD", DUPE_MAGIC .. dupe)) then
		MsgN("Couldn't save the dupe to " .. name .. ".dupe")
		return false
	end

	if (type(picture) == "string" and picture ~= "") then
		writeBinary(name .. ".jpg", "MOD", picture)
	end

	return true
end

function engine.OpenDupe(name)
	name = tostring(name or ""):gsub("\\", "/")

	if (name == "" or name:find("..", 1, true)) then
		return nil
	end

	local data

	if (name:sub(1, #GMOD_PREFIX) == GMOD_PREFIX) then
		data = readBinary(name:sub(#GMOD_PREFIX + 1), GMOD_PATH)
	else
		data = readBinary(name, "MOD") or readBinary(name, "GAME")
	end

	-- "DUP" and a version digit, then the compressed dupe
	if (not data or #data <= 4 or data:sub(1, 3) ~= "DUP") then
		return nil
	end

	return { data = data:sub(5), name = name }
end

--[[
	render.Capture ( RenderExtensions.Capture in C++; it always gives a PNG )
--]]
if (not render.Capture and RenderExtensions and RenderExtensions.Capture) then
	render.Capture = RenderExtensions.Capture
end

-- The dupe picture only places ragdolls' bones when it can; a plain model shows the rest pose
if (not ClientsideRagdoll and ClientsideModel) then
	function ClientsideRagdoll(model, renderGroup)
		return ClientsideModel(model, renderGroup)
	end
end

--[[
	The spawnmenu's Dupes tab: Physbox's dupes and the Garry's Mod install's, with pictures
--]]
local function pictureURL(path, pathID)
	local data = readBinary(path, pathID)

	if (not data or data == "") then
		return nil
	end

	local mime = data:sub(1, 4) == "\137PNG" and "image/png" or "image/jpeg"
	return "data:" .. mime .. ";base64," .. Serializers.Base64Encode(data, true)
end

local function findDupes(pathID, prefix, note, entries, seen)
	local ok, files = pcall(Files.Find, "dupes/*.dupe", pathID)

	if (not ok or type(files) ~= "table") then
		return
	end

	for _, name in ipairs(files) do
		local lowered = name:lower()

		if (not seen[lowered]) then
			seen[lowered] = true

			local path = "dupes/" .. name
			entries[#entries + 1] = {
				file = prefix .. path,
				name = name:gsub("%.[^%.]+$", ""),
				picture = { path = path:gsub("%.dupe$", ".jpg"), pathID = pathID },
				description = note,
				time = fileTime(path, pathID),
			}
		end
	end
end

local function fetchLocalDupes(self, offset, perPage)
	local entries, seen = {}, {}

	findDupes("MOD", "", "Saved in Physbox.", entries, seen)
	findDupes(GMOD_PATH, GMOD_PREFIX, "From your Garry's Mod install.", entries, seen)

	-- Newest first ( GMod's "datedesc" )
	table.sort(entries, function(a, b)
		if (a.time ~= b.time) then
			return a.time > b.time
		end

		return a.name:lower() < b.name:lower()
	end)

	offset = tonumber(offset) or 0
	perPage = tonumber(perPage) or 20

	local results = {}

	for index = offset + 1, math.min(#entries, offset + perPage) do
		local entry = entries[index]

		results[#results + 1] = {
			file = entry.file,
			name = entry.name,
			preview = pictureURL(entry.picture.path, entry.picture.pathID),
			description = entry.description,
		}
	end

	if (IsValid(self.HTML)) then
		self.HTML:Call("dupe.ReceiveLocal( " .. util.TableToJSON({ totalresults = #entries, results = results }, false) .. " )")
	end
end

-- The Dupes tab sets its own FetchLocal ( Physbox's dupes/ only, pictures as paths the cached
-- page can't reach ); its "local" list comes from fetchLocalDupes instead
if (WorkshopFileBase and not SRCBOX_WORKSHOP_FILE_BASE) then
	SRCBOX_WORKSHOP_FILE_BASE = WorkshopFileBase

	function WorkshopFileBase(namespace, requiredTags)
		local object = SRCBOX_WORKSHOP_FILE_BASE(namespace, requiredTags)

		if (namespace == "dupe") then
			local fetch = object.Fetch

			function object:Fetch(kind, offset, perPage, ...)
				if (kind == "local") then
					return fetchLocalDupes(self, offset, perPage)
				end

				return fetch(self, kind, offset, perPage, ...)
			end
		end

		return object
	end
end
