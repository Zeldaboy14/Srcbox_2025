
	

-- Helpful debug recursive table printer from
-- http://www.gammon.com.au/forum/bbshowpost.php?bbsubject_id=4903

	
	function tprint (t, indent, done)

	  done = done or {}
	  indent = indent or 0

	  for key, value in pairs (t) do

	    _Msg ( string.rep ("-", indent) ) -- indent it
	    if type (value) == "table" and not done [value] then

	      done [value] = true
	      _Msg (tostring (key) .. ":" .. "\n");
	      tprint (value, indent + 2, done)

	    else

	      _Msg (tostring (key) .. "=")
	      _Msg (tostring(value) .. "\n")

	    end

	  end

	end

	

-- Because I'm always typing Msg instead of _Msg..

	function Msg( text )

		_Msg( tostring(text) );

	end

		

-- A simple helper function to weld two entities together

	function WeldEntities( enta, entb )

		local iConstraint = _EntCreate( "phys_constraint" );

		_EntSetPos( iConstraint, _EntGetPos( enta ) );
		_EntSetKeyValue( iConstraint, "attachpoint", _EntGetPos( entb ) );
		_PhysConstraintSetEnts( iConstraint, enta, entb );	

		_EntSpawn( iConstraint );
		_EntActivate( iConstraint );

		return iConstraint;

	end

	

	-- Given a time in seconds, returns formatted time
	--
	-- 90 becomes 1:30
	--
	function ToMinutesSeconds( TimeInSeconds )

		local iMinutes = TimeInSeconds / 60.0;
		iMinutes = math.floor( iMinutes );

		local iSeconds = math.mod( TimeInSeconds, 60 );
		iSeconds = math.floor( iSeconds );
		if (iSeconds < 10) then iSeconds = "0" .. iSeconds; end; -- (the printf method of 0 padding didn't seem to work)

		return iMinutes .. ":" .. iSeconds;		

	end


	-- Given a time in seconds, returns formatted time
	--
	-- 90.56 becomes 1:30:56
	--
	function ToMinutesSecondsMilliseconds( TimeInSeconds )

		local iMinutes = TimeInSeconds / 60.0;
		iMinutes = math.floor( iMinutes );

		local iSeconds = math.mod( TimeInSeconds, 60 );
		iSeconds = math.floor( iSeconds );
		if (iSeconds < 10) then iSeconds = "0" .. iSeconds; end; -- (the printf method of 0 padding didn't seem to work)

		local iMilliseconds = math.mod( TimeInSeconds, 60 );

		iMilliseconds = iMilliseconds - iSeconds;
		iMilliseconds = iMilliseconds * 1000;
		iMilliseconds = math.floor( iMilliseconds );

		if ( iMilliseconds < 10 ) then

			iMilliseconds = "00" .. iMilliseconds;

		elseif ( iMilliseconds < 100 ) then

			iMilliseconds = "0" .. iMilliseconds;

		end

		return iMinutes .. ":" .. iSeconds .. ":" .. iMilliseconds;	

	end


	-- Trace along the player's aim angle.
	function PlayerLookTrace( iPlayer, MaxLength )
		
		local vPlayerAng = _PlayerGetShootAng( iPlayer );
		local vPlayerPos = _PlayerGetShootPos( iPlayer );
		_TraceLine( vPlayerPos, vPlayerAng, MaxLength, iPlayer );
		
	end  

