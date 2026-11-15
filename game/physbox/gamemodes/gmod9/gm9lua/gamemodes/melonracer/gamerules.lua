
	function gamerulesThink ()
		DoControls();	
	end 
	function GiveDefaultItems( playerid ) end; -- (nothing)
	function gamerulesStartMap ()
		PlayerFreezeAll( false );		
	end

	-- Their model is moot because the player won't actually be drawn.
	function PlayerSpawnChooseModel ( playerid )	
		_PlayerSetModel( playerid, PLAYER_MODEL );
	end
	-- Start on the spectator team (joins the game when fire is pressed)
	function PickDefaultSpawnTeam( userid )
		_PlayerChangeTeam( userid, TEAM_SPECTATOR );
		return true; 
	end
	function eventPlayerActive ( name, userid, steamid )
		DrawIntro( userid );
	end
	function PlayerDoneLap( iPlayer )
		PlayerInfo[ iPlayer ].LapTime = _CurTime() - PlayerInfo[ iPlayer ].LapStart;
		-- Personal best changed
		if ( PlayerInfo[ iPlayer ].LapTime < PlayerInfo[ iPlayer ].BestLap or PlayerInfo[ iPlayer ].BestLap == 0 ) then
			PlayerInfo[iPlayer].BestLap = PlayerInfo[ iPlayer ].LapTime;
			-- Check to see if this is a new server best
			if ( PlayerInfo[iPlayer].BestLap < Stats.BestLap or Stats.BestLap == 0 ) then
				Stats.BestLap  = PlayerInfo[iPlayer].BestLap;
				Stats.BestLapPrint = ToMinutesSecondsMilliseconds( PlayerInfo[iPlayer].BestLap );
				Stats.BestLapName = _PlayerInfo( iPlayer, "name" );				DrawStats(0);		
			end
		end
		
		-- Reset lap time counter
		PlayerInfo[ iPlayer ].LapStart = _CurTime();
		PlayerInfo[ iPlayer ].Laps = PlayerInfo[ iPlayer ].Laps + 1;
		DrawPersonalStats( iPlayer );		DrawStats(0);
	end
	function GetLeader( First, Second )
		local iTop = 0;
		local iTopLaps = 0;
		local iTopCheckpoint = 0;
		for i=1, _MaxPlayers() do
			if ( PlayerInfo[ i ].Melon ~= 0 and i ~= First and i~= Second) then				if ( PlayerInfo[ i ].Laps >= iTopLaps ) then
					iTop = i;
					iTopLaps = PlayerInfo[ i ].Laps;
					iTopCheckpoint = 0;					
					if ( PlayerInfo[ i ].Checkpoint > iTopLaps ) then
						iTopCheckpoint = PlayerInfo[ i ].Checkpoint;					end
				end
			end	
		end
		return iTop;
		
	end
	
	-- I know nothing about sorting functions
	function UpdateTopThree()
		local OldFirst = Stats.FirstPlace;
		local OldSecond = Stats.SecondPlace;
		local OldThird = Stats.ThirdPlace;
		Stats.FirstPlace = GetLeader( 0, 0 );
		Stats.SecondPlace = GetLeader( Stats.FirstPlace, 0 );
		Stats.ThirdPlace = GetLeader( Stats.FirstPlace, Stats.SecondPlace );	
		if (OldFirst ~= Stats.FirstPlace or
			OldSecond ~= Stats.SecondPlace or
			OldThird ~= Stats.ThirdPlace) then			DrawStats( 0 );		end
	end
	
	function GetPlayerName( iPlayer )
		if (iPlayer == 0) then return NO_NAME; end
		if (_PlayerInfo( iPlayer, "connected") == false) then return NO_NAME; end
		
		return _PlayerInfo( iPlayer, "name");		
		
	end

	function CheckRoundFinished( iPlayer )
		if ( PlayerInfo[ iPlayer ].Laps <= NUM_LAPS ) then return; end;
		PlayerInfo[ iPlayer ].FinishedRace = true; 
		if (bRestartingRound) then return; end;
		AddTimer( 10, 1, StartRound );
		bRestartingRound = true;
		DeclareWinner( _PlayerInfo( iPlayer, "name" ) );	
		
	end

	-- Start a whole new round
	function StartRound()
		for i=1, _MaxPlayers() do
			if ( IsPlayerOnline( i ) and
				 _PlayerInfo( i, "team" ) ~= TEAM_SPECTATOR ) then
				 
				-- smash the player's melon if he has one
				BreakPlayersMelon( i );
				-- reset their statistics
				PlayerResetStats( i );
				-- respawn the bastard
				_EntSpawn( i );
				-- If the player doesn't have a melon then we're fucked anyway so lets not do any error checking
				_phys.EnableMotion( PlayerInfo[i].Melon, false );
			end
		end	
		bRestartingRound	=	false;
		bIntermission		=	true;
		
		AddTimer( 1, 4, _PlaySound, "hl1/fvox/bell.wav" );
		AddTimer( 1, 1, DrawRoundStart, "3" );
		AddTimer( 2, 1, DrawRoundStart, "2" );
		AddTimer( 3, 1, DrawRoundStart, "1" );
		AddTimer( 4, 1, DrawRoundStart, "GO!!" );		AddTimer( 4, 1, RaceStart );
	end
	
	
	function RaceStart()
		for i=1, _MaxPlayers() do
			if ( IsPlayerOnline( i ) and 
				  PlayerInfo[i].Melon ~= 0) then
				  
				-- unfreeze the melon
				_PhysEnableMotion( PlayerInfo[i].Melon, true );				
			end
		end				bIntermission		=	false;
		bRestartingRound	=	false;
	end
	
	function PlayerResetStats( i )
		PlayerInfo[i].Melon = 0;
		PlayerInfo[i].Checkpoint = 0;
		PlayerInfo[i].Laps = 0;
		PlayerInfo[i].LapTime = 0;
		PlayerInfo[i].LapStart = 0;
		PlayerInfo[i].FinishedRace = false;
	--	PlayerInfo[i].BestLap = 0;
	end
	
	function BreakPlayersMelon( i )
	
		if (PlayerInfo[i].Melon ~= 0) then
		
			_EntFire( PlayerInfo[i].Melon, "break", 0, 0 );
			PlayerInfo[i].Melon = 0;
			
		end
	end
	
