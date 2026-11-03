// Compile standalone: c++ -std=c++11 tools/test-vote-rules.cpp -o /tmp/vote-tests
#include "../src/game/shared/hl2mp/hl2mp_vote_rules.h"
#include <cassert>
#include <cstdio>
using namespace HL2MPVoteRules;
int main()
{
    assert( InitialChoice( 100, 100, 200 ) == 0 );
    assert( InitialChoice( 200, 100, 200 ) == 1 );
    assert( InitialChoice( 300, 100, 200 ) == -1 );
    assert( !KickPasses( 1, 2 ) );
    // The denominator includes abstentions and players who leave after start.
    for ( int eligible = 3; eligible <= 64; ++eligible )
    {
        assert( !KickPasses( eligible / 2, eligible ) );
        assert( KickPasses( eligible / 2 + 1, eligible ) );
        for ( int no = 0; no <= eligible; ++no )
            assert( KickImpossible( no, eligible ) == !KickPasses( eligible - no, eligible ) );
    }
    Ballot voter = { 100, 7, 2, -1 };
    assert( MatchesBallot( voter, 100, 7, 2, true ) );
    assert( !MatchesBallot( voter, 100, 8, 2, true ) ); // reconnect
    assert( !MatchesBallot( voter, 200, 7, 2, true ) ); // reused slot
    assert( !MatchesBallot( voter, 0, 7, 2, true ) ); // unauthenticated
    assert( !MatchesBallot( voter, 100, 7, 3, true ) ); // changed team
    assert( MatchesBallot( voter, 100, 7, 3, false ) );
    int counts[MAX_MAPS] = { 0, 0, 0, 0, 0 };
    assert( MapWinner( counts, 0, 0 ) == -1 );
    assert( MapWinner( counts, MAX_MAPS, 123 ) == -1 );
    counts[2] = 4;
    for ( unsigned ticket = 0; ticket < 100; ++ticket ) assert( MapWinner( counts, MAX_MAPS, ticket ) == 2 );
    counts[0] = counts[4] = 4; counts[1] = 3;
    int results[MAX_MAPS] = {};
    for ( unsigned ticket = 0; ticket < 300; ++ticket ) ++results[MapWinner( counts, MAX_MAPS, ticket )];
    assert( results[0] == 100 && results[2] == 100 && results[4] == 100 && results[1] == 0 && results[3] == 0 );
    assert( BanMinutes( false, -20 ) == 1 && BanMinutes( false, 90 ) == 30 );
    assert( BanMinutes( false, 5 ) == 5 && BanMinutes( false, 15 ) == 15 );
    assert( BanMinutes( true, 1 ) == 5 && BanMinutes( true, 30 ) == 5 );
    assert( MAP_DELAY == 3 && MAP_SECONDS == 20 && RESULT_SECONDS == 5 );
    puts( "Vote rules passed: majority, automatic ballots, identity/team/reconnect, map ties, timing and ban bounds." );
}
