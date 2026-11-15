


	-- the only real way this can happen is 'kill' in the console
	function eventPlayerKilled ( killed, attacker, weapon )
		-- If the player owns a melon then explode the melon.
		BreakPlayersMelon( userid );		
	end
	
	function eventPlayerDisconnect ( name, userid, address, steamid, reason )
		-- This will be 0 if the player hasn't become active yet
		if (userid == 0) then return end;			
		-- If the player owns a melon then explode the melon.
		BreakPlayersMelon( userid );			
	end

	function SetSpectatorMode ( userid, iMelon )
		_PlayerSpectatorStart( userid, OBS_MODE_CHASE );
		_PlayerSpectatorTarget( userid, iMelon );
		UpdatePlayerLabels();
		
	end

	function eventPlayerSpawn ( userid )
		-- Don't do anything if they're spectating
		if ( _PlayerInfo( userid, "team" ) == TEAM_SPECTATOR ) then return; end
		
		-- Spawn a melon
		local vPos = _EntGetPos( userid );				vPos = vecAdd( vPos, vector3( 0, 0, 8 ) );
		local iMelon = _EntCreate( "physics_prop" );
			_EntSetModel( iMelon, PLAYER_MODEL );
			_EntSetPos( iMelon, vPos );		
		_EntSpawn( iMelon );
		PlayerInfo[ userid ].Melon = iMelon;
		PlayerInfo[ userid ].Checkpoint = 0;
		PlayerInfo[ userid ].LapTime = 0;
		PlayerInfo[ userid ].LapStart = _CurTime();	
		-- We need to time this because PlayerSpawn is called while they're still spawning
		AddTimer( 0.1, 1, SetSpectatorMode, userid, iMelon );		_PlayerSetChaseCamDistance( userid, PlayerInfo[ userid ].CamDistance );
		DrawStats( userid );
		DrawPersonalStats( userid );
		if (bFirstRoundStarted == false) then
		
			AddTimer( 2, 1, StartRound );
			bFirstRoundStarted = true;		
			
		end
		-- If they're spawning during the intermission - freeze them.
		if (bIntermission == true) then
			
			_phys.EnableMotion( iMelon, false );
			
		end
		
	end
		function eventPropBreak ( breakerid, propid )
		local iPlayer = MelonToPlayer( propid );		if (iPlayer == 0) then return; end;
		_PlayerAddDeath( iPlayer, 1 );
		AddTimer( 3, 1, _EntSpawn, iPlayer );
		PlayerInfo[ iPlayer ].Melon = 0;
	end
	

	
	-- Coverts a melon to a player
	function MelonToPlayer( Melon )
		for i=1, _MaxPlayers() do
			if ( PlayerInfo[ i ].Melon ~= 0 and 
				 PlayerInfo[ i ].Melon == Melon ) then 									return i; 									end
		end
		return 0;
		
	end
	-- This is built for an unlimited amount of checkpoints - to make mapping a bit easier.
	-- A player must not be allowed to SKIP PAST checkpoints.
	function HitCheckpoint( Activator, Caller, NewCP )
		local iPlayer = MelonToPlayer( Activator );
		if ( IsPlayer(iPlayer) == false ) then return end;
		local LastCP = PlayerInfo[ iPlayer ].Checkpoint;
		--Msg("Player " .. iPlayer .. " Hit checkpoint ".. iCheckpoint .."\n" );
		-- They're going backwards!
		if ( LastCP == NewCP+1 ) then
			_GModText_Start( "Default" );
			 _GModText_SetColor( 255, 0, 0, 255 );
			 _GModText_SetTime( 2, 0, 1 );
			 _GModText_SetPos( -1, 0.3 )
			 _GModText_SetText( "YOU'RE GOING THE WRONG WAY!" );
			_GModText_Send( iPlayer, 102 );
			PlayerInfo[ iPlayer ].Checkpoint = NewCP;			return; 			end
		-- Going forwards - but no lap.
		if ( LastCP == NewCP-1 ) then
			_GModText_Start( "Default" );
			 _GModText_SetColor( 255, 255, 255, 255 );
			 _GModText_SetTime( 1, 0.2, 0.5 );
			 _GModText_SetPos( -1, 0.8 )
			 _GModText_SetText( "Checkpoint " .. NewCP );
			_GModText_Send( iPlayer, 100 );	
			PlayerInfo[ iPlayer ].Checkpoint = NewCP;			UpdateTopThree();
			
		end
		-- Lap!
		if ( NewCP == 0 and LastCP > 1) then
			PlayerInfo[ iPlayer ].Checkpoint = NewCP;			_PlayerAddScore( iPlayer, 1 );	
			DrawLapZoom( iPlayer );
			PlayerDoneLap( iPlayer );
			UpdateTopThree();
			CheckRoundFinished( iPlayer );
			return;
			
		end
	end

	function eventKeyPressed( userid, in_key )
		if ( PlayerInfo[userid].FinishedRace ) then return false; end;		if ( bIntermission ) then return false; end;
		if ( in_key == IN_ATTACK2 ) then	
			PlayerInfo[userid].CamDistance = PlayerInfo[userid].CamDistance + 40;
			if ( PlayerInfo[userid].CamDistance > 140 ) then 
				PlayerInfo[userid].CamDistance = 10;
			end
			_PlayerSetChaseCamDistance( userid, PlayerInfo[userid].CamDistance );					end
		if ( in_key == IN_ATTACK and 
			_PlayerInfo( userid, "team" ) == TEAM_SPECTATOR) then							_PlayerChangeTeam( userid, TEAM_BLUE );
				_EntSpawn( userid );
				
		end
	end

	function onShowHelp ( userid )
		_GModText_Start( "Default" );
		 _GModText_SetPos( -1, 0.3 );
		 _GModText_SetColor( 255, 255, 255, 255 );
		 _GModText_SetTime( 4, 0.2, 1 );
		 _GModText_SetText( "A simple racing game.\n\npressing Forward makes you go forward\npressing back makes you go back\nPressing the right mouse button changes the camera angle" );
		_GModText_Send( userid, 50 );
	end
