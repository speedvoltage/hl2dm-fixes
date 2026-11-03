#include "../src/game/client/hl2mp/ui/hl2mp_hud_editing.h"
#include <cassert>
#include <cstdio>
#include <initializer_list>
using namespace HL2MPHudEditing;
int main()
{
    for(float pixels: {640.f,1280.f,2560.f})
    {
        float distance=7,offset=0,guide=-1;
        SnapCandidate(.4f,.4f+6/pixels,pixels,distance,offset,guide);
        assert(std::fabs(offset-6/pixels)<.00001f);
        SnapCandidate(.4f,.4f-2/pixels,pixels,distance,offset,guide);
        assert(std::fabs(offset+2/pixels)<.00001f);
        SnapCandidate(.4f,.4f+8/pixels,pixels,distance,offset,guide);
        assert(std::fabs(offset+2/pixels)<.00001f); // farther edges cannot steal the snap
        distance=7;offset=0;guide=-1;
        SnapCandidate(.5f,.5f+8/pixels,pixels,distance,offset,guide);
        assert(offset==0 && guide==-1); // constant visual tolerance at every zoom
    }
    assert(ZoomFactor(-1)==0 && ZoomFactor(8)==4);
    for(int i=1;i<8;++i)assert(ZoomFactor(i)>ZoomFactor(i-1));
    puts("HUD snapping and zoom checks passed.");
}
