#include "cbase.h"
#include "hl2mp_loading.h"
#include "hl2mp_messages.h"
#include "vgui/ILocalize.h"
#include "GameUI/IGameUI.h"
#include "GameEventListener.h"
#include "filesystem.h"
#include "ienginevgui.h"
#include "vgui/ISurface.h"
#include "vgui/IScheme.h"
#include "vgui/IVGui.h"
#include "vgui_controls/EditablePanel.h"
#include "vgui_controls/Label.h"
#include "tier0/memdbgon.h"

namespace
{
vgui::VPANEL ChildNamed( vgui::VPANEL parent, const char *name )
{
    for ( int i = 0; i < vgui::ipanel()->GetChildCount( parent ); ++i )
    {
        vgui::VPANEL child = vgui::ipanel()->GetChild( parent, i );
        if ( !Q_stricmp( vgui::ipanel()->GetName( child ), name ) ) return child;
    }
    return 0;
}

vgui::VPANEL ErrorDialog()
{
    for ( int i = 0; i < vgui::surface()->GetPopupCount(); ++i )
    {
        vgui::VPANEL popup = vgui::surface()->GetPopup( i );
        if ( !vgui::ipanel()->IsVisible( popup ) || Q_stricmp( vgui::ipanel()->GetName( popup ), "LoadingDialog" ) ) continue;
        vgui::VPANEL progress = ChildNamed( popup, "Progress" ), info = ChildNamed( popup, "InfoLabel" );
        if ( progress && info && !vgui::ipanel()->IsVisible( progress ) && vgui::ipanel()->IsVisible( info ) ) return popup;
    }
    return 0;
}

void SetPanelText( vgui::VPANEL panel, const char *token )
{
    KeyValuesAD message( "SetText" ); message->SetString( "text", token );
    vgui::ipanel()->SendMessage( panel, message, 0 );
}

struct LoadingContext
{
    char map[128], mode[32], server[256];
};
LoadingContext s_Pending = {};
double s_PendingUntil = 0;

struct MapCard
{
    const char *map, *title, *description;
};
const MapCard s_Maps[] = {
    { "dm_lockdown", "#HL2MP_UI_90f856c19355", "#HL2MP_UI_6d88b7e2b876" },
    { "dm_overwatch", "#HL2MP_UI_e1ca2870940e", "#HL2MP_UI_0d0756e1b80f" },
    { "dm_powerhouse", "#HL2MP_UI_82da1eedfc4a", "#HL2MP_UI_7ea1af5c6717" },
    { "dm_resistance", "#HL2MP_UI_89be0e0647c5", "#HL2MP_UI_1dec0779a5ce" },
    { "dm_runoff", "#HL2MP_UI_c357ce3d342a", "#HL2MP_UI_125729e2f72c" },
    { "dm_steamlab", "#HL2MP_UI_1dd3334c17d1", "#HL2MP_UI_afa7c6ee249c" },
    { "dm_underpass", "#HL2MP_UI_4e02bef8e43a", "#HL2MP_UI_f6856c34f5de" }
};
const char *s_Tips[] = {
    "#HL2MP_UI_52073f6c2130",
    "#HL2MP_UI_9ab9e2bbdd8b",
    "#HL2MP_UI_ecb58bfa7a74",
    "#HL2MP_UI_1f9cd0b80015",
    "#HL2MP_UI_edb2f0a4fa38"
};

// A server-supplied name is display data, never a material path or console command.
void MapName( const char *source, char *dest, int length )
{
    Q_FileBase( source ? source : "", dest, length );
    Q_strlower( dest );
}

vgui::HFont LoadingFont( const char *face, int height, int weight = 400 )
{
    vgui::HFont font = vgui::surface()->CreateFont();
    if ( !vgui::surface()->SetFontGlyphSet( font, face, height, weight, 0, 0, vgui::ISurface::FONTFLAG_ANTIALIAS ) )
        vgui::surface()->SetFontGlyphSet( font, "Tahoma", height, weight, 0, 0, vgui::ISurface::FONTFLAG_ANTIALIAS );
    return font;
}

class CHL2MPLoading : public vgui::EditablePanel, public CGameEventListener
{
    DECLARE_CLASS_SIMPLE( CHL2MPLoading, vgui::EditablePanel );
public:
    explicit CHL2MPLoading( IGameUI *gameUI ) : BaseClass( NULL, "HL2MPLoading" ), m_GameUI( gameUI ),
        m_Texture( -1 ), m_MapIndex( -1 ), m_FontWidth( 0 ), m_FontHeight( 0 ), m_TipIndex( 0 ), m_Active( false ), m_ErrorDialog( 0 )
    {
        memset( &m_Context, 0, sizeof( m_Context ) );
        SetParent( enginevgui->GetPanel( PANEL_GAMEUIDLL ) );
        SetProportional( false ); SetKeyBoardInputEnabled( false ); SetMouseInputEnabled( false );
        m_Logo = Label( "Logo", "deathmatch" );
        m_Tag = Label( "Tag", "#HL2MP_UI_dd67c326ae8a" );
        m_Mode = Label( "Mode", "" );
        m_Map = Label( "Map", "#HL2MP_UI_a0e1a519f04a" );
        m_Description = Label( "Description", "" ); m_Description->SetWrap( true );
        m_Server = Label( "Server", "" );
        m_TipHeading = Label( "TipHeading", "#HL2MP_UI_12c42506eefd" );
        m_Tip = Label( "Tip", "" ); m_Tip->SetWrap( true );
        ListenForGameEvent( "server_spawn" );
        ListenForGameEvent( "game_newmap" );
        MakePopup( false );
        SetKeyBoardInputEnabled( false ); SetMouseInputEnabled( false );
        SetVisible( false );
        m_GameUI->SetLoadingBackgroundDialog( GetVPanel() );
    }
    ~CHL2MPLoading()
    {
        m_GameUI->SetLoadingBackgroundDialog( 0 );
        if ( m_Texture >= 0 ) vgui::surface()->DestroyTextureID( m_Texture );
    }
    void LevelInit( const char *map )
    {
        if ( m_Active ) { MapName( map, m_Context.map, sizeof( m_Context.map ) ); UpdateCard(); }
        else s_PendingUntil = 0;
    }
    void FireGameEvent( IGameEvent *event ) OVERRIDE
    {
        if ( !m_Active ) return;
        MapName( event->GetString( "mapname" ), m_Context.map, sizeof( m_Context.map ) );
        const char *host = event->GetString( "hostname" );
        if ( host[0] ) Q_strncpy( m_Context.server, host, sizeof( m_Context.server ) );
        UpdateCard();
    }
    void ApplySchemeSettings( vgui::IScheme *scheme ) OVERRIDE
    {
        BaseClass::ApplySchemeSettings( scheme );
        m_FontWidth = 0;
        SetBgColor( Color( 10, 16, 19, 255 ) );
        m_Logo->SetFgColor( Color( 236, 237, 230, 255 ) );
        m_Map->SetFgColor( Color( 236, 237, 230, 255 ) );
        vgui::Label *amber[] = { m_Mode, m_TipHeading };
        for ( int i = 0; i < ARRAYSIZE( amber ); ++i ) amber[i]->SetFgColor( Color( 235, 164, 79, 255 ) );
        vgui::Label *muted[] = { m_Tag, m_Description, m_Server, m_Tip };
        for ( int i = 0; i < ARRAYSIZE( muted ); ++i ) muted[i]->SetFgColor( Color( 190, 200, 201, 255 ) );
    }
    void PerformLayout() OVERRIDE
    {
        BaseClass::PerformLayout();
        int w, h; vgui::surface()->GetScreenSize( w, h ); SetBounds( 0, 0, w, h );
        const float s = MIN( w / 1024.0f, h / 720.0f );
        const int pad = int( 44 * s ), deck = h - MAX( 190, int( 238 * s ) );
        const int leftW = int( w * 0.45f ) - pad * 2;
        if ( m_FontWidth != w || m_FontHeight != h )
        {
            m_FontWidth = w; m_FontHeight = h;
            const char *faces[] = { "HL2MP", "GorDIN", "GorDIN Bold" };
            const char *files[] = { "resource/hl2mp.ttf", "resource/gordin-regular.ttf", "resource/gordin-bold.ttf" };
            for ( int i = 0; i < ARRAYSIZE( faces ); ++i )
            {
                char path[MAX_PATH];
                if ( filesystem->RelativePathToFullPath( files[i], "GAME", path, sizeof( path ) ) )
                    vgui::surface()->AddCustomFontFile( faces[i], path );
            }
            m_Logo->SetFont( LoadingFont( "HL2MP", MAX( 24, int( 48 * s ) ) ) );
            m_Map->SetFont( LoadingFont( "GorDIN", MAX( 26, int( 48 * s ) ) ) );
            vgui::HFont body = LoadingFont( "GorDIN", MAX( 12, int( 18 * s ) ) );
            vgui::HFont small = LoadingFont( "GorDIN", MAX( 11, int( 14 * s ) ) );
            vgui::HFont heading = LoadingFont( "GorDIN Bold", MAX( 11, int( 14 * s ) ), 700 );
            m_Tag->SetFont( small ); m_Server->SetFont( small );
            m_Mode->SetFont( heading ); m_TipHeading->SetFont( heading );
            m_Description->SetFont( body ); m_Tip->SetFont( body );
        }
        m_Logo->SetBounds( pad, int( 24 * s ), w - pad * 2, int( 58 * s ) );
        m_Tag->SetBounds( pad, int( 83 * s ), w - pad * 2, int( 22 * s ) );
        m_Mode->SetBounds( pad, deck + int( 23 * s ), leftW, int( 24 * s ) );
        m_Map->SetBounds( pad - int( 2 * s ), deck + int( 49 * s ), leftW, int( 58 * s ) );
        m_Description->SetBounds( pad, deck + int( 111 * s ), leftW, int( 66 * s ) );
        m_Server->SetBounds( pad, h - int( 45 * s ), leftW, int( 24 * s ) );
        // The engine owns the progress/download/cancel controls in this right-hand area.
        const int right = int( w * 0.53f );
        m_TipHeading->SetBounds( right, deck + int( 23 * s ), w - right - pad, int( 24 * s ) );
        m_Tip->SetBounds( right, deck + int( 51 * s ), w - right - pad, int( 60 * s ) );
    }
    void PaintBackground() OVERRIDE
    {
        // During sign-on the engine knows the level before the client entities exist.
        if ( m_Active && engine->IsConnected() && !engine->IsLevelMainMenuBackground() )
        {
            char map[128]; MapName( engine->GetLevelName(), map, sizeof( map ) );
            // While disconnecting, GetLevelName can still refer to the old world.
            // A same-map reload is confirmed by LevelInit instead.
            if ( map[0] && Q_strcmp( map, m_PreviousMap ) && Q_strcmp( map, m_Context.map ) )
            {
                Q_strncpy( m_Context.map, map, sizeof( m_Context.map ) ); UpdateCard();
            }
        }
        int w = GetWide(), h = GetTall();
        const float s = MIN( w / 1024.0f, h / 720.0f );
        vgui::ISurface *surface = vgui::surface();
        surface->DrawSetColor( Color( 16, 25, 30, 255 ) ); surface->DrawFilledRect( 0, 0, w, h );
        if ( m_Texture >= 0 )
        {
            // Artwork is captured at 16:9. Crop to fill without stretching on 4:3/ultrawide.
            const float ratio = ( w / float( MAX( h, 1 ) ) ) / ( 16.0f / 9.0f );
            const float sx = MIN( ratio, 1.0f ), sy = MIN( 1.0f / ratio, 1.0f );
            surface->DrawSetTexture( m_Texture ); surface->DrawSetColor( Color( 230, 235, 235, 255 ) );
            surface->DrawTexturedSubRect( 0, 0, w, h, ( 1 - sx ) / 2, ( 1 - sy ) / 2, ( 1 + sx ) / 2, ( 1 + sy ) / 2 );
        }
        surface->DrawSetColor( Color( 5, 10, 14, 255 ) );
        if ( ErrorDialog() )
        {
            surface->DrawSetColor( Color( 0, 0, 0, 145 ) ); surface->DrawFilledRect( 0, 0, w, h );
            return;
        }
        surface->DrawFilledRectFade( 0, 0, w, int( 200 * s ), 200, 0, false );
        surface->DrawFilledRectFade( 0, 0, w, h, 90, 0, true );
        const int deck = h - MAX( 190, int( 238 * s ) );
        surface->DrawSetColor( Color( 7, 14, 18, 237 ) ); surface->DrawFilledRect( 0, deck, w, h );
        surface->DrawSetColor( Color( 235, 164, 79, 165 ) ); surface->DrawFilledRect( 0, deck, w, deck + MAX( 1, int( 2 * s ) ) );
        surface->DrawSetColor( Color( 180, 198, 201, 38 ) );
        surface->DrawFilledRect( int( w * 0.49f ), deck + int( 26 * s ), int( w * 0.49f ) + 1, h - int( 28 * s ) );
    }
    void OnThink() OVERRIDE
    {
        BaseClass::OnThink();
        int w, h; vgui::surface()->GetScreenSize( w, h );
        if ( w != GetWide() || h != GetTall() ) InvalidateLayout( true, true );
        const vgui::VPANEL error = ErrorDialog();
        for ( int i = 0; i < GetChildCount(); ++i ) GetChild( i )->SetVisible( !error );
        if ( error && error != m_ErrorDialog )
        {
            // Keep native error data/actions intact; only arrange the controls.
            // Generic errors size themselves to their wrapped message first.
            int width, height; vgui::ipanel()->GetSize( error, width, height );
            width = MIN( width, w - 40 ); height = MIN( h - 40, MAX( 236, height + 22 ) );
            vgui::ipanel()->SetSize( error, width, height );
            vgui::ipanel()->SetPos( error, ( w - width ) / 2, ( h - height ) / 2 );
            vgui::VPANEL info = ChildNamed( error, "InfoLabel" ), button = ChildNamed( error, "CancelButton" );
            vgui::ipanel()->SetPos( info, 28, 78 ); vgui::ipanel()->SetSize( info, width - 56, height - 142 );
            if ( button )
            {
                KeyValuesAD label( "GetText" );
                vgui::ipanel()->RequestInfo( button, label );
                const wchar_t *login = g_pVGuiLocalize->Find( "#GameUI_RefreshLogin_Login" );
                const bool signIn = login && !wcscmp( label->GetWString( "text", L"" ), login );
                SetPanelText( button, signIn ? "#HL2MP_SignIn" : "#HL2MP_OK" );
                vgui::ipanel()->SetPos( button, width - 168, height - 64 );
                vgui::ipanel()->SetSize( button, 140, 36 );
            }
            // Older servers sent identifiers instead of readable reasons.
            // Never use this short native text query to replace arbitrary long
            // errors: preserve their complete engine-owned text and support URLs.
            KeyValuesAD label( "GetText" );
            vgui::ipanel()->RequestInfo( info, label );
            char text[1024]; V_UnicodeToUTF8( label->GetWString( "text", L"" ), text, sizeof( text ) );
            if ( Q_stristr( text, "Matchmaking_" ) ) SetPanelText( info, HL2MPMatchReasonToken( text ) );
            else if ( Q_stristr( text, "Removed_by_server_admin" ) ) SetPanelText( info, "#HL2MP_AdminRemoved" );
        }
        m_ErrorDialog = error;
    }
private:
    vgui::Label *Label( const char *name, const char *text )
    {
        vgui::Label *label = new vgui::Label( this, name, text );
        label->SetContentAlignment( vgui::Label::a_northwest );
        label->SetMouseInputEnabled( false ); label->SetKeyBoardInputEnabled( false ); return label;
    }
    MESSAGE_FUNC( OnActivate, "activate" )
    {
        m_Active = true;
        MapName( engine->GetLevelName(), m_PreviousMap, sizeof( m_PreviousMap ) );
        memset( &m_Context, 0, sizeof( m_Context ) );
        if ( Plat_FloatTime() < s_PendingUntil ) m_Context = s_Pending;
        memset( &s_Pending, 0, sizeof( s_Pending ) ); s_PendingUntil = 0;
        m_Tip->SetText( s_Tips[m_TipIndex++ % ARRAYSIZE( s_Tips )] );
        InvalidateLayout( true, true ); UpdateCard();
        DevMsg( "Loading screen: activated, map=%s mode=%s\n", m_Context.map, m_Context.mode );
    }
    MESSAGE_FUNC( OnDeactivate, "deactivate" )
    {
        m_Active = false;
        memset( &m_Context, 0, sizeof( m_Context ) );
        DevMsg( "Loading screen: finished\n" );
    }
    void UpdateCard()
    {
        int index = -1;
        for ( int i = 0; i < ARRAYSIZE( s_Maps ); ++i ) if ( !Q_stricmp( s_Maps[i].map, m_Context.map ) ) { index = i; break; }
        if ( index != m_MapIndex || m_Texture < 0 )
        {
            if ( m_Texture >= 0 ) { vgui::surface()->DestroyTextureID( m_Texture ); m_Texture = -1; }
            m_MapIndex = index;
            if ( index >= 0 )
            {
                char material[MAX_PATH], path[MAX_PATH];
                Q_snprintf( material, sizeof( material ), "vgui/matchmaking/%s", s_Maps[index].map );
                Q_snprintf( path, sizeof( path ), "materials/%s.vmt", material );
                if ( filesystem->FileExists( path, "GAME" ) )
                {
                    m_Texture = vgui::surface()->CreateNewTextureID();
                    vgui::surface()->DrawSetTextureFile( m_Texture, material, true, false );
                }
            }
            if ( m_Texture < 0 )
            {
                m_Texture = vgui::surface()->CreateNewTextureID();
                vgui::surface()->DrawSetTextureFile( m_Texture, "vgui/matchmaking/dm_lockdown", true, false );
            }
        }
        char title[128]; Q_strncpy( title, m_Context.map, sizeof( title ) ); Q_strupr( title );
        m_Map->SetText( index >= 0 ? s_Maps[index].title : title[0] ? title : "#HL2MP_UI_a0e1a519f04a" );
        m_Description->SetText( index >= 0 ? s_Maps[index].description : title[0] ? "#HL2MP_UI_fb10f50b23d6" : "#HL2MP_UI_c68c7f69f3eb" );
        const char *mode = "#HL2MP_UI_127aad574b83";
        if ( !Q_strcmp( m_Context.mode, "menu" ) ) mode = "#HL2MP_UI_cb2a495822a6";
        else if ( !Q_strcmp( m_Context.mode, "duel" ) ) mode = "#HL2MP_UI_e7def291093c";
        else if ( !Q_strcmp( m_Context.mode, "team" ) ) mode = "#HL2MP_UI_b938e9d84260";
        else if ( !Q_strcmp( m_Context.mode, "team3" ) ) mode = "#HL2MP_UI_9fe23f8da144";
        else if ( !Q_strcmp( m_Context.mode, "team4" ) ) mode = "#HL2MP_UI_937a9a5ec4b7";
        else if ( !Q_strcmp( m_Context.mode, "team5" ) ) mode = "#HL2MP_UI_9f447deb2a04";
        else if ( !Q_strcmp( m_Context.mode, "casual" ) ) mode = "#HL2MP_UI_ff2fb9e38104";
        else if ( !Q_strcmp( m_Context.mode, "custom" ) ) mode = "#HL2MP_UI_20b3f3627b47";
        else if ( !Q_strcmp( m_Context.mode, "community" ) ) mode = "#HL2MP_UI_9cfb5935b8af";
        else if ( !Q_strcmp( m_Context.mode, "listen" ) ) mode = "#HL2MP_UI_7c4960bb51c5";
        else if ( !Q_strcmp( m_Context.mode, "listen_team" ) ) mode = "#HL2MP_UI_9e907940e783";
        m_Mode->SetText( mode );
        wchar_t server[256]; V_UTF8ToUnicode( m_Context.server, server, sizeof( server ) ); m_Server->SetText( server );
    }
    IGameUI *m_GameUI;
    LoadingContext m_Context;
    char m_PreviousMap[128];
    int m_Texture, m_MapIndex, m_FontWidth, m_FontHeight, m_TipIndex;
    bool m_Active;
    vgui::VPANEL m_ErrorDialog;
    vgui::Label *m_Logo, *m_Tag, *m_Mode, *m_Map, *m_Description, *m_Server, *m_TipHeading, *m_Tip;
};
CHL2MPLoading *s_Loading = NULL;
}

void HL2MPLoading_Create( IGameUI *gameUI ) { if ( !s_Loading ) s_Loading = new CHL2MPLoading( gameUI ); }
void HL2MPLoading_Destroy() { delete s_Loading; s_Loading = NULL; s_PendingUntil = 0; }
bool HL2MPLoading_IsVisible()
{
    if ( s_Loading && s_Loading->IsVisible() ) return true;
    // On an error GameUI may hide the background before the user dismisses its
    // native dialog. Starting map_background would destroy that error dialog.
    for ( int i = 0; i < vgui::surface()->GetPopupCount(); ++i )
    {
        vgui::VPANEL popup = vgui::surface()->GetPopup( i );
        if ( vgui::ipanel()->IsVisible( popup ) && !Q_stricmp( vgui::ipanel()->GetName( popup ), "LoadingDialog" ) ) return true;
    }
    return false;
}
void HL2MPLoading_SetContext( const char *map, const char *mode, const char *server )
{
    MapName( map, s_Pending.map, sizeof( s_Pending.map ) );
    Q_strncpy( s_Pending.mode, mode, sizeof( s_Pending.mode ) );
    Q_strncpy( s_Pending.server, server, sizeof( s_Pending.server ) );
    s_PendingUntil = Plat_FloatTime() + 15;
}
void HL2MPLoading_LevelInit( const char *map ) { if ( s_Loading ) s_Loading->LevelInit( map ); }
