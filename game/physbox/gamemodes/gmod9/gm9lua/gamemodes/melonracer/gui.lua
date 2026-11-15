



	-- Rect index

	-- 0  - Melon for intro

	-- 1  - Melon 2

	-- 2  - Title

	-- 3  - Lap!





	-- Text index

	-- 0 = 1st

	-- 1 = 2nd + 3rd

	-- 2 = Fastest lap + your fastest

	-- 3 = Data for 2

	-- 4 = Personal data labels

	-- 5 = Personal data stats

	-- 6 = intro countdown

	-- 50+ = PlayerLabels



	function UpdatePlayerLabels()

		

		for i=1, _MaxPlayers() do

			

			if ( _PlayerInfo( i, "connected" ) and PlayerInfo[i].Melon ~= 0 ) then

			

			

				_GModText_Start( "BrandingSmall" );

				 _GModText_SetColor( 255, 255, 255, 255 );

				 _GModText_SetTime( 999, 0, 5 );

				 _GModText_SetEntityOffset( vector3( 0, 0, 16 ) );

				 _GModText_SetEntity( PlayerInfo[i].Melon );

				 _GModText_SetText( _PlayerInfo( i, "name" ) );

				_GModText_Send( 0, 50+i );

			

			else

			

				_GModText_Hide( 0, 50+i, 0, 0 );

			

			end

	

		end

		

	end



	

	function DrawIntro( PlayerID )

		

		_GModRect_Start( "gmod/melonracer/melon" );

		 _GModRect_SetPos( -0.5, 0.25, 0.3, 0.3 );

		 _GModRect_SetColor( 255, 255, 255, 255 );

		 _GModRect_SetTime( 5, 0, 0 );

		_GModRect_Send( PlayerID, 0 );

		

		_GModRect_Start( "" );

		 _GModRect_SetPos( 1.2, 0.25, 0.4, 0.3 );

		 _GModRect_SetColor( 255, 255, 255, 255 );

		_GModRect_SendAnimate( PlayerID, 0, 2, 0.3 );

		

		

		_GModRect_Start( "gmod/melonracer/melon" );

		 _GModRect_SetPos( -0.5, 0.25, 0.4, 0.4 );

		 _GModRect_SetColor( 255, 255, 255, 255 );

		 _GModRect_SetTime( 5, 0.0, 0 );

		 _GModRect_SetDelay( 1 );

		_GModRect_Send( PlayerID, 1 );

		

		_GModRect_Start( "" );

		 _GModRect_SetPos( 1.0, 0.25, 0.4, 0.5 );

		 _GModRect_SetColor( 255, 255, 255, 255 );

		 _GModRect_SetDelay( 1 );

		_GModRect_SendAnimate( PlayerID, 1, 1.0, 0.4 );

		

		

		_GModRect_Start( "gmod/melonracer/title" );

		 _GModRect_SetPos( -0.4, 0.0, 0.4, 0.25 );

		 _GModRect_SetColor( 0, 0, 0, 255 );

		 _GModRect_SetTime( 5, 0.0, 1 );

		_GModRect_Send( PlayerID, 2 );

		

		_GModRect_Start( "" );

		 _GModRect_SetPos( 0.03, 0.0, 0.4, 0.25 );

		 _GModRect_SetColor( 255, 255, 255, 255 );

		_GModRect_SendAnimate( PlayerID, 2, 0.5, 0.7 );

		

	end

	

	

		

	function DrawLapZoom( iPlayer )

	

		_GModRect_Start( "gmod/melonracer/lap" );

		 _GModRect_SetPos( 0.5, 0.5, 0, 0 );

		 _GModRect_SetColor( 255, 255, 255, 255 );

		 _GModRect_SetTime( 0, 0.0, 1 );

		_GModRect_Send( iPlayer, 3 );

		

		_GModRect_Start( "" );

		 _GModRect_SetPos( -3, -2, 7, 5 );

		 _GModRect_SetColor( 255, 255, 255, 255 );

		_GModRect_SendAnimate( iPlayer, 3, 1, 0.2 );	

		

	end



	function DrawStats( iPlayer )	

		

		_GModText_Start( "DefaultShadow" );

		 _GModText_SetColor( 255, 255, 0, 255 );

		 _GModText_SetTime( 9999, 0, 0 );

		 _GModText_SetPos( 0.02, 0.1 )

		 _GModText_SetText( "1. " .. GetPlayerName(Stats.FirstPlace) );

		_GModText_Send( iPlayer, 0 );

	

		_GModText_Start( "DefaultShadow" );

		 _GModText_SetColor( 200, 200, 200, 255 );

		 _GModText_SetTime( 9999, 0, 0 );

		 _GModText_SetPos( 0.02, 0.12 )

		 _GModText_SetText( "2. " .. GetPlayerName(Stats.SecondPlace) .. "\n3. " .. GetPlayerName(Stats.ThirdPlace) );

		_GModText_Send( iPlayer, 1 );

		

		_GModText_Start( "DefaultShadow" );

		 _GModText_SetColor( 120, 120, 120, 255 );

		 _GModText_SetTime( 9999, 0, 0 );

		 _GModText_SetPos( 0.02, 0.18 )

		 _GModText_SetText( "Best Lap: \nBest Lap By:" );

		_GModText_Send( iPlayer, 2 );

		

		_GModText_Start( "DefaultShadow" );

		 _GModText_SetColor( 200, 200, 200, 255 );

		 _GModText_SetTime( 9999, 0, 0 );

		 _GModText_SetPos( 0.14, 0.18 )

		 _GModText_SetText( Stats.BestLapPrint .. "\n" .. Stats.BestLapName );

		_GModText_Send( iPlayer, 3 );

				

	end

	

	

	function DrawPersonalStats( iPlayer )

		

		_GModText_Start( "DefaultShadow" );

		 _GModText_SetColor( 120, 120, 120, 255 );

		 _GModText_SetTime( 9999, 0, 0 );

		 _GModText_SetPos( 0.02, 0.24 )

		 _GModText_SetText( "Your Best Lap: \nYour last Lap: \n\nLaps: " );

		_GModText_Send( iPlayer, 4 );

		

		_GModText_Start( "DefaultShadow" );

		 _GModText_SetColor( 200, 200, 200, 255 );

		 _GModText_SetTime( 9999, 0, 0 );

		 _GModText_SetPos( 0.14, 0.24 )

		 _GModText_SetText( ToMinutesSecondsMilliseconds(PlayerInfo[iPlayer].BestLap) .. "\n" .. ToMinutesSecondsMilliseconds(PlayerInfo[iPlayer].LapTime) .. "\n\n" .. PlayerInfo[iPlayer].Laps );

		_GModText_Send( iPlayer, 5 );

		

	end



	function DrawRoundStart( iNumber )

		

		_GModText_Start( "ImpactMassive" );

		 _GModText_SetPos( -1, 0.5 );

		 _GModText_SetColor( 0, 0, 0, 255 );

		 _GModText_SetTime( 0.5, 0, 0.5 );

		 _GModText_SetText( iNumber );

		_GModText_Send( 0, 6 );

		

		_GModText_Start( "ImpactMassive" );

		 _GModText_SetPos( -1, 0.4 );

		 _GModText_SetColor( 255, 255, 255, 255 );

		_GModText_SendAnimate( 0, 6, 1.5, 0.7 );

		

	end

	

	function DeclareWinner( WinnerName )

		

		_GModText_Start( "ImpactMassive" );

		 _GModText_SetPos( -1, -1 );

		 _GModText_SetColor( 20, 255, 20, 255 );

		 _GModText_SetTime( 5, 0.5, 2 );

		 _GModText_SetText( WinnerName .. " wins the game!" );

		_GModText_Send( 0, 6 );

		

	end

		

