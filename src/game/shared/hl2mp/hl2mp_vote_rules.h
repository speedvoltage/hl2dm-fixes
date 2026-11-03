#ifndef HL2MP_VOTE_RULES_H
#define HL2MP_VOTE_RULES_H

namespace HL2MPVoteRules
{
enum { MAX_MAPS = 5, KICK_SECONDS = 30, MAP_DELAY = 3, MAP_SECONDS = 20, RESULT_SECONDS = 5 };
enum Phase { NONE, KICK, KICK_PASSED, KICK_FAILED, MAP_WAIT, MAP_BALLOT, MAP_RESULT };
struct Ballot { unsigned long long steam; int userid, team, choice; };
inline bool MatchesBallot( const Ballot &v, unsigned long long steam, int userid, int team, bool teamVote )
{ return steam != 0 && v.steam == steam && v.userid == userid && ( !teamVote || v.team == team ); }
inline int InitialChoice( unsigned long long voter, unsigned long long caller, unsigned long long target )
{ return voter == target ? 1 : voter == caller ? 0 : -1; }
inline int RequiredYes( int eligible ) { return eligible / 2 + 1; }
inline bool KickPasses( int yes, int eligible ) { return eligible >= 3 && yes >= RequiredYes( eligible ); }
inline bool KickImpossible( int no, int eligible ) { return eligible - no < RequiredYes( eligible ); }
// A random ticket is supplied by the server, never by a client. Zero-vote
// elections deliberately use the ordinary mapcycle instead of a random map.
inline int MapWinner( const int *counts, int count, unsigned ticket )
{
    int best = 0, ties = 0;
    for ( int i = 0; i < count; ++i ) if ( counts[i] > best ) best = counts[i];
    if ( !best ) return -1;
    for ( int i = 0; i < count; ++i ) if ( counts[i] == best ) ++ties;
    unsigned choice = ticket % ties;
    for ( int i = 0; i < count; ++i ) if ( counts[i] == best && choice-- == 0 ) return i;
    return -1;
}
inline int BanMinutes( bool matchmaking, int requested ) { return matchmaking ? 5 : requested < 1 ? 1 : requested > 30 ? 30 : requested; }
}
#endif
