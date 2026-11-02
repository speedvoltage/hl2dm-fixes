#include "cbase.h"
#include "hl2mp_community.h"
#include "hl2mp_match_server.h"
#include "hl2mp_gamerules.h"
#include "hl2mp_player.h"
#include "filesystem.h"
#include "inetchannelinfo.h"
#include "igamesystem.h"
#include "steam/steamclientpublic.h"
#include "tier0/memdbgon.h"

namespace
{
const char *kAdminFile = "cfg/community_admins.txt";
int s_Generation = 0;
EHANDLE s_RequestPlayers[MAX_PLAYERS + 1];
double s_NextRequest[MAX_PLAYERS + 1];
double s_NextStatus[MAX_PLAYERS + 1];

class CCommunitySystem : public CAutoGameSystem
{
public:
    CCommunitySystem() : CAutoGameSystem( "CommunityServer" ) {}
    void LevelInitPreEntity() OVERRIDE { ++s_Generation; memset( s_NextRequest, 0, sizeof( s_NextRequest ) ); memset( s_NextStatus, 0, sizeof( s_NextStatus ) ); }
    void LevelInitPostEntity() OVERRIDE
    {
        if ( !HL2MPMatchServerEnabled() && gpGlobals->eLoadType != MapLoad_Background && filesystem->FileExists( "cfg/banned_user.cfg", "MOD" ) )
            engine->ServerCommand( "exec banned_user.cfg\n" );
    }
    void LevelShutdownPreEntity() OVERRIDE
    {
        if ( !HL2MPMatchServerEnabled() && engine->IsPaused() ) engine->SetPausedForced( false );
    }
} s_CommunitySystem;

bool IsCommunity()
{
    return gpGlobals->eLoadType != MapLoad_Background && !HL2MPMatchServerEnabled();
}

bool IsAdmin( CBasePlayer *player )
{
    if ( !player || player->IsFakeClient() || !IsCommunity() ) return false;
    if ( !engine->IsDedicatedServer() )
    {
        INetChannelInfo *net = engine->GetPlayerNetInfo( player->entindex() );
        return player == UTIL_GetListenServerHost() && net && net->IsLoopback();
    }
    if ( !engine->IsClientFullyAuthenticated( player->edict() ) ) return false;
    const CSteamID *id = engine->GetClientSteamID( player->edict() );
    if ( !id || !id->IsValid() || !id->BIndividualAccount() ) return false;
    KeyValues *admins = new KeyValues( "CommunityAdmins" );
    char steamID[32]; Q_snprintf( steamID, sizeof( steamID ), "%llu", (unsigned long long)id->ConvertToUint64() );
    // Re-read on every request: removals take effect without a server restart.
    bool allowed = admins->LoadFromFile( filesystem, kAdminFile, "MOD" ) && admins->GetInt( steamID, 0 ) == 1;
    admins->deleteThis();
    return allowed;
}

bool SafeMap( const char *map )
{
    int len = Q_strlen( map );
    if ( len < 1 || len > 63 ) return false;
    for ( int i = 0; i < len; ++i )
        if ( !( ( map[i] >= 'a' && map[i] <= 'z' ) || ( map[i] >= 'A' && map[i] <= 'Z' ) ||
                ( map[i] >= '0' && map[i] <= '9' ) || map[i] == '_' || map[i] == '-' ) ) return false;
    char path[80]; Q_snprintf( path, sizeof( path ), "maps/%s.bsp", map );
    return engine->IsMapValid( path );
}

bool Integer( KeyValues *kv, const char *key, int low, int high, int &value )
{
    const char *text = kv->GetString( key, "" );
    if ( !text[0] || Q_strlen( text ) > 7 ) return false;
    value = 0;
    for ( const char *c = text; *c; ++c )
    {
        if ( *c < '0' || *c > '9' ) return false;
        value = value * 10 + ( *c - '0' );
    }
    return value >= low && value <= high;
}

void State( CBasePlayer *player, const char *status )
{
    CSingleUserRecipientFilter filter( player ); filter.MakeReliable();
    char map[64], next[64], text[80];
    Q_strncpy( map, STRING( gpGlobals->mapname ), sizeof( map ) );
    Q_strncpy( next, ConVarRef( "nextlevel" ).GetString(), sizeof( next ) );
    Q_strncpy( text, status, sizeof( text ) );
    UserMessageBegin( filter, "CommunityState" );
    WRITE_LONG( s_Generation );
    WRITE_BYTE( IsCommunity() ); WRITE_BYTE( IsAdmin( player ) ); WRITE_BYTE( engine->IsPaused() );
    WRITE_BYTE( HL2MPRules()->IsTeamplay() );
    WRITE_SHORT( clamp( ConVarRef( "mp_timelimit" ).GetInt(), 0, 1440 ) );
    WRITE_SHORT( clamp( ConVarRef( "mp_fraglimit" ).GetInt(), 0, 9999 ) );
    WRITE_BYTE( ConVarRef( "mp_friendlyfire" ).GetBool() );
    WRITE_BYTE( ConVarRef( "mp_weaponstay" ).GetBool() );
    WRITE_BYTE( ConVarRef( "mp_forcerespawn" ).GetBool() );
    WRITE_STRING( map ); WRITE_STRING( next ); WRITE_STRING( text );
    MessageEnd();
}

void Maps( CBasePlayer *player )
{
    CSingleUserRecipientFilter filter( player ); filter.MakeReliable();
    FileFindHandle_t find;
    int count = 0;
    for ( const char *file = filesystem->FindFirstEx( "maps/*.bsp", "GAME", &find ); file; file = filesystem->FindNext( find ) )
    {
        char map[64]; Q_StripExtension( file, map, sizeof( map ) );
        if ( SafeMap( map ) && count++ < 256 )
        {
            UserMessageBegin( filter, "CommunityMap" ); WRITE_LONG( s_Generation ); WRITE_STRING( map ); MessageEnd();
        }
    }
    filesystem->FindClose( find );
    UserMessageBegin( filter, "CommunityMap" ); WRITE_LONG( s_Generation ); WRITE_STRING( "" ); MessageEnd();
}

CHL2MP_Player *Target( int userID )
{
    for ( int i = 1; i <= gpGlobals->maxClients; ++i )
    {
        CHL2MP_Player *p = ToHL2MPPlayer( UTIL_PlayerByIndex( i ) );
        if ( p && p->GetUserID() == userID ) return p;
    }
    return NULL;
}
}

void HL2MPCommunityRequest( CBasePlayer *player, KeyValues *request )
{
    if ( !player || player->IsFakeClient() ) return;
    const int slot = clamp( player->entindex(), 0, MAX_PLAYERS );
    const char *action = request->GetString( "action", "" );
    if ( s_RequestPlayers[slot].Get() != player ) { s_RequestPlayers[slot] = player; s_NextRequest[slot] = s_NextStatus[slot] = 0; }
    const bool read = !Q_strcmp( action, "status" ) || !Q_strcmp( action, "open" );
    double &deadline = read ? s_NextStatus[slot] : s_NextRequest[slot];
    if ( Plat_FloatTime() < deadline ) return;
    deadline = Plat_FloatTime() + ( read ? 0.5 : 0.1 );
    if ( !IsCommunity() ) { State( player, "Server controls are available in community games." ); return; }
    if ( !Q_strcmp( action, "status" ) || !Q_strcmp( action, "open" ) )
    {
        State( player, "" );
        if ( !Q_strcmp( action, "open" ) && IsAdmin( player ) ) Maps( player );
        return;
    }
    if ( request->GetInt( "generation", -1 ) != s_Generation ) { State( player, "The map changed. Reopen server controls." ); return; }
    if ( !Q_strcmp( action, "join" ) )
    {
        int team;
        if ( !Integer( request, "team", 0, 3, team ) || ( team > 1 && !HL2MPRules()->IsTeamplay() ) ) return;
        CHL2MP_Player *p = ToHL2MPPlayer( player );
        if ( team == 0 && HL2MPRules()->IsTeamplay() )
        {
            int combine = 0, rebels = 0;
            for ( int i = 1; i <= gpGlobals->maxClients; ++i )
            {
                CBasePlayer *other = UTIL_PlayerByIndex( i ); if ( !other || other == player ) continue;
                if ( other->GetTeamNumber() == TEAM_COMBINE ) ++combine;
                if ( other->GetTeamNumber() == TEAM_REBELS ) ++rebels;
            }
            team = combine <= rebels ? TEAM_COMBINE : TEAM_REBELS;
        }
        if ( team == TEAM_UNASSIGNED )
        {
            const bool spectator = p->IsObserver(); p->StopObserverMode(); p->State_Transition( STATE_ACTIVE ); p->ChangeTeam( team );
            if ( spectator ) p->Spawn();
        }
        else p->HandleCommand_JoinTeam( team );
        State( player, "Player selection updated." ); return;
    }
    if ( !IsAdmin( player ) ) { State( player, "Only the host or a listed server admin can change these settings." ); return; }
    if ( !Q_strcmp( action, "settings" ) )
    {
        int time, frags, friendly, stay, respawn;
        if ( !Integer( request, "time", 0, 1440, time ) || !Integer( request, "frags", 0, 9999, frags ) ||
            !Integer( request, "friendly", 0, 1, friendly ) || !Integer( request, "stay", 0, 1, stay ) ||
            !Integer( request, "respawn", 0, 1, respawn ) ) { State( player, "Enter valid limits: 0-1440 minutes and 0-9999 frags." ); return; }
        ConVarRef( "mp_timelimit" ).SetValue( time ); ConVarRef( "mp_fraglimit" ).SetValue( frags );
        ConVarRef( "mp_friendlyfire" ).SetValue( friendly ); ConVarRef( "mp_weaponstay" ).SetValue( stay );
        ConVarRef( "mp_forcerespawn" ).SetValue( respawn ); State( player, "Server settings applied." ); return;
    }
    if ( !Q_strcmp( action, "pause" ) )
    {
        int paused; if ( !Integer( request, "paused", 0, 1, paused ) ) return;
        engine->SetPausedForced( paused != 0 ); State( player, paused ? "Game paused by an admin." : "Game resumed." ); return;
    }
    if ( !Q_strcmp( action, "restart" ) )
    {
        engine->SetPausedForced( false ); HL2MPRules()->RestartGame(); State( player, "Round restarted." ); return;
    }
    if ( !Q_strcmp( action, "nextmap" ) )
    {
        const char *map = request->GetString( "map" );
        if ( !SafeMap( map ) ) { State( player, "Choose a map installed on this server." ); return; }
        ConVarRef( "nextlevel" ).SetValue( map ); State( player, "Next map updated." ); return;
    }
    if ( !Q_strcmp( action, "changelevel" ) )
    {
        int team; const char *map = request->GetString( "map" );
        if ( !Integer( request, "teamplay", 0, 1, team ) || !SafeMap( map ) ) { State( player, "Choose a valid map and game mode." ); return; }
        ConVarRef( "mp_teamplay" ).SetValue( team ); engine->SetPausedForced( false ); engine->ChangeLevel( map, NULL ); return;
    }
    if ( !Q_strcmp( action, "kick" ) || !Q_strcmp( action, "ban" ) )
    {
        int userID; if ( !Integer( request, "userid", 1, 9999999, userID ) ) return;
        CHL2MP_Player *target = Target( userID );
        if ( !target || target == player || ( !engine->IsDedicatedServer() && target == UTIL_GetListenServerHost() ) )
        { State( player, "Choose another connected player." ); return; }
        if ( !Q_strcmp( action, "ban" ) )
        {
            int minutes;
            if ( !Integer( request, "minutes", 0, 10080, minutes ) ) return;
            const CSteamID *id = engine->GetClientSteamID( target->edict() );
            if ( !id || !id->IsValid() || !id->BIndividualAccount() || !engine->IsClientFullyAuthenticated( target->edict() ) )
            { State( player, "Bans require an authenticated Steam player." ); return; }
            // Only server-derived numeric identities reach the engine command buffer.
            engine->ServerCommand( UTIL_VarArgs( "banid %d STEAM_0:%u:%u\nwriteid\n", minutes, id->GetAccountID() & 1, id->GetAccountID() >> 1 ) );
        }
        engine->ServerCommand( UTIL_VarArgs( "kickid %d Removed_by_server_admin\n", userID ) );
        State( player, !Q_strcmp( action, "ban" ) ? "Player banned." : "Player kicked." ); return;
    }
    State( player, "Unknown server action." );
}
