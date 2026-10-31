#include "cbase.h"
#include "hl2mp_match_server.h"
#include "hl2mp_player.h"
#include "hl2mp_gamerules.h"
#include "igamesystem.h"
#include "team.h"
#include "steam/steam_gameserver.h"
#include "tier1/keyvaluesjson.h"
#include "tier1/utlbuffer.h"
#include "tier0/icommandline.h"
#include <stdlib.h>
#include "tier0/memdbgon.h"

extern CSteamGameServerAPIContext *steamgameserverapicontext;

namespace
{
bool SafeText( const char *text, int minimum, int maximum, bool url = false )
{
	if ( !text || Q_strlen( text ) < minimum || Q_strlen( text ) > maximum ) return false;
	for ( const char *p = text; *p; ++p )
		if ( !( ( *p >= 'a' && *p <= 'z' ) || ( *p >= 'A' && *p <= 'Z' ) ||
			( *p >= '0' && *p <= '9' ) || *p == '-' || *p == '_' ||
			( url && ( *p == ':' || *p == '.' || *p == '/' ) ) ) ) return false;
	return true;
}

void Kick( CBasePlayer *player )
{
	if ( !player ) return;
	char command[128];
	Q_snprintf( command, sizeof( command ), "kickid %d Matchmaking_reservation_ended\n", player->GetUserID() );
	engine->ServerCommand( command );
}

struct Connection
{
	EHANDLE player;
	bool verified;
	bool admitted;
	char grant[128];
	double arrived;
	Connection() : verified( false ), admitted( false ), arrived( 0 ) { grant[0] = 0; }
};

struct RosterEntry
{
	uint64 steamID;
	int team;
	int frags;
	int deaths;
	bool joined;
	double lastSeen;
};

class CMatchServer : public CAutoGameSystemPerFrame
{
public:
	CMatchServer() : CAutoGameSystemPerFrame( "HL2MPMatchServer" ), m_Enabled( false ), m_Configured( false ),
		m_LevelReady( false ), m_Prepared( false ), m_Playing( false ), m_ResultPending( false ),
		m_Request( 0 ), m_NextRequest( 0 ), m_NextHeartbeat( 0 ), m_LastContact( 0 ),
		m_AdmissionDeadline( 0 ), m_Countdown( 0 ), m_ReturnDeadline( 0 ), m_Winner( -1 ), m_Operation( HEARTBEAT )
	{
		m_URL[0] = m_Credential[0] = m_Match[0] = m_Mode[0] = m_Map[0] = m_State[0] = m_Abort[0] = 0;
		m_Response.SetGameserverFlag();
	}
	bool Init() OVERRIDE;
	void LevelInitPostEntity() OVERRIDE { m_LevelReady = true; m_NextHeartbeat = 0; }
	void LevelShutdownPreClearSteamAPIContext() OVERRIDE
	{
		if ( m_Enabled && !Q_strcmp( m_State, "active" ) && !m_ResultPending )
			Q_strncpy( m_Abort, "server_error", sizeof( m_Abort ) );
		Release();
		m_LevelReady = false;
	}
	void Shutdown() OVERRIDE { Release(); }
	void FrameUpdatePostEntityThink() OVERRIDE;
	bool Enabled() const { return m_Enabled; }
	bool Playing() const { return m_Playing && Plat_FloatTime() - m_LastContact < 20 && !m_Abort[0]; }
	int Team( CBasePlayer *player );
	void Spawn( CHL2MP_Player *player );
	void Disconnect( CBasePlayer *player );
	void Join( CBasePlayer *player, KeyValues *message );
	void Finished();
private:
	enum Operation { HEARTBEAT, READY, AUTHORIZE, RESULT, ABORT, LEFT };
	Connection &Client( CBasePlayer *player );
	int RosterIndex( CBasePlayer *player );
	bool Send( Operation operation, const char *path, const char *body );
	void Response( HTTPRequestCompleted_t *result, bool failed );
	void Assignment( KeyValues *match );
	void BeginReturn();
	void Heartbeat();
	void ClearMatch();
	void Release();
	void FreezePlayers();
	bool m_Enabled, m_Configured, m_LevelReady, m_Prepared, m_Playing, m_ResultPending;
	char m_URL[512], m_Credential[128], m_Match[40], m_Mode[16], m_Map[64], m_State[32], m_Abort[32];
	HTTPRequestHandle m_Request;
	double m_NextRequest, m_NextHeartbeat, m_LastContact, m_AdmissionDeadline, m_Countdown, m_ReturnDeadline;
	int m_Winner;
	Operation m_Operation;
	EHANDLE m_Authorizing;
	Connection m_Clients[MAX_PLAYERS + 1];
	CUtlVector<RosterEntry> m_Roster;
	CCallResult<CMatchServer, HTTPRequestCompleted_t> m_Response;
};

CMatchServer s_Server;

bool CMatchServer::Init()
{
	m_Enabled = CommandLine()->CheckParm( "-mm_server" ) && engine->IsDedicatedServer();
	if ( !m_Enabled ) return true;
	const char *url = getenv( "HL2MP_MM_SERVER_URL" );
	const char *credential = getenv( "HL2MP_MM_SERVER_TOKEN" );
	if ( !SafeText( url, 8, 480, true ) ||
		( Q_strncmp( url, "https://", 8 ) && Q_strncmp( url, "http://127.0.0.1:", 17 ) ) ||
		!SafeText( credential, 32, 120 ) )
	{
		Warning( "Matchmaking server: set a valid HL2MP_MM_SERVER_URL and HL2MP_MM_SERVER_TOKEN. Admission stays closed.\n" );
		return true;
	}
	Q_strncpy( m_URL, url, sizeof( m_URL ) );
	Q_strncpy( m_Credential, credential, sizeof( m_Credential ) );
	m_Configured = true;
	Msg( "Matchmaking server: reservation agent enabled.\n" );
	return true;
}

Connection &CMatchServer::Client( CBasePlayer *player )
{
	Connection &connection = m_Clients[clamp( player->entindex(), 0, MAX_PLAYERS )];
	if ( connection.player.Get() != player )
	{
		connection = Connection();
		connection.player = player;
		connection.arrived = Plat_FloatTime();
	}
	// IVEngineServer's contract returns NULL until this connection authenticates.
	// Consult the engine on every use so slot reuse and revoked sessions cannot
	// inherit a prior player's approval. The identity never comes from the client.
	const CSteamID *identity = engine->GetClientSteamID( player->edict() );
	const bool verified = identity && identity->IsValid() && identity->BIndividualAccount() && !ConVarRef( "sv_lan" ).GetBool();
	if ( verified && !connection.verified ) Msg( "Matchmaking server: engine authenticated player %d.\n", player->GetUserID() );
	connection.verified = verified;
	return connection;
}

int CMatchServer::RosterIndex( CBasePlayer *player )
{
	CSteamID id;
	if ( !player || !player->GetSteamID( &id ) || !id.IsValid() || !id.BIndividualAccount() ) return -1;
	FOR_EACH_VEC( m_Roster, i ) if ( m_Roster[i].steamID == id.ConvertToUint64() ) return i;
	return -1;
}

int CMatchServer::Team( CBasePlayer *player )
{
	const int index = RosterIndex( player );
	if ( index < 0 || !Client( player ).admitted ) return TEAM_SPECTATOR;
	return !Q_strcmp( m_Mode, "team" ) ? ( m_Roster[index].team == 0 ? TEAM_COMBINE : TEAM_REBELS ) : TEAM_UNASSIGNED;
}

void CMatchServer::Spawn( CHL2MP_Player *player )
{
	if ( !m_Enabled ) return;
	const int team = Team( player );
	if ( player->GetTeamNumber() != team ) player->ChangeTeam( team );
	if ( !Playing() ) player->AddFlag( FL_FROZEN );
}

void CMatchServer::Disconnect( CBasePlayer *player )
{
	if ( !m_Enabled || !player ) return;
	const int index = RosterIndex( player );
	if ( index >= 0 && Client( player ).admitted )
	{
		m_Roster[index].frags = player->FragCount();
		m_Roster[index].deaths = player->DeathCount();
	}
	Client( player ) = Connection();
}

void CMatchServer::Join( CBasePlayer *player, KeyValues *message )
{
	if ( !m_Enabled || !player ) return;
	Connection &connection = Client( player );
	if ( connection.admitted || connection.grant[0] ) return;
	const char *grant = message->GetString( "grant", "" );
	if ( Q_strcmp( message->GetString( "match", "" ), m_Match ) ||
		Q_strcmp( m_State, "active" ) || !SafeText( grant, 32, 120 ) ||
		( connection.verified && RosterIndex( player ) < 0 ) )
	{
		Kick( player );
		return;
	}
	Q_strncpy( connection.grant, grant, sizeof( connection.grant ) );
	Msg( "Matchmaking server: received reservation proof for player %d; Steam verified %d.\n", player->GetUserID(), connection.verified ? 1 : 0 );
}

void CMatchServer::Release()
{
	m_Response.Cancel();
	if ( m_Request && steamgameserverapicontext && steamgameserverapicontext->SteamHTTP() )
		steamgameserverapicontext->SteamHTTP()->ReleaseHTTPRequest( m_Request );
	m_Request = 0;
}

bool CMatchServer::Send( Operation operation, const char *path, const char *body )
{
	ISteamHTTP *http = steamgameserverapicontext ? steamgameserverapicontext->SteamHTTP() : NULL;
	if ( !http || m_Request ) return false;
	char url[768], header[160];
	Q_snprintf( url, sizeof( url ), "%s%s", m_URL, path );
	Q_snprintf( header, sizeof( header ), "Bearer %s", m_Credential );
	m_Request = http->CreateHTTPRequest( k_EHTTPMethodPOST, url );
	if ( !m_Request ) return false;
	http->SetHTTPRequestAbsoluteTimeoutMS( m_Request, 10000 );
	http->SetHTTPRequestRequiresVerifiedCertificate( m_Request, !Q_strncmp( m_URL, "https://", 8 ) );
	SteamAPICall_t call;
	if ( !http->SetHTTPRequestHeaderValue( m_Request, "Authorization", header ) ||
		!http->SetHTTPRequestRawPostBody( m_Request, "application/json", (uint8 *)body, Q_strlen( body ) ) ||
		!http->SendHTTPRequest( m_Request, &call ) )
	{
		Release();
		return false;
	}
	m_Operation = operation;
	m_Response.Set( call, this, &CMatchServer::Response );
	return true;
}

void CMatchServer::BeginReturn()
{
	if ( !m_Match[0] || m_ReturnDeadline ) return;
	// Let authenticated clients read their result and disconnect voluntarily.
	// Keep the reservation closed until they leave, with a bounded fallback.
	m_ReturnDeadline = Plat_FloatTime() + 10;
	Q_strncpy( m_State, "ending", sizeof( m_State ) );
	m_Playing = m_ResultPending = false;
	m_Abort[0] = 0;
	FreezePlayers();
	Msg( "Matchmaking server: match ended; waiting for clients to return.\n" );
}

void CMatchServer::Heartbeat()
{
	m_NextHeartbeat = Plat_FloatTime() + 5;
	char body[80];
	if ( m_Match[0] ) Q_snprintf( body, sizeof( body ), "{\"running_match\":\"%s\"}", m_Match );
	else Q_strncpy( body, "{\"running_match\":null}", sizeof( body ) );
	Send( HEARTBEAT, "/v1/servers/heartbeat", body );
}

void CMatchServer::ClearMatch()
{
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CBasePlayer *player = UTIL_PlayerByIndex( i );
		if ( player ) { Client( player ).admitted = false; player->AddFlag( FL_FROZEN ); Kick( player ); }
	}
	m_Match[0] = m_Mode[0] = m_Map[0] = m_State[0] = m_Abort[0] = 0;
	m_Prepared = m_Playing = m_ResultPending = false;
	m_Countdown = m_AdmissionDeadline = m_ReturnDeadline = 0;
	m_Roster.Purge();
	m_NextHeartbeat = 0;
	Msg( "Matchmaking server: reservation released.\n" );
}

void CMatchServer::Assignment( KeyValues *match )
{
	const char *id = match ? match->GetString( "id", "" ) : "";
	if ( !id[0] ) { if ( m_Match[0] ) BeginReturn(); return; }
	const char *state = match->GetString( "state", "" );
	if ( !SafeText( id, 32, 32 ) || ( m_Match[0] && Q_strcmp( id, m_Match ) ) ) return;
	if ( !Q_strcmp( state, "finished" ) || !Q_strcmp( state, "cancelled" ) ) { BeginReturn(); return; }
	if ( m_ReturnDeadline ) return;
	if ( !m_Match[0] )
	{
		Q_strncpy( m_Match, id, sizeof( m_Match ) );
		Q_strncpy( m_Mode, match->GetString( "mode", "" ), sizeof( m_Mode ) );
		Q_strncpy( m_Map, match->GetString( "map", "" ), sizeof( m_Map ) );
		char mapFile[96];
		Q_snprintf( mapFile, sizeof( mapFile ), "maps/%s.bsp", m_Map );
		if ( ( Q_strcmp( m_Mode, "duel" ) && Q_strcmp( m_Mode, "team" ) && Q_strcmp( m_Mode, "casual" ) ) ||
			!SafeText( m_Map, 1, 60 ) || !engine->IsMapValid( mapFile ) )
			Q_strncpy( m_Abort, "server_error", sizeof( m_Abort ) );
		Msg( "Matchmaking server: reservation %s (%s).\n", m_Match, m_Mode );
	}
	Q_strncpy( m_State, state, sizeof( m_State ) );
	if ( !Q_strcmp( state, "active" ) && !m_AdmissionDeadline ) m_AdmissionDeadline = Plat_FloatTime() + 120;
	CUtlVector<RosterEntry> roster;
	KeyValues *players = match->FindKey( "players" );
	for ( KeyValues *p = players ? players->GetFirstSubKey() : NULL; p; p = p->GetNextKey() )
	{
		RosterEntry entry;
		entry.steamID = Q_atoui64( p->GetString( "steam_id", "0" ) );
		entry.team = p->GetInt( "team", -1 );
		entry.frags = entry.deaths = 0;
		entry.joined = false;
		entry.lastSeen = Plat_FloatTime();
		FOR_EACH_VEC( m_Roster, i ) if ( m_Roster[i].steamID == entry.steamID ) entry = m_Roster[i];
		if ( !entry.steamID || entry.team < 0 || entry.team > 1 || roster.Count() >= gpGlobals->maxClients )
		{
			Q_strncpy( m_Abort, "server_error", sizeof( m_Abort ) );
			return;
		}
		roster.AddToTail( entry );
	}
	m_Roster = roster;
	if ( m_Abort[0] || Q_strcmp( m_State, "starting" ) || m_Prepared ) return;
	m_Prepared = true;
	ConVarRef( "mp_teamplay" ).SetValue( !Q_strcmp( m_Mode, "team" ) ? 1 : 0 );
	ConVarRef( "mp_timelimit" ).SetValue( !Q_strcmp( m_Mode, "duel" ) ? 10 : 20 );
	ConVarRef( "mp_fraglimit" ).SetValue( !Q_strcmp( m_Mode, "team" ) ? 50 : 0 );
	ConVarRef( "sv_cheats" ).SetValue( 0 );
	ConVarRef( "mp_restartgame" ).SetValue( 0 );
	m_LevelReady = false;
	engine->ChangeLevel( m_Map, NULL );
}

void CMatchServer::Response( HTTPRequestCompleted_t *result, bool failed )
{
	if ( result->m_hRequest != m_Request ) return;
	ISteamHTTP *http = steamgameserverapicontext ? steamgameserverapicontext->SteamHTTP() : NULL;
	KeyValues *root = NULL;
	uint32 size = 0;
	CUtlBuffer data;
	if ( !failed && result->m_bRequestSuccessful && http && http->GetHTTPResponseBodySize( m_Request, &size ) && size && size <= 65536 )
	{
		data.EnsureCapacity( size + 1 );
		if ( http->GetHTTPResponseBodyData( m_Request, (uint8 *)data.Base(), size ) )
		{
			((char *)data.Base())[size] = 0;
			KeyValuesJSONParser parser( (char *)data.Base(), size );
			root = parser.ParseFile();
		}
	}
	const Operation operation = m_Operation;
	Release();
	if ( !root || failed || result->m_eStatusCode != 200 )
	{
		Warning( "Matchmaking server: request %d failed (HTTP %d).\n", (int)operation, (int)result->m_eStatusCode );
		m_NextRequest = Plat_FloatTime() + 3;
		if ( operation == AUTHORIZE && result->m_eStatusCode == 403 )
		{
			CBasePlayer *player = ToBasePlayer( m_Authorizing.Get() );
			if ( player ) { Client( player ).grant[0] = 0; Kick( player ); }
		}
		if ( root ) root->deleteThis();
		return;
	}
	m_LastContact = Plat_FloatTime();
	if ( operation == HEARTBEAT ) Assignment( root->FindKey( "match" ) );
	else if ( operation == READY )
	{
		Q_strncpy( m_State, "active", sizeof( m_State ) );
		m_AdmissionDeadline = Plat_FloatTime() + 120;
		Msg( "Matchmaking server: map loaded; waiting for authenticated players.\n" );
	}
	else if ( operation == AUTHORIZE )
	{
		CHL2MP_Player *player = ToHL2MPPlayer( m_Authorizing.Get() );
		const int index = RosterIndex( player );
		if ( index >= 0 && Client( player ).verified && root->GetInt( "team", -1 ) == m_Roster[index].team &&
			Q_atoui64( root->GetString( "steam_id", "0" ) ) == m_Roster[index].steamID )
		{
			Connection &connection = Client( player );
			connection.admitted = true;
			connection.grant[0] = 0;
			if ( !player->HandleCommand_JoinTeam( Team( player ) ) )
			{
				connection.admitted = false;
				Q_strncpy( m_Abort, "server_error", sizeof( m_Abort ) );
				root->deleteThis();
				return;
			}
			player->ResetScores();
			player->IncrementFragCount( m_Roster[index].frags );
			player->IncrementDeathCount( m_Roster[index].deaths );
			if ( !Playing() ) player->AddFlag( FL_FROZEN );
			ClientPrint( player, HUD_PRINTTALK, "Welcome to your match." );
			Msg( "Matchmaking server: admitted player %d.\n", player->GetUserID() );
		}
	}
	else if ( operation == RESULT || operation == ABORT ) BeginReturn();
	else if ( operation == LEFT ) m_NextHeartbeat = 0;
	root->deleteThis();
}

void CMatchServer::FreezePlayers()
{
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CBasePlayer *player = UTIL_PlayerByIndex( i );
		if ( !player ) continue;
		if ( !Playing() ) player->AddFlag( FL_FROZEN );
		else if ( Client( player ).admitted ) player->RemoveFlag( FL_FROZEN );
	}
}

void CMatchServer::Finished()
{
	if ( !m_Enabled || !m_Playing || m_ResultPending ) return;
	// The final frag may arrive before this frame's roster update.
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CBasePlayer *player = UTIL_PlayerByIndex( i );
		const int index = RosterIndex( player );
		if ( index >= 0 && Client( player ).admitted ) m_Roster[index].frags = player->FragCount();
	}
	m_Winner = -1;
	if ( Q_strcmp( m_Mode, "casual" ) )
	{
		int scores[2] = { 0, 0 };
		if ( !Q_strcmp( m_Mode, "team" ) )
		{
			scores[0] = GetGlobalTeam( TEAM_COMBINE )->GetScore();
			scores[1] = GetGlobalTeam( TEAM_REBELS )->GetScore();
		}
		else FOR_EACH_VEC( m_Roster, i ) scores[m_Roster[i].team] += m_Roster[i].frags;
		if ( scores[0] != scores[1] ) m_Winner = scores[0] > scores[1] ? 0 : 1;
	}
	m_ResultPending = true;
	m_Playing = false;
}

void CMatchServer::FrameUpdatePostEntityThink()
{
	if ( !m_Enabled || !m_LevelReady ) return;
	const double now = Plat_FloatTime();
	if ( m_ReturnDeadline )
	{
		FreezePlayers();
		bool connected = false;
		for ( int i = 1; i <= gpGlobals->maxClients; ++i ) if ( UTIL_PlayerByIndex( i ) ) connected = true;
		if ( !connected || now >= m_ReturnDeadline ) ClearMatch();
		else
		{
			if ( m_Configured && !m_Request && now >= m_NextRequest && now >= m_NextHeartbeat ) Heartbeat();
			return;
		}
	}
	int present = 0;
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CBasePlayer *player = UTIL_PlayerByIndex( i );
		if ( !player ) continue;
		Connection &connection = Client( player );
		const int index = RosterIndex( player );
		if ( connection.admitted && ( index < 0 || !connection.verified ) ) { connection.admitted = false; Kick( player ); }
		if ( connection.verified && index < 0 ) Kick( player );
		if ( connection.admitted && index >= 0 )
		{
			++present;
			m_Roster[index].lastSeen = now;
			m_Roster[index].joined = true;
			m_Roster[index].frags = player->FragCount();
			m_Roster[index].deaths = player->DeathCount();
		}
		else if ( now - connection.arrived > 45 ) Kick( player );
	}
	if ( !Q_strcmp( m_State, "active" ) && !m_ResultPending && !m_Abort[0] )
	{
		if ( !m_Playing && ( ( !Q_strcmp( m_Mode, "casual" ) && present > 0 ) || ( present > 0 && present == m_Roster.Count() ) ) )
		{
			if ( !m_Countdown )
			{
				m_Countdown = now + ( !Q_strcmp( m_Mode, "casual" ) ? 0 : 5 );
				if ( Q_strcmp( m_Mode, "casual" ) ) UTIL_ClientPrintAll( HUD_PRINTCENTER, "Match begins in 5 seconds" );
			}
			if ( now >= m_Countdown )
			{
				m_Playing = true;
				HL2MPRules()->RestartGame();
				Msg( "Matchmaking server: match started.\n" );
			}
		}
		else if ( !m_Playing )
		{
			m_Countdown = 0;
			if ( m_AdmissionDeadline && now >= m_AdmissionDeadline ) Q_strncpy( m_Abort, "players_missing", sizeof( m_Abort ) );
		}
		if ( m_Playing && Q_strcmp( m_Mode, "casual" ) )
		{
			bool missing[2] = { false, false };
			FOR_EACH_VEC( m_Roster, i ) if ( now - m_Roster[i].lastSeen > 90 ) missing[m_Roster[i].team] = true;
			if ( missing[0] && missing[1] ) Q_strncpy( m_Abort, "players_missing", sizeof( m_Abort ) );
			else if ( missing[0] || missing[1] )
			{
				m_Winner = missing[0] ? 1 : 0;
				m_ResultPending = true;
				m_Playing = false;
			}
		}
	}
	FreezePlayers();
	if ( !m_Configured || m_Request || now < m_NextRequest ) return;
	m_NextRequest = now + 0.2;
	char path[160], body[320];
	if ( now >= m_NextHeartbeat )
	{
		Heartbeat();
		return;
	}
	if ( m_Abort[0] || m_ResultPending )
	{
		Q_snprintf( path, sizeof( path ), "/v1/servers/matches/%s/%s", m_Match, m_Abort[0] ? "abort" : "result" );
		if ( m_Abort[0] ) Q_snprintf( body, sizeof( body ), "{\"reason\":\"%s\"}", m_Abort );
		else if ( m_Winner >= 0 ) Q_snprintf( body, sizeof( body ), "{\"winner\":%d}", m_Winner );
		else Q_strncpy( body, "{\"winner\":null}", sizeof( body ) );
		Send( m_Abort[0] ? ABORT : RESULT, path, body );
		return;
	}
	if ( m_Prepared && !Q_strcmp( m_State, "starting" ) && !Q_strcmp( STRING( gpGlobals->mapname ), m_Map ) )
	{
		ISteamGameServer *steam = steamgameserverapicontext ? steamgameserverapicontext->SteamGameServer() : NULL;
		if ( !steam || !steam->BLoggedOn() || ConVarRef( "sv_lan" ).GetBool() ) return;
		Q_snprintf( path, sizeof( path ), "/v1/servers/matches/%s/ready", m_Match );
		Send( READY, path, "{}" );
		return;
	}
	if ( !Q_strcmp( m_State, "active" ) && !Q_strcmp( m_Mode, "casual" ) )
	{
		FOR_EACH_VEC( m_Roster, i )
		{
			if ( now - m_Roster[i].lastSeen < ( m_Roster[i].joined ? 30 : 120 ) ) continue;
			Q_snprintf( path, sizeof( path ), "/v1/servers/matches/%s/players/%llu/left", m_Match, m_Roster[i].steamID );
			Send( LEFT, path, "{}" );
			return;
		}
	}
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CBasePlayer *player = UTIL_PlayerByIndex( i );
		if ( !player ) continue;
		Connection &connection = Client( player );
		const int index = RosterIndex( player );
		if ( connection.verified && !connection.admitted && connection.grant[0] && index >= 0 )
		{
			Q_snprintf( path, sizeof( path ), "/v1/servers/matches/%s/authorize", m_Match );
			Q_snprintf( body, sizeof( body ), "{\"steam_id\":\"%llu\",\"join_token\":\"%s\"}", m_Roster[index].steamID, connection.grant );
			m_Authorizing = player;
			Send( AUTHORIZE, path, body );
			return;
		}
	}
}
}

bool HL2MPMatchServerEnabled() { return s_Server.Enabled(); }
bool HL2MPMatchServerPlaying() { return s_Server.Playing(); }
int HL2MPMatchServerTeam( CBasePlayer *player ) { return s_Server.Team( player ); }
void HL2MPMatchServerSpawn( CHL2MP_Player *player ) { s_Server.Spawn( player ); }
void HL2MPMatchServerDisconnect( CBasePlayer *player ) { s_Server.Disconnect( player ); }
void HL2MPMatchServerJoin( CBasePlayer *player, KeyValues *message ) { s_Server.Join( player, message ); }
void HL2MPMatchServerFinished() { s_Server.Finished(); }
