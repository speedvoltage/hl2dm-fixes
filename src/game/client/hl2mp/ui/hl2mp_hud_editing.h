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
}
#endif
