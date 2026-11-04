#ifndef HL2MP_HUD_EDITING_H
#define HL2MP_HUD_EDITING_H
#include <cmath>
namespace HL2MPHudEditing
{
// Find the closest edge/center match in canvas pixels, independent of zoom.
inline void SnapCandidate( float moving, float target, float pixels, float &distance, float &offset, float &guide )
{
    const float delta = target - moving, d = std::fabs( delta * pixels );
    if ( d < distance ) { distance = d; offset = delta; guide = target; }
}
inline float ZoomFactor( int step ) { const float levels[] = { 0, .75f, 1, 1.25f, 1.5f, 2, 3, 4 }; return levels[step < 0 ? 0 : step > 7 ? 7 : step]; }
// One size policy for the actual vote panel, editor handles and saved layouts.
inline void VotePanelSize( int screenW, int screenH, float width, float height, int &w, int &h )
{
    const float scale = screenW / 1280.f < screenH / 720.f ? screenW / 1280.f : screenH / 720.f;
    const int minW = int(400*scale) > 320 ? int(400*scale) : 320;
    const int minH = int(240*scale) > 210 ? int(240*scale) : 210;
    w = int(width*screenW+.5f); h = int(height*screenH+.5f);
    if ( w < minW ) w = minW; if ( h < minH ) h = minH;
    if ( w > screenW-16 ) w = screenW-16; if ( h > screenH-16 ) h = screenH-16;
}
}
#endif
