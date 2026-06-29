#include "cbase.h"
#include "p3_weapon_shared.h"
#ifndef CLIENT_DLL
#include "p3/p3_player.h"
#include "p3/weapons/p3_base_weapon.h"
#else
#include "p3/p3_c_player.h"
#include "p3/weapons/p3_c_base_weapon.h"
#endif
#include "core/parser.h"
#include "debugoverlay_shared.h"
#include "takedamageinfo.h"

#ifdef CLIENT_DLL
#define CP3_BaseWeapon C_P3_BaseWeapon
#endif

static ConVar p3_weapon_pos_debug( "p3_weapon_pos_debug", "0", FCVAR_CHEAT | FCVAR_REPLICATED );


WeaponType_t ParseWeaponType( const char *name )
{
	static char WeaponTypeNames[][32] = {
		"flesh",
		"none",
		"animal",
		"gadget",
		"nonlethal_melee",
		"nonlethal_ranged",
		"melee",
		"ranged",
		"big_gun"
	};

	return ( WeaponType_t )( ParseToken( name, WeaponTypeNames, WPN_TYPE_SIZE + 1, WPN_TYPE_NONE ) - 1 );
}


static char WeaponTypeNames[][32] = {
	"emptyhands",
	"wolverine",
	"nailbat",
	"grenade",
	"deserteagle",
	"taser",
	"shotgun",
	"m60",
	"m16",
	"cat",
	"machete",
	"shovel",
	"molotov",
	"gasoline",
	"catnip",
	"banner",
	"beenest",
	"baton",
	"crotchy_grenade",
	"hammer",
	"laserpen",
	"match",
	"maxel_sniper",
	"metabulky",
	"metaonehandled",
	"metatwohandled",
	"motorhead",
	"photocam",
	"seed",
	"shopvac",
	"spray",
	"tvcam",
	"weemote",
	"m136",
	"fireaxe",
	"gamamet",
	"glock17",
	"revolver",
	"ttpistol",
	"ak47",
	"p90",
	"uzi",
};

WeaponID_t ParseWeaponID( const char *sId )
{
	WeaponID_t id = (WeaponID_t)(ParseToken(sId, WeaponTypeNames, P3_WEAPON_COUNT-1)+1);
	if (id == P3_WEAPON_WRONG || id == P3_WEAPON_DISABLED)
	{
		DevWarning("Parse unknown weapon %s\n", sId);
	}
	return id;
}

const char* WeaponID2Name( WeaponID_t id )
{
	if (id < P3_WEAPON_EMPTYHANDS || id >= P3_WEAPON_COUNT) 
	{
		DevWarning("Weapon ID=%d isn't valid", (int)id);
		return NULL;
	}
	return WeaponTypeNames[id-1];
}

void P3_FillDmgInfo( const CTakeDamageInfo& info, IGameEvent *event )
{
#if defined(GAME_DLL)
	event->SetInt( "attacker_id", info.GetAttacker() ? info.GetAttacker()->entindex() : 0 );
	if ( !info.GetWeapon() && info.GetDamageType() & DMG_BURN )
	{
		event->SetInt( "attacker_id", 1 ); // ;)
		event->SetInt( "weapon_id", P3_WEAPON_MOLOTOV );
	}
	else
	{
		event->SetInt( "weapon_id", info.GetWeapon() ? info.GetWeapon()->MyP3BaseWeapon()->GetWeaponID() : 0 );
	}
	event->SetInt( "weapon_flags", info.GetWeapon() ? info.GetWeapon()->MyP3BaseWeapon()->GetWeaponFlags() : 0 );
#endif
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
bool
CP3_BaseWeapon::UpdateHoldPos( float dt )
{
	if ( GetOwner()->IsNPC() )
		return false;

	CBaseCombatCharacter* pOwner = GetOwner()->MyCombatCharacterPointer();
	CP3_Player* pPlayer = ToP3Player( pOwner );

	Assert( pOwner );

	bool ret = false;

	Vector frontSensorPos;
	Vector backSensorPos;
	Vector sensorsStart;

	if (pPlayer)
	{
		Vector pl_frontSensorPos;
		Vector pl_backSensorPos;

		float body_and = pPlayer->IsAiming() ? pPlayer->GetPoseParameter(pPlayer->LookupPoseParameter("aim_yaw")) : pPlayer->GetPoseParameter(pPlayer->LookupPoseParameter("body_yaw"));

		body_and = (int(body_and/10.0f) * 10);
		matrix3x4_t xform;

		AngleMatrix( QAngle( 0, body_and, 0 ), Vector(0,0,0), xform );

		VectorTransform( m_vFrontSensorOffset, xform, pl_frontSensorPos );
		VectorTransform( m_vBackSensorOffset, xform, pl_backSensorPos );

		pOwner->EntityToWorldSpace( pl_frontSensorPos, &frontSensorPos );
		pOwner->EntityToWorldSpace( pl_backSensorPos, &backSensorPos );
		pOwner->EntityToWorldSpace( Vector( 0, 0, m_vFrontSensorOffset.z ), &sensorsStart );
	}
	else
	{
		pOwner->EntityToWorldSpace( m_vFrontSensorOffset, &frontSensorPos );
		pOwner->EntityToWorldSpace( m_vBackSensorOffset, &backSensorPos );
		pOwner->EntityToWorldSpace( Vector( 0, 0, m_vFrontSensorOffset.z ), &sensorsStart );
	}


	if ( CheckCollision( sensorsStart, backSensorPos ) )
	{
		m_flTargetHoldPos = 1;
		ret = true;
	}
	else if ( CheckCollision( sensorsStart, frontSensorPos ) )
	{
		m_flTargetHoldPos = -1;
		ret = true;
	}
	else
	{
		m_flTargetHoldPos = 0;
	}

	if (ret)
	{
		pOwner->SetPoseParameter( "aim_pitch", 0 );
	}

	if ( m_flCurrentHoldPos != m_flTargetHoldPos )
	{
		if ( ( m_flTargetHoldPos - m_flCurrentHoldPos ) < 0 )
		{
			m_flCurrentHoldPos -=  dt * m_flHoldPosChangeSpeed;
			if ( m_flCurrentHoldPos < m_flTargetHoldPos )
				m_flCurrentHoldPos = m_flTargetHoldPos;
		}
		else
		{
			m_flCurrentHoldPos +=  dt * m_flHoldPosChangeSpeed;
			if ( m_flCurrentHoldPos > m_flTargetHoldPos )
				m_flCurrentHoldPos = m_flTargetHoldPos;
		}
	}

	if ( !m_bAttacking )
	{
		pOwner->SetPoseParameter( "hold_pos", m_flCurrentHoldPos );
	}
	else
	{
		pOwner->SetPoseParameter( "hold_pos", 0 );
	}

	if ( p3_weapon_pos_debug.GetBool() )
	{
		if (ret)
		{
			NDebugOverlay::Line( sensorsStart, frontSensorPos, 255,0,0, true, 0.1f );
			NDebugOverlay::Line( sensorsStart, backSensorPos, 255,0,255, true, 0.1f );
		}
		else
		{
			NDebugOverlay::Line( sensorsStart, frontSensorPos, 0,255,0, true, 0.1f );
			NDebugOverlay::Line( sensorsStart, backSensorPos, 0,0,255, true, 0.1f );
		}

		char buf[16];
		Q_snprintf( buf, sizeof buf, "%.02f (%0.2f) %s", m_flCurrentHoldPos, m_flTargetHoldPos, ret ? "b" : "" );
		NDebugOverlay::Text( pOwner->GetAbsOrigin() + Vector(0,0,80), buf, true, 0.1f );
	}

	if ( pPlayer )
	{
		if ( pPlayer->IsCovering() )
		{
			if ( pPlayer->IsPlayerAimingBack() )
			{
				ret = false;
			}

			int flags = pPlayer->GetPlayerState();
			if ( P3_CHECK_FLAGS( flags, P3_PLAYERSTATE_READYTOSHOOT) )
			{
				ret = false;
			}
		}
	}

	m_bWeaponBlocked = ret;

	return ret;
}


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
bool
CP3_BaseWeapon::CheckCollision( const Vector& start, const Vector& end )
{
#if NEW_BLOCKED_TRACE
	class CTraceFilterNotPlayer : public ITraceFilter
	{
	public:
		virtual TraceType_t	GetTraceType() const
		{
			return TRACE_EVERYTHING;
		}

		virtual bool ShouldHitEntity( IHandleEntity *pEntityHandle, int contentsMask )
		{
			CBaseEntity *pEntity = EntityFromEntityHandle( pEntityHandle );
			if ( pEntity && !pEntity->IsPlayer() )
			{
				for ( CBaseEntity* pOwner = pEntity->GetOwnerEntity();
					  pOwner; pOwner = pOwner->GetOwnerEntity() )
				{
					if ( pOwner->IsPlayer() )
					{
						return false;
					}
				}

				return true;
			}
			else
			{
				return false;
			}
		}
	};

	CTraceFilterNotPlayer traceFilter;
	trace_t trace;
#else
	CBaseCombatCharacter *pOwner = ToBaseCombatCharacter( GetOwner() );
	CBaseEntity *pOwnerParent = pOwner->GetMoveParent();

	trace_t trace;
	CTraceFilterSimpleList traceFilter( COLLISION_GROUP_NONE );
	traceFilter.AddEntityToIgnore( this );
	traceFilter.AddEntityToIgnore( pOwner );
	if ( pOwnerParent )
		traceFilter.AddEntityToIgnore( pOwnerParent );
	if ( m_pSensorIgnoreEntity )
		traceFilter.AddEntityToIgnore( m_pSensorIgnoreEntity );
#endif

	const float flTraceRadius = 1;
	const Vector vecMins( -flTraceRadius, -flTraceRadius, -flTraceRadius);
	const Vector vecMaxs( flTraceRadius, flTraceRadius, flTraceRadius );

	UTIL_TraceLine( start, end, MASK_SHOT_HULL, &traceFilter, &trace );
	//UTIL_TraceHull ( start, end, vecMins, vecMaxs,MASK_SHOT_HULL, &traceFilter, &trace );

#ifdef GAME_DLL
	m_hBlockEnt = trace.m_pEnt;
#endif

	return trace.fraction < 1;
}

void
CP3_BaseWeapon::DrawDebugOverlay()
{
}
