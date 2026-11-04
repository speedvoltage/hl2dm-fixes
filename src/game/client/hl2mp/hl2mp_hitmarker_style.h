#ifndef HL2MP_HITMARKER_STYLE_H
#define HL2MP_HITMARKER_STYLE_H
#include "vgui/ISurface.h"
#include "vgui_controls/Controls.h"
#include "filesystem.h"

extern ConVar cl_hitmarker_font;
extern ConVar cl_hitmarker_color;
extern ConVar sv_hitmarkers;

enum { HL2MP_HITMARKER_FONTS = 8 };
bool HL2MPHitmarkerRegisterFont(const char *face,const char *path);

inline Color HL2MPHitmarkerColor(const char *text)
{
    int r, g, b; char extra;
    if (sscanf(text, "%d %d %d %c", &r, &g, &b, &extra) != 3) return Color(255,0,0,255);
    return Color(clamp(r,0,255),clamp(g,0,255),clamp(b,0,255),255);
}
inline void HL2MPHitmarkerFont(vgui::HFont font, int choice, int height)
{
    const char *faces[] = { "HalfLife2", "GorDIN", "GorDIN Bold", "Tahoma", "TF2", "TF2 Build", "TF2 Secondary", "TF2 Professor" };
    choice = clamp(choice,0,HL2MP_HITMARKER_FONTS-1);
    if (choice >= 4)
    {
        const char *files[] = { "TF2.ttf", "TF2Build.ttf", "TF2Secondary.ttf", "TF2Professor.ttf" };
        char relative[MAX_PATH],path[MAX_PATH];
        Q_snprintf(relative,sizeof(relative),"resource/fonts/%s",files[choice-4]);
        if(filesystem->RelativePathToFullPath(relative,"GAME",path,sizeof(path)))
        {
            const bool loaded=HL2MPHitmarkerRegisterFont(faces[choice],path);
            DevMsg(2,"Hitmarker font file: %s loaded=%d\n",path,loaded);
        }
        else DevMsg(2,"Hitmarker font file missing: %s\n",relative);
    }
    int flags = vgui::ISurface::FONTFLAG_ANTIALIAS | vgui::ISurface::FONTFLAG_DROPSHADOW;
    if (choice == 0 || choice >= 4) flags |= vgui::ISurface::FONTFLAG_CUSTOM;
    if(!vgui::surface()->SetFontGlyphSet(font,faces[choice],height,choice == 1 || choice >= 4 ? 400 : 700,0,0,flags))
        vgui::surface()->SetFontGlyphSet(font,"Tahoma",height,700,0,0,flags & ~vgui::ISurface::FONTFLAG_CUSTOM);
    DevMsg(2,"Hitmarker font: requested=%s resolved=%s family=%s\n",faces[choice],vgui::surface()->GetFontName(font),vgui::surface()->GetFontFamilyName(font));
}
inline int HL2MPHitmarkerAlpha(float age)
{
    return int(255 * RemapValClamped(age,.4f,.8f,1.f,0.f));
}
inline vgui::HFont HL2MPGetHitmarkerFont(int choice,int height)
{
    // Surface caches glyph textures by handle. Each face/size needs its own
    // handle so changing the preview never reuses the previous face's digits.
    struct CachedFont { int choice,height;vgui::HFont handle; };
    static CUtlVector<CachedFont> fonts;
    choice=clamp(choice,0,HL2MP_HITMARKER_FONTS-1);
    FOR_EACH_VEC(fonts,i) if(fonts[i].choice==choice && fonts[i].height==height)return fonts[i].handle;
    CachedFont font={choice,height,vgui::surface()->CreateFont()};
    HL2MPHitmarkerFont(font.handle,choice,height);fonts.AddToTail(font);return font.handle;
}
#endif
