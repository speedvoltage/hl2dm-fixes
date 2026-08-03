//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Implements the tripmine grenade.
//
// $NoKeywords: $
//=============================================================================//

#include "cbase.h"
#include "beam_shared.h"
#include "shake.h"
#include "grenade_tripmine.h"
#include "vstdlib/random.h"
#include "engine/IEngineSound.h"
#include "explode.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

#define TRIPMINE_BEAM_MAX_LENGTH 32768.0f

extern const char* g_pModelNameLaser;

ConVar    sk_plr_dmg_tripmine		( "sk_plr_dmg_tripmine","0");
ConVar    sk_npc_dmg_tripmine		( "sk_npc_dmg_tripmine","0");
ConVar    sk_tripmine_radius		( "sk_tripmine_radius","0");

LINK_ENTITY_TO_CLASS( npc_tripmine, CTripmineGrenade );

BEGIN_DATADESC( CTripmineGrenade )

	DEFINE_FIELD( m_hOwner,		FIELD_EHANDLE ),
	DEFINE_FIELD( m_flPowerUp,	FIELD_TIME ),
	DEFINE_FIELD( m_vecDir,		FIELD_VECTOR ),
	DEFINE_FIELD( m_vecEnd,		FIELD_POSITION_VECTOR ),
	DEFINE_FIELD( m_flBeamLength, FIELD_FLOAT ),
	DEFINE_FIELD( m_hBeam,		FIELD_EHANDLE ),
	DEFINE_FIELD( m_posOwner,		FIELD_POSITION_VECTOR ),
	DEFINE_FIELD( m_angleOwner,	FIELD_VECTOR ),
	DEFINE_FIELD( m_hAttachedObject, FIELD_EHANDLE ),
	DEFINE_FIELD( m_vecAttachedPosition, FIELD_POSITION_VECTOR ),
	DEFINE_FIELD( m_angAttachedAngles, FIELD_VECTOR ),
	DEFINE_FIELD( m_bAttachedToEntity, FIELD_BOOLEAN ),

	// Function Pointers
	DEFINE_THINKFUNC( WarningThink ),
	DEFINE_THINKFUNC( PowerupThink ),
	DEFINE_THINKFUNC( BeamBreakThink ),
	DEFINE_THINKFUNC( DelayDeathThink ),

END_DATADESC()

CTripmineGrenade::CTripmineGrenade()
{
	m_vecDir.Init();
	m_vecEnd.Init();
	m_flBeamLength = 0.0f;
	m_posOwner.Init();
	m_angleOwner.Init();
	m_hBeam = NULL;
	m_hAttachedObject = NULL;
	m_vecAttachedPosition.Init();
	m_angAttachedAngles.Init();
	m_bAttachedToEntity = false;
}

void CTripmineGrenade::Spawn( void )
{
	Precache( );
	// motor
	SetMoveType( MOVETYPE_FLY );
	SetSolid( SOLID_BBOX );
	SetModel( "models/Weapons/w_slam.mdl" );

	IPhysicsObject *pObject = VPhysicsInitNormal( SOLID_BBOX, GetSolidFlags(), true );
	if ( pObject )
	{
		pObject->EnableMotion( false );
	}
	SetCollisionGroup( COLLISION_GROUP_WEAPON );

	SetCycle( 0.0f );
	m_nBody			= 3;
	m_flDamage		= sk_plr_dmg_tripmine.GetFloat();
	m_DmgRadius		= sk_tripmine_radius.GetFloat();

	ResetSequenceInfo( );
	m_flPlaybackRate	= 0;
	
	UTIL_SetSize(this, Vector( -4, -4, -2), Vector(4, 4, 2));

	m_flPowerUp = gpGlobals->curtime + 2.0;
	
	SetThink( &CTripmineGrenade::PowerupThink );
	SetNextThink( gpGlobals->curtime + 0.2 );

	m_takedamage		= DAMAGE_YES;

	m_iHealth = 1;

	EmitSound( "TripmineGrenade.Place" );
	SetDamage ( 200 );

	// Tripmine sits at 90 on wall so rotate back to get m_vecDir
	QAngle angles = GetAbsAngles();
	angles.x -= 90;

	AngleVectors( angles, &m_vecDir );
	m_vecEnd = GetAbsOrigin() + m_vecDir * TRIPMINE_BEAM_MAX_LENGTH;

	AddEffects( EF_NOSHADOW );
}


void CTripmineGrenade::Precache( void )
{
	PrecacheModel("models/Weapons/w_slam.mdl"); 

	PrecacheScriptSound( "TripmineGrenade.Place" );
	PrecacheScriptSound( "TripmineGrenade.Activate" );
}


void CTripmineGrenade::WarningThink( void  )
{
	// set to power up
	SetThink( &CTripmineGrenade::PowerupThink );
	SetNextThink( gpGlobals->curtime + 1.0f );
}


void CTripmineGrenade::PowerupThink( void  )
{
	CBaseEntity *pAttachedObject = m_hAttachedObject.Get();
	if ( m_bAttachedToEntity &&
		( !pAttachedObject ||
		!VectorsAreEqual( m_vecAttachedPosition, pAttachedObject->GetAbsOrigin(), 1.0f ) ||
		!QAnglesAreEqual( m_angAttachedAngles, pAttachedObject->GetAbsAngles(), 1.0f ) ) )
	{
		m_iHealth = 0;
		Event_Killed( CTakeDamageInfo( (CBaseEntity *)m_hOwner, this, 100, GIB_NORMAL ) );
		return;
	}

	if (gpGlobals->curtime > m_flPowerUp)
	{
		MakeBeam( );
		RemoveSolidFlags( FSOLID_NOT_SOLID );
		m_bIsLive			= true;

		// play enabled sound
		EmitSound( "TripmineGrenade.Activate" );
		return;
	}
	SetNextThink( gpGlobals->curtime + 0.1f );
}


void CTripmineGrenade::KillBeam( void )
{
	if ( m_hBeam )
	{
		UTIL_Remove( m_hBeam );
		m_hBeam = NULL;
	}
}


void CTripmineGrenade::MakeBeam( void )
{
	trace_t tr;

	UTIL_TraceLine( GetAbsOrigin(), m_vecEnd, MASK_SHOT, this, COLLISION_GROUP_NONE, &tr );

	// If I hit a living thing, send the beam through me so it turns on briefly
	// and then blows the living thing up
	CBaseEntity *pEntity = tr.m_pEnt;
	CBaseCombatCharacter *pBCC  = ToBaseCombatCharacter( pEntity );

	if (pBCC)
	{
		SetOwnerEntity( pBCC );
		UTIL_TraceLine( GetAbsOrigin(), m_vecEnd, MASK_SHOT, this, COLLISION_GROUP_NONE, &tr );
		SetOwnerEntity( NULL );
	}

	m_flBeamLength = tr.fraction;

	// set to follow laser spot
	SetThink( &CTripmineGrenade::BeamBreakThink );

	// Delay first think slightly so beam has time
	// to appear if person right in front of it
	SetNextThink( gpGlobals->curtime + 1.0f );

	m_hBeam = CBeam::BeamCreate( g_pModelNameLaser, 0.35 );
	if ( !m_hBeam )
	{
		return;
	}

	m_hBeam->PointEntInit( tr.endpos, this );
	m_hBeam->SetColor( 255, 55, 52 );
	m_hBeam->SetScrollRate( 25.6 );
	m_hBeam->SetBrightness( 64 );
	
	int beamAttach = LookupAttachment("beam_attach");
	m_hBeam->SetEndAttachment( beamAttach );
}

void CTripmineGrenade::AttachToEntity( CBaseEntity *pEntity )
{
	m_hAttachedObject = pEntity;
	m_bAttachedToEntity = pEntity != NULL;
	if ( pEntity )
	{
		m_vecAttachedPosition = pEntity->GetAbsOrigin();
		m_angAttachedAngles = pEntity->GetAbsAngles();
	}
}


void CTripmineGrenade::BeamBreakThink( void  )
{
	CBaseEntity *pAttachedObject = m_hAttachedObject.Get();
	if ( m_bAttachedToEntity &&
		( !pAttachedObject ||
		!VectorsAreEqual( m_vecAttachedPosition, pAttachedObject->GetAbsOrigin(), 1.0f ) ||
		!QAnglesAreEqual( m_angAttachedAngles, pAttachedObject->GetAbsAngles(), 1.0f ) ) )
	{
		m_iHealth = 0;
		Event_Killed( CTakeDamageInfo( (CBaseEntity *)m_hOwner, this, 100, GIB_NORMAL ) );
		return;
	}

	// See if I can go solid yet (has dropper moved out of way?)
	if (IsSolidFlagSet( FSOLID_NOT_SOLID ))
	{
		trace_t tr;
		Vector	vUpBit = GetAbsOrigin();
		vUpBit.z += 5.0;

		UTIL_TraceEntity( this, GetAbsOrigin(), vUpBit, MASK_SHOT, &tr );
		if ( !tr.startsolid && (tr.fraction == 1.0) )
		{
			RemoveSolidFlags( FSOLID_NOT_SOLID );
		}
	}

	trace_t tr;

	// NOT MASK_SHOT because we want only simple hit boxes
	UTIL_TraceLine( GetAbsOrigin(), m_vecEnd, MASK_SOLID, this, COLLISION_GROUP_NONE, &tr );

	// ALERT( at_console, "%f : %f\n", tr.flFraction, m_flBeamLength );

	// respawn detect. 
	if ( !m_hBeam )
	{
		MakeBeam( );
		return;
	}


	CBaseEntity *pEntity = tr.m_pEnt;
	CBaseCombatCharacter *pBCC  = ToBaseCombatCharacter( pEntity );

	if (pBCC || fabs( m_flBeamLength - tr.fraction ) > 0.001)
	{
		m_iHealth = 0;
		Event_Killed( CTakeDamageInfo( (CBaseEntity*)m_hOwner, this, 100, GIB_NORMAL ) );
		return;
	}

	SetNextThink( gpGlobals->curtime + 0.05f );
}

#if 0 // FIXME: OnTakeDamage_Alive() is no longer called now that base grenade derives from CBaseAnimating
int CTripmineGrenade::OnTakeDamage_Alive( const CTakeDamageInfo &info )
{
	if (gpGlobals->curtime < m_flPowerUp && info.GetDamage() < m_iHealth)
	{
		// disable
		// Create( "weapon_tripmine", GetLocalOrigin() + m_vecDir * 24, GetAngles() );
		SetThink( &CTripmineGrenade::SUB_Remove );
		SetNextThink( gpGlobals->curtime + 0.1f );
		KillBeam();
		return FALSE;
	}
	return BaseClass::OnTakeDamage_Alive( info );
}
#endif

//-----------------------------------------------------------------------------
// Purpose:
// Input  :
// Output :
//-----------------------------------------------------------------------------
void CTripmineGrenade::Event_Killed( const CTakeDamageInfo &info )
{
	m_takedamage		= DAMAGE_NO;

	SetThink( &CTripmineGrenade::DelayDeathThink );
	SetNextThink( gpGlobals->curtime + 0.25 );

	EmitSound( "TripmineGrenade.StopSound" );
}


void CTripmineGrenade::DelayDeathThink( void )
{
	KillBeam();
	trace_t tr;
	UTIL_TraceLine ( GetAbsOrigin() + m_vecDir * 8, GetAbsOrigin() - m_vecDir * 64,  MASK_SOLID, this, COLLISION_GROUP_NONE, & tr);
	UTIL_ScreenShake( GetAbsOrigin(), 25.0, 150.0, 1.0, 750, SHAKE_START );

	ExplosionCreate( GetAbsOrigin() + m_vecDir * 8, GetAbsAngles(), m_hOwner, GetDamage(), 200, 
		SF_ENVEXPLOSION_NOSPARKS | SF_ENVEXPLOSION_NODLIGHTS | SF_ENVEXPLOSION_NOSMOKE, 0.0f, this);

	UTIL_Remove( this );
}
