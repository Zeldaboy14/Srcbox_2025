

FORWARD_SPEED 	= 170;
REVERSE_SPEED 	= -40;
PLAYER_MODEL 	= "models/props_junk/watermelon01.mdl";
NO_NAME			= "n/a";
NUM_LAPS		= 10;


-- Hay guyz I maed a new gamemode its my first gamemode it tuk ages
-- Ps all I did was uncomment the bowlingball line from melonracer

-- PLAYER_MODEL 	= "models/mixerman3d/bowling/bowling_ball.mdl";

_OpenScript( "gamemodes/melonracer/gamerules.lua" );
_OpenScript( "gamemodes/melonracer/events.lua" );
_OpenScript( "gamemodes/melonracer/gui.lua" );
_OpenScript( "gamemodes/melonracer/controls.lua" );
PlayerInfo = {}
for i=1, _MaxPlayers() do
	PlayerInfo[i] = {};
	PlayerInfo[i].Melon = 0;
	PlayerInfo[i].Checkpoint = 0;
	PlayerInfo[i].CamDistance = 96;
	PlayerInfo[i].Laps = 0;
	PlayerInfo[i].LapTime = 0;
	PlayerInfo[i].LapStart = 0;
	PlayerInfo[i].BestLap = 0;
	PlayerInfo[i].FinishedRace = false;	
end

Stats = {}

Stats.BestLap		= 0;
Stats.BestLapPrint	= "0:00:000";
Stats.BestLapName 	= NO_NAME;
Stats.FirstPlace = 0
Stats.SecondPlace = 0
Stats.ThirdPlace = 0
_EntPrecacheModel( PLAYER_MODEL );	

bRestartingRound	=	false;
bFirstRoundStarted	=	false; -- We start a round when the first player spawnsbIntermission		=	false;
