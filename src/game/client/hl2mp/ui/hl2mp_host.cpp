#include "cbase.h"
#include "hl2mp_messages.h"
#include "hl2mp_host.h"
#include "hl2mp_loading.h"
#include "hl2mp_menucontrols.h"
#include "hl2mp_coordinator.h"
#include "filesystem.h"
#include "vgui_controls/EditablePanel.h"
#include "vgui_controls/TextEntry.h"
#include "vgui_controls/CheckButton.h"
#include "tier0/memdbgon.h"

namespace
{
bool HostMapName( const char *map )
{
    if ( !map[0] || Q_strlen( map ) > 63 ) return false;
    for ( const char *c = map; *c; ++c )
        if ( !( ( *c >= 'a' && *c <= 'z' ) || ( *c >= 'A' && *c <= 'Z' ) || ( *c >= '0' && *c <= '9' ) || *c == '_' || *c == '-' ) ) return false;
    char path[80]; Q_snprintf( path, sizeof( path ), "maps/%s.bsp", map ); return filesystem->FileExists( path, "GAME" );
}
bool Number( vgui::TextEntry *entry, int low, int high, int &number )
{
    char text[16]; entry->GetText( text, sizeof( text ) ); number = 0;
    if ( !text[0] || Q_strlen( text ) > 6 ) return false;
    for ( const char *c = text; *c; ++c ) { if ( *c < '0' || *c > '9' ) return false; number = number * 10 + *c - '0'; }
    return number >= low && number <= high;
}
void ReadUTF8( vgui::TextEntry *entry, char *text, int size )
{
    wchar_t wide[128]; entry->GetText( wide, sizeof( wide ) ); V_UnicodeToUTF8( wide, text, size );
}

class CHostServer : public vgui::EditablePanel
{
    DECLARE_CLASS_SIMPLE( CHostServer, vgui::EditablePanel );
public:
    CHostServer( vgui::Panel *parent );
    void Open();
    void PerformLayout() OVERRIDE;
    void PaintBackground() OVERRIDE;
    void OnThink() OVERRIDE;
    void OnCommand( const char *command ) OVERRIDE;
    void OnKeyCodeTyped( vgui::KeyCode code ) OVERRIDE;
private:
    void Close(); void Start();
    enum { NAME, PASSWORD, SLOTS, TIME, FRAGS, GRAVITY, BOTS, ENTRIES };
    enum { MAP, MODE, NETWORK, DIFFICULTY, DROPS };
    enum { FRIENDLY, STAY, RESPAWN, FALLDAMAGE, CHECKS };
    vgui::Label *m_Title, *m_Description, *m_Status, *m_NetworkHint, *m_BotHint, *m_Section[3], *m_Caption[ENTRIES + DROPS];
    vgui::TextEntry *m_Entry[ENTRIES]; CHL2MPDropdown *m_Drop[DROPS]; vgui::CheckButton *m_Check[CHECKS];
    CHL2MPSettingsButton *m_Back, *m_Start;
    vgui::HFont m_HeadingFont, m_Font, m_Small;
    int m_Width, m_Height; float m_Scale;
};

CHostServer::CHostServer( vgui::Panel *parent ) : BaseClass( parent, "CreateServer" ), m_Width( 0 ), m_Height( 0 ), m_Scale( 1 )
{
    SetVisible( false ); SetMouseInputEnabled( true ); SetKeyBoardInputEnabled( true );
    m_HeadingFont = vgui::surface()->CreateFont(); m_Font = vgui::surface()->CreateFont(); m_Small = vgui::surface()->CreateFont();
    m_Title = new vgui::Label( this, "Title", "#HL2MP_UI_6e142bc97c2d" );
    m_Description = new vgui::Label( this, "Description", "#HL2MP_UI_c78d3e50b5f0" );
    m_Status = new vgui::Label( this, "Status", "" ); m_Status->SetWrap( true );
    m_NetworkHint = new vgui::Label( this, "NetworkHint", "" ); m_NetworkHint->SetWrap( true );
    m_BotHint = new vgui::Label( this, "BotHint", "" ); m_BotHint->SetWrap( true );
    const char *sections[] = { "#HL2MP_UI_830ef117e138", "#HL2MP_UI_d898b7f024f0", "#HL2MP_UI_bbed1da098ec" };
    const char *captions[] = { "#HL2MP_UI_21babfed6c38", "#HL2MP_UI_54c44337615a", "#HL2MP_UI_651dc42c69e4", "#HL2MP_UI_a092ca8db875", "#HL2MP_UI_475bba55cc00", "#HL2MP_UI_7071825d9a0d", "#HL2MP_UI_f17c0acbd8e5", "#HL2MP_UI_be176b0015c4", "#HL2MP_UI_ede76ceab6f1", "#HL2MP_UI_639a40e82b9a", "#HL2MP_UI_f7f4f5eba6f8" };
    const char *defaults[] = { "Half-Life 2 Deathmatch", "", "8", "20", "0", "600", "0" };
    for ( int i = 0; i < 3; ++i ) m_Section[i] = new vgui::Label( this, sections[i], sections[i] );
    for ( int i = 0; i < ENTRIES + DROPS; ++i ) m_Caption[i] = new vgui::Label( this, captions[i], captions[i] );
    for ( int i = 0; i < ENTRIES; ++i )
    {
        m_Entry[i] = new vgui::TextEntry( this, VarArgs( "Value%d", i ) ); m_Entry[i]->SetMaximumCharCount( i <= PASSWORD ? 63 : 4 );
        m_Entry[i]->SetAllowNumericInputOnly( i > PASSWORD ); m_Entry[i]->SetText( defaults[i] );
    }
    m_Entry[PASSWORD]->SetTextHidden( true );
    for ( int i = 0; i < DROPS; ++i ) { m_Drop[i] = new CHL2MPDropdown( this, VarArgs( "Choice%d", i ) ); m_Drop[i]->ServerStyle(); }
    m_Drop[MODE]->AddItem( "#HL2MP_UI_127aad574b83", NULL ); m_Drop[MODE]->AddItem( "#HL2MP_UI_f756ee9002fc", NULL );
    m_Drop[NETWORK]->AddItem( "#HL2MP_UI_b4bba182e55e", NULL ); m_Drop[NETWORK]->AddItem( "#HL2MP_UI_2a47e28ec0cf", NULL ); m_Drop[NETWORK]->AddItem( "#HL2MP_UI_7c8356578e20", NULL );
    const char *skills[] = { "#HL2MP_UI_27e88b861fa9", "#HL2MP_UI_db2cb3fe28e2", "#HL2MP_UI_0e24076baf99", "#HL2MP_UI_884981556c79" };
    for ( int i = 0; i < 4; ++i ) m_Drop[DIFFICULTY]->AddItem( skills[i], NULL );
    m_Drop[MODE]->SilentActivateItemByRow( 0 ); m_Drop[NETWORK]->SilentActivateItemByRow( 0 ); m_Drop[DIFFICULTY]->SilentActivateItemByRow( 1 );
    const char *checks[] = { "#HL2MP_UI_d5134a5bdfcc", "#HL2MP_UI_18d384fbe9ba", "#HL2MP_UI_d8205bf54d70", "#HL2MP_UI_a16fd61ee2e6" };
    for ( int i = 0; i < CHECKS; ++i ) m_Check[i] = new vgui::CheckButton( this, checks[i], checks[i] );
    m_Check[RESPAWN]->SetSelected( true );
    m_Back = new CHL2MPSettingsButton( this, "#HL2MP_UI_24c76252a90c", "close" ); m_Start = new CHL2MPSettingsButton( this, "#HL2MP_UI_485ffc6dcae0", "start" );
    m_Back->ServerStyle(); m_Start->ServerStyle( true );
}
void CHostServer::Open()
{
    if ( HL2MPCoordinator().Busy() ) return;
    char selected[64]; m_Drop[MAP]->GetText( selected, sizeof( selected ) ); m_Drop[MAP]->DeleteAllItems();
    FileFindHandle_t handle;
    for ( const char *file = filesystem->FindFirstEx( "maps/*.bsp", "GAME", &handle ); file; file = filesystem->FindNext( handle ) )
    {
        char map[64]; Q_StripExtension( file, map, sizeof( map ) ); if ( !HostMapName( map ) ) continue;
        bool duplicate = false;
        for ( int i = 0; i < m_Drop[MAP]->GetItemCount(); ++i ) { char existing[64]; m_Drop[MAP]->GetItemText( m_Drop[MAP]->GetItemIDFromRow( i ), existing, sizeof( existing ) ); if ( !Q_stricmp( existing, map ) ) duplicate = true; }
        if ( !duplicate ) m_Drop[MAP]->AddItem( map, NULL );
    }
    filesystem->FindClose( handle ); m_Drop[MAP]->SilentActivateItemByRow( 0 );
    for ( int i = 0; i < m_Drop[MAP]->GetItemCount(); ++i )
    {
        char map[64]; m_Drop[MAP]->GetItemText( m_Drop[MAP]->GetItemIDFromRow( i ), map, sizeof( map ) );
        if ( !Q_stricmp( map, selected[0] ? selected : "dm_lockdown" ) ) m_Drop[MAP]->SilentActivateItemByRow( i );
    }
    m_Status->SetText( m_Drop[MAP]->GetItemCount() ? "#HL2MP_UI_9ec2843d0184" : "#HL2MP_UI_368c797c01d2" );
    SetVisible( true ); MoveToFront(); InvalidateLayout( true, true ); m_Back->RequestFocus();
}
void CHostServer::Close()
{
    for ( int i = 0; i < DROPS; ++i ) m_Drop[i]->HideMenu();
    m_Entry[PASSWORD]->SetText( "" ); SetVisible( false ); GetParent()->OnCommand( "host_closed" );
}
void CHostServer::PerformLayout()
{
    BaseClass::PerformLayout(); int w, h; vgui::surface()->GetScreenSize( w, h ); SetBounds( 0, 0, w, h );
    const float s = m_Scale = MIN( w / 1280.0f, h / 720.0f ); const int left = ( w - int( 1180 * s ) ) / 2;
    if ( w != m_Width || h != m_Height )
    {
        m_HeadingFont = vgui::surface()->CreateFont(); m_Font = vgui::surface()->CreateFont(); m_Small = vgui::surface()->CreateFont();
        vgui::surface()->SetFontGlyphSet( m_HeadingFont, "GorDIN Bold", MAX( 20, int( 34 * s ) ), 600, 0, 0, vgui::ISurface::FONTFLAG_ANTIALIAS );
        vgui::surface()->SetFontGlyphSet( m_Font, "GorDIN", MAX( 12, int( 18 * s ) ), 400, 0, 0, vgui::ISurface::FONTFLAG_ANTIALIAS );
        vgui::surface()->SetFontGlyphSet( m_Small, "GorDIN", MAX( 11, int( 15 * s ) ), 400, 0, 0, vgui::ISurface::FONTFLAG_ANTIALIAS );
    }
    m_Width = w; m_Height = h;
    for ( int i = 0; i < GetChildCount(); ++i )
    {
        vgui::Label *label = dynamic_cast<vgui::Label *>( GetChild( i ) );
        if ( label ) { label->SetFont( m_Font ); label->SetFgColor( HL2MPServerTheme::Text() ); }
    }
    auto place = [s, left]( vgui::Panel *p, int x, int y, int wide, int tall ) { p->SetBounds( left + int( x * s ), int( y * s ), int( wide * s ), int( tall * s ) ); };
    m_Title->SetFont( m_HeadingFont ); m_Title->SetFgColor( HL2MPServerTheme::Orange() ); place( m_Title, 0, 30, 1180, 45 ); place( m_Description, 0, 82, 1180, 30 );
    for ( int i = 0; i < 3; ++i ) { place( m_Section[i], i * 400 + 16, 139, 348, 28 ); m_Section[i]->SetFgColor( HL2MPServerTheme::Orange() ); }
    for ( int i = 0; i < ENTRIES; ++i ) { m_Entry[i]->SetFont( m_Font ); m_Entry[i]->SetBgColor( Color( 29, 29, 29, 255 ) ); m_Entry[i]->SetFgColor( HL2MPServerTheme::Text() ); m_Entry[i]->SetBorder( NULL ); }
    for ( int i = 0; i < DROPS; ++i ) m_Drop[i]->Style( m_Font, s );
    auto entry = [&]( int id, int x, int y ) { place( m_Caption[id], x, y, 348, 24 ); place( m_Entry[id], x, y + 28, 348, 34 ); };
    auto drop = [&]( int id, int x, int y ) { place( m_Caption[ENTRIES + id], x, y, 348, 24 ); place( m_Drop[id], x, y + 28, 348, 34 ); };
    entry( NAME, 16, 182 ); drop( MAP, 16, 262 ); drop( MODE, 16, 342 ); entry( SLOTS, 16, 422 ); entry( PASSWORD, 16, 502 );
    entry( TIME, 416, 182 ); entry( FRAGS, 416, 262 ); entry( GRAVITY, 416, 342 );
    for ( int i = 0; i < CHECKS; ++i ) place( m_Check[i], 416, 420 + i * 39, 348, 32 );
    drop( NETWORK, 816, 182 ); place( m_NetworkHint, 816, 250, 348, 72 ); m_NetworkHint->SetFont( m_Small ); m_NetworkHint->SetFgColor( HL2MPServerTheme::Muted() );
    entry( BOTS, 816, 342 ); drop( DIFFICULTY, 816, 422 ); place( m_BotHint, 816, 502, 348, 92 ); m_BotHint->SetFont( m_Small ); m_BotHint->SetFgColor( HL2MPServerTheme::Muted() );
    place( m_Status, 0, 618, 1180, 40 ); m_Status->SetFont( m_Small ); place( m_Back, 0, 669, 160, 36 ); place( m_Start, 916, 665, 264, 42 );
    for ( int i = 0; i < GetChildCount(); ++i ) if ( dynamic_cast<vgui::Label *>( GetChild( i ) ) ) GetChild( i )->InvalidateLayout( true );
}
void CHostServer::PaintBackground()
{
    vgui::surface()->DrawSetColor( Color( 32, 32, 32, 250 ) ); vgui::surface()->DrawFilledRect( 0, 0, GetWide(), GetTall() );
    const float s = m_Scale; const int left = ( GetWide() - int( 1180 * s ) ) / 2;
    for ( int i = 0; i < 3; ++i ) HL2MPServerTheme::Rounded( left + int( i * 400 * s ), int( 128 * s ), int( 380 * s ), int( 480 * s ), Color( 24, 24, 24, 235 ), MAX( 3, int( 6 * s ) ) );
}
void CHostServer::OnThink()
{
    BaseClass::OnThink(); if ( !IsVisible() ) return;
    int w, h; vgui::surface()->GetScreenSize( w, h ); if ( w != m_Width || h != m_Height ) InvalidateLayout( true );
    const bool connected = engine->IsConnected() && !engine->IsLevelMainMenuBackground();
    m_Start->SetEnabled( !connected && m_Drop[MAP]->GetItemCount() && !HL2MPCoordinator().Busy() );
    if ( connected ) m_Status->SetText( "#HL2MP_UI_0179108e7c7b" );
    const int network = m_Drop[NETWORK]->GetActiveItem();
    m_NetworkHint->SetText( network == 0 ? "#HL2MP_UI_45529828398d" : network == 1 ? "#HL2MP_UI_ba39f3cb790f" : "#HL2MP_UI_6e034a4223ca" );
    char map[64], nav[80]; m_Drop[MAP]->GetText( map, sizeof( map ) ); Q_snprintf( nav, sizeof( nav ), "maps/%s.nav", map );
    m_BotHint->SetText( filesystem->FileExists( nav, "GAME" ) ? "#HL2MP_UI_3561de434fea" : "#HL2MP_UI_97d791749210" );
}
void CHostServer::Start()
{
    if ( ( engine->IsConnected() && !engine->IsLevelMainMenuBackground() ) || HL2MPCoordinator().Busy() ) return;
    int slots, time, frags, gravity, bots; char map[64], name[256], password[256];
    m_Drop[MAP]->GetText( map, sizeof( map ) ); ReadUTF8( m_Entry[NAME], name, sizeof( name ) ); ReadUTF8( m_Entry[PASSWORD], password, sizeof( password ) );
    if ( !HostMapName( map ) ) { m_Status->SetText( "#HL2MP_UI_920d0aebe963" ); return; }
    if ( !name[0] ) { m_Status->SetText( "#HL2MP_UI_ced454c5bc44" ); return; }
    if ( !Number( m_Entry[SLOTS], 2, 32, slots ) || !Number( m_Entry[BOTS], 0, 31, bots ) || bots >= slots ) { m_Status->SetText( "#HL2MP_UI_c0e69bfa44ae" ); return; }
    if ( !Number( m_Entry[TIME], 0, 1440, time ) || !Number( m_Entry[FRAGS], 0, 9999, frags ) || !Number( m_Entry[GRAVITY], 0, 4000, gravity ) ) { m_Status->SetText( "#HL2MP_UI_cbb80014bfee" ); return; }
    const char *required[] = { "hostname", "sv_password", "mp_teamplay", "mp_timelimit", "mp_fraglimit", "sv_gravity", "sv_lan", "sv_use_steam_networking", "hl2mp_bot_quota", "hl2mp_bot_difficulty" };
    for ( int i = 0; i < ARRAYSIZE( required ); ++i ) if ( !ConVarRef( required[i], true ).IsValid() ) { m_Status->SetText( "#HL2MP_UI_63f97c4274f7" ); return; }
    // Player-entered names/passwords go directly to cvars, never through the command parser.
    ConVarRef( "hostname" ).SetValue( name ); ConVarRef( "sv_password" ).SetValue( password );
    ConVarRef( "mp_teamplay" ).SetValue( m_Drop[MODE]->GetActiveItem() == 1 );
    ConVarRef( "mp_timelimit" ).SetValue( time ); ConVarRef( "mp_fraglimit" ).SetValue( frags ); ConVarRef( "sv_gravity" ).SetValue( gravity );
    ConVarRef( "mp_friendlyfire" ).SetValue( m_Check[FRIENDLY]->IsSelected() ); ConVarRef( "mp_weaponstay" ).SetValue( m_Check[STAY]->IsSelected() );
    ConVarRef( "mp_forcerespawn" ).SetValue( m_Check[RESPAWN]->IsSelected() ); ConVarRef( "mp_falldamage" ).SetValue( m_Check[FALLDAMAGE]->IsSelected() );
    ConVarRef( "sv_lan" ).SetValue( m_Drop[NETWORK]->GetActiveItem() == 2 ); ConVarRef( "sv_use_steam_networking" ).SetValue( m_Drop[NETWORK]->GetActiveItem() == 0 );
    ConVarRef( "hl2mp_bot_quota_mode" ).SetValue( "normal" ); ConVarRef( "hl2mp_bot_auto_vacate" ).SetValue( 0 );
    ConVarRef( "hl2mp_bot_difficulty" ).SetValue( m_Drop[DIFFICULTY]->GetActiveItem() ); ConVarRef( "hl2mp_bot_quota" ).SetValue( bots );
    // Only the validated map token and bounded integer enter the engine command buffer.
    // Like the engine's Create Server flow, enable real progress before loading
    // the local map. Otherwise GameUI leaves the main menu up during startup.
    HL2MPLoading_SetContext( map, m_Drop[MODE]->GetActiveItem() == 1 ? "listen_team" : "listen", name );
    char command[160]; Q_snprintf( command, sizeof( command ), "disconnect\nmaxplayers %d\nprogress_enable\nmap %s\n", slots, map );
    Close(); engine->ClientCmd_Unrestricted( command );
}
void CHostServer::OnCommand( const char *command )
{
    if ( !Q_strcmp( command, "close" ) ) Close();
    else if ( !Q_strcmp( command, "start" ) ) Start();
    else if ( !Q_strcmp( command, "refresh_fonts" ) ) { m_Width = 0; InvalidateLayout( true ); }
    else BaseClass::OnCommand( command );
}
void CHostServer::OnKeyCodeTyped( vgui::KeyCode code )
{
    if ( code == KEY_ESCAPE ) { for ( int i = 0; i < DROPS; ++i ) if ( m_Drop[i]->IsDropdownVisible() ) { m_Drop[i]->HideMenu(); return; } Close(); return; }
    BaseClass::OnKeyCodeTyped( code );
}
}
vgui::Panel *HL2MPHost_Create( vgui::Panel *parent ) { return new CHostServer( parent ); }
void HL2MPHost_Open( vgui::Panel *panel ) { static_cast<CHostServer *>( panel )->Open(); }
