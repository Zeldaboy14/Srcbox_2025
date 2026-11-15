

-- A simple script to make it easier to create plugin scripts that can run alongside 
-- other gamemode scripts without modifying code.



gLuaThinkFunctions = {}



	function DoLuaThinkFunctions ()

		for k, v in gLuaThinkFunctions do

			if ( gLuaThinkFunctions[k] ) then

				gLuaThinkFunctions[k]();

			end

		end

	end





	function AddThinkFunction ( functionname )

		gLuaThinkFunctions[ table.getn(gLuaThinkFunctions) + 1 ] = functionname;

	end

