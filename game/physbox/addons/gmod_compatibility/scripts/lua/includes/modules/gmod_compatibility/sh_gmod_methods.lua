--[[
	Srcbox: GMod object methods that srcbox_glua_audit found missing. Engine ones that need C++
	( Entity:PhysicsInitSphere, GetParentAttachment, GetRenderMode, GetRenderFX in
	game/shared/lbaseentity_shared.cpp, Player:SetViewEntity in game/server/lplayer.cpp ) are
	there; the rest are built here from what the engine has. Each is only set when missing.
--]]

local function setDefault(meta, key, value)
	if (meta and meta[key] == nil and value ~= nil) then
		meta[key] = value
	end
end

local ENTITY = FindMetaTable("Entity")
local PLAYER = FindMetaTable("Player")
local DAMAGE = FindMetaTable("CTakeDamageInfo")
local PHYSOBJ = FindMetaTable("PhysObj")

-- Per entity data that isn't networked ( weak, so removed entities are forgotten )
local entityData = setmetatable({}, { __mode = "k" })
local function dataOf(entity)
	local data = entityData[entity]
	if (not data) then
		data = {}
		entityData[entity] = data
	end
	return data
end

--[[
	Enumerations GMod's motion controllers use
--]]
SIM_NOTHING = SIM_NOTHING or 0
SIM_LOCAL_ACCELERATION = SIM_LOCAL_ACCELERATION or 1
SIM_LOCAL_FORCE = SIM_LOCAL_FORCE or 2
SIM_GLOBAL_ACCELERATION = SIM_GLOBAL_ACCELERATION or 3
SIM_GLOBAL_FORCE = SIM_GLOBAL_FORCE or 4

--[[
	Entity
--]]
local creationCounter = 0

setDefault(ENTITY, "GetCreationTime", function(self)
	local data = dataOf(self)
	data.creationTime = data.creationTime or CurTime()
	return data.creationTime
end)

setDefault(ENTITY, "GetCreationID", function(self)
	local data = dataOf(self)
	if (not data.creationID) then
		creationCounter = creationCounter + 1
		data.creationID = creationCounter
	end
	return data.creationID
end)

-- Everything there when the map finished loading counts as made by the map
hook.Add("InitPostEntity", "Srcbox_CreatedByMap", function()
	for _, entity in ipairs(ents.GetAll()) do
		local data = dataOf(entity)
		data.createdByMap = true
		data.creationTime = data.creationTime or 0
		if (not data.creationID) then
			creationCounter = creationCounter + 1
			data.creationID = creationCounter
		end
	end
end)

setDefault(ENTITY, "CreatedByMap", function(self)
	return dataOf(self).createdByMap == true
end)

setDefault(ENTITY, "GetModelPhysBoneCount", function(self)
	if (self.GetPhysicsObjectCount) then
		return self:GetPhysicsObjectCount()
	end
	return IsValid(self:GetPhysicsObject()) and 1 or 0
end)

setDefault(ENTITY, "GetSequenceCount", function(self)
	local ok, sequences = pcall(self.GetSequences, self)
	if (not ok or type(sequences) ~= "table") then
		return 0
	end
	local count = 0
	for _ in pairs(sequences) do
		count = count + 1
	end
	return count
end)

setDefault(ENTITY, "GetFlexType", function(self)
	return self:GetModel()
end)

-- SetFlexScale / GetFlexScale are engine bindings now ( game/shared/lbaseentity_shared.cpp )

setDefault(ENTITY, "GetCollisionBounds", function(self)
	return self:OBBMins(), self:OBBMaxs()
end)

setDefault(ENTITY, "GetNetworkOrigin", function(self)
	return self:GetPos()
end)

setDefault(ENTITY, "SetNetworkOrigin", function(self, position)
	self:SetPos(position)
end)

setDefault(ENTITY, "BoneLength", function(self, bone)
	local parent = self:GetBoneParent(bone)
	if (not parent or parent < 0) then
		return 0
	end
	local a = self:GetBonePosition(bone)
	local b = self:GetBonePosition(parent)
	if (not a or not b) then
		return 0
	end
	return a:Distance(b)
end)

setDefault(ENTITY, "GetWorkshopID", function() return nil end)
setDefault(ENTITY, "EnableCustomCollisions", function() end)
setDefault(ENTITY, "SetRenderInScreenshots", function() end)
setDefault(ENTITY, "AddToMotionController", function() end)
setDefault(ENTITY, "SetRagdollBuildFunction", function() end)
setDefault(ENTITY, "MotionSensorPos", function() return nil end)

if (SERVER) then
	-- Physics damage: push the physics object the way the hit would
	setDefault(ENTITY, "TakePhysicsDamage", function(self, damageInfo)
		local physicsObject = self:GetPhysicsObject()
		if (not IsValid(physicsObject)) then
			return
		end
		local force = damageInfo:GetDamageForce()
		if (force and force:LengthSqr() > 0) then
			physicsObject:ApplyForceOffset(force, damageInfo:GetDamagePosition())
		end
	end)

	setDefault(ENTITY, "AddPAS", function() end)

	--[[
		Motion controllers: GMod calls ENT:PhysicsSimulate( physObj, deltaTime ) every physics
		step and applies what it returns ( angular, linear, SIM_* mode ). Done here each tick.
	--]]
	local controlled = {}

	setDefault(ENTITY, "StartMotionController", function(self)
		controlled[self] = true
	end)

	setDefault(ENTITY, "StopMotionController", function(self)
		controlled[self] = nil
	end)

	local function toVector(value)
		if (isvector and isvector(value)) then
			return value
		end
		if (type(value) == "Vector") then
			return value
		end
		if (type(value) == "Angle") then
			return Vector(value.p or value[1], value.y or value[2], value.r or value[3])
		end
		return nil
	end

	hook.Add("Tick", "Srcbox_MotionControllers", function()
		local deltaTime = engine.TickInterval and engine.TickInterval() or FrameTime()

		for entity in pairs(controlled) do
			if (not IsValid(entity)) then
				controlled[entity] = nil
			elseif (entity.PhysicsSimulate) then
				local physicsObject = entity:GetPhysicsObject()

				if (IsValid(physicsObject) and physicsObject:IsMotionEnabled()) then
					local ok, angular, linear, mode = pcall(entity.PhysicsSimulate, entity, physicsObject, deltaTime)

					if (not ok) then
						ErrorNoHalt(tostring(angular) .. "\n")
						controlled[entity] = nil
					elseif (mode and mode ~= SIM_NOTHING) then
						linear = toVector(linear)
						angular = toVector(angular)

						local isLocal = (mode == SIM_LOCAL_ACCELERATION or mode == SIM_LOCAL_FORCE)
						local isAcceleration = (mode == SIM_LOCAL_ACCELERATION or mode == SIM_GLOBAL_ACCELERATION)

						if (linear and linear:LengthSqr() > 0) then
							if (isLocal) then
								linear = physicsObject:LocalToWorldVector(linear)
							end
							if (isAcceleration) then
								physicsObject:AddVelocity(linear * deltaTime)
							else
								physicsObject:ApplyForceCenter(linear * deltaTime)
							end
						end

						if (angular and angular:LengthSqr() > 0) then
							-- AddAngleVelocity takes local space, ApplyTorqueCenter world space
							if (isAcceleration) then
								if (not isLocal) then
									angular = physicsObject:WorldToLocalVector(angular)
								end
								physicsObject:AddAngleVelocity(angular * deltaTime)
							else
								if (isLocal) then
									angular = physicsObject:LocalToWorldVector(angular)
								end
								physicsObject:ApplyTorqueCenter(angular * deltaTime)
							end
						end

						physicsObject:Wake()
					end
				end
			end
		end
	end)
end

if (CLIENT) then
	setDefault(ENTITY, "SetLOD", function() end)
end

--[[
	Player
--]]
if (PLAYER) then
	setDefault(PLAYER, "SetEyeAngles", PLAYER.SnapEyeAngles)
	setDefault(PLAYER, "IsFullyAuthenticated", function() return true end)
	setDefault(PLAYER, "GetAllowWeaponsInVehicle", function() return false end)
	setDefault(PLAYER, "VoiceVolume", function() return 0 end)

	if (SERVER) then
		setDefault(PLAYER, "AddFrags", function(self, count)
			if (self.SetFrags and self.Frags) then
				self:SetFrags(self:Frags() + (count or 1))
			else
				self:IncrementFragCount(count or 1)
			end
		end)

		setDefault(PLAYER, "AddDeaths", function(self, count)
			if (self.SetDeaths and self.Deaths) then
				self:SetDeaths(self:Deaths() + (count or 1))
			else
				self:IncrementDeathCount(count or 1)
			end
		end)

		-- The HL2MP player makes its own ragdoll when it dies, so making another would double it
		setDefault(PLAYER, "CreateRagdoll", function() end)
	end
end

--[[
	CTakeDamageInfo
--]]
if (DAMAGE) then
	setDefault(DAMAGE, "IsDamageType", function(self, damageType)
		return bit.band(self:GetDamageType(), damageType) ~= 0
	end)
end

--[[
	Text entries
--]]
if (CLIENT) then
	local TEXT_ENTRY = FindMetaTable("TextEntry")
	local PANEL = FindMetaTable("Panel")

	local function killFocus(self)
		if (self.HasFocus and not self:HasFocus()) then
			return
		end
		local parent = self:GetParent()
		if (IsValid(parent) and parent.RequestFocus) then
			parent:RequestFocus()
		end
	end

	setDefault(TEXT_ENTRY, "KillFocus", killFocus)
	setDefault(PANEL, "KillFocus", killFocus)
	setDefault(TEXT_ENTRY, "FocusNext", function() end)
	setDefault(PANEL, "FocusNext", function() end)
end

--[[
	Round two, from the realm-aware audit
--]]

-- Entity:MuzzleFlash(): the engine calls it DoMuzzleFlash
setDefault(ENTITY, "MuzzleFlash", function(self)
	if (self.DoMuzzleFlash) then
		self:DoMuzzleFlash()
	end
end)

-- GMod's own spelling ( sic ) of HasFlexManipulator
setDefault(ENTITY, "HasFlexManipulatior", function(self)
	if (self.HasFlexManipulator) then
		return self:HasFlexManipulator()
	end
	return false
end)

if (CLIENT) then
	-- Spawn flags aren't networked, so the client can't know them
	setDefault(ENTITY, "HasSpawnFlags", function() return false end)
end

if (SERVER) then
	-- IK only exists on the client; the server has nothing to turn off
	setDefault(ENTITY, "SetIK", function() end)
end

--[[
	net.WriteMatrix / net.ReadMatrix: the 16 fields as floats, row by row
--]]
if (net) then
	setDefault(net, "WriteMatrix", function(matrix)
		for row = 1, 4 do
			for column = 1, 4 do
				net.WriteFloat(matrix:GetField(row, column))
			end
		end
	end)

	setDefault(net, "ReadMatrix", function()
		local matrix = Matrix()
		for row = 1, 4 do
			for column = 1, 4 do
				matrix:SetField(row, column, net.ReadFloat())
			end
		end
		return matrix
	end)
end

--[[
	LoadPresets / SavePresets: what GMod's presets module ( the tool preset dropdowns ) reads
	and writes. Kept as JSON in data/srcbox_presets.txt; SavePresets merges the groups it's given.
--]]
local PRESETS_FILE = "srcbox_presets.txt"

if (LoadPresets == nil) then
	function LoadPresets()
		local text = file.Read(PRESETS_FILE, "DATA")
		local presets = text and util.JSONToTable(text)
		return type(presets) == "table" and presets or {}
	end
end

if (SavePresets == nil) then
	function SavePresets(groups)
		local presets = LoadPresets()
		for name, group in pairs(groups or {}) do
			presets[name] = group
		end
		file.Write(PRESETS_FILE, util.TableToJSON(presets, true))
	end
end

--[[
	TF2's material proxies ( C++ in TF2, so this engine doesn't have them ). TF2 items and
	player models use them, and each one printed 'proxy "BurnLevel" not found!'. These hold
	each at its resting value ( not burning, not covered in Jarate, no glow ). One an addon
	adds itself ( Hat Painter's ItemTintColor, Cloak Effect Tool's ) takes its place.
--]]
if (CLIENT and matproxy and matproxy.Add) then
	local function resting(name, value)
		if (matproxy.ProxyList and matproxy.ProxyList[name]) then
			return
		end

		matproxy.Add({
			name = name,

			init = function(self, material, values)
				self.ResultTo = values and values.resultvar
			end,

			bind = function(self, material)
				if (not self.ResultTo or value == nil) then
					return
				end

				if (isvector(value)) then
					material:SetVector(self.ResultTo, value)
				else
					material:SetFloat(self.ResultTo, value)
				end
			end,
		})
	end

	resting("BurnLevel", 0)                     -- burning players' char overlay
	resting("YellowLevel", Vector(1, 1, 1))     -- Jarate tint
	resting("ModelGlowColor", Vector(1, 1, 1))
	resting("ItemTintColor", Vector(0, 0, 0))   -- unpainted: the material's own $colortint_base shows
	resting("StickybombGlowColor", Vector(1, 1, 1))
	resting("BuildingRescueLevel", 0)
	resting("AnimatedWeaponSheen")              -- no killstreak sheen
	resting("WeaponSkin")
	resting("CommunityWeapon")
	resting("InvisWarFX")
	resting("spy_invis", 0)
	resting("weapon_invis", 0)
	resting("vm_invis", 0)
	resting("building_invis", 0)
end
