//========= Copyright Valve Corporation, All rights reserved. ============//
//
// EnvBeam: declaration moved out of EnvBeam.cpp
//
//=============================================================================//
#ifndef ENVBEAM_H
#define ENVBEAM_H
#ifdef _WIN32
#pragma once
#endif

#include "beam_shared.h"
#include "filters.h"

// Touch filter enum used by env_beam
enum Touch_t
{
	touch_none = 0,
	touch_player_only,
	touch_npc_only,
	touch_player_or_npc,
	touch_player_or_npc_or_physicsprop,
};

class CEnvBeam : public CBeam
{
public:
	DECLARE_CLASS( CEnvBeam, CBeam );
	DECLARE_DATADESC();

	// CBaseEntity
	void	Spawn( void );
	void	Precache( void );
	void	Activate( void );

	// Thinks
	void	StrikeThink( void );
	void	UpdateThink( void );

	// Behavior
	void	RandomArea( void );
	void	RandomPoint( const Vector &vecSrc );
	void	Zap( const Vector &vecSrc, const Vector &vecDest );
	void	Strike( void );

	// Pause comp (as in your .cpp)
	void	ApplyPauseComp( float dt );

	// Touch filter gate
	bool	PassesTouchFilters( CBaseEntity *pOther );

	// Inputs
	void	InputTurnOn( inputdata_t &inputdata );
	void	InputTurnOff( inputdata_t &inputdata );
	void	InputToggle( inputdata_t &inputdata );
	void	InputStrikeOnce( inputdata_t &inputdata );

	// On/Off helpers
	void	TurnOn( void );
	void	TurnOff( void );
	void	Toggle( void );

	const char *GetDecalName( void ) { return STRING( m_iszDecal ); }

	inline bool ServerSide( void )
	{
		if ( m_life == 0 && !HasSpawnFlags( SF_BEAM_RING ) )
			return true;

		return false;
	}

	void	BeamUpdateVars( void );

	// Key/Member data (unchanged)
	int			m_active;
	int			m_spriteTexture;

	string_t	m_iszStartEntity;
	string_t	m_iszEndEntity;
	float		m_life;
	float		m_boltWidth;
	float		m_noiseAmplitude;
	int			m_speed;
	float		m_restrike;
	string_t	m_iszSpriteName;
	int			m_frameStart;

	float		m_radius;

	Touch_t		m_TouchType;
	string_t	m_iFilterName;
	EHANDLE		m_hFilter;

	string_t	m_iszDecal;

	COutputEvent	m_OnTouchedByEntity;
};

// If you reference this helper from elsewhere, keep its prototype visible.
void ApplyPauseCompensation_EnvBeam( float dt );

#endif // ENVBEAM_H
