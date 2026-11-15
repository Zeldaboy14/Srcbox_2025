--[[
	gmod_runfunction (GMod 9 map entity)

	Calls the GMod 9 Lua function named by "FunctionName" when the map fires one
	of its inputs:
		RunScript					function(activator, caller)
		RunScriptInteger/Float/String/Vector	function(activator, caller, value)
	Activator and caller are entity indices, as in GMod 9.
--]]

ENT.Base = "prop_scripted"

function ENT:KeyValue(key, value)
	if (string.lower(key) == "functionname") then
		self.FunctionName = value
	end
end

function ENT:Initialize()
	if (CLIENT) then
		return
	end

	self:AddEffects(32)	-- EF_NODRAW
end

function ENT:AcceptInput(inputName, activator, caller, data)
	if (CLIENT or not GM9 or not GM9.Call) then
		return
	end

	local name = string.lower(inputName)
	local func = self.FunctionName
	if (not func or func == "" or name:sub(1, 9) ~= "runscript") then
		return
	end

	local a, c = GM9.Idx(activator), GM9.Idx(caller)
	data = data or ""

	if (name == "runscript") then
		GM9.Call(func, a, c)
	elseif (name == "runscriptinteger") then
		local n = tonumber(data) or 0
		GM9.Call(func, a, c, math.tointeger(n) or math.floor(n))
	elseif (name == "runscriptfloat") then
		GM9.Call(func, a, c, tonumber(data) or 0)
	elseif (name == "runscriptstring") then
		GM9.Call(func, a, c, data)
	elseif (name == "runscriptvector") then
		local x, y, z = data:match("(%S+)%s+(%S+)%s+(%S+)")
		GM9.Call(func, a, c, { x = tonumber(x) or 0, y = tonumber(y) or 0, z = tonumber(z) or 0 })
	else
		return
	end

	return true
end
