#include "cbase.h"
#include "func_simpleladder.h"
#include "team.h"
#include "fmtstr.h"

extern ConVar sv_showladders;

// Most L4D ladders are infected-only (team 2): in c11m1 that's 28 of 30. Survivors can't
// climb those in L4D, and they lead onto rooftops/ledges the map isn't built for.
ConVar sv_simpleladder_infected( "sv_simpleladder_infected", "0", FCVAR_GAMEDLL,
	"Let players climb L4D infected-only func_simpleladders (takes effect on map load)." );

// The player hull is 32x32x72, so the climb line has to sit at least half a
// hull out from the brush face. Otherwise the mount position is inside the
// (solid) ladder brush and the player gets stuck the moment they mount.
#define SIMPLELADDER_MOUNT_OFFSET		18.0f
#define SIMPLELADDER_DISMOUNT_DISTANCE	40.0f
#define SIMPLELADDER_FACE_PROBE			48.0f

IMPLEMENT_SERVERCLASS_ST( CFuncSimpleLadder, DT_FuncSimpleLadder )
	SendPropVector( SENDINFO( m_climbableNormal ) )
END_SEND_TABLE()

BEGIN_DATADESC( CFuncSimpleLadder )

	DEFINE_FIELD( m_climbableNormal, FIELD_VECTOR ),
	DEFINE_FIELD( m_bAdapterSpawned, FIELD_BOOLEAN ),
	DEFINE_FIELD( m_iLadderTeam, FIELD_INTEGER ),

	DEFINE_THINKFUNC( AdapterThink ),

END_DATADESC()

LINK_ENTITY_TO_CLASS( func_simpleladder, CFuncSimpleLadder );

//-----------------------------------------------------------------------------
// Purpose: Defaults only. The map keys are parsed after construction, so
//			nothing here may reset m_climbableNormal or m_iLadderTeam.
//-----------------------------------------------------------------------------
CFuncSimpleLadder::CFuncSimpleLadder()
{
	m_bAdapterSpawned = false;
	m_iLadderTeam = 0;
	m_climbableNormal.Init();
}

//-----------------------------------------------------------------------------
// Purpose: Spawn a L4D-style ladder brush.
//-----------------------------------------------------------------------------
void CFuncSimpleLadder::Spawn( void )
{
	BaseClass::Spawn();

	Precache();

	// A brush entity without a model is not a ladder, it is a bad map entity.
	if ( GetModelName() == NULL_STRING )
	{
		Warning( "func_simpleladder with no model at (%.0f %.0f %.0f), removing\n",
			GetAbsOrigin().x, GetAbsOrigin().y, GetAbsOrigin().z );
		UTIL_Remove( this );
		return;
	}

	SetModel( STRING( GetModelName() ) );

	// Keep it solid: L4D ladder brushes block the player and are climbed from
	// the face. The runtime CFuncLadder provides the actual climbing.
	SetSolid( SOLID_BSP );
	SetMoveType( MOVETYPE_PUSH );
}

//-----------------------------------------------------------------------------
// Purpose: Build the player-climbable volume once the map is fully spawned.
//-----------------------------------------------------------------------------
void CFuncSimpleLadder::Activate( void )
{
	BaseClass::Activate();

	if ( sv_showladders.GetBool() )
	{
		m_debugOverlays |= OVERLAY_TEXT_BIT;
	}

#ifdef TERROR
	if ( m_iLadderTeam == LADDER_TEAM_SURVIVOR )
		ChangeTeam( TEAM_SURVIVOR );
	else if ( m_iLadderTeam == LADDER_TEAM_INFECTED )
		ChangeTeam( TEAM_ZOMBIE );
#endif
	// Srcbox: L4D's team key (1 survivor / 2 infected) was being passed straight to
	// ChangeTeam, which in hl2mp is spectator / combine. Leave the entity unassigned.

	if ( !m_bAdapterSpawned && GetModel() != NULL )
	{
		// Deferred one tick: the CFuncLadder we are about to create needs to be
		// able to trace against fully spawned level geometry, and it wants to
		// run its own Activate() outside of ours.
		SetThink( &CFuncSimpleLadder::AdapterThink );
		SetNextThink( gpGlobals->curtime + 0.1f );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Parse the L4D keys. Note that these are key *names*, not values.
//-----------------------------------------------------------------------------
bool CFuncSimpleLadder::KeyValue( const char *szKeyName, const char *szValue )
{
	if ( FStrEq( szKeyName, "normal.x" ) )
	{
		m_climbableNormal.SetX( atof( szValue ) );
		return true;
	}

	if ( FStrEq( szKeyName, "normal.y" ) )
	{
		m_climbableNormal.SetY( atof( szValue ) );
		return true;
	}

	if ( FStrEq( szKeyName, "normal.z" ) )
	{
		m_climbableNormal.SetZ( atof( szValue ) );
		return true;
	}

	if ( FStrEq( szKeyName, "team" ) )
	{
		m_iLadderTeam = atoi( szValue );
		return true;
	}

	return BaseClass::KeyValue( szKeyName, szValue );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CFuncSimpleLadder::AdapterThink( void )
{
	SetThink( NULL );

	if ( !m_bAdapterSpawned )
	{
		CreatePlayerLadder();
	}
}

//-----------------------------------------------------------------------------
// Purpose: Pick the side of the brush that the player climbs from. The face is
//			the one without a wall behind it.
//-----------------------------------------------------------------------------
void CFuncSimpleLadder::ComputeClimbableNormal( const Vector &mins, const Vector &maxs, Vector &outNormal )
{
	float xSize = maxs.x - mins.x;
	float ySize = maxs.y - mins.y;

	// Ladders are wide along the rungs and thin along the direction you climb
	// from, so the facing axis is the thin one.
	bool thinIsX = ( ySize > xSize );

	Vector axis = thinIsX ? Vector( 1, 0, 0 ) : Vector( 0, 1, 0 );
	float halfDepth = 0.5f * ( thinIsX ? xSize : ySize );

	Vector center = ( mins + maxs ) * 0.5f;

	// Candidate directions: the L4D normal key first, then the brush's thin
	// axis.
	Vector keyNormal = m_climbableNormal;
	keyNormal.z = 0.0f;

	bool hasKey = keyNormal.LengthSqr() > 0.001f;
	if ( hasKey )
	{
		VectorNormalize( keyNormal );
	}

	// Srcbox: compiled L4D maps always write a real, horizontal normal (vbsp computes it
	// from the ladder face); every func_simpleladder in c11m1 has one, and it matches the
	// nav ladder's facing. Trust it outright: the probe below can flip it whenever there
	// is a railing or wall within 48 units in front of the ladder.
	if ( hasKey && m_climbableNormal.Get().Length2DSqr() > 0.25f )
	{
		outNormal = keyNormal;
		return;
	}

	Vector candidates[ 4 ];
	int numCandidates = 0;

	if ( hasKey )
	{
		candidates[ numCandidates++ ] = keyNormal;
		candidates[ numCandidates++ ] = -keyNormal;
	}

	candidates[ numCandidates++ ] = axis;
	candidates[ numCandidates++ ] = -axis;

	// The climbable side is the open one: the wall the ladder is bolted to
	// blocks the other side. Probe outward from just outside the brush.
	for ( int i = 0; i < numCandidates; i++ )
	{
		Vector dir = candidates[ i ];
		Vector start = center + dir * ( halfDepth + 2.0f );

		trace_t tr;
		UTIL_TraceLine( start, start + dir * SIMPLELADDER_FACE_PROBE,
			MASK_PLAYERSOLID, this, COLLISION_GROUP_NONE, &tr );

		if ( tr.fraction == 1.0f && !tr.startsolid )
		{
			outNormal = dir;
			return;
		}
	}

	// Geometry was inconclusive (freestanding ladder, recessed brush, etc).
	// Prefer the authored normal, then the thin axis.
	outNormal = hasKey ? keyNormal : axis;
}

//-----------------------------------------------------------------------------
// Purpose: Drop the player hull until it hits something solid.
//-----------------------------------------------------------------------------
bool CFuncSimpleLadder::FindStandingSpot( const Vector &probe, float flUp, float flDown, Vector &out )
{
	Vector start = probe;
	start.z = probe.z + flUp;

	Vector end = probe;
	end.z = probe.z - flDown;

	trace_t tr;
	UTIL_TraceHull( start, end, VEC_HULL_MIN, VEC_HULL_MAX, MASK_PLAYERSOLID,
		this, COLLISION_GROUP_PLAYER_MOVEMENT, &tr );

	if ( tr.startsolid || tr.fraction >= 1.0f )
		return false;

	out = tr.endpos;
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Translate this brush into the CFuncLadder volume that HL2 movement
//			actually understands, plus dismount nodes so players can get off at
//			the top and bottom.
//-----------------------------------------------------------------------------
void CFuncSimpleLadder::CreatePlayerLadder( void )
{
	// Only ever try once, even if the bounds turn out to be unusable.
	m_bAdapterSpawned = true;

	if ( GetModel() == NULL || GetMoveParent() != NULL )
		return;

	if ( m_iLadderTeam == LADDER_TEAM_INFECTED && !sv_simpleladder_infected.GetBool() )
		return;

	Vector mins, maxs;
	CollisionProp()->WorldSpaceSurroundingBounds( &mins, &maxs );

	if ( mins.x >= maxs.x || mins.y >= maxs.y || mins.z >= maxs.z )
		return;

	Vector normal;
	ComputeClimbableNormal( mins, maxs, normal );

	// Support point of the brush bounds along the climb normal. This is exact
	// for the axis-aligned normals that nearly every ladder uses, and close
	// enough for the rare rotated one.
	Vector half = ( maxs - mins ) * 0.5f;
	Vector face = ( mins + maxs ) * 0.5f;
	face.x += normal.x * half.x;
	face.y += normal.y * half.y;

	// Bottom dismount: on the floor in front of the ladder.
	Vector bottomDismount;
	Vector probe = face + normal * SIMPLELADDER_DISMOUNT_DISTANCE;
	probe.z = mins.z;

	bool foundBottom = FindStandingSpot( probe, 64.0f, 64.0f, bottomDismount );
	if ( foundBottom && fabs( bottomDismount.z - mins.z ) > 32.0f )
	{
		foundBottom = false;
	}

	if ( !foundBottom )
	{
		bottomDismount = probe;
		bottomDismount.z = mins.z + 1.0f;
	}

	// Top dismount: usually on top of the wall/ledge the ladder leans against,
	// but it can also be a window sill or balcony on the climb side.
	Vector topDismount;
	Vector topProbe = face - normal * SIMPLELADDER_DISMOUNT_DISTANCE;
	topProbe.z = maxs.z;

	bool foundTop = FindStandingSpot( topProbe, 96.0f, 96.0f, topDismount );
	if ( foundTop && topDismount.z < maxs.z - 72.0f )
	{
		// Dropped way past the top of the ladder, so that was not a landing.
		foundTop = false;
	}

	if ( !foundTop )
	{
		topProbe = face + normal * SIMPLELADDER_DISMOUNT_DISTANCE;
		topProbe.z = maxs.z;
		foundTop = FindStandingSpot( topProbe, 96.0f, 96.0f, topDismount );
	}

	if ( !foundTop )
	{
		topDismount = face - normal * SIMPLELADDER_DISMOUNT_DISTANCE;
		topDismount.z = maxs.z + 1.0f;
	}

	// Put the climb line just outside the player's hull so the hull does not
	// overlap the solid ladder brush while climbing.
	Vector mount = face + normal * SIMPLELADDER_MOUNT_OFFSET;

	// Player origins are at the feet (VEC_HULL_MIN.z == 0), so the mount points
	// belong on the floor at each end, not half a hull above it.
	Vector bottom = mount;
	bottom.z = foundBottom ? bottomDismount.z : mins.z;

	Vector top = mount;
	top.z = maxs.z;

	if ( top.z <= bottom.z + 16.0f )
	{
		DevMsg( 1, "func_simpleladder at (%.0f %.0f %.0f) is too short to climb\n",
			GetAbsOrigin().x, GetAbsOrigin().y, GetAbsOrigin().z );
		return;
	}

	// Dismount nodes are what makes the ends of a ladder usable without the
	// player having to aim into a wall and press +use.
	CBaseEntity *pDismount = CreateEntityByName( "info_ladder_dismount" );
	if ( pDismount )
	{
		pDismount->SetAbsOrigin( topDismount );
		DispatchSpawn( pDismount );
	}

	pDismount = CreateEntityByName( "info_ladder_dismount" );
	if ( pDismount )
	{
		pDismount->SetAbsOrigin( bottomDismount );
		DispatchSpawn( pDismount );
	}

	CFuncLadder *pLadder = (CFuncLadder *)CreateEntityByName( "func_useableladder" );
	if ( !pLadder )
		return;

	pLadder->KeyValue( "point0", CFmtStr( "%f %f %f", top.x, top.y, top.z ) );
	pLadder->KeyValue( "point1", CFmtStr( "%f %f %f", bottom.x, bottom.y, bottom.z ) );

	DispatchSpawn( pLadder );

	// DispatchSpawn only calls Spawn(). CFuncLadder::Activate() is what hooks
	// the dismount nodes (and sv_showladders), so runtime-created ladders have
	// to be activated by hand or they are impossible to get off of.
	pLadder->Activate();

	DevMsg( 2, "func_simpleladder at (%.0f %.0f %.0f): mount line (%.0f %.0f %.0f)-(%.0f %.0f %.0f), normal (%.0f %.0f %.0f)\n",
		GetAbsOrigin().x, GetAbsOrigin().y, GetAbsOrigin().z,
		bottom.x, bottom.y, bottom.z, top.x, top.y, top.z,
		normal.x, normal.y, normal.z );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
int CFuncSimpleLadder::DrawDebugTextOverlays( void )
{
	int text_offset = BaseClass::DrawDebugTextOverlays();

	if ( ( m_debugOverlays & OVERLAY_TEXT_BIT ) == 0 )
		return text_offset;

	char tempstr[ 512 ];

	static const char *s_pszL4DTeams[] = { "any", "survivors", "infected" };
	V_snprintf( tempstr, sizeof( tempstr ), "Team: %s", ( m_iLadderTeam >= 0 && m_iLadderTeam <= 2 ) ? s_pszL4DTeams[ m_iLadderTeam ] : "?" );
	EntityText( text_offset++, tempstr, 0.0 );

	Vector mins, maxs;
	CollisionProp()->WorldSpaceSurroundingBounds( &mins, &maxs );

	Vector normal;
	ComputeClimbableNormal( mins, maxs, normal );
	V_snprintf( tempstr, sizeof( tempstr ), "Climb normal: (%.0f %.0f %.0f)", normal.x, normal.y, normal.z );
	EntityText( text_offset++, tempstr, 0.0 );

	return text_offset;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CFuncSimpleLadder::DrawDebugGeometryOverlays( void )
{
	BaseClass::DrawDebugGeometryOverlays();

	if ( !sv_showladders.GetBool() )
		return;

	Vector mins, maxs;
	CollisionProp()->WorldSpaceSurroundingBounds( &mins, &maxs );
	NDebugOverlay::Box( vec3_origin, mins, maxs, 0, 255, 0, 32, 0.0f );
}
