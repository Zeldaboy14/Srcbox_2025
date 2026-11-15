

-- 	A simple script to emulate the registering of console commands



gConsoleCommands = {};



	-- returns true if the console command is handled, false if it isn't
	function onUnknownConsoleCommand ( playerid, incommand, args )		

		if ( gConsoleCommands[ incommand ] ~= nil ) then

			gConsoleCommands[ incommand ]( playerid, args );
			return true;

		end

		return false;

	end



	-- Register the console command
	function CONCOMMAND( command, funct, help )

		gConsoleCommands[ command ] = funct;

	end

	

	-- EXAMPLE OF ADDING A CONSOLE COMMAND
	-- All console commands should take the same parameters, fromplayer and args.
	-- fromplayer is the id of the player that sent the command
	-- args is the bit after the command (and is a string)

	function cc_luacommands ( playerID, args )

		table.foreach( gConsoleCommands, function (k,v) _PrintMessage( playerID, HUD_PRINTCONSOLE, k .."\n") end );

	end

	-- After making the function like above you need to register the console command
	-- so that it can receive messages from the engine. You do this using the function below.
	-- The first parameter is the name of the console command, the second is the name of the function to
	-- call when the engine gets that command.
	CONCOMMAND( "luac", cc_luacommands )

	

