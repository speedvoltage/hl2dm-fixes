//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Pistol weapon (header)
// 
//=============================================================================//
#pragma once

#include "weapon_hl2mpbasehlmpcombatweapon.h"

// Tunables / constants used by both client & server
#define PISTOL_FASTEST_REFIRE_TIME             0.1f
#define PISTOL_FASTEST_DRY_REFIRE_TIME         0.2f

// Applied amount of time each shot adds to the time we must recover from
#define PISTOL_ACCURACY_SHOT_PENALTY_TIME      0.2f
// Maximum penalty to deal out
#define PISTOL_ACCURACY_MAXIMUM_PENALTY_TIME   1.5f

#ifdef CLIENT_DLL
#define CWeaponPistol C_WeaponPistol
#endif

//-----------------------------------------------------------------------------
// CWeaponPistol
//-----------------------------------------------------------------------------
class CWeaponPistol : public CBaseHL2MPCombatWeapon
{
public:
	DECLARE_CLASS( CWeaponPistol, CBaseHL2MPCombatWeapon );

	CWeaponPistol( void );

	DECLARE_NETWORKCLASS();
	DECLARE_PREDICTABLE();

	// Standard overrides
	void	Precache( void );
	void	ItemPostFrame( void );
	void	ItemPreFrame( void );
	void	ItemBusyFrame( void );
	void	PrimaryAttack( void );
	void	AddViewKick( void );
	void	DryFire( void );
	float	GetSoonestPrimaryAttack() const;
	void	SetSoonestPrimaryAttack( float flTime );

	void	UpdatePenaltyTime( void );

	Activity	GetPrimaryAttackActivity( void );

	virtual bool Reload( void );

	// Spread, burst and fire rate are small inline helpers; leave inline for perf.
	virtual const Vector &GetBulletSpread( void )
	{
		static Vector cone;

		float ramp = RemapValClamped( m_flAccuracyPenalty,
			0.0f,
			PISTOL_ACCURACY_MAXIMUM_PENALTY_TIME,
			0.0f,
			1.0f );

		// Lerp from very accurate to inaccurate over time
		VectorLerp( VECTOR_CONE_1DEGREES, VECTOR_CONE_6DEGREES, ramp, cone );
		return cone;
	}

	virtual int		GetMinBurst() { return 1; }
	virtual int		GetMaxBurst() { return 3; }
	virtual float	GetFireRate( void ) { return 0.5f; }

#ifndef CLIENT_DLL
	DECLARE_ACTTABLE();
#endif

private:
	// Networking
	CNetworkVar( float, m_flSoonestPrimaryAttack );
	CNetworkVar( float, m_flLastAttackTime );
	CNetworkVar( float, m_flAccuracyPenalty );
	CNetworkVar( int, m_nNumShotsFired );

private:
	// Non-copyable
	CWeaponPistol( const CWeaponPistol & );
};
