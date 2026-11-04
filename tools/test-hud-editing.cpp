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
    int w,h;
    VotePanelSize(1280,720,.02f,.02f,w,h); assert(w==400 && h==240);
    VotePanelSize(640,480,.02f,.02f,w,h); assert(w==320 && h==210);
    VotePanelSize(1920,1080,.02f,.02f,w,h); assert(w==600 && h==360);
    VotePanelSize(1280,720,.5f,.5f,w,h); assert(w==640 && h==360);
    VotePanelSize(640,480,1,1,w,h); assert(w==624 && h==464);
    // Normalized values saved at the minimum must not shrink on reloading.
    VotePanelSize(1366,768,0,0,w,h); const int savedW=w,savedH=h;
    VotePanelSize(1366,768,float(w)/1366,float(h)/768,w,h); assert(w==savedW && h==savedH);
    puts("HUD snapping, zoom and vote size checks passed.");
}
