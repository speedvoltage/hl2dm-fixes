//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Player animations for Half-Life 2: Deathmatch
//
//=============================================================================//

#ifndef HL2MP_PLAYERANIMSTATE_H
#define HL2MP_PLAYERANIMSTATE_H
#pragma once

#include "multiplayer_animstate.h"

#ifdef CLIENT_DLL
class C_HL2MP_Player;
#define CHL2MP_Player C_HL2MP_Player
#else
class CHL2MP_Player;
#endif

class CHL2MPPlayerAnimState : public CMultiPlayerAnimState
{
public:
	DECLARE_CLASS( CHL2MPPlayerAnimState, CMultiPlayerAnimState )

	CHL2MPPlayerAnimState();
	CHL2MPPlayerAnimState( CBasePlayer *pPlayer, MultiPlayerMovementData_t &movementData );
	~CHL2MPPlayerAnimState();

	CHL2MP_Player *GetHL2MPPlayer( void ) { return m_pHL2MPPlayer; }

	void InitHL2MP( CHL2MP_Player *pPlayer );

	virtual void Update( float eyeYaw, float eyePitch );

	virtual Activity CalcMainActivity( void );
	virtual Activity TranslateActivity( Activity actDesired );

	bool HandleJumping( Activity &idealActivity );
	bool HandleDucking( Activity &idealActivity );
	bool HandleSwimming( Activity &idealActivity );
	bool HandleMoving( Activity &idealActivity );

private:
	bool SetupPoseParameters( CStudioHdr *pStudioHdr );

	virtual void EstimateYaw( void );

	virtual void ComputePoseParam_MoveYaw( CStudioHdr *pStudioHdr );
	virtual void ComputePoseParam_AimPitch( CStudioHdr *pStudioHdr );
	virtual void ComputePoseParam_AimYaw( CStudioHdr *pStudioHdr );;

	void ComputePlaybackRate( void );

	CHL2MP_Player *m_pHL2MPPlayer;
};

CHL2MPPlayerAnimState *CreateHL2MPPlayerAnimState( CHL2MP_Player *pPlayer );

#endif // HL2MP_PLAYERANIMSTATE_H
