#include "cbase.h"
#include "hl2mp_mainmenu.h"
#include "hl2mp_party.h"
#include "hl2mp_coordinator.h"
#include "GameUI/IGameUI.h"
#include "ienginevgui.h"
#include "vgui/IVGui.h"
#include "vgui/ISurface.h"
#include "vgui/IScheme.h"
#include "vgui_controls/EditablePanel.h"
#include "vgui_controls/Button.h"
#include "vgui_controls/Label.h"
#include "tier1/interface.h"
#include "tier0/memdbgon.h"

static CDllDemandLoader s_GameUI( "GameUI" );
static ConVar mm_menu( "mm_menu", "1", FCVAR_ARCHIVE, "Use the matchmaking main menu (restart required)." );

class CMatchmakingButton : public vgui::Button
{
	DECLARE_CLASS_SIMPLE( CMatchmakingButton, vgui::Button );
public:
	CMatchmakingButton( vgui::Panel *parent, const char *name, const char *text, const char *command, vgui::HFont font )
		: BaseClass( parent, name, text, parent, command ), m_Font( font ) {}
	void ApplySchemeSettings( vgui::IScheme *scheme ) OVERRIDE
	{
		BaseClass::ApplySchemeSettings( scheme );
		SetFont( m_Font );
		SetDefaultColor( Color( 239, 240, 235, 255 ), Color( 38, 43, 47, 255 ) );
		SetArmedColor( Color( 20, 23, 26, 255 ), Color( 244, 163, 58, 255 ) );
		SetDepressedColor( Color( 20, 23, 26, 255 ), Color( 216, 139, 44, 255 ) );
		SetDisabledFgColor1( Color( 135, 143, 148, 255 ) );
		SetDisabledFgColor2( Color( 0, 0, 0, 0 ) );
		SetTextInset( 12, 0 );
		SetButtonBorderEnabled( false );
	}
	void PaintBorder() OVERRIDE {}
	void Paint() OVERRIDE
	{
		BaseClass::Paint();
		if ( HasFocus() )
		{
			vgui::surface()->DrawSetColor( Color( 244, 163, 58, 255 ) );
			vgui::surface()->DrawOutlinedRect( 0, 0, GetWide(), GetTall() );
		}
	}
private:
	vgui::HFont m_Font;
};

class CHL2MPMainMenu : public vgui::EditablePanel
{
	DECLARE_CLASS_SIMPLE( CHL2MPMainMenu, vgui::EditablePanel );
public:
	CHL2MPMainMenu( IGameUI *gameUI );
	~CHL2MPMainMenu();
	void PerformLayout() OVERRIDE;
	void ApplySchemeSettings( vgui::IScheme *scheme ) OVERRIDE;
	void PaintBackground() OVERRIDE;
	void OnTick() OVERRIDE;
	void OnCommand( const char *command ) OVERRIDE;
	void OnKeyCodePressed( vgui::KeyCode code ) OVERRIDE;

private:
	enum { DUEL, TEAM, CASUAL, CREATE_PARTY, INVITE, LEAVE, ACCEPT, CANCEL, RESUME, SERVERS, OPTIONS, QUIT, BUTTON_COUNT };
	IGameUI *m_GameUI;
	vgui::Button *m_Buttons[BUTTON_COUNT];
	vgui::Label *m_Title;
	vgui::Label *m_Subtitle;
	vgui::Label *m_PlayHeading;
	vgui::Label *m_PartyHeading;
	vgui::Label *m_Members[4];
	vgui::Label *m_Status;
	vgui::Label *m_QueueStatus;
	int m_ScreenWidth;
	int m_ScreenHeight;
	vgui::HFont m_TitleFont;
	vgui::HFont m_BodyFont;
	vgui::HFont m_SectionFont;
};

static CHL2MPMainMenu *s_Menu = NULL;

CHL2MPMainMenu::CHL2MPMainMenu( IGameUI *gameUI ) : BaseClass( NULL, "HL2MPMainMenu" ),
	m_GameUI( gameUI ), m_ScreenWidth( 0 ), m_ScreenHeight( 0 )
{
	SetProportional( false );
	SetMouseInputEnabled( true );
	SetKeyBoardInputEnabled( true );
	SetCursor( vgui::dc_arrow );
	m_TitleFont = vgui::surface()->CreateFont();
	m_BodyFont = vgui::surface()->CreateFont();
	m_SectionFont = vgui::surface()->CreateFont();
	m_Title = new vgui::Label( this, "Title", "HALF-LIFE 2" );
	m_Subtitle = new vgui::Label( this, "Subtitle", "DEATHMATCH  /  MATCHMAKING" );
	m_PlayHeading = new vgui::Label( this, "PlayHeading", "PLAY" );
	m_PartyHeading = new vgui::Label( this, "PartyHeading", "YOUR PARTY" );
	m_Status = new vgui::Label( this, "PartyStatus", "" );
	m_QueueStatus = new vgui::Label( this, "QueueStatus", "Classic combat. A new way to play together." );
	m_Status->SetWrap( true );
	m_QueueStatus->SetWrap( true );
	const char *labels[BUTTON_COUNT] = { "COMPETITIVE  /  DUEL", "COMPETITIVE  /  TEAM", "CASUAL  /  DEATHMATCH",
		"CREATE PARTY", "INVITE FRIENDS", "LEAVE PARTY", "ACCEPT MATCH", "CANCEL / DECLINE", "RESUME GAME", "SERVER BROWSER", "OPTIONS", "QUIT" };
	const char *commands[BUTTON_COUNT] = { "queue_duel", "queue_team", "queue_casual", "party_create",
		"party_invite", "party_leave", "match_accept", "queue_cancel", "ResumeGame", "OpenServerBrowser", "OpenOptionsDialog", "Quit" };
	for ( int i = 0; i < BUTTON_COUNT; ++i )
	{
		m_Buttons[i] = new CMatchmakingButton( this, commands[i], labels[i], commands[i], m_BodyFont );
		m_Buttons[i]->SetTabPosition( i + 1 );
		m_Buttons[i]->SetContentAlignment( vgui::Label::a_west );
	}
	for ( int i = 0; i < 4; ++i )
		m_Members[i] = new vgui::Label( this, VarArgs( "Member%d", i ), "" );
	HL2MPParty().Init();
	HL2MPCoordinator().Init();
	m_GameUI->SetMainMenuOverride( GetVPanel() );
	vgui::ivgui()->AddTickSignal( GetVPanel(), 100 );
}

CHL2MPMainMenu::~CHL2MPMainMenu()
{
	vgui::ivgui()->RemoveTickSignal( GetVPanel() );
	m_GameUI->SetMainMenuOverride( 0 );
	HL2MPParty().Shutdown();
	HL2MPCoordinator().Shutdown();
}

void CHL2MPMainMenu::ApplySchemeSettings( vgui::IScheme *scheme )
{
	BaseClass::ApplySchemeSettings( scheme );
	m_Title->SetFont( m_TitleFont );
	m_Title->SetFgColor( Color( 242, 242, 237, 255 ) );
	m_Subtitle->SetFgColor( Color( 244, 163, 58, 255 ) );
	m_PlayHeading->SetFgColor( Color( 244, 163, 58, 255 ) );
	m_PartyHeading->SetFgColor( Color( 244, 163, 58, 255 ) );
	m_Status->SetFgColor( Color( 188, 193, 196, 255 ) );
	m_QueueStatus->SetFgColor( Color( 188, 193, 196, 255 ) );
	for ( int i = 0; i < BUTTON_COUNT; ++i )
	{
		m_Buttons[i]->SetDefaultColor( Color( 239, 240, 235, 255 ), Color( 38, 43, 47, 255 ) );
		m_Buttons[i]->SetArmedColor( Color( 20, 23, 26, 255 ), Color( 244, 163, 58, 255 ) );
		m_Buttons[i]->SetDepressedColor( Color( 20, 23, 26, 255 ), Color( 216, 139, 44, 255 ) );
		m_Buttons[i]->SetTextInset( 12, 0 );
		m_Buttons[i]->SetButtonBorderEnabled( false );
	}
}

void CHL2MPMainMenu::PerformLayout()
{
	BaseClass::PerformLayout();
	int w, h;
	vgui::surface()->GetScreenSize( w, h );
	SetBounds( 0, 0, w, h );
	// Keep the menu usable at 640x480 and ultrawide, with a bounded reading width.
	const float scale = MAX( 0.65f, MIN( h / 720.0f, 2.5f ) );
	vgui::surface()->SetFontGlyphSet( m_TitleFont, "Tahoma", int( 42 * scale ), 600, 0, 0, vgui::ISurface::FONTFLAG_ANTIALIAS );
	vgui::surface()->SetFontGlyphSet( m_BodyFont, "Tahoma", int( 16 * scale ), 400, 0, 0, vgui::ISurface::FONTFLAG_ANTIALIAS );
	vgui::surface()->SetFontGlyphSet( m_SectionFont, "Tahoma", int( 13 * scale ), 600, 0, 0, vgui::ISurface::FONTFLAG_ANTIALIAS );
	m_Title->SetFont( m_TitleFont );
	m_Subtitle->SetFont( m_SectionFont );
	m_PlayHeading->SetFont( m_SectionFont );
	m_PartyHeading->SetFont( m_SectionFont );
	m_Status->SetFont( m_BodyFont );
	m_QueueStatus->SetFont( m_BodyFont );
	for ( int i = 0; i < BUTTON_COUNT; ++i ) m_Buttons[i]->SetFont( m_BodyFont );
	for ( int i = 0; i < 4; ++i ) m_Members[i]->SetFont( m_BodyFont );
	const int margin = int( 40 * scale );
	const int width = MIN( w - margin * 2, int( 1100 * scale ) );
	const int left = ( w - width ) / 2;
	const int gap = int( 24 * scale );
	const int column = ( width - gap ) / 2;
	const int right = left + column + gap;
	const int buttonHeight = int( 48 * scale );
	const int top = int( 190 * scale );
	m_Title->SetBounds( left, int( 58 * scale ), width, int( 50 * scale ) );
	m_Subtitle->SetBounds( left, int( 110 * scale ), width, int( 28 * scale ) );
	m_PlayHeading->SetBounds( left, top - int( 32 * scale ), column, int( 24 * scale ) );
	m_PartyHeading->SetBounds( right, top - int( 32 * scale ), column, int( 24 * scale ) );
	for ( int i = DUEL; i <= CASUAL; ++i )
		m_Buttons[i]->SetBounds( left, top + i * ( buttonHeight + gap / 2 ), column, buttonHeight );
	m_QueueStatus->SetBounds( left, top + 3 * ( buttonHeight + gap / 2 ), column, int( 80 * scale ) );
	m_Buttons[ACCEPT]->SetBounds( left, top + int( 280 * scale ), ( column - gap ) / 2, buttonHeight );
	m_Buttons[CANCEL]->SetBounds( left + ( column + gap ) / 2, top + int( 280 * scale ), ( column - gap ) / 2, buttonHeight );
	for ( int i = 0; i < 4; ++i )
		m_Members[i]->SetBounds( right, top + i * int( 30 * scale ), column, int( 28 * scale ) );
	m_Status->SetBounds( right, top + int( 125 * scale ), column, int( 70 * scale ) );
	m_Buttons[CREATE_PARTY]->SetBounds( right, top + int( 205 * scale ), column, buttonHeight );
	m_Buttons[INVITE]->SetBounds( right, top + int( 205 * scale ), ( column - gap ) / 2, buttonHeight );
	m_Buttons[LEAVE]->SetBounds( right + ( column + gap ) / 2, top + int( 205 * scale ), ( column - gap ) / 2, buttonHeight );
	const int footerY = h - margin - buttonHeight;
	const int footerWidth = ( width - gap * 3 ) / 4;
	for ( int i = RESUME; i < BUTTON_COUNT; ++i )
		m_Buttons[i]->SetBounds( left + ( i - RESUME ) * ( footerWidth + gap ), footerY, footerWidth, buttonHeight );
}

void CHL2MPMainMenu::PaintBackground()
{
	vgui::surface()->DrawSetColor( Color( 20, 24, 28, 255 ) );
	vgui::surface()->DrawFilledRect( 0, 0, GetWide(), GetTall() );
	vgui::surface()->DrawSetColor( Color( 244, 163, 58, 255 ) );
	vgui::surface()->DrawFilledRect( 0, 0, GetWide(), 3 );
}

void CHL2MPMainMenu::OnTick()
{
	HL2MPParty().Tick();
	HL2MPCoordinator().Tick();
	int w, h;
	vgui::surface()->GetScreenSize( w, h );
	if ( w != m_ScreenWidth || h != m_ScreenHeight )
	{
		m_ScreenWidth = w;
		m_ScreenHeight = h;
		InvalidateLayout( true );
	}
	CHL2MPParty &party = HL2MPParty();
	const bool grouped = party.Lobby().IsValid();
	m_Buttons[CREATE_PARTY]->SetVisible( !grouped && !party.Pending() );
	m_Buttons[CREATE_PARTY]->SetEnabled( party.Available() );
	m_Buttons[INVITE]->SetVisible( grouped );
	m_Buttons[LEAVE]->SetVisible( grouped || party.Pending() );
	m_Buttons[RESUME]->SetEnabled( engine->IsInGame() && !engine->IsLevelMainMenuBackground() );
	const bool canQueue = !party.Pending() && ( !grouped || party.IsOwner() ) && !HL2MPCoordinator().Busy();
	for ( int i = DUEL; i <= CASUAL; ++i ) m_Buttons[i]->SetEnabled( canQueue );
	m_QueueStatus->SetText( HL2MPCoordinator().Status() );
	m_Buttons[ACCEPT]->SetVisible( HL2MPCoordinator().NeedsConfirmation() );
	m_Buttons[CANCEL]->SetVisible( HL2MPCoordinator().Searching() || HL2MPCoordinator().HasReadyCheck() );
	m_Status->SetText( party.Available() ? party.Status() : "Steam is offline. Server browser and options are still available." );
	m_PartyHeading->SetText( grouped ? ( party.IsOwner() ? "YOUR PARTY  /  LEADER" : "YOUR PARTY" ) : "YOUR PARTY  /  SOLO" );
	for ( int i = 0; i < 4; ++i )
	{
		// SetText displays names as text; never interpolate Steam names into commands.
		m_Members[i]->SetText( i < party.MemberCount() ? party.MemberName( i ) : ( i == 0 && !grouped ? "You" : "Open slot" ) );
	}
}

void CHL2MPMainMenu::OnCommand( const char *command )
{
	if ( !Q_strcmp( command, "party_create" ) ) HL2MPParty().Create();
	else if ( !Q_strcmp( command, "party_invite" ) ) HL2MPParty().Invite();
	else if ( !Q_strcmp( command, "party_leave" ) ) HL2MPParty().Leave();
	else if ( !Q_strcmp( command, "match_accept" ) ) HL2MPCoordinator().Confirm();
	else if ( !Q_strcmp( command, "queue_cancel" ) ) HL2MPCoordinator().Cancel();
	else if ( !Q_strncmp( command, "queue_", 6 ) )
	{
		HL2MPCoordinator().Queue( command + 6 );
	}
	else
	{
		// Only forward our fixed menu actions, never lobby data or arbitrary commands.
		const char *allowed[] = { "ResumeGame", "OpenServerBrowser", "OpenOptionsDialog", "Quit" };
		for ( int i = 0; i < ARRAYSIZE( allowed ); ++i )
			if ( !Q_strcmp( command, allowed[i] ) ) { m_GameUI->SendMainMenuCommand( allowed[i] ); return; }
		BaseClass::OnCommand( command );
	}
}

void CHL2MPMainMenu::OnKeyCodePressed( vgui::KeyCode code )
{
	if ( code == KEY_ESCAPE && engine->IsInGame() && !engine->IsLevelMainMenuBackground() )
		m_GameUI->SendMainMenuCommand( "ResumeGame" );
	else BaseClass::OnKeyCodePressed( code );
}

void HL2MPMainMenu_Create()
{
	if ( s_Menu || !mm_menu.GetBool() ) return;
	CreateInterfaceFn factory = s_GameUI.GetFactory();
	IGameUI *gameUI = factory ? static_cast<IGameUI *>( factory( GAMEUI_INTERFACE_VERSION, NULL ) ) : NULL;
	if ( !gameUI ) { Warning( "Matchmaking menu: GameUI011 unavailable; using retail menu.\n" ); return; }
	s_Menu = new CHL2MPMainMenu( gameUI );
}

void HL2MPMainMenu_Destroy()
{
	delete s_Menu;
	s_Menu = NULL;
}
