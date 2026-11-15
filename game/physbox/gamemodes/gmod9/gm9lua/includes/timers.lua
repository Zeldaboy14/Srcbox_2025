-- TIMER FUNCTIONS v1.1
--    by n42
--
--
-- CHANGES:
-- 1.2: (garry)
--	 - removed debug stuff
--   - replaced old global variable with _CurTime()
--	 - added AddThinkFunction which means you don't need to call DoTimers manually
-- 1.1:
--   - There is no limit to the amount of timers anymore
--   - Added 'repetitions' argument
-- 1.0:
--   - Initial release
--
-- FUNCTIONS:
-- DoTimers()
-- AddTimer( delay, repetitions, function, arguments)
-- HaltTimer( timer )
-- 
-- EXAMPLE:
-- 
-- This script adds a function that freezes a player for a certain amount of time.
-- 
-- function _PlayerFreezeTimed ( userid, length )
-- 	_PlayerFreeze( userid, true )
-- 	AddTimer( length, 1, _PlayerFreeze, userid, false)
-- end
-- 

Timer = {}

	function DoTimers ()
		for key,value in Timer do
			if ( Timer[key] ) then
				if ( ( Timer[key].time + Timer[key].delay ) < _CurTime() ) then
					Timer[key].func(unpack(Timer[key].arg))
					if ( Timer[key].reps > 1 ) then
						Timer[key].reps = Timer[key].reps - 1
					elseif ( Timer[key].reps == 1 ) then
						Timer[key] = nil
					end
					if ( Timer[key] ) then Timer[key].time = _CurTime(); end
				end
			end
		end
	end
	
	
	function AddTimer (delay, reps, func, ...)
		local i = 0
		while ( Timer[i] ) do
			i = i + 1
		end
		Timer[i] = {}
		Timer[i].delay 	= delay
		Timer[i].time 	= _CurTime()
		Timer[i].func 	= func
		Timer[i].arg 	= arg
		Timer[i].reps 	= reps
		return i
	end
	
	
	function HaltTimer (timer)
		if ( Timer[timer] ) then Timer[timer] = nil; end
	end


AddThinkFunction( DoTimers );
