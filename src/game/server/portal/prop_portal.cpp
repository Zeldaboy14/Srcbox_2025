//========= Portal Gun for TF2 - original implementation =================//
//
// Purpose: See tf_portal.h.
//
//==========================================================================//

#include "cbase.h"
#include "prop_portal.h"
#include "hl2mp_player.h"
#include "hl2mp_portal_shared.h"

//-----------------------------------------------------------------------------
IMPLEMENT_NETWORKCLASS_ALIASED( TFPortal, DT_TFPortal )

BEGIN_NETWORK_TABLE( CTFPortal, DT_TFPortal )
	SendPropEHandle( SENDINFO( m_hLinkedPortal ) ),
	SendPropInt( SENDINFO( m_nPortalColor ), 2, SPROP_UNSIGNED ),
	SendPropBool( SENDINFO( m_bActive ) ),
END_NETWORK_TABLE()

LINK_ENTITY_TO_CLASS( prop_portal, CTFPortal );

//-----------------------------------------------------------------------------
CTFPortal::CTFPortal()
{
	m_nPortalColor = PORTAL_COLOR_BLUE;
	m_bActive = false;
	m_flLastTeleportTime = -1.0f;
}

//-----------------------------------------------------------------------------
void CTFPortal::Precache( void )
{
	BaseClass::Precache();

	PrecacheScriptSound( "Portalgun.portal_enter_blue" );
	PrecacheScriptSound( "Portalgun.portal_enter_orange" );
	PrecacheScriptSound( "Portalgun.portal_exit_blue" );
	PrecacheScriptSound( "Portalgun.portal_exit_orange" );

	// NOTE: portal-PLACEMENT particle effects are precached and played
	// from tf_weapon_portalgun.cpp (CTFWeaponPortalgun::Precache /
	// FirePortal), not here. This entity's TeleportEntity() only ever
	// emits sound, so that's all this file needs to precache. (An earlier
	// version of this file precached particle names that this file never
	// actually creates -- removed.)
}

//-----------------------------------------------------------------------------
void CTFPortal::Spawn( void )
{
	Precache();

	SetSolid( SOLID_BBOX );
	AddSolidFlags( FSOLID_TRIGGER | FSOLID_NOT_SOLID );
	SetMoveType( MOVETYPE_NONE );

	// CTFPortal has no model (see header) -- it renders via C_TFPortal's
	// custom dynamic mesh instead. CBaseEntity::UpdateTransmitState()
	// defaults to FL_EDICT_DONTSEND for any entity with no model index
	// UNLESS EFL_FORCE_CHECK_TRANSMIT is set, in which case it falls
	// through to the normal FL_EDICT_PVSCHECK path instead. Without this,
	// the entity is never sent to any client at all -- not a rendering or
	// visibility bug, an outright network-layer block -- which is exactly
	// what was happening: the client-side C_TFPortal was never being
	// created in the first place, so nothing downstream of that (leaf
	// system, ShouldDraw, DrawModel) ever had a chance to run.
	AddEFlags( EFL_FORCE_CHECK_TRANSMIT );

	// Local +X is the portal's outward normal (forward axis).
	SetCollisionBounds( Vector( -TF_PORTAL_TRIGGER_DEPTH, -TF_PORTAL_HALF_WIDTH, -TF_PORTAL_HALF_HEIGHT ),
	                    Vector(  TF_PORTAL_TRIGGER_DEPTH,  TF_PORTAL_HALF_WIDTH,  TF_PORTAL_HALF_HEIGHT ) );

	m_bActive = false;
}

//-----------------------------------------------------------------------------
void CTFPortal::PlaceAt( const Vector &vecOrigin, const QAngle &angles, const Vector &vecPlacementNormal )
{
	SetAbsOrigin( vecOrigin );
	SetAbsAngles( angles );
	m_vecPortalNormal = vecPlacementNormal;
	m_bActive = true;

	m_hLastTeleported = NULL;
	m_flLastTeleportTime = -1.0f;
}

//-----------------------------------------------------------------------------
void CTFPortal::LinkTo( CTFPortal *pOther )
{
	m_hLinkedPortal = pOther;
	if ( pOther )
		pOther->m_hLinkedPortal = this;
}

//-----------------------------------------------------------------------------
void CTFPortal::StartTouch( CBaseEntity *pOther )
{
	BaseClass::StartTouch( pOther );

	if ( !m_bActive )
		return;

	CTFPortal *pExit = GetLinkedPortal();
	if ( !pExit || !pExit->IsActive() )
		return;

	if ( !CanTeleport( pOther ) )
		return;

	TeleportEntity( pOther, pExit );
}

//-----------------------------------------------------------------------------
bool CTFPortal::CanTeleport( CBaseEntity *pOther ) const
{
	CHL2MP_Player *pPlayer = ToHL2MPPlayer( pOther );
	if ( !pPlayer || !pPlayer->IsAlive() )
		return false;

	if ( m_hLastTeleported.Get() == pOther &&
	     ( gpGlobals->curtime - m_flLastTeleportTime ) < TF_PORTAL_RETOUCH_GRACE_PERIOD )
		return false;

	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Teleport player through portal with momentum conservation.
//
// EXIT NUDGE
// ==========
// After the transform the player's feet (origin) may sit very close to --
// or mathematically inside -- the exit wall.  We push the player along the
// exit portal's outward normal until they clear their own hull.
//
// The TF2 player hull occupies ±16 units in X/Y and 0..82 units in Z
// (world up).  Projecting these onto the exit normal gives the worst-case
// penetration distance per surface orientation:
//
//   Wall portal   (n ~ horizontal): 16 * (|nx|+|ny|) + 10 buffer ≈ 26 units
//   Floor portal  (n =  +Z):        0  (hull extends up, away from floor)
//   Ceiling portal (n = -Z):        82 (full height projects INTO ceiling)
//                                      + 10 buffer = 92 units
//
// flMinExitDist formula: 16*(|nx|+|ny|) + 82*max(0,-nz) + 10, clamped to 24.
// The original code used a fixed 40 units which is fine for walls but
// puts the player 42 units INSIDE a ceiling -- hence the stuck-in-ceiling bug.
//-----------------------------------------------------------------------------
void CTFPortal::TeleportEntity( CBaseEntity *pOther, CTFPortal *pExitPortal )
{
	CHL2MP_Player*pPlayer = ToHL2MPPlayer( pOther );
	if ( !pPlayer )
		return;

	matrix3x4_t matTransform;
	TF_Portal_ComputeTransform( GetAbsOrigin(), GetAbsAngles(),
	                             pExitPortal->GetAbsOrigin(), pExitPortal->GetAbsAngles(),
	                             matTransform );

	Vector vecNewOrigin;
	TF_Portal_TransformPoint( matTransform, pPlayer->GetAbsOrigin(), vecNewOrigin );

	Vector vecNewVelocity;
	TF_Portal_TransformDirection( matTransform, pPlayer->GetAbsVelocity(), vecNewVelocity );

	QAngle vecNewAngles;
	TF_Portal_TransformAngles( matTransform, pPlayer->EyeAngles(), vecNewAngles );

	// --- Orientation-aware exit clearance ---
	Vector vecExitNormal = pExitPortal->GetPortalNormal();

	// Minimum safe distance from the exit surface so the full player hull
	// doesn't intersect it.  See function-level comment above.
	float flMinExitDist = 16.0f * ( fabsf( vecExitNormal.x ) + fabsf( vecExitNormal.y ) )
	                    + 82.0f * MAX( 0.0f, -vecExitNormal.z )
	                    + 10.0f;
	flMinExitDist = MAX( flMinExitDist, 24.0f );

	float flDistAlongNormal = vecExitNormal.Dot( vecNewOrigin - pExitPortal->GetAbsOrigin() );
	if ( flDistAlongNormal < flMinExitDist )
	{
		vecNewOrigin += vecExitNormal * ( flMinExitDist - flDistAlongNormal );
	}

	// Portal enter/exit sounds from scripts/game_sounds_portalgun.txt.
	// Color-specific, not random variants -- confirmed against a real
	// working implementation: enter1/exit1 are the BLUE files, enter2/exit2
	// are the ORANGE files, tied to portal color rather than interchangeable.
	pPlayer->EmitSound( GetPortalColor() == PORTAL_COLOR_BLUE ? "Portalgun.portal_enter_blue" : "Portalgun.portal_enter_orange" );
	pExitPortal->EmitSound( pExitPortal->GetPortalColor() == PORTAL_COLOR_BLUE ? "Portalgun.portal_exit_blue" : "Portalgun.portal_exit_orange" );

	// Move the player directly via SetAbsOrigin / SetAbsVelocity /
	// SnapEyeAngles. (CTFPlayer::Teleport() isn't actually overridden in
	// tf_player.cpp, so there'd be nothing gained by calling it instead --
	// this is the same base CBaseEntity behavior either way.)
	pPlayer->SetGroundEntity( NULL );
	pPlayer->SetAbsOrigin( vecNewOrigin );
	pPlayer->SetAbsVelocity( vecNewVelocity );
	pPlayer->SnapEyeAngles( vecNewAngles );

	pExitPortal->m_hLastTeleported = pPlayer;
	pExitPortal->m_flLastTeleportTime = gpGlobals->curtime;
}
