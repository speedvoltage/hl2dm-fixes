#include "../src/game/shared/hl2mp/hl2mp_combat_stats.h"
#include <cassert>
#include <cstdio>
int main()
{
    HL2MPCombatStats stats;
    stats.Fired(7); // One shotgun attack is seven independent pellets.
    stats.Hit(false);stats.Hit(true);stats.Hit(false);
    assert(stats.shots==7 && stats.hits==3 && stats.headHits==1);
    assert(HL2MPCombatStats::Percentage(stats.hits,stats.shots)>42.8f);
    assert(HL2MPCombatStats::Percentage(stats.headHits,stats.hits)>33.3f);
    assert(HL2MPCombatStats::Percentage(0,0)==0);
    stats.Damaged(25,false);stats.Damaged(75,true);
    assert(stats.damage==100 && stats.kills==1);
    stats.Fired(-10);assert(stats.shots==7);
    stats.damage=999999995;stats.Damaged(20,false);assert(stats.damage==1000000000);
    stats=HL2MPCombatStats();assert(!stats.shots && !stats.hits && !stats.headHits && !stats.damage && !stats.kills);
    puts("Combat statistics: pellet accounting, percentages, round reset and bounded accumulation passed");
}
