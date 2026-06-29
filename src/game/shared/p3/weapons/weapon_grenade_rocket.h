//========= Copyright © 1996-2005, Valve Corporation, All rights reserved. ============//
//
// Purpose: Rockets (Weapon)
//
//=============================================================================//

#ifndef WEAPON_GRENADE_ROCKET_H
#define WEAPON_GRENADE_ROCKET_H
#ifdef _WIN32
#pragma once
#endif

#if defined( CLIENT_DLL )
// Client Only
	#define CWeaponGrenadeRocket C_WeaponGrenadeRocket
#endif

class CWeaponGrenadeRocket: public CBaseAnimating
{
	DECLARE_CLASS( CWeaponGrenadeRocket, CBaseAnimating );

public:

	DECLARE_NETWORKCLASS();
	DECLARE_DATADESC();
	DECLARE_PREDICTABLE();

	CWeaponGrenadeRocket();
	~CWeaponGrenadeRocket();

	void	Spawn( void );
	void	SetRealOwner( CBasePlayer *pOwner ) { m_hOwner = pOwner; }
	float	GetDamage( void ) { return m_flDamage; }
	void	SetDamage( float flDamage ) { m_flDamage = flDamage; }
	int		GetDamageType() const { return DMG_BLAST; }
	float	GetDamageRadius( void ) { return m_flRadius; }
	void	SetDamageRadius( float flRadius ) { m_flRadius = flRadius; }
	void	SetMaxRange( float flRange );
	void	RocketTouch( CBaseEntity *pOther );

	void	SetTankMode() { m_bTankMode = true; }

#if !defined( CLIENT_DLL )
// Server Only
	void	Precache( void );

	void	SetAnglesToMatchVelocity( void );	
	void	ApplyForcesToVehicle( CBaseEntity *pEntity );

	static CWeaponGrenadeRocket *Create( const Vector &vecOrigin, const Vector &vecAngles, float flMaxRange, CBaseEntity *pRealOwner );

#else
// Client Only
	void	OnDataChanged( DataUpdateType_t updateType );	
#endif

private:

	CWeaponGrenadeRocket( const CWeaponGrenadeRocket& );

private:
	float			m_flDamage;
	float			m_flRadius;
	float			m_flMaxRange;
	float			m_flFallingSpeed;
	float			m_flExceedRangeTime;
	EHANDLE			m_hOwner;
	bool			m_bTankMode;
};

#endif // WEAPON_GRENADE_ROCKET_H
