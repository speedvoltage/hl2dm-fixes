// Standalone: c++ -std=c++11 tools/test-footstep-preferences.cpp -o footstep-tests
#include "../src/game/shared/hl2mp/hl2mp_footsteps.h"
#include <cassert>
#include <cstdio>

int main()
{
    // Different listeners can hear different banks for the very same source.
    const int sourcePolice = 2;
    const int ally = HL2MPRemoteFootstepStyle(true, true, true, FOOTSTEPS_REBEL, FOOTSTEPS_COMBINE);
    const int enemy = HL2MPRemoteFootstepStyle(true, true, false, FOOTSTEPS_REBEL, FOOTSTEPS_COMBINE);
    assert(HL2MPFootstepBank(ally, sourcePolice) == 0);
    assert(HL2MPFootstepBank(enemy, sourcePolice) == 1);
    assert(HL2MPFootstepBank(FOOTSTEPS_MODEL, sourcePolice) == 2);

    for (int preference=0; preference<=4; ++preference)
    {
        // Matchmaking and disabling bright models always restore the source's
        // actual model sound, including after a preference was already saved.
        for (int model=0; model<3; ++model)
        {
            assert(HL2MPFootstepBank(HL2MPRemoteFootstepStyle(false,true,true,preference,preference),model)==model);
            assert(HL2MPFootstepBank(HL2MPRemoteFootstepStyle(false,true,false,preference,preference),model)==model);
            assert(HL2MPFootstepBank(HL2MPRemoteFootstepStyle(true,false,true,preference,preference),model)==model);
            assert(HL2MPFootstepBank(HL2MPRemoteFootstepStyle(true,false,false,preference,preference),model)==model);
        }
    }
    assert(HL2MPRemoteFootstepStyle(true,true,true,FOOTSTEPS_MATERIAL,FOOTSTEPS_POLICE)==FOOTSTEPS_MATERIAL);
    assert(HL2MPRemoteFootstepStyle(true,true,false,FOOTSTEPS_MATERIAL,FOOTSTEPS_POLICE)==FOOTSTEPS_POLICE);
    assert(HL2MPFootstepBank(FOOTSTEPS_MATERIAL,0)==3);
    assert(HL2MPFootstepBank(FOOTSTEPS_MATERIAL,2)==3);
    assert(HL2MPRemoteFootstepStyle(true,true,true,-1,4)==FOOTSTEPS_MODEL);
    assert(HL2MPRemoteFootstepStyle(true,true,false,4,999)==FOOTSTEPS_MODEL);
    assert(HL2MPFootstepBank(999,2)==2);
    assert(HL2MPFootstepBank(FOOTSTEPS_MODEL,999)==0);
    std::puts("Footstep preference and community gating tests passed");
}
