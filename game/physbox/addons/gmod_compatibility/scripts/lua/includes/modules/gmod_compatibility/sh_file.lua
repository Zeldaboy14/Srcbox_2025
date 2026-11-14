local Files = require("Files")

file = {}

-- GMod's "LUA"/"lsv"/"lcl" path IDs mean "the lua/ folder of everything mounted"
-- (the game, addons, mounted .gma files). Srcbox's own "LUA" path holds the
-- gamemode entity folders and <mod>/lua; mounted content's lua/ folders are
-- reached as "lua/<name>" on GAME. Look in both, Srcbox's own first.
local LUA_PATH_IDS = { LUA = true, lsv = true, lcl = true }

local function isLuaPath(name, gamePath)
	return gamePath and LUA_PATH_IDS[gamePath] and type(name) == "string"
end

local function cleanName(name)
	return (name:gsub("^[/\\]+", ""))
end

-- For single-file operations: where the file actually is
local function translatePath(name, gamePath)
	if (not isLuaPath(name, gamePath)) then
		return name, gamePath
	end

	name = cleanName(name)

	if (Files.FileExists(name, "LUA") or Files.IsDirectory(name, "LUA")) then
		return name, "LUA"
	end

	return "lua/" .. name, "GAME"
end

-- Appends content to a file relative to the data folder
function file.Append(fileName, content)
    local file = Files.Open(fileName, "a", "DATA")

	Files.Write(content, file)
	Files.Close(file)
end

-- Reads the content of a file asynchronously
function file.AsyncRead(fileName, gamePath, callback, sync)
	-- (file.Read below translates the path)
	callback(file.Read(fileName, gamePath)) -- TODO: Implement async file reading
end

-- Creates a directory relative to the data folder
function file.CreateDir(name)
    Files.CreateDirectoryHierarchy(name, "DATA")
end

-- Deletes a file or empty folder relative to the data folder
function file.Delete(name, gamePath)
    Files.RemoveFile(name, gamePath or "DATA")
end

-- Checks if a file or directory exists
function file.Exists(name, gamePath)
    if (isLuaPath(name, gamePath)) then
        name = cleanName(name)
        return Files.FileExists(name, "LUA") or Files.FileExists("lua/" .. name, "GAME")
    end

    return Files.FileExists(name, gamePath or "DATA")
end

-- Returns a list of files and directories inside a folder
function file.Find(name, path, sorting)
    local files, directories

    if (isLuaPath(name, path)) then
        name = cleanName(name)
        local luaFiles, luaDirectories = Files.Find(name, "LUA")
        local gameFiles, gameDirectories = Files.Find("lua/" .. name, "GAME")

        files, directories = {}, {}
        for _, list in ipairs({ luaFiles, gameFiles }) do
            if (type(list) == "table") then
                for _, entry in ipairs(list) do files[#files + 1] = entry end
            end
        end
        for _, list in ipairs({ luaDirectories, gameDirectories }) do
            if (type(list) == "table") then
                for _, entry in ipairs(list) do directories[#directories + 1] = entry end
            end
        end
    else
        files, directories = Files.Find(name, path)
    end

    -- GMod returns each name once, sorted ("nameasc" by default). Here results come
    -- in search-path order and may repeat across paths; addons rely on GMod's order.
    local function tidy(list, descending)
        if (type(list) ~= "table") then
            return {}
        end

        local seen, result = {}, {}
        for _, entry in ipairs(list) do
            local key = entry:lower()
            if (not seen[key]) then
                seen[key] = true
                result[#result + 1] = entry
            end
        end

        table.sort(result, function(a, b)
            if (descending) then
                return a:lower() > b:lower()
            end
            return a:lower() < b:lower()
        end)

        return result
    end

    local descending = (sorting == "namedesc")
    return tidy(files, descending), tidy(directories, descending)
end

-- Checks if the given path is a directory
function file.IsDir(fileName, gamePath)
    if (isLuaPath(fileName, gamePath)) then
        fileName = cleanName(fileName)
        return Files.IsDirectory(fileName, "LUA") or Files.IsDirectory("lua/" .. fileName, "GAME")
    end

    return Files.IsDirectory(fileName, gamePath or "DATA")
end

-- GMod's File object. Srcbox's Files.Open hands back a raw handle, but GMod code
-- calls methods on it (f:ReadLine(), f:Read(), f:Close(), ...). Reads load the
-- whole file up front and work from memory; writes are buffered and written out
-- on Flush/Close.
local FILE = {}
FILE.__index = FILE
FILE.MetaName = "File"

local function rawReadAll(handle)
	local size = Files.Size(handle)

	if (not size or size <= 0) then
		return ""
	end

	local _, content = Files.Read(size, handle)
	return content or ""
end

function FILE:__tostring()
	return "File [" .. tostring(self._name) .. "]"
end

function FILE:Close()
	if (self._closed) then
		return
	end

	self:Flush()

	if (self._handle) then
		Files.Close(self._handle)
		self._handle = nil
	end

	self._closed = true
end

function FILE:Flush()
	if (self._handle and #self._pending > 0) then
		Files.Write(table.concat(self._pending), self._handle)
		self._pending = {}
	end
end

function FILE:Size()
	if (self._data) then
		return #self._data
	end

	return self._written
end

function FILE:Tell()
	if (self._data) then
		return self._pos
	end

	return self._written
end

function FILE:Seek(position)
	if (self._data) then
		self._pos = math.max(0, math.min(math.floor(position or 0), #self._data))
	end
end

function FILE:Skip(amount)
	if (self._data) then
		self:Seek(self._pos + (amount or 0))
	end

	return self:Tell()
end

function FILE:EndOfFile()
	if (self._data) then
		return self._pos >= #self._data
	end

	return true
end

function FILE:Read(length)
	if (not self._data or self._pos >= #self._data) then
		return nil
	end

	length = length or (#self._data - self._pos)
	local chunk = self._data:sub(self._pos + 1, self._pos + length)
	self._pos = self._pos + #chunk
	return chunk
end

-- Like GMod: returns the line including its "\n", nil once nothing is left
function FILE:ReadLine()
	if (not self._data or self._pos >= #self._data) then
		return nil
	end

	local newline = self._data:find("\n", self._pos + 1, true)
	local last = newline or #self._data
	local line = self._data:sub(self._pos + 1, last)
	self._pos = last
	return line
end

local function readPacked(self, format, size)
	if (not self._data or self._pos + size > #self._data) then
		return nil
	end

	local value = string.unpack(format, self._data, self._pos + 1)
	self._pos = self._pos + size
	return value
end

function FILE:ReadByte() return readPacked(self, "<I1", 1) end
function FILE:ReadBool() local value = readPacked(self, "<I1", 1); return value ~= nil and value ~= 0 end
function FILE:ReadShort() return readPacked(self, "<i2", 2) end
function FILE:ReadUShort() return readPacked(self, "<I2", 2) end
function FILE:ReadLong() return readPacked(self, "<i4", 4) end
function FILE:ReadULong() return readPacked(self, "<I4", 4) end
function FILE:ReadFloat() return readPacked(self, "<f", 4) end
function FILE:ReadDouble() return readPacked(self, "<d", 8) end
function FILE:ReadUInt64() local value = readPacked(self, "<I8", 8); return value and tostring(value) end

function FILE:Write(data)
	if (self._data or self._closed) then
		return
	end

	data = tostring(data)
	self._pending[#self._pending + 1] = data
	self._written = self._written + #data
end

local function writePacked(self, format, value)
	self:Write(string.pack(format, value))
end

function FILE:WriteByte(value) writePacked(self, "<I1", math.floor(value or 0) % 256) end
function FILE:WriteBool(value) writePacked(self, "<I1", value and 1 or 0) end
function FILE:WriteShort(value) writePacked(self, "<i2", math.floor(value or 0)) end
function FILE:WriteUShort(value) writePacked(self, "<I2", math.floor(value or 0)) end
function FILE:WriteLong(value) writePacked(self, "<i4", math.floor(value or 0)) end
function FILE:WriteULong(value) writePacked(self, "<I4", math.floor(value or 0)) end
function FILE:WriteFloat(value) writePacked(self, "<f", value or 0) end
function FILE:WriteDouble(value) writePacked(self, "<d", value or 0) end
function FILE:WriteUInt64(value) writePacked(self, "<I8", math.floor(tonumber(value) or 0)) end

-- Opens a file with the given mode ("r", "rb", "w", "wb", "a", "ab")
function file.Open(fileName, fileMode, gamePath)
    fileName, gamePath = translatePath(fileName, gamePath)
    gamePath = gamePath or "DATA"
    fileMode = fileMode or "r"

    local reading = fileMode:sub(1, 1) == "r"
    local handle = Files.Open(fileName, fileMode, gamePath)

    if (handle == nil or handle == FILESYSTEM_INVALID_HANDLE) then
        return nil
    end

    local object = setmetatable({
        _name = fileName,
        _handle = handle,
        _pending = {},
        _written = 0,
        _pos = 0,
    }, FILE)

    if (reading) then
        object._data = rawReadAll(handle)
        Files.Close(handle)
        object._handle = nil
    end

    return object
end

-- Reads the content of a file
function file.Read(fileName, gamePath)
    fileName, gamePath = translatePath(fileName, gamePath)
    local file = Files.Open(fileName, "r", gamePath)

    if (file == FILESYSTEM_INVALID_HANDLE) then
        return nil
    end

	local size = Files.Size(file)
	local size, content = Files.Read(size, file)
	Files.Close(file)
	return content
end

-- Renames a file
function file.Rename(originalFileName, targetFileName)
    return Files.RenameFile(originalFileName, targetFileName, "DATA")
end

-- Returns the size of a file in bytes
function file.Size(fileName, gamePath)
    fileName, gamePath = translatePath(fileName, gamePath)
    return Files.Size(fileName, gamePath or "DATA")
end

-- Returns the last modified time of a file or folder in Unix time
function file.Time(path, gamePath)
    path, gamePath = translatePath(path, gamePath)
    return Files.Time(path, gamePath or "DATA")
end

-- Writes content to a file, erasing previous data
function file.Write(fileName, content)
    local file = Files.Open(fileName, "w", "DATA")

	Files.Write(content, file)
	Files.Close(file)
end

-- GMod code gets the File metatable with FindMetaTable("File") (e.g. to read structs)
do
	local registry = debug.getregistry()
	registry.File = registry.File or FILE
end
