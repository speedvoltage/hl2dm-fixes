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
#include "tier0/memdbgon.h"

static ConVar cl_custom_hud( "cl_custom_hud", "1", FCVAR_ARCHIVE, "Use the customizable HUD.", true, 0, true, 1 );
namespace
{
const char *s_Names[HUD_PARTS] = { "health", "armor", "ammo", "altammo", "power", "crosshair", "chat", "killfeed", "match", "vote" };
const char *s_Labels[HUD_PARTS] = { "#Valve_Hud_HEALTH", "#HL2MP_HudArmor", "#Valve_Hud_AMMO", "#HL2MP_HudAltAmmo", "#HL2MP_HudPower", "#HL2MP_HudCrosshair", "#HL2MP_HudChat", "#HL2MP_HudKillfeed", "#HL2MP_HudMatch", "#HL2MP_HudVote" };
struct Layout { HL2MPHudElementStyle parts[HUD_PARTS]; int crossShape, crossGap, crossLength, crossThickness; bool crossOutline, crossDot, lowHealth; } s_Layout;
bool s_Loaded = false;
struct Font { vgui::HFont handle; int face, pixels; bool bold; };
CUtlVector<Font> s_Fonts;
void Defaults( int preset )
{
    const float bounds[HUD_PARTS][4] = {
        {.025f,.89f,.15f,.085f}, {.19f,.89f,.15f,.085f}, {.76f,.89f,.20f,.085f}, {.89f,.80f,.07f,.07f},
        {.025f,.85f,.15f,.025f}, {.5f,.5f,.04f,.07f}, {.025f,.51f,.37f,.30f}, {.62f,.08f,.35f,.22f}, {.32f,.015f,.36f,.10f}, {.025f,.23f,.32f,.33f}
    };
    for ( int i = 0; i < HUD_PARTS; ++i )
    {
        HL2MPHudElementStyle &p = s_Layout.parts[i]; p.x = bounds[i][0]; p.y = bounds[i][1]; p.w = bounds[i][2]; p.h = bounds[i][3]; p.textScale = 1;
        p.foreground = Color( 255, 183, 67, 255 ); p.background = Color( 24, 25, 25, i == HUD_VOTE ? 235 : i == HUD_KILLFEED ? 200 : 105 );
        p.visible = p.labels = p.numbers = true; p.bars = i == HUD_POWER; p.font = 0;
    }
    s_Layout.parts[HUD_CROSSHAIR].foreground = Color( 220, 245, 210, 255 ); s_Layout.parts[HUD_CHAT].foreground = Color( 235, 235, 230, 255 );
    s_Layout.parts[HUD_KILLFEED].foreground = Color( 245, 245, 238, 255 );
    s_Layout.parts[HUD_CHAT].background[3] = 0;
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
bool ReadLayout( const char *path )
{
    KeyValues *root = new KeyValues( "HUDLayout" );
    if ( !root->LoadFromFile( filesystem, path, "MOD" ) || root->GetInt( "version" ) != 1 ) { root->deleteThis(); return false; }
    for ( int i = 0; i < HUD_PARTS; ++i )
    {
        KeyValues *k = root->FindKey( s_Names[i] ); if ( !k ) continue; HL2MPHudElementStyle &p = s_Layout.parts[i];
        p.x = Number( k, "x", p.x, 0, 1 ); p.y = Number( k, "y", p.y, 0, 1 ); p.w = Number( k, "width", p.w, .02f, 1 ); p.h = Number( k, "height", p.h, .02f, 1 ); p.textScale = Number( k, "text_scale", 1, .5f, 2 );
        p.visible = k->GetBool( "visible", p.visible ); p.labels = k->GetBool( "labels", p.labels ); p.numbers = k->GetBool( "numbers", p.numbers ); p.bars = k->GetBool( "bars", p.bars ); p.font = clamp( k->GetInt( "font" ), 0, 2 );
        for ( int c = 0; c < 4; ++c ) { p.foreground[c] = int( Number( k, VarArgs( "fg%d", c ), p.foreground[c], 0, 255 ) ); p.background[c] = int( Number( k, VarArgs( "bg%d", c ), p.background[c], 0, 255 ) ); }
    }
    s_Layout.crossShape = clamp( root->GetInt( "crosshair_shape" ), 0, 3 ); s_Layout.crossGap = clamp( root->GetInt( "crosshair_gap", 4 ), 0, 24 );
    s_Layout.crossLength = clamp( root->GetInt( "crosshair_length", 8 ), 1, 32 ); s_Layout.crossThickness = clamp( root->GetInt( "crosshair_thickness", 2 ), 1, 8 );
    s_Layout.crossOutline = root->GetBool( "crosshair_outline", true ); s_Layout.crossDot = root->GetBool( "crosshair_dot", false ); s_Layout.lowHealth = root->GetBool( "low_health_warning", true );
    root->deleteThis(); return true;
}
bool SaveLayout( const char *path )
{
    KeyValues *root = new KeyValues( "HUDLayout" ); root->SetInt( "version", 1 );
    for ( int i = 0; i < HUD_PARTS; ++i )
    {
        KeyValues *k = root->FindKey( s_Names[i], true ); const HL2MPHudElementStyle &p = s_Layout.parts[i];
        k->SetFloat( "x", p.x ); k->SetFloat( "y", p.y ); k->SetFloat( "width", p.w ); k->SetFloat( "height", p.h ); k->SetFloat( "text_scale", p.textScale );
        k->SetBool( "visible", p.visible ); k->SetBool( "labels", p.labels ); k->SetBool( "numbers", p.numbers ); k->SetBool( "bars", p.bars ); k->SetInt( "font", p.font );
        for ( int c = 0; c < 4; ++c ) { k->SetInt( VarArgs( "fg%d", c ), p.foreground[c] ); k->SetInt( VarArgs( "bg%d", c ), p.background[c] ); }
    }
    root->SetInt( "crosshair_shape", s_Layout.crossShape ); root->SetInt( "crosshair_gap", s_Layout.crossGap ); root->SetInt( "crosshair_length", s_Layout.crossLength ); root->SetInt( "crosshair_thickness", s_Layout.crossThickness );
    root->SetBool( "crosshair_outline", s_Layout.crossOutline ); root->SetBool( "crosshair_dot", s_Layout.crossDot ); root->SetBool( "low_health_warning", s_Layout.lowHealth );
    bool saved = root->SaveToFile( filesystem, path, "MOD" ); root->deleteThis(); return saved;
}
void Load() { if ( s_Loaded ) return; Defaults( 0 ); ReadLayout( "cfg/hud_layout.res" ); s_Loaded = true; }
void Rect( int x, int y, int w, int h, Color color ) { vgui::surface()->DrawSetColor( color ); vgui::surface()->DrawFilledRect( x, y, x + w, y + h ); }
void Text( int part, const char *text, int x, int y, int pixels, Color color, bool bold = false )
{
    vgui::surface()->DrawSetTextFont( HL2MPHudFont( part, pixels, bold ) ); vgui::surface()->DrawSetTextColor( color ); vgui::surface()->DrawSetTextPos( x, y );
    const wchar_t *wide = HL2MPWideText( text ); vgui::surface()->DrawPrintText( wide, V_wcslen( wide ) );
}
void Crosshair( int x, int y, float scale )
{
    const HL2MPHudElementStyle &p = s_Layout.parts[HUD_CROSSHAIR];
    const int gap = int( s_Layout.crossGap * scale ), length = MAX( 1, int( s_Layout.crossLength * scale ) ), thick = MAX( 1, int( s_Layout.crossThickness * scale ) );
    for ( int pass = s_Layout.crossOutline ? 0 : 1; pass < 2; ++pass )
    {
        int o = pass ? 0 : 1; Color color = pass ? p.foreground : Color( 0, 0, 0, p.foreground.a() );
        if ( s_Layout.crossShape == 0 || s_Layout.crossShape == 1 )
        {
            Rect( x - gap - length - o, y - thick / 2 - o, length + o * 2, thick + o * 2, color );
            Rect( x + gap - o, y - thick / 2 - o, length + o * 2, thick + o * 2, color );
            if ( s_Layout.crossShape == 0 ) Rect( x - thick / 2 - o, y - gap - length - o, thick + o * 2, length + o * 2, color );
            Rect( x - thick / 2 - o, y + gap - o, thick + o * 2, length + o * 2, color );
        }
        else if ( s_Layout.crossShape == 2 ) { vgui::surface()->DrawSetColor( color ); for ( int r = MAX( 1, gap + length - thick / 2 - o ); r <= gap + length + ( thick - 1 ) / 2 + o; ++r ) vgui::surface()->DrawOutlinedCircle( x, y, r, 40 ); }
        if ( s_Layout.crossDot || s_Layout.crossShape == 3 ) Rect( x - thick / 2 - o, y - thick / 2 - o, thick + o * 2, thick + o * 2, color );
    }
}
void DrawPart( int part, int x, int y, int w, int h, float scale, bool preview )
{
    const HL2MPHudElementStyle &style = s_Layout.parts[part]; if ( !style.visible && !preview ) return;
    if ( part == HUD_CROSSHAIR ) { if ( style.visible || preview ) Crosshair( x + w / 2, y + h / 2, scale * style.textScale ); return; }
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
    HL2MPServerTheme::Rounded( x, y, w, h, style.background );
    const int pad = MAX( 3, int( 8 * scale ) ); int font = MAX( 10, int( 32 * scale * style.textScale ) );
    Color color = style.foreground; if ( part == HUD_HEALTH && value <= 25 && s_Layout.lowHealth ) color = Color( 255, 90, 78, color.a() );
    if ( part <= HUD_POWER )
    {
        if ( style.labels && h > font + 12 * scale ) Text( part, s_Labels[part], x + pad, y + pad / 2, MAX( 9, int( 11 * scale * style.textScale ) ), color );
        if ( reserve >= 0 ) Q_snprintf( number, sizeof( number ), "%d / %d", value, reserve ); else Q_snprintf( number, sizeof( number ), "%d", value );
        if ( style.numbers && h >= 22 * scale ) Text( part, number, x + pad, y + MAX( 0, ( h - font ) / 2 ), MIN( font, h - pad ), color, true );
        if ( style.bars ) { int bar = MAX( 2, int( 4 * scale ) ); Rect( x + pad, y + h - bar - 3, MAX( 0, w - pad * 2 ), bar, Color( 150, 150, 150, 70 ) ); Rect( x + pad, y + h - bar - 3, int( MAX( 0, w - pad * 2 ) * clamp( float( value ) / maxValue, 0.0f, 1.0f ) ), bar, color ); }
        return;
    }
    if ( part == HUD_MATCH )
    {
        int seconds = preview ? 497 : HL2MPRules() ? MAX( 0, int( ceil( HL2MPRules()->GetMapRemainingTime() ) ) ) : 0;
        if ( !preview && mp_timelimit.GetInt() <= 0 ) Q_strncpy( number, "--:--", sizeof( number ) ); else Q_snprintf( number, sizeof( number ), "%02d:%02d", seconds / 60, seconds % 60 );
        Text( part, number, x + pad, y + pad / 2, MAX( 10, int( 23 * scale * style.textScale ) ), color, true );
        if ( style.labels )
        {
            int limit = ConVarRef( "mp_fraglimit" ).GetInt();
            if ( HL2MPRules() && HL2MPRules()->IsTeamplay() && !preview )
            { C_Team *a = GetGlobalTeam( TEAM_COMBINE ), *b = GetGlobalTeam( TEAM_REBELS ); Q_snprintf( number, sizeof( number ), "%s %d  /  %s %d", HL2MPText( "#HL2MP_TeamCombine" ), a ? a->Get_Score() : 0, HL2MPText( "#HL2MP_TeamRebels" ), b ? b->Get_Score() : 0 ); }
            else Q_snprintf( number, sizeof( number ), "%d / %d", preview ? 12 : player && g_PR ? g_PR->GetPlayerScore( player->entindex() ) : 0, preview ? 30 : limit );
            Text( part, number, x + pad, y + int( 32 * scale ), MAX( 9, int( 13 * scale * style.textScale ) ), color );
        }
    }
    else if ( preview )
    {
        const char *sample = part == HUD_CHAT ? "#HL2MP_HudChatSample" : part == HUD_KILLFEED ? "Player 1    >    Player 2" : "#HL2MP_HudVoteSample";
        Text( part, s_Labels[part], x + pad, y + pad, MAX( 9, int( 13 * scale * style.textScale ) ), color, true );
        Text( part, sample, x + pad, y + pad + int( 22 * scale ), MAX( 9, int( 14 * scale * style.textScale ) ), color );
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
class CEditorCanvas : public vgui::Panel
{
    DECLARE_CLASS_SIMPLE( CEditorCanvas, vgui::Panel );
public:
    CEditorCanvas( CHudEditor *editor );
    void Paint() OVERRIDE; void OnMousePressed( vgui::MouseCode code ) OVERRIDE; void OnCursorMoved( int x, int y ) OVERRIDE; void OnMouseReleased( vgui::MouseCode code ) OVERRIDE;
    void OnMouseCaptureLost() OVERRIDE { dragging = false; }
    CHudEditor *owner; bool dragging, resize; int startX, startY; HL2MPHudElementStyle original;
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
    void Open(); void Close( bool apply ); void BuildRows(); void Select( int part );
    void SetProperty( int id, int value ); int Property( int id );
    void OnCommand( const char *command ) OVERRIDE;
    void PerformLayout() OVERRIDE; void PaintBackground() OVERRIDE;
    void OnKeyCodeTyped( vgui::KeyCode key ) OVERRIDE { if ( key == KEY_ESCAPE ) { for ( int i = 0; i < 2; ++i ) if ( combos[i]->IsDropdownVisible() ) { combos[i]->HideMenu(); return; } Close( false ); } else BaseClass::OnKeyCodeTyped( key ); }
    void OnThink() OVERRIDE;
    MESSAGE_FUNC_PTR( ComboChanged, "TextChanged", panel );
    MESSAGE_FUNC_PARAMS( PropertyChanged, "Property", data ) { SetProperty( data->GetInt( "id" ), data->GetInt( "value" ) ); }
    int selected; CEditorCanvas *canvas; vgui::HFont font; bool reading;
private:
    Layout before; bool beforeEnabled, fromGame; double openedAt; int width, height; CHL2MPDropdown *combos[2];
    CHL2MPSettingsButton *buttons[7]; vgui::PanelListPanel *rows; vgui::Label *title, *hint, *status;
};
vgui::DHANDLE<CHudEditor> s_Editor;
class CHudFontRefresh : public CAutoGameSystemPerFrame
{
public:
    CHudFontRefresh() : CAutoGameSystemPerFrame( "HUDFonts" ), width( 0 ), height( 0 ), refreshAt( 0 ) {}
    void Update( float ) OVERRIDE
    {
        int w, h; vgui::surface()->GetScreenSize( w, h );
        if ( w != width || h != height ) { width = w; height = h; refreshAt = Plat_FloatTime() + .75; }
        if ( !refreshAt || Plat_FloatTime() < refreshAt ) return;
        refreshAt = 0;
        // GameUI resets dynamic font glyphs after its video-mode scheme reload.
        // Rebind existing handles after that reload has settled.
        const char *faces[] = { "Tahoma", "Arial", "Consolas" };
        FOR_EACH_VEC( s_Fonts, i ) { const Font &f = s_Fonts[i]; vgui::surface()->SetFontGlyphSet( f.handle, faces[f.face], f.pixels, f.bold ? 700 : 400, 0, 0, vgui::ISurface::FONTFLAG_ANTIALIAS | vgui::ISurface::FONTFLAG_DROPSHADOW ); }
        if ( s_Editor ) s_Editor->InvalidateLayout( true );
    }
private:
    int width, height; double refreshAt;
} s_FontRefresh;
const char *properties[] = { "#HL2MP_HudX", "#HL2MP_HudY", "#HL2MP_HudWidth", "#HL2MP_HudHeight", "#HL2MP_HudTextSize", "#HL2MP_HudRed", "#HL2MP_HudGreen", "#HL2MP_HudBlue", "#HL2MP_HudOpacity", "#HL2MP_HudBgRed", "#HL2MP_HudBgGreen", "#HL2MP_HudBgBlue", "#HL2MP_HudBgOpacity", "#HL2MP_HudVisible", "#HL2MP_HudLabels", "#HL2MP_HudNumbers", "#HL2MP_HudBars", "#HL2MP_HudFont", "#HL2MP_HudShape", "#HL2MP_HudGap", "#HL2MP_HudLength", "#HL2MP_HudThickness", "#HL2MP_HudOutline", "#HL2MP_HudDot", "#HL2MP_HudLowHealth" };
CEditorRow::CEditorRow( vgui::Panel *parent, int property, const char *label, int low, int high, int value ) : BaseClass( parent, "PropertyRow" ), id( property ), slider( NULL ), choice( NULL )
{
    name = new vgui::Label( this, "Name", label ); name->SetWrap( true ); name->SetContentAlignment( vgui::Label::a_west ); name->SetMouseInputEnabled( false );
    number = new vgui::Label( this, "Value", "" ); number->SetContentAlignment( vgui::Label::a_east );
    if ( ( id >= 13 && id <= 18 ) || id >= 22 )
    {
        choice = new CHL2MPDropdown( this, "Choice" ); choice->ServerStyle();
        if ( id == 17 ) { choice->AddItem( "Tahoma", NULL ); choice->AddItem( "Arial", NULL ); choice->AddItem( "Consolas", NULL ); }
        else if ( id == 18 ) { const char *shapes[] = { "#HL2MP_HudShapeCross", "#HL2MP_HudShapeT", "#HL2MP_HudShapeCircle", "#HL2MP_HudShapeDot" }; for ( int i = 0; i < 4; ++i ) choice->AddItem( shapes[i], NULL ); }
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
CEditorCanvas::CEditorCanvas( CHudEditor *editor ) : BaseClass( editor, "Canvas" ), owner( editor ), dragging( false ), resize( false ) { SetMouseInputEnabled( true ); }
void CEditorCanvas::Paint()
{
    Rect( 0, 0, GetWide(), GetTall(), Color( 14, 19, 21, 180 ) );
    vgui::surface()->DrawSetColor( Color( 140, 155, 160, 25 ) );
    for ( int i = 1; i < 10; ++i ) { vgui::surface()->DrawLine( i * GetWide() / 10, 0, i * GetWide() / 10, GetTall() ); vgui::surface()->DrawLine( 0, i * GetTall() / 10, GetWide(), i * GetTall() / 10 ); }
    float scale = MIN( GetWide() / 1280.0f, GetTall() / 720.0f );
    for ( int i = 0; i < HUD_PARTS; ++i )
    {
        int x, y, w, h; HL2MPHudBounds( i, GetWide(), GetTall(), x, y, w, h ); DrawPart( i, x, y, w, h, scale, true );
        if ( i == owner->selected ) { vgui::surface()->DrawSetColor( Color( 240, 133, 42, 255 ) ); vgui::surface()->DrawOutlinedRect( x, y, x + w, y + h ); Rect( x + w - 8, y + h - 8, 8, 8, Color( 240, 133, 42, 255 ) ); }
    }
}
void CEditorCanvas::OnMousePressed( vgui::MouseCode code )
{
    if ( code != MOUSE_LEFT ) return;
    vgui::input()->GetCursorPos( startX, startY ); ScreenToLocal( startX, startY );
    for ( int k = 0; k <= HUD_PARTS; ++k )
    {
        int i = k == 0 ? owner->selected : HUD_PARTS - k;
        int x, y, w, h; HL2MPHudBounds( i, GetWide(), GetTall(), x, y, w, h );
        if ( startX < x || startY < y || startX > x + w || startY > y + h ) continue;
        owner->Select( i ); original = s_Layout.parts[i]; resize = startX >= x + w - 12 && startY >= y + h - 12;
        dragging = true; vgui::input()->SetMouseCapture( GetVPanel() ); return;
    }
}
void CEditorCanvas::OnCursorMoved( int x, int y )
{
    if ( !dragging ) return; HL2MPHudElementStyle &p = s_Layout.parts[owner->selected];
    float dx = float( x - startX ) / MAX( 1, GetWide() ), dy = float( y - startY ) / MAX( 1, GetTall() );
    if ( resize )
    {
        if ( owner->selected == HUD_CROSSHAIR ) { p.textScale = clamp( original.textScale * ( 1 + dx / original.w ), .5f, 2.0f ); p.w = original.w * p.textScale / original.textScale; p.h = original.h * p.textScale / original.textScale; }
        else { p.w = clamp( original.w + dx, .025f, MAX( .025f, 1.0f - p.x ) ); p.h = clamp( original.h + dy, .025f, MAX( .025f, 1.0f - p.y ) ); }
    }
    else { p.x = clamp( original.x + dx, 0.0f, owner->selected == HUD_CROSSHAIR ? 1.0f : 1.0f - p.w ); p.y = clamp( original.y + dy, 0.0f, owner->selected == HUD_CROSSHAIR ? 1.0f : 1.0f - p.h ); }
    Repaint();
}
void CEditorCanvas::OnMouseReleased( vgui::MouseCode code ) { if ( code == MOUSE_LEFT && dragging ) { dragging = false; vgui::input()->SetMouseCapture( NULL ); owner->BuildRows(); } }
CHudEditor::CHudEditor() : BaseClass( NULL, "HUDEditor" ), selected( 0 ), reading( false ), width( 0 ), height( 0 )
{
    SetParent( enginevgui->GetPanel( PANEL_GAMEUIDLL ) ); MakePopup( false ); SetVisible( false ); SetMouseInputEnabled( true ); SetKeyBoardInputEnabled( true ); SetZPos( 500 );
    font = vgui::surface()->CreateFont(); title = new vgui::Label( this, "Title", "#HL2MP_HudEditor" ); hint = new vgui::Label( this, "Hint", "#HL2MP_HudDragHint" ); hint->SetWrap( true ); status = new vgui::Label( this, "Status", "" );
    canvas = new CEditorCanvas( this );
    for ( int i = 0; i < 2; ++i ) { combos[i] = new CHL2MPDropdown( this, i ? "Preset" : "Element" ); combos[i]->AddActionSignalTarget( this ); combos[i]->ServerStyle(); }
    for ( int i = 0; i < HUD_PARTS; ++i ) combos[0]->AddItem( s_Labels[i], NULL );
    combos[1]->AddItem( "#HL2MP_HudClassic", NULL ); combos[1]->AddItem( "#HL2MP_HudCompact", NULL ); combos[1]->AddItem( "#HL2MP_HudMinimal", NULL );
    for ( int i = 1; i <= 5; ++i ) combos[1]->AddItem( HL2MPFormat( "Custom slot %d", i ), NULL );
    combos[0]->ActivateItemByRow( 0 ); combos[1]->ActivateItemByRow( 0 );
    const char *names[] = { "#HL2MP_HudLoad", "#HL2MP_HudSave", "#HL2MP_HudResetElement", "#HL2MP_HudResetAll", "#HL2MP_HudEnable", "#HL2MP_UI_33bf9eeb20a9", "#HL2MP_Cancel" };
    const char *commands[] = { "load", "save", "reset_element", "reset_all", "enable", "apply", "cancel" };
    for ( int i = 0; i < 7; ++i ) { buttons[i] = new CHL2MPSettingsButton( this, names[i], commands[i] ); buttons[i]->ServerStyle( i == 5 ); }
    rows = new vgui::PanelListPanel( this, "Properties" ); rows->SetFirstColumnWidth( 0 ); rows->SetVerticalBufferPixels( 2 ); HL2MPStyleScrollBar( rows->GetScrollbar() );
}
void CHudEditor::Open()
{
    if ( IsVisible() ) { Close( false ); return; } Load(); before = s_Layout; beforeEnabled = cl_custom_hud.GetBool(); fromGame = !enginevgui->IsGameUIVisible();
    openedAt = Plat_FloatTime();
    engine->ClientCmd_Unrestricted( "gameui_activate;gameui_preventescape" ); SetVisible( true ); MoveToFront(); RequestFocus(); BuildRows(); InvalidateLayout( true ); vgui::input()->SetAppModalSurface( GetVPanel() );
}
void CHudEditor::Close( bool apply )
{
    if ( apply && !SaveLayout( "cfg/hud_layout.res" ) ) { status->SetText( "#HL2MP_HudSaveFailed" ); return; }
    if ( !apply ) { s_Layout = before; cl_custom_hud.SetValue( beforeEnabled ); }
    for ( int i = 0; i < 2; ++i ) combos[i]->HideMenu();
    canvas->dragging = false; if ( vgui::input()->GetMouseCapture() == canvas->GetVPanel() ) vgui::input()->SetMouseCapture( NULL );
    if ( vgui::input()->GetAppModalSurface() == GetVPanel() ) vgui::input()->ReleaseAppModalSurface();
    SetVisible( false );
    if ( fromGame ) engine->ClientCmd_Unrestricted( "gameui_allowescape;gameui_hide" );
    else if ( !HL2MPOptions_RestoreFocus() ) engine->ClientCmd_Unrestricted( "gameui_allowescape" );
}
void CHudEditor::OnThink()
{
    if ( !IsVisible() ) return; int sw, sh; vgui::surface()->GetScreenSize( sw, sh ); if ( sw != width || sh != height ) InvalidateLayout( true );
    buttons[4]->Active( cl_custom_hud.GetBool() ); if ( !enginevgui->IsGameUIVisible() && Plat_FloatTime() - openedAt > .5 ) Close( false );
}
void CHudEditor::PerformLayout()
{
    BaseClass::PerformLayout(); vgui::surface()->GetScreenSize( width, height ); SetBounds( 0, 0, width, height ); float s = MAX( .65f, MIN( width / 1280.0f, height / 720.0f ) );
    vgui::surface()->SetFontGlyphSet( font, "Tahoma", MAX( 11, int( 15 * s ) ), 500, 0, 0, vgui::ISurface::FONTFLAG_ANTIALIAS );
    int margin = int( 20 * s ), side = int( 330 * s ), leftW = width - side - margin * 3, top = int( 116 * s );
    title->SetBounds( margin, margin, width / 2, int( 32 * s ) ); title->SetFont( font ); title->SetFgColor( Color( 238, 133, 40, 255 ) );
    combos[0]->SetBounds( width - side - margin, int( 65 * s ), side, int( 38 * s ) ); combos[1]->SetBounds( margin, int( 65 * s ), leftW / 2, int( 38 * s ) );
    for ( int i = 0; i < 2; ++i ) combos[i]->Style( font, s );
    buttons[0]->SetBounds( margin + leftW / 2 + 8, int( 65 * s ), leftW / 4 - 8, int( 38 * s ) ); buttons[1]->SetBounds( margin + leftW * 3 / 4 + 4, int( 65 * s ), leftW / 4 - 4, int( 38 * s ) );
    int ch = MIN( int( leftW * float( height ) / width ), height - top - int( 130 * s ) ); int cw = int( ch * float( width ) / height );
    canvas->SetBounds( margin + ( leftW - cw ) / 2, top + int( 12 * s ), cw, ch );
    hint->SetBounds( margin, top + ch + int( 24 * s ), leftW, int( 48 * s ) ); hint->SetFont( font ); hint->SetFgColor( Color( 180, 185, 185, 255 ) );
    hint->GetTextImage()->SetSize( hint->GetWide(), hint->GetTall() ); hint->GetTextImage()->RecalculateNewLinePositions();
    status->SetBounds( margin, top + ch + int( 74 * s ), leftW, int( 25 * s ) ); status->SetFont( font );
    rows->SetBounds( width - side - margin, top, side, height - top - int( 80 * s ) );
    for ( int i = 0; i < rows->GetItemCount(); ++i ) { vgui::Panel *row = rows->GetItemPanel( rows->GetItemIDFromRow( i ) ); if ( row ) { row->SetTall( int( 62 * s ) ); row->InvalidateLayout( true ); } }
    for ( int i = 0; i < 7; ++i ) buttons[i]->SetFont( font );
    int by = height - int( 58 * s ), bw = ( width - margin * 6 ) / 5;
    for ( int i = 2; i < 7; ++i ) buttons[i]->SetBounds( margin + ( i - 2 ) * ( bw + margin ), by, bw, int( 40 * s ) );
}
void CHudEditor::PaintBackground() { Rect( 0, 0, GetWide(), GetTall(), Color( 20, 23, 25, 242 ) ); Rect( 0, 0, GetWide(), 2, Color( 231, 121, 32, 255 ) ); }
void CHudEditor::Select( int part ) { selected = clamp( part, 0, HUD_PARTS - 1 ); reading = true; combos[0]->ActivateItemByRow( selected ); reading = false; BuildRows(); }
void CHudEditor::ComboChanged( vgui::Panel *panel ) { if ( !reading && panel == combos[0] ) { selected = clamp( combos[0]->GetActiveItem(), 0, HUD_PARTS - 1 ); BuildRows(); } }
int CHudEditor::Property( int id )
{
    const HL2MPHudElementStyle &p = s_Layout.parts[selected];
    if ( id == 0 ) return int( p.x * 100 ); if ( id == 1 ) return int( p.y * 100 ); if ( id == 2 ) return int( p.w * 100 ); if ( id == 3 ) return int( p.h * 100 ); if ( id == 4 ) return int( p.textScale * 100 );
    if ( id < 9 ) return p.foreground[id - 5]; if ( id < 13 ) return p.background[id - 9];
    switch ( id ) { case 13:return p.visible; case 14:return p.labels; case 15:return p.numbers; case 16:return p.bars; case 17:return p.font; case 18:return s_Layout.crossShape; case 19:return s_Layout.crossGap; case 20:return s_Layout.crossLength; case 21:return s_Layout.crossThickness; case 22:return s_Layout.crossOutline; case 23:return s_Layout.crossDot; case 24:return s_Layout.lowHealth; } return 0;
}
void CHudEditor::SetProperty( int id, int v )
{
    HL2MPHudElementStyle &p = s_Layout.parts[selected];
    if ( id == 0 ) p.x = v / 100.0f; else if ( id == 1 ) p.y = v / 100.0f; else if ( id == 2 ) p.w = v / 100.0f; else if ( id == 3 ) p.h = v / 100.0f; else if ( id == 4 ) p.textScale = v / 100.0f;
    else if ( id < 9 ) p.foreground[id - 5] = v; else if ( id < 13 ) p.background[id - 9] = v;
    else switch ( id ) { case 13:p.visible = v != 0;break; case 14:p.labels = v != 0;break; case 15:p.numbers = v != 0;break; case 16:p.bars = v != 0;break; case 17:p.font = v;break; case 18:s_Layout.crossShape = v;break; case 19:s_Layout.crossGap = v;break; case 20:s_Layout.crossLength = v;break; case 21:s_Layout.crossThickness = v;break; case 22:s_Layout.crossOutline = v != 0;break; case 23:s_Layout.crossDot = v != 0;break; case 24:s_Layout.lowHealth = v != 0;break; }
    canvas->Repaint();
}
void CHudEditor::BuildRows()
{
    reading = true; rows->DeleteAllItems();
    for ( int i = 0; i < ARRAYSIZE( properties ); ++i )
    {
        if ( i >= 18 && i <= 23 && selected != HUD_CROSSHAIR ) continue;
        if ( i == 24 && selected != HUD_HEALTH ) continue;
        if ( selected >= HUD_CROSSHAIR && i >= 14 && i <= 16 && !( selected == HUD_MATCH && i == 14 ) ) continue;
        if ( selected == HUD_CROSSHAIR && ( ( i >= 9 && i <= 12 ) || i == 17 ) ) continue;
        int low = i == 2 || i == 3 ? 2 : i == 4 ? 50 : i == 20 || i == 21 ? 1 : 0;
        int high = i < 4 ? 100 : i == 4 ? 200 : i < 13 ? 255 : i < 17 ? 1 : i == 17 ? 2 : i == 18 ? 3 : i == 19 ? 24 : i == 20 ? 32 : i == 21 ? 8 : 1;
        CEditorRow *row = new CEditorRow( rows, i, properties[i], low, high, Property( i ) ); row->SetTall( 62 ); rows->AddItem( NULL, row );
    }
    reading = false; InvalidateLayout( true );
}
void CHudEditor::OnCommand( const char *command )
{
    if ( !Q_strcmp( command, "apply" ) ) { Close( true ); return; }
    if ( !Q_strcmp( command, "cancel" ) ) { Close( false ); return; }
    if ( !Q_strcmp( command, "enable" ) ) { cl_custom_hud.SetValue( !cl_custom_hud.GetBool() ); return; }
    if ( !Q_strcmp( command, "reset_all" ) ) Defaults( 0 );
    else if ( !Q_strcmp( command, "reset_element" ) ) { Layout old = s_Layout; Defaults( 0 ); Layout fresh = s_Layout; s_Layout = old; s_Layout.parts[selected] = fresh.parts[selected]; if ( selected == HUD_CROSSHAIR ) { s_Layout.crossShape = fresh.crossShape; s_Layout.crossGap = fresh.crossGap; s_Layout.crossLength = fresh.crossLength; s_Layout.crossThickness = fresh.crossThickness; s_Layout.crossOutline = fresh.crossOutline; s_Layout.crossDot = fresh.crossDot; } if ( selected == HUD_HEALTH ) s_Layout.lowHealth = fresh.lowHealth; }
    else if ( !Q_strcmp( command, "load" ) )
    {
        int preset = combos[1]->GetActiveItem(); if ( preset < 3 ) Defaults( preset );
        else if ( !ReadLayout( VarArgs( "cfg/hud_profile_%d.res", clamp( preset - 2, 1, 5 ) ) ) ) status->SetText( "#HL2MP_HudSlotEmpty" );
    }
    else if ( !Q_strcmp( command, "save" ) )
    { int preset = combos[1]->GetActiveItem(); if ( preset < 3 ) status->SetText( "#HL2MP_HudPickSlot" ); else status->SetText( SaveLayout( VarArgs( "cfg/hud_profile_%d.res", clamp( preset - 2, 1, 5 ) ) ) ? "#HL2MP_HudSaved" : "#HL2MP_HudSaveFailed" ); }
    else { BaseClass::OnCommand( command ); return; }
    BuildRows();
}
}
const HL2MPHudElementStyle &HL2MPHudStyle( int part ) { Load(); return s_Layout.parts[clamp( part, 0, HUD_PARTS - 1 )]; }
void HL2MPHudBounds( int part, int sw, int sh, int &x, int &y, int &w, int &h )
{
    const HL2MPHudElementStyle &p = HL2MPHudStyle( part ); w = clamp( int( p.w * sw ), 8, sw ); h = clamp( int( p.h * sh ), 8, sh );
    x = clamp( int( p.x * sw ) - ( part == HUD_CROSSHAIR ? w / 2 : 0 ), 0, sw - w ); y = clamp( int( p.y * sh ) - ( part == HUD_CROSSHAIR ? h / 2 : 0 ), 0, sh - h );
}
vgui::HFont HL2MPHudFont( int part, int pixels, bool bold )
{
    int face = HL2MPHudStyle( part ).font; pixels = clamp( pixels, 8, 96 );
    FOR_EACH_VEC( s_Fonts, i ) if ( s_Fonts[i].face == face && s_Fonts[i].pixels == pixels && s_Fonts[i].bold == bold ) return s_Fonts[i].handle;
    Font f = { vgui::surface()->CreateFont(), face, pixels, bold }; const char *names[] = { "Tahoma", "Arial", "Consolas" };
    vgui::surface()->SetFontGlyphSet( f.handle, names[face], pixels, bold ? 700 : 400, 0, 0, vgui::ISurface::FONTFLAG_ANTIALIAS | vgui::ISurface::FONTFLAG_DROPSHADOW ); s_Fonts.AddToTail( f ); return f.handle;
}
bool HL2MPHudCustomEnabled() { return cl_custom_hud.GetBool(); }
bool HL2MPHudReplaces( const char *name )
{
    if ( !HL2MPHudCustomEnabled() ) return false;
    const char *names[] = { "CHudHealth", "CHudBattery", "CHudAmmo", "CHudSecondaryAmmo", "CHudSuitPower", "CHudServerInfo", "CHUDQuickInfo" };
    for ( int i = 0; i < ARRAYSIZE( names ); ++i ) if ( !Q_strcmp( name, names[i] ) ) return true; return false;
}
bool HL2MPHudDrawCrosshair( int centerX, int centerY )
{
    if ( !HL2MPHudCustomEnabled() ) return false; const HL2MPHudElementStyle &p = HL2MPHudStyle( HUD_CROSSHAIR ); if ( !p.visible ) return true;
    int sw, sh; vgui::surface()->GetScreenSize( sw, sh );
    Crosshair( centerX + int( ( p.x - .5f ) * sw ), centerY + int( ( p.y - .5f ) * sh ), MIN( sw / 1280.0f, sh / 720.0f ) * p.textScale ); return true;
}
void HL2MPHudCustomization_Init() { Load(); if ( !s_Editor ) s_Editor = new CHudEditor; }
void HL2MPHudEditor_Open() { if ( s_Editor ) s_Editor->Open(); }
CON_COMMAND_F( hud_editor, "Open the visual HUD editor.", FCVAR_CLIENTDLL ) { HL2MPHudEditor_Open(); }
