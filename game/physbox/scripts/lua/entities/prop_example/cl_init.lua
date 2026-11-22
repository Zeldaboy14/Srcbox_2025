--[[
	Original code by Team Sandbox:
		Copyleft © 2010 - 2013, Team Sandbox, Some rights reserved.

	Modified for Experiment.
--]]

Include("shared.lua")

function ENT:Draw(flags)
	-- Draw does the drawing ( as in GMod ), so draw the model
	self:DrawModel(flags)
end

function ENT:ClientThink()
end
