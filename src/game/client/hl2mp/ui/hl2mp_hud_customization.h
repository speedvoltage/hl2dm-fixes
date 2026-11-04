#ifndef HL2MP_HUD_CUSTOMIZATION_H
#define HL2MP_HUD_CUSTOMIZATION_H
#include "Color.h"
#include "vgui/VGUI.h"
namespace vgui { class Panel; }
void HL2MPHudChat_AttachEditorSample( vgui::Panel *owner );
enum HL2MPHudPart { HUD_HEALTH, HUD_ARMOR, HUD_AMMO, HUD_ALTAMMO, HUD_POWER, HUD_CROSSHAIR, HUD_CHAT, HUD_KILLFEED, HUD_MATCH, HUD_VOTE, HUD_PARTS };
struct HL2MPHudElementStyle
{
    float x, y, w, h, textScale;
    Color foreground, background;
    bool visible, labels, bars, numbers, fitContent;
    int font;
};
const HL2MPHudElementStyle &HL2MPHudStyle( int part );
void HL2MPHudBounds( int part, int screenW, int screenH, int &x, int &y, int &w, int &h, bool preview = false );
void HL2MPHudSetChatBounds( int x, int y, int w, int h );
vgui::HFont HL2MPHudFont( int part, int pixels, bool bold = false );
bool HL2MPHudCustomEnabled();
bool HL2MPHudCustomCrosshairEnabled();
bool HL2MPHudReplaces( const char *element );
void HL2MPHudCustomization_Init();
void HL2MPHudEditor_Open();
void HL2MPHudEditor_OpenLive();
bool HL2MPHudEditor_LiveAvailable();
bool HL2MPHudEditor_IsLive();
bool HL2MPHudEditor_IsOpen();
class CUserCmd;
bool HL2MPHudEditor_FilterInput( CUserCmd *cmd );
bool HL2MPHudDrawCrosshair( int centerX, int centerY );
void HL2MPHudDeathNoticePreview( int x, int y, int width, int height, float scale );
#endif
