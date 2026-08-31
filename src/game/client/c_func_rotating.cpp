//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
//===========================================================================//

#include "cbase.h"
#include <KeyValues.h>

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
class C_FuncRotating : public C_BaseEntity
{
public:
	DECLARE_CLASS( C_FuncRotating, C_BaseEntity );
	DECLARE_CLIENTCLASS();
	DECLARE_PREDICTABLE();

	C_FuncRotating();

	bool ShouldPredict( void ) OVERRIDE;
	void OnDataChanged( DataUpdateType_t updateType ) OVERRIDE;
	void PhysicsSimulate( void ) OVERRIDE;

private:
	QAngle m_angPreviousNetworkAngles;
	QAngle m_angPredictedVelocity;
	float m_flPreviousSimulationTime;
};

extern void RecvProxy_SimulationTime( const CRecvProxyData *pData, void *pStruct, void *pOut );

IMPLEMENT_CLIENTCLASS_DT( C_FuncRotating, DT_FuncRotating, CFuncRotating )
	RecvPropVector( RECVINFO_NAME( m_vecNetworkOrigin, m_vecOrigin ) ),
	RecvPropFloat( RECVINFO_NAME( m_angNetworkAngles[0], m_angRotation[0] ) ),
	RecvPropFloat( RECVINFO_NAME( m_angNetworkAngles[1], m_angRotation[1] ) ),
	RecvPropFloat( RECVINFO_NAME( m_angNetworkAngles[2], m_angRotation[2] ) ),
	RecvPropInt( RECVINFO(m_flSimulationTime), 0, RecvProxy_SimulationTime ),
END_RECV_TABLE()

BEGIN_PREDICTION_DATA( C_FuncRotating )
	DEFINE_FIELD( m_angPreviousNetworkAngles, FIELD_VECTOR ),
	DEFINE_FIELD( m_angPredictedVelocity, FIELD_VECTOR ),
	DEFINE_FIELD( m_flPreviousSimulationTime, FIELD_FLOAT ),
END_PREDICTION_DATA()

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
C_FuncRotating::C_FuncRotating()
{
	m_angPreviousNetworkAngles.Init();
	m_angPredictedVelocity.Init();
	m_flPreviousSimulationTime = 0.0f;
	SetPredictionEligible( true );
}

bool C_FuncRotating::ShouldPredict( void )
{
	return C_BasePlayer::GetLocalPlayer() != NULL;
}

void C_FuncRotating::OnDataChanged( DataUpdateType_t updateType )
{
	BaseClass::OnDataChanged( updateType );

	const float flSimulationTime = GetSimulationTime();
	const QAngle &angNetworkAngles = GetNetworkAngles();

	if ( m_flPreviousSimulationTime > 0.0f && flSimulationTime > m_flPreviousSimulationTime )
	{
		const float flDeltaTime = flSimulationTime - m_flPreviousSimulationTime;
		for ( int i = 0; i < 3; ++i )
		{
			m_angPredictedVelocity[i] = AngleDiff( angNetworkAngles[i], m_angPreviousNetworkAngles[i] ) / flDeltaTime;
		}
	}

	m_angPreviousNetworkAngles = angNetworkAngles;
	m_flPreviousSimulationTime = flSimulationTime;
}

void C_FuncRotating::PhysicsSimulate( void )
{
	BaseClass::PhysicsSimulate();

	C_BasePlayer *pPlayer = C_BasePlayer::GetLocalPlayer();
	if ( !pPlayer || !( pPlayer->GetFlags() & FL_ONGROUND ) || pPlayer->GetGroundEntity() != this )
		return;

	if ( m_angPredictedVelocity == vec3_angle )
		return;

	const QAngle angStart = GetAbsAngles();
	const QAngle angEnd = angStart + m_angPredictedVelocity * TICK_INTERVAL;

	matrix3x4_t startToWorld;
	matrix3x4_t endToWorld;
	AngleMatrix( angStart, GetAbsOrigin(), startToWorld );
	AngleMatrix( angEnd, GetAbsOrigin(), endToWorld );

	Vector vecLocal;
	Vector vecEnd;
	VectorITransform( pPlayer->GetAbsOrigin(), startToWorld, vecLocal );
	VectorTransform( vecLocal, endToWorld, vecEnd );

	pPlayer->SetAbsOrigin( vecEnd );
	pPlayer->SetNetworkOrigin( vecEnd );
}
