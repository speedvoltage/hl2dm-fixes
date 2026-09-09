//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Player animations for Half-Life 2: Deathmatch
//
//=============================================================================//

#include "cbase.h"
#include "datacache/imdlcache.h"
#include "base_playeranimstate.h"
#include "hl2mp_playeranimstate.h"

#ifdef CLIENT_DLL
#include "c_hl2mp_player.h"
#else
#include "hl2mp_player.h"
#endif

#define HL2MP_RUN_SPEED				320.0f
#define HL2MP_WALK_SPEED			75.0f
#define HL2MP_CROUCHWALK_SPEED		110.0f

extern ConVar anim_showmainactivity;

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *pPlayer - 
// Output : CMultiPlayerAnimState*
//-----------------------------------------------------------------------------
CHL2MPPlayerAnimState *CreateHL2MPPlayerAnimState( CHL2MP_Player *pPlayer )
{
	MDLCACHE_CRITICAL_SECTION();

	MultiPlayerMovementData_t movementData;

	movementData.m_flBodyYawRate = 720.0f;
	movementData.m_flRunSpeed = HL2MP_RUN_SPEED;
	movementData.m_flWalkSpeed = HL2MP_WALK_SPEED;
	movementData.m_flSprintSpeed = -1.0f;

	CHL2MPPlayerAnimState *pRet = new CHL2MPPlayerAnimState( pPlayer, movementData );

	pRet->InitHL2MP( pPlayer );

	return pRet;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
CHL2MPPlayerAnimState::CHL2MPPlayerAnimState()
{
	m_pHL2MPPlayer = NULL;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *pPlayer - 
//			&movementData - 
//-----------------------------------------------------------------------------
CHL2MPPlayerAnimState::CHL2MPPlayerAnimState( CBasePlayer *pPlayer, MultiPlayerMovementData_t &movementData )
	: CMultiPlayerAnimState( pPlayer, movementData )
{
	m_pHL2MPPlayer = NULL;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
CHL2MPPlayerAnimState::~CHL2MPPlayerAnimState()
{
}

//-----------------------------------------------------------------------------
// Purpose: Initialize HL2MP specific animation state.
// Input  : *pPlayer - 
//-----------------------------------------------------------------------------
void CHL2MPPlayerAnimState::InitHL2MP( CHL2MP_Player *pPlayer )
{
	m_pHL2MPPlayer = pPlayer;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Output : Activity
//-----------------------------------------------------------------------------
Activity CHL2MPPlayerAnimState::CalcMainActivity( void )
{
	Activity idealActivity = ACT_HL2MP_IDLE;

	if ( HandleJumping( idealActivity ) ||
	     HandleDucking( idealActivity ) ||
	     HandleSwimming( idealActivity ) ||
	     HandleMoving( idealActivity ) )
	{
	}

	ShowDebugInfo();

#ifdef CLIENT_DLL
	if ( anim_showmainactivity.GetBool() )
		DebugShowActivity( idealActivity );
#endif

	return idealActivity;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : actDesired - 
// Output : Activity
//-----------------------------------------------------------------------------
Activity CHL2MPPlayerAnimState::TranslateActivity( Activity actDesired )
{
	CHL2MP_Player *pPlayer = GetHL2MPPlayer();

	if ( !pPlayer )
		return actDesired;

	Activity translateActivity = actDesired;

	CBaseCombatWeapon *pWeapon = pPlayer->GetActiveWeapon();

	if ( pWeapon )
		translateActivity = pWeapon->ActivityOverride( translateActivity, false );

	return translateActivity;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *idealActivity - 
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
bool CHL2MPPlayerAnimState::HandleJumping( Activity &idealActivity )
{
	CHL2MP_Player *pPlayer = GetHL2MPPlayer();

	if ( !pPlayer )
		return false;

	if ( m_bJumping )
	{
		if ( m_bFirstJumpFrame )
		{
			m_bFirstJumpFrame = false;

			RestartMainSequence();
		}

		if ( pPlayer->GetWaterLevel() >= WL_Waist )
		{
			m_bJumping = false;

			RestartMainSequence();
		}
		else if ( gpGlobals->curtime - m_flJumpStartTime > 0.2f )
		{
			if ( pPlayer->GetFlags() & FL_ONGROUND )
			{
				m_bJumping = false;

				RestartMainSequence();
			}
		}

		if ( m_bJumping )
			idealActivity = ACT_HL2MP_JUMP;
	}

	return m_bJumping;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *idealActivity - 
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
bool CHL2MPPlayerAnimState::HandleDucking( Activity &idealActivity )
{
	CHL2MP_Player *pPlayer = GetHL2MPPlayer();

	if ( !pPlayer )
		return false;

	bool bDucking = pPlayer->GetFlags() & FL_DUCKING;

	if ( bDucking )
	{
		if ( GetOuterXYSpeed() > MOVING_MINIMUM_SPEED )
			idealActivity = ACT_HL2MP_WALK_CROUCH;
		else
			idealActivity = ACT_HL2MP_IDLE_CROUCH;
	}

	return bDucking;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *idealActivity - 
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
bool CHL2MPPlayerAnimState::HandleSwimming( Activity &idealActivity )
{
	CHL2MP_Player *pPlayer = GetHL2MPPlayer();

	if ( !pPlayer )
		return false;

	m_bInSwim = pPlayer->GetWaterLevel() >= WL_Waist;

	if ( m_bInSwim )
	{
		if ( m_bFirstSwimFrame )
		{
			RestartMainSequence();

			pPlayer->SetCycle( 1.0f );

			m_bFirstSwimFrame = false;
		}

		idealActivity = ACT_HL2MP_JUMP;
	}
	else
	{
		if ( !m_bFirstSwimFrame )
			m_bFirstSwimFrame = true;
	}

	return m_bInSwim;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *idealActivity - 
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
bool CHL2MPPlayerAnimState::HandleMoving( Activity &idealActivity )
{
	CHL2MP_Player *pPlayer = GetHL2MPPlayer();

	if ( !pPlayer )
		return false;

	bool bMoving = GetOuterXYSpeed() > MOVING_MINIMUM_SPEED;

	if ( bMoving )
		idealActivity = ACT_HL2MP_RUN;

	return bMoving;
}
