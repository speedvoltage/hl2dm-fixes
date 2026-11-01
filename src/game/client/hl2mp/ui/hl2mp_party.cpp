#include "cbase.h"
#include "cdll_client_int.h"
#include "hl2mp_party.h"
#include "hl2mp_coordinator.h"
#include "tier0/platform.h"
#include "tier0/memdbgon.h"

static const char *s_ModKey = "hl2dm_matchmaking";
static const char *s_Protocol = "1";
static const int s_MaxParty = HL2MP_MAX_PARTY_MEMBERS;

CHL2MPParty &HL2MPParty()
{
	static CHL2MPParty party;
	return party;
}

CHL2MPParty::CHL2MPParty() : m_bPending( false ), m_bCancelPending( false ),
	m_bAwaitingMetadata( false ), m_bInitialized( false ), m_flMetadataDeadline( 0 )
{
	SetStatus( "Play solo or create a party to invite friends." );
}

void CHL2MPParty::SetStatus( const char *text ) { Q_strncpy( m_Status, text, sizeof( m_Status ) ); }

bool CHL2MPParty::Available() const
{
	return steamapicontext && steamapicontext->SteamMatchmaking() &&
		steamapicontext->SteamFriends() && steamapicontext->SteamUser() &&
		steamapicontext->SteamUser()->BLoggedOn();
}

void CHL2MPParty::Init()
{
	if ( m_bInitialized ) return;
	m_InviteCallback.Register( this, &CHL2MPParty::OnInvite );
	m_DataCallback.Register( this, &CHL2MPParty::OnData );
	m_bInitialized = true;
}

void CHL2MPParty::Shutdown()
{
	Leave( true );
	m_CreateResult.Cancel();
	m_JoinResult.Cancel();
	m_InviteCallback.Unregister();
	m_DataCallback.Unregister();
	m_bPending = false;
	m_bInitialized = false;
}

void CHL2MPParty::Create()
{
	if ( HL2MPCoordinator().Busy() ) { SetStatus( "Finish or cancel matchmaking before creating a party." ); return; }
	if ( m_bPending || m_Lobby.IsValid() ) return;
	if ( !Available() ) { SetStatus( "Sign in to Steam to create a party." ); return; }
	SteamAPICall_t call = steamapicontext->SteamMatchmaking()->CreateLobby( k_ELobbyTypePrivate, s_MaxParty );
	if ( call == k_uAPICallInvalid ) { SetStatus( "Steam could not create a party. Try again." ); return; }
	m_bPending = true;
	m_bCancelPending = false;
	SetStatus( "Creating party..." );
	m_CreateResult.Set( call, this, &CHL2MPParty::OnCreated );
}

void CHL2MPParty::OnCreated( LobbyCreated_t *result, bool failed )
{
	m_bPending = false;
	if ( failed || result->m_eResult != k_EResultOK || !Available() )
	{
		m_PendingInvite.Clear();
		Warning( "Steam party creation: IO failure=%d, result=%d, online=%d\n", failed ? 1 : 0,
			failed ? -1 : (int)result->m_eResult, Available() ? 1 : 0 );
		SetStatus( !failed && result->m_eResult == k_EResultAccessDenied ?
			"Steam parties are not available for this app yet. You can still use the server browser." :
			"Party creation failed. Check your Steam connection and try again." );
		return;
	}
	m_Lobby = CSteamID( result->m_ulSteamIDLobby );
	if ( m_bCancelPending ) { Leave( true ); return; }
	ISteamMatchmaking *mm = steamapicontext->SteamMatchmaking();
	if ( !mm->SetLobbyData( m_Lobby, "mod", s_ModKey ) ||
		 !mm->SetLobbyData( m_Lobby, "protocol", s_Protocol ) )
	{
		Leave( true );
		SetStatus( "Could not initialize the party. Please try again." );
		return;
	}
	SetStatus( "Party ready. Invite friends using Steam." );
	if ( m_PendingInvite.IsValid() )
	{
		CSteamID friendID = m_PendingInvite; m_PendingInvite.Clear(); InviteFriend( friendID );
	}
}

void CHL2MPParty::Join( CSteamID lobby )
{
	if ( HL2MPCoordinator().Busy() ) { SetStatus( "Finish or cancel matchmaking before joining another party." ); return; }
	if ( m_bPending || lobby == m_Lobby ) return;
	if ( !Available() ) { SetStatus( "Sign in to Steam to join a party." ); return; }
	if ( !lobby.IsValid() || !lobby.IsLobby() ) { SetStatus( "Invalid party invite." ); return; }
	// Joining from the overlay is an explicit user action. Do not interrupt a match.
	if ( engine->IsInGame() && !engine->IsLevelMainMenuBackground() )
	{
		SetStatus( "Disconnect from your current server before joining another party." );
		return;
	}
	Leave();
	SteamAPICall_t call = steamapicontext->SteamMatchmaking()->JoinLobby( lobby );
	if ( call == k_uAPICallInvalid ) { SetStatus( "Steam could not join this party." ); return; }
	m_bPending = true;
	m_bCancelPending = false;
	SetStatus( "Joining party..." );
	m_JoinResult.Set( call, this, &CHL2MPParty::OnEntered );
}

bool CHL2MPParty::Compatible() const
{
	if ( !Available() || !m_Lobby.IsValid() ) return false;
	ISteamMatchmaking *mm = steamapicontext->SteamMatchmaking();
	return !Q_strcmp( mm->GetLobbyData( m_Lobby, "mod" ), s_ModKey ) &&
		!Q_strcmp( mm->GetLobbyData( m_Lobby, "protocol" ), s_Protocol );
}

void CHL2MPParty::OnEntered( LobbyEnter_t *result, bool failed )
{
	m_bPending = false;
	if ( failed || result->m_EChatRoomEnterResponse != k_EChatRoomEnterResponseSuccess || !Available() )
	{
		Warning( "Steam party join: IO failure=%d, response=%u, online=%d\n", failed ? 1 : 0,
			failed ? 0 : result->m_EChatRoomEnterResponse, Available() ? 1 : 0 );
		SetStatus( "Unable to join. The party may be full, closed, or no longer available." );
		return;
	}
	m_Lobby = CSteamID( result->m_ulSteamIDLobby );
	if ( m_bCancelPending ) { Leave( true ); return; }
	if ( Compatible() ) { SetStatus( "Joined party." ); return; }
	// Metadata may arrive after LobbyEnter. Allow a bounded refresh before rejecting.
	m_bAwaitingMetadata = true;
	m_flMetadataDeadline = Plat_FloatTime() + 10.0;
	steamapicontext->SteamMatchmaking()->RequestLobbyData( m_Lobby );
	SetStatus( "Checking party version..." );
}

void CHL2MPParty::OnInvite( GameLobbyJoinRequested_t *result ) { Join( result->m_steamIDLobby ); }

void CHL2MPParty::OnData( LobbyDataUpdate_t *result )
{
	if ( CSteamID( result->m_ulSteamIDLobby ) != m_Lobby || !result->m_bSuccess ) return;
	if ( Compatible() )
	{
		if ( m_bAwaitingMetadata ) SetStatus( "Joined party." );
		m_bAwaitingMetadata = false;
	}
	else if ( !m_bAwaitingMetadata )
	{
		Leave( true );
		SetStatus( "Party version changed. Please create or join a compatible party." );
	}
}

void CHL2MPParty::Tick()
{
	if ( m_Lobby.IsValid() && !Available() )
	{
		Leave( true );
		SetStatus( "Steam connection lost. Reconnect, then create or join a party." );
	}
	if ( m_bAwaitingMetadata && Plat_FloatTime() >= m_flMetadataDeadline )
	{
		Leave( true );
		SetStatus( "This party is unavailable or uses a different mod version." );
	}
}

void CHL2MPParty::Leave( bool force )
{
	if ( !force && HL2MPCoordinator().Busy() ) { SetStatus( "Finish or cancel matchmaking before leaving your party." ); return; }
	// Keep pending call results registered so a late success is immediately left.
	m_bCancelPending = m_bPending;
	if ( m_Lobby.IsValid() && steamapicontext && steamapicontext->SteamMatchmaking() )
		steamapicontext->SteamMatchmaking()->LeaveLobby( m_Lobby );
	m_Lobby.Clear();
	m_PendingInvite.Clear();
	m_bAwaitingMetadata = false;
	SetStatus( m_bPending ? "Cancelling party request..." : "Play solo or create a party to invite friends." );
}

void CHL2MPParty::Invite()
{
	if ( !Available() || !m_Lobby.IsValid() || m_bAwaitingMetadata ) return;
	if ( !steamapicontext->SteamUtils() || !steamapicontext->SteamUtils()->IsOverlayEnabled() )
	{
		SetStatus( "Enable the Steam overlay to invite friends." );
		return;
	}
	steamapicontext->SteamFriends()->ActivateGameOverlayInviteDialog( m_Lobby );
}

void CHL2MPParty::InviteFriend( CSteamID friendID )
{
	if ( !Available() || !friendID.IsValid() || m_bPending || HL2MPCoordinator().Busy() || m_bAwaitingMetadata ) return;
	if ( steamapicontext->SteamFriends()->GetFriendRelationship( friendID ) != k_EFriendRelationshipFriend ) return;
	if ( !m_Lobby.IsValid() )
	{
		m_PendingInvite = friendID; Create();
		if ( !m_bPending ) m_PendingInvite.Clear();
		return;
	}
	if ( MemberCount() >= s_MaxParty ) { SetStatus( "Your party is full." ); return; }
	SetStatus( steamapicontext->SteamMatchmaking()->InviteUserToLobby( m_Lobby, friendID ) ?
		"Invitation sent." : "Steam could not send the invitation. Try again." );
}

bool CHL2MPParty::IsOwner() const
{
	return Available() && m_Lobby.IsValid() &&
		steamapicontext->SteamMatchmaking()->GetLobbyOwner( m_Lobby ) == steamapicontext->SteamUser()->GetSteamID();
}

int CHL2MPParty::MemberCount() const
{
	return Available() && m_Lobby.IsValid() && !m_bAwaitingMetadata ?
		steamapicontext->SteamMatchmaking()->GetNumLobbyMembers( m_Lobby ) : 0;
}

const char *CHL2MPParty::MemberName( int index ) const
{
	if ( index < 0 || index >= MemberCount() ) return "";
	return steamapicontext->SteamFriends()->GetFriendPersonaName(
		steamapicontext->SteamMatchmaking()->GetLobbyMemberByIndex( m_Lobby, index ) );
}

CSteamID CHL2MPParty::Member( int index ) const
{
	return index >= 0 && index < MemberCount() ?
		steamapicontext->SteamMatchmaking()->GetLobbyMemberByIndex( m_Lobby, index ) : CSteamID();
}

CSteamID CHL2MPParty::Owner() const
{
	return Available() && m_Lobby.IsValid() ? steamapicontext->SteamMatchmaking()->GetLobbyOwner( m_Lobby ) : CSteamID();
}

const char *CHL2MPParty::MatchmakingData( const char *key ) const
{
	return Compatible() && !m_bAwaitingMetadata ? steamapicontext->SteamMatchmaking()->GetLobbyData( m_Lobby, key ) : "";
}

void CHL2MPParty::PublishMatchmaking( const char *url, const char *partyID, const char *invitation )
{
	if ( !IsOwner() || !Compatible() ) return;
	char owner[24]; Q_snprintf( owner, sizeof( owner ), "%llu", (unsigned long long)Owner().ConvertToUint64() );
	const char *keys[] = { "mm_url", "mm_party", "mm_invite", "mm_owner" };
	const char *values[] = { url, partyID, invitation, partyID[0] ? owner : "" };
	for ( int i = 0; i < ARRAYSIZE( keys ); ++i )
		if ( Q_strcmp( MatchmakingData( keys[i] ), values[i] ) )
			steamapicontext->SteamMatchmaking()->SetLobbyData( m_Lobby, keys[i], values[i] );
}

void CHL2MPParty::SetJoinable( bool joinable )
{
	if ( IsOwner() ) steamapicontext->SteamMatchmaking()->SetLobbyJoinable( m_Lobby, joinable );
}

CON_COMMAND_F( mm_party_create, "Create a Steam party.", FCVAR_CLIENTDLL )
{
	HL2MPParty().Create();
}

CON_COMMAND_F( mm_party_leave, "Leave your Steam party or cancel a pending request.", FCVAR_CLIENTDLL )
{
	HL2MPParty().Leave();
}

CON_COMMAND_F( mm_party_status, "Show Steam party state for troubleshooting.", FCVAR_CLIENTDLL )
{
	Msg( "Steam AppID: %u, online: %d, friends: %d\n", steamapicontext && steamapicontext->SteamUtils() ? steamapicontext->SteamUtils()->GetAppID() : 0,
		HL2MPParty().Available() ? 1 : 0, HL2MPParty().Available() ? steamapicontext->SteamFriends()->GetFriendCount( k_EFriendFlagImmediate ) : 0 );
	Msg( "Party: %llu, members: %d, pending: %d, status: %s\n",
		( unsigned long long )HL2MPParty().Lobby().ConvertToUint64(), HL2MPParty().MemberCount(),
		HL2MPParty().Pending() ? 1 : 0, HL2MPParty().Status() );
}

// Steam sends +connect_lobby when an invitation launches the game.
CON_COMMAND_F( connect_lobby, "Join a Steam party by lobby ID.", FCVAR_CLIENTDLL )
{
	if ( args.ArgC() != 2 ) return;
	uint64 id = 0;
	for ( const char *p = args[1]; *p; ++p )
	{
		if ( *p < '0' || *p > '9' || id > ( ~uint64( 0 ) - ( *p - '0' ) ) / 10 ) return;
		id = id * 10 + ( *p - '0' );
	}
	HL2MPParty().Join( CSteamID( id ) );
}
