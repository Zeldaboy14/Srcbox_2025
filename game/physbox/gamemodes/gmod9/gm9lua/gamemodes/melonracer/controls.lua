	
	function DoForward( iPlayer )
		if ( _PlayerIsKeyDown( iPlayer, IN_FORWARD ) == false ) then return; end;
		local vAim = _PlayerGetShootAng( iPlayer );
		if ( vAim == nil) then	return; end
		vAim.x = vAim.x * FORWARD_SPEED;
		vAim.y = vAim.y * FORWARD_SPEED;
		vAim.z = 0;
		
		_phys.ApplyForceCenter( PlayerInfo[iPlayer].Melon, vAim );	
	end
	
	function DoReverse( iPlayer )
		if ( _PlayerIsKeyDown( iPlayer, IN_BACK ) == false ) then return; end;
		local vAim = _PlayerGetShootAng( iPlayer );
		if ( vAim == nil) then	return; end
		vAim.x = vAim.x * REVERSE_SPEED;
		vAim.y = vAim.y * REVERSE_SPEED;
		vAim.z = 0;
		_PhysApplyForce( PlayerInfo[iPlayer].Melon, vAim );
	end

	function DoControls()
		for i=1, _MaxPlayers() do
			if ( _PlayerInfo( i, "connected" ) and 
				 PlayerInfo[i].Melon ~= 0 and
				 _PlayerInfo( i, "team" ) ~= TEAM_SPECTATOR and
				 PlayerInfo[i].FinishedRace == false  ) then
				DoForward( i );
				DoReverse( i );
			end
			
		end
		
	end
