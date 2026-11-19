//========= Srcbox ============================================================//
//
// Purpose: GMod-style bone manipulation ( Entity:ManipulateBonePosition /
//          ManipulateBoneAngles / ManipulateBoneScale ) and bone/physics-bone
//          lookups, for Lua.
//
//  Manipulations are per entity, per bone:
//    position  added in the bone's own ( animated ) frame
//    angles    applied as a local rotation of the bone
//    scale     scales the bone's matrix, not its children's
//
//  The server keeps them on a "srcbox_bonemanip" entity that follows its
//  target and is sent to every client; the client mirrors them ( and Lua on
//  the client can set its own ). Both sides apply them while building bones
//  ( CBaseAnimating::SetupBones / C_BaseAnimating::SetupBones ).
//
//=============================================================================//

#ifndef SRCBOX_BONEMANIP_H
#define SRCBOX_BONEMANIP_H
#ifdef _WIN32
#pragma once
#endif

#ifdef LUA_SDK

#include "studio.h"

class CStudioHdr;
// ( CBaseAnimating is C_BaseAnimating on the client, through shared_classnames.h )

// Per-bone manipulation values
struct SrcboxBoneManipData_t
{
	SrcboxBoneManipData_t() { Reset(); }

	void Reset()
	{
		for ( int i = 0; i < MAXSTUDIOBONES; i++ )
		{
			m_Pos[i].Init();
			m_Ang[i].Init();
			m_Scale[i].Init( 1.0f, 1.0f, 1.0f );
		}
		m_bAny = false;
	}

	void UpdateAny()
	{
		m_bAny = false;
		for ( int i = 0; i < MAXSTUDIOBONES && !m_bAny; i++ )
		{
			if ( m_Pos[i] != vec3_origin || m_Ang[i] != vec3_angle || m_Scale[i] != Vector( 1, 1, 1 ) )
				m_bAny = true;
		}
	}

	Vector	m_Pos[MAXSTUDIOBONES];
	QAngle	m_Ang[MAXSTUDIOBONES];
	Vector	m_Scale[MAXSTUDIOBONES];
	bool	m_bAny;
};

// The manipulations for an entity, or NULL when it has none
const SrcboxBoneManipData_t *SrcboxBoneManip_Get( const CBaseEntity *pEntity );

// Setters ( server: networked to clients; client: local only, until the server next sends that entity's )
void SrcboxBoneManip_SetPosition( CBaseAnimating *pEntity, int iBone, const Vector &vecPos );
void SrcboxBoneManip_SetAngles( CBaseAnimating *pEntity, int iBone, const QAngle &ang );
void SrcboxBoneManip_SetScale( CBaseAnimating *pEntity, int iBone, const Vector &vecScale );

// While building bones: local ( pos / q ) and final ( bone-to-world matrices )
void SrcboxBoneManip_ApplyLocal( const CBaseEntity *pEntity, const CStudioHdr *pHdr, Vector *pos, Quaternion *q, int boneMask );
void SrcboxBoneManip_ApplyScale( const CBaseEntity *pEntity, const CStudioHdr *pHdr, matrix3x4_t *pBoneToWorld, int boneMask );

// Physics bone ( ragdoll solid index ) <-> model bone, from the model's collision data
int SrcboxPhysBoneToBone( CBaseAnimating *pEntity, int iPhysBone );
int SrcboxBoneToPhysBone( CBaseAnimating *pEntity, int iBone );

#endif // LUA_SDK

#endif // SRCBOX_BONEMANIP_H
