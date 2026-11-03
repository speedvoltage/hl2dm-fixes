#ifndef HL2MP_COORDINATOR_H
#define HL2MP_COORDINATOR_H
#ifdef _WIN32
#pragma once
#endif
#include "steam/steam_api.h"
#include "hl2mp_matchmaking_modes.h"

class CHL2MPCoordinator
{
public:
	CHL2MPCoordinator();
	void Init();
	void Shutdown();
	void Tick();
	void Queue( const char *mode );
	void CustomMatch( const char *map, int teamSize, int timeLimit, int fragLimit );
	void Cancel();
	void Confirm();
	void Connect();
	void TogglePartyReady();
	bool PartySynchronized() const;
	bool PartyMemberReady( CSteamID member ) const;
	bool PartyReady() const;
	const char *PartyStatus() const { return m_Token[0] ? m_PartyStatus : m_Status; }
	bool HasServer() const { return m_Endpoint[0] && !Q_strcmp( m_MatchState, "active" ); }
	bool CanLeaveCasual() const { return m_MatchID[0] && !HasReadyCheck() && HL2MPMatchTeamSize( m_MatchMode ) == 0; }
	const char *CustomSummary() const { return m_CustomSummary; }
	const char *Status() const { return m_Status; }
	const char *Mode() const { return m_PendingMode[0] ? m_PendingMode : ( m_Searching || m_MatchID[0] ) ? m_MatchMode : ""; }
	const char *ResultHeading() const { return m_ResultHeading; }
	const char *ResultDetails() const { return m_ResultDetails; }
	bool Searching() const { return m_Searching; }
	bool HasReadyCheck() const { return !Q_strcmp( m_MatchState, "ready_check" ); }
	bool NeedsConfirmation() const { return HasReadyCheck() && !m_Confirmed; }
	bool Busy() const { return m_Searching || m_MatchID[0] || m_PendingMode[0] || m_ReadyPending ||
		( m_Request && !m_PollRequest ) || m_AuthTicket != k_HAuthTicketInvalid; }
private:
	enum PendingAction { ACTION_NONE, ACTION_CANCEL, ACTION_ACCEPT };
	void DispatchAction();
	void UpdateReadyStatus();
	void SignIn();
	void OnTicket( GetTicketForWebApiResponse_t *result );
	void OnResponse( HTTPRequestCompleted_t *result, bool failed );
	bool Request( EHTTPMethod method, const char *path, const char *body = NULL, bool auth = false );
	void SetStatus( const char *text );
	void ReleaseRequest();
	void ReadParty( class KeyValues *root );
	void ReadLastMatch( class KeyValues *last );
	void SyncParty();
	bool LobbyRosterMatches( bool joined ) const;
	char m_PartyID[40];
	char m_PartyLobby[24];
	char m_PartyLeader[24];
	char m_UserID[24];
	char m_PartyInvite[128];
	char m_PartyMembers[HL2MP_MAX_PARTY_MEMBERS][24];
	char m_LobbyMembers[HL2MP_MAX_PARTY_MEMBERS][24];
	bool m_MemberReady[HL2MP_MAX_PARTY_MEMBERS];
	int m_PartyCount;
	int m_LobbyCount;
	int m_PartyRevision;
	char m_PartyStatus[2048];
	bool m_WantSession;
	double m_NextSignIn;
	bool m_ReadyPending;
	bool m_ReadyValue;
	char m_ReadyPartyID[40];
	int m_ReadyRevision;
	char m_BaseURL[512];
	char m_Token[128];
	char m_Status[2048];
	char m_PendingMode[16];
	char m_CustomMap[64];
	char m_CustomSummary[2048];
	int m_CustomTeamSize, m_CustomTimeLimit, m_CustomFragLimit;
	char m_MatchID[40];
	char m_MatchState[32];
	char m_MatchMode[16];
	char m_ActionMatchID[40];
	char m_Endpoint[64];
	char m_JoinGrant[128];
	char m_ConnectedMatch[40];
	char m_ConnectedEndpoint[64];
	char m_ResultID[40];
	char m_ResultHeading[2048];
	char m_ResultDetails[2048];
	double m_NextGrant;
	PendingAction m_PendingAction;
	HTTPRequestHandle m_Request;
	HAuthTicket m_AuthTicket;
	bool m_AuthRequest;
	bool m_PollRequest;
	bool m_Confirmed;
	bool m_Searching;
	bool m_Initialized;
	double m_NextPoll;
	double m_AuthDeadline;
	double m_ReadyExpires;
	int m_ReadyPlayers;
	int m_ReadyAccepted;
	double m_PreviewCaptureTime;
	bool m_PreviewCaptured;
	bool m_PreviewGameplay;
	CCallResult<CHL2MPCoordinator, HTTPRequestCompleted_t> m_Response;
	CCallbackManual<CHL2MPCoordinator, GetTicketForWebApiResponse_t> m_TicketCallback;
};
CHL2MPCoordinator &HL2MPCoordinator();
#endif
