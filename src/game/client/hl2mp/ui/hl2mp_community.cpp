#include "cbase.h"
#include "hl2mp_community.h"
#include "hl2mp_gamerules.h"
#include "hl2mp_menucontrols.h"
#include "usermessages.h"
#include "ienginevgui.h"
#include "vgui_controls/EditablePanel.h"
#include "vgui_controls/CheckButton.h"
#include "vgui_controls/TextEntry.h"
#include "tier0/memdbgon.h"

ConVar cl_bright_player_models( "cl_bright_player_models", "0", FCVAR_ARCHIVE, "Evenly light textured player models on community servers." );

bool HL2MPCommunity_Available()
{
    return engine->IsInGame() && !engine->IsLevelMainMenuBackground() && HL2MPRules() && HL2MPRules()->IsCommunityServer();
}

namespace
{
class CServerControls;
CServerControls *s_Panel = NULL;
struct ServerState
{
    int generation, minutes, frags;
    bool enabled, admin, paused, teamplay, friendly, stay, respawn;
    char map[64], next[64], status[80];
};

class CCommunityConfirmation : public vgui::EditablePanel
{
    DECLARE_CLASS_SIMPLE( CCommunityConfirmation, vgui::EditablePanel );
public:
    CCommunityConfirmation( vgui::Panel *parent ) : BaseClass( parent, "Confirmation" )
    {
        SetVisible( false ); SetZPos( 200 ); SetMouseInputEnabled( true ); SetKeyBoardInputEnabled( true );
        heading = new vgui::Label( this, "Heading", "CONFIRM ACTION" );
        body = new vgui::Label( this, "Body", "" ); body->SetWrap( true );
        yes = new CHL2MPSettingsButton( this, "CONFIRM", "confirm" ); no = new CHL2MPSettingsButton( this, "CANCEL", "cancel" );
        yes->ServerStyle( true ); no->ServerStyle();
        yes->RemoveActionSignalTarget( this ); yes->AddActionSignalTarget( parent );
        no->RemoveActionSignalTarget( this ); no->AddActionSignalTarget( parent );
    }
    ~CCommunityConfirmation() { ReleaseModal(); }
    void ReleaseModal() { if ( vgui::input()->GetAppModalSurface() == GetVPanel() ) vgui::input()->ReleaseAppModalSurface(); }
    void Layout( int w, int h, float s, vgui::HFont title, vgui::HFont font )
    {
        SetBounds( 0, 0, w, h );
        const int width = MIN( w - 32, int( 680 * s ) ), height = MIN( h - 32, int( 240 * s ) );
        const int x = ( w - width ) / 2, y = ( h - height ) / 2, pad = MAX( 12, int( 24 * s ) );
        m_Card.x = x; m_Card.y = y; m_Card.width = width; m_Card.height = height;
        heading->SetBounds( x + pad, y + pad, width - pad * 2, int( 36 * s ) ); heading->SetFont( title ); heading->SetFgColor( HL2MPServerTheme::Orange() );
        body->SetBounds( x + pad, y + int( 70 * s ), width - pad * 2, height - int( 140 * s ) ); body->SetFont( font ); body->SetFgColor( HL2MPServerTheme::Text() );
        const int bw = ( width - pad * 3 ) / 2, bh = int( 40 * s );
        yes->SetFont( font ); no->SetFont( font );
        yes->SetBounds( x + pad, y + height - pad - bh, bw, bh ); no->SetBounds( x + pad * 2 + bw, y + height - pad - bh, bw, bh );
        heading->InvalidateLayout( true ); body->InvalidateLayout( true );
    }
    void PaintBackground() OVERRIDE
    {
        vgui::surface()->DrawSetColor( Color( 0, 0, 0, 190 ) ); vgui::surface()->DrawFilledRect( 0, 0, GetWide(), GetTall() );
        HL2MPServerTheme::Rounded( m_Card.x, m_Card.y, m_Card.width, m_Card.height, Color( 42, 42, 42, 255 ), 6 );
    }
    void OnKeyCodeTyped( vgui::KeyCode code ) OVERRIDE
    {
        if ( code == KEY_ESCAPE ) GetParent()->OnCommand( "cancel" ); else BaseClass::OnKeyCodeTyped( code );
    }
    vgui::Label *heading, *body; CHL2MPSettingsButton *yes, *no;
private:
    Rect_t m_Card;
};

class CServerControls : public vgui::EditablePanel
{
    DECLARE_CLASS_SIMPLE( CServerControls, vgui::EditablePanel );
public:
    CServerControls( vgui::Panel *parent );
    ~CServerControls() { if ( IsVisible() ) engine->ClientCmd_Unrestricted( "gameui_allowescape" ); if ( m_Pending ) m_Pending->deleteThis(); if ( s_Panel == this ) s_Panel = NULL; }
    void Open( bool fromHotkey = false );
    void Toggle() { if ( IsVisible() ) Close( true ); else { engine->ClientCmd_Unrestricted( "gameui_activate" ); Open( true ); } }
    void Receive( const ServerState &state );
    void Map( int generation, const char *map );
    void PerformLayout() OVERRIDE;
    void ApplySchemeSettings( vgui::IScheme *scheme ) OVERRIDE;
    void PaintBackground() OVERRIDE;
    void OnThink() OVERRIDE;
    void OnCommand( const char *command ) OVERRIDE;
    void OnKeyCodeTyped( vgui::KeyCode code ) OVERRIDE;
private:
    MESSAGE_FUNC_PARAMS( OnCheckChanged, "CheckButtonChecked", message )
    {
        if ( message->GetPtr( "panel" ) == m_Bright ) cl_bright_player_models.SetValue( message->GetInt( "state" ) != 0 );
    }
    void Close( bool resume = false );
    void RefreshPlayers();
    void Request( const char *action );
    KeyValues *Message( const char *action );
    void Confirm( const char *action, const char *text, KeyValues *message );
    enum { MODE, MAP_NOW, MAP_NEXT, TEAM, PLAYER, DURATION, DROPDOWNS };
    enum { CHANGE, NEXT, PAUSE, RESTART, APPLY, JOIN, SPECTATE, KICK, BAN, CLOSE, RESUME, BUTTONS };
    vgui::Label *m_Title, *m_Access, *m_Info, *m_Status, *m_Sections[3], *m_Captions[9];
    CCommunityConfirmation *m_Confirm;
    CHL2MPDropdown *m_Drop[DROPDOWNS];
    CHL2MPSettingsButton *m_Button[BUTTONS];
    vgui::TextEntry *m_Time, *m_Frags, *m_BanMinutes;
    vgui::CheckButton *m_Friendly, *m_Stay, *m_Respawn, *m_Bright;
    vgui::HFont m_TitleFont, m_BodyFont, m_SmallFont;
    ServerState m_State;
    KeyValues *m_Pending;
    bool m_Received, m_MapsDone, m_FromHotkey;
    double m_NextPoll;
    int m_Width, m_Height;
    float m_Scale;
};

static void Choice( CHL2MPDropdown *drop, const char *text, int value )
{
    KeyValues *kv = new KeyValues( "Choice", "value", value ); drop->AddItem( text, kv ); kv->deleteThis();
}
static int Value( CHL2MPDropdown *drop )
{
    KeyValues *kv = drop->GetActiveItemUserData(); return kv ? kv->GetInt( "value" ) : -1;
}
static void SelectMap( CHL2MPDropdown *drop, const char *map )
{
    for ( int i = 0; i < drop->GetItemCount(); ++i )
    {
        char text[64]; drop->GetItemText( drop->GetItemIDFromRow( i ), text, sizeof( text ) );
        if ( !Q_stricmp( text, map ) ) { drop->SilentActivateItemByRow( i ); return; }
    }
    if ( drop->GetItemCount() ) drop->SilentActivateItemByRow( 0 );
}

CServerControls::CServerControls( vgui::Panel *parent ) : BaseClass( parent, "CommunityServerControls" ),
    m_Pending( NULL ), m_Received( false ), m_MapsDone( false ), m_FromHotkey( false ), m_NextPoll( 0 ), m_Width( 0 ), m_Height( 0 ), m_Scale( 1 )
{
    s_Panel = this; memset( &m_State, 0, sizeof( m_State ) );
    SetVisible( false ); SetMouseInputEnabled( true ); SetKeyBoardInputEnabled( true );
    m_TitleFont = vgui::surface()->CreateFont(); m_BodyFont = vgui::surface()->CreateFont(); m_SmallFont = vgui::surface()->CreateFont();
    m_Title = new vgui::Label( this, "Title", "SERVER CONTROLS" );
    m_Access = new vgui::Label( this, "Access", "" ); m_Info = new vgui::Label( this, "Info", "" );
    m_Status = new vgui::Label( this, "Status", "" ); m_Confirm = new CCommunityConfirmation( this );
    const char *sections[] = { "MATCH", "SERVER RULES", "PLAYER" };
    const char *captions[] = { "Game mode", "Current map", "Next map", "Time limit (minutes, 0 = unlimited)", "Frag limit (0 = unlimited)", "Join a team", "Manage player", "Ban duration", "Custom minutes" };
    for ( int i = 0; i < 3; ++i ) m_Sections[i] = new vgui::Label( this, sections[i], sections[i] );
    for ( int i = 0; i < 9; ++i ) m_Captions[i] = new vgui::Label( this, captions[i], captions[i] );
    for ( int i = 0; i < DROPDOWNS; ++i ) { m_Drop[i] = new CHL2MPDropdown( this, VarArgs( "Choice%d", i ) ); m_Drop[i]->ServerStyle(); }
    Choice( m_Drop[MODE], "DEATHMATCH", 0 ); Choice( m_Drop[MODE], "TEAM DEATHMATCH", 1 );
    Choice( m_Drop[DURATION], "5 MINUTES", 5 ); Choice( m_Drop[DURATION], "15 MINUTES", 15 ); Choice( m_Drop[DURATION], "30 MINUTES", 30 );
    Choice( m_Drop[DURATION], "1 HOUR", 60 ); Choice( m_Drop[DURATION], "4 HOURS", 240 ); Choice( m_Drop[DURATION], "1 DAY", 1440 );
    Choice( m_Drop[DURATION], "1 WEEK", 10080 ); Choice( m_Drop[DURATION], "30 DAYS", 43200 ); Choice( m_Drop[DURATION], "PERMANENT", 0 ); Choice( m_Drop[DURATION], "CUSTOM", -1 );
    const char *texts[] = { "CHANGE MAP / MODE", "SET NEXT MAP", "PAUSE", "RESTART ROUND", "APPLY RULES", "JOIN / AUTO-ASSIGN", "SPECTATE", "KICK", "BAN", "<  BACK", "RESUME GAME" };
    const char *commands[] = { "changelevel", "nextmap", "pause", "restart", "settings", "join", "spectate", "kick", "ban", "close", "resume" };
    for ( int i = 0; i < BUTTONS; ++i ) { m_Button[i] = new CHL2MPSettingsButton( this, texts[i], commands[i] ); m_Button[i]->ServerStyle( i == APPLY ); }
    m_Time = new vgui::TextEntry( this, "Time" ); m_Frags = new vgui::TextEntry( this, "Frags" );
    m_Time->SetAllowNumericInputOnly( true ); m_Frags->SetAllowNumericInputOnly( true ); m_Time->SetMaximumCharCount( 4 ); m_Frags->SetMaximumCharCount( 4 );
    m_BanMinutes = new vgui::TextEntry( this, "BanMinutes" ); m_BanMinutes->SetAllowNumericInputOnly( true ); m_BanMinutes->SetMaximumCharCount( 6 ); m_BanMinutes->SetText( "60" );
    m_Friendly = new vgui::CheckButton( this, "Friendly", "Friendly fire" );
    m_Stay = new vgui::CheckButton( this, "Stay", "Weapon stay" );
    m_Respawn = new vgui::CheckButton( this, "Respawn", "Force respawn" );
    m_Bright = new vgui::CheckButton( this, "Bright", "Bright player models" );
    m_Bright->AddActionSignalTarget( this );
}

void CServerControls::ApplySchemeSettings( vgui::IScheme *scheme )
{
    BaseClass::ApplySchemeSettings( scheme );
    for ( int i = 0; i < GetChildCount(); ++i )
    {
        vgui::Label *label = dynamic_cast<vgui::Label *>( GetChild( i ) );
        if ( label ) { label->SetFgColor( Color( 223, 229, 226, 255 ) ); label->SetBgColor( Color( 0, 0, 0, 0 ) ); }
    }
    vgui::TextEntry *entries[] = { m_Time, m_Frags, m_BanMinutes };
    for ( int i = 0; i < 3; ++i ) { entries[i]->SetBgColor( Color( 29, 29, 29, 255 ) ); entries[i]->SetFgColor( HL2MPServerTheme::Text() ); entries[i]->SetBorder( NULL ); }
    InvalidateLayout();
}

void CServerControls::PerformLayout()
{
    BaseClass::PerformLayout(); int w, h; vgui::surface()->GetScreenSize( w, h ); SetBounds( 0, 0, w, h );
    const float s = m_Scale = MIN( w / 1280.0f, h / 720.0f );
    if ( w != m_Width || h != m_Height )
    {
        m_TitleFont = vgui::surface()->CreateFont(); m_BodyFont = vgui::surface()->CreateFont(); m_SmallFont = vgui::surface()->CreateFont();
        vgui::surface()->SetFontGlyphSet( m_TitleFont, "GorDIN Bold", MAX( 20, int( 34 * s ) ), 600, 0, 0, vgui::ISurface::FONTFLAG_ANTIALIAS );
        vgui::surface()->SetFontGlyphSet( m_BodyFont, "GorDIN", MAX( 12, int( 18 * s ) ), 400, 0, 0, vgui::ISurface::FONTFLAG_ANTIALIAS );
        vgui::surface()->SetFontGlyphSet( m_SmallFont, "GorDIN", MAX( 11, int( 15 * s ) ), 400, 0, 0, vgui::ISurface::FONTFLAG_ANTIALIAS );
    }
    m_Width = w; m_Height = h;
    for ( int i = 0; i < GetChildCount(); ++i )
    {
        vgui::Label *label = dynamic_cast<vgui::Label *>( GetChild( i ) ); if ( label ) label->SetFont( m_BodyFont );
    }
    for ( int i = 0; i < DROPDOWNS; ++i ) m_Drop[i]->Style( m_BodyFont, s );
    m_Time->SetFont( m_BodyFont ); m_Frags->SetFont( m_BodyFont ); m_BanMinutes->SetFont( m_BodyFont ); m_Title->SetFont( m_TitleFont ); m_Access->SetFont( m_SmallFont );
    m_Title->SetFgColor( HL2MPServerTheme::Orange() );
    for ( int i = 0; i < 3; ++i ) { m_Sections[i]->SetFont( m_SmallFont ); m_Sections[i]->SetFgColor( HL2MPServerTheme::Orange() ); }
    const int left = ( w - int( 1180 * s ) ) / 2;
    auto place = [s, left]( vgui::Panel *p, int x, int y, int wide, int tall ) { p->SetBounds( left + int( x * s ), int( y * s ), int( wide * s ), int( tall * s ) ); };
    place( m_Title, 0, 30, 680, 45 ); place( m_Access, 810, 37, 370, 28 ); place( m_Info, 0, 82, 1180, 30 );
    for ( int i = 0; i < 3; ++i ) place( m_Sections[i], i * 400 + 16, 139, 348, 28 );
    place( m_Captions[0], 16, 182, 348, 24 ); place( m_Drop[MODE], 16, 210, 348, 34 );
    place( m_Captions[1], 16, 262, 348, 24 ); place( m_Drop[MAP_NOW], 16, 290, 348, 34 );
    place( m_Button[CHANGE], 16, 336, 348, 38 );
    place( m_Captions[2], 16, 394, 348, 24 ); place( m_Drop[MAP_NEXT], 16, 422, 348, 34 );
    place( m_Button[NEXT], 16, 468, 348, 38 ); place( m_Button[PAUSE], 16, 550, 164, 38 ); place( m_Button[RESTART], 194, 550, 170, 38 );
    place( m_Captions[3], 416, 182, 348, 24 ); place( m_Time, 416, 210, 348, 34 );
    place( m_Captions[4], 416, 262, 348, 24 ); place( m_Frags, 416, 290, 348, 34 );
    place( m_Friendly, 416, 350, 348, 36 ); place( m_Stay, 416, 400, 348, 36 ); place( m_Respawn, 416, 450, 348, 36 );
    place( m_Button[APPLY], 416, 550, 348, 38 );
    place( m_Captions[5], 816, 182, 348, 24 ); place( m_Drop[TEAM], 816, 210, 348, 34 );
    place( m_Button[JOIN], 816, 258, 210, 38 ); place( m_Button[SPECTATE], 1038, 258, 126, 38 );
    place( m_Bright, 816, 318, 348, 36 );
    place( m_Captions[6], 816, 362, 348, 24 ); place( m_Drop[PLAYER], 816, 390, 348, 34 );
    place( m_Captions[7], 816, 432, 348, 24 ); place( m_Drop[DURATION], 816, 460, 348, 34 );
    place( m_Captions[8], 816, 506, 180, 30 ); place( m_BanMinutes, 1000, 504, 164, 32 );
    place( m_Button[KICK], 816, 550, 164, 38 ); place( m_Button[BAN], 994, 550, 170, 38 );
    place( m_Status, 0, 622, 1180, 30 ); place( m_Button[CLOSE], 0, 669, 160, 36 );
    place( m_Button[RESUME], 960, 669, 220, 36 );
    m_Confirm->Layout( w, h, s, m_TitleFont, m_BodyFont );
    for ( int i = 0; i < GetChildCount(); ++i ) if ( dynamic_cast<vgui::Label *>( GetChild( i ) ) ) GetChild( i )->InvalidateLayout( true );
}

void CServerControls::PaintBackground()
{
    vgui::ISurface *surface = vgui::surface(); surface->DrawSetColor( Color( 32, 32, 32, 250 ) ); surface->DrawFilledRect( 0, 0, GetWide(), GetTall() );
    const float s = m_Scale; const int left = ( GetWide() - int( 1180 * s ) ) / 2;
    for ( int i = 0; i < 3; ++i )
    {
        int x = left + int( i * 400 * s );
        HL2MPServerTheme::Rounded( x, int( 128 * s ), int( 380 * s ), int( 480 * s ), Color( 24, 24, 24, 235 ), MAX( 3, int( 6 * s ) ) );
    }
}

KeyValues *CServerControls::Message( const char *action )
{
    KeyValues *message = new KeyValues( "HL2MPCommunity" ); message->SetString( "action", action ); message->SetInt( "generation", m_State.generation ); return message;
}
void CServerControls::Request( const char *action ) { engine->ServerCmdKeyValues( Message( action ) ); m_NextPoll = Plat_FloatTime() + 2; }
void CServerControls::Open( bool fromHotkey )
{
    if ( !HL2MPCommunity_Available() ) return;
    m_FromHotkey = fromHotkey;
    engine->ClientCmd_Unrestricted( "gameui_preventescape" );
    m_Confirm->ReleaseModal(); m_Confirm->SetVisible( false );
    if ( m_Pending ) { m_Pending->deleteThis(); m_Pending = NULL; }
    m_Received = m_MapsDone = false; m_Drop[MAP_NOW]->DeleteAllItems(); m_Drop[MAP_NEXT]->DeleteAllItems();
    m_Drop[DURATION]->SilentActivateItemByRow( 2 ); m_Drop[TEAM]->DeleteAllItems();
    Choice( m_Drop[TEAM], "AUTO-ASSIGN / PLAY", 0 );
    if ( HL2MPRules()->IsTeamplay() ) { Choice( m_Drop[TEAM], "COMBINE", TEAM_COMBINE ); Choice( m_Drop[TEAM], "REBELS", TEAM_REBELS ); }
    m_Drop[TEAM]->SilentActivateItemByRow( 0 ); m_Bright->SetSelected( cl_bright_player_models.GetBool() );
    m_Status->SetText( "Requesting server settings..." ); SetVisible( true ); MoveToFront(); RequestFocus(); InvalidateLayout( true, true ); Request( "open" );
}
void CServerControls::Close( bool resume )
{
    engine->ClientCmd_Unrestricted( "gameui_allowescape" );
    m_Confirm->ReleaseModal(); m_Confirm->SetVisible( false );
    if ( m_Pending ) { m_Pending->deleteThis(); m_Pending = NULL; }
    for ( int i = 0; i < DROPDOWNS; ++i ) m_Drop[i]->HideMenu();
    SetVisible( false ); PostMessage( GetParent(), new KeyValues( "Command", "command", "community_closed" ) );
    if ( resume && engine->IsInGame() ) engine->ClientCmd_Unrestricted( "gameui_hide" );
}
void CServerControls::Receive( const ServerState &state )
{
    if ( !IsVisible() ) return;
    if ( m_Received && m_State.generation != state.generation ) { Close(); return; }
    m_State = state;
    if ( !state.enabled ) { Close(); return; }
    if ( !m_Received || !Q_strcmp( state.status, "Server settings applied." ) )
    {
        m_Time->SetText( VarArgs( "%d", state.minutes ) ); m_Frags->SetText( VarArgs( "%d", state.frags ) );
        m_Friendly->SetSelected( state.friendly ); m_Stay->SetSelected( state.stay ); m_Respawn->SetSelected( state.respawn );
        m_Drop[MODE]->SilentActivateItemByRow( state.teamplay ? 1 : 0 );
    }
    m_Received = true; m_Access->SetText( state.admin ? "ADMIN ACCESS" : "PLAYER ACCESS / SERVER RULES ARE READ-ONLY" );
    m_Info->SetText( VarArgs( "%s  /  %s  /  NEXT: %s", state.map, state.teamplay ? "TEAM DEATHMATCH" : "DEATHMATCH", state.next[0] ? state.next : "MAP CYCLE" ) );
    if ( state.status[0] ) m_Status->SetText( state.status );
    else if ( !state.admin ) m_Status->SetText( "The host or a listed administrator can change server settings." );
    else m_Status->SetText( state.paused ? "Game paused. Use RESUME to continue." : "Changes apply to this server. Map and mode changes reload the map." );
    m_Button[PAUSE]->SetText( state.paused ? "RESUME" : "PAUSE" );
    if ( !m_Pending && !m_Drop[PLAYER]->IsDropdownVisible() ) RefreshPlayers();
}
void CServerControls::Map( int generation, const char *map )
{
    if ( !IsVisible() || generation != m_State.generation ) return;
    if ( !map[0] ) { m_MapsDone = true; SelectMap( m_Drop[MAP_NOW], m_State.map ); SelectMap( m_Drop[MAP_NEXT], m_State.next[0] ? m_State.next : m_State.map ); return; }
    for ( int d = MAP_NOW; d <= MAP_NEXT; ++d )
    {
        bool exists = false;
        for ( int i = 0; i < m_Drop[d]->GetItemCount(); ++i ) { char text[64]; m_Drop[d]->GetItemText( m_Drop[d]->GetItemIDFromRow( i ), text, sizeof( text ) ); if ( !Q_stricmp( text, map ) ) exists = true; }
        if ( !exists ) Choice( m_Drop[d], map, 0 );
    }
}
void CServerControls::RefreshPlayers()
{
    const int selected = Value( m_Drop[PLAYER] ); m_Drop[PLAYER]->DeleteAllItems(); int row = 0, chosen = -1;
    for ( int i = 1; i <= engine->GetMaxClients(); ++i )
    {
        player_info_t info; if ( i == engine->GetLocalPlayer() || !engine->GetPlayerInfo( i, &info ) || info.ishltv ) continue;
        Choice( m_Drop[PLAYER], info.name, info.userID ); if ( info.userID == selected ) chosen = row; ++row;
    }
    if ( row ) m_Drop[PLAYER]->SilentActivateItemByRow( chosen >= 0 ? chosen : 0 );
}
void CServerControls::Confirm( const char *, const char *text, KeyValues *message )
{
    if ( m_Pending ) m_Pending->deleteThis(); m_Pending = message; m_Confirm->body->SetText( text );
    for ( int i = 0; i < DROPDOWNS; ++i ) m_Drop[i]->HideMenu();
    m_Confirm->SetVisible( true ); m_Confirm->MoveToFront(); m_Confirm->no->RequestFocus();
    vgui::input()->SetAppModalSurface( m_Confirm->GetVPanel() );
}
void CServerControls::OnThink()
{
    BaseClass::OnThink(); if ( !IsVisible() ) return;
    if ( !HL2MPCommunity_Available() ) { Close(); return; }
    if ( !enginevgui->IsGameUIVisible() ) { Close(); return; }
    int width, height; vgui::surface()->GetScreenSize( width, height );
    if ( width != m_Width || height != m_Height ) InvalidateLayout( true );
    if ( Plat_FloatTime() >= m_NextPoll && !m_Pending && !m_Drop[PLAYER]->IsDropdownVisible() ) Request( m_Received && ( !m_State.admin || m_MapsDone ) ? "status" : "open" );
    const bool admin = m_Received && m_State.admin && !m_Pending;
    for ( int i = CHANGE; i <= APPLY; ++i ) m_Button[i]->SetEnabled( admin && ( i > NEXT || m_MapsDone ) );
    m_Button[KICK]->SetEnabled( admin && Value( m_Drop[PLAYER] ) > 0 ); m_Button[BAN]->SetEnabled( admin && Value( m_Drop[PLAYER] ) > 0 );
    m_Time->SetEnabled( admin ); m_Frags->SetEnabled( admin ); m_Friendly->SetEnabled( admin ); m_Stay->SetEnabled( admin ); m_Respawn->SetEnabled( admin );
    m_BanMinutes->SetEnabled( admin && Value( m_Drop[DURATION] ) == -1 );
    for ( int i = 0; i < DROPDOWNS; ++i ) m_Drop[i]->SetEnabled( i == TEAM ? m_Received && !m_Pending : admin );
    m_Button[JOIN]->SetEnabled( m_Received && !m_Pending ); m_Button[SPECTATE]->SetEnabled( m_Received && !m_Pending ); m_Bright->SetEnabled( !m_Pending );
    m_Status->SetVisible( !m_Pending ); m_Button[CLOSE]->SetVisible( !m_Pending );
    m_Button[RESUME]->SetVisible( !m_Pending );
    m_Confirm->SetVisible( m_Pending != NULL );
}
void CServerControls::OnKeyCodeTyped( vgui::KeyCode code )
{
    const char *binding = engine->Key_BindingForKey( ButtonCode_t( code ) );
    if ( binding && !Q_strcmp( binding, "community_menu" ) ) { Close( true ); return; }
    if ( code == KEY_ESCAPE )
    {
        for ( int i = 0; i < DROPDOWNS; ++i ) if ( m_Drop[i]->IsDropdownVisible() ) { m_Drop[i]->HideMenu(); return; }
        if ( m_Pending ) OnCommand( "cancel" ); else Close( m_FromHotkey ); return;
    }
    BaseClass::OnKeyCodeTyped( code );
}
void CServerControls::OnCommand( const char *command )
{
    if ( !Q_strcmp( command, "close" ) ) { Close(); return; }
    if ( !Q_strcmp( command, "resume" ) ) { Close( true ); return; }
    if ( !Q_strcmp( command, "cancel" ) ) { if ( m_Pending ) m_Pending->deleteThis(); m_Pending = NULL; m_Confirm->ReleaseModal(); m_Confirm->SetVisible( false ); m_Button[CLOSE]->RequestFocus(); return; }
    if ( !Q_strcmp( command, "confirm" ) )
    {
        if ( m_Pending ) { engine->ServerCmdKeyValues( m_Pending ); m_Pending = NULL; m_NextPoll = Plat_FloatTime() + 2; } m_Confirm->ReleaseModal(); m_Confirm->SetVisible( false ); m_Button[CLOSE]->RequestFocus(); return;
    }
    if ( !Q_strcmp( command, "refresh_fonts" ) ) { m_Width = 0; InvalidateLayout( true ); return; }
    if ( !m_Received || m_Pending ) return;
    KeyValues *message = Message( command ); char text[128];
    if ( !Q_strcmp( command, "settings" ) )
    {
        m_Time->GetText( text, sizeof( text ) ); message->SetString( "time", text ); m_Frags->GetText( text, sizeof( text ) ); message->SetString( "frags", text );
        message->SetInt( "friendly", m_Friendly->IsSelected() ); message->SetInt( "stay", m_Stay->IsSelected() ); message->SetInt( "respawn", m_Respawn->IsSelected() );
    }
    else if ( !Q_strcmp( command, "pause" ) ) message->SetInt( "paused", !m_State.paused );
    else if ( !Q_strcmp( command, "join" ) || !Q_strcmp( command, "spectate" ) ) { message->SetString( "action", "join" ); message->SetInt( "team", !Q_strcmp( command, "spectate" ) ? TEAM_SPECTATOR : Value( m_Drop[TEAM] ) ); }
    else if ( !Q_strcmp( command, "changelevel" ) || !Q_strcmp( command, "nextmap" ) )
    {
        m_Drop[!Q_strcmp( command, "changelevel" ) ? MAP_NOW : MAP_NEXT]->GetText( text, sizeof( text ) ); message->SetString( "map", text ); message->SetInt( "teamplay", Value( m_Drop[MODE] ) );
        if ( !Q_strcmp( command, "changelevel" ) ) { Confirm( command, VarArgs( "Load %s in %s mode? This ends the current round.", text, Value( m_Drop[MODE] ) ? "team deathmatch" : "deathmatch" ), message ); return; }
    }
    else if ( !Q_strcmp( command, "restart" ) ) { Confirm( command, "Restart the round? Scores and player loadouts will reset.", message ); return; }
    else if ( !Q_strcmp( command, "kick" ) || !Q_strcmp( command, "ban" ) )
    {
        int minutes = Value( m_Drop[DURATION] );
        if ( !Q_strcmp( command, "ban" ) && minutes < 0 )
        {
            char custom[16]; m_BanMinutes->GetText( custom, sizeof( custom ) ); minutes = 0;
            bool valid = custom[0] != 0;
            for ( const char *c = custom; *c; ++c ) { if ( *c < '0' || *c > '9' ) { valid = false; break; } minutes = minutes * 10 + *c - '0'; }
            if ( !valid || minutes < 1 || minutes > 525600 ) { message->deleteThis(); m_Status->SetText( "Custom bans: enter 1 to 525600 minutes. Use PERMANENT for no expiry." ); return; }
        }
        message->SetInt( "userid", Value( m_Drop[PLAYER] ) ); message->SetInt( "minutes", minutes ); m_Drop[PLAYER]->GetText( text, sizeof( text ) );
        char duration[64]; m_Drop[DURATION]->GetText( duration, sizeof( duration ) );
        if ( Value( m_Drop[DURATION] ) < 0 ) Q_snprintf( duration, sizeof( duration ), "%d MINUTES", minutes );
        Confirm( command, !Q_strcmp( command, "ban" ) ? VarArgs( "Ban %s from this server (%s)?", text, duration ) : VarArgs( "Kick %s from this server?", text ), message ); return;
    }
    else { message->deleteThis(); BaseClass::OnCommand( command ); return; }
    engine->ServerCmdKeyValues( message ); m_NextPoll = Plat_FloatTime() + 2;
}

void StateMessage( bf_read &reader )
{
    ServerState state = {}; state.generation = reader.ReadLong(); state.enabled = reader.ReadByte() != 0; state.admin = reader.ReadByte() != 0;
    state.paused = reader.ReadByte() != 0; state.teamplay = reader.ReadByte() != 0; state.minutes = reader.ReadShort(); state.frags = reader.ReadShort();
    state.friendly = reader.ReadByte() != 0; state.stay = reader.ReadByte() != 0; state.respawn = reader.ReadByte() != 0;
    reader.ReadString( state.map, sizeof( state.map ) ); reader.ReadString( state.next, sizeof( state.next ) ); reader.ReadString( state.status, sizeof( state.status ) );
    if ( !reader.IsOverflowed() && s_Panel ) s_Panel->Receive( state );
}
void MapMessage( bf_read &reader )
{
    int generation = reader.ReadLong(); char map[64]; reader.ReadString( map, sizeof( map ) );
    if ( !reader.IsOverflowed() && s_Panel ) s_Panel->Map( generation, map );
}
}

void HL2MPCommunity_Init() { usermessages->HookMessage( "CommunityState", StateMessage ); usermessages->HookMessage( "CommunityMap", MapMessage ); }
vgui::Panel *HL2MPCommunity_Create( vgui::Panel *parent ) { return new CServerControls( parent ); }
void HL2MPCommunity_Open( vgui::Panel *panel ) { static_cast<CServerControls *>( panel )->Open(); }
void HL2MPCommunity_Toggle( vgui::Panel *panel ) { static_cast<CServerControls *>( panel )->Toggle(); }
