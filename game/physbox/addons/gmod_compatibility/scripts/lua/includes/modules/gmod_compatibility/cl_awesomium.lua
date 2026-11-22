local PANEL = {}

-- Adds a new object in JavaScript
function PANEL:NewObject(objectName)
	self:AddJavascriptObject(objectName)
end

-- Adds a new object callback in JavaScript
function PANEL:NewObjectCallback(objectName, callbackName)
	self:AddJavascriptObjectCallback(objectName, callbackName)
end

-- Srcbox: GMod's own JS_Language / JS_Utility / JS_Workshop ( includes/util/javascript_util.lua, loaded
-- by includes/init.lua before this file ) add the Lua functions the spawnmenu's Saves and Dupes pages
-- call. Only fill in empty ones when GMod's file wasn't loaded, instead of replacing them.
if (not JS_Language) then
	function JS_Language(htmlPanel)
	end
end

if (not JS_Utility) then
	function JS_Utility(htmlPanel)
	end
end

if (not JS_Workshop) then
	function JS_Workshop(htmlPanel)
	end
end

derma.DefineControl("Awesomium", "A wrapper around the HTML to simulate awesomium", PANEL, "HTML")
