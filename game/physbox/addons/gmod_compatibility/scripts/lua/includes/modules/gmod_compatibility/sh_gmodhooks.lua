--[[
	Srcbox: Garry's Mod hooks that Physbox's engine calls under its own name.

	These run the GMod hook ( hook.Run: addon hooks, then the gamemode's function ) from the
	Physbox hook that fires at the same moment, translating the arguments and the return value.
	The hooks with no Physbox counterpart are called by the engine itself ( see
	srcbox/srcbox_gmodhooks.h in the source ): EntityTakeDamage, ScalePlayerDamage, PlayerHurt,
	PlayerSay, PlayerUse, KeyPress, SetupMove, Move, FinishMove, PlayerSwitchWeapon, the render and
	HUD hooks, and so on.

	Also here: the player / entity functions GMod's own versions of those hooks call
	( Player:AddFrozenPhysicsObject, Player:PhysgunUnfreeze, Entity:GetUnFreezable, ... ).
--]]

local PLAYER_META = FindMetaTable("Player")
local ENTITY_META = FindMetaTable("Entity")

local function returnedFalse(value)
	return value == false
end

if (SERVER) then
	-- PlayerShouldTakeDamage( ply, attacker ): false = no damage
	hook.Add("FPlayerCanTakeDamage", "GModCompatibility.PlayerShouldTakeDamage", function(client, attacker)
		local allowed = hook.Run("PlayerShouldTakeDamage", client, attacker)

		if (allowed ~= nil) then
			return allowed and true or false
		end
	end)

	-- DoPlayerDeath( ply, attacker, dmginfo ) and PlayerDeath( victim, inflictor, attacker ).
	-- DoPlayerDeath runs addon hooks only: the gamemode's version makes the ragdoll and counts
	-- the frags, which Physbox's engine has already done.
	hook.Add("PlayerKilled", "GModCompatibility.PlayerDeath", function(victim, damageInfo)
		local attacker = damageInfo and damageInfo:GetAttacker() or NULL
		local inflictor = damageInfo and damageInfo:GetInflictor() or NULL

		hook.Call("DoPlayerDeath", nil, victim, attacker, damageInfo)
		hook.Run("PlayerDeath", victim, inflictor, attacker)
	end)

	hook.Add("ClientDisconnected", "GModCompatibility.PlayerDisconnected", function(client)
		if (IsValid(client)) then
			hook.Run("PlayerDisconnected", client)
		end
	end)

	-- PlayerAuthed( ply, steamid, uniqueid ); Physbox gives the name and the network ID
	hook.Add("NetworkIDValidated", "GModCompatibility.PlayerAuthed", function(name, networkID)
		for _, client in ipairs(player.GetAll()) do
			if (client:Nick() == name) then
				hook.Run("PlayerAuthed", client, networkID, client.UniqueID and client:UniqueID() or networkID)
				return
			end
		end
	end)

	-- PlayerCanPickupWeapon( ply, weapon ): only a refusal is passed on, so the engine's own
	-- rules still apply otherwise
	hook.Add("CanHavePlayerItem", "GModCompatibility.PlayerCanPickupWeapon", function(client, weapon)
		if (returnedFalse(hook.Run("PlayerCanPickupWeapon", client, weapon))) then
			return false
		end
	end)

	hook.Add("CanEnterVehicle", "GModCompatibility.CanPlayerEnterVehicle", function(client, vehicle, role)
		if (returnedFalse(hook.Run("CanPlayerEnterVehicle", client, vehicle, role))) then
			return false
		end
	end)

	-- PlayerCanHearPlayersVoice( listener, talker ) returns canHear, is3D; Physbox's hook wants
	-- proximity first, then canHear
	hook.Add("CanPlayerHearPlayer", "GModCompatibility.PlayerCanHearPlayersVoice", function(listener, talker, proximity)
		local canHear, is3D = hook.Run("PlayerCanHearPlayersVoice", listener, talker)

		if (canHear ~= nil) then
			return is3D == true, canHear and true or false
		end
	end)

	-- PlayerSelectSpawn( ply, transition ) returns the spawn point entity
	hook.Add("PlayerEntSelectSpawnPoint", "GModCompatibility.PlayerSelectSpawn", function(client)
		local spawn = hook.Run("PlayerSelectSpawn", client, false)

		if (IsValid(spawn)) then
			return spawn
		end
	end)

	--[[
		What GMod's OnPhysgunFreeze / OnPhysgunReload call
	--]]
	if (not PLAYER_META.AddFrozenPhysicsObject) then
		function PLAYER_META:AddFrozenPhysicsObject(entity, physicsObject)
			self.SrcboxFrozenPhysicsObjects = self.SrcboxFrozenPhysicsObjects or {}
			self.SrcboxFrozenPhysicsObjects[#self.SrcboxFrozenPhysicsObjects + 1] = { entity = entity, physicsObject = physicsObject }
		end
	end

	if (not PLAYER_META.PhysgunUnfreeze) then
		-- Unfreezes what this player froze with the physgun; returns how many
		function PLAYER_META:PhysgunUnfreeze()
			local count = 0

			for _, frozen in ipairs(self.SrcboxFrozenPhysicsObjects or {}) do
				if (IsValid(frozen.entity) and IsValid(frozen.physicsObject) and not frozen.physicsObject:IsMoveable()) then
					frozen.physicsObject:EnableMotion(true)
					frozen.physicsObject:Wake()
					count = count + 1
				end
			end

			self.SrcboxFrozenPhysicsObjects = {}
			return count
		end
	end

	PLAYER_META.UnfreezePhysicsObjects = PLAYER_META.UnfreezePhysicsObjects or PLAYER_META.PhysgunUnfreeze

	-- Sandbox's hints are drawn on the client; the server-side calls have nothing to send them to yet
	PLAYER_META.SendHint = PLAYER_META.SendHint or function() end
	PLAYER_META.SuppressHint = PLAYER_META.SuppressHint or function() end
end

if (CLIENT) then
	-- Physbox calls it PostRenderVgui, GMod PostRenderVGUI
	hook.Add("PostRenderVgui", "GModCompatibility.PostRenderVGUI", function()
		hook.Run("PostRenderVGUI")
	end)

	-- PlayerBindPress( ply, bind, pressed, code ): true = the bind doesn't run. Like GMod, only
	-- key presses ( not releases ) are asked about.
	hook.Add("KeyInput", "GModCompatibility.PlayerBindPress", function(isDown, keyCode, binding)
		if (isDown == 0 or not binding or binding == "") then
			return
		end

		local client = LocalPlayer()

		if (IsValid(client) and hook.Run("PlayerBindPress", client, binding, true, keyCode) == true) then
			return false
		end
	end)
end

--[[
	Shared helpers GMod's hooks use
--]]
if (not ENTITY_META.GetUnFreezable) then
	function ENTITY_META:SetUnFreezable(unfreezable)
		self.SrcboxUnFreezable = unfreezable and true or false
	end

	function ENTITY_META:GetUnFreezable()
		return self.SrcboxUnFreezable == true
	end
end

--[[
	Engine functions Sandbox calls that Physbox didn't have ( found by srcbox_glua_audit's list )
--]]

-- Player:ChatPrint( text ): a line in that player's chat
if (not PLAYER_META.ChatPrint) then
	if (SERVER) then
		function PLAYER_META:ChatPrint(text)
			Players.ClientPrint(self, HUD_PRINTTALK, tostring(text))
		end
	else
		function PLAYER_META:ChatPrint(text)
			if (chat and chat.AddText) then
				chat.AddText(tostring(text))
			else
				MsgN(tostring(text))
			end
		end
	end
end

-- Entity:BoundingRadius() is GetBoundingRadius here
if (not ENTITY_META.BoundingRadius and ENTITY_META.GetBoundingRadius) then
	ENTITY_META.BoundingRadius = ENTITY_META.GetBoundingRadius
end

-- Angle:AlignAngles( from, to ): this angle, moved the way that turns "from" into "to"
do
	local ANGLE_META = FindMetaTable("Angle")

	if (ANGLE_META and not ANGLE_META.AlignAngles and WorldToLocal and LocalToWorld) then
		function ANGLE_META:AlignAngles(from, to)
			local _, localAngles = WorldToLocal(vector_origin, self, vector_origin, from)
			local _, aligned = LocalToWorld(vector_origin, localAngles, vector_origin, to)
			return aligned
		end
	end
end
