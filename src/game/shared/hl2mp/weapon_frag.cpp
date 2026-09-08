//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
//=============================================================================//

#include "cbase.h"
#include "npcevent.h"
#include "in_buttons.h"
#include "animation.h"
#include "baseviewmodel_shared.h"
#include "datacache/imdlcache.h"

#ifdef CLIENT_DLL
	#include "c_hl2mp_player.h"
	#include "c_te_effect_dispatch.h"
#else
	#include "hl2mp_player.h"
	#include "te_effect_dispatch.h"
	#include "grenade_frag.h"
#endif

#include "weapon_ar2.h"
#include "effect_dispatch_data.h"
#include "weapon_hl2mpbasehlmpcombatweapon.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

#define GRENADE_TIMER	2.5f //Seconds
#define RETHROW_DELAY	0.5f

#define GRENADE_PAUSED_NO			0
#define GRENADE_PAUSED_PRIMARY		1
#define GRENADE_PAUSED_SECONDARY	2

#define GRENADE_RADIUS	4.0f // inches

#define GRENADE_DAMAGE_RADIUS 250.0f

#ifdef CLIENT_DLL
#define CWeaponFrag C_WeaponFrag
#endif

//-----------------------------------------------------------------------------
// Fragmentation grenades
//-----------------------------------------------------------------------------
class CWeaponFrag: public CBaseHL2MPCombatWeapon
{
	DECLARE_CLASS( CWeaponFrag, CBaseHL2MPCombatWeapon );
public:

	DECLARE_NETWORKCLASS(); 
	DECLARE_PREDICTABLE();

	CWeaponFrag();

	void	Precache( void );
	void	PrimaryAttack( void );
	void	SecondaryAttack( void );
	void	DecrementAmmo( CBaseCombatCharacter *pOwner );
	void	ItemPostFrame( void );
	void	SendViewModelAnim( int nSequence );

	bool	Deploy( void );
	bool	Holster( CBaseCombatWeapon *pSwitchingTo = NULL );
	
	bool	Reload( void );

#ifndef CLIENT_DLL
	void Operator_HandleAnimEvent( animevent_t *pEvent, CBaseCombatCharacter *pOperator );
#endif

	void	ThrowGrenade( CBasePlayer *pPlayer );
	bool	IsPrimed( bool ) { return ( m_AttackPaused != 0 );	}
	
private:

	void	RollGrenade( CBasePlayer *pPlayer );
	void	LobGrenade( CBasePlayer *pPlayer );
	// check a throw from vecSrc.  If not valid, move the position back along the line to vecEye
	void	CheckThrowPosition( CBasePlayer *pPlayer, const Vector &vecEye, Vector &vecSrc, const QAngle &angles = vec3_angle );

	CNetworkVar( bool,	m_bRedraw );	//Draw the weapon again after throwing a grenade
	
	CNetworkVar( int,	m_AttackPaused );
	CNetworkVar( bool,	m_fDrawbackFinished );
	CNetworkVar( float,	m_flAnimStartTime );

	CWeaponFrag( const CWeaponFrag & );

#ifndef CLIENT_DLL
	DECLARE_ACTTABLE();
#endif
};

#ifndef CLIENT_DLL

acttable_t	CWeaponFrag::m_acttable[] = 
{
	{ ACT_HL2MP_IDLE,					ACT_HL2MP_IDLE_GRENADE,					false },
	{ ACT_HL2MP_RUN,					ACT_HL2MP_RUN_GRENADE,					false },
	{ ACT_HL2MP_IDLE_CROUCH,			ACT_HL2MP_IDLE_CROUCH_GRENADE,			false },
	{ ACT_HL2MP_WALK_CROUCH,			ACT_HL2MP_WALK_CROUCH_GRENADE,			false },
	{ ACT_HL2MP_GESTURE_RANGE_ATTACK,	ACT_HL2MP_GESTURE_RANGE_ATTACK_GRENADE,	false },
	{ ACT_HL2MP_GESTURE_RELOAD,			ACT_HL2MP_GESTURE_RELOAD_GRENADE,		false },
	{ ACT_HL2MP_JUMP,					ACT_HL2MP_JUMP_GRENADE,					false },
};

IMPLEMENT_ACTTABLE(CWeaponFrag);

#endif

IMPLEMENT_NETWORKCLASS_ALIASED( WeaponFrag, DT_WeaponFrag )

BEGIN_NETWORK_TABLE( CWeaponFrag, DT_WeaponFrag )

#ifdef CLIENT_DLL
	RecvPropBool( RECVINFO( m_bRedraw ) ),
	RecvPropBool( RECVINFO( m_fDrawbackFinished ) ),
	RecvPropInt( RECVINFO( m_AttackPaused ) ),
	RecvPropFloat( RECVINFO( m_flAnimStartTime ) ),
#else
	SendPropBool( SENDINFO( m_bRedraw ) ),
	SendPropBool( SENDINFO( m_fDrawbackFinished ) ),
	SendPropInt( SENDINFO( m_AttackPaused ) ),
	SendPropFloat( SENDINFO( m_flAnimStartTime ), 0, SPROP_NOSCALE ),
#endif
	
END_NETWORK_TABLE()

#ifdef CLIENT_DLL
BEGIN_PREDICTION_DATA( CWeaponFrag )
	DEFINE_PRED_FIELD( m_bRedraw, FIELD_BOOLEAN, FTYPEDESC_INSENDTABLE ),
	DEFINE_PRED_FIELD( m_fDrawbackFinished, FIELD_BOOLEAN, FTYPEDESC_INSENDTABLE ),
	DEFINE_PRED_FIELD( m_AttackPaused, FIELD_INTEGER, FTYPEDESC_INSENDTABLE ),
	DEFINE_PRED_FIELD_TOL( m_flAnimStartTime, FIELD_FLOAT, FTYPEDESC_INSENDTABLE, TD_MSECTOLERANCE ),
END_PREDICTION_DATA()
#endif

LINK_ENTITY_TO_CLASS( weapon_frag, CWeaponFrag );
PRECACHE_WEAPON_REGISTER(weapon_frag);

CWeaponFrag::CWeaponFrag( void ) :
	CBaseHL2MPCombatWeapon()
{
	m_bRedraw = false;
	m_fDrawbackFinished = false;
	m_AttackPaused = GRENADE_PAUSED_NO;
	m_flAnimStartTime = 0.0f;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CWeaponFrag::Precache( void )
{
	BaseClass::Precache();

#ifndef CLIENT_DLL
	UTIL_PrecacheOther( "npc_grenade_frag" );
#endif

	PrecacheScriptSound( "WeaponFrag.Throw" );
	PrecacheScriptSound( "WeaponFrag.Roll" );
}

#ifndef CLIENT_DLL
//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *pEvent - 
//			*pOperator - 
//-----------------------------------------------------------------------------
void CWeaponFrag::Operator_HandleAnimEvent( animevent_t *pEvent, CBaseCombatCharacter *pOperator )
{
	switch( pEvent->event )
	{
		case EVENT_WEAPON_SEQUENCE_FINISHED:
		case EVENT_WEAPON_THROW:
		case EVENT_WEAPON_THROW2:
		case EVENT_WEAPON_THROW3:
			return;

		default:
			BaseClass::Operator_HandleAnimEvent( pEvent, pOperator );
			break;
	}
}

#endif

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
bool CWeaponFrag::Deploy( void )
{
	m_bRedraw = false;
	m_fDrawbackFinished = false;

	return BaseClass::Deploy();
}

//-----------------------------------------------------------------------------
// Purpose: 
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
bool CWeaponFrag::Holster( CBaseCombatWeapon *pSwitchingTo )
{
	m_bRedraw = false;
	m_fDrawbackFinished = false;

	return BaseClass::Holster( pSwitchingTo );
}

//-----------------------------------------------------------------------------
// Purpose: 
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
bool CWeaponFrag::Reload( void )
{
	if ( !HasPrimaryAmmo() )
		return false;

	if ( ( m_bRedraw ) && ( m_flNextPrimaryAttack <= gpGlobals->curtime ) && ( m_flNextSecondaryAttack <= gpGlobals->curtime ) )
	{
		//Redraw the weapon
		SendWeaponAnim( ACT_VM_DRAW );

		//Update our times
		m_flNextPrimaryAttack	= gpGlobals->curtime + SequenceDuration();
		m_flNextSecondaryAttack	= gpGlobals->curtime + SequenceDuration();
		m_flTimeWeaponIdle = gpGlobals->curtime + SequenceDuration();

		//Mark this as done
		m_bRedraw = false;
	}

	return true;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CWeaponFrag::SecondaryAttack( void )
{
	if ( m_bRedraw )
		return;

	if ( !HasPrimaryAmmo() )
		return;

	CBaseCombatCharacter *pOwner  = GetOwner();

	if ( pOwner == NULL )
		return;

	CBasePlayer *pPlayer = ToBasePlayer( pOwner );
	
	if ( pPlayer == NULL )
		return;

	// Note that this is a secondary attack and prepare the grenade attack to pause.
	m_AttackPaused = GRENADE_PAUSED_SECONDARY;
	SendWeaponAnim( ACT_VM_PULLBACK_LOW );

	// Don't let weapon idle interfere in the middle of a throw!
	m_flTimeWeaponIdle = FLT_MAX;
	m_flNextSecondaryAttack	= FLT_MAX;

	// If I'm now out of ammo, switch away
	if ( !HasPrimaryAmmo() )
	{
		pPlayer->SwitchToNextBestWeapon( this );
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CWeaponFrag::PrimaryAttack( void )
{
	if ( m_bRedraw )
		return;

	CBaseCombatCharacter *pOwner  = GetOwner();
	
	if ( pOwner == NULL )
	{ 
		return;
	}

	CBasePlayer *pPlayer = ToBasePlayer( GetOwner() );;

	if ( !pPlayer )
		return;

	// Note that this is a primary attack and prepare the grenade attack to pause.
	m_AttackPaused = GRENADE_PAUSED_PRIMARY;
	SendWeaponAnim( ACT_VM_PULLBACK_HIGH );
	
	// Put both of these off indefinitely. We do not know how long
	// the player will hold the grenade.
	m_flTimeWeaponIdle = FLT_MAX;
	m_flNextPrimaryAttack = FLT_MAX;

	// If I'm now out of ammo, switch away
	if ( !HasPrimaryAmmo() )
	{
		pPlayer->SwitchToNextBestWeapon( this );
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *pOwner - 
//-----------------------------------------------------------------------------
void CWeaponFrag::DecrementAmmo( CBaseCombatCharacter *pOwner )
{
	pOwner->RemoveAmmo( 1, m_iPrimaryAmmoType );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CWeaponFrag::SendViewModelAnim( int nSequence )
{
	BaseClass::SendViewModelAnim( nSequence );

	CBasePlayer *pOwner = ToBasePlayer( GetOwner() );
	CBaseViewModel *pViewModel = pOwner ? pOwner->GetViewModel( m_nViewModelIndex ) : NULL;
	if ( nSequence >= 0 && pViewModel && pViewModel->GetOwningWeapon() == this && pViewModel->GetSequence() == nSequence )
		m_flAnimStartTime = gpGlobals->curtime;
}

void CWeaponFrag::ItemPostFrame( void )
{
	CBasePlayer *pPlayer = ToBasePlayer( GetOwner() );
	if ( !pPlayer )
		return;

#ifdef CLIENT_DLL
	if ( pPlayer != CBasePlayer::GetLocalPlayer() )
		return;
#endif

	bool bSequenceFinished = false;
	CBaseViewModel *pViewModel = pPlayer->GetViewModel( m_nViewModelIndex );
	if ( pViewModel && pViewModel->GetOwningWeapon() == this )
	{
		MDLCACHE_CRITICAL_SECTION();
		CStudioHdr *pStudioHdr = pViewModel->GetModelPtr();
		int nSequence = pViewModel->GetSequence();
		if ( pStudioHdr && nSequence >= 0 && nSequence < pStudioHdr->GetNumSeq() )
		{
			float flCycleRate = pViewModel->GetSequenceCycleRate( pStudioHdr, nSequence ) * pViewModel->GetPlaybackRate();
			float flCycle = ( gpGlobals->curtime - m_flAnimStartTime ) * flCycleRate;
			mstudioseqdesc_t &seqdesc = pStudioHdr->pSeqdesc( nSequence );
			bSequenceFinished = flCycle >= 1.0f ||
				( !( seqdesc.flags & STUDIO_LOOPING ) && flCycle > 1.0f - seqdesc.fadeouttime * flCycleRate );
		}
	}

	if( m_fDrawbackFinished )
	{
		CBasePlayer *pOwner = ToBasePlayer( GetOwner() );

		if (pOwner)
		{
			switch( m_AttackPaused )
			{
			case GRENADE_PAUSED_PRIMARY:
				if( !(pOwner->m_nButtons & IN_ATTACK) )
				{
					SendWeaponAnim( ACT_VM_THROW );
					m_fDrawbackFinished = false;
				}
				break;

			case GRENADE_PAUSED_SECONDARY:
				if( !(pOwner->m_nButtons & IN_ATTACK2) )
				{
					//See if we're ducking
					if ( pOwner->m_nButtons & IN_DUCK )
					{
						//Send the weapon animation
						SendWeaponAnim( ACT_VM_SECONDARYATTACK );
					}
					else
					{
						//Send the weapon animation
						SendWeaponAnim( ACT_VM_HAULBACK );
					}

					m_fDrawbackFinished = false;
				}
				break;

			default:
				break;
			}
		}
	}

	BaseClass::ItemPostFrame();

	if ( m_bRedraw && bSequenceFinished )
		Reload();

	if ( pViewModel && pViewModel->GetOwningWeapon() == this && gpGlobals->frametime > 0.0f )
	{
		MDLCACHE_CRITICAL_SECTION();
		CStudioHdr *pStudioHdr = pViewModel->GetModelPtr();
		int nSequence = pViewModel->GetSequence();
		if ( pStudioHdr && nSequence >= 0 && nSequence < pStudioHdr->GetNumSeq() )
		{
			float flCycleRate = pViewModel->GetSequenceCycleRate( pStudioHdr, nSequence ) * pViewModel->GetPlaybackRate();
			int nTick = TIME_TO_TICKS( gpGlobals->curtime );
			float flStartCycle = ( TICKS_TO_TIME( nTick ) - m_flAnimStartTime ) * flCycleRate;
			float flEndCycle = ( TICKS_TO_TIME( nTick + 1 ) - m_flAnimStartTime ) * flCycleRate;
			mstudioseqdesc_t &seqdesc = pStudioHdr->pSeqdesc( nSequence );
			if ( !( seqdesc.flags & STUDIO_LOOPING ) )
			{
				float flLastVisibleCycle = 1.0f - seqdesc.fadeouttime * flCycleRate;
				if ( flStartCycle > 0.0f && ( flStartCycle >= 1.0f || flStartCycle > flLastVisibleCycle ) )
					flStartCycle = 1.01f;
				if ( flEndCycle >= 1.0f || flEndCycle > flLastVisibleCycle )
					flEndCycle = 1.01f;
			}
			animevent_t event;
			int nEvent = 0;
			while ( ( nEvent = GetAnimationEvent( pStudioHdr, nSequence, &event, flStartCycle, flEndCycle, nEvent ) ) != 0 )
			{
				switch ( event.event )
				{
				case EVENT_WEAPON_SEQUENCE_FINISHED:
					m_fDrawbackFinished = true;
					continue;

				case EVENT_WEAPON_THROW:
					ThrowGrenade( pPlayer );
					break;

				case EVENT_WEAPON_THROW2:
					RollGrenade( pPlayer );
					break;

				case EVENT_WEAPON_THROW3:
					LobGrenade( pPlayer );
					break;

				default:
					continue;
				}

				DecrementAmmo( pPlayer );
				m_flNextPrimaryAttack = gpGlobals->curtime + RETHROW_DELAY;
				m_flNextSecondaryAttack = gpGlobals->curtime + RETHROW_DELAY;
				m_flTimeWeaponIdle = FLT_MAX;
			}
		}
	}
}

	// check a throw from vecSrc.  If not valid, move the position back along the line to vecEye
void CWeaponFrag::CheckThrowPosition( CBasePlayer *pPlayer, const Vector &vecEye, Vector &vecSrc, const QAngle &angles )
{
	matrix3x4_t rotation;
	AngleMatrix( angles, rotation );

	Vector vecMins;
	Vector vecMaxs;
	RotateAABB( rotation, Vector( -GRENADE_RADIUS - 2, -GRENADE_RADIUS - 2, -2 ),
		Vector( GRENADE_RADIUS + 2, GRENADE_RADIUS + 2, GRENADE_RADIUS * 2 + 2 ), vecMins, vecMaxs );

	trace_t tr;

	UTIL_TraceHull( vecEye, vecSrc, vecMins, vecMaxs,
		pPlayer->PhysicsSolidMaskForEntity(), pPlayer, pPlayer->GetCollisionGroup(), &tr );
	
	if ( tr.DidHit() )
	{
		vecSrc = tr.endpos;
	}
}

void DropPrimedFragGrenade( CHL2MP_Player *pPlayer, CBaseCombatWeapon *pGrenade )
{
	CWeaponFrag *pWeaponFrag = dynamic_cast<CWeaponFrag*>( pGrenade );

	if ( pWeaponFrag )
	{
		pWeaponFrag->ThrowGrenade( pPlayer );
		pWeaponFrag->DecrementAmmo( pPlayer );
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *pPlayer - 
//-----------------------------------------------------------------------------
void CWeaponFrag::ThrowGrenade( CBasePlayer *pPlayer )
{
#ifndef CLIENT_DLL
	Vector	vecEye = pPlayer->EyePosition();
	Vector	vForward, vRight;

	pPlayer->EyeVectors( &vForward, &vRight, NULL );
	Vector vecSrc = vecEye + vForward * 18.0f + vRight * 8.0f;
	CheckThrowPosition( pPlayer, vecEye, vecSrc );
//	vForward[0] += 0.1f;
	vForward[2] += 0.1f;

	Vector vecThrow;
	pPlayer->GetVelocity( &vecThrow, NULL );
	vecThrow += vForward * 1200;
	CBaseGrenade *pGrenade = Fraggrenade_Create( vecSrc, vec3_angle, vecThrow, AngularImpulse(600,random->RandomInt(-1200,1200),0), pPlayer, GRENADE_TIMER, false );

	if ( pGrenade )
	{
		if ( pPlayer && pPlayer->m_lifeState != LIFE_ALIVE )
		{
			pPlayer->GetVelocity( &vecThrow, NULL );

			IPhysicsObject *pPhysicsObject = pGrenade->VPhysicsGetObject();
			if ( pPhysicsObject )
			{
				pPhysicsObject->SetVelocity( &vecThrow, NULL );
			}
		}
		
		pGrenade->SetDamage( GetHL2MPWpnData().m_iPlayerDamage );
		pGrenade->SetDamageRadius( GRENADE_DAMAGE_RADIUS );
	}
#endif

	m_bRedraw = true;

	WeaponSound( SINGLE );
	
	// player "shoot" animation
	pPlayer->SetAnimation( PLAYER_ATTACK1 );

#ifdef GAME_DLL
	pPlayer->OnMyWeaponFired( this );
#endif
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *pPlayer - 
//-----------------------------------------------------------------------------
void CWeaponFrag::LobGrenade( CBasePlayer *pPlayer )
{
#ifndef CLIENT_DLL
	Vector	vecEye = pPlayer->EyePosition();
	Vector	vForward, vRight;

	pPlayer->EyeVectors( &vForward, &vRight, NULL );
	Vector vecSrc = vecEye + vForward * 18.0f + vRight * 8.0f + Vector( 0, 0, -8 );
	CheckThrowPosition( pPlayer, vecEye, vecSrc );
	
	Vector vecThrow;
	pPlayer->GetVelocity( &vecThrow, NULL );
	vecThrow += vForward * 350 + Vector( 0, 0, 50 );
	CBaseGrenade *pGrenade = Fraggrenade_Create( vecSrc, vec3_angle, vecThrow, AngularImpulse(200,random->RandomInt(-600,600),0), pPlayer, GRENADE_TIMER, false );

	if ( pGrenade )
	{
		pGrenade->SetDamage( GetHL2MPWpnData().m_iPlayerDamage );
		pGrenade->SetDamageRadius( GRENADE_DAMAGE_RADIUS );
	}
#endif

	WeaponSound( WPN_DOUBLE );

	// player "shoot" animation
	pPlayer->SetAnimation( PLAYER_ATTACK1 );

	m_bRedraw = true;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *pPlayer - 
//-----------------------------------------------------------------------------
void CWeaponFrag::RollGrenade( CBasePlayer *pPlayer )
{
#ifndef CLIENT_DLL
	// BUGBUG: Hardcoded grenade width of 4 - better not change the model :)
	Vector vecSrc;
	pPlayer->CollisionProp()->NormalizedToWorldSpace( Vector( 0.5f, 0.5f, 0.0f ), &vecSrc );
	vecSrc.z += GRENADE_RADIUS;

	Vector vecFacing = pPlayer->BodyDirection2D( );
	// no up/down direction
	vecFacing.z = 0;
	VectorNormalize( vecFacing );
	trace_t tr;
	UTIL_TraceLine( vecSrc, vecSrc - Vector(0,0,16), MASK_PLAYERSOLID, pPlayer, COLLISION_GROUP_NONE, &tr );
	if ( tr.fraction != 1.0 )
	{
		// compute forward vec parallel to floor plane and roll grenade along that
		Vector tangent;
		CrossProduct( vecFacing, tr.plane.normal, tangent );
		CrossProduct( tr.plane.normal, tangent, vecFacing );
	}
	vecSrc += (vecFacing * 18.0);

	Vector vecThrow;
	pPlayer->GetVelocity( &vecThrow, NULL );
	vecThrow += vecFacing * 700;
	// put it on its side
	QAngle orientation(0,pPlayer->GetLocalAngles().y,-90);
	// roll it
	AngularImpulse rotSpeed(0,0,720);
	CheckThrowPosition( pPlayer, pPlayer->WorldSpaceCenter(), vecSrc, orientation );
	CBaseGrenade *pGrenade = Fraggrenade_Create( vecSrc, orientation, vecThrow, rotSpeed, pPlayer, GRENADE_TIMER, false );

	if ( pGrenade )
	{
		pGrenade->SetDamage( GetHL2MPWpnData().m_iPlayerDamage );
		pGrenade->SetDamageRadius( GRENADE_DAMAGE_RADIUS );
	}

#endif

	WeaponSound( SPECIAL1 );

	// player "shoot" animation
	pPlayer->SetAnimation( PLAYER_ATTACK1 );

	m_bRedraw = true;
}
