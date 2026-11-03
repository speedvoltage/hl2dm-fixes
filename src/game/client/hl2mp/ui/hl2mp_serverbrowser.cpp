#include "cbase.h"
#include "cdll_client_int.h"
#include "hl2mp_serverbrowser.h"
#include "hl2mp_loading.h"
#include "hl2mp_menucontrols.h"
#include "hl2mp_coordinator.h"
#include "filesystem.h"
#include "vgui/IVGui.h"
#include "vgui/IInput.h"
#include "vgui_controls/EditablePanel.h"
#include "vgui_controls/PanelListPanel.h"
#include "vgui_controls/TextEntry.h"
#include "tier1/utlstring.h"
#include <algorithm>
#include <time.h>
#include "tier0/memdbgon.h"

namespace
{
enum Source { INTERNET, FAVORITES, HISTORY, LAN, FRIENDS, SPECTATE, BLOCKED };
enum Filter { GAME, POPULATION, PING, SECURITY, PASSWORD, FILTER_COUNT };
static const char *s_Sources[] = { "INTERNET", "FAVORITES", "HISTORY", "LAN", "FRIENDS", "SPECTATE", "BLOCKED" };
static const char *s_Columns[] = { "SERVER", "MAP", "PLAYERS", "PING", "ACCESS" };
static ISteamMatchmakingServers *Servers() { return steamapicontext ? steamapicontext->SteamMatchmakingServers() : NULL; }
static ISteamMatchmaking *Favorites() { return steamapicontext ? steamapicontext->SteamMatchmaking() : NULL; }
static AppId_t CurrentApp() { return steamapicontext && steamapicontext->SteamUtils() ? steamapicontext->SteamUtils()->GetAppID() : 0; }
static void SetLabelText( vgui::Label *label, const char *text )
{
    wchar_t wide[2048]; V_UTF8ToUnicode( text, wide, sizeof( wide ) ); label->SetText( wide );
}
static void ReadText( vgui::TextEntry *entry, char *text, int size )
{
    wchar_t wide[256]; entry->GetText( wide, sizeof( wide ) ); V_UnicodeToUTF8( wide, text, size );
}
static vgui::Label *MakeLabel( vgui::Panel *parent, const char *text )
{
    vgui::Label *label = new vgui::Label( parent, "Label", "" ); SetLabelText( label, text ); label->SetMouseInputEnabled( false );
    label->SetContentAlignment( vgui::Label::a_west ); return label;
}
static void Choices( CHL2MPDropdown *combo, const char *choices )
{
    char buffer[512]; Q_strncpy( buffer, choices, sizeof( buffer ) ); char *part = buffer;
    while ( part && *part ) { char *next = strchr( part, '|' ); if ( next ) *next++ = 0; combo->AddItem( part, NULL ); part = next; }
    combo->SilentActivateItemByRow( 0 );
}
static bool SameServer( const gameserveritem_t &a, const gameserveritem_t &b )
{
    return a.m_NetAdr.GetIP() == b.m_NetAdr.GetIP() && a.m_NetAdr.GetConnectionPort() == b.m_NetAdr.GetConnectionPort();
}
static bool ParseAddress( const char *text, uint32 &ip, uint16 &port )
{
    // Numeric endpoints only: never interpolate untrusted server text into a console command.
    ip = 0; port = 27015;
    for ( int part = 0; part < 5; ++part )
    {
        if ( *text < '0' || *text > '9' ) return false;
        unsigned value = 0, limit = part == 4 ? 65535 : 255;
        do { value = value * 10 + ( *text++ - '0' ); if ( value > limit ) return false; } while ( *text >= '0' && *text <= '9' );
        if ( part < 4 ) ip = ( ip << 8 ) | value; else { if ( !value ) return false; port = uint16( value ); }
        if ( part < 3 ) { if ( *text++ != '.' ) return false; }
        else if ( !*text ) return ip != 0;
        else if ( part == 3 ) { if ( *text++ != ':' ) return false; }
        else return false;
    }
    return false;
}

class CServerRow : public CHL2MPSettingsButton
{
    DECLARE_CLASS_SIMPLE( CServerRow, CHL2MPSettingsButton );
public:
    CServerRow( vgui::Panel *parent, vgui::Panel *target, int index, const gameserveritem_t &server ) : BaseClass( parent, "", VarArgs( "select_%d", index ) ), m_Index( index ), m_Target( target ), m_Selected( false )
    {
        RemoveActionSignalTarget( parent ); AddActionSignalTarget( target );
        m_Labels[0] = MakeLabel( this, server.GetName() ); m_Labels[1] = MakeLabel( this, server.m_szMap );
        m_Labels[2] = MakeLabel( this, VarArgs( "%d / %d", server.m_nPlayers, server.m_nMaxPlayers ) );
        m_Labels[3] = MakeLabel( this, server.m_bHadSuccessfulResponse ? VarArgs( "%d ms", server.m_nPing ) : "--" );
        m_Labels[4] = MakeLabel( this, server.m_bPassword ? "PASSWORD" : server.m_bSecure ? "VAC" : "OPEN" );
    }
    void Style( vgui::HFont font, float scale, bool selected )
    {
        SetTall( int( 54 * scale ) ); m_Selected = selected; Active( selected );
        for ( int i = 0; i < 5; ++i )
        {
            m_Labels[i]->SetFont( font ); m_Labels[i]->SetFgColor( HL2MPServerTheme::Text() );
            m_Labels[i]->InvalidateLayout( true );
        }
        InvalidateLayout( true );
    }
    void PerformLayout() OVERRIDE
    {
        BaseClass::PerformLayout(); const float columns[] = { 0, .43f, .66f, .78f, .88f, 1 };
        for ( int i = 0; i < 5; ++i ) m_Labels[i]->SetBounds( int( GetWide() * columns[i] ) + 8, 0, int( GetWide() * ( columns[i+1] - columns[i] ) ) - 12, GetTall() );
    }
    void PaintBackground() OVERRIDE
    {
        const bool hover = HL2MPControlHighlighted( this );
        HL2MPServerTheme::Rounded( 0, 0, GetWide(), GetTall(), m_Selected ? HL2MPServerTheme::Orange() : Color( hover ? 56 : 25, hover ? 56 : 25, hover ? 56 : 25, 255 ) );
    }
    void OnMouseDoublePressed( vgui::MouseCode code ) OVERRIDE
    {
        if ( code == MOUSE_LEFT ) { m_Target->OnCommand( VarArgs( "select_%d", m_Index ) ); m_Target->OnCommand( "connect" ); }
    }
    void OnKeyCodeTyped( vgui::KeyCode code ) OVERRIDE
    {
        if ( code == KEY_ENTER ) m_Target->OnCommand( "connect" );
        else if ( code == KEY_ESCAPE ) m_Target->OnCommand( "back" );
        else if ( code == KEY_UP || code == KEY_DOWN ) m_Target->OnCommand( code == KEY_UP ? "previous" : "next" );
        else BaseClass::OnKeyCodeTyped( code );
    }
private:
    int m_Index; vgui::Panel *m_Target; bool m_Selected; vgui::Label *m_Labels[5];
};

class CBrowserDialog : public vgui::EditablePanel
{
    DECLARE_CLASS_SIMPLE( CBrowserDialog, vgui::EditablePanel );
public:
    CBrowserDialog( vgui::Panel *parent ) : BaseClass( parent, "BrowserDialog" )
    {
        SetVisible( false ); SetZPos( 200 ); title = MakeLabel( this, "" ); body = MakeLabel( this, "" ); body->SetWrap( true );
        entry = new vgui::TextEntry( this, "AddressOrPassword" ); entry->SetMaximumCharCount( 63 );
        yes = new CHL2MPSettingsButton( this, "", "dialog_accept" ); no = new CHL2MPSettingsButton( this, "CANCEL", "dialog_cancel" );
        yes->ServerStyle( true ); no->ServerStyle();
        yes->RemoveActionSignalTarget( this ); yes->AddActionSignalTarget( parent ); no->RemoveActionSignalTarget( this ); no->AddActionSignalTarget( parent );
    }
    void OnKeyCodeTyped( vgui::KeyCode code ) OVERRIDE
    {
        if ( code == KEY_ESCAPE || code == KEY_ENTER ) GetParent()->OnCommand( code == KEY_ESCAPE ? "dialog_cancel" : "dialog_accept" );
        else BaseClass::OnKeyCodeTyped( code );
    }
    void PaintBackground() OVERRIDE { vgui::surface()->DrawSetColor( Color( 24, 24, 24, 250 ) ); vgui::surface()->DrawFilledRect( 0, 0, GetWide(), GetTall() ); }
    vgui::Label *title, *body; vgui::TextEntry *entry; CHL2MPSettingsButton *yes, *no;
};

class CHL2MPServerBrowser : public vgui::EditablePanel, public ISteamMatchmakingServerListResponse, public ISteamMatchmakingPingResponse
{
    DECLARE_CLASS_SIMPLE( CHL2MPServerBrowser, vgui::EditablePanel );
public:
    CHL2MPServerBrowser( vgui::Panel *parent );
    ~CHL2MPServerBrowser();
    void Open();
    void OnCommand( const char *command ) OVERRIDE;
    void OnTick() OVERRIDE;
    void OnKeyCodeTyped( vgui::KeyCode code ) OVERRIDE;
    void PerformLayout() OVERRIDE;
    void PaintBackground() OVERRIDE;
    void ServerResponded( HServerListRequest request, int index ) OVERRIDE;
    void ServerFailedToRespond( HServerListRequest request, int index ) OVERRIDE;
    void RefreshComplete( HServerListRequest request, EMatchMakingServerResponse response ) OVERRIDE;
    void ServerResponded( gameserveritem_t &server ) OVERRIDE;
    void ServerFailedToRespond() OVERRIDE;
private:
    void CancelQueries(); void Refresh(); void Rebuild(); void UpdateDetails(); void AddServer( const gameserveritem_t &server );
    void Close(); void ShowDialog( bool password ); void HideDialog(); void Connect( const char *password );
    bool Compatible( const gameserveritem_t &server ) const; bool IsFavorite( const gameserveritem_t &server ) const;
    bool IsBlocked( const gameserveritem_t &server ) const; void SaveBlocked(); bool Matches( const gameserveritem_t &server ) const;
    AppId_t QueryApp() const { return m_Filters[GAME]->GetActiveItem() == 1 ? 320 : CurrentApp(); }
    CUtlVector<gameserveritem_t> m_Servers, m_Blocked;
    CUtlVector<int> m_Visible; CUtlVector<CServerRow *> m_Rows;
    HServerListRequest m_Request; HServerQuery m_PingRequest;
    Source m_Source; int m_Selected, m_Sort, m_Width, m_Height; bool m_Ascending, m_Dirty, m_Searching, m_PasswordDialog;
    double m_NextRebuild; float m_Scale; CUtlString m_FilterState, m_Message; int m_LastGame;
    gameserveritem_t m_Connecting;
    vgui::HFont m_Font, m_Small, m_Title;
    vgui::Label *m_Heading, *m_Count, *m_Details, *m_Hint, *m_SearchLabel, *m_MapLabel;
    vgui::TextEntry *m_Search, *m_Map;
    CHL2MPDropdown *m_Filters[FILTER_COUNT];
    CHL2MPSettingsButton *m_Back, *m_Tabs[7], *m_Headers[5], *m_Refresh, *m_Add, *m_Favorite, *m_Block, *m_Connect;
    vgui::PanelListPanel *m_List; CBrowserDialog *m_Dialog;
};

CHL2MPServerBrowser::CHL2MPServerBrowser( vgui::Panel *parent ) : BaseClass( parent, "ServerBrowser" ), m_Request( NULL ), m_PingRequest( HSERVERQUERY_INVALID ), m_Source( INTERNET ), m_Selected( -1 ), m_Sort( 3 ), m_Width( 0 ), m_Height( 0 ), m_Ascending( true ), m_Dirty( false ), m_Searching( false ), m_PasswordDialog( false ), m_NextRebuild( 0 ), m_Scale( 1 ), m_LastGame( 0 )
{
    SetVisible( false ); SetZPos( 100 ); SetKeyBoardInputEnabled( true ); SetMouseInputEnabled( true );
    m_Font = m_Small = m_Title = 0;
    m_Heading = MakeLabel( this, "COMMUNITY SERVERS" ); m_Count = MakeLabel( this, "" ); m_Details = MakeLabel( this, "Select a server to see its details." ); m_Details->SetWrap( true );
    m_Hint = MakeLabel( this, "" ); m_Hint->SetWrap( true ); m_SearchLabel = MakeLabel( this, "SEARCH NAME / TAGS" ); m_MapLabel = MakeLabel( this, "MAP" );
    m_Search = new vgui::TextEntry( this, "Search" ); m_Search->SetMaximumCharCount( 127 ); m_Map = new vgui::TextEntry( this, "Map" ); m_Map->SetMaximumCharCount( 63 );
    m_Back = new CHL2MPSettingsButton( this, "<  BACK", "back" );
    for ( int i = 0; i < 7; ++i ) m_Tabs[i] = new CHL2MPSettingsButton( this, s_Sources[i], VarArgs( "source_%d", i ) );
    for ( int i = 0; i < 5; ++i ) { m_Headers[i] = new CHL2MPSettingsButton( this, s_Columns[i], VarArgs( "sort_%d", i ) ); m_Headers[i]->SetContentAlignment( vgui::Label::a_west ); m_Headers[i]->SetTextInset( 8, 0 ); }
    for ( int i = 0; i < FILTER_COUNT; ++i ) { m_Filters[i] = new CHL2MPDropdown( this, VarArgs( "Filter%d", i ) ); m_Filters[i]->ServerStyle(); }
    Choices( m_Filters[GAME], "THIS MOD|RETAIL HL2: DEATHMATCH" ); Choices( m_Filters[POPULATION], "ANY POPULATION|HAS PLAYERS|HAS OPEN SLOTS|EMPTY" );
    Choices( m_Filters[PING], "ANY PING|UNDER 50 ms|UNDER 100 ms|UNDER 150 ms|UNDER 250 ms" ); Choices( m_Filters[SECURITY], "ANY SECURITY|VAC SECURED" ); Choices( m_Filters[PASSWORD], "ANY ACCESS|NO PASSWORD|PASSWORD REQUIRED" );
    m_Refresh = new CHL2MPSettingsButton( this, "REFRESH", "refresh" ); m_Add = new CHL2MPSettingsButton( this, "FIND BY ADDRESS", "address" );
    m_Favorite = new CHL2MPSettingsButton( this, "ADD FAVORITE", "favorite" ); m_Block = new CHL2MPSettingsButton( this, "BLOCK", "block" ); m_Connect = new CHL2MPSettingsButton( this, "CONNECT", "connect" );
    CHL2MPSettingsButton *buttons[] = { m_Back, m_Refresh, m_Add, m_Favorite, m_Block, m_Connect };
    for ( int i = 0; i < ARRAYSIZE( buttons ); ++i ) buttons[i]->ServerStyle( buttons[i] == m_Connect );
    for ( int i = 0; i < 7; ++i ) m_Tabs[i]->ServerStyle();
    for ( int i = 0; i < 5; ++i ) m_Headers[i]->ServerStyle();
    m_List = new vgui::PanelListPanel( this, "Servers" ); m_List->SetFirstColumnWidth( 0 ); m_List->SetVerticalBufferPixels( 6 ); HL2MPStyleScrollBar( m_List->GetScrollbar() );
    m_Dialog = new CBrowserDialog( this );
    KeyValues *blocked = new KeyValues( "BlockedServers" );
    if ( blocked->LoadFromFile( filesystem, "cfg/serverbrowser_blocked.vdf", "MOD" ) )
        for ( KeyValues *entry = blocked->GetFirstSubKey(); entry && m_Blocked.Count() < 512; entry = entry->GetNextKey() )
        {
            uint32 ip; uint16 port; if ( !ParseAddress( entry->GetString( "address" ), ip, port ) ) continue;
            gameserveritem_t server; server.m_NetAdr.Init( ip, uint16( entry->GetInt( "queryport", port ) ), port ); server.m_nAppID = entry->GetInt( "app" ); m_Blocked.AddToTail( server );
        }
    blocked->deleteThis(); vgui::ivgui()->AddTickSignal( GetVPanel(), 100 );
}
CHL2MPServerBrowser::~CHL2MPServerBrowser() { CancelQueries(); HideDialog(); vgui::ivgui()->RemoveTickSignal( GetVPanel() ); }
void CHL2MPServerBrowser::Open()
{
    if ( IsVisible() ) return; SetVisible( true ); MoveToFront(); m_Width = 0; InvalidateLayout( true ); m_Back->RequestFocus(); Refresh();
}
void CHL2MPServerBrowser::CancelQueries()
{
    if ( Servers() ) { if ( m_Request ) Servers()->ReleaseRequest( m_Request ); if ( m_PingRequest != HSERVERQUERY_INVALID ) Servers()->CancelServerQuery( m_PingRequest ); }
    m_Request = NULL; m_PingRequest = HSERVERQUERY_INVALID; m_Searching = false;
}
void CHL2MPServerBrowser::Close()
{
    CancelQueries(); HideDialog(); for ( int i = 0; i < FILTER_COUNT; ++i ) m_Filters[i]->HideMenu(); SetVisible( false ); GetParent()->OnCommand( "browser_closed" );
}
void CHL2MPServerBrowser::Refresh()
{
    CancelQueries(); m_Servers.Purge(); m_Selected = -1; m_Dirty = true; m_Message = "";
    if ( m_Source == BLOCKED ) { m_Servers = m_Blocked; return; }
    if ( !Servers() || !QueryApp() ) { m_Message = "Steam server browsing is unavailable. Check that Steam is running."; return; }
    m_Searching = true;
    // Query the game's pool, then apply reversible local filters without restarting discovery.
    switch ( m_Source )
    {
    case INTERNET: m_Request = Servers()->RequestInternetServerList( QueryApp(), NULL, 0, this ); break;
    case FAVORITES: m_Request = Servers()->RequestFavoritesServerList( QueryApp(), NULL, 0, this ); break;
    case HISTORY: m_Request = Servers()->RequestHistoryServerList( QueryApp(), NULL, 0, this ); break;
    case LAN: m_Request = Servers()->RequestLANServerList( QueryApp(), this ); break;
    case FRIENDS: m_Request = Servers()->RequestFriendsServerList( QueryApp(), NULL, 0, this ); break;
    case SPECTATE: m_Request = Servers()->RequestSpectatorServerList( QueryApp(), NULL, 0, this ); break;
    default: break;
    }
    if ( !m_Request ) { m_Searching = false; m_Message = "Steam could not start the server search. Try Refresh."; }
}
void CHL2MPServerBrowser::AddServer( const gameserveritem_t &server )
{
    FOR_EACH_VEC( m_Servers, i ) if ( SameServer( m_Servers[i], server ) ) { m_Servers[i] = server; m_Dirty = true; return; }
    if ( m_Servers.Count() < 4096 ) m_Servers.AddToTail( server ); m_Dirty = true;
}
void CHL2MPServerBrowser::ServerResponded( HServerListRequest request, int index )
{
    if ( request != m_Request || !Servers() ) return; gameserveritem_t *server = Servers()->GetServerDetails( request, index );
    if ( server ) AddServer( *server );
}
void CHL2MPServerBrowser::ServerFailedToRespond( HServerListRequest request, int index )
{
    // Keep unreachable favorites/history visible so the user can remove stale entries.
    if ( m_Source == FAVORITES || m_Source == HISTORY ) ServerResponded( request, index );
}
void CHL2MPServerBrowser::RefreshComplete( HServerListRequest request, EMatchMakingServerResponse response )
{
    if ( request != m_Request ) return; m_Searching = false; m_Dirty = true;
    if ( response == eServerFailedToRespond ) m_Message = "The server search did not respond. Try Refresh or Find by Address.";
}
void CHL2MPServerBrowser::ServerResponded( gameserveritem_t &server )
{
    m_PingRequest = HSERVERQUERY_INVALID; AddServer( server );
    FOR_EACH_VEC( m_Servers, i ) if ( SameServer( m_Servers[i], server ) ) m_Selected = i;
    m_Message = "Server found. Select Connect to join, or Add Favorite to save it.";
}
void CHL2MPServerBrowser::ServerFailedToRespond()
{
    m_PingRequest = HSERVERQUERY_INVALID; m_Message = "No reply from that address. Check the server's query port and try again.";
}
bool CHL2MPServerBrowser::Compatible( const gameserveritem_t &server ) const
{
    return server.m_bHadSuccessfulResponse && server.m_nAppID == CurrentApp() && ( !Q_stricmp( server.m_szGameDir, "mod_hl2mp" ) || !Q_stricmp( server.m_szGameDir, "hl2mp" ) );
}
bool CHL2MPServerBrowser::IsFavorite( const gameserveritem_t &server ) const
{
    if ( !Favorites() ) return false;
    for ( int i = 0; i < Favorites()->GetFavoriteGameCount(); ++i )
    {
        AppId_t app; uint32 ip, flags, played; uint16 connection, query;
        if ( Favorites()->GetFavoriteGame( i, &app, &ip, &connection, &query, &flags, &played ) && app == server.m_nAppID && ip == server.m_NetAdr.GetIP() && connection == server.m_NetAdr.GetConnectionPort() && ( flags & k_unFavoriteFlagFavorite ) ) return true;
    }
    return false;
}
bool CHL2MPServerBrowser::IsBlocked( const gameserveritem_t &server ) const { FOR_EACH_VEC( m_Blocked, i ) if ( SameServer( server, m_Blocked[i] ) ) return true; return false; }
void CHL2MPServerBrowser::SaveBlocked()
{
    KeyValues *root = new KeyValues( "BlockedServers" );
    FOR_EACH_VEC( m_Blocked, i )
    {
        KeyValues *entry = new KeyValues( VarArgs( "%d", i ) ); entry->SetString( "address", m_Blocked[i].m_NetAdr.GetConnectionAddressString() ); entry->SetInt( "queryport", m_Blocked[i].m_NetAdr.GetQueryPort() ); entry->SetInt( "app", m_Blocked[i].m_nAppID ); root->AddSubKey( entry );
    }
    if ( !root->SaveToFile( filesystem, "cfg/serverbrowser_blocked.vdf", "MOD" ) ) m_Message = "Could not save the blocked server list.";
    root->deleteThis();
}
bool CHL2MPServerBrowser::Matches( const gameserveritem_t &server ) const
{
    if ( m_Source == BLOCKED ) return true;
    if ( IsBlocked( server ) ) return false;
    // SDK Base also hosts unrelated mods. Do not mix those into this game's results.
    if ( server.m_bHadSuccessfulResponse && Q_stricmp( server.m_szGameDir, "mod_hl2mp" ) && Q_stricmp( server.m_szGameDir, "hl2mp" ) ) return false;
    char search[128], map[64]; ReadText( m_Search, search, sizeof( search ) ); ReadText( m_Map, map, sizeof( map ) );
    if ( search[0] && !Q_stristr( server.GetName(), search ) && !Q_stristr( server.m_szGameTags, search ) ) return false;
    if ( map[0] && !Q_stristr( server.m_szMap, map ) ) return false;
    const int population = m_Filters[POPULATION]->GetActiveItem(), ping = m_Filters[PING]->GetActiveItem(), password = m_Filters[PASSWORD]->GetActiveItem();
    if ( ( population == 1 && server.m_nPlayers == 0 ) || ( population == 2 && server.m_nPlayers >= server.m_nMaxPlayers ) || ( population == 3 && server.m_nPlayers > 0 ) ) return false;
    const int maxPing[] = { 0, 50, 100, 150, 250 }; if ( ping > 0 && ( !server.m_bHadSuccessfulResponse || server.m_nPing > maxPing[ping] ) ) return false;
    if ( m_Filters[SECURITY]->GetActiveItem() == 1 && !server.m_bSecure ) return false;
    return !( ( password == 1 && server.m_bPassword ) || ( password == 2 && !server.m_bPassword ) );
}
void CHL2MPServerBrowser::Rebuild()
{
    m_Dirty = false; m_NextRebuild = Plat_FloatTime() + .3; const int scroll = m_List->GetScrollbar()->GetValue();
    m_List->DeleteAllItems(); m_Rows.Purge(); m_Visible.Purge();
    FOR_EACH_VEC( m_Servers, i ) if ( Matches( m_Servers[i] ) ) m_Visible.AddToTail( i );
    if ( m_Visible.Count() > 1 ) std::sort( m_Visible.Base(), m_Visible.Base() + m_Visible.Count(), [this]( int ai, int bi ) {
        const gameserveritem_t &a = m_Servers[ai], &b = m_Servers[bi]; int result = 0;
        switch ( m_Sort ) { case 0: result = Q_stricmp( a.GetName(), b.GetName() ); break; case 1: result = Q_stricmp( a.m_szMap, b.m_szMap ); break; case 2: result = a.m_nPlayers - b.m_nPlayers; break; case 3: result = ( a.m_bHadSuccessfulResponse ? a.m_nPing : 99999 ) - ( b.m_bHadSuccessfulResponse ? b.m_nPing : 99999 ); break; case 4: result = int( a.m_bPassword ) - int( b.m_bPassword ); break; }
        return result ? ( m_Ascending ? result < 0 : result > 0 ) : ai < bi;
    } );
    if ( m_Visible.Find( m_Selected ) == m_Visible.InvalidIndex() ) m_Selected = -1;
    FOR_EACH_VEC( m_Visible, i )
    {
        CServerRow *row = new CServerRow( m_List, this, m_Visible[i], m_Servers[m_Visible[i]] ); row->Style( m_Font, m_Scale, m_Selected == m_Visible[i] ); m_Rows.AddToTail( row ); m_List->AddItem( NULL, row );
    }
    m_List->InvalidateLayout( true ); m_List->GetScrollbar()->SetValue( scroll ); UpdateDetails();
}
void CHL2MPServerBrowser::UpdateDetails()
{
    const bool selected = m_Servers.IsValidIndex( m_Selected );
    m_Favorite->SetEnabled( selected && Favorites() ); m_Block->SetEnabled( selected && ( m_Source != HISTORY || Favorites() ) );
    m_Block->SetText( m_Source == BLOCKED ? "UNBLOCK" : m_Source == HISTORY ? "REMOVE HISTORY" : "BLOCK" );
    m_Connect->SetEnabled( selected && m_Source != BLOCKED && Compatible( m_Servers[m_Selected] ) && !HL2MPCoordinator().Busy() ); m_Connect->Active( m_Connect->IsEnabled() );
    if ( !selected ) { m_Details->SetText( m_Visible.Count() ? "Select a server to see its details. Double-click to connect." : m_Searching ? "Discovering servers..." : "No servers match. Change filters, refresh, or find a server by address." ); m_Favorite->SetText( "ADD FAVORITE" ); return; }
    const gameserveritem_t &server = m_Servers[m_Selected]; m_Favorite->SetText( IsFavorite( server ) ? "REMOVE FAVORITE" : "ADD FAVORITE" );
    SetLabelText( m_Details, VarArgs( "%s  /  %s\n%s  /  %d players, %d bots  /  %s%s\n%s", server.GetName(), server.m_NetAdr.GetConnectionAddressString(), server.m_szMap, MAX( 0, server.m_nPlayers - server.m_nBotPlayers ), server.m_nBotPlayers, server.m_bSecure ? "VAC secured" : "Not VAC secured", server.m_bPassword ? "  /  Password required" : "", !server.m_bHadSuccessfulResponse ? "Server not responding." : !Compatible( server ) ? "This server requires a different game build. Browsing and favorites are available." : HL2MPCoordinator().Busy() ? "Finish or cancel matchmaking before joining a community server." : server.m_szGameTags ) );
}
void CHL2MPServerBrowser::ShowDialog( bool password )
{
    for ( int i = 0; i < FILTER_COUNT; ++i ) m_Filters[i]->HideMenu();
    m_PasswordDialog = password; m_Dialog->title->SetText( password ? "SERVER PASSWORD" : "FIND A SERVER" );
    SetLabelText( m_Dialog->body, password ? m_Connecting.GetName() : "Enter an IPv4 address and query port, for example 192.168.1.20:27015. The server will be checked before you connect." );
    m_Dialog->entry->SetText( "" ); m_Dialog->entry->SetTextHidden( password ); m_Dialog->yes->SetText( password ? "CONNECT" : "FIND SERVER" );
    m_Dialog->SetVisible( true ); m_Dialog->MoveToFront(); vgui::input()->SetAppModalSurface( m_Dialog->GetVPanel() ); m_Dialog->entry->RequestFocus();
}
void CHL2MPServerBrowser::HideDialog()
{
    m_Dialog->entry->SetText( "" ); m_Dialog->SetVisible( false ); if ( vgui::input()->GetAppModalSurface() == m_Dialog->GetVPanel() ) vgui::input()->ReleaseAppModalSurface();
}
void CHL2MPServerBrowser::Connect( const char *password )
{
    if ( !Compatible( m_Connecting ) || IsBlocked( m_Connecting ) || HL2MPCoordinator().Busy() ) return;
    ConVarRef serverPassword( "password", true ); if ( serverPassword.IsValid() ) serverPassword.SetValue( password );
    if ( Favorites() ) Favorites()->AddFavoriteGame( m_Connecting.m_nAppID, m_Connecting.m_NetAdr.GetIP(), m_Connecting.m_NetAdr.GetConnectionPort(), m_Connecting.m_NetAdr.GetQueryPort(), k_unFavoriteFlagHistory, uint32( time( NULL ) ) );
    HL2MPLoading_SetContext( m_Connecting.m_szMap, "community", m_Connecting.GetName() );
    char command[80]; Q_snprintf( command, sizeof( command ), "connect %s\n", m_Connecting.m_NetAdr.GetConnectionAddressString() ); Close(); engine->ClientCmd_Unrestricted( command );
}
void CHL2MPServerBrowser::OnCommand( const char *command )
{
    if ( !Q_strcmp( command, "refresh_fonts" ) ) { m_Width = 0; InvalidateLayout( true ); return; }
    if ( !Q_strcmp( command, "back" ) ) { if ( m_Dialog->IsVisible() ) HideDialog(); else Close(); }
    else if ( !Q_strncmp( command, "source_", 7 ) ) { int source = atoi( command + 7 ); if ( source >= 0 && source < 7 ) { m_Source = Source( source ); Refresh(); InvalidateLayout( true ); } }
    else if ( !Q_strncmp( command, "sort_", 5 ) ) { int column = atoi( command + 5 ); if ( column >= 0 && column < 5 ) { m_Ascending = column == m_Sort ? !m_Ascending : true; m_Sort = column; m_Dirty = true; } }
    else if ( !Q_strncmp( command, "select_", 7 ) ) { int index = atoi( command + 7 ); if ( m_Servers.IsValidIndex( index ) ) { m_Selected = index; FOR_EACH_VEC( m_Rows, i ) m_Rows[i]->Style( m_Font, m_Scale, m_Visible[i] == index ); UpdateDetails(); } }
    else if ( !Q_strcmp( command, "next" ) || !Q_strcmp( command, "previous" ) )
    {
        int row = m_Visible.Find( m_Selected ) + ( !Q_strcmp( command, "next" ) ? 1 : -1 );
        if ( m_Visible.Count() ) { row = clamp( row, 0, m_Visible.Count() - 1 ); OnCommand( VarArgs( "select_%d", m_Visible[row] ) ); m_Rows[row]->RequestFocus(); m_List->ScrollToItem( row ); }
    }
    else if ( !Q_strcmp( command, "refresh" ) ) { if ( m_Searching || m_PingRequest != HSERVERQUERY_INVALID ) { CancelQueries(); m_Message = "Search stopped."; } else Refresh(); }
    else if ( !Q_strcmp( command, "address" ) ) { if ( Servers() ) ShowDialog( false ); }
    else if ( !Q_strcmp( command, "dialog_cancel" ) ) HideDialog();
    else if ( !Q_strcmp( command, "dialog_accept" ) && m_Dialog->IsVisible() )
    {
        char value[256]; ReadText( m_Dialog->entry, value, sizeof( value ) );
        if ( m_PasswordDialog ) { HideDialog(); Connect( value ); memset( value, 0, sizeof( value ) ); }
        else
        {
            uint32 ip; uint16 port; if ( !ParseAddress( value, ip, port ) ) { m_Dialog->body->SetText( "Enter a valid IPv4 address with an optional port from 1 to 65535." ); return; }
            HideDialog(); if ( !Servers() ) return;
            if ( m_PingRequest != HSERVERQUERY_INVALID ) Servers()->CancelServerQuery( m_PingRequest );
            m_Search->SetText( "" ); m_Map->SetText( "" ); for ( int i = 1; i < FILTER_COUNT; ++i ) m_Filters[i]->SilentActivateItemByRow( 0 );
            if ( m_Source == BLOCKED ) { m_Source = INTERNET; Refresh(); InvalidateLayout( true ); }
            m_PingRequest = Servers()->PingServer( ip, port, this ); m_Message = m_PingRequest == HSERVERQUERY_INVALID ? "Could not start the address lookup." : "Checking server address...";
        }
    }
    else if ( !Q_strcmp( command, "connect" ) && m_Servers.IsValidIndex( m_Selected ) && m_Connect->IsEnabled() )
    {
        m_Connecting = m_Servers[m_Selected]; if ( m_Connecting.m_bPassword ) ShowDialog( true ); else Connect( "" );
    }
    else if ( !Q_strcmp( command, "favorite" ) && m_Servers.IsValidIndex( m_Selected ) && Favorites() )
    {
        const gameserveritem_t &s = m_Servers[m_Selected];
        if ( IsFavorite( s ) ) Favorites()->RemoveFavoriteGame( s.m_nAppID, s.m_NetAdr.GetIP(), s.m_NetAdr.GetConnectionPort(), s.m_NetAdr.GetQueryPort(), k_unFavoriteFlagFavorite );
        else Favorites()->AddFavoriteGame( s.m_nAppID, s.m_NetAdr.GetIP(), s.m_NetAdr.GetConnectionPort(), s.m_NetAdr.GetQueryPort(), k_unFavoriteFlagFavorite, uint32( time( NULL ) ) );
        UpdateDetails(); if ( m_Source == FAVORITES ) Refresh();
    }
    else if ( !Q_strcmp( command, "block" ) && m_Servers.IsValidIndex( m_Selected ) )
    {
        const gameserveritem_t &s = m_Servers[m_Selected];
        if ( m_Source == HISTORY )
        {
            if ( Favorites() ) Favorites()->RemoveFavoriteGame( s.m_nAppID, s.m_NetAdr.GetIP(), s.m_NetAdr.GetConnectionPort(), s.m_NetAdr.GetQueryPort(), k_unFavoriteFlagHistory );
            Refresh(); return;
        }
        if ( IsBlocked( s ) ) { FOR_EACH_VEC_BACK( m_Blocked, i ) if ( SameServer( s, m_Blocked[i] ) ) m_Blocked.Remove( i ); }
        else if ( m_Blocked.Count() < 512 ) m_Blocked.AddToTail( s );
        SaveBlocked(); if ( m_Source == BLOCKED ) Refresh(); else m_Dirty = true;
    }
    else BaseClass::OnCommand( command );
}
void CHL2MPServerBrowser::OnKeyCodeTyped( vgui::KeyCode code )
{
    if ( code == KEY_ESCAPE ) for ( int i = 0; i < FILTER_COUNT; ++i ) if ( m_Filters[i]->GetMenu()->IsVisible() ) { m_Filters[i]->HideMenu(); return; }
    if ( code == KEY_ESCAPE ) OnCommand( "back" ); else if ( code == KEY_F5 ) OnCommand( "refresh" ); else BaseClass::OnKeyCodeTyped( code );
}
void CHL2MPServerBrowser::OnTick()
{
    if ( !IsVisible() ) return;
    int w, h; vgui::surface()->GetScreenSize( w, h ); if ( w != m_Width || h != m_Height ) InvalidateLayout( true );
    char search[128], map[64], state[256]; ReadText( m_Search, search, sizeof( search ) ); ReadText( m_Map, map, sizeof( map ) );
    Q_snprintf( state, sizeof( state ), "%s\n%s\n%d%d%d%d", search, map, m_Filters[POPULATION]->GetActiveItem(), m_Filters[PING]->GetActiveItem(), m_Filters[SECURITY]->GetActiveItem(), m_Filters[PASSWORD]->GetActiveItem() );
    if ( m_FilterState != state ) { m_FilterState = state; m_Dirty = true; }
    const int game = m_Filters[GAME]->GetActiveItem(); if ( game != m_LastGame ) { m_LastGame = game; Refresh(); }
    if ( m_Dirty && Plat_FloatTime() >= m_NextRebuild ) Rebuild();
    UpdateDetails(); m_Refresh->SetText( m_Searching || m_PingRequest != HSERVERQUERY_INVALID ? "STOP SEARCH" : "REFRESH" ); m_Add->SetEnabled( Servers() != NULL );
    m_Count->SetText( VarArgs( "%d SERVERS%s", m_Visible.Count(), m_Searching ? "  /  SEARCHING..." : "" ) );
    m_Hint->SetText( m_Message.Length() ? m_Message.String() : m_Source == BLOCKED ? "Blocked servers are hidden from the other tabs. Select a server to unblock it." : "Community servers use their own rules. Favorites and history are saved through Steam." );
    for ( int i = 0; i < 5; ++i ) { m_Headers[i]->Active( i == m_Sort ); m_Headers[i]->SetText( VarArgs( "%s%s", s_Columns[i], i == m_Sort ? m_Ascending ? "  +" : "  -" : "" ) ); }
}
void CHL2MPServerBrowser::PerformLayout()
{
    BaseClass::PerformLayout(); int w, h; vgui::surface()->GetScreenSize( w, h ); SetBounds( 0, 0, w, h );
    const float s = m_Scale = MAX( .65f, MIN( h / 720.0f, w / 1024.0f ) );
    if ( w != m_Width || h != m_Height )
    {
        m_Font = vgui::surface()->CreateFont(); m_Small = vgui::surface()->CreateFont(); m_Title = vgui::surface()->CreateFont();
        vgui::surface()->SetFontGlyphSet( m_Font, "GorDIN", MAX( 13, int( 18 * s ) ), 400, 0, 0, vgui::ISurface::FONTFLAG_ANTIALIAS );
        vgui::surface()->SetFontGlyphSet( m_Small, "GorDIN", MAX( 11, int( 15 * s ) ), 400, 0, 0, vgui::ISurface::FONTFLAG_ANTIALIAS );
        vgui::surface()->SetFontGlyphSet( m_Title, "GorDIN Bold", int( 30 * s ), 600, 0, 0, vgui::ISurface::FONTFLAG_ANTIALIAS );
    }
    m_Width = w; m_Height = h; const int margin = int( 42 * s ), width = w - margin * 2;
    m_Back->SetFont( m_Small ); m_Back->SetBounds( margin - 10, int( 20 * s ), int( 95 * s ), int( 35 * s ) );
    m_Heading->SetFont( m_Title ); m_Heading->SetFgColor( HL2MPServerTheme::Orange() ); m_Heading->SetBounds( margin + int( 110 * s ), int( 20 * s ), width - int( 110 * s ), int( 40 * s ) );
    for ( int i = 0; i < 7; ++i ) { m_Tabs[i]->SetFont( m_Small ); m_Tabs[i]->SetBounds( margin + width * i / 7, int( 78 * s ), width / 7, int( 35 * s ) ); m_Tabs[i]->Active( i == m_Source ); }
    m_SearchLabel->SetBounds( margin, int( 127 * s ), width * .60f, int( 20 * s ) ); m_MapLabel->SetBounds( margin + width * .64f, int( 127 * s ), width * .36f, int( 20 * s ) );
    m_Search->SetBounds( margin, int( 151 * s ), width * .61f, int( 32 * s ) ); m_Map->SetBounds( margin + width * .64f, int( 151 * s ), width * .36f, int( 32 * s ) );
    for ( int i = 0; i < FILTER_COUNT; ++i ) { m_Filters[i]->Style( m_Small, s ); m_Filters[i]->SetBounds( margin + width * i / FILTER_COUNT, int( 194 * s ), width / FILTER_COUNT - 8, int( 33 * s ) ); }
    const int tableW = width - int( 14 * s ); const float columns[] = { 0, .43f, .66f, .78f, .88f, 1 };
    for ( int i = 0; i < 5; ++i ) { m_Headers[i]->SetFont( m_Small ); m_Headers[i]->SetBounds( margin + tableW * columns[i], int( 240 * s ), tableW * ( columns[i+1] - columns[i] ), int( 32 * s ) ); }
    const int tableY = int( 277 * s ), tableBottom = h - int( 195 * s );
    m_List->SetBounds( margin, tableY, width, MAX( 70, tableBottom - tableY ) ); m_List->SetPaintBackgroundEnabled( false ); m_List->SetBorder( NULL ); m_List->GetScrollbar()->SetWide( MAX( 10, int( 12 * s ) ) );
    FOR_EACH_VEC( m_Rows, i ) m_Rows[i]->Style( m_Font, s, m_Visible[i] == m_Selected );
    m_Count->SetBounds( margin, tableBottom + int( 8 * s ), width, int( 25 * s ) ); m_Details->SetBounds( margin, tableBottom + int( 38 * s ), width, int( 54 * s ) );
    m_Hint->SetBounds( margin, h - int( 95 * s ), width, int( 30 * s ) );
    vgui::Label *labels[] = { m_Count, m_Details, m_Hint, m_SearchLabel, m_MapLabel }; for ( int i = 0; i < 5; ++i ) { labels[i]->SetFont( m_Small ); labels[i]->SetFgColor( i == 0 ? HL2MPServerTheme::Orange() : HL2MPServerTheme::Muted() ); }
    vgui::TextEntry *entries[] = { m_Search, m_Map, m_Dialog->entry }; for ( int i = 0; i < 3; ++i ) { entries[i]->SetFont( m_Font ); entries[i]->SetBgColor( Color( 24, 24, 24, 255 ) ); entries[i]->SetFgColor( HL2MPServerTheme::Text() ); entries[i]->SetBorder( NULL ); }
    CHL2MPSettingsButton *footer[] = { m_Refresh, m_Add, m_Favorite, m_Block, m_Connect };
    for ( int i = 0; i < 5; ++i ) { footer[i]->SetFont( m_Small ); footer[i]->SetBounds( margin + width * i / 5, h - int( 55 * s ), width / 5 - 8, int( 36 * s ) ); }
    m_Dialog->SetBounds( 0, 0, w, h ); const int dw = MIN( width, int( 640 * s ) ), dx = ( w - dw ) / 2, dy = h / 2 - int( 130 * s );
    m_Dialog->title->SetFont( m_Title ); m_Dialog->title->SetFgColor( Color( 238, 231, 213, 255 ) ); m_Dialog->title->SetBounds( dx, dy, dw, int( 44 * s ) );
    m_Dialog->body->SetFont( m_Font ); m_Dialog->body->SetFgColor( Color( 201, 212, 213, 255 ) ); m_Dialog->body->SetBounds( dx, dy + int( 60 * s ), dw, int( 70 * s ) );
    m_Dialog->entry->SetBounds( dx, dy + int( 140 * s ), dw, int( 36 * s ) );
    m_Dialog->yes->SetFont( m_Small ); m_Dialog->no->SetFont( m_Small ); m_Dialog->yes->SetBounds( dx, dy + int( 200 * s ), dw / 2 - 8, int( 42 * s ) ); m_Dialog->no->SetBounds( dx + dw / 2, dy + int( 200 * s ), dw / 2 - 8, int( 42 * s ) );
}
void CHL2MPServerBrowser::PaintBackground()
{
    vgui::ISurface *surface = vgui::surface(); const int w = GetWide(), h = GetTall();
    surface->DrawSetColor( Color( 32, 32, 32, 250 ) ); surface->DrawFilledRect( 0, 0, w, h );
    surface->DrawSetColor( Color( 26, 26, 26, 255 ) ); surface->DrawFilledRect( 0, 0, w, int( 68 * m_Scale ) ); surface->DrawFilledRect( 0, h - int( 65 * m_Scale ), w, h );
    surface->DrawSetColor( HL2MPServerTheme::Orange() ); surface->DrawFilledRect( int( 134 * m_Scale ), int( 20 * m_Scale ), int( 138 * m_Scale ), int( 56 * m_Scale ) );
}
}

vgui::Panel *HL2MPServerBrowser_Create( vgui::Panel *parent ) { return new CHL2MPServerBrowser( parent ); }
void HL2MPServerBrowser_Open( vgui::Panel *panel ) { static_cast<CHL2MPServerBrowser *>( panel )->Open(); }
