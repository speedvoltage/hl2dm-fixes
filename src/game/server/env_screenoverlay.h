#ifndef ENV_SCREENOVERLAY_H
#define ENV_SCREENOVERLAY_H

#ifdef _WIN32
#pragma once
#endif

#include "cbase.h"
#include "shareddefs.h"

class CEnvScreenOverlay : public CPointEntity
{
	DECLARE_CLASS( CEnvScreenOverlay, CPointEntity );
public:
	DECLARE_DATADESC();
	DECLARE_SERVERCLASS();

	CEnvScreenOverlay();

	virtual int UpdateTransmitState();
	virtual int ShouldTransmit( const CCheckTransmitInfo *pInfo );
	virtual void Spawn();
	virtual void Precache();

	void	InputStartOverlay( inputdata_t &inputdata );
	void	InputStopOverlay( inputdata_t &inputdata );
	void	InputSwitchOverlay( inputdata_t &inputdata );

	void	SetActive( bool bActive ) { m_bIsActive = bActive; }

	CBaseEntity *GetTargetPlayer() const;

protected:
	CNetworkArray( string_t, m_iszOverlayNames, MAX_SCREEN_OVERLAYS );
	CNetworkArray( float, m_flOverlayTimes, MAX_SCREEN_OVERLAYS );
	CNetworkVar( float, m_flStartTime );
	CNetworkVar( int, m_iDesiredOverlay );
	CNetworkVar( bool, m_bIsActive );
	EHANDLE m_hTargetPlayer;
};

#endif // ENV_SCREENOVERLAY_H
