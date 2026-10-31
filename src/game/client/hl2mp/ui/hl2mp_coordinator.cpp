#include "cbase.h"
#include "cdll_client_int.h"
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

CHL2MPCoordinator::CHL2MPCoordinator() : m_PendingAction( ACTION_NONE ), m_Request( 0 ), m_AuthTicket( k_HAuthTicketInvalid ),
	m_AuthRequest( false ), m_PollRequest( false ), m_Confirmed( false ), m_Searching( false ), m_Initialized( false ), m_NextPoll( 0 ), m_AuthDeadline( 0 ),
	m_ReadyExpires( 0 ), m_ReadyPlayers( 0 ), m_ReadyAccepted( 0 ),
	m_PreviewCaptureTime( 0 ), m_PreviewCaptured( false )
{
	m_BaseURL[0] = m_Token[0] = m_PendingMode[0] = m_MatchID[0] = m_MatchState[0] = m_ActionMatchID[0] = 0;
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
}

void CHL2MPCoordinator::Tick()
{
	if ( HasReadyCheck() ) UpdateReadyStatus();
	if ( m_PreviewCaptureTime > 0 && Plat_FloatTime() >= m_PreviewCaptureTime )
	{
		m_PreviewCaptureTime = 0;
		engine->ClientCmd_Unrestricted( "jpeg matchmaking_ready_check 95" );
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
	if ( m_Token[0] && !m_Request && Plat_FloatTime() >= m_NextPoll )
	{
		m_NextPoll = Plat_FloatTime() + 2;
		Request( k_EHTTPMethodGET, "/v1/me" );
	}
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
	if ( Busy() ) return;
	if ( engine->IsInGame() && !engine->IsLevelMainMenuBackground() ) { SetStatus( "Disconnect from your current server before searching." ); return; }
	if ( HL2MPParty().MemberCount() > 1 ) { SetStatus( "Steam party synchronization is not connected yet. Keep your party together using the server browser." ); return; }
	if ( !m_Token[0] )
	{
		Q_strncpy( m_PendingMode, mode, sizeof( m_PendingMode ) );
		SignIn();
		return;
	}
	if ( !SafeToken( mm_region.GetString(), 1, 32 ) ) { SetStatus( "Choose a valid matchmaking region." ); return; }
	char body[160];
	Q_snprintf( body, sizeof( body ), "{\"mode\":\"%s\",\"region\":\"%s\",\"protocol\":1}", mode, mm_region.GetString() );
	if ( Request( k_EHTTPMethodPOST, "/v1/queue", body ) ) SetStatus( "Starting matchmaking..." );
}

void CHL2MPCoordinator::Cancel()
{
	if ( !m_Searching && !HasReadyCheck() ) return;
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
		SetStatus( root ? root->GetString( "error", "Matchmaking request failed. Please try again." ) : "Matchmaking connection lost. Retrying shortly..." );
		m_NextPoll = Plat_FloatTime() + 5;
		if ( result->m_eStatusCode == 401 ) { m_Token[0] = m_MatchID[0] = m_MatchState[0] = 0; m_Searching = false; m_PendingAction = ACTION_NONE; }
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
		KeyValues *queue = root->FindKey( "queue" );
		m_Searching = queue && queue->GetString( "mode", "" )[0];
		KeyValues *match = root->FindKey( "match" );
		m_MatchID[0] = m_MatchState[0] = 0;
		m_Confirmed = false;
		if ( match && SafeToken( match->GetString( "id", "" ), 32, 32 ) )
		{
			Q_strncpy( m_MatchID, match->GetString( "id" ), sizeof( m_MatchID ) );
			Q_strncpy( m_MatchState, match->GetString( "state" ), sizeof( m_MatchState ) );
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
			else SetStatus( "Server reservation ready. Game-server handoff is not connected yet." );
		}
		else if ( m_Searching ) Q_snprintf( m_Status, sizeof( m_Status ), "Searching for %s in %s...", queue->GetString( "mode" ), queue->GetString( "region" ) );
		else
		{
			KeyValues *last = root->FindKey( "last_match" );
			SetStatus( last && last->GetString( "reason", "" )[0] ? last->GetString( "reason" ) : "Ready. Choose a mode to find a match." );
		}
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
