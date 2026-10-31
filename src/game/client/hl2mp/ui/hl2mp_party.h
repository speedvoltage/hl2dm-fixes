#ifndef HL2MP_PARTY_H
#define HL2MP_PARTY_H
#ifdef _WIN32
#pragma once
#endif

#include "steam/steam_api.h"

// Steam owns membership; the coordinator will own queues, reservations and ratings.
// Never treat lobby metadata as an authenticated match assignment.
class CHL2MPParty
{
public:
	CHL2MPParty();
	void Init();
	void Shutdown();
	void Create();
	void Join( CSteamID lobby );
	void Leave();
	void Invite();
	void Tick();
	bool Available() const;
	bool Pending() const { return m_bPending; }
	bool IsOwner() const;
	CSteamID Lobby() const { return m_Lobby; }
	int MemberCount() const;
	const char *MemberName( int index ) const;
	const char *Status() const { return m_Status; }

private:
	void OnCreated( LobbyCreated_t *result, bool failed );
	void OnEntered( LobbyEnter_t *result, bool failed );
	void OnInvite( GameLobbyJoinRequested_t *result );
	void OnData( LobbyDataUpdate_t *result );
	bool Compatible() const;
	void SetStatus( const char *text );
	CSteamID m_Lobby;
	bool m_bPending;
	bool m_bCancelPending;
	bool m_bAwaitingMetadata;
	bool m_bInitialized;
	double m_flMetadataDeadline;
	char m_Status[256];
	CCallResult<CHL2MPParty, LobbyCreated_t> m_CreateResult;
	CCallResult<CHL2MPParty, LobbyEnter_t> m_JoinResult;
	CCallbackManual<CHL2MPParty, GameLobbyJoinRequested_t> m_InviteCallback;
	CCallbackManual<CHL2MPParty, LobbyDataUpdate_t> m_DataCallback;
};

CHL2MPParty &HL2MPParty();
#endif
