#ifndef HL2MP_HITMARKER_STYLE_H
#define HL2MP_HITMARKER_STYLE_H
#include "vgui/ISurface.h"
#include "vgui_controls/Controls.h"

extern ConVar cl_hitmarker_font;
extern ConVar cl_hitmarker_color;

inline Color HL2MPHitmarkerColor(const char *text)
{
    int r, g, b; char extra;
    if (sscanf(text, "%d %d %d %c", &r, &g, &b, &extra) != 3) return Color(255,0,0,255);
    return Color(clamp(r,0,255),clamp(g,0,255),clamp(b,0,255),255);
}
inline void HL2MPHitmarkerFont(vgui::HFont font, int choice, int height)
{
    const char *faces[] = { "HalfLife2", "GorDIN", "GorDIN Bold", "Tahoma" };
    choice = clamp(choice,0,3);
    int flags = vgui::ISurface::FONTFLAG_ANTIALIAS | vgui::ISurface::FONTFLAG_DROPSHADOW;
    if (choice == 0) flags |= vgui::ISurface::FONTFLAG_CUSTOM;
    vgui::surface()->SetFontGlyphSet(font,faces[choice],height,choice == 1 ? 400 : 700,0,0,flags);
}
inline int HL2MPHitmarkerAlpha(float age)
{
    return int(255 * RemapValClamped(age,.4f,.8f,1.f,0.f));
}
#endif
