//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
//===========================================================================//

#include "cbase.h"
#include "beam_shared.h"
#include "spotlightend.h"
#include "dlight.h"
#include "iefx.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

// Spawnflags
enum BeamSpotlightSpawnFlags_t
{
	SF_BEAM_SPOTLIGHT_START_LIGHT_ON = 1,
	SF_BEAM_SPOTLIGHT_NO_DYNAMIC_LIGHT = 2,
	SF_BEAM_SPOTLIGHT_START_ROTATE_ON = 4,
	SF_BEAM_SPOTLIGHT_REVERSE_DIRECTION = 8,
	SF_BEAM_SPOTLIGHT_X_AXIS = 16,
	SF_BEAM_SPOTLIGHT_Y_AXIS = 32,
};

// For backwards compatibility
#define SF_SPOTLIGHT_START_LIGHT_ON SF_BEAM_SPOTLIGHT_START_LIGHT_ON
#define SF_SPOTLIGHT_NO_DYNAMIC_LIGHT SF_BEAM_SPOTLIGHT_NO_DYNAMIC_LIGHT

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
class CSpotlightTraceCacheEntry
{
public:
	CSpotlightTraceCacheEntry()
	{
		m_origin.Init();
		m_radius = -1.0f;
	}
	bool IsValidFor(const Vector& origin)
	{
		if (m_radius > 0 && m_origin.DistToSqr(origin) < 1.0f)
			return true;
		return false;
	}
	void Cache(const Vector& origin, const trace_t& tr)
	{
		m_radius = (tr.endpos - origin).Length();
		m_origin = origin;
	}

	Vector	m_origin;
	float	m_radius;
};

static const int NUM_CACHE_ENTRIES = 64;

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
class CPointSpotlight : public CServerOnlyPointEntity
{
	DECLARE_CLASS(CPointSpotlight, CServerOnlyPointEntity);
public:
	DECLARE_DATADESC();

	CPointSpotlight();

	void	Precache(void);
	void	Spawn(void);
	virtual void Activate();

	virtual void OnEntityEvent(EntityEvent_t event, void* pEventData);

	virtual void SetParent(CBaseEntity* pNewParent, int iAttachment = -1);

private:
	int 	UpdateTransmitState();
	void	RecalcRotation(void);

	void	SpotlightThink(void);
	void	SpotlightUpdate(void);
	Vector	SpotlightCurrentPos(void);
	void	SpotlightCreate(void);
	void	SpotlightDestroy(void);

	// ------------------------------
	//  Inputs
	// ------------------------------
	void InputLightOn(inputdata_t& inputdata);
	void InputLightOff(inputdata_t& inputdata);

	void InputStart(inputdata_t& inputdata);
	void InputStop(inputdata_t& inputdata);
	void InputReverse(inputdata_t& inputdata);

	// Creates the efficient spotlight 
	void CreateEfficientSpotlight();

	// Computes render info for a spotlight
	void ComputeRenderInfo();

	void PassParentToChildren(CBaseEntity* pParent);

private:
	bool	m_bSpotlightOn;
	bool	m_bHasDynamicLight;
	bool	m_bEfficientSpotlight;
	bool	m_bIgnoreSolid;
	Vector	m_vSpotlightTargetPos;
	Vector	m_vSpotlightCurrentPos;
	Vector	m_vSpotlightDir;
	int		m_nHaloSprite;
	CHandle<CBeam>			m_hSpotlight;
	CHandle<CSpotlightEnd>	m_hSpotlightTarget;

	float	m_flLightScale;
	dlight_t* m_pDynamicLight;
	float	m_lastTime;
	CSpotlightTraceCacheEntry* m_pCache;

	float	m_flSpotlightMaxLength;
	float	m_flSpotlightCurLength;
	float	m_flSpotlightGoalWidth;
	float	m_flHDRColorScale;
	int		m_nMinDXLevel;

	int m_nRotationAxis;
	float m_flRotationSpeed;

	float m_flmaxSpeed;
	bool m_isRotating;
	bool m_isReversed;

public:
	COutputEvent m_OnOn, m_OnOff;     ///< output fires when turned on, off
};

BEGIN_DATADESC(CPointSpotlight)
DEFINE_FIELD(m_flSpotlightCurLength, FIELD_FLOAT),

DEFINE_FIELD(m_bSpotlightOn, FIELD_BOOLEAN),

DEFINE_FIELD(m_bHasDynamicLight, FIELD_BOOLEAN),
DEFINE_FIELD(m_flRotationSpeed, FIELD_FLOAT),
DEFINE_FIELD(m_isRotating, FIELD_BOOLEAN),
DEFINE_FIELD(m_isReversed, FIELD_BOOLEAN),
DEFINE_FIELD(m_nRotationAxis, FIELD_INTEGER),

DEFINE_FIELD(m_bEfficientSpotlight, FIELD_BOOLEAN),
DEFINE_FIELD(m_vSpotlightTargetPos, FIELD_POSITION_VECTOR),
DEFINE_FIELD(m_vSpotlightCurrentPos, FIELD_POSITION_VECTOR),

// Robin: Don't Save, recreated after restore/transition
//DEFINE_FIELD( m_hSpotlight,			FIELD_EHANDLE ),
//DEFINE_FIELD( m_hSpotlightTarget,		FIELD_EHANDLE ),

DEFINE_FIELD(m_vSpotlightDir, FIELD_VECTOR),
DEFINE_FIELD(m_nHaloSprite, FIELD_INTEGER),

DEFINE_KEYFIELD(m_bIgnoreSolid, FIELD_BOOLEAN, "IgnoreSolid"),
DEFINE_KEYFIELD(m_flSpotlightMaxLength, FIELD_FLOAT, "SpotlightLength"),
DEFINE_KEYFIELD(m_flSpotlightGoalWidth, FIELD_FLOAT, "SpotlightWidth"),
DEFINE_KEYFIELD(m_flHDRColorScale, FIELD_FLOAT, "HDRColorScale"),
DEFINE_KEYFIELD(m_nMinDXLevel, FIELD_INTEGER, "mindxlevel"),

// Inputs
DEFINE_INPUTFUNC(FIELD_VOID, "LightOn", InputLightOn),
DEFINE_INPUTFUNC(FIELD_VOID, "LightOff", InputLightOff),

DEFINE_INPUTFUNC(FIELD_VOID, "Start", InputStart),
DEFINE_INPUTFUNC(FIELD_VOID, "Stop", InputStop),
DEFINE_INPUTFUNC(FIELD_VOID, "Reverse", InputReverse),

DEFINE_OUTPUT(m_OnOn, "OnLightOn"),
DEFINE_OUTPUT(m_OnOff, "OnLightOff"),

DEFINE_THINKFUNC(SpotlightThink),

END_DATADESC()


LINK_ENTITY_TO_CLASS(point_spotlight, CPointSpotlight);
LINK_ENTITY_TO_CLASS(beam_spotlight, CPointSpotlight);

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
CPointSpotlight::CPointSpotlight()
{
#ifdef _DEBUG
	m_vSpotlightTargetPos.Init();
	m_vSpotlightCurrentPos.Init();
	m_vSpotlightDir.Init();
#endif
	m_flHDRColorScale = 1.0f;
	m_nMinDXLevel = 0;
	m_bIgnoreSolid = false;

	m_bHasDynamicLight = true;
	m_flSpotlightMaxLength = 500.0f;
	m_flSpotlightGoalWidth = 50.0f;
	m_flHDRColorScale = 0.7f;
	m_isRotating = false;
	m_isReversed = false;
	m_flmaxSpeed = 100.0f;
	m_flRotationSpeed = 0.0f;
	m_nRotationAxis = 0;

	// beam_spotlight client stuff
	m_flLightScale = 100.0f;


	AddEFlags(EFL_FORCE_ALLOW_MOVEPARENT);
}


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CPointSpotlight::Precache(void)
{
	BaseClass::Precache();

	// Sprites.
	m_nHaloSprite = PrecacheModel("sprites/light_glow03.vmt");
	PrecacheModel("sprites/glow_test02.vmt");
}


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CPointSpotlight::Spawn(void)
{
	Precache();

	UTIL_SetSize(this, vec3_origin, vec3_origin);
	AddSolidFlags(FSOLID_NOT_SOLID);
	SetMoveType(MOVETYPE_NONE);
	AddEFlags(EFL_FORCE_CHECK_TRANSMIT);
	m_bEfficientSpotlight = true;

	m_bHasDynamicLight = !HasSpawnFlags(SF_BEAM_SPOTLIGHT_NO_DYNAMIC_LIGHT);
	m_bSpotlightOn = HasSpawnFlags(SF_SPOTLIGHT_START_LIGHT_ON);
	m_isRotating = HasSpawnFlags(SF_BEAM_SPOTLIGHT_START_ROTATE_ON);
	m_isReversed = HasSpawnFlags(SF_BEAM_SPOTLIGHT_REVERSE_DIRECTION);

	RecalcRotation();
}


//-----------------------------------------------------------------------------
// Computes render info for a spotlight
//-----------------------------------------------------------------------------
void CPointSpotlight::ComputeRenderInfo()
{
	// Fade out spotlight end if past max length.  
	if (m_flSpotlightCurLength > 2 * m_flSpotlightMaxLength)
	{
		m_hSpotlightTarget->SetRenderColorA(0);
		m_hSpotlight->SetFadeLength(m_flSpotlightMaxLength);
	}
	else if (m_flSpotlightCurLength > m_flSpotlightMaxLength)
	{
		m_hSpotlightTarget->SetRenderColorA((1 - ((m_flSpotlightCurLength - m_flSpotlightMaxLength) / m_flSpotlightMaxLength)));
		m_hSpotlight->SetFadeLength(m_flSpotlightMaxLength);
	}
	else
	{
		m_hSpotlightTarget->SetRenderColorA(1.0);
		m_hSpotlight->SetFadeLength(m_flSpotlightCurLength);
	}

	// Adjust end width to keep beam width constant
	float flNewWidth = m_flSpotlightGoalWidth * (m_flSpotlightCurLength / m_flSpotlightMaxLength);
	flNewWidth = clamp(flNewWidth, 0.f, MAX_BEAM_WIDTH);
	m_hSpotlight->SetEndWidth(flNewWidth);

	// Adjust width of light on the end.  
	/*if (m_bHasDynamicLight)
	{
		// <<TODO>> - magic number 1.8 depends on sprite size
		m_hSpotlightTarget->m_flLightScale = 1.8 * flNewWidth;
		if (m_flLightScale > 0)
		{
			const color32 c = GetRenderColor();
			//float a = GetRenderAlpha() / 255.0f;
			//ColorRGBExp32 color;
			//color.r = c.r * a;
			//color.g = c.g * a;
			//color.b = c.b * a;
			//color.exponent = 0;
			//if (color.r == 0 && color.g == 0 && color.b == 0)
			//	return;

			// Deal with the environment light
			//if (!m_pDynamicLight || (m_pDynamicLight->key != index))
			//{
			//	m_pDynamicLight = effects->CL_AllocDlight(index);
			//	assert(m_pDynamicLight);
			//}

			//m_pDynamicLight->flags = DLIGHT_NO_MODEL_ILLUMINATION;
			m_pDynamicLight->radius = m_flLightScale * 3.0f;
			m_pDynamicLight->origin = GetAbsOrigin() + Vector(0, 0, 5);
			m_pDynamicLight->die = gpGlobals->curtime + 0.05f;
			//m_pDynamicLight->color = color;
		}
	}*/

	// Adjust width of light on the end.  
	if (FBitSet(m_spawnflags, SF_SPOTLIGHT_NO_DYNAMIC_LIGHT))
	{
		m_hSpotlightTarget->m_flLightScale = 0.0;
	}
	else
	{
		// <<TODO>> - magic number 1.8 depends on sprite size
		m_hSpotlightTarget->m_flLightScale = 1.8 * flNewWidth;
	}
}


//-----------------------------------------------------------------------------
// Creates the efficient spotlight 
//-----------------------------------------------------------------------------
void CPointSpotlight::CreateEfficientSpotlight()
{
	if (m_hSpotlightTarget.Get() != NULL)
		return;

	SpotlightCreate();
	m_vSpotlightCurrentPos = SpotlightCurrentPos();
	m_hSpotlightTarget->SetAbsOrigin(m_vSpotlightCurrentPos);
	m_hSpotlightTarget->m_vSpotlightOrg = GetAbsOrigin();
	VectorSubtract(m_hSpotlightTarget->GetAbsOrigin(), m_hSpotlightTarget->m_vSpotlightOrg, m_hSpotlightTarget->m_vSpotlightDir);
	m_flSpotlightCurLength = VectorNormalize(m_hSpotlightTarget->m_vSpotlightDir);
	m_hSpotlightTarget->SetMoveType(MOVETYPE_NONE);
	ComputeRenderInfo();

	m_OnOn.FireOutput(this, this);
}


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CPointSpotlight::Activate(void)
{
	BaseClass::Activate();

	if (GetMoveParent())
	{
		m_bEfficientSpotlight = false;
	}

	if (m_bEfficientSpotlight)
	{
		if (m_bSpotlightOn)
		{
			CreateEfficientSpotlight();
		}

		// Don't think
		SetThink(NULL);

		// No targetname and no parent implies this is a static beam
		// Hence, we can kill off ourselves and the end point. The beam visual will remain fixed in place
		if (GetEntityName() == NULL_STRING)
		{
			UTIL_Remove(m_hSpotlightTarget);
			m_hSpotlightTarget = NULL;

			UTIL_Remove(this);
		}
	}
}


//-------------------------------------------------------------------------------------
// Optimization to deal with spotlights
//-------------------------------------------------------------------------------------
void CPointSpotlight::OnEntityEvent(EntityEvent_t event, void* pEventData)
{
	if (event == ENTITY_EVENT_PARENT_CHANGED)
	{
		if (GetMoveParent())
		{
			m_bEfficientSpotlight = false;
			if (m_hSpotlightTarget)
			{
				m_hSpotlightTarget->SetMoveType(MOVETYPE_FLY);
			}
			SetThink(&CPointSpotlight::SpotlightThink);
			SetNextThink(gpGlobals->curtime + 0.1f);
		}
	}

	BaseClass::OnEntityEvent(event, pEventData);
}


void CPointSpotlight::SetParent(CBaseEntity* pNewParent, int iAttachment)
{
	CBaseEntity* pOldParent = GetMoveParent();

	BaseClass::SetParent(pNewParent, iAttachment);

	if (pOldParent != pNewParent)
		PassParentToChildren(pNewParent);
}


//-------------------------------------------------------------------------------------
// Purpose : Send even though we don't have a model so spotlight gets proper position
// Input   :
// Output  :
//-------------------------------------------------------------------------------------
int CPointSpotlight::UpdateTransmitState()
{
	if (m_bEfficientSpotlight)
		return SetTransmitState(FL_EDICT_DONTSEND);

	return SetTransmitState(FL_EDICT_PVSCHECK);
}

//-----------------------------------------------------------------------------
// Purpose: Plays the engine sound.
//-----------------------------------------------------------------------------
void CPointSpotlight::SpotlightThink(void)
{
	if (GetMoveParent())
	{
		SetNextThink(gpGlobals->curtime + TICK_INTERVAL);
	}
	else
	{
		SetNextThink(gpGlobals->curtime + 0.1f);
	}

	SpotlightUpdate();
}

//------------------------------------------------------------------------------
// Purpose :
// Input   :
// Output  :
//------------------------------------------------------------------------------
void CPointSpotlight::SpotlightCreate(void)
{
	if (m_hSpotlightTarget.Get() != NULL)
		return;

	AngleVectors(GetAbsAngles(), &m_vSpotlightDir);

	Vector vTargetPos;
	if (m_bIgnoreSolid)
	{
		vTargetPos = GetAbsOrigin() + m_vSpotlightDir * m_flSpotlightMaxLength;
	}
	else
	{
		trace_t tr;
		UTIL_TraceLine(GetAbsOrigin(), GetAbsOrigin() + m_vSpotlightDir * m_flSpotlightMaxLength, MASK_SOLID_BRUSHONLY, this, COLLISION_GROUP_NONE, &tr);
		vTargetPos = tr.endpos;
	}

	m_hSpotlightTarget = (CSpotlightEnd*)CreateEntityByName("spotlight_end");
	m_hSpotlightTarget->Spawn();
	m_hSpotlightTarget->SetAbsOrigin(vTargetPos);
	m_hSpotlightTarget->SetOwnerEntity(this);
	m_hSpotlightTarget->m_clrRender = m_clrRender;
	m_hSpotlightTarget->m_Radius = m_flSpotlightMaxLength;

	if (FBitSet(m_spawnflags, SF_SPOTLIGHT_NO_DYNAMIC_LIGHT))
	{
		m_hSpotlightTarget->m_flLightScale = 0.0;
	}

	//m_hSpotlight = CBeam::BeamCreate( "sprites/spotlight.vmt", m_flSpotlightGoalWidth );
	m_hSpotlight = CBeam::BeamCreate("sprites/glow_test02.vmt", m_flSpotlightGoalWidth);
	// Set the temporary spawnflag on the beam so it doesn't save (we'll recreate it on restore)
	m_hSpotlight->SetHDRColorScale(m_flHDRColorScale);
	m_hSpotlight->AddSpawnFlags(SF_BEAM_TEMPORARY);
	m_hSpotlight->SetColor(m_clrRender->r, m_clrRender->g, m_clrRender->b);
	m_hSpotlight->SetHaloTexture(m_nHaloSprite);
	m_hSpotlight->SetHaloScale(60);
	m_hSpotlight->SetEndWidth(m_flSpotlightGoalWidth);
	m_hSpotlight->SetBeamFlags((FBEAM_SHADEOUT | FBEAM_NOTILE));
	m_hSpotlight->SetBrightness(64);
	m_hSpotlight->SetNoise(0);
	m_hSpotlight->SetMinDXLevel(m_nMinDXLevel);
	m_hSpotlight->SetAbsOrigin(GetAbsOrigin());

	if (m_bEfficientSpotlight)
	{
		m_hSpotlight->PointsInit(GetAbsOrigin(), m_hSpotlightTarget->GetAbsOrigin());
	}
	else
	{
		m_hSpotlight->EntsInit(m_hSpotlight, m_hSpotlightTarget);
	}

	CBaseEntity* pParent = GetMoveParent();
	if (pParent)
		PassParentToChildren(pParent);
}

//-----------------------------------------------------------------------------
void CPointSpotlight::RecalcRotation(void)
{
	if (!m_isRotating || m_flmaxSpeed == 0.0f)
	{
		m_flRotationSpeed = 0.0f;
		return;
	}

	//
	// Build the axis of rotation based on spawnflags.
	//
	// Pitch Yaw Roll -> Y Z X
	m_nRotationAxis = 1;
	if (HasSpawnFlags(SF_BEAM_SPOTLIGHT_Y_AXIS))
	{
		m_nRotationAxis = 0;
	}
	else if (HasSpawnFlags(SF_BEAM_SPOTLIGHT_X_AXIS))
	{
		m_nRotationAxis = 2;
	}

	m_flRotationSpeed = m_flmaxSpeed;
	if (m_isReversed)
	{
		m_flRotationSpeed *= -1.0f;
	}
}

//------------------------------------------------------------------------------
// Purpose :
// Input   :
// Output  :
//------------------------------------------------------------------------------
Vector CPointSpotlight::SpotlightCurrentPos(void)
{
	AngleVectors(GetAbsAngles(), &m_vSpotlightDir);

	//	Get beam end point.  Only collide with solid objects, not npcs
	Vector vEndPos = GetAbsOrigin() + (m_vSpotlightDir * 2 * m_flSpotlightMaxLength);
	if (m_bIgnoreSolid)
	{
		return vEndPos;
	}
	else
	{
		trace_t tr;
		UTIL_TraceLine(GetAbsOrigin(), vEndPos, MASK_SOLID_BRUSHONLY, this, COLLISION_GROUP_NONE, &tr);
		return tr.endpos;
	}
}

//------------------------------------------------------------------------------
// Purpose :
// Input   :
// Output  :
//------------------------------------------------------------------------------
void CPointSpotlight::SpotlightDestroy(void)
{
	if (m_hSpotlight)
	{
		m_OnOff.FireOutput(this, this);

		UTIL_Remove(m_hSpotlight);
		UTIL_Remove(m_hSpotlightTarget);
	}
}

//------------------------------------------------------------------------------
// Purpose : Update the direction and position of my spotlight
// Input   :
// Output  :
//------------------------------------------------------------------------------
void CPointSpotlight::SpotlightUpdate(void)
{
	float dt = gpGlobals->curtime - m_lastTime;
	if (!m_lastTime)
	{
		dt = 0.0f;
	}
	m_lastTime = gpGlobals->curtime;
	// ---------------------------------------------------
	//  If I don't have a spotlight attempt to create one
	// ---------------------------------------------------
	if (!m_hSpotlight)
	{
		if (m_bSpotlightOn)
		{
			// Make the spotlight
			SpotlightCreate();
		}
		else
		{
			return;
		}
	}
	else if (!m_bSpotlightOn)
	{
		SpotlightDestroy();
		return;
	}

	// update rotation
	if (m_flRotationSpeed != 0.0f)
	{
		QAngle angles = GetAbsAngles();
		angles[m_nRotationAxis] += m_flRotationSpeed * dt;
		angles[m_nRotationAxis] = anglemod(angles[m_nRotationAxis]);
		if (!m_pCache)
		{
			m_pCache = new CSpotlightTraceCacheEntry[NUM_CACHE_ENTRIES];
		}

		SetAbsAngles(angles);
	}

	m_vSpotlightCurrentPos = SpotlightCurrentPos();

	//  Update spotlight target velocity
	Vector vTargetDir;
	VectorSubtract(m_vSpotlightCurrentPos, m_hSpotlightTarget->GetAbsOrigin(), vTargetDir);
	float vTargetDist = vTargetDir.Length();

	// If we haven't moved at all, don't recompute
	if (vTargetDist < 1)
	{
		m_hSpotlightTarget->SetAbsVelocity(vec3_origin);
		return;
	}

	Vector vecNewVelocity = vTargetDir;
	VectorNormalize(vecNewVelocity);
	vecNewVelocity *= (10 * vTargetDist);

	// If a large move is requested, just jump to final spot as we probably hit a discontinuity
	if (vecNewVelocity.Length() > 200)
	{
		VectorNormalize(vecNewVelocity);
		vecNewVelocity *= 200;
		VectorNormalize(vTargetDir);
		m_hSpotlightTarget->SetAbsOrigin(m_vSpotlightCurrentPos);
	}
	m_hSpotlightTarget->SetAbsVelocity(vecNewVelocity);
	m_hSpotlightTarget->m_vSpotlightOrg = GetAbsOrigin();

	// Avoid sudden change in where beam fades out when cross disconinuities
	VectorSubtract(m_hSpotlightTarget->GetAbsOrigin(), m_hSpotlightTarget->m_vSpotlightOrg, m_hSpotlightTarget->m_vSpotlightDir);
	float flBeamLength = VectorNormalize(m_hSpotlightTarget->m_vSpotlightDir);
	m_flSpotlightCurLength = (0.60 * m_flSpotlightCurLength) + (0.4 * flBeamLength);

	ComputeRenderInfo();

	//NDebugOverlay::Cross3D(GetAbsOrigin(),Vector(-5,-5,-5),Vector(5,5,5),0,255,0,true,0.1);
	//NDebugOverlay::Cross3D(m_vSpotlightCurrentPos,Vector(-5,-5,-5),Vector(5,5,5),0,255,0,true,0.1);
	//NDebugOverlay::Cross3D(m_vSpotlightTargetPos,Vector(-5,-5,-5),Vector(5,5,5),255,0,0,true,0.1);

	// Do we need to keep updating?
	if (!GetMoveParent() && m_flRotationSpeed == 0)
	{
		// No reason to think again, we're not going to move unless there's a data change
		SetNextThink(TICK_NEVER_THINK);
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CPointSpotlight::InputLightOn(inputdata_t& inputdata)
{
	if (!m_bSpotlightOn)
	{
		m_bSpotlightOn = true;
		if (m_bEfficientSpotlight)
		{
			CreateEfficientSpotlight();
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CPointSpotlight::InputLightOff(inputdata_t& inputdata)
{
	if (m_bSpotlightOn)
	{
		m_bSpotlightOn = false;
		if (m_bEfficientSpotlight)
		{
			SpotlightDestroy();
		}
	}
}

//-----------------------------------------------------------------------------
void CPointSpotlight::InputStart(inputdata_t& inputdata)
{
	if (!m_isRotating)
	{
		m_isRotating = true;
		RecalcRotation();
	}
}

//-----------------------------------------------------------------------------
void CPointSpotlight::InputStop(inputdata_t& inputdata)
{
	if (m_isRotating)
	{
		m_isRotating = false;
		RecalcRotation();
	}
}

//-----------------------------------------------------------------------------
void CPointSpotlight::InputReverse(inputdata_t& inputdata)
{
	m_isReversed = !m_isReversed;
	RecalcRotation();
}

void CPointSpotlight::PassParentToChildren(CBaseEntity* pParent)
{
	// Since the spotlight itself is server-only, parenting wouldn't look correct on the client
	// Instead, pass the parent entity down to our beams

	if (m_hSpotlight)
	{
		// Ensure we are at the most up-to-date position
		m_hSpotlight->SetAbsOrigin(GetAbsOrigin());

		m_hSpotlight->SetParent(pParent);
		// SetParent can change our solidity state
		m_hSpotlight->SetSolid(SOLID_NONE);
		m_hSpotlight->SetMoveType(MOVETYPE_NONE);

		if (m_hSpotlightTarget)
		{
			m_hSpotlightTarget->SetParent(pParent);
			m_hSpotlightTarget->SetSolid(SOLID_NONE);
			m_hSpotlightTarget->SetMoveType(MOVETYPE_NONE);
		}
	}
}