

-- 	PLAYER RELATED FUNCTIONS




	-- Run function on all players
	function AllPlayers( InFunction )

		for i=1, _MaxPlayers() do

			if ( IsPlayerOnline(i) ) then
				InFunction( i )
			end

		end			

	end


	function PlayerFreezeAll ( bFreeze )

		for i=1, _MaxPlayers() do
			_PlayerFreeze( i, bFreeze )
		end			

	end

	
	function PlayerSpawnAll ( )

		AllPlayers( _EntSpawn )

	end

	function PlayerKillAll ( )

		AllPlayers( _PlayerKill )		

	end


	function PlayerInfoLine( i )

		if ( _PlayerInfo( i, "connected" ) ) then
			
			_Msg( "\n" )
			_Msg( "PlayerID:   " .. i .. "\n" )
			_Msg( "    Name:        " .. _PlayerInfo( i, "name" ) .. "\n" )
			_Msg( "    Ping:        " .. _PlayerInfo( i, "ping" ) .. "\n" )
			_Msg( "    Health:      " .. _PlayerInfo( i, "health" ) .. "\n" )
			_Msg( "    Armor:       " .. _PlayerInfo( i, "armor" ) .. "\n" )
			_Msg( "    Team:        " .. _PlayerInfo( i, "team" ) .. "\n" )
			_Msg( "    Kills:       " .. _PlayerInfo( i, "kills" ) .. "\n" )
			_Msg( "    Deaths:      " .. _PlayerInfo( i, "deaths" ) .. "\n" )
			_Msg( "    Model:       " .. _PlayerInfo( i, "model" ) .. "\n" )
			_Msg( "    SteamID:     " .. _PlayerInfo( i, "networkid" ) .. "\n" )
			_Msg( "    EntIdx:      " .. _PlayerInfo( i, "entindex" ) .. "\n" )
			_Msg( "    Weapon:      " .. _PlayerInfo( i, "weapon" ) .. "\n" )
			
			local alive = "No"
			if ( _PlayerInfo( i, "alive" ) ) then alive = "Yes" end
			_Msg( "    Alive:       " .. alive .. "\n" )

		end

	end


	-- This is mainly for debugging etc
	function PlayerList()

		AllPlayers( PlayerInfoLine )
		_Msg( "\n" )

	end


	function IsPlayer( EntNum )

		if (EntNum <= 0) then return false; end
		if (EntNum > _MaxPlayers() ) then return false; end		

		return true

	end

	

-- Move a player by vector3

	function PlayerMove ( iPlayer, vMoveVector )

		local vNewPos = vecAdd( _EntGetPos(iPlayer), vMoveVector )
		_EntSetPos( iPlayer , vNewPos )

	end

	

-- returns true if the player is valid, connected and ready to get busy

	function IsPlayerOnline( iPlayer )

		if ( IsPlayer( iPlayer ) == false ) then return false; end;
		if ( _PlayerInfo( iPlayer, "connected" ) == false ) then return false; end;
			
		return true;			

	end

