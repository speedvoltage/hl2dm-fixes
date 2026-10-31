#include "cbase.h"
#include "cdll_client_int.h"
#include "hl2mp_mainmenu.h"
#include "hl2mp_party.h"
#include "hl2mp_coordinator.h"
#include "GameUI/IGameUI.h"
#include "ienginevgui.h"
#include "filesystem.h"
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
static ConVar mm_menu_motion( "mm_menu_motion", "1", FCVAR_ARCHIVE, "Animate the matchmaking menu." );
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

class CHL2MPMainMenu : public vgui::EditablePanel
{
	DECLARE_CLASS_SIMPLE( CHL2MPMainMenu, vgui::EditablePanel );
public:
	CHL2MPMainMenu( IGameUI *gameUI );
	~CHL2MPMainMenu();
	void PerformLayout() OVERRIDE;
	void ApplySchemeSettings( vgui::IScheme *scheme ) OVERRIDE;
	void PaintBackground() OVERRIDE;
	void OnThink() OVERRIDE { BaseClass::OnThink(); if ( IsVisible() && mm_menu_motion.GetBool() ) Repaint(); }
	void OnTick() OVERRIDE;
	void OnCommand( const char *command ) OVERRIDE;
private:
	void SetPlayPage( bool play );
	enum { PLAY, RESUME, SERVERS, OPTIONS, QUIT, BACK, DUEL, TEAM, CASUAL, FIND,
		CREATE_PARTY, INVITE, LEAVE, READY, ACCEPT, CANCEL, BUTTON_COUNT };
	IGameUI *m_GameUI;
	CMatchmakingButton *m_Buttons[BUTTON_COUNT];
	vgui::Label *m_Title, *m_Subtitle, *m_Tag, *m_PlayHeading, *m_ModeDescription;
	vgui::Label *m_PartyHeading, *m_Members[4], *m_Status, *m_QueueStatus, *m_ResultHeading, *m_ResultDetails;
	vgui::HFont m_LogoFont, m_DeathmatchFont, m_NavFont, m_CardFont, m_BodyFont, m_SectionFont, m_DetailFont;
	int m_ScreenWidth, m_ScreenHeight, m_Background, m_SelectedMode;
	int m_PartyX, m_PartyY, m_PartyW, m_PartyH;
	bool m_PlayPage, m_WasBusy, m_WasReady, m_GroupedLayout, m_InGameLayout;
	float m_Scale;
};

static CHL2MPMainMenu *s_Menu = NULL;

CHL2MPMainMenu::CHL2MPMainMenu( IGameUI *gameUI ) : BaseClass( NULL, "HL2MPMainMenu" ),
	m_GameUI( gameUI ), m_ScreenWidth( 0 ), m_ScreenHeight( 0 ), m_Background( -1 ), m_SelectedMode( 0 ),
	m_PartyX( 0 ), m_PartyY( 0 ), m_PartyW( 0 ), m_PartyH( 0 ), m_PlayPage( false ), m_WasBusy( false ),
	m_WasReady( false ), m_GroupedLayout( false ), m_InGameLayout( false ), m_Scale( 1 )
{
	SetProportional( false ); SetMouseInputEnabled( true ); SetKeyBoardInputEnabled( true ); SetCursor( vgui::dc_arrow );
	MenuFontFile( "HL2MP", "resource/hl2mp.ttf" );
	MenuFontFile( "GorDIN", "resource/gordin-regular.ttf" );
	MenuFontFile( "GorDIN Bold", "resource/gordin-bold.ttf" );
	m_LogoFont = vgui::surface()->CreateFont(); m_DeathmatchFont = vgui::surface()->CreateFont();
	m_NavFont = vgui::surface()->CreateFont(); m_CardFont = vgui::surface()->CreateFont();
	m_BodyFont = vgui::surface()->CreateFont(); m_SectionFont = vgui::surface()->CreateFont(); m_DetailFont = vgui::surface()->CreateFont();
	m_Background = vgui::surface()->CreateNewTextureID();
	vgui::surface()->DrawSetTextureFile( m_Background, "vgui/matchmaking/city17_yard", true, false );
	m_Title = new vgui::Label( this, "Title", "deathmatch" );
	m_Subtitle = new vgui::Label( this, "Subtitle", "HALF+LIFE'" );
	m_Tag = new vgui::Label( this, "Tag", "M A T C H M A K I N G" );
	m_PlayHeading = new vgui::Label( this, "PlayHeading", "PLAY" );
	m_ModeDescription = new vgui::Label( this, "ModeDescription", "" );
	m_PartyHeading = new vgui::Label( this, "PartyHeading", "YOUR PARTY" );
	m_Status = new vgui::Label( this, "PartyStatus", "" ); m_QueueStatus = new vgui::Label( this, "QueueStatus", "" );
	m_ResultHeading = new vgui::Label( this, "ResultHeading", "" ); m_ResultDetails = new vgui::Label( this, "ResultDetails", "" );
	vgui::Label *labels[] = { m_Title, m_Subtitle, m_Tag, m_PlayHeading, m_ModeDescription, m_PartyHeading, m_Status, m_QueueStatus, m_ResultHeading, m_ResultDetails };
	for ( int i = 0; i < ARRAYSIZE( labels ); ++i )
	{
		labels[i]->SetMouseInputEnabled( false ); labels[i]->SetKeyBoardInputEnabled( false ); labels[i]->SetContentAlignment( vgui::Label::a_west );
	}
	m_ModeDescription->SetWrap( true ); m_ResultDetails->SetWrap( true ); m_Status->SetWrap( true ); m_QueueStatus->SetWrap( true );
	const char *texts[BUTTON_COUNT] = { "PLAY", "RESUME GAME", "SERVER BROWSER", "OPTIONS", "QUIT", "<  BACK",
		"DUEL", "TEAM DEATHMATCH", "CASUAL", "FIND MATCH", "CREATE PARTY", "INVITE", "LEAVE", "READY TO PLAY", "ACCEPT MATCH", "CANCEL" };
	const char *commands[BUTTON_COUNT] = { "play", "ResumeGame", "OpenServerBrowser", "OpenOptionsDialog", "Quit", "back",
		"mode_duel", "mode_team", "mode_casual", "find_match", "party_create", "party_invite", "party_leave", "party_ready", "match_accept", "queue_cancel" };
	for ( int i = 0; i < BUTTON_COUNT; ++i )
	{
		const CMatchmakingButton::Style style = i <= BACK ? CMatchmakingButton::NAVIGATION :
			i <= CASUAL ? CMatchmakingButton::MODE : ( i == FIND || i == ACCEPT ) ? CMatchmakingButton::PRIMARY : CMatchmakingButton::ACTION;
		m_Buttons[i] = new CMatchmakingButton( this, commands[i], texts[i], style ); m_Buttons[i]->SetTabPosition( i + 1 );
	}
	m_Buttons[DUEL]->SetDetail( "1 vs 1   /   RANKED", 0, m_Background );
	m_Buttons[TEAM]->SetDetail( "2 vs 2   /   RANKED", 1, m_Background );
	m_Buttons[CASUAL]->SetDetail( "DROP-IN   /   UNRANKED", 2, m_Background );
	for ( int i = 0; i < 4; ++i )
	{
		m_Members[i] = new vgui::Label( this, VarArgs( "Member%d", i ), "" );
		m_Members[i]->SetMouseInputEnabled( false ); m_Members[i]->SetKeyBoardInputEnabled( false );
	}
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
	m_Title->SetFgColor( Color( 244, 243, 234, 255 ) ); m_Subtitle->SetFgColor( Color( 244, 243, 234, 255 ) );
	m_Tag->SetFgColor( s_Amber ); m_PlayHeading->SetFgColor( Color( 244, 243, 234, 255 ) ); m_PartyHeading->SetFgColor( s_Amber );
	m_ModeDescription->SetFgColor( Color( 216, 220, 220, 255 ) ); m_Status->SetFgColor( Color( 178, 187, 190, 255 ) );
	m_QueueStatus->SetFgColor( Color( 229, 230, 225, 255 ) ); m_ResultHeading->SetFgColor( s_Amber ); m_ResultDetails->SetFgColor( Color( 215, 219, 221, 255 ) );
}

void CHL2MPMainMenu::PerformLayout()
{
	BaseClass::PerformLayout();
	int w, h; vgui::surface()->GetScreenSize( w, h ); SetBounds( 0, 0, w, h );
	const float s = m_Scale = MAX( 0.65f, MIN( MIN( h / 720.0f, w / 1024.0f ), 2.5f ) );
	const int left = MAX( int( 60 * s ), ( w - int( 1320 * s ) ) / 2 ), gap = int( 28 * s );
	m_PartyW = int( 270 * s ); m_PartyX = w - left - m_PartyW;
	m_PartyH = int( ( m_GroupedLayout ? 334 : 194 ) * s );
	m_PartyY = m_PlayPage ? int( 198 * s ) : h - int( 60 * s ) - m_PartyH;
	const int mainWidth = m_PartyX - left - gap;
	const bool compact = mainWidth < int( 640 * s );
	int logoHeight = int( ( m_PlayPage ? 64 : 94 ) * s );
	MenuFont( m_LogoFont, "HL2MP", logoHeight );
	int logoWidth, logoTall; vgui::surface()->GetTextSize( m_LogoFont, L"deathmatch", logoWidth, logoTall );
	const int maxLogoWidth = MIN( w - left * 2, int( ( m_PlayPage ? 360 : 530 ) * s ) );
	if ( logoWidth > maxLogoWidth ) MenuFont( m_LogoFont, "HL2MP", MAX( 1, logoHeight * maxLogoWidth / logoWidth ) );
	MenuFont( m_DeathmatchFont, "HL2MP", int( 20 * s ) );
	MenuFont( m_NavFont, "GorDIN", MAX( 22, int( 31 * s ) ) );
	MenuFont( m_CardFont, "GorDIN", MAX( 16, int( 25 * s ) ) );
	MenuFont( m_BodyFont, "GorDIN", MAX( 14, int( 18 * s ) ) );
	MenuFont( m_SectionFont, "GorDIN Bold", MAX( 12, int( 14 * s ) ), 700 );
	MenuFont( m_DetailFont, "GorDIN", MAX( 12, int( 14 * s ) ) );
	m_Title->SetFont( m_LogoFont ); m_Subtitle->SetFont( m_DeathmatchFont ); m_Tag->SetFont( m_SectionFont );
	m_PlayHeading->SetFont( m_NavFont ); m_PartyHeading->SetFont( m_SectionFont ); m_ModeDescription->SetFont( m_BodyFont );
	m_Status->SetFont( m_DetailFont ); m_QueueStatus->SetFont( m_BodyFont ); m_ResultHeading->SetFont( m_SectionFont ); m_ResultDetails->SetFont( m_DetailFont );
	const int logoTop = int( ( m_PlayPage ? 32 : 96 ) * s );
	m_Title->SetBounds( left, logoTop, w - left * 2, int( ( m_PlayPage ? 82 : 100 ) * s ) );
	m_Subtitle->SetBounds( left, int( 64 * s ), w - left * 2, int( 32 * s ) );
	m_Tag->SetBounds( left + int( 3 * s ), int( 194 * s ), int( 460 * s ), int( 24 * s ) );
	int navY = int( 312 * s );
	const int navWidth = MIN( int( 470 * s ), mainWidth ), navOrder[] = { RESUME, PLAY, SERVERS, OPTIONS, QUIT };
	for ( int i = 0; i < ARRAYSIZE( navOrder ); ++i )
	{
		const int id = navOrder[i];
		m_Buttons[id]->SetBounds( left - int( 14 * s ), navY, navWidth, int( 48 * s ) );
		if ( id != RESUME || ( engine->IsInGame() && !engine->IsLevelMainMenuBackground() ) ) navY += int( 53 * s );
	}
	m_Buttons[BACK]->SetBounds( left - int( 14 * s ), int( 145 * s ), int( 150 * s ), int( 34 * s ) );
	m_PlayHeading->SetBounds( left, int( 184 * s ), mainWidth, int( 40 * s ) );
	const int cardGap = int( 12 * s );
	for ( int i = 0; i < 3; ++i )
	{
		if ( compact ) m_Buttons[DUEL + i]->SetBounds( left, int( ( 240 + 76 * i ) * s ), mainWidth, int( 68 * s ) );
		else m_Buttons[DUEL + i]->SetBounds( left + i * ( ( mainWidth - cardGap * 2 ) / 3 + cardGap ), int( 240 * s ), ( mainWidth - cardGap * 2 ) / 3, int( 184 * s ) );
	}
	const int contentEnd = int( ( compact ? 466 : 440 ) * s );
	m_ModeDescription->SetBounds( left, contentEnd, mainWidth, int( 48 * s ) );
	m_QueueStatus->SetBounds( left, contentEnd + int( 50 * s ), mainWidth, int( 48 * s ) );
	const int actionY = contentEnd + int( 108 * s ), actionWidth = ( mainWidth - cardGap ) / 2;
	m_Buttons[FIND]->SetBounds( left, actionY, actionWidth, int( 48 * s ) );
	m_Buttons[ACCEPT]->SetBounds( left, actionY, actionWidth, int( 48 * s ) );
	m_Buttons[CANCEL]->SetBounds( left + actionWidth + cardGap, actionY, actionWidth, int( 48 * s ) );
	const int pad = int( 18 * s );
	m_PartyHeading->SetBounds( m_PartyX + pad, m_PartyY + pad, m_PartyW - pad * 2, int( 24 * s ) );
	for ( int i = 0; i < 4; ++i )
	{
		m_Members[i]->SetFont( m_BodyFont );
		m_Members[i]->SetBounds( m_PartyX + pad, m_PartyY + int( ( 50 + i * 30 ) * s ), m_PartyW - pad * 2, int( 28 * s ) );
	}
	const int statusY = m_PartyY + int( ( m_GroupedLayout ? 176 : 86 ) * s );
	m_Status->SetBounds( m_PartyX + pad, statusY, m_PartyW - pad * 2, int( 56 * s ) );
	const int partyActionY = m_PartyY + m_PartyH - int( ( m_GroupedLayout ? 100 : 58 ) * s );
	m_Buttons[CREATE_PARTY]->SetBounds( m_PartyX + pad, partyActionY, m_PartyW - pad * 2, int( 40 * s ) );
	m_Buttons[INVITE]->SetBounds( m_PartyX + pad, partyActionY, ( m_PartyW - pad * 3 ) / 2, int( 40 * s ) );
	m_Buttons[LEAVE]->SetBounds( m_PartyX + ( m_PartyW + pad ) / 2, partyActionY, ( m_PartyW - pad * 3 ) / 2, int( 40 * s ) );
	m_Buttons[READY]->SetBounds( m_PartyX + pad, partyActionY + int( 46 * s ), m_PartyW - pad * 2, int( 40 * s ) );
	m_ResultHeading->SetBounds( left, h - int( 96 * s ), mainWidth, int( 24 * s ) );
	m_ResultDetails->SetBounds( left, h - int( 70 * s ), mainWidth, int( 42 * s ) );
	for ( int i = 0; i < BUTTON_COUNT; ++i )
		m_Buttons[i]->SetMenuScale( s, i <= QUIT ? m_NavFont : i >= DUEL && i <= CASUAL ? m_CardFont : m_BodyFont, m_DetailFont, compact );
}

void CHL2MPMainMenu::PaintBackground()
{
	vgui::ISurface *surface = vgui::surface(); const int w = GetWide(), h = GetTall();
	surface->DrawSetColor( Color( 255, 255, 255, 255 ) ); surface->DrawSetTexture( m_Background );
	// Cover the viewport without stretching the composition, including ultrawide.
	const float aspect = 16.0f / 9.0f, viewport = float( w ) / MAX( h, 1 );
	float spanX = MIN( 1.0f, viewport / aspect ), spanY = MIN( 1.0f, aspect / viewport );
	const float zoom = mm_menu_motion.GetBool() ? 0.98f : 1.0f;
	spanX *= zoom; spanY *= zoom;
	const float drift = mm_menu_motion.GetBool() ? sinf( float( Plat_FloatTime() * 0.035 ) ) * 0.003f : 0;
	const float x = ( 1 - spanX ) * 0.5f + drift, y = ( 1 - spanY ) * 0.5f;
	surface->DrawTexturedSubRect( 0, 0, w, h, x, y, x + spanX, y + spanY );
	surface->DrawSetColor( Color( 4, 8, 12, 255 ) );
	surface->DrawFilledRectFade( 0, 0, int( w * 0.78f ), h, m_PlayPage ? 220 : 145, 0, true );
	surface->DrawFilledRectFade( 0, h / 2, w, h, 0, 175, false );
	surface->DrawFilledRectFade( 0, 0, w, h / 4, 75, 0, false );
	surface->DrawSetColor( Color( 9, 15, 20, m_PlayPage ? 213 : 175 ) );
	surface->DrawFilledRect( m_PartyX, m_PartyY, m_PartyX + m_PartyW, m_PartyY + m_PartyH );
	surface->DrawSetColor( Color( 224, 229, 228, 45 ) );
	surface->DrawFilledRect( m_PartyX, m_PartyY, m_PartyX + m_PartyW, m_PartyY + 1 );
}

void CHL2MPMainMenu::SetPlayPage( bool play )
{
	m_PlayPage = play; OnTick(); InvalidateLayout( true );
	m_Buttons[play ? DUEL + m_SelectedMode : PLAY]->RequestFocus();
}

void CHL2MPMainMenu::OnTick()
{
	HL2MPParty().Tick(); HL2MPCoordinator().Tick();
	CHL2MPParty &party = HL2MPParty(); CHL2MPCoordinator &coordinator = HL2MPCoordinator();
	const bool grouped = party.Lobby().IsValid(), readyCheck = coordinator.NeedsConfirmation();
	const bool inGame = engine->IsInGame() && !engine->IsLevelMainMenuBackground();
	const char *activeMode = coordinator.Mode();
	if ( activeMode[0] ) m_SelectedMode = !Q_strcmp( activeMode, "casual" ) ? 2 : !Q_strcmp( activeMode, "team" ) ? 1 : 0;
	const bool matchmaking = activeMode[0] && coordinator.Busy();
	if ( matchmaking && !m_WasBusy ) { m_PlayPage = true; InvalidateLayout( true ); }
	m_WasBusy = matchmaking;
	int w, h; vgui::surface()->GetScreenSize( w, h );
	if ( w != m_ScreenWidth || h != m_ScreenHeight || grouped != m_GroupedLayout || inGame != m_InGameLayout )
	{
		m_ScreenWidth = w; m_ScreenHeight = h; m_GroupedLayout = grouped; m_InGameLayout = inGame; InvalidateLayout( true );
	}
	for ( int i = PLAY; i <= QUIT; ++i ) m_Buttons[i]->SetVisible( !m_PlayPage && ( i != RESUME || inGame ) );
	m_Buttons[BACK]->SetVisible( m_PlayPage ); m_Buttons[BACK]->SetEnabled( inGame || !coordinator.Busy() );
	m_Buttons[BACK]->SetText( inGame ? "RESUME GAME" : "<  BACK" );
	m_Subtitle->SetVisible( !m_PlayPage ); m_Tag->SetVisible( !m_PlayPage ); m_PlayHeading->SetVisible( m_PlayPage );
	m_ModeDescription->SetVisible( m_PlayPage ); m_QueueStatus->SetVisible( m_PlayPage );
	const bool canQueue = !party.Pending() && ( !grouped || ( party.IsOwner() && coordinator.PartySynchronized() ) ) && !coordinator.Busy();
	for ( int i = DUEL; i <= CASUAL; ++i )
	{
		m_Buttons[i]->SetVisible( m_PlayPage );
		m_Buttons[i]->SetEnabled( canQueue && ( i == CASUAL || party.MemberCount() <= ( i == DUEL ? 1 : 2 ) ) );
		m_Buttons[i]->SetModeSelected( i == DUEL + m_SelectedMode );
	}
	const char *descriptions[] = { "One opponent. Every frag counts. Classic weapons, ranked 1v1.",
		"Two teams of two. Queue with a friend and fight together.", "Drop in, warm up, or stay a while. Classic deathmatch without rankings." };
	m_ModeDescription->SetText( descriptions[m_SelectedMode] );
	m_Buttons[FIND]->SetVisible( m_PlayPage && !coordinator.Busy() );
	m_Buttons[FIND]->SetEnabled( canQueue && m_Buttons[DUEL + m_SelectedMode]->IsEnabled() );
	m_Buttons[ACCEPT]->SetVisible( m_PlayPage && ( readyCheck || coordinator.HasServer() ) );
	m_Buttons[ACCEPT]->SetText( coordinator.HasServer() ? "JOIN / RECONNECT" : "ACCEPT MATCH" );
	m_Buttons[CANCEL]->SetVisible( m_PlayPage && ( coordinator.Searching() || coordinator.HasReadyCheck() || coordinator.CanLeaveCasual() ) );
	m_Buttons[CANCEL]->SetText( coordinator.CanLeaveCasual() ? "LEAVE MATCH" : "CANCEL / DECLINE" );
	m_QueueStatus->SetText( coordinator.Status() );
	m_ResultHeading->SetText( coordinator.ResultHeading() ); m_ResultDetails->SetText( coordinator.ResultDetails() );
	m_Buttons[CREATE_PARTY]->SetVisible( !grouped && !party.Pending() ); m_Buttons[CREATE_PARTY]->SetEnabled( party.Available() && !coordinator.Busy() );
	m_Buttons[INVITE]->SetVisible( grouped ); m_Buttons[LEAVE]->SetVisible( grouped || party.Pending() );
	m_Buttons[LEAVE]->SetEnabled( !coordinator.Busy() ); m_Buttons[INVITE]->SetEnabled( !coordinator.Busy() );
	m_Buttons[READY]->SetVisible( grouped ); m_Buttons[READY]->SetEnabled( coordinator.PartySynchronized() && !coordinator.Busy() );
	m_Buttons[READY]->SetText( coordinator.PartyReady() ? "READY  /  UNREADY" : "READY TO PLAY" );
	m_Status->SetText( party.Available() ? ( grouped ? coordinator.PartyStatus() : party.Status() ) : "Steam is offline. The server browser is still available." );
	m_PartyHeading->SetText( grouped ? ( party.IsOwner() ? "YOUR PARTY  /  LEADER" : "YOUR PARTY" ) : "YOUR PARTY  /  SOLO" );
	for ( int i = 0; i < 4; ++i )
	{
		m_Members[i]->SetVisible( grouped || i == 0 );
		if ( i < party.MemberCount() )
		{
			const bool ready = coordinator.PartyMemberReady( party.Member( i ) );
			char text[256]; Q_snprintf( text, sizeof( text ), "%s  /  %s", ready ? "READY" : "WAITING", party.MemberName( i ) );
			m_Members[i]->SetText( text ); m_Members[i]->SetFgColor( ready ? Color( 155, 206, 159, 255 ) : Color( 215, 219, 221, 255 ) );
		}
		else
		{
			const char *name = i == 0 && !grouped && party.Available() ? steamapicontext->SteamFriends()->GetPersonaName() : i == 0 && !grouped ? "You" : "Open slot";
			m_Members[i]->SetText( name ); m_Members[i]->SetFgColor( Color( 195, 204, 207, 255 ) );
		}
	}
	if ( readyCheck && !m_WasReady ) m_Buttons[ACCEPT]->RequestFocus();
	m_WasReady = readyCheck;
}

void CHL2MPMainMenu::OnCommand( const char *command )
{
	if ( !Q_strcmp( command, "play" ) ) SetPlayPage( true );
	else if ( !Q_strcmp( command, "back" ) )
	{
		if ( engine->IsInGame() && !engine->IsLevelMainMenuBackground() ) m_GameUI->SendMainMenuCommand( "ResumeGame" );
		else SetPlayPage( false );
	}
	else if ( !Q_strncmp( command, "mode_", 5 ) )
	{
		if ( !Q_strcmp( command, "mode_duel" ) ) m_SelectedMode = 0;
		else if ( !Q_strcmp( command, "mode_team" ) ) m_SelectedMode = 1;
		else if ( !Q_strcmp( command, "mode_casual" ) ) m_SelectedMode = 2;
		OnTick();
	}
	else if ( !Q_strcmp( command, "find_match" ) )
	{
		const char *modes[] = { "duel", "team", "casual" }; HL2MPCoordinator().Queue( modes[m_SelectedMode] );
	}
	else if ( !Q_strcmp( command, "party_create" ) ) HL2MPParty().Create();
	else if ( !Q_strcmp( command, "party_invite" ) ) HL2MPParty().Invite();
	else if ( !Q_strcmp( command, "party_leave" ) ) HL2MPParty().Leave();
	else if ( !Q_strcmp( command, "party_ready" ) ) HL2MPCoordinator().TogglePartyReady();
	else if ( !Q_strcmp( command, "match_accept" ) )
	{
		if ( HL2MPCoordinator().HasServer() ) HL2MPCoordinator().Connect(); else HL2MPCoordinator().Confirm();
	}
	else if ( !Q_strcmp( command, "queue_cancel" ) ) HL2MPCoordinator().Cancel();
	else
	{
		const char *allowed[] = { "ResumeGame", "OpenServerBrowser", "OpenOptionsDialog", "Quit" };
		for ( int i = 0; i < ARRAYSIZE( allowed ); ++i )
			if ( !Q_strcmp( command, allowed[i] ) ) { m_GameUI->SendMainMenuCommand( allowed[i] ); return; }
		BaseClass::OnCommand( command );
	}
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
	delete s_Menu; s_Menu = NULL;
}
