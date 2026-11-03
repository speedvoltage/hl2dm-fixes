#include "cbase.h"
#include "hl2mp_vote.h"
#include "hl2mp_vote_rules.h"
#include "hl2mp_community.h"
#include "hl2mp_gamerules.h"
#include "hl2mp_match_server.h"
#include "igamesystem.h"
#include "filesystem.h"
#include "steam/steamclientpublic.h"
#include "tier1/utlstring.h"
#include "tier0/memdbgon.h"

using namespace HL2MPVoteRules;
static ConVar sv_vote_enabled( "sv_vote_enabled", "1", FCVAR_NOTIFY, "Allow player votes.", true, 0, true, 1 );
static ConVar sv_vote_kick( "sv_vote_kick", "1", FCVAR_NOTIFY, "Allow vote kicks.", true, 0, true, 1 );
static ConVar sv_vote_kick_matchmaking( "sv_vote_kick_matchmaking", "1", FCVAR_NOTIFY, "Allow vote kicks on reserved matchmaking servers.", true, 0, true, 1 );
static ConVar sv_vote_kick_ban_minutes( "sv_vote_kick_ban_minutes", "5", FCVAR_NOTIFY, "Community vote-kick rejoin block in minutes (1-30). Matchmaking always uses 5.", true, 1, true, 30 );
static ConVar sv_vote_cooldown( "sv_vote_cooldown", "120", FCVAR_NOTIFY, "Seconds between a player's kick votes.", true, 30, true, 600 );
static ConVar sv_vote_end_of_map( "sv_vote_end_of_map", "1", FCVAR_NOTIFY, "Vote on community mapcycle maps after the round. Reserved matchmaking maps are assigned by the coordinator.", true, 0, true, 1 );

namespace
{
typedef Ballot Voter;
struct Cooldown { uint64 steam; double expires; };
struct RejoinBlock { uint64 steam; double expires; };
class CVoteSystem : public CAutoGameSystemPerFrame
{
public:
    CVoteSystem() : CAutoGameSystemPerFrame( "HL2MPVotes" ), phase( NONE ), generation( 0 ), deadline( 0 ), nextUpdate( 0 ), winner( -1 ), targetSteam( 0 ), targetUser( 0 ), callerUser( 0 ) { counts[0] = 0; }
    void LevelInitPreEntity() OVERRIDE { Reset(); }
    void LevelShutdownPreEntity() OVERRIDE { Reset(); }
    void FrameUpdatePostEntityThink() OVERRIDE;
    void Reset() { phase = NONE; ++generation; voters.Purge(); maps.Purge(); deadline = nextUpdate = 0; winner = -1; targetSteam = 0; targetUser = callerUser = 0; caller[0] = target[0] = 0; memset( counts, 0, sizeof( counts ) ); }
    void Request( CBasePlayer *player, KeyValues *message );
    bool BeginMaps();
    void Snapshot( int team );
    void Send( CBasePlayer *player );
    void Broadcast();
    void FinishKick( bool passed );
    int FindVoter( CBasePlayer *player );
    void Tally();
    Phase phase; int generation; double deadline, nextUpdate; int winner;
    uint64 targetSteam; int targetUser, callerUser; char caller[128], target[128];
    int counts[MAX_MAPS]; CUtlVector<Voter> voters; CUtlVector<CUtlString> maps;
    CUtlVector<Cooldown> cooldowns; CUtlVector<RejoinBlock> blocks;
    EHANDLE requestPlayers[MAX_PLAYERS + 1]; double nextRequest[MAX_PLAYERS + 1] = {}, nextStatus[MAX_PLAYERS + 1] = {};
} s_Votes;

uint64 Identity( CBasePlayer *p )
{
    if ( !p || p->IsFakeClient() || p->IsHLTV() || !engine->IsClientFullyAuthenticated( p->edict() ) ) return 0;
    const CSteamID *id = engine->GetClientSteamID( p->edict() );
    return id && id->IsValid() && id->BIndividualAccount() ? id->ConvertToUint64() : 0;
}
bool Active( CBasePlayer *p ) { return p && !p->IsFakeClient() && !p->IsHLTV() && p->GetTeamNumber() != TEAM_SPECTATOR; }
CBasePlayer *ByUser( int id )
{
    for ( int i = 1; i <= gpGlobals->maxClients; ++i ) { CBasePlayer *p = UTIL_PlayerByIndex( i ); if ( p && p->GetUserID() == id ) return p; }
    return NULL;
}
void Feedback( CBasePlayer *p, const char *token ) { ClientPrint( p, HUD_PRINTTALK, token ); }
bool SafeMap( const char *map )
{
    if ( !map || !*map || Q_strlen( map ) > 63 ) return false;
    for ( const char *c = map; *c; ++c ) if ( !( ( *c >= 'a' && *c <= 'z' ) || ( *c >= 'A' && *c <= 'Z' ) || ( *c >= '0' && *c <= '9' ) || *c == '_' || *c == '-' ) ) return false;
    char path[80]; Q_snprintf( path, sizeof( path ), "maps/%s.bsp", map ); return engine->IsMapValid( path );
}
void CVoteSystem::Snapshot( int team )
{
    voters.Purge();
    for ( int i = 1; i <= gpGlobals->maxClients; ++i )
    {
        CBasePlayer *p = UTIL_PlayerByIndex( i );
        if ( !Active( p ) || ( team >= 0 && p->GetTeamNumber() != team ) ) continue;
        const uint64 id = Identity( p ); if ( !id ) continue;
        bool duplicate = false; FOR_EACH_VEC( voters, v ) if ( voters[v].steam == id ) duplicate = true;
        if ( !duplicate ) { Voter v = { id, p->GetUserID(), p->GetTeamNumber(), -1 }; voters.AddToTail( v ); }
    }
}
int CVoteSystem::FindVoter( CBasePlayer *p )
{
    if ( !Active( p ) ) return -1;
    const uint64 id = Identity( p );
    FOR_EACH_VEC( voters, i ) if ( MatchesBallot( voters[i], id, p->GetUserID(), p->GetTeamNumber(), phase == KICK ) ) return i;
    return -1;
}
void CVoteSystem::Tally()
{
    memset( counts, 0, sizeof( counts ) );
    FOR_EACH_VEC( voters, i ) if ( voters[i].choice >= 0 && voters[i].choice < MAX_MAPS ) ++counts[voters[i].choice];
}
void CVoteSystem::Send( CBasePlayer *p )
{
    if ( !p || p->IsFakeClient() ) return;
    int index = FindVoter( p );
    CSingleUserRecipientFilter filter( p ); filter.MakeReliable();
    UserMessageBegin( filter, "HL2MPVote" );
    WRITE_LONG( generation ); WRITE_BYTE( phase ); WRITE_FLOAT( MAX( 0.0, deadline - Plat_FloatTime() ) );
    WRITE_SHORT( voters.Count() ); WRITE_SHORT( counts[0] ); WRITE_SHORT( counts[1] );
    WRITE_BYTE( index >= 0 ); WRITE_CHAR( index >= 0 ? voters[index].choice : -1 ); WRITE_CHAR( winner );
    WRITE_LONG( callerUser ); WRITE_LONG( targetUser );
    // Keep each message below the engine's 255-byte user-message limit.
    char c[64], t[64]; Q_strncpy( c, caller, sizeof( c ) ); Q_strncpy( t, target, sizeof( t ) );
    WRITE_STRING( c ); WRITE_STRING( t ); WRITE_BYTE( maps.Count() );
    MessageEnd();
    FOR_EACH_VEC( maps, i )
    {
        UserMessageBegin( filter, "HL2MPVoteMap" ); WRITE_LONG( generation ); WRITE_BYTE( i ); WRITE_SHORT( counts[i] ); WRITE_STRING( maps[i].String() ); MessageEnd();
    }
}
void CVoteSystem::Broadcast() { for ( int i = 1; i <= gpGlobals->maxClients; ++i ) Send( UTIL_PlayerByIndex( i ) ); nextUpdate = Plat_FloatTime() + 1; }
void CVoteSystem::FinishKick( bool passed )
{
    CBasePlayer *p = ByUser( targetUser );
    if ( p && ( Identity( p ) != targetSteam || HL2MPCommunityIsProtected( p ) ) ) passed = false;
    phase = passed ? KICK_PASSED : KICK_FAILED; deadline = Plat_FloatTime() + RESULT_SECONDS;
    Broadcast();
    if ( passed )
    {
        const int minutes = BanMinutes( HL2MPMatchServerEnabled(), sv_vote_kick_ban_minutes.GetInt() );
        RejoinBlock block = { targetSteam, Plat_FloatTime() + minutes * 60 }; blocks.AddToTail( block );
        CSteamID id( targetSteam );
        engine->ServerCommand( UTIL_VarArgs( "banid %d STEAM_0:%u:%u\n", minutes, id.GetAccountID() & 1, id.GetAccountID() >> 1 ) );
        if ( p ) engine->ServerCommand( UTIL_VarArgs( "kickid %d #HL2MP_VoteRemoved\n", p->GetUserID() ) );
        Msg( "Vote kick passed: SteamID %llu, %d yes / %d eligible, blocked %d minutes.\n", (unsigned long long)targetSteam, counts[0], voters.Count(), minutes );
    }
}
void CVoteSystem::Request( CBasePlayer *p, KeyValues *message )
{
    if ( !p || p->IsFakeClient() || gpGlobals->eLoadType == MapLoad_Background ) return;
    const int slot = p->entindex(); if ( slot < 1 || slot > MAX_PLAYERS ) return;
    if ( requestPlayers[slot].Get() != p ) { requestPlayers[slot] = p; nextRequest[slot] = nextStatus[slot] = 0; }
    const char *action = message->GetString( "action" );
    // Background synchronization must never consume a player's ballot budget.
    if ( !Q_strcmp( action, "status" ) )
    {
        if ( Plat_FloatTime() >= nextStatus[slot] ) { nextStatus[slot] = Plat_FloatTime() + 1; Send( p ); }
        return;
    }
    if ( Plat_FloatTime() < nextRequest[slot] ) return;
    nextRequest[slot] = Plat_FloatTime() + .1;
    if ( !Q_strcmp( action, "cast" ) )
    {
        if ( message->GetInt( "generation", -1 ) != generation || Plat_FloatTime() >= deadline || ( phase != KICK && phase != MAP_BALLOT ) ) return;
        int v = FindVoter( p ), choice = message->GetInt( "choice", -1 );
        if ( v < 0 || choice < 0 || choice >= ( phase == KICK ? 2 : maps.Count() ) ) return;
        if ( phase == KICK && voters[v].choice != -1 ) return;
        if ( voters[v].choice == choice ) return;
        voters[v].choice = choice; Tally(); Broadcast();
        if ( phase == KICK && KickPasses( counts[0], voters.Count() ) ) FinishKick( true );
        else if ( phase == KICK && KickImpossible( counts[1], voters.Count() ) ) FinishKick( false );
        return;
    }
    if ( Q_strcmp( action, "kick" ) ) return;
    if ( !sv_vote_enabled.GetBool() || !sv_vote_kick.GetBool() || ( HL2MPMatchServerEnabled() && !sv_vote_kick_matchmaking.GetBool() ) ) { Feedback( p, "#HL2MP_VoteDisabled" ); return; }
    if ( phase != NONE || g_fGameOver ) { Feedback( p, "#HL2MP_VoteBusy" ); return; }
    CBasePlayer *victim = ByUser( message->GetInt( "userid", -1 ) );
    const uint64 callerID = Identity( p ), victimID = Identity( victim );
    if ( !Active( p ) || !Active( victim ) || !callerID || !victimID || callerID == victimID || HL2MPCommunityIsProtected( victim ) || ( HL2MPRules()->IsTeamplay() && p->GetTeamNumber() != victim->GetTeamNumber() ) )
    { Feedback( p, "#HL2MP_VoteInvalidTarget" ); return; }
    FOR_EACH_VEC( cooldowns, i ) if ( cooldowns[i].steam == callerID && cooldowns[i].expires > Plat_FloatTime() ) { Feedback( p, "#HL2MP_VoteCooldown" ); return; }
    Snapshot( HL2MPRules()->IsTeamplay() ? p->GetTeamNumber() : -1 );
    if ( voters.Count() < 3 ) { voters.Purge(); Feedback( p, "#HL2MP_VoteMinimum" ); return; }
    ++generation; phase = KICK; maps.Purge(); winner = -1;
    targetSteam = victimID; targetUser = victim->GetUserID(); callerUser = p->GetUserID();
    Q_strncpy( target, victim->GetPlayerName(), sizeof( target ) ); Q_strncpy( caller, p->GetPlayerName(), sizeof( caller ) );
    FOR_EACH_VEC( voters, i ) voters[i].choice = InitialChoice( voters[i].steam, callerID, victimID );
    Cooldown cooldown = { callerID, Plat_FloatTime() + sv_vote_cooldown.GetFloat() }; cooldowns.AddToTail( cooldown );
    deadline = Plat_FloatTime() + KICK_SECONDS; Tally(); Broadcast();
}
bool CVoteSystem::BeginMaps()
{
    // Reservations have a coordinator-assigned map and return players to their
    // lobby at completion. Community servers own their continuous mapcycle.
    if ( HL2MPMatchServerEnabled() || !sv_vote_enabled.GetBool() || !sv_vote_end_of_map.GetBool() ) return false;
    Reset();
    CUtlVector<char *> cycle; char filename[MAX_PATH];
    CMultiplayRules::DetermineMapCycleFilename( filename, sizeof( filename ), false );
    HL2MPRules()->LoadMapCycleFileIntoVector( filename, cycle );
    CUtlVector<CUtlString> valid;
    FOR_EACH_VEC( cycle, i )
    {
        if ( !SafeMap( cycle[i] ) || !Q_stricmp( cycle[i], STRING( gpGlobals->mapname ) ) ) continue;
        bool duplicate = false; FOR_EACH_VEC( valid, j ) if ( !Q_stricmp( valid[j].String(), cycle[i] ) ) duplicate = true;
        if ( !duplicate ) valid.AddToTail( CUtlString( cycle[i] ) );
    }
    CMultiplayRules::FreeMapCycleFileVector( cycle );
    while ( valid.Count() && maps.Count() < MAX_MAPS ) { int n = RandomInt( 0, valid.Count() - 1 ); maps.AddToTail( valid[n] ); valid.Remove( n ); }
    if ( !maps.Count() ) return false;
    phase = MAP_WAIT; deadline = Plat_FloatTime() + MAP_DELAY; Broadcast();
    Msg( "Map vote: round ended; ballot opens in %d seconds (%d maps).\n", MAP_DELAY, maps.Count() ); return true;
}
void CVoteSystem::FrameUpdatePostEntityThink()
{
    if ( gpGlobals->eLoadType == MapLoad_Background ) return;
    double now = Plat_FloatTime();
    FOR_EACH_VEC_BACK( cooldowns, i ) if ( cooldowns[i].expires <= now ) cooldowns.Remove( i );
    FOR_EACH_VEC_BACK( blocks, b ) if ( blocks[b].expires <= now ) blocks.Remove( b );
    // Also enforce against the authenticated identity if engine ban filters are
    // disabled. The list survives map changes and never trusts client userinfo.
    for ( int i = 1; blocks.Count() && i <= gpGlobals->maxClients; ++i )
    {
        CBasePlayer *p = UTIL_PlayerByIndex( i ); uint64 id = Identity( p );
        FOR_EACH_VEC( blocks, b ) if ( id && id == blocks[b].steam ) engine->ServerCommand( UTIL_VarArgs( "kickid %d #HL2MP_VoteRemoved\n", p->GetUserID() ) );
    }
    if ( phase == NONE ) return;
    if ( now >= deadline )
    {
        if ( phase == KICK ) FinishKick( KickPasses( counts[0], voters.Count() ) );
        else if ( phase == MAP_WAIT ) { Snapshot( -1 ); phase = MAP_BALLOT; deadline = now + MAP_SECONDS; Tally(); Broadcast(); Msg( "Map vote: ballot open for %d seconds; %d eligible humans.\n", MAP_SECONDS, voters.Count() ); }
        else if ( phase == MAP_BALLOT )
        {
            Tally(); winner = MapWinner( counts, maps.Count(), RandomInt( 0, 2147483646 ) ); phase = MAP_RESULT; deadline = now + RESULT_SECONDS; Broadcast();
            Msg( "Map vote: winner=%s; changing in %d seconds.\n", winner < 0 ? "mapcycle (no votes)" : maps[winner].String(), RESULT_SECONDS );
        }
        else if ( phase == MAP_RESULT )
        {
            if ( winner >= 0 && winner < maps.Count() && SafeMap( maps[winner].String() ) ) ConVarRef( "nextlevel" ).SetValue( maps[winner].String() );
            phase = NONE; HL2MPRules()->ChangeLevel();
        }
        else { Reset(); Broadcast(); }
    }
    else if ( now >= nextUpdate ) Broadcast();
}
}
void HL2MPVoteRequest( CBasePlayer *p, KeyValues *message ) { s_Votes.Request( p, message ); }
bool HL2MPVoteBeginIntermission() { return s_Votes.BeginMaps(); }
bool HL2MPVoteHoldsIntermission() { return s_Votes.phase == MAP_WAIT || s_Votes.phase == MAP_BALLOT || s_Votes.phase == MAP_RESULT; }
void HL2MPVoteReset() { s_Votes.Reset(); }
