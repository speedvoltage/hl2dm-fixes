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
inline int ClampPixel( int value, int low, int high ) { return value < low ? low : value > high ? high : value; }
struct ChatLimits { int margin, minW, minH; };
inline ChatLimits ChatWindowLimits( int screenW, int screenH )
{
    const float scale = screenH / 1080.f > .65f ? screenH / 1080.f : .65f;
    const int pad = ClampPixel(int(10*scale),8,screenH), gap = ClampPixel(int(6*scale),5,screenH);
    const int input = ClampPixel(int(32*scale),26,screenH), header = ClampPixel(int(26*scale),22,screenH);
    const int filter = pad*2 + ClampPixel(int(2*scale),1,screenH)*5 + ClampPixel(int(20*scale),18,screenH)*6;
    ChatLimits limits;
    limits.margin = ClampPixel(int(22*scale),12,screenH);
    limits.minW = ClampPixel(int(440*scale),1,screenW-limits.margin*2);
    limits.minH = ClampPixel(int(260*scale),header+pad+input+gap*2+filter,screenH-limits.margin*2);
    return limits;
}
inline void ConstrainChatBounds( int screenW, int screenH, int &x, int &y, int &w, int &h )
{
    const ChatLimits limits = ChatWindowLimits(screenW,screenH);
    w=ClampPixel(w,limits.minW,screenW-limits.margin*2); h=ClampPixel(h,limits.minH,screenH-limits.margin*2);
    x=ClampPixel(x,limits.margin,screenW-limits.margin-w); y=ClampPixel(y,limits.margin,screenH-limits.margin-h);
}
// Chat cvars measure position within the remaining travel, while HUD layouts
// measure position against the whole screen. Preserve the existing cvar format.
inline float ChatCvarPosition( int position, int screenSize, int size, int margin )
{
    const int travel=screenSize-margin*2-size;
    return travel>0 ? float(ClampPixel(position-margin,0,travel))/travel : 0.f;
}
inline void ChatBoundsFromCvars( int screenW, int screenH, float cx, float cy, float cw, float ch, int &x, int &y, int &w, int &h )
{
    x=y=0; w=int(cw*screenW+.5f); h=int(ch*screenH+.5f); ConstrainChatBounds(screenW,screenH,x,y,w,h);
    const int margin=ChatWindowLimits(screenW,screenH).margin;
    x=margin+int((screenW-margin*2-w)*cx+.5f); y=margin+int((screenH-margin*2-h)*cy+.5f);
    ConstrainChatBounds(screenW,screenH,x,y,w,h);
}
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
