#include "cbase.h"
#include "cdll_client_int.h"
#include "c_baseplayer.h"
#include "inetchannelinfo.h"
#include "hl2mp_coordinator.h"
#include "hl2mp_party.h"
#include "tier1/keyvaluesjson.h"
#include "tier1/utlbuffer.h"
#include "tier0/icommandline.h"
#include <stdlib.h>
#include "tier0/memdbgon.h"

static ConVar mm_coordinator_url( "mm_coordinator_url", "", FCVAR_ARCHIVE | FCVAR_CLIENTDLL, "HTTPS matchmaking coordinator URL. Local development may use http://127.0.0.1:port." );
static ConVar mm_region( "mm_region", "eu", FCVAR_ARCHIVE | FCVAR_CLIENTDLL, "Matchmaking server region." );

static const char *CoordinatorURL()
{
	if ( CommandLine()->CheckParm( "-mm_local_test" ) )
	{
		const char *url = getenv( "HL2MP_MM_DEV_URL" );
		if ( url && !Q_strncmp( url, "http://127.0.0.1:", 17 ) ) return url;
	}
	return mm_coordinator_url.GetString();
}

static bool SafeToken( const char *text, int minLength, int maxLength )
{
	const int length = Q_strlen( text );
	if ( length < minLength || length > maxLength ) return false;
	for ( const char *p = text; *p; ++p )
		if ( !( ( *p >= 'a' && *p <= 'z' ) || ( *p >= 'A' && *p <= 'Z' ) ||
			( *p >= '0' && *p <= '9' ) || *p == '-' || *p == '_' ) ) return false;
	return true;
}

static bool ValidURL( const char *url )
{
	if ( Q_strlen( url ) > 480 ) return false;
	if ( Q_strncmp( url, "https://", 8 ) && Q_strncmp( url, "http://127.0.0.1:", 17 ) ) return false;
	for ( const char *p = url; *p; ++p )
		if ( !( ( *p >= 'a' && *p <= 'z' ) || ( *p >= 'A' && *p <= 'Z' ) ||
			( *p >= '0' && *p <= '9' ) || *p == '-' || *p == '.' || *p == ':' || *p == '/' || *p == '_' ) ) return false;
	return true;
}

CHL2MPCoordinator &HL2MPCoordinator() { static CHL2MPCoordinator service; return service; }

CHL2MPCoordinator::CHL2MPCoordinator() : m_NextGrant( 0 ), m_PendingAction( ACTION_NONE ), m_Request( 0 ), m_AuthTicket( k_HAuthTicketInvalid ),
	m_AuthRequest( false ), m_PollRequest( false ), m_Confirmed( false ), m_Searching( false ), m_Initialized( false ), m_NextPoll( 0 ), m_AuthDeadline( 0 ),
	m_ReadyExpires( 0 ), m_ReadyPlayers( 0 ), m_ReadyAccepted( 0 ),
	m_PreviewCaptureTime( 0 ), m_PreviewCaptured( false ), m_PreviewGameplay( false )
{
	m_BaseURL[0] = m_Token[0] = m_PendingMode[0] = m_MatchID[0] = m_MatchState[0] = m_ActionMatchID[0] = 0;
	m_Endpoint[0] = m_JoinGrant[0] = m_ConnectedMatch[0] = 0;
	m_ConnectedEndpoint[0] = m_ResultID[0] = m_ResultHeading[0] = m_ResultDetails[0] = 0;
	m_MatchMode[0] = 0;
	m_PartyID[0] = m_PartyLobby[0] = m_PartyLeader[0] = m_UserID[0] = m_PartyInvite[0] = m_ReadyPartyID[0] = 0;
	m_PartyCount = m_LobbyCount = m_PartyRevision = m_ReadyRevision = 0;
	m_WantSession = m_ReadyPending = m_ReadyValue = false;
	m_NextSignIn = 0;
	Q_strncpy( m_PartyStatus, "Create a party to invite friends.", sizeof( m_PartyStatus ) );
	SetStatus( "Choose a mode to find a match." );
}

void CHL2MPCoordinator::SetStatus( const char *text )
{
	Q_strncpy( m_Status, text, sizeof( m_Status ) );
	if ( CommandLine()->CheckParm( "-mm_local_test" ) ) Msg( "Matchmaking preview: %s\n", text );
}

void CHL2MPCoordinator::Init()
{
	if ( m_Initialized ) return;
	m_TicketCallback.Register( this, &CHL2MPCoordinator::OnTicket );
	m_Initialized = true;
}

void CHL2MPCoordinator::ReleaseRequest()
{
	m_Response.Cancel();
	if ( m_Request && steamapicontext && steamapicontext->SteamHTTP() )
		steamapicontext->SteamHTTP()->ReleaseHTTPRequest( m_Request );
	m_Request = 0;
}

void CHL2MPCoordinator::Shutdown()
{
	ReleaseRequest();
	if ( m_AuthTicket != k_HAuthTicketInvalid && steamapicontext && steamapicontext->SteamUser() )
		steamapicontext->SteamUser()->CancelAuthTicket( m_AuthTicket );
	m_AuthTicket = k_HAuthTicketInvalid;
	m_TicketCallback.Unregister();
	m_Token[0] = m_PendingMode[0] = m_MatchID[0] = m_MatchState[0] = 0;
	m_Searching = m_Initialized = false;
	m_Confirmed = false;
	m_PendingAction = ACTION_NONE;
	m_PreviewCaptureTime = 0;
	m_Endpoint[0] = m_JoinGrant[0] = m_ConnectedMatch[0] = 0;
	m_ConnectedEndpoint[0] = m_ResultID[0] = m_ResultHeading[0] = m_ResultDetails[0] = 0;
	m_PartyID[0] = m_PartyLobby[0] = m_PartyInvite[0] = 0;
	m_PartyCount = m_LobbyCount = 0;
	m_WantSession = m_ReadyPending = false;
}

void CHL2MPCoordinator::Tick()
{
	if ( HasServer() && engine->IsInGame() && Plat_FloatTime() >= m_NextGrant )
	{
		INetChannelInfo *channel = engine->GetNetChannelInfo();
		C_BasePlayer *player = C_BasePlayer::GetLocalPlayer();
		if ( channel && player && !Q_strcmp( channel->GetAddress(), m_Endpoint ) &&
			( !m_NextGrant || player->GetTeamNumber() == TEAM_SPECTATOR ) )
		{
			// Private command payload: never put a join grant in userinfo or a console command.
			KeyValues *message = new KeyValues( "HL2MPMatchJoin" );
			message->SetString( "match", m_MatchID );
			message->SetString( "grant", m_JoinGrant );
			engine->ServerCmdKeyValues( message );
			m_NextGrant = Plat_FloatTime() + 2;
		}
		if ( channel && player && !Q_strcmp( channel->GetAddress(), m_Endpoint ) &&
			player->GetTeamNumber() != TEAM_SPECTATOR && !m_PreviewGameplay && CommandLine()->CheckParm( "-mm_local_test" ) )
		{
			m_PreviewGameplay = true;
			m_PreviewCaptureTime = Plat_FloatTime() + 6;
		}
	}
	if ( HasReadyCheck() ) UpdateReadyStatus();
	if ( m_PreviewCaptureTime > 0 && Plat_FloatTime() >= m_PreviewCaptureTime )
	{
		m_PreviewCaptureTime = 0;
		engine->ClientCmd_Unrestricted( m_PreviewGameplay ? "jpeg matchmaking_gameplay 95" : "jpeg matchmaking_ready_check 95" );
	}
	if ( Q_strcmp( m_BaseURL, CoordinatorURL() ) )
	{
		Shutdown();
		Init();
		Q_strncpy( m_BaseURL, CoordinatorURL(), sizeof( m_BaseURL ) );
		m_NextPoll = 0;
		// Local development sessions are process-only, never archived in a config.
		if ( CommandLine()->CheckParm( "-mm_local_test" ) && !Q_strncmp( m_BaseURL, "http://127.0.0.1:", 17 ) )
		{
			const char *token = getenv( "HL2MP_MM_DEV_SESSION" );
			if ( token && SafeToken( token, 32, 120 ) ) Q_strncpy( m_Token, token, sizeof( m_Token ) );
		}
	}
	if ( m_AuthTicket != k_HAuthTicketInvalid && Plat_FloatTime() >= m_AuthDeadline )
	{
		if ( steamapicontext && steamapicontext->SteamUser() ) steamapicontext->SteamUser()->CancelAuthTicket( m_AuthTicket );
		m_AuthTicket = k_HAuthTicketInvalid;
		m_PendingMode[0] = 0;
		SetStatus( "Steam sign-in timed out. Please try again." );
	}
	if ( !m_Request && m_PendingAction != ACTION_NONE ) DispatchAction();
	if ( HL2MPParty().Lobby().IsValid() ) m_WantSession = true;
	if ( m_WantSession && !m_Token[0] && !m_Request && m_AuthTicket == k_HAuthTicketInvalid && Plat_FloatTime() >= m_NextSignIn ) SignIn();
	if ( m_Token[0] && !m_Request && Plat_FloatTime() >= m_NextPoll )
	{
		m_NextPoll = Plat_FloatTime() + 2;
		SyncParty();
		if ( !m_Request ) Request( k_EHTTPMethodGET, "/v1/me" );
	}
}

static bool DecimalID( const char *value )
{
	const int length = Q_strlen( value );
	if ( length < 1 || length > 20 ) return false;
	for ( const char *p = value; *p; ++p ) if ( *p < '0' || *p > '9' ) return false;
	return true;
}

void CHL2MPCoordinator::ReadParty( KeyValues *root )
{
	m_PartyID[0] = m_PartyLobby[0] = m_PartyInvite[0] = 0;
	m_PartyCount = m_LobbyCount = 0;
	KeyValues *party = root->FindKey( "party" );
	if ( !party || !SafeToken( party->GetString( "id" ), 32, 32 ) || !DecimalID( root->GetString( "steam_id" ) ) ) return;
	Q_strncpy( m_UserID, root->GetString( "steam_id" ), sizeof( m_UserID ) );
	Q_strncpy( m_PartyID, party->GetString( "id" ), sizeof( m_PartyID ) );
	Q_strncpy( m_PartyLeader, party->GetString( "leader" ), sizeof( m_PartyLeader ) );
	m_PartyRevision = party->GetInt( "revision" );
	if ( DecimalID( party->GetString( "lobby", "" ) ) ) Q_strncpy( m_PartyLobby, party->GetString( "lobby" ), sizeof( m_PartyLobby ) );
	if ( SafeToken( party->GetString( "invite", "" ), 16, 120 ) ) Q_strncpy( m_PartyInvite, party->GetString( "invite" ), sizeof( m_PartyInvite ) );
	KeyValues *members = party->FindKey( "members" );
	for ( KeyValues *member = members ? members->GetFirstSubKey() : NULL; member && m_PartyCount < 4; member = member->GetNextKey() )
		if ( DecimalID( member->GetString() ) ) Q_strncpy( m_PartyMembers[m_PartyCount++], member->GetString(), sizeof( m_PartyMembers[0] ) );
	members = party->FindKey( "lobby_members" );
	for ( KeyValues *member = members ? members->GetFirstSubKey() : NULL; member && m_LobbyCount < 4; member = member->GetNextKey() )
		if ( DecimalID( member->GetString() ) ) Q_strncpy( m_LobbyMembers[m_LobbyCount++], member->GetString(), sizeof( m_LobbyMembers[0] ) );
	KeyValues *ready = party->FindKey( "ready" );
	for ( int i = 0; i < m_PartyCount; ++i )
	{
		m_MemberReady[i] = false;
		for ( KeyValues *member = ready ? ready->GetFirstSubKey() : NULL; member; member = member->GetNextKey() )
			if ( !Q_strcmp( member->GetString(), m_PartyMembers[i] ) ) m_MemberReady[i] = true;
	}
}

bool CHL2MPCoordinator::LobbyRosterMatches( bool joined ) const
{
	CHL2MPParty &party = HL2MPParty();
	char lobby[24]; Q_snprintf( lobby, sizeof( lobby ), "%llu", (unsigned long long)party.Lobby().ConvertToUint64() );
	const int count = joined ? m_PartyCount : m_LobbyCount;
	if ( !party.Lobby().IsValid() || Q_strcmp( m_PartyLobby, lobby ) || count != party.MemberCount() || !count ) return false;
	for ( int i = 0; i < party.MemberCount(); ++i )
	{
		char id[24]; Q_snprintf( id, sizeof( id ), "%llu", (unsigned long long)party.Member( i ).ConvertToUint64() );
		bool found = false;
		for ( int j = 0; j < count; ++j ) if ( !Q_strcmp( id, joined ? m_PartyMembers[j] : m_LobbyMembers[j] ) ) found = true;
		if ( !found ) return false;
	}
	return true;
}

bool CHL2MPCoordinator::PartySynchronized() const
{
	CHL2MPParty &party = HL2MPParty();
	if ( !m_PartyID[0] || party.Pending() ) return false;
	if ( !party.Lobby().IsValid() ) return !m_PartyLobby[0] && m_PartyCount == 1;
	char owner[24]; Q_snprintf( owner, sizeof( owner ), "%llu", (unsigned long long)party.Owner().ConvertToUint64() );
	return !Q_strcmp( owner, m_PartyLeader ) && LobbyRosterMatches( false ) && LobbyRosterMatches( true );
}

bool CHL2MPCoordinator::PartyMemberReady( CSteamID member ) const
{
	char id[24]; Q_snprintf( id, sizeof( id ), "%llu", (unsigned long long)member.ConvertToUint64() );
	for ( int i = 0; i < m_PartyCount; ++i ) if ( !Q_strcmp( id, m_PartyMembers[i] ) ) return m_MemberReady[i];
	return false;
}

bool CHL2MPCoordinator::PartyReady() const
{
	for ( int i = 0; i < m_PartyCount; ++i ) if ( !Q_strcmp( m_UserID, m_PartyMembers[i] ) ) return m_MemberReady[i];
	return false;
}

void CHL2MPCoordinator::TogglePartyReady()
{
	if ( Busy() || !PartySynchronized() || m_ReadyPending ) return;
	m_ReadyPending = true;
	m_ReadyValue = !PartyReady();
	Q_strncpy( m_ReadyPartyID, m_PartyID, sizeof( m_ReadyPartyID ) );
	m_ReadyRevision = m_PartyRevision;
	m_NextPoll = 0;
}

void CHL2MPCoordinator::SyncParty()
{
	CHL2MPParty &party = HL2MPParty();
	party.SetJoinable( !m_Searching && !m_MatchID[0] );
	if ( !m_PartyID[0] ) return; // Read our authenticated state before changing any party.
	const bool synchronized = PartySynchronized();
	if ( ( m_Searching || HasReadyCheck() ) && !synchronized )
	{
		Cancel();
		Q_strncpy( m_PartyStatus, "Party changed. Cancelling the search...", sizeof( m_PartyStatus ) );
		return;
	}
	if ( m_Searching || m_MatchID[0] ) return;
	if ( m_ReadyPending )
	{
		m_ReadyPending = false;
		if ( synchronized && !Q_strcmp( m_ReadyPartyID, m_PartyID ) && m_ReadyRevision == m_PartyRevision )
		{
			char body[160]; Q_snprintf( body, sizeof( body ), "{\"ready\":%s,\"party_id\":\"%s\",\"revision\":%d}", m_ReadyValue ? "true" : "false", m_PartyID, m_PartyRevision );
			Request( k_EHTTPMethodPOST, "/v1/party/ready", body );
			return;
		}
	}
	if ( party.Pending() ) return;
	if ( !party.Lobby().IsValid() )
	{
		if ( m_PartyLobby[0] || m_PartyCount > 1 ) Request( k_EHTTPMethodPOST, "/v1/party/leave", "{}" );
		return;
	}
	if ( !party.MemberCount() ) return;
	Q_strncpy( m_PartyStatus, "Synchronizing your party...", sizeof( m_PartyStatus ) );
	char lobby[24]; Q_snprintf( lobby, sizeof( lobby ), "%llu", (unsigned long long)party.Lobby().ConvertToUint64() );
	if ( party.IsOwner() )
	{
		if ( Q_strcmp( m_UserID, m_PartyLeader ) || ( m_PartyLobby[0] && Q_strcmp( lobby, m_PartyLobby ) ) || ( !m_PartyLobby[0] && m_PartyCount > 1 ) )
		{
			party.PublishMatchmaking( m_BaseURL, "", "" );
			Request( k_EHTTPMethodPOST, "/v1/party/leave", "{}" );
			return;
		}
		if ( !LobbyRosterMatches( false ) )
		{
			party.PublishMatchmaking( m_BaseURL, "", "" );
			char body[512]; Q_snprintf( body, sizeof( body ), "{\"lobby_id\":\"%s\",\"party_id\":\"%s\",\"revision\":%d,\"members\":[", lobby, m_PartyID, m_PartyRevision );
			for ( int i = 0; i < party.MemberCount(); ++i )
			{
				char entry[32]; Q_snprintf( entry, sizeof( entry ), "%s\"%llu\"", i ? "," : "", (unsigned long long)party.Member( i ).ConvertToUint64() );
				Q_strncat( body, entry, sizeof( body ) );
			}
			Q_strncat( body, "]}", sizeof( body ) );
			Request( k_EHTTPMethodPOST, "/v1/party/lobby", body );
			return;
		}
		party.PublishMatchmaking( m_BaseURL, m_PartyID, m_PartyInvite );
	}
	else
	{
		char owner[24]; Q_snprintf( owner, sizeof( owner ), "%llu", (unsigned long long)party.Owner().ConvertToUint64() );
		const char *invitation = party.MatchmakingData( "mm_invite" );
		const char *target = party.MatchmakingData( "mm_party" );
		if ( Q_strcmp( owner, party.MatchmakingData( "mm_owner" ) ) || !SafeToken( target, 32, 32 ) || !SafeToken( invitation, 16, 120 ) ) return;
		// Lobby data can invite us, but cannot redirect Steam credentials to another service.
		if ( Q_strcmp( m_BaseURL, party.MatchmakingData( "mm_url" ) ) )
		{
			Q_strncpy( m_PartyStatus, "Your party uses a different matchmaking service.", sizeof( m_PartyStatus ) );
			return;
		}
		if ( Q_strcmp( target, m_PartyID ) )
		{
			char body[160]; Q_snprintf( body, sizeof( body ), "{\"invitation\":\"%s\"}", invitation );
			Request( k_EHTTPMethodPOST, "/v1/party/join", body );
			return;
		}
	}
	Q_strncpy( m_PartyStatus, synchronized ? "Mark ready when you want the leader to start matchmaking." : "Waiting for every member to join matchmaking...", sizeof( m_PartyStatus ) );
}

bool CHL2MPCoordinator::Request( EHTTPMethod method, const char *path, const char *body, bool auth )
{
	if ( m_Request ) return false;
	if ( !ValidURL( m_BaseURL ) ) { SetStatus( "Matchmaking is not configured yet. The server browser is available." ); return false; }
	ISteamHTTP *http = steamapicontext ? steamapicontext->SteamHTTP() : NULL;
	if ( !http ) { SetStatus( "Steam networking is unavailable." ); return false; }
	char url[768];
	Q_snprintf( url, sizeof( url ), "%s%s", m_BaseURL, path );
	m_Request = http->CreateHTTPRequest( method, url );
	if ( !m_Request ) { SetStatus( "Could not start the matchmaking request." ); return false; }
	http->SetHTTPRequestAbsoluteTimeoutMS( m_Request, 12000 );
	// Plain HTTP is accepted only for the loopback development endpoint.
	// Steam rejects a plain HTTP request if a verified TLS certificate is required.
	http->SetHTTPRequestRequiresVerifiedCertificate( m_Request, !Q_strncmp( m_BaseURL, "https://", 8 ) );
	bool valid = true;
	if ( !auth && m_Token[0] )
	{
		char header[160];
		Q_snprintf( header, sizeof( header ), "Bearer %s", m_Token );
		valid = http->SetHTTPRequestHeaderValue( m_Request, "Authorization", header );
	}
	if ( body ) valid = valid && http->SetHTTPRequestRawPostBody( m_Request, "application/json", (uint8 *)body, Q_strlen( body ) );
	SteamAPICall_t call;
	if ( !valid || !http->SendHTTPRequest( m_Request, &call ) )
	{
		ReleaseRequest();
		SetStatus( "Could not send the matchmaking request." );
		return false;
	}
	m_AuthRequest = auth;
	m_PollRequest = method == k_EHTTPMethodGET;
	m_Response.Set( call, this, &CHL2MPCoordinator::OnResponse );
	return true;
}

void CHL2MPCoordinator::SignIn()
{
	m_NextSignIn = Plat_FloatTime() + 30;
	if ( !ValidURL( m_BaseURL ) ) { SetStatus( "Matchmaking is not configured yet. The server browser is available." ); m_PendingMode[0] = 0; return; }
	if ( !steamapicontext || !steamapicontext->SteamUser() || !steamapicontext->SteamUser()->BLoggedOn() )
	{
		SetStatus( "Sign in to Steam to use matchmaking." );
		m_PendingMode[0] = 0;
		return;
	}
	m_AuthTicket = steamapicontext->SteamUser()->GetAuthTicketForWebApi( "hl2dm-matchmaking" );
	m_AuthDeadline = Plat_FloatTime() + 20;
	if ( m_AuthTicket == k_HAuthTicketInvalid ) m_PendingMode[0] = 0;
	SetStatus( m_AuthTicket == k_HAuthTicketInvalid ? "Could not request a Steam sign-in ticket." : "Signing in to matchmaking..." );
}

void CHL2MPCoordinator::OnTicket( GetTicketForWebApiResponse_t *result )
{
	if ( result->m_hAuthTicket != m_AuthTicket ) return;
	if ( result->m_eResult != k_EResultOK || result->m_cubTicket <= 0 || result->m_cubTicket > result->k_nCubTicketMaxLength )
	{
		steamapicontext->SteamUser()->CancelAuthTicket( m_AuthTicket );
		m_AuthTicket = k_HAuthTicketInvalid;
		m_PendingMode[0] = 0;
		SetStatus( "Steam could not authenticate this session." );
		return;
	}
	char body[5200];
	Q_strncpy( body, "{\"ticket\":\"", sizeof( body ) );
	char *out = body + Q_strlen( body );
	static const char hex[] = "0123456789abcdef";
	for ( int i = 0; i < result->m_cubTicket; ++i )
	{
		*out++ = hex[result->m_rgubTicket[i] >> 4];
		*out++ = hex[result->m_rgubTicket[i] & 15];
	}
	*out++ = '"'; *out++ = '}'; *out = 0;
	m_AuthDeadline = Plat_FloatTime() + 15;
	Request( k_EHTTPMethodPOST, "/v1/auth/steam", body, true );
}

void CHL2MPCoordinator::Queue( const char *mode )
{
	if ( Q_strcmp( mode, "duel" ) && Q_strcmp( mode, "team" ) && Q_strcmp( mode, "casual" ) ) return;
	if ( Q_strcmp( m_BaseURL, CoordinatorURL() ) ) Tick();
	if ( m_Request && !m_Searching && !m_MatchID[0] && m_AuthTicket == k_HAuthTicketInvalid )
	{
		Q_strncpy( m_PendingMode, mode, sizeof( m_PendingMode ) );
		return;
	}
	if ( Busy() || HL2MPParty().Pending() ) return;
	if ( engine->IsInGame() && !engine->IsLevelMainMenuBackground() ) { SetStatus( "Disconnect from your current server before searching." ); return; }
	m_WantSession = true;
	if ( !m_Token[0] )
	{
		Q_strncpy( m_PendingMode, mode, sizeof( m_PendingMode ) );
		SignIn();
		return;
	}
	if ( !m_PartyID[0] )
	{
		Q_strncpy( m_PendingMode, mode, sizeof( m_PendingMode ) );
		Request( k_EHTTPMethodGET, "/v1/me" );
		return;
	}
	if ( !PartySynchronized() ) { SetStatus( "Wait for your party to finish synchronizing." ); return; }
	if ( Q_strcmp( m_UserID, m_PartyLeader ) ) { SetStatus( "The party leader starts matchmaking. Mark ready to join them." ); return; }
	if ( !SafeToken( mm_region.GetString(), 1, 32 ) ) { SetStatus( "Choose a valid matchmaking region." ); return; }
	char body[256];
	Q_snprintf( body, sizeof( body ), "{\"mode\":\"%s\",\"region\":\"%s\",\"protocol\":1,\"party_id\":\"%s\",\"revision\":%d}", mode, mm_region.GetString(), m_PartyID, m_PartyRevision );
	if ( Request( k_EHTTPMethodPOST, "/v1/queue", body ) ) SetStatus( "Starting matchmaking..." );
}

void CHL2MPCoordinator::Cancel()
{
	if ( !m_Searching && !HasReadyCheck() && !CanLeaveCasual() ) return;
	m_PendingAction = ACTION_CANCEL;
	Q_strncpy( m_ActionMatchID, m_MatchID, sizeof( m_ActionMatchID ) );
	if ( !m_Request ) DispatchAction();
}

void CHL2MPCoordinator::Confirm()
{
	if ( !NeedsConfirmation() || m_PendingAction == ACTION_CANCEL ) return;
	m_PendingAction = ACTION_ACCEPT;
	Q_strncpy( m_ActionMatchID, m_MatchID, sizeof( m_ActionMatchID ) );
	if ( !m_Request ) DispatchAction();
}

void CHL2MPCoordinator::DispatchAction()
{
	const PendingAction action = m_PendingAction;
	m_PendingAction = ACTION_NONE;
	// A click belongs to the match visible when it was made, never a later match.
	if ( m_ActionMatchID[0] && Q_strcmp( m_ActionMatchID, m_MatchID ) ) return;
	if ( action == ACTION_CANCEL && CanLeaveCasual() )
	{
		if ( Request( k_EHTTPMethodPOST, "/v1/casual/leave", "{}" ) )
		{
			INetChannelInfo *channel = engine->GetNetChannelInfo();
			if ( channel && m_Endpoint[0] && !Q_strcmp( channel->GetAddress(), m_Endpoint ) )
				engine->ClientCmd_Unrestricted( "disconnect" );
		}
		return;
	}
	if ( HasReadyCheck() && ( action == ACTION_CANCEL || ( action == ACTION_ACCEPT && !m_Confirmed ) ) )
	{
		char path[128]; Q_snprintf( path, sizeof( path ), "/v1/matches/%s/confirm", m_MatchID );
		Request( k_EHTTPMethodPOST, path, action == ACTION_ACCEPT ? "{\"accept\":true}" : "{\"accept\":false}" );
	}
	else if ( action == ACTION_CANCEL && m_Searching ) Request( k_EHTTPMethodDELETE, "/v1/queue" );
}

void CHL2MPCoordinator::UpdateReadyStatus()
{
	const int seconds = MAX( 0, (int)ceil( m_ReadyExpires - Plat_FloatTime() ) );
	Q_snprintf( m_Status, sizeof( m_Status ), "%s  %d / %d ready.  %ds remaining.",
		m_Confirmed ? "You accepted." : "Match found! Accept to play.", m_ReadyAccepted, m_ReadyPlayers, seconds );
}

void CHL2MPCoordinator::Connect()
{
	if ( !HasServer() ) return;
	char command[96];
	// m_Endpoint is constructed only from a parsed IPv4 address and numeric port.
	Q_snprintf( command, sizeof( command ), "connect %s", m_Endpoint );
	Q_strncpy( m_ConnectedMatch, m_MatchID, sizeof( m_ConnectedMatch ) );
	Q_strncpy( m_ConnectedEndpoint, m_Endpoint, sizeof( m_ConnectedEndpoint ) );
	m_NextGrant = 0;
	engine->ClientCmd_Unrestricted( command );
	SetStatus( "Connecting to your dedicated server..." );
}

void CHL2MPCoordinator::ReadLastMatch( KeyValues *last )
{
	const char *id = last ? last->GetString( "id", "" ) : "";
	if ( !SafeToken( id, 32, 32 ) ) return;
	const char *state = last->GetString( "state", "" );
	const bool finished = !Q_strcmp( state, "finished" );
	const bool cancelled = !Q_strcmp( state, "cancelled" );
	if ( !finished && !cancelled && Q_strcmp( state, "left" ) ) return;
	if ( !Q_strcmp( id, m_ConnectedMatch ) && !m_MatchID[0] )
	{
		INetChannelInfo *channel = engine->GetNetChannelInfo();
		// A late result must never disconnect a player from a different server.
		if ( channel && m_ConnectedEndpoint[0] && !Q_strcmp( channel->GetAddress(), m_ConnectedEndpoint ) )
		{
			engine->ClientCmd_Unrestricted( "disconnect\ngameui_activate" );
			if ( CommandLine()->CheckParm( "-mm_local_test" ) ) Msg( "Matchmaking preview: returning from completed reservation.\n" );
		}
		m_ConnectedMatch[0] = m_ConnectedEndpoint[0] = 0;
	}
	if ( !Q_strcmp( id, m_ResultID ) ) return;
	Q_strncpy( m_ResultID, id, sizeof( m_ResultID ) );
	const char *mode = last->GetString( "mode", "" );
	const bool ranked = !Q_strcmp( mode, "duel" ) || !Q_strcmp( mode, "team" );
	const char *name = !Q_strcmp( mode, "duel" ) ? "DUEL" : !Q_strcmp( mode, "team" ) ? "TEAM DEATHMATCH" : !Q_strcmp( mode, "casual" ) ? "CASUAL" : "MATCH";
	const char *outcome = last->GetString( "outcome", "" );
	const char *title = cancelled ? "CANCELLED" : !finished ? "LEFT MATCH" :
		!Q_strcmp( outcome, "win" ) ? "VICTORY" : !Q_strcmp( outcome, "loss" ) ? "DEFEAT" : !Q_strcmp( outcome, "draw" ) ? "DRAW" : "COMPLETE";
	Q_snprintf( m_ResultHeading, sizeof( m_ResultHeading ), "LAST MATCH  /  %s  /  %s", name, title );
	if ( finished && ranked && last->FindKey( "rating" ) )
		Q_snprintf( m_ResultDetails, sizeof( m_ResultDetails ), "Rating: %d (%+d). Ready for another match.", last->GetInt( "rating" ), last->GetInt( "rating_change" ) );
	else if ( !finished )
		Q_snprintf( m_ResultDetails, sizeof( m_ResultDetails ), "%s. %s", last->GetString( "reason", "The match ended" ), ranked ? "Your rating is unchanged." : "" );
	else Q_strncpy( m_ResultDetails, !Q_strcmp( mode, "casual" ) ? "No ranking. Play another round whenever you like." : "Match complete. Ready for another match.", sizeof( m_ResultDetails ) );
}

void CHL2MPCoordinator::OnResponse( HTTPRequestCompleted_t *result, bool failed )
{
	if ( result->m_hRequest != m_Request ) return;
	ISteamHTTP *http = steamapicontext ? steamapicontext->SteamHTTP() : NULL;
	uint32 size = 0;
	CUtlBuffer data;
	KeyValues *root = NULL;
	if ( !failed && result->m_bRequestSuccessful && http && http->GetHTTPResponseBodySize( m_Request, &size ) && size > 0 && size <= 65536 )
	{
		data.EnsureCapacity( size + 1 );
		if ( http->GetHTTPResponseBodyData( m_Request, (uint8 *)data.Base(), size ) )
		{
			((char *)data.Base())[size] = 0;
			KeyValuesJSONParser parser( (char *)data.Base(), size );
			root = parser.ParseFile();
		}
	}
	const bool auth = m_AuthRequest;
	if ( CommandLine()->CheckParm( "-mm_local_test" ) )
		Msg( "Matchmaking HTTP: status=%d, io=%d, success=%d, bytes=%u, parsed=%d\n",
			(int)result->m_eStatusCode, failed ? 1 : 0, result->m_bRequestSuccessful ? 1 : 0, size, root ? 1 : 0 );
	ReleaseRequest();
	if ( auth && m_AuthTicket != k_HAuthTicketInvalid && steamapicontext && steamapicontext->SteamUser() )
	{
		steamapicontext->SteamUser()->CancelAuthTicket( m_AuthTicket );
		m_AuthTicket = k_HAuthTicketInvalid;
	}
	m_NextPoll = Plat_FloatTime() + 2;
	if ( !root || failed || result->m_eStatusCode != 200 )
	{
		// A rejected mutation may mean our roster revision is stale. Read before retrying.
		m_PartyID[0] = 0;
		SetStatus( root ? root->GetString( "error", "Matchmaking request failed. Please try again." ) : "Matchmaking connection lost. Retrying shortly..." );
		m_NextPoll = Plat_FloatTime() + 5;
		if ( result->m_eStatusCode == 401 ) { m_Token[0] = m_MatchID[0] = m_MatchState[0] = m_PartyID[0] = 0; m_Searching = false; m_PendingAction = ACTION_NONE; }
		m_PendingMode[0] = 0;
		if ( root ) root->deleteThis();
		return;
	}
	if ( auth )
	{
		const char *token = root->GetString( "token", "" );
		if ( !SafeToken( token, 32, 120 ) || root->GetInt( "protocol" ) != 1 )
		{
			m_PendingMode[0] = 0;
			SetStatus( "The matchmaking service returned an incompatible session." );
		}
		else
		{
			Q_strncpy( m_Token, token, sizeof( m_Token ) );
			char mode[16]; Q_strncpy( mode, m_PendingMode, sizeof( mode ) ); m_PendingMode[0] = 0;
			Queue( mode );
		}
	}
	else if ( root->GetInt( "protocol" ) != 1 ) SetStatus( "Please update the mod to use this matchmaking service." );
	else
	{
		ReadParty( root );
		KeyValues *queue = root->FindKey( "queue" );
		m_Searching = queue && queue->GetString( "mode", "" )[0];
		KeyValues *match = root->FindKey( "match" );
		m_MatchID[0] = m_MatchState[0] = 0;
		m_Endpoint[0] = m_JoinGrant[0] = 0;
		Q_strncpy( m_MatchMode, m_Searching ? queue->GetString( "mode", "" ) : "", sizeof( m_MatchMode ) );
		m_Confirmed = false;
		if ( match && SafeToken( match->GetString( "id", "" ), 32, 32 ) )
		{
			Q_strncpy( m_MatchID, match->GetString( "id" ), sizeof( m_MatchID ) );
			Q_strncpy( m_MatchState, match->GetString( "state" ), sizeof( m_MatchState ) );
			Q_strncpy( m_MatchMode, match->GetString( "mode", "" ), sizeof( m_MatchMode ) );
			if ( HasReadyCheck() )
			{
				m_Confirmed = match->GetInt( "you_accepted" ) != 0;
				m_ReadyPlayers = match->GetInt( "players" );
				m_ReadyAccepted = match->GetInt( "accepted" );
				m_ReadyExpires = Plat_FloatTime() + clamp( match->GetInt( "seconds_remaining" ), 0, 30 );
				UpdateReadyStatus();
				if ( CommandLine()->CheckParm( "-mm_local_test" ) && !m_PreviewCaptured )
				{
					m_PreviewCaptureTime = Plat_FloatTime() + 1;
					m_PreviewCaptured = true;
				}
			}
			else if ( !Q_strcmp( m_MatchState, "starting" ) ) SetStatus( "Players ready. Preparing your dedicated server..." );
			else if ( !Q_strcmp( m_MatchState, "active" ) )
			{
				KeyValues *connection = match->FindKey( "connection" );
				const char *address = connection ? connection->GetString( "address", "" ) : "";
				const char *grant = connection ? connection->GetString( "join_token", "" ) : "";
				int octets[4]; char trailing;
				const int port = connection ? connection->GetInt( "port" ) : 0;
				if ( sscanf( address, "%d.%d.%d.%d%c", &octets[0], &octets[1], &octets[2], &octets[3], &trailing ) == 4 &&
					octets[0] >= 0 && octets[0] <= 255 && octets[1] >= 0 && octets[1] <= 255 &&
					octets[2] >= 0 && octets[2] <= 255 && octets[3] >= 0 && octets[3] <= 255 &&
					port > 0 && port <= 65535 && SafeToken( grant, 32, 120 ) )
				{
					Q_snprintf( m_Endpoint, sizeof( m_Endpoint ), "%d.%d.%d.%d:%d", octets[0], octets[1], octets[2], octets[3], port );
					Q_strncpy( m_JoinGrant, grant, sizeof( m_JoinGrant ) );
					SetStatus( "Your match server is ready. Join or reconnect." );
					if ( Q_strcmp( m_ConnectedMatch, m_MatchID ) && !engine->IsConnected() ) Connect();
				}
				else SetStatus( "The service returned an invalid server assignment." );
			}
		}
		else if ( m_Searching ) Q_snprintf( m_Status, sizeof( m_Status ), "Searching for %s in %s...", queue->GetString( "mode" ), queue->GetString( "region" ) );
		else
		{
			SetStatus( "Ready. Choose a mode to find a match." );
		}
		ReadLastMatch( root->FindKey( "last_match" ) );
	}
	root->deleteThis();
	if ( !auth && m_PendingMode[0] && !m_Searching && !m_MatchID[0] )
	{
		char mode[16]; Q_strncpy( mode, m_PendingMode, sizeof( mode ) ); m_PendingMode[0] = 0;
		Queue( mode );
	}
}

CON_COMMAND_F( mm_queue, "Search for a duel, team, or casual match.", FCVAR_CLIENTDLL )
{
	if ( args.ArgC() == 2 ) HL2MPCoordinator().Queue( args[1] );
}
CON_COMMAND_F( mm_queue_cancel, "Cancel a search or decline a ready check.", FCVAR_CLIENTDLL ) { HL2MPCoordinator().Cancel(); }
CON_COMMAND_F( mm_match_accept, "Accept the current ready check.", FCVAR_CLIENTDLL ) { HL2MPCoordinator().Confirm(); }
CON_COMMAND_F( mm_match_connect, "Join or reconnect to your reserved match server.", FCVAR_CLIENTDLL ) { HL2MPCoordinator().Connect(); }
