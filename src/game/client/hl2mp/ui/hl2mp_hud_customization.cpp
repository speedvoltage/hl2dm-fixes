#include "cbase.h"
#include "hl2mp_hud_customization.h"
#include "hl2mp_messages.h"
#include "hl2mp_options.h"
#include "hl2mp_menucontrols.h"
#include "hud.h"
#include "hudelement.h"
#include "iclientmode.h"
#include "c_hl2mp_player.h"
#include "c_playerresource.h"
#include "c_team.h"
#include "hl2mp_gamerules.h"
#include "filesystem.h"
#include "igamesystem.h"
#include "ienginevgui.h"
#include "vgui/IInput.h"
#include "vgui/IVGui.h"
#include "vgui_controls/EditablePanel.h"
#include "vgui_controls/PanelListPanel.h"
#include "vgui_controls/CheckButton.h"
#include "vgui_controls/Label.h"
#include "vgui_controls/TextEntry.h"
#include "hl2mp_vote.h"
#include "hl2mp_hud_editing.h"
#include "usercmd.h"
#include "tier0/memdbgon.h"

static ConVar cl_custom_hud( "cl_custom_hud", "0", FCVAR_ARCHIVE, "HUD customization: 0 = original HUD, 1 = full custom HUD, 2 = custom crosshair only.", true, 0, true, 2 );
namespace
{
enum { PRESET_DEFAULT, PRESET_CLASSIC, PRESET_COMPACT, PRESET_MINIMAL, PRESET_WORKING, PRESET_CROSSHAIR, PRESET_FIRST_SLOT };
bool CrosshairOnly() { return cl_custom_hud.GetInt() == 2; }
int CurrentPreset() { return CrosshairOnly() ? PRESET_CROSSHAIR : cl_custom_hud.GetBool() ? PRESET_WORKING : PRESET_DEFAULT; }
bool EditablePart( int part ) { return cl_custom_hud.GetBool() && ( !CrosshairOnly() || part == HUD_CROSSHAIR ); }
QAngle s_LiveViewAngles;
const char *s_Names[HUD_PARTS] = { "health", "armor", "ammo", "altammo", "power", "crosshair", "chat", "killfeed", "match", "vote" };
const char *s_Labels[HUD_PARTS] = { "#Valve_Hud_HEALTH", "#HL2MP_HudArmor", "#Valve_Hud_AMMO", "#HL2MP_HudAltAmmo", "#HL2MP_HudPower", "#HL2MP_HudCrosshair", "#HL2MP_HudChat", "#HL2MP_HudKillfeed", "#HL2MP_HudMatch", "#HL2MP_HudVote" };
const char *s_Weapons[] = { "weapon_crowbar", "weapon_stunstick", "weapon_physcannon", "weapon_pistol", "weapon_357", "weapon_smg1", "weapon_ar2", "weapon_shotgun", "weapon_crossbow", "weapon_frag", "weapon_rpg", "weapon_slam" };
const char *s_WeaponLabels[] = { "#HL2_Crowbar_Menu", "#HL2_StunBaton_Menu", "#HL2_GravityGun_Menu", "#HL2_Pistol_Menu", "#HL2_357Handgun_Menu", "#HL2_SMG1_Menu", "#HL2_Pulse_Rifle_Menu", "#HL2_Shotgun_Menu", "#HL2_Crossbow_Menu", "#HL2_Grenade_Menu", "#HL2_RPG_Menu", "#HL2_SLAM_Menu" };
const char *s_Shapes[] = { "#HL2MP_HudShapeCross", "#HL2MP_HudShapeT", "#HL2MP_HudShapeCircle", "#HL2MP_HudShapeDot", "#HL2MP_CrossX", "#HL2MP_CrossDiamond", "#HL2MP_CrossSquare", "#HL2MP_CrossChevron", "#HL2MP_CrossDoubleChevron", "#HL2MP_CrossBrackets", "#HL2MP_CrossCorners", "#HL2MP_CrossRingDot", "#HL2MP_CrossCircleCross", "#HL2MP_CrossTriangle", "#HL2MP_CrossHorizontal", "#HL2MP_CrossStar" };
struct CrosshairConfig { int shape, gap, length, thickness; bool outline, dot; Color color; float scale; };
struct Layout { HL2MPHudElementStyle parts[HUD_PARTS]; int crossShape, crossGap, crossLength, crossThickness; bool crossOutline, crossDot, lowHealth; CrosshairConfig weapons[ARRAYSIZE(s_Weapons)]; bool overrides[ARRAYSIZE(s_Weapons)]; } s_Layout;
bool SameCrosshair( const CrosshairConfig &a, const CrosshairConfig &b )
{
    return a.shape == b.shape && a.gap == b.gap && a.length == b.length && a.thickness == b.thickness &&
        a.outline == b.outline && a.dot == b.dot && a.color == b.color && a.scale == b.scale;
}
bool SameLayout( const Layout &a, const Layout &b )
{
    // Compare saved values, not struct padding or unused weapon overrides.
    for ( int i = 0; i < HUD_PARTS; ++i )
    {
        const HL2MPHudElementStyle &p = a.parts[i], &q = b.parts[i];
        if ( p.x != q.x || p.y != q.y || p.w != q.w || p.h != q.h || p.textScale != q.textScale ||
            p.foreground != q.foreground || p.background != q.background || p.visible != q.visible ||
            p.labels != q.labels || p.bars != q.bars || p.numbers != q.numbers || p.fitContent != q.fitContent || p.font != q.font ) return false;
    }
    if ( a.crossShape != b.crossShape || a.crossGap != b.crossGap || a.crossLength != b.crossLength ||
        a.crossThickness != b.crossThickness || a.crossOutline != b.crossOutline || a.crossDot != b.crossDot || a.lowHealth != b.lowHealth ) return false;
    for ( int i = 0; i < ARRAYSIZE(s_Weapons); ++i )
        if ( a.overrides[i] != b.overrides[i] || ( a.overrides[i] && !SameCrosshair( a.weapons[i], b.weapons[i] ) ) ) return false;
    return true;
}
Layout s_DefaultLayout;
int s_EditWeapon = 0;
CrosshairConfig GlobalCrosshair()
{
    CrosshairConfig c = { s_Layout.crossShape, s_Layout.crossGap, s_Layout.crossLength, s_Layout.crossThickness, s_Layout.crossOutline, s_Layout.crossDot, s_Layout.parts[HUD_CROSSHAIR].foreground, s_Layout.parts[HUD_CROSSHAIR].textScale }; return c;
}
CrosshairConfig CrosshairFor( int weapon ) { return weapon > 0 && weapon <= ARRAYSIZE(s_Weapons) && s_Layout.overrides[weapon-1] ? s_Layout.weapons[weapon-1] : GlobalCrosshair(); }
void SetCrosshair( const CrosshairConfig &c )
{
    if ( s_EditWeapon ) { s_Layout.weapons[s_EditWeapon-1] = c; s_Layout.overrides[s_EditWeapon-1] = true; return; }
    s_Layout.crossShape = c.shape; s_Layout.crossGap = c.gap; s_Layout.crossLength = c.length; s_Layout.crossThickness = c.thickness; s_Layout.crossOutline = c.outline; s_Layout.crossDot = c.dot; s_Layout.parts[HUD_CROSSHAIR].foreground = c.color; s_Layout.parts[HUD_CROSSHAIR].textScale = c.scale;
}
bool s_Loaded = false;
struct Font { vgui::HFont handle; int face, pixels; bool bold; };
CUtlVector<Font> s_Fonts;
const char *s_FontFaces[] = { "Tahoma", "Arial", "Consolas", "Verdana", "Trebuchet MS", "Georgia", "Times New Roman", "Courier New" };
bool CanHide( int part ) { return part != HUD_CROSSHAIR && part != HUD_CHAT && part != HUD_VOTE; }
void Defaults( int preset )
{
    memset( s_Layout.overrides, 0, sizeof(s_Layout.overrides) );
    const float bounds[HUD_PARTS][4] = {
        {.025f,.89f,.15f,.085f}, {.19f,.89f,.15f,.085f}, {.76f,.89f,.20f,.085f}, {.89f,.80f,.07f,.07f},
        {.025f,.85f,.15f,.025f}, {.5f,.5f,.04f,.07f}, {.025f,.51f,.37f,.30f}, {.62f,.08f,.35f,.22f}, {.32f,.015f,.36f,.10f}, {.025f,.23f,.32f,.33f}
    };
    for ( int i = 0; i < HUD_PARTS; ++i )
    {
        HL2MPHudElementStyle &p = s_Layout.parts[i]; p.x = bounds[i][0]; p.y = bounds[i][1]; p.w = bounds[i][2]; p.h = bounds[i][3]; p.textScale = 1;
        p.foreground = Color( 255, 183, 67, 255 ); p.background = Color( 24, 25, 25, i == HUD_VOTE ? 235 : i == HUD_KILLFEED ? 200 : 105 );
        p.visible = p.labels = p.numbers = true; p.bars = i == HUD_POWER; p.font = 0; p.fitContent = i <= HUD_ALTAMMO;
    }
    s_Layout.parts[HUD_CROSSHAIR].foreground = Color( 220, 245, 210, 255 ); s_Layout.parts[HUD_CHAT].foreground = Color( 235, 235, 230, 255 );
    s_Layout.parts[HUD_KILLFEED].foreground = Color( 245, 245, 238, 255 );
    s_Layout.parts[HUD_CHAT].background[3] = 0;
    s_Layout.parts[HUD_MATCH].background[3] = 0;
    s_Layout.crossShape = 0; s_Layout.crossGap = 4; s_Layout.crossLength = 8; s_Layout.crossThickness = 2; s_Layout.crossOutline = true; s_Layout.crossDot = false; s_Layout.lowHealth = true;
    if ( preset == 1 )
    {
        s_Layout.parts[HUD_HEALTH].x = .35f; s_Layout.parts[HUD_ARMOR].x = .45f; s_Layout.parts[HUD_AMMO].x = .57f;
        s_Layout.parts[HUD_HEALTH].w = s_Layout.parts[HUD_ARMOR].w = .09f; s_Layout.parts[HUD_AMMO].w = .15f;
        s_Layout.parts[HUD_POWER].x = .35f; s_Layout.parts[HUD_POWER].w = .19f;
        s_Layout.parts[HUD_ALTAMMO].x = .73f; s_Layout.parts[HUD_ALTAMMO].y = .89f;
        s_Layout.parts[HUD_HEALTH].foreground = Color( 255, 145, 132, 255 ); s_Layout.parts[HUD_ARMOR].foreground = Color( 125, 210, 245, 255 );
        for ( int i = HUD_HEALTH; i <= HUD_AMMO; ++i ) s_Layout.parts[i].bars = true;
    }
    if ( preset == 2 ) for ( int i = 0; i < HUD_PARTS; ++i )
    { s_Layout.parts[i].background[3] = i == HUD_VOTE ? 220 : 0; s_Layout.parts[i].labels = false; s_Layout.parts[i].textScale = .85f; }
}
float Number( KeyValues *k, const char *name, float original, float low, float high ) { float n = k->GetFloat( name, original ); return IsFinite( n ) ? clamp( n, low, high ) : original; }
bool ReadLayout( const char *path, bool loadMode = false )
{
    KeyValues *root = new KeyValues( "HUDLayout" );
    if ( !root->LoadFromFile( filesystem, path, "MOD" ) || ( root->GetInt( "version" ) < 1 || root->GetInt( "version" ) > 3 ) ) { root->deleteThis(); return false; }
    for ( int i = 0; i < HUD_PARTS; ++i )
    {
        KeyValues *k = root->FindKey( s_Names[i] ); if ( !k ) continue; HL2MPHudElementStyle &p = s_Layout.parts[i];
        p.x = Number( k, "x", p.x, 0, 1 ); p.y = Number( k, "y", p.y, 0, 1 ); p.w = Number( k, "width", p.w, .02f, 1 ); p.h = Number( k, "height", p.h, .02f, 1 ); p.textScale = Number( k, "text_scale", 1, .5f, 2 );
        p.visible = !CanHide( i ) || k->GetBool( "visible", p.visible ); p.labels = k->GetBool( "labels", p.labels ); p.numbers = k->GetBool( "numbers", p.numbers ); p.bars = k->GetBool( "bars", p.bars ); p.font = clamp( k->GetInt( "font" ), 0, ARRAYSIZE( s_FontFaces ) - 1 );
        p.fitContent = i <= HUD_ALTAMMO && k->GetBool( "fit_content", true );
        for ( int c = 0; c < 4; ++c ) { p.foreground[c] = int( Number( k, VarArgs( "fg%d", c ), p.foreground[c], 0, 255 ) ); p.background[c] = int( Number( k, VarArgs( "bg%d", c ), p.background[c], 0, 255 ) ); }
    }
    s_Layout.crossShape = clamp( root->GetInt( "crosshair_shape" ), 0, ARRAYSIZE(s_Shapes)-1 ); s_Layout.crossGap = clamp( root->GetInt( "crosshair_gap", 4 ), 0, 24 );
    s_Layout.crossLength = clamp( root->GetInt( "crosshair_length", 8 ), 1, 32 ); s_Layout.crossThickness = clamp( root->GetInt( "crosshair_thickness", 2 ), 1, 8 );
    s_Layout.crossOutline = root->GetBool( "crosshair_outline", true ); s_Layout.crossDot = root->GetBool( "crosshair_dot", false ); s_Layout.lowHealth = root->GetBool( "low_health_warning", true );
    memset( s_Layout.overrides, 0, sizeof(s_Layout.overrides) );
    if ( KeyValues *weapons = root->FindKey("weapon_crosshairs") ) for ( int i=0; i<ARRAYSIZE(s_Weapons); ++i )
    {
        KeyValues *k = weapons->FindKey(s_Weapons[i]); if (!k) continue;
        CrosshairConfig c = GlobalCrosshair(); c.shape=clamp(k->GetInt("shape",c.shape),0,ARRAYSIZE(s_Shapes)-1); c.gap=clamp(k->GetInt("gap",c.gap),0,24); c.length=clamp(k->GetInt("length",c.length),1,32); c.thickness=clamp(k->GetInt("thickness",c.thickness),1,8);
        c.outline=k->GetBool("outline",c.outline); c.dot=k->GetBool("dot",c.dot); c.scale=Number(k,"scale",c.scale,.5f,2);
        for(int j=0;j<4;++j) c.color[j]=int(Number(k,VarArgs("color%d",j),c.color[j],0,255));
        s_Layout.weapons[i]=c; s_Layout.overrides[i]=true;
    }
    if ( loadMode ) cl_custom_hud.SetValue( clamp( root->GetInt( "mode", 1 ), 0, 2 ) );
    root->deleteThis(); return true;
}
bool SaveLayout( const char *path )
{
    // Callers can pass VarArgs(); serialization below also uses its rotating buffers.
    char savePath[MAX_PATH]; Q_strncpy( savePath, path, sizeof( savePath ) );
    KeyValues *root = new KeyValues( "HUDLayout" ); root->SetInt( "version", 3 ); root->SetInt( "mode", cl_custom_hud.GetInt() );
    for ( int i = 0; i < HUD_PARTS; ++i )
    {
        KeyValues *k = root->FindKey( s_Names[i], true ); const HL2MPHudElementStyle &p = s_Layout.parts[i];
        k->SetFloat( "x", p.x ); k->SetFloat( "y", p.y ); k->SetFloat( "width", p.w ); k->SetFloat( "height", p.h ); k->SetFloat( "text_scale", p.textScale );
        k->SetBool( "visible", p.visible ); k->SetBool( "labels", p.labels ); k->SetBool( "numbers", p.numbers ); k->SetBool( "bars", p.bars ); k->SetInt( "font", p.font );
        k->SetBool( "fit_content", p.fitContent );
        for ( int c = 0; c < 4; ++c ) { k->SetInt( VarArgs( "fg%d", c ), p.foreground[c] ); k->SetInt( VarArgs( "bg%d", c ), p.background[c] ); }
    }
    root->SetInt( "crosshair_shape", s_Layout.crossShape ); root->SetInt( "crosshair_gap", s_Layout.crossGap ); root->SetInt( "crosshair_length", s_Layout.crossLength ); root->SetInt( "crosshair_thickness", s_Layout.crossThickness );
    root->SetBool( "crosshair_outline", s_Layout.crossOutline ); root->SetBool( "crosshair_dot", s_Layout.crossDot ); root->SetBool( "low_health_warning", s_Layout.lowHealth );
    KeyValues *weapons=root->FindKey("weapon_crosshairs",true);
    for(int i=0;i<ARRAYSIZE(s_Weapons);++i) if(s_Layout.overrides[i])
    {
        KeyValues *k=weapons->FindKey(s_Weapons[i],true); const CrosshairConfig &c=s_Layout.weapons[i];
        k->SetInt("shape",c.shape); k->SetInt("gap",c.gap); k->SetInt("length",c.length); k->SetInt("thickness",c.thickness); k->SetBool("outline",c.outline); k->SetBool("dot",c.dot); k->SetFloat("scale",c.scale);
        for(int j=0;j<4;++j) k->SetInt(VarArgs("color%d",j),c.color[j]);
    }
    bool saved = root->SaveToFile( filesystem, savePath, "MOD" ); root->deleteThis(); return saved;
}
void Load() { if ( s_Loaded ) return; Defaults( 0 ); s_DefaultLayout = s_Layout; ReadLayout( "cfg/hud_layout.res" ); s_Loaded = true; }
void Rect( int x, int y, int w, int h, Color color ) { vgui::surface()->DrawSetColor( color ); vgui::surface()->DrawFilledRect( x, y, x + w, y + h ); }
void Text( int part, const char *text, int x, int y, int pixels, Color color, bool bold = false )
{
    vgui::surface()->DrawSetTextFont( HL2MPHudFont( part, pixels, bold ) ); vgui::surface()->DrawSetTextColor( color ); vgui::surface()->DrawSetTextPos( x, y );
    const wchar_t *wide = HL2MPWideText( text ); vgui::surface()->DrawPrintText( wide, V_wcslen( wide ) );
}
void CenterText( int part, const char *text, int x, int y, int width, int pixels, Color color, bool bold = false )
{
    int tw, th; vgui::surface()->GetTextSize( HL2MPHudFont( part, pixels, bold ), HL2MPWideText( text ), tw, th );
    Text( part, text, x + ( width - tw ) / 2, y, pixels, color, bold );
}
struct StatMetrics
{
    int pad, labelSize, numberSize, labelHeight, numberHeight, bar, width, height;
};
StatMetrics MeasureStat( int part, float scale, bool preview )
{
    const HL2MPHudElementStyle &p = HL2MPHudStyle( part );
    StatMetrics m = {};
    m.pad = MAX( 3, int( 8 * scale ) );
    m.labelSize = MAX( preview ? 12 : 9, int( 11 * scale * p.textScale ) );
    m.numberSize = MAX( preview ? 22 : 10, int( 32 * scale * p.textScale ) );
    int labelWidth = 0, numberWidth = 0;
    if ( p.labels ) vgui::surface()->GetTextSize( HL2MPHudFont( part, m.labelSize ), HL2MPWideText( s_Labels[part] ), labelWidth, m.labelHeight );
    // Reserve digit space so firing, reloading and taking damage do not make
    // the frame or its editor handles jump around.
    if ( p.numbers ) vgui::surface()->GetTextSize( HL2MPHudFont( part, m.numberSize, true ), part == HUD_AMMO ? L"888 / 888" : L"888", numberWidth, m.numberHeight );
    m.bar = p.bars ? MAX( 2, int( 4 * scale ) ) : 0;
    m.width = MAX( labelWidth, numberWidth ) + m.pad * 2;
    if ( p.bars ) m.width = MAX( m.width, int( 72 * scale ) );
    m.height = m.pad * 2 + m.labelHeight + m.numberHeight + ( m.bar ? m.bar + m.pad / 2 : 0 );
    return m;
}
void Stroke( int x1, int y1, int x2, int y2, int thick, Color color )
{
    int steps=MAX(abs(x2-x1),abs(y2-y1));
    for(int i=0;i<=steps;++i) Rect(x1+(x2-x1)*i/MAX(1,steps)-thick/2,y1+(y2-y1)*i/MAX(1,steps)-thick/2,thick,thick,color);
}
void Crosshair( int x, int y, float scale, const CrosshairConfig &c )
{
    scale *= c.scale;
    const int gap=int(c.gap*scale), length=MAX(1,int(c.length*scale)), thick=MAX(1,int(c.thickness*scale)), radius=gap+length;
    for(int pass=c.outline?0:1;pass<2;++pass)
    {
        const int o=pass?0:1, t=thick+o*2; Color color=pass?c.color:Color(0,0,0,c.color.a());
        if(c.shape==0 || c.shape==1 || c.shape==12 || c.shape==14 || c.shape==15)
        {
            Rect(x-gap-length-o,y-thick/2-o,length+o*2,t,color); Rect(x+gap-o,y-thick/2-o,length+o*2,t,color);
            if(c.shape!=14) { if(c.shape!=1) Rect(x-thick/2-o,y-gap-length-o,t,length+o*2,color); Rect(x-thick/2-o,y+gap-o,t,length+o*2,color); }
        }
        if(c.shape==2 || c.shape==11 || c.shape==12) { vgui::surface()->DrawSetColor(color); for(int r=MAX(1,radius-thick/2-o);r<=radius+(thick-1)/2+o;++r) vgui::surface()->DrawOutlinedCircle(x,y,r,48); }
        if(c.shape==4 || c.shape==15) for(int a=-1;a<=1;a+=2) for(int b=-1;b<=1;b+=2) Stroke(x+a*gap,y+b*gap,x+a*radius,y+b*radius,t,color);
        if(c.shape==5) { Stroke(x,y-radius,x+radius,y,t,color); Stroke(x+radius,y,x,y+radius,t,color); Stroke(x,y+radius,x-radius,y,t,color); Stroke(x-radius,y,x,y-radius,t,color); }
        if(c.shape==6 || c.shape==9 || c.shape==10)
        {
            for(int a=-1;a<=1;a+=2)
            {
                if(c.shape!=10) Stroke(x+a*radius,y-radius,x+a*radius,y+radius,t,color);
                for(int b=-1;b<=1;b+=2) { Stroke(x+a*radius,y+b*radius,x+a*(c.shape==6?0:gap),y+b*radius,t,color); if(c.shape==10) Stroke(x+a*radius,y+b*radius,x+a*radius,y+b*gap,t,color); }
            }
        }
        if(c.shape==7 || c.shape==8) for(int i=0;i<(c.shape==8?2:1);++i) { int yy=y+i*MAX(3,gap); Stroke(x-radius,yy+radius/2,x,yy-radius/2,t,color); Stroke(x,yy-radius/2,x+radius,yy+radius/2,t,color); }
        if(c.shape==13) { Stroke(x,y-radius,x+radius,y+radius,t,color); Stroke(x+radius,y+radius,x-radius,y+radius,t,color); Stroke(x-radius,y+radius,x,y-radius,t,color); }
        if(c.dot || c.shape==3 || c.shape==11) Rect(x-thick/2-o,y-thick/2-o,t,t,color);
    }
}
void DrawPart( int part, int x, int y, int w, int h, float scale, bool preview )
{
    const HL2MPHudElementStyle &style = s_Layout.parts[part]; if ( !style.visible && !preview ) return;
    if ( part == HUD_CROSSHAIR ) { if ( style.visible || preview ) Crosshair( x + w / 2, y + h / 2, scale, CrosshairFor(s_EditWeapon) ); return; }
    if ( part == HUD_KILLFEED && preview ) { HL2MPHudDeathNoticePreview( x, y, w, h, scale ); return; }
    C_BasePlayer *player = GetHudPlayer(); C_BaseCombatWeapon *weapon = player ? player->GetActiveWeapon() : NULL;
    int value = 100, reserve = -1, maxValue = 100; char number[128]; number[0] = 0;
    if ( part == HUD_HEALTH ) value = player && !preview ? MAX( 0, player->GetHealth() ) : 100;
    else if ( part == HUD_ARMOR ) value = player && !preview ? MAX( 0, player->ArmorValue() ) : 75;
    else if ( part == HUD_POWER ) { C_HL2MP_Player *hl = dynamic_cast<C_HL2MP_Player *>( player ); value = hl && !preview ? int( hl->m_HL2Local.m_flSuitPower ) : 80; }
    else if ( part == HUD_AMMO )
    {
        value = 18; reserve = 150;
        if ( !preview ) { if ( !player || !weapon || weapon->GetPrimaryAmmoType() < 0 ) return; value = weapon->Clip1(); reserve = player->GetAmmoCount( weapon->GetPrimaryAmmoType() ); if ( value < 0 ) { value = reserve; reserve = -1; } maxValue = MAX( 1, weapon->GetMaxClip1() ); }
    }
    else if ( part == HUD_ALTAMMO )
    { value = 3; if ( !preview ) { if ( !player || !weapon || weapon->GetSecondaryAmmoType() < 0 ) return; value = weapon->Clip2() >= 0 ? weapon->Clip2() : player->GetAmmoCount( weapon->GetSecondaryAmmoType() ); } }
    if ( part <= HUD_ALTAMMO && style.fitContent )
    {
        if ( !style.labels && !style.numbers && !style.bars ) return;
        const StatMetrics m = MeasureStat( part, scale, preview );
        Color color = style.foreground; if ( part == HUD_HEALTH && value <= 25 && s_Layout.lowHealth ) color = Color( 255, 90, 78, color.a() );
        HL2MPServerTheme::Rounded( x, y, w, h, style.background );
        if ( style.labels ) Text( part, s_Labels[part], x + m.pad, y + m.pad, m.labelSize, color );
        if ( reserve >= 0 ) Q_snprintf( number, sizeof( number ), "%d / %d", value, reserve ); else Q_snprintf( number, sizeof( number ), "%d", value );
        if ( style.numbers )
        {
            int pixels = m.numberSize, tw, th;
            do { vgui::surface()->GetTextSize( HL2MPHudFont( part, pixels, true ), HL2MPWideText( number ), tw, th ); if ( tw <= w - m.pad * 2 || pixels <= 8 ) break; --pixels; } while ( true );
            Text( part, number, x + m.pad, y + m.pad + m.labelHeight, pixels, color, true );
        }
        if ( m.bar )
        {
            const int width = MAX( 0, w - m.pad * 2 ), by = y + h - m.pad - m.bar;
            Rect( x + m.pad, by, width, m.bar, Color( 150, 150, 150, 70 ) );
            Rect( x + m.pad, by, int( width * clamp( float( value ) / maxValue, 0.0f, 1.0f ) ), m.bar, color );
        }
        return;
    }
    HL2MPServerTheme::Rounded( x, y, w, h, style.background );
    const int pad = MAX( 3, int( 8 * scale ) ); int font = MAX( preview ? 22 : 10, int( 32 * scale * style.textScale ) );
    const int labelSize = MAX( preview ? 12 : 9, int( 11 * scale * style.textScale ) );
    Color color = style.foreground; if ( part == HUD_HEALTH && value <= 25 && s_Layout.lowHealth ) color = Color( 255, 90, 78, color.a() );
    if ( part <= HUD_POWER )
    {
        const bool label = style.labels && h >= font + labelSize;
        if ( label ) Text( part, s_Labels[part], x + pad, y + pad / 2, labelSize, color );
        if ( reserve >= 0 ) Q_snprintf( number, sizeof( number ), "%d / %d", value, reserve ); else Q_snprintf( number, sizeof( number ), "%d", value );
        if ( style.numbers && h >= 22 * scale ) Text( part, number, x + pad, y + MAX( label ? labelSize : 0, ( h - font ) / 2 ), MIN( font, h - pad ), color, true );
        if ( style.bars ) { int bar = MAX( 2, int( 4 * scale ) ); Rect( x + pad, y + h - bar - 3, MAX( 0, w - pad * 2 ), bar, Color( 150, 150, 150, 70 ) ); Rect( x + pad, y + h - bar - 3, int( MAX( 0, w - pad * 2 ) * clamp( float( value ) / maxValue, 0.0f, 1.0f ) ), bar, color ); }
        return;
    }
    if ( part == HUD_MATCH )
    {
        int seconds = preview ? 497 : HL2MPRules() ? MAX( 0, int( ceil( HL2MPRules()->GetMapRemainingTime() ) ) ) : 0;
        if ( !preview && mp_timelimit.GetInt() <= 0 ) Q_strncpy( number, "--:--", sizeof( number ) ); else Q_snprintf( number, sizeof( number ), "%02d:%02d", seconds / 60, seconds % 60 );
        CenterText( part, number, x, y + pad / 2, w, MAX( preview ? 20 : 10, int( 23 * scale * style.textScale ) ), color, true );
        if ( style.labels )
        {
            int limit = ConVarRef( "mp_fraglimit" ).GetInt();
            if ( HL2MPRules() && HL2MPRules()->IsTeamplay() && !preview )
            { C_Team *a = GetGlobalTeam( TEAM_COMBINE ), *b = GetGlobalTeam( TEAM_REBELS ); Q_snprintf( number, sizeof( number ), "%s %d  /  %s %d", HL2MPText( "#HL2MP_TeamCombine" ), a ? a->Get_Score() : 0, HL2MPText( "#HL2MP_TeamRebels" ), b ? b->Get_Score() : 0 ); }
            else Q_snprintf( number, sizeof( number ), "%d / %d", preview ? 12 : player && g_PR ? g_PR->GetPlayerScore( player->entindex() ) : 0, preview ? 30 : limit );
            CenterText( part, number, x, y + MAX( preview ? 24 : 0, int( 32 * scale ) ), w, MAX( preview ? 13 : 9, int( 13 * scale * style.textScale ) ), color );
        }
    }
    else if ( preview )
    {
        const char *sample = part == HUD_CHAT ? "#HL2MP_HudChatSample" : "#HL2MP_HudVoteSample";
        Text( part, s_Labels[part], x + pad, y + pad, MAX( 13, int( 13 * scale * style.textScale ) ), color, true );
        Text( part, sample, x + pad, y + pad + MAX( 20, int( 22 * scale ) ), MAX( 14, int( 14 * scale * style.textScale ) ), color );
    }
}
class CCustomHud : public CHudElement, public vgui::Panel
{
    DECLARE_CLASS_SIMPLE( CCustomHud, vgui::Panel );
public:
    CCustomHud( const char *name ) : CHudElement( name ), BaseClass( g_pClientMode->GetViewport(), "CustomHUD" )
    { SetHiddenBits( HIDEHUD_HEALTH | HIDEHUD_PLAYERDEAD ); SetMouseInputEnabled( false ); SetKeyBoardInputEnabled( false ); SetPaintBackgroundEnabled( false ); }
    bool ShouldDraw() OVERRIDE { return HL2MPHudCustomEnabled() && !engine->IsLevelMainMenuBackground() && CHudElement::ShouldDraw(); }
    void OnThink() OVERRIDE { SetBounds( 0, 0, ScreenWidth(), ScreenHeight() ); }
    void Paint() OVERRIDE
    {
        int sw, sh; vgui::surface()->GetScreenSize( sw, sh ); SetBounds( 0, 0, sw, sh ); float scale = MIN( sw / 1280.0f, sh / 720.0f );
        for ( int i = HUD_HEALTH; i <= HUD_MATCH; ++i ) if ( i <= HUD_POWER || i == HUD_MATCH )
        { int x, y, w, h; HL2MPHudBounds( i, sw, sh, x, y, w, h ); DrawPart( i, x, y, w, h, scale, false ); }
    }
};
DECLARE_HUDELEMENT( CCustomHud );

class CHudEditor;
class CColorPicker;
class CHudHelp;
class CHudInspector;
class CHudClosePrompt;
struct EditState { Layout layout; int mode; };
class CEditorCanvas : public vgui::Panel
{
    DECLARE_CLASS_SIMPLE( CEditorCanvas, vgui::Panel );
public:
    CEditorCanvas( CHudEditor *editor );
    void Paint() OVERRIDE; void OnMousePressed( vgui::MouseCode code ) OVERRIDE; void OnCursorMoved( int x, int y ) OVERRIDE; void OnMouseReleased( vgui::MouseCode code ) OVERRIDE;
    void OnMouseCaptureLost() OVERRIDE;
    void View( int &width, int &height ); void SetZoom( int level ); void FocusSelection();
    void OnMouseWheeled( int delta ) OVERRIDE; void Snap( HL2MPHudElementStyle &p, int vw, int vh );
    float guideX, guideY;
    CHudEditor *owner; bool dragging, resize, panning; int startX, startY, zoom, panX, panY, startPanX, startPanY; HL2MPHudElementStyle original; CrosshairConfig originalCross;
};
class CEditorRow : public vgui::EditablePanel
{
    DECLARE_CLASS_SIMPLE( CEditorRow, vgui::EditablePanel );
public:
    CEditorRow( vgui::Panel *parent, int property, const char *label, int low, int high, int value );
    void PerformLayout() OVERRIDE;
    MESSAGE_FUNC( Changed, "SliderMoved" );
    MESSAGE_FUNC( ChoiceChanged, "TextChanged" );
    int id; vgui::Label *name, *number; CHL2MPSettingsSlider *slider; CHL2MPDropdown *choice;
};
class CHudEditor : public vgui::EditablePanel
{
    DECLARE_CLASS_SIMPLE( CHudEditor, vgui::EditablePanel );
public:
    CHudEditor();
    void Open( bool live = false ); void Close( bool apply, bool resume = true ); void BuildRows(); void Select( int part ); void Edited(); void ChoosePreset( int preset );
    void RequestClose( bool resume = true );
    void CheckSession();
    void KeepEditing() { openedAt = Plat_FloatTime(); vgui::input()->SetAppModalSurface( GetVPanel() ); RequestFocus(); if ( liveView ) engine->ClientCmd_Unrestricted( "gameui_allowescape;gameui_hide" ); }
    bool HasChanges() const { return beforeMode != cl_custom_hud.GetInt() || !SameLayout( before, s_Layout ); }
    void SetLive( bool enabled ); void ParentControls( vgui::Panel *parent ); void LayoutLive();
    bool IsLive() { return liveView && IsVisible(); }
    void SetProperty( int id, int value ); int Property( int id );
    void Checkpoint( int group = -1 ); void History( int direction ); void Zoom( int delta ); void Shortcuts();
    Color *EditColor( bool background );
    bool snapping, liveView, controlsHidden;
    void OnCommand( const char *command ) OVERRIDE;
    void PerformLayout() OVERRIDE; void PaintBackground() OVERRIDE;
    void OnKeyCodeTyped( vgui::KeyCode key ) OVERRIDE;
    void OnThink() OVERRIDE;
    MESSAGE_FUNC_PTR( ComboChanged, "TextChanged", panel );
    MESSAGE_FUNC_PARAMS( PropertyChanged, "Property", data ) { SetProperty( data->GetInt( "id" ), data->GetInt( "value" ) ); }
    int selected, section; CEditorCanvas *canvas; vgui::HFont font; bool reading;
private:
    CUtlVector<EditState> history; int historyAt, historyGroup; bool keys[4];
    CHL2MPSettingsButton *toolbar[4]; CHudHelp *help;
    CHudInspector *inspector; CHL2MPSettingsButton *viewButton, *controlsButton;
    CHudClosePrompt *closePrompt; bool toggleHeld;
    Layout before; int beforeMode; bool fromGame; double openedAt; int width, height; CHL2MPDropdown *combos[3], *saveSlot;
    CHL2MPSettingsButton *buttons[7], *tabs[2], *align[2]; CColorPicker *picker;
    vgui::PanelListPanel *rows; vgui::Label *title, *hint, *status;
};
vgui::DHANDLE<CHudEditor> s_Editor;
class CHudFontRefresh : public CAutoGameSystemPerFrame
{
public:
    CHudFontRefresh() : CAutoGameSystemPerFrame( "HUDFonts" ), width( 0 ), height( 0 ), refreshAt( 0 ) {}
    void Update( float ) OVERRIDE
    {
        if ( s_Editor ) s_Editor->CheckSession();
        int w, h; vgui::surface()->GetScreenSize( w, h );
        if ( w != width || h != height ) { width = w; height = h; refreshAt = Plat_FloatTime() + .75; }
        if ( !refreshAt || Plat_FloatTime() < refreshAt ) return;
        refreshAt = 0;
        // GameUI resets dynamic font glyphs after its video-mode scheme reload.
        // Rebind existing handles after that reload has settled.
        FOR_EACH_VEC( s_Fonts, i ) { const Font &f = s_Fonts[i]; vgui::surface()->SetFontGlyphSet( f.handle, s_FontFaces[f.face], f.pixels, f.bold ? 700 : 400, 0, 0, vgui::ISurface::FONTFLAG_ANTIALIAS | vgui::ISurface::FONTFLAG_DROPSHADOW ); }
        if ( s_Editor ) s_Editor->InvalidateLayout( true );
    }
private:
    int width, height; double refreshAt;
} s_FontRefresh;
const char *properties[] = { "#HL2MP_HudX", "#HL2MP_HudY", "#HL2MP_HudWidth", "#HL2MP_HudHeight", "#HL2MP_HudTextSize", "#HL2MP_HudRed", "#HL2MP_HudGreen", "#HL2MP_HudBlue", "#HL2MP_HudOpacity", "#HL2MP_HudBgRed", "#HL2MP_HudBgGreen", "#HL2MP_HudBgBlue", "#HL2MP_HudBgOpacity", "#HL2MP_HudVisible", "#HL2MP_HudLabels", "#HL2MP_HudNumbers", "#HL2MP_HudBars", "#HL2MP_HudFont", "#HL2MP_HudShape", "#HL2MP_HudGap", "#HL2MP_HudLength", "#HL2MP_HudThickness", "#HL2MP_HudOutline", "#HL2MP_HudDot", "#HL2MP_HudLowHealth", "#HL2MP_CrossWeapon", "#HL2MP_CrossOverride", "#HL2MP_HudFitContent" };
class CHudInspector : public vgui::EditablePanel
{
    DECLARE_CLASS_SIMPLE( CHudInspector, vgui::EditablePanel );
public:
    CHudInspector( CHudEditor *parent ) : BaseClass( parent, "LiveHudControls" ), owner( parent ), dragging( false )
    { SetVisible( false ); SetMouseInputEnabled( true ); SetKeyBoardInputEnabled( true ); SetZPos( 10 ); }
    void OnChildAdded( vgui::VPANEL child ) OVERRIDE
    {
        BaseClass::OnChildAdded( child );
        // These controls already target the editor. EditablePanel otherwise
        // adds a second command route when they move into this container.
        vgui::Panel *panel = vgui::ipanel()->GetPanel( child, GetModuleName() );
        if ( panel ) panel->RemoveActionSignalTarget( this );
    }
    void PaintBackground() OVERRIDE { HL2MPServerTheme::Rounded( 0, 0, GetWide(), GetTall(), Color( 25, 28, 30, 245 ), 6 ); }
    void OnMousePressed( vgui::MouseCode code ) OVERRIDE
    {
        int x, y; vgui::input()->GetCursorPos( x, y ); ScreenToLocal( x, y );
        if ( code != MOUSE_LEFT || y > 36 || y < 0 ) return;
        dragging = true; grabX = x; grabY = y; vgui::input()->SetMouseCapture( GetVPanel() );
    }
    void OnCursorMoved( int, int ) OVERRIDE
    {
        if ( !dragging ) return; int x,y; vgui::input()->GetCursorPos( x, y ); owner->ScreenToLocal( x, y );
        SetPos( clamp( x - grabX, 0, MAX( 0, owner->GetWide() - GetWide() ) ), clamp( y - grabY, 0, MAX( 0, owner->GetTall() - GetTall() ) ) );
        owner->InvalidateLayout( true );
    }
    void OnMouseReleased( vgui::MouseCode ) OVERRIDE { dragging = false; if ( vgui::input()->GetMouseCapture() == GetVPanel() ) vgui::input()->SetMouseCapture( NULL ); }
    void OnMouseCaptureLost() OVERRIDE { dragging = false; }
    void OnCommand( const char *command ) OVERRIDE { owner->OnCommand( command ); }
    void OnKeyCodeTyped( vgui::KeyCode key ) OVERRIDE { owner->OnKeyCodeTyped( key ); }
private:
    CHudEditor *owner; bool dragging; int grabX, grabY;
};
class CColorPicker : public vgui::EditablePanel
{
    DECLARE_CLASS_SIMPLE( CColorPicker, vgui::EditablePanel );
public:
    CColorPicker( CHudEditor *parent ) : BaseClass( parent, "HudColorPicker" ), owner( parent ), target( NULL ), drag( 0 ), writing( false )
    {
        SetVisible( false ); SetMouseInputEnabled( true ); SetKeyBoardInputEnabled( true ); SetZPos( 1000 );
        hex = new vgui::TextEntry( this, "HexColor" ); hex->SetMaximumCharCount( 7 ); hex->AddActionSignalTarget( this );
        done = new CHL2MPSettingsButton( this, "#HL2MP_HudColorDone", "done" ); done->ServerStyle( true );
        cancel = new CHL2MPSettingsButton( this, "#HL2MP_Cancel", "cancel" ); cancel->ServerStyle();
    }
    void Open( bool background )
    {
        originalLayout = s_Layout; target = owner->EditColor(background); original = *target; ReadColor(); SetVisible( true ); InvalidateLayout( true ); MoveToFront(); RequestFocus();
        vgui::input()->SetAppModalSurface( GetVPanel() );
    }
    void Close( bool accept )
    {
        if ( !IsVisible() ) return;
        if ( !accept && target ) s_Layout = originalLayout;
        if ( accept ) { owner->Edited(); owner->Checkpoint(); }
        drag = 0; if ( vgui::input()->GetMouseCapture() == GetVPanel() ) vgui::input()->SetMouseCapture( NULL );
        SetVisible( false ); target = NULL; vgui::input()->SetAppModalSurface( owner->GetVPanel() ); owner->RequestFocus(); owner->canvas->Repaint();
    }
    void PerformLayout() OVERRIDE
    {
        int sw, sh; vgui::surface()->GetScreenSize( sw, sh ); int w = MIN( 440, sw - 32 ), h = MIN( 440, sh - 32 ); SetBounds( ( sw - w ) / 2, ( sh - h ) / 2, w, h );
        field = MIN( w - 92, h - 184 );
        hex->SetBounds( 20, h - 96, w - 40, 30 ); hex->SetFont( owner->font ); hex->SetBgColor( Color( 42, 42, 42, 255 ) ); hex->SetFgColor( Color( 240, 240, 235, 255 ) );
        done->SetBounds( 20, h - 52, ( w - 52 ) / 2, 32 ); cancel->SetBounds( 32 + ( w - 52 ) / 2, h - 52, ( w - 52 ) / 2, 32 ); done->SetFont( owner->font ); cancel->SetFont( owner->font );
    }
    void ReadColor()
    {
        Vector rgb( target->r() / 255.f, target->g() / 255.f, target->b() / 255.f ); RGBtoHSV( rgb, hsv ); UpdateHex();
    }
    void UpdateHex() { writing = true; hex->SetText( VarArgs( "#%02X%02X%02X", target->r(), target->g(), target->b() ) ); writing = false; Repaint(); }
    static Color RGB( const Vector &hsv ) { Vector rgb; HSVtoRGB( hsv, rgb ); return Color( int( rgb.x * 255 + .5f ), int( rgb.y * 255 + .5f ), int( rgb.z * 255 + .5f ), 255 ); }
    void PaintBackground() OVERRIDE
    {
        HL2MPServerTheme::Rounded( 0, 0, GetWide(), GetTall(), Color( 31, 32, 34, 255 ), 6 );
        const wchar_t *title = HL2MPWideText( "#HL2MP_HudColorPicker" );
        vgui::surface()->DrawSetTextFont( owner->font ); vgui::surface()->DrawSetTextColor( Color( 240, 160, 70, 255 ) ); vgui::surface()->DrawSetTextPos( 20, 14 ); vgui::surface()->DrawPrintText( title, V_wcslen( title ) );
        if ( !target ) return;
        for ( int y = 0; y < 32; ++y ) for ( int x = 0; x < 32; ++x ) Rect( 20 + x * field / 32, 48 + y * field / 32, ( x + 1 ) * field / 32 - x * field / 32, ( y + 1 ) * field / 32 - y * field / 32, RGB( Vector( hsv.x, x / 31.f, 1 - y / 31.f ) ) );
        for ( int y = 0; y < field; ++y ) Rect( 36 + field, 48 + y, 24, 1, RGB( Vector( y * 360.f / field, 1, 1 ) ) );
        int sx = 20 + int( hsv.y * ( field - 1 ) ), sy = 48 + int( ( 1 - hsv.z ) * ( field - 1 ) );
        vgui::surface()->DrawSetColor( Color( 0, 0, 0, 255 ) ); vgui::surface()->DrawOutlinedCircle( sx, sy, 5, 16 );
        vgui::surface()->DrawSetColor( Color( 255, 255, 255, 255 ) ); vgui::surface()->DrawOutlinedCircle( sx, sy, 4, 16 );
        Rect( 32 + field, 48 + int( hsv.x / 360 * ( field - 1 ) ), 32, 2, Color( 255, 255, 255, 255 ) );
        const int count = 12, step = ( GetWide() - 40 ) / count;
        for ( int i = 0; i < count; ++i ) Rect( 20 + i * step, GetTall() - 132, step - 3, 24, Swatch( i ) );
    }
    static Color Swatch( int i )
    {
        const Color colors[] = { Color(255,183,67,255), Color(255,255,255,255), Color(0,0,0,255), Color(128,128,128,255), Color(240,80,70,255), Color(255,130,30,255), Color(255,225,75,255), Color(110,210,100,255), Color(70,215,210,255), Color(85,155,245,255), Color(165,115,230,255), Color(235,115,185,255) }; return colors[clamp( i, 0, 11 )];
    }
    void OnMousePressed( vgui::MouseCode code ) OVERRIDE
    {
        if ( code != MOUSE_LEFT || !target ) return; int x, y; vgui::input()->GetCursorPos( x, y ); ScreenToLocal( x, y );
        if ( y >= GetTall() - 132 && y < GetTall() - 108 && x >= 20 && x < GetWide() - 20 ) { Color c = Swatch( ( x - 20 ) / MAX( 1, ( GetWide() - 40 ) / 12 ) ); c[3] = target->a(); *target = c; ReadColor(); return; }
        if ( y < 48 || y >= 48 + field ) return;
        drag = x >= 20 && x < 20 + field ? 1 : x >= 36 + field && x < 60 + field ? 2 : 0;
        if ( drag ) { vgui::input()->SetMouseCapture( GetVPanel() ); OnCursorMoved( x, y ); }
    }
    void OnCursorMoved( int x, int y ) OVERRIDE
    {
        if ( !drag || !target ) return;
        if ( drag == 1 ) { hsv.y = clamp( ( x - 20.f ) / ( field - 1 ), 0.f, 1.f ); hsv.z = 1 - clamp( ( y - 48.f ) / ( field - 1 ), 0.f, 1.f ); }
        else hsv.x = clamp( ( y - 48.f ) / ( field - 1 ), 0.f, .9999f ) * 360;
        Color c = RGB( hsv ); c[3] = target->a(); *target = c; UpdateHex(); owner->canvas->Repaint();
    }
    void OnMouseReleased( vgui::MouseCode ) OVERRIDE { drag = 0; if ( vgui::input()->GetMouseCapture() == GetVPanel() ) vgui::input()->SetMouseCapture( NULL ); }
    void OnMouseCaptureLost() OVERRIDE { drag = 0; }
    void OnCommand( const char *command ) OVERRIDE { Close( !Q_strcmp( command, "done" ) ); }
    void OnKeyCodeTyped( vgui::KeyCode code ) OVERRIDE { if ( code == KEY_ESCAPE ) Close( false ); else if ( code == KEY_ENTER ) Close( true ); else BaseClass::OnKeyCodeTyped( code ); }
    MESSAGE_FUNC( HexChanged, "TextChanged" )
    {
        if ( writing || !target ) return; char text[16]; hex->GetText( text, sizeof( text ) ); const char *p = text[0] == '#' ? text + 1 : text; if ( Q_strlen( p ) != 6 ) return;
        int value = 0; for ( int i = 0; i < 6; ++i ) { char c = p[i]; int n = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; if ( n < 0 ) return; value = value * 16 + n; }
        *target = Color( value >> 16, ( value >> 8 ) & 255, value & 255, target->a() ); Vector rgb( target->r()/255.f, target->g()/255.f, target->b()/255.f ); RGBtoHSV( rgb, hsv ); Repaint(); owner->canvas->Repaint();
    }
private:
    Layout originalLayout; CHudEditor *owner; Color *target, original; Vector hsv; int field, drag; bool writing; vgui::TextEntry *hex; CHL2MPSettingsButton *done, *cancel;
};
class CHudHelp : public vgui::EditablePanel
{
    DECLARE_CLASS_SIMPLE(CHudHelp,vgui::EditablePanel);
public:
    CHudHelp(CHudEditor *p):BaseClass(p,"HudHelp"),owner(p)
    {
        SetVisible(false); SetMouseInputEnabled(true); SetKeyBoardInputEnabled(true); SetZPos(1000);
        title=new vgui::Label(this,"Title","#HL2MP_HudHelp"); body=new vgui::Label(this,"Body","#HL2MP_HudHelpBody"); body->SetWrap(true); body->SetContentAlignment(vgui::Label::a_northwest);
        close=new CHL2MPSettingsButton(this,"#GameUI_Close","close"); close->ServerStyle(true);
    }
    void Open() { SetVisible(true); InvalidateLayout(true); MoveToFront(); RequestFocus(); vgui::input()->SetAppModalSurface(GetVPanel()); }
    void Close() { SetVisible(false); vgui::input()->SetAppModalSurface(owner->GetVPanel()); owner->RequestFocus(); }
    void OnCommand(const char *) OVERRIDE { Close(); }
    void OnKeyCodeTyped(vgui::KeyCode key) OVERRIDE { if(key==KEY_ESCAPE || key==KEY_F1) Close(); else BaseClass::OnKeyCodeTyped(key); }
    void PerformLayout() OVERRIDE
    {
        int sw,sh; vgui::surface()->GetScreenSize(sw,sh); int w=MIN(680,sw-32),h=MIN(520,sh-32); SetBounds((sw-w)/2,(sh-h)/2,w,h);
        title->SetBounds(20,16,w-40,28); body->SetBounds(20,56,w-40,h-124); close->SetBounds(w-180,h-52,160,32);
        title->SetFont(owner->font); title->SetFgColor(HL2MPServerTheme::Orange()); body->SetFont(owner->font); body->SetFgColor(HL2MPServerTheme::Text()); close->SetFont(owner->font);
        body->GetTextImage()->SetSize(body->GetWide(),body->GetTall()); body->GetTextImage()->RecalculateNewLinePositions();
    }
    void PaintBackground() OVERRIDE { HL2MPServerTheme::Rounded(0,0,GetWide(),GetTall(),Color(30,31,33,255),6); }
private: CHudEditor *owner; vgui::Label *title,*body; CHL2MPSettingsButton *close;
};
class CHudClosePrompt : public vgui::EditablePanel
{
    DECLARE_CLASS_SIMPLE( CHudClosePrompt, vgui::EditablePanel );
public:
    CHudClosePrompt( CHudEditor *p ) : BaseClass( p, "HudUnsavedChanges" ), owner( p ), resume( true )
    {
        SetVisible( false ); SetMouseInputEnabled( true ); SetKeyBoardInputEnabled( true ); SetZPos( 2000 );
        title = new vgui::Label( this, "Title", "#HL2MP_HudEditor" );
        body = new vgui::Label( this, "Body", "#HL2MP_HudUnsavedPrompt" ); body->SetWrap( true ); body->SetContentAlignment( vgui::Label::a_northwest );
        const char *labels[] = { "#HL2MP_HudSaveChanges", "#HL2MP_HudDiscardChanges", "#HL2MP_Cancel" };
        const char *commands[] = { "save", "discard", "cancel" };
        for ( int i=0; i<3; ++i ) { buttons[i] = new CHL2MPSettingsButton( this, labels[i], commands[i] ); buttons[i]->ServerStyle( i == 0 ); }
    }
    void Open( bool resumeGame )
    {
        if ( IsVisible() ) return;
        resume = resumeGame; SetVisible( true ); InvalidateLayout( true ); MoveToFront();
        vgui::input()->SetAppModalSurface( GetVPanel() ); buttons[2]->RequestFocus();
    }
    void Dismiss()
    {
        if ( !IsVisible() ) return;
        SetVisible( false ); owner->KeepEditing();
    }
    void OnCommand( const char *command ) OVERRIDE
    {
        if ( !Q_strcmp( command, "save" ) ) owner->Close( true, resume );
        else if ( !Q_strcmp( command, "discard" ) ) owner->Close( false, resume );
        else Dismiss();
    }
    void OnKeyCodeTyped( vgui::KeyCode key ) OVERRIDE { if ( key == KEY_ESCAPE ) Dismiss(); else BaseClass::OnKeyCodeTyped( key ); }
    void PerformLayout() OVERRIDE
    {
        int sw,sh; vgui::surface()->GetScreenSize(sw,sh); SetBounds(0,0,sw,sh);
        w=MIN(620,sw-32); h=MIN(260,sh-32); x=(sw-w)/2; y=(sh-h)/2;
        title->SetBounds(x+24,y+16,w-48,32); title->SetFont(owner->font); title->SetFgColor(HL2MPServerTheme::Orange());
        body->SetBounds(x+24,y+64,w-48,h-144); body->SetFont(owner->font); body->SetFgColor(HL2MPServerTheme::Text());
        body->GetTextImage()->SetSize(body->GetWide(),body->GetTall()); body->GetTextImage()->RecalculateNewLinePositions();
        const int bw=(w-64)/3;
        for(int i=0;i<3;++i) { buttons[i]->SetBounds(x+24+i*(bw+8),y+h-64,bw,44); buttons[i]->SetFont(owner->font); }
    }
    void PaintBackground() OVERRIDE { Rect(0,0,GetWide(),GetTall(),Color(0,0,0,155)); HL2MPServerTheme::Rounded(x,y,w,h,Color(30,31,33,255),6); }
private:
    CHudEditor *owner; bool resume; int x,y,w,h; vgui::Label *title,*body; CHL2MPSettingsButton *buttons[3];
};
class CShapePalette : public vgui::Panel
{
    DECLARE_CLASS_SIMPLE(CShapePalette,vgui::Panel);
public:
    CShapePalette(vgui::Panel *p):BaseClass(p,"CrosshairGallery") { SetMouseInputEnabled(true); }
    void Paint() OVERRIDE
    {
        const CrosshairConfig selected=CrosshairFor(s_EditWeapon); int w=GetWide()/4,h=GetTall()/4;
        for(int i=0;i<ARRAYSIZE(s_Shapes);++i)
        {
            int x=(i%4)*w,y=(i/4)*h; HL2MPServerTheme::Rounded(x+2,y+2,w-4,h-4,selected.shape==i?Color(95,63,30,255):Color(36,38,39,255));
            CrosshairConfig c=selected; c.shape=i; c.scale=1; c.gap=3; c.length=8; c.thickness=2; c.dot=false;
            Crosshair(x+w/2,y+h/2,MIN(w/70.f,h/44.f),c);
        }
    }
    void OnMousePressed(vgui::MouseCode code) OVERRIDE
    {
        if(code!=MOUSE_LEFT)return; int x,y;vgui::input()->GetCursorPos(x,y);ScreenToLocal(x,y);
        int shape=clamp(y/MAX(1,GetTall()/4),0,3)*4+clamp(x/MAX(1,GetWide()/4),0,3); s_Editor->SetProperty(18,shape); s_Editor->BuildRows();
    }
};
class CColorRow : public CHL2MPSettingsButton
{
    DECLARE_CLASS_SIMPLE( CColorRow, CHL2MPSettingsButton );
public:
    CColorRow( vgui::Panel *parent, bool background ) : BaseClass( parent, background ? "#HL2MP_HudBackgroundColor" : "#HL2MP_HudTextColor", background ? "color_bg" : "color_fg" ), bg( background ) { ServerStyle(); AddActionSignalTarget( s_Editor.Get() ); }
    void Paint() OVERRIDE { BaseClass::Paint(); const HL2MPHudElementStyle &style = HL2MPHudStyle( s_Editor->selected ); Color c = bg ? style.background : s_Editor->selected==HUD_CROSSHAIR ? CrosshairFor(s_EditWeapon).color : style.foreground; c[3] = 255; Rect( GetWide() - 38, GetTall()/2 - 10, 24, 20, c ); }
private: bool bg;
};
CEditorRow::CEditorRow( vgui::Panel *parent, int property, const char *label, int low, int high, int value ) : BaseClass( parent, "PropertyRow" ), id( property ), slider( NULL ), choice( NULL )
{
    name = new vgui::Label( this, "Name", label ); name->SetWrap( true ); name->SetContentAlignment( vgui::Label::a_west ); name->SetMouseInputEnabled( false );
    number = new vgui::Label( this, "Value", "" ); number->SetContentAlignment( vgui::Label::a_east );
    if ( ( id >= 13 && id <= 18 ) || id >= 22 )
    {
        choice = new CHL2MPDropdown( this, "Choice" ); choice->ServerStyle();
        if ( id == 17 ) { for ( int i = 0; i < ARRAYSIZE( s_FontFaces ); ++i ) choice->AddItem( s_FontFaces[i], NULL ); }
        else if ( id == 18 ) { for ( int i=0;i<ARRAYSIZE(s_Shapes);++i ) choice->AddItem(s_Shapes[i],NULL); }
        else if ( id == 25 ) { choice->AddItem("#HL2MP_CrossAllWeapons",NULL); for(int i=0;i<ARRAYSIZE(s_Weapons);++i) choice->AddItem(s_WeaponLabels[i],NULL); }
        else { choice->AddItem( "#GameUI_No", NULL ); choice->AddItem( "#GameUI_Yes", NULL ); }
        choice->ActivateItemByRow( value ); choice->AddActionSignalTarget( this ); number->SetVisible( false );
    }
    else { slider = new CHL2MPSettingsSlider( this ); slider->SetRange( low, high ); slider->SetValue( value, false ); slider->AddActionSignalTarget( this ); }
}
void CEditorRow::PerformLayout()
{
    BaseClass::PerformLayout(); int h = GetTall(); name->SetBounds( 8, 0, GetWide() - 66, h / 2 ); number->SetBounds( GetWide() - 56, 0, 48, h / 2 );
    if ( choice ) { choice->SetBounds( 8, h / 2, GetWide() - 16, h / 2 ); choice->Style( s_Editor->font, h / 62.0f ); }
    else slider->SetBounds( 8, h / 2, GetWide() - 16, h / 2 );
    name->SetFont( s_Editor->font ); number->SetFont( s_Editor->font ); name->SetFgColor( Color( 220, 220, 215, 255 ) ); number->SetFgColor( Color( 242, 161, 77, 255 ) );
    name->SetTextInset( 0, 0 ); name->GetTextImage()->SetSize( name->GetWide(), name->GetTall() ); name->GetTextImage()->RecalculateNewLinePositions();
    if ( slider ) number->SetText( VarArgs( "%d", slider->GetValue() ) );
}
void CEditorRow::Changed()
{ number->SetText( VarArgs( "%d", slider->GetValue() ) ); if ( s_Editor && !s_Editor->reading ) s_Editor->SetProperty( id, slider->GetValue() ); }
void CEditorRow::ChoiceChanged() { if ( choice && s_Editor && !s_Editor->reading ) s_Editor->SetProperty( id, choice->GetActiveItem() ); }
CEditorCanvas::CEditorCanvas( CHudEditor *editor ) : BaseClass( editor, "Canvas" ), owner( editor ), dragging( false ), resize( false ), panning( false ), zoom( 0 ), panX( 0 ), panY( 0 ), guideX(-1), guideY(-1) { SetMouseInputEnabled( true ); SetPaintBackgroundEnabled( false ); }
void CEditorCanvas::View( int &width, int &height )
{
    int sw, sh; vgui::surface()->GetScreenSize( sw, sh );
    if ( owner->liveView ) { width = sw; height = sh; panX = panY = 0; return; }
    width = zoom ? MAX(GetWide(),int(sw * HL2MPHudEditing::ZoomFactor(zoom))) : GetWide(); height = zoom ? MAX(GetTall(),int(sh * HL2MPHudEditing::ZoomFactor(zoom))) : GetTall();
    panX = clamp( panX, 0, MAX( 0, width - GetWide() ) ); panY = clamp( panY, 0, MAX( 0, height - GetTall() ) );
}
void CEditorCanvas::SetZoom( int level ) { if ( owner->liveView ) return; zoom = clamp( level, 0, 7 ); FocusSelection(); }
void CEditorCanvas::OnMouseWheeled(int delta) { if(delta && !owner->liveView) owner->Zoom(delta>0?1:-1); }
void CEditorCanvas::Snap(HL2MPHudElementStyle &p,int vw,int vh)
{
    guideX=guideY=-1; if(!owner->snapping || vgui::input()->IsKeyDown(KEY_LALT) || vgui::input()->IsKeyDown(KEY_RALT))return;
    const bool cross=owner->selected==HUD_CROSSHAIR;
    if ( resize && ( cross || p.fitContent ) ) return;
    int px, py, pw, ph; HL2MPHudBounds( owner->selected, vw, vh, px, py, pw, ph, !owner->liveView );
    float width = float(pw) / vw, height = float(ph) / vh;
    float bx=7,by=7,dx=0,dy=0;
    for(int i=-1;i<HUD_PARTS;++i)
    {
        if(i==owner->selected || (i>=0 && (!EditablePart(i) || !s_Layout.parts[i].visible)))continue;
        float tx[3]={0,.5f,1},ty[3]={0,.5f,1};
        if(i>=0) { int x,y,w,h; HL2MPHudBounds(i,vw,vh,x,y,w,h,!owner->liveView); for(int edge=0;edge<3;++edge) { tx[edge]=float(x+w*edge/2)/vw; ty[edge]=float(y+h*edge/2)/vh; } }
        for(int a=0;a<3;++a) for(int b=0;b<(cross||resize?1:3);++b)
        {
            HL2MPHudEditing::SnapCandidate(cross?p.x:resize?p.x+width:p.x+width*b/2,tx[a],float(vw),bx,dx,guideX);
            HL2MPHudEditing::SnapCandidate(cross?p.y:resize?p.y+height:p.y+height*b/2,ty[a],float(vh),by,dy,guideY);
        }
    }
    if(resize&&cross) { guideX=guideY=-1;return; }
    if(resize&&!cross) { p.w=clamp(p.w+dx,.025f,1-p.x);p.h=clamp(p.h+dy,.025f,1-p.y); }
    else if(!resize) { p.x=clamp(p.x+dx,0.f,cross?1.f:1-width);p.y=clamp(p.y+dy,0.f,cross?1.f:1-height); }
}
void CEditorCanvas::FocusSelection()
{
    if ( owner->liveView ) { panX = panY = 0; Repaint(); return; }
    int vw, vh, x, y, w, h; View( vw, vh ); HL2MPHudBounds( owner->selected, vw, vh, x, y, w, h, true );
    panX = owner->selected == HUD_KILLFEED ? x + w - GetWide() : x + w / 2 - GetWide() / 2;
    panY = y + h / 2 - GetTall() / 2; View( vw, vh ); Repaint();
}
void CEditorCanvas::Paint()
{
    if ( !owner->liveView ) Rect( 0, 0, GetWide(), GetTall(), Color( 14, 19, 21, 180 ) );
    if ( !cl_custom_hud.GetBool() )
    {
        if ( owner->liveView ) return;
        CenterText( HUD_HEALTH, "#HL2MP_HudDefault", 0, GetTall() / 2 - 32, GetWide(), 22, Color( 240, 180, 90, 255 ), true );
        CenterText( HUD_HEALTH, "#HL2MP_HudChooseLayout", 0, GetTall() / 2 + 6, GetWide(), MAX( 11, MIN( 15, GetWide() / 32 ) ), Color( 205, 205, 200, 255 ) );
        return;
    }
    int vw, vh; View( vw, vh );
    if ( CrosshairOnly() && !owner->liveView ) CenterText( HUD_CROSSHAIR, "#HL2MP_HudCrosshairOnlyHint", 0, 12, GetWide(), MAX( 10, MIN( 14, GetWide() / 52 ) ), Color( 205, 205, 200, 255 ) );
    vgui::surface()->DrawSetColor( Color( 140, 155, 160, 25 ) );
    if ( !owner->liveView ) for ( int i = 1; i < 10; ++i ) { vgui::surface()->DrawLine( i * vw / 10 - panX, 0, i * vw / 10 - panX, GetTall() ); vgui::surface()->DrawLine( 0, i * vh / 10 - panY, GetWide(), i * vh / 10 - panY ); }
    if(dragging) { vgui::surface()->DrawSetColor(Color(100,215,225,210)); if(guideX>=0) { int x=int(guideX*vw)-panX; vgui::surface()->DrawLine(x,0,x,GetTall()); } if(guideY>=0) { int y=int(guideY*vh)-panY; vgui::surface()->DrawLine(0,y,GetWide(),y); } }
    float scale = MIN( vw / 1280.0f, vh / 720.0f );
    for ( int i = 0; i < HUD_PARTS; ++i )
    {
        if ( !EditablePart( i ) ) continue;
        int x, y, w, h; HL2MPHudBounds( i, vw, vh, x, y, w, h, !owner->liveView ); x -= panX; y -= panY;
        if ( !owner->liveView ) DrawPart( i, x, y, w, h, scale, true );
        else
        {
            int mx, my; vgui::input()->GetCursorPos( mx, my ); ScreenToLocal( mx, my );
            bool hover = mx >= x && my >= y && mx < x+w && my < y+h;
            if ( i != owner->selected && !hover ) continue;
            Text( i, s_Labels[i], x + 4, MAX( 0, y - 18 ), MAX( 11, int( 13 * scale ) ), Color( 255, 183, 67, 255 ), true );
            if ( hover ) { vgui::surface()->DrawSetColor( Color( 255, 183, 67, 130 ) ); vgui::surface()->DrawOutlinedRect( x, y, x+w, y+h ); }
        }
        if ( i == owner->selected ) { vgui::surface()->DrawSetColor( Color( 240, 133, 42, 255 ) ); vgui::surface()->DrawOutlinedRect( x, y, x + w, y + h ); Rect( x + w - 8, y + h - 8, 8, 8, Color( 240, 133, 42, 255 ) ); }
    }
}
void CEditorCanvas::OnMousePressed( vgui::MouseCode code )
{
    vgui::input()->GetCursorPos( startX, startY ); ScreenToLocal( startX, startY );
    if ( code == MOUSE_RIGHT && !owner->liveView ) { panning = true; startPanX = panX; startPanY = panY; vgui::input()->SetMouseCapture( GetVPanel() ); return; }
    if ( code != MOUSE_LEFT ) return;
    int vw, vh; View( vw, vh );
    for ( int k = 0; k <= HUD_PARTS; ++k )
    {
        int i = k == 0 ? owner->selected : HUD_PARTS - k;
        if ( !EditablePart( i ) ) continue;
        int x, y, w, h; HL2MPHudBounds( i, vw, vh, x, y, w, h, !owner->liveView ); x -= panX; y -= panY;
        if ( startX < x || startY < y || startX > x + w || startY > y + h ) continue;
        owner->Select( i ); original = s_Layout.parts[i]; if ( original.fitContent ) { original.w = float(w) / vw; original.h = float(h) / vh; } originalCross=CrosshairFor(s_EditWeapon); resize = startX >= x + w - 12 && startY >= y + h - 12;
        dragging = true; vgui::input()->SetMouseCapture( GetVPanel() ); return;
    }
}
void CEditorCanvas::OnCursorMoved( int x, int y )
{
    int vw, vh; View( vw, vh );
    if ( panning ) { panX = startPanX + startX - x; panY = startPanY + startY - y; View( vw, vh ); Repaint(); return; }
    if ( !dragging ) return; owner->Edited(); HL2MPHudElementStyle &p = s_Layout.parts[owner->selected];
    float dx = float( x - startX ) / MAX( 1, vw ), dy = float( y - startY ) / MAX( 1, vh );
    if ( resize )
    {
        if ( owner->selected == HUD_CROSSHAIR ) { CrosshairConfig c=originalCross; c.scale=clamp(originalCross.scale*(1+dx/original.w),.5f,2.f);SetCrosshair(c);p.w=original.w*c.scale/originalCross.scale;p.h=original.h*c.scale/originalCross.scale; }
        else if ( p.fitContent ) p.textScale = clamp( original.textScale * ( 1 + dx / original.w ), .5f, 2.f );
        else { p.w = clamp( original.w + dx, .025f, MAX( .025f, 1.0f - p.x ) ); p.h = clamp( original.h + dy, .025f, MAX( .025f, 1.0f - p.y ) ); }
    }
    else { p.x = clamp( original.x + dx, 0.0f, owner->selected == HUD_CROSSHAIR ? 1.0f : 1.0f - original.w ); p.y = clamp( original.y + dy, 0.0f, owner->selected == HUD_CROSSHAIR ? 1.0f : 1.0f - original.h ); }
    Snap(p,vw,vh); Repaint();
}
void CEditorCanvas::OnMouseReleased( vgui::MouseCode code )
{
    if ( code == MOUSE_RIGHT && panning ) { panning = false; vgui::input()->SetMouseCapture( NULL ); }
    if ( code == MOUSE_LEFT && dragging ) { dragging = false; vgui::input()->SetMouseCapture( NULL ); owner->Checkpoint(); owner->BuildRows(); guideX=guideY=-1; }
}
void CEditorCanvas::OnMouseCaptureLost() { if(dragging)owner->Checkpoint();dragging=panning=false;guideX=guideY=-1; }
CHudEditor::CHudEditor() : BaseClass( NULL, "HUDEditor" ), selected( 0 ), section( 0 ), reading( false ), width( 0 ), height( 0 ), liveView( false ), controlsHidden( false )
{
    SetParent( enginevgui->GetPanel( PANEL_GAMEUIDLL ) ); MakePopup( false ); SetVisible( false ); SetMouseInputEnabled( true ); SetKeyBoardInputEnabled( true ); SetZPos( 500 );
    font = vgui::surface()->CreateFont(); title = new vgui::Label( this, "Title", "#HL2MP_HudEditor" ); hint = new vgui::Label( this, "Hint", "#HL2MP_HudDragHint" ); hint->SetWrap( true ); status = new vgui::Label( this, "Status", "" );
    canvas = new CEditorCanvas( this );
    HL2MPVoteUI_CreateEditorSample( this );
    closePrompt = new CHudClosePrompt( this ); toggleHeld = true;
    inspector = new CHudInspector( this ); inspector->SetSize( 0, 0 );
    viewButton = new CHL2MPSettingsButton( this, "#HL2MP_HudLiveView", "view" ); viewButton->ServerStyle();
    controlsButton = new CHL2MPSettingsButton( this, "#HL2MP_HudHideControls", "controls" ); controlsButton->ServerStyle(); controlsButton->SetZPos( 20 ); controlsButton->SetVisible( false );
    title->SetMouseInputEnabled( false );
    for ( int i = 0; i < 3; ++i ) { combos[i] = new CHL2MPDropdown( this, i == 0 ? "Element" : i == 1 ? "Preset" : "PreviewZoom" ); combos[i]->AddActionSignalTarget( this ); combos[i]->ServerStyle(); }
    for ( int i = 0; i < HUD_PARTS; ++i ) combos[0]->AddItem( s_Labels[i], NULL );
    combos[1]->AddItem( "#HL2MP_HudDefault", NULL ); combos[1]->AddItem( "#HL2MP_HudClassic", NULL ); combos[1]->AddItem( "#HL2MP_HudCompact", NULL ); combos[1]->AddItem( "#HL2MP_HudMinimal", NULL );
    combos[1]->AddItem( "#HL2MP_HudWorkingLayout", NULL );
    combos[1]->AddItem( "#HL2MP_HudCrosshairOnly", NULL );
    for ( int i = 1; i <= 5; ++i ) combos[1]->AddItem( HL2MPFormat( "Custom slot %d", i ), NULL );
    saveSlot = new CHL2MPDropdown( this, "SaveSlot" ); saveSlot->ServerStyle(); for ( int i = 1; i <= 5; ++i ) saveSlot->AddItem( HL2MPFormat( "Custom slot %d", i ), NULL ); saveSlot->ActivateItemByRow( 0 );
    tabs[0] = new CHL2MPSettingsButton( this, "#HL2MP_HudLayoutTab", "layout" ); tabs[1] = new CHL2MPSettingsButton( this, "#HL2MP_HudAppearanceTab", "appearance" );
    align[0] = new CHL2MPSettingsButton( this, "#HL2MP_HudCenterX", "center_x" ); align[1] = new CHL2MPSettingsButton( this, "#HL2MP_HudCenterY", "center_y" );
    for ( int i = 0; i < 2; ++i ) { tabs[i]->ServerStyle(); align[i]->ServerStyle(); }
    picker = new CColorPicker( this ); help=new CHudHelp(this); snapping=true; historyAt=0; historyGroup=-1; memset(keys,0,sizeof(keys));
    const char *toolLabels[]={"#HL2MP_HudUndo","#HL2MP_HudRedo","#HL2MP_HudSnap","#HL2MP_HudHelp"}; const char *toolCmds[]={"undo","redo","snap","help"};
    for(int i=0;i<4;++i) { toolbar[i]=new CHL2MPSettingsButton(this,toolLabels[i],toolCmds[i]); toolbar[i]->ServerStyle(); }
    combos[0]->ActivateItemByRow( 0 ); combos[1]->ActivateItemByRow( 0 );
    const char *zooms[]={"#HL2MP_HudPreviewFit","75%","100%","125%","150%","200%","300%","400%"}; for(int i=0;i<ARRAYSIZE(zooms);++i) combos[2]->AddItem(zooms[i],NULL); combos[2]->ActivateItemByRow( 0 );
    const char *names[] = { "#HL2MP_HudLoad", "#HL2MP_HudSave", "#HL2MP_HudResetElement", "#HL2MP_HudResetAll", "#HL2MP_HudVotePreview", "#HL2MP_UI_33bf9eeb20a9", "#HL2MP_Cancel" };
    const char *commands[] = { "load", "save", "reset_element", "reset_all", "vote_preview", "apply", "cancel" };
    for ( int i = 0; i < 7; ++i ) { buttons[i] = new CHL2MPSettingsButton( this, names[i], commands[i] ); buttons[i]->ServerStyle( i == 5 ); }
    rows = new vgui::PanelListPanel( this, "Properties" ); rows->SetFirstColumnWidth( 0 ); rows->SetVerticalBufferPixels( 2 ); HL2MPStyleScrollBar( rows->GetScrollbar() );
}
void CHudEditor::ParentControls( vgui::Panel *parent )
{
    title->SetParent( parent ); status->SetParent( parent ); hint->SetParent( parent ); rows->SetParent( parent ); saveSlot->SetParent( parent ); viewButton->SetParent( parent );
    // Reparenting inherits input flags; leave the title transparent to drags.
    title->SetMouseInputEnabled( false ); title->SetKeyBoardInputEnabled( false );
    for ( int i=0; i<3; ++i ) combos[i]->SetParent( parent );
    for ( int i=0; i<2; ++i ) { tabs[i]->SetParent( parent ); align[i]->SetParent( parent ); }
    for ( int i=0; i<7; ++i ) buttons[i]->SetParent( parent );
    for ( int i=0; i<4; ++i ) toolbar[i]->SetParent( parent );
}
void CHudEditor::SetLive( bool enabled )
{
    closePrompt->Dismiss(); toggleHeld = true;
    HL2MPHudChat_AttachEditorSample( this );
    HL2MPVoteUI_ClosePreview(); picker->Close( false ); if ( help->IsVisible() ) help->Close();
    for ( int i=0; i<3; ++i ) combos[i]->HideMenu(); saveSlot->HideMenu();
    canvas->dragging = canvas->panning = false;
    if ( vgui::input()->GetMouseCapture() ) vgui::input()->SetMouseCapture( NULL );
    liveView = enabled; controlsHidden = false;
    if ( enabled )
    {
        fromGame = true; engine->GetViewAngles( s_LiveViewAngles );
        SetParent( g_pClientMode->GetViewport() ); ParentControls( inspector );
        engine->ClientCmd_Unrestricted( "gameui_allowescape;gameui_hide" );
    }
    else
    {
        ParentControls( this ); SetParent( enginevgui->GetPanel( PANEL_GAMEUIDLL ) );
        engine->ClientCmd_Unrestricted( "gameui_activate;gameui_preventescape" );
    }
    inspector->SetVisible( enabled ); controlsButton->SetVisible( enabled );
    viewButton->SetText( enabled ? "#HL2MP_HudGridView" : "#HL2MP_HudLiveView" );
    title->SetText( enabled ? "#HL2MP_HudLiveEditor" : "#HL2MP_HudEditor" );
    openedAt = Plat_FloatTime(); InvalidateLayout( true );
    SetVisible( true ); SetMouseInputEnabled( true ); SetKeyBoardInputEnabled( true ); SetCursor( vgui::dc_arrow );
    vgui::input()->SetAppModalSurface( GetVPanel() ); RequestFocus(); MoveToFront();
}
void CHudEditor::Open( bool live )
{
    if ( live && !HL2MPHudEditor_LiveAvailable() ) { Msg( "%s\n", HL2MPText( "#HL2MP_HudLiveUnavailable" ) ); return; }
    if ( IsVisible() && live != liveView ) { SetLive( live ); return; }
    if ( IsVisible() ) { RequestClose(); return; } Load(); before = s_Layout; beforeMode = cl_custom_hud.GetInt(); fromGame = !enginevgui->IsGameUIVisible();
    reading = true; combos[1]->ActivateItemByRow( CurrentPreset() ); reading = false;
    history.Purge(); EditState initial={s_Layout,beforeMode}; history.AddToTail(initial); historyAt=0; historyGroup=-1;
    if ( CrosshairOnly() ) { selected = HUD_CROSSHAIR; section = 1; reading = true; combos[0]->ActivateItemByRow( selected ); reading = false; }
    SetLive( live ); BuildRows();
}
void CHudEditor::RequestClose( bool resume )
{
    if ( closePrompt->IsVisible() ) return;
    // Finish any active edit before comparing with the opening snapshot.
    picker->Close( true ); if ( help->IsVisible() ) help->Close(); HL2MPVoteUI_ClosePreview();
    for ( int i=0; i<3; ++i ) combos[i]->HideMenu(); saveSlot->HideMenu();
    canvas->dragging = canvas->panning = false;
    if ( vgui::input()->GetMouseCapture() ) vgui::input()->SetMouseCapture( NULL );
    if ( !HasChanges() ) { Close( false, resume ); return; }
    // A lost connection or an externally opened menu must not strand a modal
    // dialog under the hidden game viewport or discard the player's edits.
    if ( liveView && !HL2MPHudEditor_LiveAvailable() ) SetLive( false );
    else if ( liveView && enginevgui->IsGameUIVisible() ) engine->ClientCmd_Unrestricted( "gameui_allowescape;gameui_hide" );
    else if ( !liveView && !enginevgui->IsGameUIVisible() ) engine->ClientCmd_Unrestricted( "gameui_activate;gameui_preventescape" );
    openedAt = Plat_FloatTime(); closePrompt->Open( resume );
}
void CHudEditor::CheckSession()
{
    if ( !IsVisible() ) return;
    if ( liveView && !HL2MPHudEditor_LiveAvailable() )
    {
        if ( HasChanges() ) SetLive( false );
        RequestClose( false ); return;
    }
    if ( Plat_FloatTime() - openedAt < .5 ) return;
    // Escape can be handled by the engine before VGUI receives it. This check
    // must run even when the pause menu hides the editor's viewport/OnThink.
    if ( liveView && enginevgui->IsGameUIVisible() )
    {
        engine->ClientCmd_Unrestricted( "gameui_allowescape;gameui_hide" );
        if ( closePrompt->IsVisible() ) closePrompt->Dismiss(); else RequestClose();
    }
    else if ( !liveView && !enginevgui->IsGameUIVisible() ) RequestClose( false );
}
void CHudEditor::Close( bool apply, bool resume )
{
    closePrompt->Dismiss();
    HL2MPVoteUI_ClosePreview();
    picker->Close( false ); if(help->IsVisible()) help->Close(); saveSlot->HideMenu();
    if ( apply && !SaveLayout( "cfg/hud_layout.res" ) ) { status->SetText( "#HL2MP_HudSaveFailed" ); if ( liveView ) title->SetText( "#HL2MP_HudSaveFailed" ); return; }
    if ( !apply ) { s_Layout = before; cl_custom_hud.SetValue( beforeMode ); }
    for ( int i = 0; i < 3; ++i ) combos[i]->HideMenu();
    canvas->dragging = canvas->panning = false;
    vgui::VPANEL capture = vgui::input()->GetMouseCapture();
    if ( capture && ( capture == GetVPanel() || vgui::ipanel()->HasParent( capture, GetVPanel() ) ) ) vgui::input()->SetMouseCapture( NULL );
    if ( vgui::input()->GetAppModalSurface() == GetVPanel() ) vgui::input()->ReleaseAppModalSurface();
    SetVisible( false );
    const bool wasLive = liveView; liveView = false; inspector->SetVisible( false ); controlsButton->SetVisible( false );
    ParentControls( this ); SetParent( enginevgui->GetPanel( PANEL_GAMEUIDLL ) );
    if ( wasLive ) { engine->ClientCmd_Unrestricted( "gameui_allowescape" ); if ( resume && engine->IsInGame() ) engine->ClientCmd_Unrestricted( "gameui_hide" ); }
    else if ( fromGame ) engine->ClientCmd_Unrestricted( "gameui_allowescape;gameui_hide" );
    else if ( !HL2MPOptions_RestoreFocus() ) engine->ClientCmd_Unrestricted( "gameui_allowescape" );
}
void CHudEditor::OnThink()
{
    if ( !IsVisible() ) return; int sw, sh; vgui::surface()->GetScreenSize( sw, sh ); if ( sw != width || sh != height ) InvalidateLayout( true );
    Shortcuts(); if(!vgui::input()->IsMouseDown(MOUSE_LEFT)) historyGroup=-1;
    for(int i=0;i<rows->GetItemCount();++i) { CEditorRow *row=dynamic_cast<CEditorRow *>(rows->GetItemPanel(rows->GetItemIDFromRow(i))); if(row&&row->id==26&&row->choice&&row->choice->GetActiveItem()!=Property(26)) { reading=true;row->choice->ActivateItemByRow(Property(26));reading=false; } }
    toolbar[0]->SetEnabled(historyAt>0); toolbar[1]->SetEnabled(historyAt+1<history.Count()); toolbar[2]->Active(snapping);
    const bool custom = cl_custom_hud.GetBool();
    rows->SetVisible( custom ); canvas->SetMouseInputEnabled( custom || liveView ); combos[0]->SetEnabled( custom && !CrosshairOnly() ); saveSlot->SetEnabled( custom );
    for ( int i = 0; i < 2; ++i ) { tabs[i]->Active( section == i ); tabs[i]->SetVisible( custom ); align[i]->SetEnabled( custom ); align[i]->SetVisible( custom && section == 0 ); }
    buttons[0]->SetVisible( false ); buttons[1]->SetEnabled( custom ); buttons[2]->SetEnabled( custom );
    combos[2]->SetVisible( !liveView ); saveSlot->SetVisible( !liveView ); buttons[1]->SetVisible( !liveView );
    status->SetVisible( !liveView );
    for ( int i=2; i<=4; ++i ) buttons[i]->SetVisible( !liveView );
    inspector->SetVisible( liveView && !controlsHidden ); controlsButton->SetVisible( liveView );
    viewButton->SetEnabled( liveView || HL2MPHudEditor_LiveAvailable() );
    hint->SetVisible( liveView && !custom && !controlsHidden );
}
void CHudEditor::PerformLayout()
{
    BaseClass::PerformLayout(); vgui::surface()->GetScreenSize( width, height ); SetBounds( 0, 0, width, height ); float s = MAX( .65f, MIN( width / 1280.0f, height / 720.0f ) );
    vgui::surface()->SetFontGlyphSet( font, "Tahoma", MAX( 11, int( 15 * s ) ), 500, 0, 0, vgui::ISurface::FONTFLAG_ANTIALIAS );
    if ( liveView ) { LayoutLive(); return; }
    int margin = int( 20 * s ), side = int( 330 * s ), leftW = width - side - margin * 3, top = int( 116 * s );
    title->SetBounds( margin, margin, leftW / 3 - 8, int( 32 * s ) ); title->SetFont( font ); title->SetFgColor( Color( 238, 133, 40, 255 ) );
    viewButton->SetBounds( margin + leftW / 3, margin, leftW / 3 - 8, int( 32 * s ) ); viewButton->SetFont( font );
    combos[2]->SetBounds( margin + leftW * 2 / 3, margin, leftW / 3, int( 32 * s ) );
    combos[0]->SetBounds( width - side - margin, int( 65 * s ), side, int( 38 * s ) ); combos[1]->SetBounds( margin, int( 65 * s ), leftW / 2, int( 38 * s ) );
    for ( int i = 0; i < 3; ++i ) combos[i]->Style( font, s );
    saveSlot->SetBounds( margin + leftW / 2 + 8, int( 65 * s ), leftW / 4 - 8, int( 38 * s ) ); saveSlot->Style( font, s );
    buttons[1]->SetBounds( margin + leftW * 3 / 4 + 4, int( 65 * s ), leftW / 4 - 4, int( 38 * s ) );
    for ( int i = 0; i < 2; ++i ) { tabs[i]->SetBounds( width - side - margin + i * ( side / 2 + 3 ), top, side / 2 - 3, int( 32 * s ) ); tabs[i]->SetFont( font ); align[i]->SetBounds( width - side - margin + i * ( side / 2 + 3 ), top + int( 40 * s ), side / 2 - 3, int( 36 * s ) ); align[i]->SetFont( font ); }
    int ch = MIN( int( leftW * float( height ) / width ), height - top - int( 130 * s ) ); int cw = int( ch * float( width ) / height );
    canvas->SetBounds( margin + ( leftW - cw ) / 2, top + int( 12 * s ), cw, ch );
    hint->SetBounds( margin, top + ch + int( 24 * s ), leftW, int( 48 * s ) ); hint->SetFont( font ); hint->SetFgColor( Color( 180, 185, 185, 255 ) );
    hint->GetTextImage()->SetSize( hint->GetWide(), hint->GetTall() ); hint->GetTextImage()->RecalculateNewLinePositions();
    status->SetBounds( width - side - margin, margin, side, int( 32 * s ) ); status->SetFont( font ); status->SetFgColor( Color( 130, 215, 130, 255 ) );
    int rowsTop = top + int( ( section == 0 ? 84 : 42 ) * s ); rows->SetBounds( width - side - margin, rowsTop, side, height - rowsTop - int( 80 * s ) );
    for ( int i = 0; i < rows->GetItemCount(); ++i ) { vgui::Panel *row = rows->GetItemPanel( rows->GetItemIDFromRow( i ) ); if ( row ) { row->SetTall( int( (!Q_strcmp(row->GetName(),"CrosshairGallery")?196:62) * s ) ); row->InvalidateLayout( true ); } }
    for ( int i = 0; i < 7; ++i ) buttons[i]->SetFont( font );
    int toolW=(leftW-18)/4; for(int i=0;i<4;++i) { toolbar[i]->SetBounds(margin+i*(toolW+6),height-int(100*s),toolW,int(32*s)); toolbar[i]->SetFont(font); }
    int by = height - int( 58 * s ), bw = ( width - margin * 6 ) / 5;
    for ( int i = 2; i < 7; ++i ) buttons[i]->SetBounds( margin + ( i - 2 ) * ( bw + margin ), by, bw, int( 40 * s ) );
}
void CHudEditor::LayoutLive()
{
    const float s = MAX( .65f, MIN( width / 1280.f, height / 720.f ) );
    const int w = MIN( width - 24, MAX( 280, int( 340*s ) ) ), h = MIN( height - 24, int( 550*s ) ), pad = int( 12*s );
    const int row = MAX( int(32*s), vgui::surface()->GetFontTall(font)*2+4 ), gap = MAX( 4, int( 6*s ) );
    int ix,iy; inspector->GetPos( ix, iy ); if ( !inspector->GetWide() ) { ix = (width-w)/2; iy = (height-h)/2; }
    ix=clamp(ix,0,MAX(0,width-w)); iy=clamp(iy,0,MAX(0,height-h)); inspector->SetBounds( ix, iy, w, h );
    canvas->SetBounds( 0, 0, width, height );
    title->SetBounds( pad, 4, w-pad*2, row ); title->SetFont( font ); title->SetFgColor( HL2MPServerTheme::Orange() );
    combos[1]->SetBounds( pad, row+gap, w-pad*2, row ); combos[1]->Style( font, s );
    combos[0]->SetBounds( pad, 2*(row+gap), w-pad*2, row ); combos[0]->Style( font, s );
    const int half=(w-pad*2-gap)/2, tabY=3*(row+gap);
    for(int i=0;i<2;++i) { tabs[i]->SetBounds(pad+i*(half+gap),tabY,half,row);tabs[i]->SetFont(font);align[i]->SetBounds(pad+i*(half+gap),tabY+row+gap,half,row);align[i]->SetFont(font); }
    const int top=tabY+(section?1:2)*(row+gap), footer=h-pad-3*row-2*gap;
    rows->SetBounds( pad, top, w-pad*2, MAX(row,footer-top-gap) );
    for(int i=0;i<rows->GetItemCount();++i) { vgui::Panel *p=rows->GetItemPanel(rows->GetItemIDFromRow(i)); if(p) { p->SetTall(int((!Q_strcmp(p->GetName(),"CrosshairGallery")?196:62)*s));p->InvalidateLayout(true); } }
    hint->SetText( "#HL2MP_HudChooseLayout" ); hint->SetBounds( pad, top, w-pad*2, MAX(row,footer-top-gap) ); hint->SetFont(font);hint->SetFgColor(HL2MPServerTheme::Text());
    hint->GetTextImage()->SetSize(hint->GetWide(),hint->GetTall());hint->GetTextImage()->RecalculateNewLinePositions();
    const int toolW=(w-pad*2-gap*3)/4;
    for(int i=0;i<4;++i) { toolbar[i]->SetBounds(pad+i*(toolW+gap),footer,toolW,row);toolbar[i]->SetFont(font); }
    for(int i=0;i<2;++i) { buttons[5+i]->SetBounds(pad+i*(half+gap),footer+row+gap,half,row);buttons[5+i]->SetFont(font); }
    viewButton->SetBounds(pad,footer+2*(row+gap),half,row);viewButton->SetFont(font);
    controlsButton->SetText(controlsHidden?"#HL2MP_HudShowControls":"#HL2MP_HudHideControls");controlsButton->SetFont(font);
    controlsButton->SetBounds(controlsHidden?ix:ix+pad+half+gap,controlsHidden?iy:iy+footer+2*(row+gap),controlsHidden?w:half,row);
    status->SetVisible( false );
}
void CHudEditor::PaintBackground() { if ( liveView ) return; Rect( 0, 0, GetWide(), GetTall(), Color( 20, 23, 25, 242 ) ); Rect( 0, 0, GetWide(), 2, Color( 231, 121, 32, 255 ) ); }
void CHudEditor::OnKeyCodeTyped( vgui::KeyCode key )
{
    if ( key == KEY_ESCAPE )
    {
        for(int i=0;i<3;++i) if(combos[i]->IsDropdownVisible()) { combos[i]->HideMenu();return; }
        if(saveSlot->IsDropdownVisible()) { saveSlot->HideMenu();return; }
        RequestClose();return;
    }
    const char *binding = engine->Key_BindingForKey( (ButtonCode_t)key );
    if ( binding && !Q_strcmp( binding, liveView ? "hud_editor_live" : "hud_editor" ) ) { RequestClose();return; }
    BaseClass::OnKeyCodeTyped(key);
}
void CHudEditor::Select( int part ) { selected = CrosshairOnly() ? HUD_CROSSHAIR : clamp( part, 0, HUD_PARTS - 1 ); reading = true; combos[0]->ActivateItemByRow( selected ); reading = false; BuildRows(); }
void CHudEditor::ComboChanged( vgui::Panel *panel )
{
    if ( reading || !IsVisible() ) return;
    if ( panel == combos[0] ) { selected = CrosshairOnly() ? HUD_CROSSHAIR : clamp( combos[0]->GetActiveItem(), 0, HUD_PARTS - 1 ); BuildRows(); if ( !canvas->dragging ) canvas->FocusSelection(); }
    else if ( panel == combos[1] ) { if ( combos[1]->GetActiveItem() != PRESET_WORKING || !HL2MPHudCustomEnabled() ) ChoosePreset( combos[1]->GetActiveItem() ); }
    else if ( panel == combos[2] ) canvas->SetZoom( combos[2]->GetActiveItem() );
}
int CHudEditor::Property( int id )
{
    const HL2MPHudElementStyle &p = s_Layout.parts[selected];
    if(id==27)return p.fitContent; if(id==25)return s_EditWeapon; if(id==26)return s_EditWeapon&&s_Layout.overrides[s_EditWeapon-1];
    if(selected==HUD_CROSSHAIR) { CrosshairConfig c=CrosshairFor(s_EditWeapon); switch(id) { case 4:return int(c.scale*100); case 8:return int(c.color.a()*100.f/255+.5f); case 18:return c.shape;case 19:return c.gap;case 20:return c.length;case 21:return c.thickness;case 22:return c.outline;case 23:return c.dot; } }
    if ( id == 0 ) return int( p.x * 100 ); if ( id == 1 ) return int( p.y * 100 ); if ( id == 2 ) return int( p.w * 100 ); if ( id == 3 ) return int( p.h * 100 ); if ( id == 4 ) return int( p.textScale * 100 );
    if ( id == 8 || id == 12 ) return int( ( id == 8 ? p.foreground.a() : p.background.a() ) * 100.f / 255 + .5f );
    if ( id < 9 ) return p.foreground[id - 5]; if ( id < 13 ) return p.background[id - 9];
    switch ( id ) { case 13:return p.visible; case 14:return p.labels; case 15:return p.numbers; case 16:return p.bars; case 17:return p.font; case 18:return s_Layout.crossShape; case 19:return s_Layout.crossGap; case 20:return s_Layout.crossLength; case 21:return s_Layout.crossThickness; case 22:return s_Layout.crossOutline; case 23:return s_Layout.crossDot; case 24:return s_Layout.lowHealth; } return 0;
}
void CHudEditor::SetProperty( int id, int v )
{
    if ( Property( id ) == v || !EditablePart( selected ) ) return;
    if(id==25) { s_EditWeapon=clamp(v,0,ARRAYSIZE(s_Weapons)); BuildRows(); canvas->Repaint();return; }
    Edited(); HL2MPHudElementStyle &p = s_Layout.parts[selected];
    if ( id == 27 ) { p.fitContent = v != 0; Checkpoint(); BuildRows(); canvas->Repaint(); return; }
    if(id==26) { if(v) SetCrosshair(CrosshairFor(s_EditWeapon)); else if(s_EditWeapon) s_Layout.overrides[s_EditWeapon-1]=false; Checkpoint();BuildRows();return; }
    if(selected==HUD_CROSSHAIR && (id==4||id==8||(id>=18&&id<=23)))
    {
        CrosshairConfig c=CrosshairFor(s_EditWeapon); switch(id) {case 4:c.scale=v/100.f;break;case 8:c.color[3]=int(v*2.55f+.5f);break;case 18:c.shape=v;break;case 19:c.gap=v;break;case 20:c.length=v;break;case 21:c.thickness=v;break;case 22:c.outline=v!=0;break;case 23:c.dot=v!=0;break;} SetCrosshair(c);Checkpoint(id);canvas->Repaint();return;
    }
    if ( id == 0 ) p.x = v / 100.0f; else if ( id == 1 ) p.y = v / 100.0f; else if ( id == 2 ) p.w = v / 100.0f; else if ( id == 3 ) p.h = v / 100.0f; else if ( id == 4 ) p.textScale = v / 100.0f;
    else if ( id == 8 || id == 12 ) { if ( id == 8 ) p.foreground[3] = int( v * 2.55f + .5f ); else p.background[3] = int( v * 2.55f + .5f ); }
    else if ( id < 9 ) p.foreground[id - 5] = v; else if ( id < 13 ) p.background[id - 9] = v;
    else switch ( id ) { case 13:p.visible = !CanHide( selected ) || v != 0;break; case 14:p.labels = v != 0;break; case 15:p.numbers = v != 0;break; case 16:p.bars = v != 0;break; case 17:p.font = v;break; case 18:s_Layout.crossShape = v;break; case 19:s_Layout.crossGap = v;break; case 20:s_Layout.crossLength = v;break; case 21:s_Layout.crossThickness = v;break; case 22:s_Layout.crossOutline = v != 0;break; case 23:s_Layout.crossDot = v != 0;break; case 24:s_Layout.lowHealth = v != 0;break; }
    Checkpoint(id); canvas->Repaint();
}
void CHudEditor::BuildRows()
{
    reading = true; rows->DeleteAllItems();
    if ( section == 1 )
    {
        if(selected==HUD_CROSSHAIR)
        {
            rows->AddItem(NULL,new CEditorRow(rows,25,"#HL2MP_CrossWeapon",0,ARRAYSIZE(s_Weapons),s_EditWeapon));
            if(s_EditWeapon) rows->AddItem(NULL,new CEditorRow(rows,26,"#HL2MP_CrossOverride",0,1,Property(26)));
            rows->AddItem(NULL,new CShapePalette(rows));
            rows->AddItem(NULL,new CEditorRow(rows,18,properties[18],0,ARRAYSIZE(s_Shapes)-1,Property(18)));
        }
        CColorRow *fg = new CColorRow( rows, false ); fg->SetFont( font ); fg->SetTall( 62 ); rows->AddItem( NULL, fg );
        if ( selected != HUD_CROSSHAIR ) { CColorRow *bg = new CColorRow( rows, true ); bg->SetFont( font ); bg->SetTall( 62 ); rows->AddItem( NULL, bg ); }
    }
    const int layout[] = { 0, 1, 27, 2, 3 };
    const int appearance[] = { 4, 17, 8, 12, 13, 14, 15, 16, 18, 19, 20, 21, 22, 23, 24 };
    const int *list = section ? appearance : layout; const int count = section ? ARRAYSIZE( appearance ) : ARRAYSIZE( layout );
    for ( int n = 0; n < count; ++n )
    {
        int i = list[n];
        if(i==18)continue;
        if ( i == 27 && selected > HUD_ALTAMMO ) continue;
        if ( ( i == 2 || i == 3 ) && s_Layout.parts[selected].fitContent ) continue;
        if ( i >= 18 && i <= 23 && selected != HUD_CROSSHAIR ) continue;
        if ( i == 24 && selected != HUD_HEALTH ) continue;
        if ( i == 13 && !CanHide( selected ) ) continue;
        if ( selected >= HUD_CROSSHAIR && i >= 14 && i <= 16 && !( selected == HUD_MATCH && i == 14 ) ) continue;
        if ( selected == HUD_CROSSHAIR && ( i == 12 || i == 17 || i == 2 || i == 3 ) ) continue;
        int low = i == 2 || i == 3 ? 2 : i == 4 ? 50 : i == 20 || i == 21 ? 1 : 0;
        int high = i < 4 || i == 8 || i == 12 ? 100 : i == 4 ? 200 : i < 13 ? 255 : i < 17 ? 1 : i == 17 ? ARRAYSIZE( s_FontFaces ) - 1 : i == 18 ? 3 : i == 19 ? 24 : i == 20 ? 32 : i == 21 ? 8 : 1;
        CEditorRow *row = new CEditorRow( rows, i, properties[i], low, high, Property( i ) ); row->SetTall( 62 ); rows->AddItem( NULL, row );
    }
    reading = false; InvalidateLayout( true );
}
Color *CHudEditor::EditColor(bool background)
{
    if(background)return &s_Layout.parts[selected].background;
    if(selected==HUD_CROSSHAIR && s_EditWeapon) { SetCrosshair(CrosshairFor(s_EditWeapon));return &s_Layout.weapons[s_EditWeapon-1].color; }
    return &s_Layout.parts[selected].foreground;
}
void CHudEditor::Checkpoint(int group)
{
    if(!history.Count())return;
    const EditState &old=history[historyAt]; if(old.mode==cl_custom_hud.GetInt() && SameLayout(old.layout,s_Layout))return;
    while(history.Count()>historyAt+1)history.Remove(history.Count()-1);
    EditState now={s_Layout,cl_custom_hud.GetInt()};
    if(group>=0 && historyGroup==group && historyAt>0) history[historyAt]=now;
    else { history.AddToTail(now); if(history.Count()>65)history.Remove(0);historyAt=history.Count()-1; }
    historyGroup=vgui::input()->IsMouseDown(MOUSE_LEFT)?group:-1;
}
void CHudEditor::History(int direction)
{
    if(canvas->dragging)return; int target=historyAt+direction; if(target<0||target>=history.Count())return;
    historyAt=target; s_Layout=history[target].layout; cl_custom_hud.SetValue(history[target].mode);historyGroup=-1;
    reading=true;combos[1]->ActivateItemByRow(CurrentPreset()); if(CrosshairOnly()) { selected=HUD_CROSSHAIR; combos[0]->ActivateItemByRow(selected); } reading=false;BuildRows();canvas->FocusSelection();canvas->Repaint();
}
void CHudEditor::Zoom(int delta)
{
    if(canvas->dragging || liveView)return; int level=clamp(canvas->zoom+delta,0,7); reading=true;combos[2]->ActivateItemByRow(level);reading=false;canvas->SetZoom(level);
}
void CHudEditor::Shortcuts()
{
    bool toggleDown = false;
    for ( int key = KEY_FIRST + 1; key <= KEY_LAST; ++key ) if ( vgui::input()->IsKeyDown( (vgui::KeyCode)key ) )
    {
        const char *binding = engine->Key_BindingForKey( (ButtonCode_t)key );
        if ( binding && !Q_strcmp( binding, liveView ? "hud_editor_live" : "hud_editor" ) ) toggleDown = true;
    }
    const bool toggle = toggleDown && !toggleHeld; toggleHeld = toggleDown;
    if ( toggle && vgui::input()->GetAppModalSurface() == GetVPanel() ) { RequestClose(); return; }
    bool ctrl=vgui::input()->IsKeyDown(KEY_LCONTROL)||vgui::input()->IsKeyDown(KEY_RCONTROL);
    bool down[]={ctrl&&vgui::input()->IsKeyDown(KEY_Z),ctrl&&vgui::input()->IsKeyDown(KEY_Y),vgui::input()->IsKeyDown(KEY_EQUAL)||vgui::input()->IsKeyDown(KEY_PAD_PLUS),vgui::input()->IsKeyDown(KEY_MINUS)||vgui::input()->IsKeyDown(KEY_PAD_MINUS)};
    bool active=vgui::input()->GetAppModalSurface()==GetVPanel(); for(int i=0;i<3;++i)active=active&&!combos[i]->IsDropdownVisible(); active=active&&!saveSlot->IsDropdownVisible();
    for(int i=0;i<4;++i) { if(active&&down[i]&&!keys[i]) { if(i<2)History(i?1:-1);else Zoom(i==2?1:-1); }keys[i]=down[i]; }
}
void CHudEditor::Edited()
{
    if ( !CrosshairOnly() ) cl_custom_hud.SetValue( 1 );
    if ( combos[1]->GetActiveItem() != CurrentPreset() ) { reading = true; combos[1]->ActivateItemByRow( CurrentPreset() ); reading = false; }
}
void CHudEditor::ChoosePreset( int preset )
{
    if ( preset == 0 ) cl_custom_hud.SetValue( 0 );
    else if ( preset <= 3 ) { Defaults( preset - 1 ); cl_custom_hud.SetValue( 1 ); }
    else if ( preset == PRESET_CROSSHAIR ) { cl_custom_hud.SetValue( 2 ); section = 1; Select( HUD_CROSSHAIR ); }
    else if ( preset >= PRESET_FIRST_SLOT )
    {
        if ( !ReadLayout( VarArgs( "cfg/hud_profile_%d.res", clamp( preset - PRESET_FIRST_SLOT + 1, 1, 5 ) ), true ) ) { status->SetText( "#HL2MP_HudSlotEmpty" ); reading = true; combos[1]->ActivateItemByRow( CurrentPreset() ); reading = false; return; }
        if ( CrosshairOnly() ) { section = 1; Select( HUD_CROSSHAIR ); }
    }
    else cl_custom_hud.SetValue( 1 );
    Checkpoint(); status->SetText( "" ); BuildRows(); canvas->FocusSelection(); canvas->Repaint();
}

void CHudEditor::OnCommand( const char *command )
{
    if(!Q_strcmp(command,"view")) { if(liveView || HL2MPHudEditor_LiveAvailable())SetLive(!liveView);return; }
    if(!Q_strcmp(command,"controls")) { controlsHidden=!controlsHidden;InvalidateLayout(true);return; }
    if(!Q_strcmp(command,"undo")) { History(-1);return; } if(!Q_strcmp(command,"redo")) { History(1);return; }
    if(!Q_strcmp(command,"snap")) { snapping=!snapping;return; } if(!Q_strcmp(command,"help")) { help->Open();return; }
    if ( !Q_strcmp( command, "apply" ) ) { Close( true ); return; }
    if ( !Q_strcmp( command, "cancel" ) ) { RequestClose(); return; }
    if ( !Q_strcmp( command, "layout" ) || !Q_strcmp( command, "appearance" ) ) { section = !Q_strcmp( command, "appearance" ); BuildRows(); return; }
    if ( !Q_strcmp( command, "color_fg" ) || !Q_strcmp( command, "color_bg" ) ) { picker->Open( !Q_strcmp( command, "color_bg" ) ); return; }
    if ( !Q_strcmp( command, "vote_preview" ) ) { HL2MPVoteUI_Preview( this ); return; }
    if ( !Q_strcmp( command, "center_x" ) || !Q_strcmp( command, "center_y" ) )
    {
        Edited(); HL2MPHudElementStyle &p = s_Layout.parts[selected];
        int sw, sh, x, y, w, h; vgui::surface()->GetScreenSize( sw, sh ); HL2MPHudBounds( selected, sw, sh, x, y, w, h );
        if ( !Q_strcmp( command, "center_x" ) ) p.x = selected == HUD_CROSSHAIR ? .5f : ( 1 - float(w) / sw ) / 2;
        else p.y = selected == HUD_CROSSHAIR ? .5f : ( 1 - float(h) / sh ) / 2;
        Checkpoint(); canvas->FocusSelection(); BuildRows(); return;
    }
    if ( !Q_strcmp( command, "reset_all" ) ) { Defaults( 0 ); reading = true; combos[1]->ActivateItemByRow( 0 ); reading = false; ChoosePreset( 0 ); return; }
    else if ( !Q_strcmp( command, "reset_element" ) ) { Edited(); Layout old = s_Layout; Defaults( 0 ); Layout fresh = s_Layout; s_Layout = old; s_Layout.parts[selected] = fresh.parts[selected]; if ( selected == HUD_CROSSHAIR && s_EditWeapon ) { s_Layout.parts[selected]=old.parts[selected];s_Layout.overrides[s_EditWeapon-1]=false; } else if ( selected == HUD_CROSSHAIR ) { s_Layout.crossShape = fresh.crossShape; s_Layout.crossGap = fresh.crossGap; s_Layout.crossLength = fresh.crossLength; s_Layout.crossThickness = fresh.crossThickness; s_Layout.crossOutline = fresh.crossOutline; s_Layout.crossDot = fresh.crossDot; } if ( selected == HUD_HEALTH ) s_Layout.lowHealth = fresh.lowHealth; }
    else if ( !Q_strcmp( command, "save" ) ) status->SetText( SaveLayout( VarArgs( "cfg/hud_profile_%d.res", clamp( saveSlot->GetActiveItem() + 1, 1, 5 ) ) ) ? "#HL2MP_HudSaved" : "#HL2MP_HudSaveFailed" );
    else { BaseClass::OnCommand( command ); return; }
    Checkpoint(); BuildRows();
}
}
const HL2MPHudElementStyle &HL2MPHudStyle( int part ) { Load(); return ( CrosshairOnly() && part != HUD_CROSSHAIR ? s_DefaultLayout : s_Layout ).parts[clamp( part, 0, HUD_PARTS - 1 )]; }
void HL2MPHudBounds( int part, int sw, int sh, int &x, int &y, int &w, int &h, bool preview )
{
    const HL2MPHudElementStyle &p = HL2MPHudStyle( part ); w = clamp( int( p.w * sw ), 8, sw ); h = clamp( int( p.h * sh ), 8, sh );
    if ( part <= HUD_ALTAMMO && p.fitContent ) { StatMetrics m = MeasureStat( part, MIN( sw / 1280.f, sh / 720.f ), preview ); w = clamp( m.width, 8, sw ); h = clamp( m.height, 8, sh ); }
    x = clamp( int( p.x * sw ) - ( part == HUD_CROSSHAIR ? w / 2 : 0 ), 0, sw - w ); y = clamp( int( p.y * sh ) - ( part == HUD_CROSSHAIR ? h / 2 : 0 ), 0, sh - h );
}
vgui::HFont HL2MPHudFont( int part, int pixels, bool bold )
{
    int face = HL2MPHudStyle( part ).font; pixels = clamp( pixels, 8, 96 );
    FOR_EACH_VEC( s_Fonts, i ) if ( s_Fonts[i].face == face && s_Fonts[i].pixels == pixels && s_Fonts[i].bold == bold ) return s_Fonts[i].handle;
    Font f = { vgui::surface()->CreateFont(), face, pixels, bold };
    vgui::surface()->SetFontGlyphSet( f.handle, s_FontFaces[face], pixels, bold ? 700 : 400, 0, 0, vgui::ISurface::FONTFLAG_ANTIALIAS | vgui::ISurface::FONTFLAG_DROPSHADOW ); s_Fonts.AddToTail( f ); return f.handle;
}
bool HL2MPHudCustomEnabled() { return cl_custom_hud.GetInt() == 1; }
bool HL2MPHudCustomCrosshairEnabled() { return cl_custom_hud.GetBool(); }
bool HL2MPHudReplaces( const char *name )
{
    if ( !HL2MPHudCustomEnabled() ) return false;
    const char *names[] = { "CHudHealth", "CHudBattery", "CHudAmmo", "CHudSecondaryAmmo", "CHudSuitPower", "CHudServerInfo" };
    for ( int i = 0; i < ARRAYSIZE( names ); ++i ) if ( !Q_strcmp( name, names[i] ) ) return true; return false;
}
bool HL2MPHudDrawCrosshair( int centerX, int centerY )
{
    if ( !HL2MPHudCustomCrosshairEnabled() ) return false; const HL2MPHudElementStyle &p = HL2MPHudStyle( HUD_CROSSHAIR );
    int sw, sh; vgui::surface()->GetScreenSize( sw, sh );
    int index=0; C_BasePlayer *player=GetHudPlayer(); C_BaseCombatWeapon *weapon=player?player->GetActiveWeapon():NULL;
    if(weapon) for(int i=0;i<ARRAYSIZE(s_Weapons);++i) if(!Q_stricmp(weapon->GetName(),s_Weapons[i])) { index=i+1; break; }
    Crosshair( centerX + int( ( p.x - .5f ) * sw ), centerY + int( ( p.y - .5f ) * sh ), MIN( sw / 1280.0f, sh / 720.0f ), CrosshairFor(index) ); return true;
}
void HL2MPHudCustomization_Init() { Load(); if ( !s_Editor ) s_Editor = new CHudEditor; }
void HL2MPHudEditor_Open() { if ( s_Editor ) s_Editor->Open(); }
bool HL2MPHudEditor_LiveAvailable()
{
    return engine->IsInGame() && !engine->IsLevelMainMenuBackground() && !engine->IsPlayingDemo() && !engine->IsHLTV() &&
        !engine->IsDrawingLoadingImage() && HL2MPRules() && HL2MPRules()->IsListenServer() && C_BasePlayer::GetLocalPlayer();
}
void HL2MPHudEditor_OpenLive() { if ( s_Editor ) s_Editor->Open( true ); }
bool HL2MPHudEditor_IsLive() { return s_Editor && s_Editor->IsLive(); }
bool HL2MPHudEditor_FilterInput( CUserCmd *cmd )
{
    if ( !HL2MPHudEditor_IsLive() ) return false;
    cmd->forwardmove = cmd->sidemove = cmd->upmove = 0;
    cmd->buttons = cmd->impulse = cmd->weaponselect = cmd->weaponsubtype = 0;
    cmd->viewangles = s_LiveViewAngles;
    return true;
}
CON_COMMAND_F( hud_editor, "Open the visual HUD editor.", FCVAR_CLIENTDLL ) { HL2MPHudEditor_Open(); }
CON_COMMAND_F( hud_editor_live, "Edit the actual HUD while connected to a listen server.", FCVAR_CLIENTDLL ) { HL2MPHudEditor_OpenLive(); }
