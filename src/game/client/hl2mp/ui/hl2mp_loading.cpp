#include "cbase.h"
#include "hl2mp_loading.h"
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
    { "dm_lockdown", "LOCKDOWN", "Cell blocks, narrow corridors and a contested central yard." },
    { "dm_overwatch", "OVERWATCH", "City streets below. Rooftop sightlines above. Watch your angles." },
    { "dm_powerhouse", "POWERHOUSE", "Industrial halls and exposed routes through a derelict power station." },
    { "dm_resistance", "RESISTANCE", "A ruined city battleground. Keep moving between cover." },
    { "dm_runoff", "RUNOFF", "An industrial complex with open ground and close-quarters routes." },
    { "dm_steamlab", "STEAM LAB", "Tight industrial passages connect a network of laboratories." },
    { "dm_underpass", "UNDERPASS", "Urban cover, concrete passages and fights across different elevations." }
};
const char *s_Tips[] = {
    "Use the gravity gun to turn loose physics objects into weapons.",
    "Listen for footsteps and weapon fire to track nearby opponents.",
    "Learn the routes between health, suit chargers and weapon pickups.",
    "Switch weapons when reloading would leave you exposed.",
    "In team matches, move together and cover different angles."
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
        m_Texture( -1 ), m_MapIndex( -1 ), m_FontWidth( 0 ), m_FontHeight( 0 ), m_TipIndex( 0 ), m_Active( false )
    {
        memset( &m_Context, 0, sizeof( m_Context ) );
        SetParent( enginevgui->GetPanel( PANEL_GAMEUIDLL ) );
        SetProportional( false ); SetKeyBoardInputEnabled( false ); SetMouseInputEnabled( false );
        m_Logo = Label( "Logo", "deathmatch" );
        m_Tag = Label( "Tag", "HALF-LIFE 2  /  MULTIPLAYER" );
        m_Mode = Label( "Mode", "" );
        m_Map = Label( "Map", "CONNECTING" );
        m_Description = Label( "Description", "" ); m_Description->SetWrap( true );
        m_Server = Label( "Server", "" );
        m_TipHeading = Label( "TipHeading", "FIELD NOTES" );
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
        m_Map->SetText( index >= 0 ? s_Maps[index].title : title[0] ? title : "CONNECTING" );
        m_Description->SetText( index >= 0 ? s_Maps[index].description : title[0] ? "Community map. Match rules are set by the server." : "Preparing your connection to the server." );
        const char *mode = "DEATHMATCH";
        if ( !Q_strcmp( m_Context.mode, "menu" ) ) mode = "MAIN MENU";
        else if ( !Q_strcmp( m_Context.mode, "duel" ) ) mode = "COMPETITIVE  /  1v1";
        else if ( !Q_strcmp( m_Context.mode, "team" ) ) mode = "COMPETITIVE  /  2v2";
        else if ( !Q_strcmp( m_Context.mode, "team3" ) ) mode = "COMPETITIVE  /  3v3";
        else if ( !Q_strcmp( m_Context.mode, "team4" ) ) mode = "COMPETITIVE  /  4v4";
        else if ( !Q_strcmp( m_Context.mode, "team5" ) ) mode = "COMPETITIVE  /  5v5";
        else if ( !Q_strcmp( m_Context.mode, "casual" ) ) mode = "CASUAL";
        else if ( !Q_strcmp( m_Context.mode, "custom" ) ) mode = "PRIVATE MATCH";
        else if ( !Q_strcmp( m_Context.mode, "community" ) ) mode = "COMMUNITY SERVER";
        else if ( !Q_strcmp( m_Context.mode, "listen" ) ) mode = "LISTEN SERVER  /  DEATHMATCH";
        else if ( !Q_strcmp( m_Context.mode, "listen_team" ) ) mode = "LISTEN SERVER  /  TEAM DEATHMATCH";
        m_Mode->SetText( mode );
        wchar_t server[256]; V_UTF8ToUnicode( m_Context.server, server, sizeof( server ) ); m_Server->SetText( server );
    }
    IGameUI *m_GameUI;
    LoadingContext m_Context;
    char m_PreviousMap[128];
    int m_Texture, m_MapIndex, m_FontWidth, m_FontHeight, m_TipIndex;
    bool m_Active;
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
