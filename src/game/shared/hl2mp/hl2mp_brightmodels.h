#ifndef HL2MP_BRIGHTMODELS_H
#define HL2MP_BRIGHTMODELS_H
#include "Color.h"
#include "tier1/convar.h"

extern ConVar sv_bright_models_dm_color;
extern ConVar sv_bright_models_rebels_color;
extern ConVar sv_bright_models_combine_color;

inline Color HL2MPBrightColor( int group )
{
    ConVar *vars[] = { &sv_bright_models_dm_color, &sv_bright_models_rebels_color, &sv_bright_models_combine_color };
    const Color defaults[] = { Color(0,255,0,255), Color(255,0,0,255), Color(50,150,255,255) };
    group = clamp(group, 0, 2);
    int r, g, b; char extra;
    if (sscanf(vars[group]->GetString(), "%d %d %d %c", &r, &g, &b, &extra) != 3 ||
        r < 0 || r > 255 || g < 0 || g > 255 || b < 0 || b > 255) return defaults[group];
    return Color(r,g,b,255);
}
#endif
