#include "cbase.h"
#include "cdll_client_int.h"
#include "hl2mp_mainmenu.h"
#include "hl2mp_loading.h"
#include "hl2mp_party.h"
#include "hl2mp_coordinator.h"
#include "hl2mp_menucontrols.h"
#include "hl2mp_options.h"
#include "hl2mp_serverbrowser.h"
#include "GameUI/IGameUI.h"
#include "ienginevgui.h"
#include "inetchannelinfo.h"
#include "filesystem.h"
#include "vgui/IVGui.h"
#include "vgui/ISurface.h"
#include "vgui/IScheme.h"
#include "vgui_controls/EditablePanel.h"
#include "vgui_controls/Button.h"
#include "vgui_controls/Label.h"
#include "vgui_controls/PanelListPanel.h"
#include "vgui_avatarimage.h"
#include "tier1/interface.h"
#include "tier0/memdbgon.h"

static CDllDemandLoader s_GameUI( "GameUI" );
static ConVar mm_menu( "mm_menu", "1", FCVAR_ARCHIVE, "Use the matchmaking main menu (restart required)." );
static ConVar mm_menu_motion( "mm_menu_motion", "1", FCVAR_ARCHIVE, "Animate the matchmaking menu." );
static ConVar mm_menu_background( "mm_menu_background", "1", FCVAR_ARCHIVE, "Render dm_lockdown behind the main menu." );
static const Color s_Amber( 235, 164, 79, 255 );

static void MenuFontFile( const char *face, const char *file )
{
	char path[MAX_PATH];
	if ( filesystem->RelativePathToFullPath( file, "GAME", path, sizeof( path ) ) )
		vgui::surface()->AddCustomFontFile( face, path );
}

static void MenuFont( vgui::HFont font, const char *face, int height, int weight = 400 )
{
	if ( !vgui::surface()->SetFontGlyphSet( font, face, height, weight, 0, 0, vgui::ISurface::FONTFLAG_ANTIALIAS ) )
		vgui::surface()->SetFontGlyphSet( font, "Tahoma", height, weight, 0, 0, vgui::ISurface::FONTFLAG_ANTIALIAS );
}

class CMatchmakingButton : public vgui::Button
{
	DECLARE_CLASS_SIMPLE( CMatchmakingButton, vgui::Button );
public:
	enum Style { NAVIGATION, ACTION, PRIMARY, MODE };
	CMatchmakingButton( vgui::Panel *parent, const char *name, const char *text, Style style )
		: BaseClass( parent, name, text, parent, name ), m_Style( style ), m_Font( 0 ), m_Detail( NULL ),
		m_Hover( 0 ), m_LastThink( Plat_FloatTime() ), m_Scale( 1 ), m_Selected( false ), m_Compact( false ), m_Texture( -1 ), m_ModeIndex( 0 )
	{
		SetContentAlignment( vgui::Label::a_west );
		SetButtonBorderEnabled( false );
		DrawFocusBox( false );
	}
	void SetDetail( const char *text, int index, int texture )
	{
		m_ModeIndex = index; m_Texture = texture;
		m_Detail = new vgui::Label( this, "Detail", text );
		m_Detail->SetMouseInputEnabled( false ); m_Detail->SetKeyBoardInputEnabled( false );
		m_Detail->SetContentAlignment( vgui::Label::a_west );
	}
	void SetModeSelected( bool selected ) { if ( selected != m_Selected ) { m_Selected = selected; Repaint(); } }
	void SetMenuScale( float scale, vgui::HFont font, vgui::HFont detailFont, bool compact = false )
	{
		m_Scale = scale; m_Compact = compact; m_Font = font;
		SetFont( font );
		const int inset = int( 14 * scale );
		SetContentAlignment( m_Style == MODE ? vgui::Label::a_northwest : vgui::Label::a_west );
		SetTextInset( inset, m_Style == MODE ? int( ( compact ? 9 : 113 ) * scale ) : 0 );
		if ( m_Detail )
		{
			m_Detail->SetFont( detailFont );
			m_Detail->SetBounds( inset, int( ( compact ? 34 : 145 ) * scale ), GetWide() - inset * 2, int( 26 * scale ) );
			m_Detail->SetFgColor( Color( 195, 201, 203, 255 ) );
		}
	}
	void ApplySchemeSettings( vgui::IScheme *scheme ) OVERRIDE
	{
		BaseClass::ApplySchemeSettings( scheme );
		SetArmedSound( "ui/buttonrollover.wav" );
		SetDepressedSound( "ui/buttonclick.wav" );
		SetReleasedSound( NULL );
		if ( m_Font ) SetFont( m_Font );
		const Color normal = m_Style == PRIMARY ? Color( 24, 26, 27, 255 ) : Color( 204, 209, 209, 255 );
		SetDefaultColor( normal, Color( 0, 0, 0, 0 ) );
		SetArmedColor( m_Style == PRIMARY ? normal : Color( 255, 251, 240, 255 ), Color( 0, 0, 0, 0 ) );
		SetDepressedColor( normal, Color( 0, 0, 0, 0 ) );
		SetDisabledFgColor1( Color( 111, 119, 121, 255 ) );
		SetDisabledFgColor2( Color( 0, 0, 0, 0 ) );
		SetButtonBorderEnabled( false ); DrawFocusBox( false );
	}
	void OnThink() OVERRIDE
	{
		BaseClass::OnThink();
		const double now = Plat_FloatTime();
		const float target = IsEnabled() && ( IsArmed() || HasFocus() ) ? 1.0f : 0.0f;
		m_Hover = mm_menu_motion.GetBool() ? Approach( target, m_Hover, float( MAX( 0.0, now - m_LastThink ) ) / 0.15f ) : target;
		m_LastThink = now; Repaint();
	}
	void PaintBackground() OVERRIDE
	{
		vgui::ISurface *surface = vgui::surface();
		const int w = GetWide(), h = GetTall();
		if ( m_Style == MODE )
		{
			if ( !m_Compact && m_Texture >= 0 )
			{
				surface->DrawSetColor( Color( 180, 190, 195, 255 ) ); surface->DrawSetTexture( m_Texture );
				const float left = 0.30f + m_ModeIndex * 0.19f, top = 0.40f - m_ModeIndex * 0.12f;
				const float spanY = 0.30f * ( 16.0f / 9.0f ) * h / MAX( w, 1 );
				surface->DrawTexturedSubRect( 0, 0, w, h, left, top, left + 0.30f, top + spanY );
			}
			surface->DrawSetColor( Color( 8, 13, 17, 255 ) );
			surface->DrawFilledRectFade( 0, 0, w, h, m_Compact ? 180 : 40, 245, false );
			surface->DrawSetColor( m_Selected ? s_Amber : Color( 220, 225, 225, int( 35 + 100 * m_Hover ) ) );
			surface->DrawOutlinedRect( 0, 0, w, h );
			if ( m_Selected ) surface->DrawFilledRect( 0, h - MAX( 2, int( 3 * m_Scale ) ), w, h );
		}
		else if ( m_Style == PRIMARY )
		{
			surface->DrawSetColor( IsEnabled() ? Color( 220 + int( 15 * m_Hover ), 151 + int( 20 * m_Hover ), 71 + int( 20 * m_Hover ), 255 ) : Color( 56, 62, 63, 210 ) );
			surface->DrawFilledRect( 0, 0, w, h );
		}
		else
		{
			surface->DrawSetColor( Color( 230, 235, 235, 255 ) );
			surface->DrawFilledRectFade( 0, 0, w, h, int( ( m_Style == ACTION ? 12 : 0 ) + 34 * m_Hover ), 0, true );
			if ( m_Style == ACTION )
			{
				surface->DrawSetColor( Color( 220, 225, 225, int( 45 + 70 * m_Hover ) ) );
				surface->DrawOutlinedRect( 0, 0, w, h );
			}
		}
		if ( m_Hover > 0 )
		{
			surface->DrawSetColor( Color( 235, 164, 79, int( 255 * m_Hover ) ) );
			surface->DrawFilledRect( 0, 0, MAX( 2, int( 3 * m_Scale ) ), h );
		}
	}
	void PaintBorder() OVERRIDE {}
private:
	Style m_Style;
	vgui::HFont m_Font;
	vgui::Label *m_Detail;
	float m_Hover;
	double m_LastThink;
	float m_Scale;
	bool m_Selected, m_Compact;
	int m_Texture, m_ModeIndex;
};


class CMatchmakingFriendRow : public vgui::EditablePanel
{
    DECLARE_CLASS_SIMPLE( CMatchmakingFriendRow, vgui::EditablePanel );
public:
    CMatchmakingFriendRow( vgui::Panel *parent, CSteamID id ) : BaseClass( parent, "Friend" ), m_ID( id ), m_Scale( 1 )
    {
        SetPaintBackgroundEnabled( false );
        m_Avatar = new CAvatarImagePanel( this, "Avatar" );
        m_Avatar->SetPlayer( id, k_EAvatarSize32x32 ); m_Avatar->SetShouldDrawFriendIcon( false );
        m_Avatar->SetShouldScaleImage( true ); m_Avatar->SetMouseInputEnabled( false );
        m_Name = new vgui::Label( this, "Name", "" ); m_Presence = new vgui::Label( this, "Presence", "" );
        m_Name->SetMouseInputEnabled( false ); m_Presence->SetMouseInputEnabled( false );
        m_Invite = new CMatchmakingButton( this, "invite", "+", CMatchmakingButton::ACTION );
    }
    CSteamID ID() const { return m_ID; }
    void Update( float scale, vgui::HFont body, vgui::HFont detail )
    {
        m_Scale = scale; SetTall( int( 54 * scale ) );
        m_Name->SetFont( body ); m_Presence->SetFont( detail );
        ISteamFriends *friends = steamapicontext->SteamFriends();
        const bool online = friends->GetFriendPersonaState( m_ID ) != k_EPersonaStateOffline;
        FriendGameInfo_t game; const bool playing = friends->GetFriendGamePlayed( m_ID, &game );
        bool inParty = false;
        for ( int i = 0; i < HL2MPParty().MemberCount(); ++i ) if ( HL2MPParty().Member( i ) == m_ID ) inParty = true;
        m_Name->SetText( friends->GetFriendPersonaName( m_ID ) );
        m_Name->SetFgColor( Color( 222, 225, 223, 255 ) );
        m_Presence->SetText( inParty ? "In your party" : playing && game.m_gameID.AppID() == steamapicontext->SteamUtils()->GetAppID() ? "Playing Half-Life 2: Deathmatch" : playing ? "In game" : online ? "Online" : "Offline" );
        m_Presence->SetFgColor( online ? Color( 153, 194, 163, 255 ) : Color( 143, 150, 153, 255 ) );
        m_Invite->SetEnabled( online && !inParty && !HL2MPParty().Pending() && !HL2MPCoordinator().Busy() && HL2MPParty().MemberCount() < HL2MP_MAX_PARTY_MEMBERS );
        m_Invite->SetMenuScale( scale, body, detail ); InvalidateLayout();
    }
    void PerformLayout() OVERRIDE
    {
        BaseClass::PerformLayout(); const int a = int( 32 * m_Scale ), inset = int( 40 * m_Scale ), button = int( 30 * m_Scale );
        m_Avatar->SetBounds( 0, int( 8 * m_Scale ), a, a );
        m_Name->SetBounds( inset, 0, MAX( 20, GetWide() - inset - button - 4 ), int( 26 * m_Scale ) );
        m_Presence->SetBounds( inset, int( 24 * m_Scale ), MAX( 20, GetWide() - inset - button - 4 ), int( 23 * m_Scale ) );
        m_Invite->SetBounds( GetWide() - button, int( 8 * m_Scale ), button, button );
    }
    void OnCommand( const char *command ) OVERRIDE
    {
        if ( !Q_strcmp( command, "invite" ) ) HL2MPParty().InviteFriend( m_ID ); else BaseClass::OnCommand( command );
    }
private:
    CSteamID m_ID; float m_Scale;
    CAvatarImagePanel *m_Avatar; vgui::Label *m_Name, *m_Presence; CMatchmakingButton *m_Invite;
};

static const char *s_Modes[] = { "duel", "team", "team3", "team4", "team5", "casual", "custom" };
static const char *s_Maps[] = { "dm_lockdown", "dm_overwatch", "dm_powerhouse", "dm_resistance", "dm_runoff", "dm_steamlab", "dm_underpass" };
static const int s_Times[] = { 10, 20, 30, 60 }, s_Frags[] = { 0, 25, 50, 100 };

class CHL2MPMainMenu : public vgui::EditablePanel
{
    DECLARE_CLASS_SIMPLE( CHL2MPMainMenu, vgui::EditablePanel );
public:
    CHL2MPMainMenu( IGameUI *gameUI );
    ~CHL2MPMainMenu();
    void PerformLayout() OVERRIDE;
    void ApplySchemeSettings( vgui::IScheme *scheme ) OVERRIDE;
    void PaintBackground() OVERRIDE;
    void OnThink() OVERRIDE;
    void OnTick() OVERRIDE;
    void OnCommand( const char *command ) OVERRIDE;
    void OnKeyCodeTyped( vgui::KeyCode code ) OVERRIDE;
private:
    void SetPlayPage( bool play );
    void UpdateFriends();
    enum { PLAY, RESUME, SERVERS, OPTIONS, DISCONNECT, QUIT, BACK, DUEL, TEAM, TEAM3, TEAM4, TEAM5, CASUAL, CUSTOM,
        FIND, CREATE_PARTY, INVITE, LEAVE, READY, ACCEPT, CANCEL, BROWSE, BUTTON_COUNT };
    IGameUI *m_GameUI;
    CMatchmakingButton *m_Buttons[BUTTON_COUNT];
    CHL2MPDropdown *m_CustomControls[4];
    vgui::Label *m_CustomCaptions[4];
    vgui::Panel *m_Options, *m_Browser;
    vgui::Label *m_Title, *m_Subtitle, *m_Tag, *m_PlayHeading, *m_ModeDescription;
    vgui::Label *m_PartyHeading, *m_Status, *m_QueueStatus, *m_ResultHeading, *m_ResultDetails;
    vgui::Label *m_FriendsHeading, *m_FriendsHint, *m_NewsHeading, *m_NewsTitle, *m_NewsBody;
    vgui::PanelListPanel *m_PartyList, *m_FriendsList;
    CUtlVector<vgui::Label *> m_Members;
    CUtlVector<CMatchmakingFriendRow *> m_Friends;
    vgui::HFont m_LogoFont, m_BrandFont, m_NavFont, m_CardFont, m_BodyFont, m_SectionFont, m_DetailFont;
    int m_ScreenWidth, m_ScreenHeight, m_Background, m_SelectedMode, m_MapChoice, m_SizeChoice, m_TimeChoice, m_FragChoice;
    int m_FontWidth, m_FontHeight;
    int m_PartyX, m_PartyY, m_PartyW, m_PartyH, m_FriendsY, m_NewsX, m_NewsY, m_NewsW;
    bool m_PlayPage, m_WasBusy, m_WasReady, m_GroupedLayout, m_InGameLayout, m_BackgroundRequested;
    float m_Scale;
    double m_NextFriends, m_IdleSince, m_FontRefreshAt;
};

static CHL2MPMainMenu *s_Menu = NULL;

CHL2MPMainMenu::CHL2MPMainMenu( IGameUI *gameUI ) : BaseClass( NULL, "HL2MPMainMenu" ),
    m_GameUI( gameUI ), m_ScreenWidth( 0 ), m_ScreenHeight( 0 ), m_Background( -1 ), m_SelectedMode( 0 ),
    m_MapChoice( 0 ), m_SizeChoice( 0 ), m_TimeChoice( 1 ), m_FragChoice( 0 ),
    m_FontWidth( 0 ), m_FontHeight( 0 ),
    m_PartyX( 0 ), m_PartyY( 0 ), m_PartyW( 0 ), m_PartyH( 0 ), m_FriendsY( 0 ), m_NewsX( 0 ), m_NewsY( 0 ), m_NewsW( 0 ),
    m_PlayPage( false ), m_WasBusy( false ), m_WasReady( false ), m_GroupedLayout( false ), m_InGameLayout( false ),
    m_BackgroundRequested( false ), m_Scale( 1 ), m_NextFriends( 0 ), m_IdleSince( Plat_FloatTime() ), m_FontRefreshAt( 0 )
{
    SetProportional( false ); SetMouseInputEnabled( true ); SetKeyBoardInputEnabled( true ); SetCursor( vgui::dc_arrow );
    // Reuse locally installed retail maps without replacing the SDK engine.
    char retailPath[MAX_PATH];
    if ( steamapicontext && steamapicontext->SteamApps() && steamapicontext->SteamApps()->GetAppInstallDir( 320, retailPath, sizeof( retailPath ) ) )
    {
        Q_AppendSlash( retailPath, sizeof( retailPath ) ); Q_strncat( retailPath, "hl2mp", sizeof( retailPath ) );
        filesystem->AddSearchPath( retailPath, "GAME", PATH_ADD_TO_TAIL );
    }
    MenuFontFile( "HL2MP", "resource/hl2mp.ttf" ); MenuFontFile( "GorDIN", "resource/gordin-regular.ttf" ); MenuFontFile( "GorDIN Bold", "resource/gordin-bold.ttf" );
    m_LogoFont = vgui::surface()->CreateFont(); m_BrandFont = vgui::surface()->CreateFont(); m_NavFont = vgui::surface()->CreateFont();
    m_CardFont = vgui::surface()->CreateFont(); m_BodyFont = vgui::surface()->CreateFont(); m_SectionFont = vgui::surface()->CreateFont(); m_DetailFont = vgui::surface()->CreateFont();
    m_Background = vgui::surface()->CreateNewTextureID(); vgui::surface()->DrawSetTextureFile( m_Background, "vgui/matchmaking/dm_lockdown", true, false );
    m_Title = new vgui::Label( this, "Title", "deathmatch" ); m_Subtitle = new vgui::Label( this, "Subtitle", "HALF+LIFE'" );
    m_Tag = new vgui::Label( this, "Tag", "M A T C H M A K I N G" ); m_PlayHeading = new vgui::Label( this, "PlayHeading", "PLAY" );
    m_ModeDescription = new vgui::Label( this, "ModeDescription", "" ); m_PartyHeading = new vgui::Label( this, "PartyHeading", "YOUR PARTY" );
    m_Status = new vgui::Label( this, "PartyStatus", "" ); m_QueueStatus = new vgui::Label( this, "QueueStatus", "" );
    m_ResultHeading = new vgui::Label( this, "ResultHeading", "" ); m_ResultDetails = new vgui::Label( this, "ResultDetails", "" );
    m_FriendsHeading = new vgui::Label( this, "FriendsHeading", "FRIENDS" ); m_FriendsHint = new vgui::Label( this, "FriendsHint", "" );
    m_NewsHeading = new vgui::Label( this, "NewsHeading", "LATEST UPDATES" );
    m_NewsTitle = new vgui::Label( this, "NewsTitle", "" ); m_NewsBody = new vgui::Label( this, "NewsBody", "" );
    KeyValues *news = new KeyValues( "MenuNews" );
    if ( news->LoadFromFile( filesystem, "resource/matchmaking_news.res", "GAME" ) )
    {
        m_NewsTitle->SetText( news->GetString( "title", "" ) ); m_NewsBody->SetText( news->GetString( "body", "" ) );
    }
    news->deleteThis();
    vgui::Label *labels[] = { m_Title, m_Subtitle, m_Tag, m_PlayHeading, m_ModeDescription, m_PartyHeading, m_Status,
        m_QueueStatus, m_ResultHeading, m_ResultDetails, m_FriendsHeading, m_FriendsHint, m_NewsHeading, m_NewsTitle, m_NewsBody };
    for ( int i = 0; i < ARRAYSIZE( labels ); ++i )
    {
        labels[i]->SetMouseInputEnabled( false ); labels[i]->SetKeyBoardInputEnabled( false ); labels[i]->SetContentAlignment( vgui::Label::a_west );
    }
    m_ModeDescription->SetWrap( true ); m_ResultDetails->SetWrap( true ); m_Status->SetWrap( true ); m_QueueStatus->SetWrap( true );
    m_FriendsHint->SetWrap( true ); m_NewsBody->SetWrap( false );
    m_PartyList = new vgui::PanelListPanel( this, "PartyList" ); m_FriendsList = new vgui::PanelListPanel( this, "FriendsList" );
    HL2MPStyleScrollBar( m_PartyList->GetScrollbar() ); HL2MPStyleScrollBar( m_FriendsList->GetScrollbar() );
    m_PartyList->SetFirstColumnWidth( 0 ); m_FriendsList->SetFirstColumnWidth( 0 );
    KeyValues *listSettings = new KeyValues( "ListSettings" ); listSettings->SetInt( "autohide_scrollbar", 1 );
    m_PartyList->ApplySettings( listSettings ); m_FriendsList->ApplySettings( listSettings ); listSettings->deleteThis();
    m_PartyList->SetVerticalBufferPixels( 0 ); m_FriendsList->SetVerticalBufferPixels( 2 );
    const char *texts[BUTTON_COUNT] = { "PLAY", "RESUME GAME", "SERVER BROWSER", "OPTIONS", "DISCONNECT", "QUIT", "<  BACK",
        "1v1 DUEL", "2v2", "3v3", "4v4", "5v5", "CASUAL", "CUSTOM", "FIND MATCH", "CREATE PARTY", "INVITE", "LEAVE", "READY TO PLAY", "ACCEPT MATCH", "CANCEL", "BROWSE SERVERS" };
    const char *commands[BUTTON_COUNT] = { "play", "ResumeGame", "OpenServerBrowser", "OpenOptionsDialog", "disconnect", "Quit", "back",
        "mode_duel", "mode_team", "mode_team3", "mode_team4", "mode_team5", "mode_casual", "mode_custom", "find_match", "party_create", "party_invite", "party_leave", "party_ready", "match_accept", "queue_cancel", "OpenServerBrowser" };
    for ( int i = 0; i < BUTTON_COUNT; ++i )
    {
        const CMatchmakingButton::Style style = i <= BACK ? CMatchmakingButton::NAVIGATION : i <= CUSTOM ? CMatchmakingButton::MODE : ( i == FIND || i == ACCEPT ) ? CMatchmakingButton::PRIMARY : CMatchmakingButton::ACTION;
        m_Buttons[i] = new CMatchmakingButton( this, commands[i], texts[i], style ); m_Buttons[i]->SetTabPosition( i + 1 );
    }
    for ( int i = DUEL; i <= CUSTOM; ++i ) m_Buttons[i]->SetDetail( i <= TEAM5 ? "RANKED" : i == CASUAL ? "UNRANKED" : "BROWSE / PRIVATE", 0, -1 );
    const char *captions[] = { "MAP", "TEAMS", "TIME", "FRAGS" };
    for ( int field = 0; field < 4; ++field )
    {
        m_CustomCaptions[field] = new vgui::Label( this, "CustomCaption", captions[field] );
        m_CustomCaptions[field]->SetMouseInputEnabled( false ); m_CustomCaptions[field]->SetKeyBoardInputEnabled( false );
        m_CustomControls[field] = new CHL2MPDropdown( this, captions[field] );
        const int count = field == 0 ? ARRAYSIZE( s_Maps ) : field == 1 ? 6 : field == 2 ? ARRAYSIZE( s_Times ) : ARRAYSIZE( s_Frags );
        for ( int i = 0; i < count; ++i )
        {
            const char *text = field == 0 ? s_Maps[i] : field == 1 ? ( i ? VarArgs( "%dv%d", i, i ) : "FREE-FOR-ALL" ) : field == 2 ? VarArgs( "%d MINUTES", s_Times[i] ) : s_Frags[i] ? VarArgs( "%d FRAGS", s_Frags[i] ) : "NO LIMIT";
            KeyValues *data = new KeyValues( "Choice", "choice", i ); m_CustomControls[field]->AddItem( text, data ); data->deleteThis();
        }
        m_CustomControls[field]->SilentActivateItemByRow( field == 2 ? 1 : 0 );
    }
    m_Options = HL2MPOptions_Create( this );
    m_Browser = HL2MPServerBrowser_Create( this );
    HL2MPParty().Init(); HL2MPCoordinator().Init(); m_GameUI->SetMainMenuOverride( GetVPanel() );
    MakePopup( false ); GetFocusNavGroup().SetFocusTopLevel( true );
    OnTick(); m_Buttons[PLAY]->RequestFocus(); vgui::ivgui()->AddTickSignal( GetVPanel(), 100 );
}

CHL2MPMainMenu::~CHL2MPMainMenu()
{
    vgui::ivgui()->RemoveTickSignal( GetVPanel() ); m_GameUI->SetMainMenuOverride( 0 );
    HL2MPParty().Shutdown(); HL2MPCoordinator().Shutdown();
    if ( m_Background >= 0 ) vgui::surface()->DestroyTextureID( m_Background );
}

void CHL2MPMainMenu::ApplySchemeSettings( vgui::IScheme *scheme )
{
    BaseClass::ApplySchemeSettings( scheme );
    vgui::Label *white[] = { m_Title, m_Subtitle, m_PlayHeading, m_ModeDescription, m_QueueStatus, m_NewsTitle };
    for ( int i = 0; i < ARRAYSIZE( white ); ++i ) white[i]->SetFgColor( Color( 232, 234, 229, 255 ) );
    vgui::Label *amber[] = { m_Tag, m_PartyHeading, m_FriendsHeading, m_NewsHeading, m_ResultHeading };
    for ( int i = 0; i < ARRAYSIZE( amber ); ++i ) amber[i]->SetFgColor( s_Amber );
    vgui::Label *muted[] = { m_Status, m_FriendsHint, m_NewsBody, m_ResultDetails };
    for ( int i = 0; i < ARRAYSIZE( muted ); ++i ) muted[i]->SetFgColor( Color( 184, 193, 195, 255 ) );
    m_PartyList->SetPaintBackgroundEnabled( false ); m_PartyList->SetBorder( NULL );
    m_FriendsList->SetPaintBackgroundEnabled( false ); m_FriendsList->SetBorder( NULL );
}

void CHL2MPMainMenu::PerformLayout()
{
    BaseClass::PerformLayout(); int w, h; vgui::surface()->GetScreenSize( w, h ); SetBounds( 0, 0, w, h );
    const float s = m_Scale = MAX( 0.65f, MIN( MIN( h / 720.0f, w / 1024.0f ), 2.5f ) );
    const int left = MAX( int( 52 * s ), ( w - int( 1400 * s ) ) / 2 ), pad = int( 16 * s ), gap = int( 24 * s );
    m_PartyW = int( 280 * s ); m_PartyX = w - left - m_PartyW; m_PartyY = int( 48 * s );
    m_PartyH = int( ( m_GroupedLayout ? 284 : 192 ) * s ); m_FriendsY = m_PartyY + m_PartyH + int( 24 * s );
    const int mainW = m_PartyX - left - gap;
    if ( w != m_FontWidth || h != m_FontHeight )
    {
        // Video mode changes invalidate surface font handles, not just glyph sizes.
        m_FontWidth = w; m_FontHeight = h;
        MenuFontFile( "HL2MP", "resource/hl2mp.ttf" ); MenuFontFile( "GorDIN", "resource/gordin-regular.ttf" ); MenuFontFile( "GorDIN Bold", "resource/gordin-bold.ttf" );
        m_LogoFont = vgui::surface()->CreateFont(); m_BrandFont = vgui::surface()->CreateFont(); m_NavFont = vgui::surface()->CreateFont();
        m_CardFont = vgui::surface()->CreateFont(); m_BodyFont = vgui::surface()->CreateFont(); m_SectionFont = vgui::surface()->CreateFont(); m_DetailFont = vgui::surface()->CreateFont();
    }
    int logoHeight = int( ( m_PlayPage ? 60 : 94 ) * s ); MenuFont( m_LogoFont, "HL2MP", logoHeight );
    int logoW, logoH; vgui::surface()->GetTextSize( m_LogoFont, L"deathmatch", logoW, logoH );
    if ( logoW > mainW ) MenuFont( m_LogoFont, "HL2MP", MAX( 1, logoHeight * mainW / logoW ) );
    MenuFont( m_BrandFont, "HL2MP", int( 20 * s ) ); MenuFont( m_NavFont, "GorDIN", MAX( 22, int( 31 * s ) ) );
    MenuFont( m_CardFont, "GorDIN", MAX( 16, int( 24 * s ) ) ); MenuFont( m_BodyFont, "GorDIN", MAX( 13, int( 18 * s ) ) );
    MenuFont( m_SectionFont, "GorDIN Bold", MAX( 11, int( 14 * s ) ), 700 ); MenuFont( m_DetailFont, "GorDIN", MAX( 11, int( 14 * s ) ) );
    m_Title->SetFont( m_LogoFont ); m_Subtitle->SetFont( m_BrandFont ); m_PlayHeading->SetFont( m_NavFont );
    vgui::Label *sections[] = { m_Tag, m_PartyHeading, m_FriendsHeading, m_NewsHeading, m_ResultHeading };
    for ( int i = 0; i < ARRAYSIZE( sections ); ++i ) sections[i]->SetFont( m_SectionFont );
    m_ModeDescription->SetFont( m_BodyFont ); m_QueueStatus->SetFont( m_BodyFont ); m_NewsTitle->SetFont( m_CardFont );
    vgui::Label *details[] = { m_Status, m_FriendsHint, m_ResultDetails, m_NewsBody };
    for ( int i = 0; i < ARRAYSIZE( details ); ++i ) details[i]->SetFont( m_DetailFont );
    m_Title->SetBounds( left, int( ( m_PlayPage ? 24 : 96 ) * s ), mainW, int( 100 * s ) );
    m_Subtitle->SetBounds( left, int( 66 * s ), mainW, int( 30 * s ) ); m_Tag->SetBounds( left, int( 194 * s ), mainW, int( 24 * s ) );
    int navY = int( 274 * s ); const int navOrder[] = { RESUME, PLAY, SERVERS, OPTIONS, DISCONNECT, QUIT };
    for ( int i = 0; i < ARRAYSIZE( navOrder ); ++i )
    {
        int id = navOrder[i]; m_Buttons[id]->SetBounds( left - int( 14 * s ), navY, MIN( mainW, int( 460 * s ) ), int( 46 * s ) );
        if ( ( id != RESUME && id != DISCONNECT ) || m_InGameLayout ) navY += int( 49 * s );
    }
    m_Buttons[BACK]->SetBounds( left - int( 14 * s ), int( 120 * s ), int( 190 * s ), int( 34 * s ) );
    if ( m_PlayPage ) m_Buttons[RESUME]->SetBounds( left + mainW - int( 190 * s ), int( 120 * s ), int( 190 * s ), int( 34 * s ) );
    m_PlayHeading->SetBounds( left, int( 161 * s ), mainW, int( 38 * s ) );
    const int cardGap = int( 10 * s ), cardW = ( mainW - cardGap * 2 ) / 3;
    for ( int i = 0; i < 6; ++i ) m_Buttons[DUEL + i]->SetBounds( left + ( i % 3 ) * ( cardW + cardGap ), int( ( 210 + ( i / 3 ) * 76 ) * s ), cardW, int( 66 * s ) );
    m_Buttons[CUSTOM]->SetBounds( left, int( 362 * s ), mainW, int( 66 * s ) );
    const bool custom = m_SelectedMode == 6;
    m_ModeDescription->SetBounds( left, int( 438 * s ), mainW, int( ( custom ? 38 : 48 ) * s ) );
    const int fieldW = ( mainW - cardGap ) / 2;
    for ( int i = 0; i < 4; ++i )
    {
        const int x = left + ( i % 2 ) * ( fieldW + cardGap ), y = int( ( 480 + ( i / 2 ) * 38 ) * s ), captionW = int( 58 * s );
        m_CustomCaptions[i]->SetFont( m_DetailFont ); m_CustomCaptions[i]->SetFgColor( Color( 184, 193, 195, 255 ) );
        m_CustomCaptions[i]->SetBounds( x, y, captionW, int( 32 * s ) );
        m_CustomControls[i]->SetBounds( x + captionW, y, fieldW - captionW, int( 32 * s ) ); m_CustomControls[i]->Style( m_DetailFont, s );
    }
    m_Buttons[BROWSE]->SetBounds( left, int( 560 * s ), mainW, int( 32 * s ) );
    const int actionY = int( ( custom ? 604 : 550 ) * s );
    m_Buttons[FIND]->SetBounds( left, actionY, fieldW, int( 44 * s ) ); m_Buttons[ACCEPT]->SetBounds( left, actionY, fieldW, int( 44 * s ) );
    m_Buttons[CANCEL]->SetBounds( left + fieldW + cardGap, actionY, fieldW, int( 44 * s ) );
    m_QueueStatus->SetBounds( left, int( ( custom ? 652 : 492 ) * s ), mainW, int( 42 * s ) );
    m_PartyHeading->SetBounds( m_PartyX + pad, m_PartyY + pad, m_PartyW - pad * 2, int( 24 * s ) );
    m_PartyList->SetBounds( m_PartyX + pad, m_PartyY + int( 46 * s ), m_PartyW - pad * 2, int( ( m_GroupedLayout ? 86 : 28 ) * s ) );
    FOR_EACH_VEC( m_Members, i ) { m_Members[i]->SetFont( m_BodyFont ); m_Members[i]->SetTall( int( 27 * s ) ); m_Members[i]->SetBorder( NULL ); }
    m_PartyList->GetScrollbar()->SetWide( MAX( 10, int( 12 * s ) ) ); m_FriendsList->GetScrollbar()->SetWide( MAX( 10, int( 12 * s ) ) );
    m_Status->SetBounds( m_PartyX + pad, m_PartyY + int( ( m_GroupedLayout ? 138 : 80 ) * s ), m_PartyW - pad * 2, int( 48 * s ) );
    const int partyActionY = m_PartyY + m_PartyH - int( ( m_GroupedLayout ? 92 : 54 ) * s );
    m_Buttons[CREATE_PARTY]->SetBounds( m_PartyX + pad, partyActionY, m_PartyW - pad * 2, int( 38 * s ) );
    m_Buttons[INVITE]->SetBounds( m_PartyX + pad, partyActionY, ( m_PartyW - pad * 3 ) / 2, int( 36 * s ) );
    m_Buttons[LEAVE]->SetBounds( m_PartyX + ( m_PartyW + pad ) / 2, partyActionY, ( m_PartyW - pad * 3 ) / 2, int( 36 * s ) );
    m_Buttons[READY]->SetBounds( m_PartyX + pad, partyActionY + int( 42 * s ), m_PartyW - pad * 2, int( 36 * s ) );
    m_FriendsHeading->SetBounds( m_PartyX + pad, m_FriendsY + pad, m_PartyW - pad * 2, int( 24 * s ) );
    m_FriendsList->SetBounds( m_PartyX + pad, m_FriendsY + int( 50 * s ), m_PartyW - pad * 2, MAX( int( 70 * s ), h - m_FriendsY - int( 144 * s ) ) );
    m_FriendsHint->SetBounds( m_PartyX + pad, h - int( 86 * s ), m_PartyW - pad * 2, int( 48 * s ) );
    m_NewsX = left; m_NewsY = h - int( 154 * s ); m_NewsW = MIN( mainW, int( 600 * s ) );
    m_NewsHeading->SetBounds( left + pad, m_NewsY + int( 12 * s ), m_NewsW - pad * 2, int( 24 * s ) );
    m_NewsTitle->SetBounds( left + pad, m_NewsY + int( 38 * s ), m_NewsW - pad * 2, int( 32 * s ) );
    m_NewsBody->SetBounds( left + pad, m_NewsY + int( 70 * s ), m_NewsW - pad * 2, int( 48 * s ) );
    m_ResultHeading->SetBounds( left, h - int( 92 * s ), mainW, int( 24 * s ) ); m_ResultDetails->SetBounds( left, h - int( 65 * s ), mainW, int( 42 * s ) );
    for ( int i = 0; i < BUTTON_COUNT; ++i ) m_Buttons[i]->SetMenuScale( s, i == RESUME && m_PlayPage ? m_BodyFont : i <= QUIT ? m_NavFont : i >= DUEL && i <= CUSTOM ? m_CardFont : i >= BROWSE ? m_DetailFont : m_BodyFont, m_DetailFont, true );
    m_NextFriends = 0;
}

void CHL2MPMainMenu::PaintBackground()
{
    vgui::ISurface *surface = vgui::surface(); const int w = GetWide(), h = GetTall();
    if ( !engine->IsInGame() && !engine->IsLevelMainMenuBackground() )
    {
        surface->DrawSetColor( Color( 255, 255, 255, 255 ) ); surface->DrawSetTexture( m_Background );
        const float aspect = 16.0f / 9.0f, viewport = float( w ) / MAX( h, 1 );
        const float sx = MIN( 1.0f, viewport / aspect ), sy = MIN( 1.0f, aspect / viewport );
        surface->DrawTexturedSubRect( 0, 0, w, h, ( 1 - sx ) / 2, ( 1 - sy ) / 2, ( 1 + sx ) / 2, ( 1 + sy ) / 2 );
    }
    if ( m_Options->IsVisible() || m_Browser->IsVisible() ) return;
    surface->DrawSetColor( Color( 4, 8, 12, 255 ) );
    surface->DrawFilledRectFade( 0, 0, int( w * 0.8f ), h, m_PlayPage ? 225 : 190, 0, true );
    surface->DrawFilledRectFade( 0, h / 2, w, h, 0, 150, false );
    surface->DrawFilledRectFade( 0, 0, w, h / 4, 65, 0, false );
    surface->DrawSetColor( Color( 10, 15, 19, 218 ) );
    surface->DrawFilledRect( m_PartyX, m_PartyY, m_PartyX + m_PartyW, m_PartyY + m_PartyH );
    surface->DrawFilledRect( m_PartyX, m_FriendsY, m_PartyX + m_PartyW, h - int( 32 * m_Scale ) );
    if ( !m_PlayPage && !m_InGameLayout ) surface->DrawFilledRect( m_NewsX, m_NewsY, m_NewsX + m_NewsW, h - int( 24 * m_Scale ) );
    surface->DrawSetColor( Color( 220, 226, 225, 45 ) );
    surface->DrawFilledRect( m_PartyX, m_PartyY, m_PartyX + m_PartyW, m_PartyY + 1 );
    surface->DrawFilledRect( m_PartyX, m_FriendsY, m_PartyX + m_PartyW, m_FriendsY + 1 );
}

void CHL2MPMainMenu::SetPlayPage( bool play )
{
    m_PlayPage = play; OnTick(); InvalidateLayout( true ); m_Buttons[play ? DUEL + m_SelectedMode : PLAY]->RequestFocus();
}

void CHL2MPMainMenu::UpdateFriends()
{
    if ( Plat_FloatTime() < m_NextFriends ) return; m_NextFriends = Plat_FloatTime() + 2;
    CUtlVector<CSteamID> ids; int online = 0;
    if ( HL2MPParty().Available() )
    {
        ISteamFriends *friends = steamapicontext->SteamFriends();
        // Keep stable Steam ordering within online and offline groups.
        for ( int pass = 0; pass < 2; ++pass )
            for ( int i = 0; i < friends->GetFriendCount( k_EFriendFlagImmediate ); ++i )
            {
                CSteamID id = friends->GetFriendByIndex( i, k_EFriendFlagImmediate );
                const bool available = friends->GetFriendPersonaState( id ) != k_EPersonaStateOffline;
                if ( available != ( pass == 0 ) ) continue;
                ids.AddToTail( id ); if ( available ) ++online;
            }
    }
    bool changed = ids.Count() != m_Friends.Count();
    for ( int i = 0; !changed && i < ids.Count(); ++i ) changed = ids[i] != m_Friends[i]->ID();
    if ( changed )
    {
        m_FriendsList->DeleteAllItems(); m_Friends.Purge();
        FOR_EACH_VEC( ids, i ) { CMatchmakingFriendRow *row = new CMatchmakingFriendRow( m_FriendsList, ids[i] ); m_Friends.AddToTail( row ); m_FriendsList->AddItem( NULL, row ); }
    }
    FOR_EACH_VEC( m_Friends, i ) m_Friends[i]->Update( m_Scale, m_BodyFont, m_DetailFont );
    m_FriendsList->InvalidateLayout( true );
    m_FriendsHeading->SetText( VarArgs( "FRIENDS  /  %d ONLINE", online ) );
    m_FriendsHint->SetText( !HL2MPParty().Available() ? "Sign in to Steam to see your friends." : !ids.Count() ? "Your Steam friends will appear here." : HL2MPParty().Status() );
}

void CHL2MPMainMenu::OnTick()
{
    HL2MPParty().Tick(); HL2MPCoordinator().Tick();
    CHL2MPParty &party = HL2MPParty(); CHL2MPCoordinator &coordinator = HL2MPCoordinator();
    const bool grouped = party.Lobby().IsValid(), readyCheck = coordinator.NeedsConfirmation();
    const bool inGame = engine->IsInGame() && !engine->IsLevelMainMenuBackground();
    if ( engine->IsInGame() || engine->IsLevelMainMenuBackground() || engine->IsConnected() || engine->IsDrawingLoadingImage() || HL2MPLoading_IsVisible() || coordinator.Busy() ) { m_IdleSince = Plat_FloatTime(); m_BackgroundRequested = false; }
    else if ( mm_menu_background.GetBool() && !m_BackgroundRequested && filesystem->FileExists( "maps/dm_lockdown.bsp", "GAME" ) && Plat_FloatTime() - m_IdleSince > 3 )
    {
        m_BackgroundRequested = true;
        Msg( "Menu background: starting the local dm_lockdown scene, not joining a match.\n" );
        engine->ClientCmd_Unrestricted( "maxplayers 1; map_background dm_lockdown" );
    }
    const char *activeMode = coordinator.Mode();
    for ( int i = 0; i < ARRAYSIZE( s_Modes ); ++i ) if ( !Q_strcmp( activeMode, s_Modes[i] ) ) m_SelectedMode = i;
    const bool matchmaking = activeMode[0] && coordinator.Busy();
    if ( matchmaking && !m_WasBusy ) m_PlayPage = true;
    m_WasBusy = matchmaking;
    int w, h; vgui::surface()->GetScreenSize( w, h );
    if ( w != m_ScreenWidth || h != m_ScreenHeight ) m_FontRefreshAt = Plat_FloatTime() + 1;
    if ( m_FontRefreshAt && Plat_FloatTime() >= m_FontRefreshAt ) { m_FontRefreshAt = 0; OnCommand( "refresh_menu_fonts" ); }
    if ( w != m_ScreenWidth || h != m_ScreenHeight || grouped != m_GroupedLayout || inGame != m_InGameLayout )
    {
        m_ScreenWidth = w; m_ScreenHeight = h; m_GroupedLayout = grouped; m_InGameLayout = inGame; InvalidateLayout( true );
    }
    for ( int i = PLAY; i <= QUIT; ++i ) m_Buttons[i]->SetVisible( !m_PlayPage && ( ( i != RESUME && i != DISCONNECT ) || inGame ) );
    m_Buttons[RESUME]->SetVisible( inGame );
    m_Buttons[BACK]->SetVisible( m_PlayPage );
    m_Subtitle->SetVisible( !m_PlayPage ); m_Tag->SetVisible( !m_PlayPage ); m_PlayHeading->SetVisible( m_PlayPage );
    m_ModeDescription->SetVisible( m_PlayPage ); m_QueueStatus->SetVisible( m_PlayPage );
    const bool custom = m_SelectedMode == 6;
    const bool canQueue = !inGame && !party.Pending() && ( !grouped || ( party.IsOwner() && coordinator.PartySynchronized() ) ) && !coordinator.Busy();
    for ( int i = DUEL; i <= CUSTOM; ++i )
    {
        m_Buttons[i]->SetVisible( m_PlayPage );
        // Selecting a mode is useful even while sign-in is unavailable.
        m_Buttons[i]->SetEnabled( !coordinator.Busy() ); m_Buttons[i]->SetModeSelected( i == DUEL + m_SelectedMode );
    }
    const char *descriptions[] = { "One opponent. Every frag counts. Ranked 1v1.", "Two teams of two. Queue together and fight together.", "Three per team. Your party stays on the same side.", "Four per team. Coordinate, control, and win.", "Five per team. Bring your full squad.", "Drop in, warm up, or stay a while. Unranked deathmatch.", "Browse community servers, or play a private unranked match with your party." };
    m_ModeDescription->SetText( custom && coordinator.Busy() && coordinator.CustomSummary()[0] ? coordinator.CustomSummary() : descriptions[m_SelectedMode] );
    const int limit = HL2MPMatchTeamSize( s_Modes[m_SelectedMode] );
    m_Buttons[FIND]->SetVisible( m_PlayPage && !coordinator.Busy() ); m_Buttons[FIND]->SetEnabled( canQueue && ( limit == 0 || party.MemberCount() <= limit ) );
    m_Buttons[FIND]->SetText( custom ? "START PRIVATE MATCH" : "FIND MATCH" );
    m_Buttons[ACCEPT]->SetVisible( m_PlayPage && ( readyCheck || coordinator.HasServer() ) ); m_Buttons[ACCEPT]->SetText( coordinator.HasServer() ? "JOIN / RECONNECT" : "ACCEPT MATCH" );
    m_Buttons[CANCEL]->SetVisible( m_PlayPage && ( coordinator.Searching() || coordinator.HasReadyCheck() || coordinator.CanLeaveCasual() ) ); m_Buttons[CANCEL]->SetText( coordinator.CanLeaveCasual() ? "LEAVE MATCH" : "CANCEL / DECLINE" );
    m_QueueStatus->SetText( coordinator.Status() ); m_ResultHeading->SetText( coordinator.ResultHeading() ); m_ResultDetails->SetText( coordinator.ResultDetails() );
    if ( inGame && !coordinator.Busy() ) m_QueueStatus->SetText( "Disconnect from your current game before starting another match." );
    else if ( custom && !coordinator.Busy() && coordinator.ResultHeading()[0] ) m_QueueStatus->SetText( VarArgs( "%s  %s", coordinator.ResultHeading(), coordinator.ResultDetails() ) );
    else if ( !coordinator.Busy() && limit > 0 && party.MemberCount() > limit ) m_QueueStatus->SetText( VarArgs( "This queue allows parties of up to %d. Choose Custom to play together.", limit ) );
    m_ResultHeading->SetVisible( m_PlayPage && !custom ); m_ResultDetails->SetVisible( m_PlayPage && !custom );
    m_Buttons[BROWSE]->SetVisible( m_PlayPage && custom && !coordinator.Busy() );
    int *choices[] = { &m_MapChoice, &m_SizeChoice, &m_TimeChoice, &m_FragChoice };
    for ( int i = 0; i < 4; ++i )
    {
        const bool visible = m_PlayPage && custom && !coordinator.Busy() && !m_Options->IsVisible() && !m_Browser->IsVisible();
        if ( !visible && m_CustomControls[i]->IsDropdownVisible() ) m_CustomControls[i]->HideMenu();
        m_CustomControls[i]->SetVisible( visible ); m_CustomCaptions[i]->SetVisible( visible );
        KeyValues *data = m_CustomControls[i]->GetActiveItemUserData(); if ( data ) *choices[i] = data->GetInt( "choice" );
    }
    m_Buttons[CREATE_PARTY]->SetVisible( !grouped && !party.Pending() ); m_Buttons[CREATE_PARTY]->SetEnabled( party.Available() && !coordinator.Busy() );
    m_Buttons[INVITE]->SetVisible( grouped ); m_Buttons[LEAVE]->SetVisible( grouped || party.Pending() );
    m_Buttons[LEAVE]->SetEnabled( !coordinator.Busy() ); m_Buttons[INVITE]->SetEnabled( !coordinator.Busy() );
    m_Buttons[READY]->SetVisible( grouped ); m_Buttons[READY]->SetEnabled( coordinator.PartySynchronized() && !coordinator.Busy() );
    m_Buttons[READY]->SetText( coordinator.PartyReady() ? "READY  /  UNREADY" : "READY TO PLAY" );
    m_Status->SetText( party.Available() ? ( grouped ? coordinator.PartyStatus() : party.Status() ) : "Steam is offline. The server browser is available." );
    m_PartyHeading->SetText( grouped ? VarArgs( "YOUR PARTY  /  %d OF 10%s", party.MemberCount(), party.IsOwner() ? "  /  LEADER" : "" ) : "YOUR PARTY  /  SOLO" );
    const int memberCount = MAX( 1, party.MemberCount() );
    if ( m_Members.Count() != memberCount )
    {
        m_PartyList->DeleteAllItems(); m_Members.Purge();
        for ( int i = 0; i < memberCount; ++i )
        {
            vgui::Label *label = new vgui::Label( m_PartyList, "Member", "" ); label->SetMouseInputEnabled( false );
            label->SetFont( m_BodyFont ); label->SetTall( int( 27 * m_Scale ) ); m_Members.AddToTail( label ); m_PartyList->AddItem( NULL, label );
        }
        m_PartyList->InvalidateLayout( true );
    }
    FOR_EACH_VEC( m_Members, i )
    {
        const bool ready = coordinator.PartyMemberReady( party.Member( i ) );
        m_Members[i]->SetText( i < party.MemberCount() ? VarArgs( "%s  /  %s", ready ? "READY" : "WAITING", party.MemberName( i ) ) : party.Available() ? steamapicontext->SteamFriends()->GetPersonaName() : "You" );
        m_Members[i]->SetFgColor( ready ? Color( 155, 206, 159, 255 ) : Color( 215, 219, 221, 255 ) );
    }
    const bool news = !m_PlayPage && !inGame; m_NewsHeading->SetVisible( news ); m_NewsTitle->SetVisible( news ); m_NewsBody->SetVisible( news );
    UpdateFriends();
    if ( readyCheck && !m_WasReady ) m_Buttons[ACCEPT]->RequestFocus(); m_WasReady = readyCheck;
    m_Title->SetVisible( true ); m_PartyHeading->SetVisible( true ); m_Status->SetVisible( true ); m_PartyList->SetVisible( true );
    m_FriendsHeading->SetVisible( true ); m_FriendsHint->SetVisible( true ); m_FriendsList->SetVisible( true );
    vgui::Panel *overlay = m_Options->IsVisible() ? m_Options : m_Browser->IsVisible() ? m_Browser : NULL;
    if ( overlay ) for ( int i = 0; i < GetChildCount(); ++i ) if ( GetChild( i ) != overlay ) GetChild( i )->SetVisible( false );
}

void CHL2MPMainMenu::OnCommand( const char *command )
{
    if ( !Q_strcmp( command, "play" ) ) SetPlayPage( true );
    else if ( !Q_strcmp( command, "back" ) ) SetPlayPage( false );
    else if ( !Q_strcmp( command, "disconnect" ) )
    {
        if ( engine->IsInGame() && !engine->IsLevelMainMenuBackground() ) engine->ClientCmd_Unrestricted( "disconnect" );
        SetPlayPage( false );
    }
    else if ( !Q_strncmp( command, "mode_", 5 ) )
    {
        for ( int i = 0; i < ARRAYSIZE( s_Modes ); ++i ) if ( !Q_strcmp( command + 5, s_Modes[i] ) ) m_SelectedMode = i;
        OnTick(); InvalidateLayout( true );
    }
    else if ( !Q_strcmp( command, "OpenServerBrowser" ) ) { HL2MPServerBrowser_Open( m_Browser ); OnTick(); }
    else if ( !Q_strcmp( command, "browser_closed" ) ) { OnTick(); m_Buttons[m_PlayPage ? BROWSE : SERVERS]->RequestFocus(); }
    else if ( !Q_strcmp( command, "OpenOptionsDialog" ) ) { HL2MPOptions_Open( m_Options ); OnTick(); }
    else if ( !Q_strcmp( command, "options_closed" ) ) { OnTick(); m_Buttons[OPTIONS]->RequestFocus(); }
    else if ( !Q_strcmp( command, "refresh_menu_fonts" ) )
    {
        m_FontWidth = 0; InvalidateLayout( true ); m_Options->OnCommand( "refresh_fonts" ); m_Browser->OnCommand( "refresh_fonts" );
    }
    else if ( !Q_strcmp( command, "find_match" ) )
    {
        OnTick(); // Consume the latest dropdown selection before submitting rules.
        if ( m_SelectedMode == 6 ) HL2MPCoordinator().CustomMatch( s_Maps[m_MapChoice], m_SizeChoice, s_Times[m_TimeChoice], s_Frags[m_FragChoice] );
        else HL2MPCoordinator().Queue( s_Modes[m_SelectedMode] );
    }
    else if ( !Q_strcmp( command, "party_create" ) ) HL2MPParty().Create();
    else if ( !Q_strcmp( command, "party_invite" ) ) HL2MPParty().Invite();
    else if ( !Q_strcmp( command, "party_leave" ) ) HL2MPParty().Leave();
    else if ( !Q_strcmp( command, "party_ready" ) ) HL2MPCoordinator().TogglePartyReady();
    else if ( !Q_strcmp( command, "match_accept" ) ) { if ( HL2MPCoordinator().HasServer() ) HL2MPCoordinator().Connect(); else HL2MPCoordinator().Confirm(); }
    else if ( !Q_strcmp( command, "queue_cancel" ) ) HL2MPCoordinator().Cancel();
    else
    {
        const char *allowed[] = { "ResumeGame", "Quit" };
        for ( int i = 0; i < ARRAYSIZE( allowed ); ++i ) if ( !Q_strcmp( command, allowed[i] ) ) { m_GameUI->SendMainMenuCommand( allowed[i] ); return; }
        BaseClass::OnCommand( command );
    }
}

void CHL2MPMainMenu::OnThink()
{
    BaseClass::OnThink();
    // GameUI's native menu is itself a popup, so our override must remain a
    // popup for keyboard focus. Keep the console above it when a menu click
    // raises this fullscreen panel; otherwise only console child widgets show.
    if ( !IsVisible() ) return;
    vgui::VPANEL console = 0;
    for ( int i = 0; i < vgui::surface()->GetPopupCount(); ++i )
    {
        const vgui::VPANEL panel = vgui::surface()->GetPopup( i );
        if ( panel == GetVPanel() )
        {
            if ( console ) vgui::surface()->BringToFront( console );
            break;
        }
        if ( vgui::ipanel()->IsVisible( panel ) && !Q_strcmp( vgui::ipanel()->GetName( panel ), "GameConsole" ) ) console = panel;
    }
}

void CHL2MPMainMenu::OnKeyCodeTyped( vgui::KeyCode code )
{
    if ( code == KEY_ESCAPE && m_PlayPage && !m_Options->IsVisible() && !m_Browser->IsVisible() )
    {
        for ( int i = 0; i < 4; ++i ) if ( m_CustomControls[i]->IsDropdownVisible() ) { m_CustomControls[i]->HideMenu(); return; }
        SetPlayPage( false ); return;
    }
    BaseClass::OnKeyCodeTyped( code );
}

void HL2MPMainMenu_Create()
{
	if ( s_Menu || !mm_menu.GetBool() ) return;
	CreateInterfaceFn factory = s_GameUI.GetFactory();
	IGameUI *gameUI = factory ? static_cast<IGameUI *>( factory( GAMEUI_INTERFACE_VERSION, NULL ) ) : NULL;
	if ( !gameUI ) { Warning( "Matchmaking menu: GameUI011 unavailable; using retail menu.\n" ); return; }
	s_Menu = new CHL2MPMainMenu( gameUI );
	HL2MPLoading_Create( gameUI );
}

void HL2MPMainMenu_Destroy()
{
	HL2MPLoading_Destroy();
	delete s_Menu; s_Menu = NULL;
}

CON_COMMAND_F( mm_menu_status, "Show menu and background state.", FCVAR_CLIENTDLL )
{
    C_BasePlayer *player = C_BasePlayer::GetLocalPlayer();
    Msg( "Menu: in_game=%d, background=%d, connected=%d, loading=%d, level=%s, local_player=%d\n",
        engine->IsInGame(), engine->IsLevelMainMenuBackground(), engine->IsConnected(), engine->IsDrawingLoadingImage(), engine->GetLevelName(), player ? 1 : 0 );
    if ( player ) Msg( "Menu player: %.1f %.1f %.1f\n", player->GetAbsOrigin().x, player->GetAbsOrigin().y, player->GetAbsOrigin().z );
    INetChannelInfo *channel = engine->GetNetChannelInfo();
    Msg( "Connection: %s; address=%s, loopback=%d\n", engine->IsLevelMainMenuBackground() ? "local menu background (not a match)" : engine->IsInGame() ? "game session" : "no active game",
        channel ? channel->GetAddress() : "none", channel && channel->IsLoopback() );
}
