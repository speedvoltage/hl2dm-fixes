#include "cbase.h"
#include "hl2mp_vote.h"
#include "hl2mp_vote_rules.h"
#include "hl2mp_messages.h"
#include "hl2mp_menucontrols.h"
#include "hl2mp_hud_customization.h"
#include "usermessages.h"
#include "iclientmode.h"
#include "ienginevgui.h"
#include "inputsystem/iinputsystem.h"
#include "vgui/IVGui.h"
#include "vgui/IInput.h"
#include "vgui_controls/EditablePanel.h"
#include "vgui_controls/Label.h"
#include "game/client/iviewport.h"
#include "tier0/memdbgon.h"
using namespace HL2MPVoteRules;
namespace
{
static ConVar cl_vote_sounds( "cl_vote_sounds", "1", FCVAR_ARCHIVE, "Play vote notifications.", true, 0, true, 1 );
static ConVar cl_vote_animations( "cl_vote_animations", "1", FCVAR_ARCHIVE, "Animate vote panels.", true, 0, true, 1 );
static ConVar cl_vote_bindings_initialized( "cl_vote_bindings_initialized", "0", FCVAR_ARCHIVE, "Vote shortcuts initialized." );
struct VoteState { int generation, phase, eligible, yes, no, choice, winner, callerUser, targetUser, mapCount; bool canVote; double deadline; char caller[64], target[64], maps[MAX_MAPS][64]; int counts[MAX_MAPS]; } s_State = {};
void Send( const char *action, int value = -1 )
{
    if ( !engine->IsInGame() || engine->IsLevelMainMenuBackground() || engine->IsPlayingDemo() ) return;
    KeyValues *kv = new KeyValues( "HL2MPVote", "action", action ); kv->SetInt( "generation", s_State.generation ); kv->SetInt( !Q_strcmp( action, "kick" ) ? "userid" : "choice", value ); engine->ServerCmdKeyValues( kv );
}
void Sound( const char *name ) { if ( cl_vote_sounds.GetBool() ) vgui::surface()->PlaySound( name ); }
const char *Key( const char *command ) { const char *k = engine->Key_LookupBinding( command ); return k && *k ? HL2MPKeyName( k ) : HL2MPText( "#HL2MP_UI_d16f3e00e8e4" ); }
class CVotePanel : public vgui::EditablePanel
{
    DECLARE_CLASS_SIMPLE( CVotePanel, vgui::EditablePanel );
public:
    CVotePanel() : BaseClass( g_pClientMode->GetViewport(), "VotePanel" ), m_LastPhase( NONE ), m_Started( 0 ), m_NextPoll( 0 ), m_FontTall( 0 ), m_Preview( false ), m_PreviousModal( NULL )
    {
        MakePopup( false ); SetZPos( 250 ); SetVisible( false ); SetMouseInputEnabled( false ); SetKeyBoardInputEnabled( false ); SetCursor( vgui::dc_arrow );
        m_Font = m_Title = vgui::INVALID_FONT;
        m_Close = new CHL2MPSettingsButton( this, "X", "close_preview" ); m_Close->ServerStyle(); m_Close->SetVisible( false );
        m_Body = new vgui::Label( this, "VoteQuestion", "" ); m_Body->SetWrap( true ); m_Body->SetContentAlignment( vgui::Label::a_northwest ); m_Body->SetMouseInputEnabled( false );
        for ( int i = 0; i < MAX_MAPS; ++i ) { m_Maps[i] = new CHL2MPSettingsButton( this, "", VarArgs( "map_%d", i ) ); m_Maps[i]->ServerStyle(); m_Maps[i]->SetMouseInputEnabled( true ); }
        vgui::ivgui()->AddTickSignal( GetVPanel(), 50 );
    }
    void OnTick() OVERRIDE
    {
        const double now = Plat_FloatTime();
        if ( m_Preview )
        {
            if ( now >= m_PreviewState.deadline ) m_PreviewState.deadline = now + KICK_SECONDS;
            SetVisible( true ); MoveToFront(); InvalidateLayout( true ); return;
        }
        if ( !cl_vote_bindings_initialized.GetBool() && now > 5 )
        {
            const char *commands[] = { "vote_yes", "vote_no" }; const ButtonCode_t keys[] = { KEY_F1, KEY_F2 };
            for ( int i = 0; i < 2; ++i )
            {
                const char *existing = engine->Key_BindingForKey( keys[i] );
                if ( !engine->Key_LookupBinding( commands[i] ) && ( !existing || !*existing || ( i == 1 && !Q_strcmp( existing, "changeteam" ) ) ) )
                    engine->ClientCmd_Unrestricted( i ? "bind F2 vote_no" : "bind F1 vote_yes" );
            }
            cl_vote_bindings_initialized.SetValue( 1 );
        }
        if ( !engine->IsInGame() || engine->IsLevelMainMenuBackground() ) { if ( State().phase ) HL2MPVoteUI_Reset(); SetVisible( false ); return; }
        if ( now >= m_NextPoll ) { Send( "status" ); m_NextPoll = now + 3; }
        bool map = State().phase >= MAP_WAIT;
        SetVisible( State().phase != NONE && State().phase != MAP_WAIT && !enginevgui->IsGameUIVisible() && !engine->IsDrawingLoadingImage() );
        SetMouseInputEnabled( map && IsVisible() );
        if ( State().phase != m_LastPhase )
        {
            m_LastPhase = State().phase; m_Started = now;
            if ( State().phase == KICK || State().phase == MAP_BALLOT ) Sound( "hl2mp/vote_start.wav" );
            else if ( State().phase == KICK_PASSED || State().phase == MAP_RESULT ) Sound( "hl2mp/vote_pass.wav" );
            else if ( State().phase == KICK_FAILED ) Sound( "hl2mp/vote_fail.wav" );
        }
        if ( !IsVisible() ) return;
        if ( map ) { gViewPortInterface->ShowPanel( PANEL_SCOREBOARD, false ); MoveToFront(); }
        InvalidateLayout( true );
    }
    void PerformLayout() OVERRIDE
    {
        int sw, sh; vgui::surface()->GetScreenSize( sw, sh ); float scale = MIN( sw / 1280.0f, sh / 720.0f );
        const bool map = State().phase >= MAP_WAIT;
        int x, y, w, h;
        if ( map ) { w = int( sw * .76f ); h = MIN( sh - 24, int( 530 * scale ) ); x = ( sw - w ) / 2; y = ( sh - h ) / 2; }
        else { HL2MPHudBounds( HUD_VOTE, sw, sh, x, y, w, h ); w = MAX( int( 320 * scale ), w ); h = MAX( int( 210 * scale ), h ); x = MIN( x, sw - w ); y = MIN( y, sh - h ); }
        if ( m_Preview ) { x = ( sw - w ) / 2; y = ( sh - h ) / 2; }
        SetBounds( x, y, w, h );
        m_Close->SetVisible( m_Preview ); m_Close->SetBounds( w - 34, 6, 28, 28 );
        float textScale = map ? 1.0f : HL2MPHudStyle( HUD_VOTE ).textScale;
        const int fontTall = MAX( 11, int( 17 * scale * textScale ) );
        m_FontTall = fontTall; m_Font = HL2MPHudFont( HUD_VOTE, fontTall ); m_Title = HL2MPHudFont( HUD_VOTE, int( fontTall * 1.3f ), true );
        SetAlpha( !m_Preview && cl_vote_animations.GetBool() ? int( 255 * clamp( ( Plat_FloatTime() - m_Started ) / .18, 0.0, 1.0 ) ) : 255 );
        m_Body->SetFont( m_Title ); m_Body->SetFgColor( Color( 238, 238, 232, 255 ) );
        m_Body->SetBounds( int( 18 * scale ), int( 50 * scale ), w - int( 36 * scale ), map ? int( 35 * scale ) : h / 3 );
        m_Body->SetText( map ? HL2MPText( State().phase == MAP_RESULT ? "#HL2MP_VoteNextMap" : "#HL2MP_VoteChooseMap" ) : HL2MPFormat( "Kick player: %s?", State().target ) );
        m_Body->GetTextImage()->SetSize( m_Body->GetWide(), m_Body->GetTall() ); m_Body->GetTextImage()->RecalculateNewLinePositions();
        for ( int i = 0; i < MAX_MAPS; ++i )
        {
            m_Maps[i]->SetVisible( map && i < State().mapCount ); if ( !m_Maps[i]->IsVisible() ) continue;
            m_Maps[i]->SetBounds( int( 18 * scale ), int( ( 98 + i * 66 ) * scale ), w - int( 36 * scale ), int( 56 * scale ) );
            m_Maps[i]->SetFont( m_Font ); m_Maps[i]->SetText( HL2MPFormat( "%s   /   %d votes", State().maps[i], State().counts[i] ) );
            m_Maps[i]->Active( State().phase == MAP_RESULT ? State().winner == i : State().choice == i );
            m_Maps[i]->SetEnabled( State().phase == MAP_RESULT || ( State().phase == MAP_BALLOT && State().canVote ) );
        }
    }
    void Text( vgui::HFont font, const char *text, int x, int y, Color color )
    { vgui::surface()->DrawSetTextFont( font ); vgui::surface()->DrawSetTextColor( color ); vgui::surface()->DrawSetTextPos( x, y ); const wchar_t *wide = HL2MPWideText( text ); vgui::surface()->DrawPrintText( wide, V_wcslen( wide ) ); }
    void PaintBackground() OVERRIDE
    {
        Color bg = State().phase >= MAP_WAIT ? Color( 25, 26, 27, 240 ) : HL2MPHudStyle( HUD_VOTE ).background;
        HL2MPServerTheme::Rounded( 0, 0, GetWide(), GetTall(), bg );
        const bool map = State().phase >= MAP_WAIT; const float scale = GetTall() / ( map ? 530.0f : 240.0f );
        int pad = MAX( 10, int( 18 * scale ) ), seconds = MAX( 0, int( ceil( State().deadline - Plat_FloatTime() ) ) );
        const Color orange = HL2MPHudStyle( HUD_VOTE ).foreground, white( 231, 231, 228, 255 ), green( 118, 204, 113, 255 ), red( 240, 92, 85, 255 );
        Text( m_Font, map ? HL2MPText( "#HL2MP_VoteMapTitle" ) : HL2MPFormat( "Vote by: %s", State().caller ), pad, pad, orange );
        const int foot = GetTall() - pad - m_FontTall;
        if ( map )
            Text( m_Font, State().phase == MAP_RESULT ? ( State().winner < 0 ? HL2MPFormat( "No votes. Following the mapcycle in %d seconds.", seconds ) : HL2MPFormat( "Changing map in %d seconds.", seconds ) ) : HL2MPFormat( "Select a map. %d seconds remaining.", seconds ), pad, foot, white );
        else
        {
            int row = GetTall() * 55 / 100;
            Text( m_Title, HL2MPFormat( "YES  %d", State().yes ), pad, row, green ); Text( m_Title, HL2MPFormat( "NO  %d", State().no ), GetWide() / 2, row, red );
            const char *status = State().phase == KICK_PASSED ? "#HL2MP_VotePassed" : State().phase == KICK_FAILED ? "#HL2MP_VoteFailed" : State().choice == 0 ? "#HL2MP_VoteYouYes" : State().choice == 1 ? "#HL2MP_VoteYouNo" : !State().canVote ? "#HL2MP_VoteWatching" : NULL;
            if ( status ) Text( m_Font, status, pad, foot, white );
            else Text( m_Font, HL2MPFormat( "%s: YES    %s: NO", Key( "vote_yes" ), Key( "vote_no" ) ), pad, foot, white );
            if ( State().phase == KICK ) Text( m_Font, HL2MPFormat( "%d needed / %ds", RequiredYes( State().eligible ), seconds ), pad, row + int( m_FontTall * 1.6f ), white );
        }
        const float duration = State().phase == KICK ? KICK_SECONDS : State().phase == MAP_BALLOT ? MAP_SECONDS : RESULT_SECONDS;
        float progress = clamp( float( ( State().deadline - Plat_FloatTime() ) / duration ), 0.0f, 1.0f );
        vgui::surface()->DrawSetColor( orange ); vgui::surface()->DrawFilledRect( pad, GetTall() - 4, pad + int( ( GetWide() - pad * 2 ) * progress ), GetTall() - 2 );
    }
    void OnCommand( const char *command ) OVERRIDE
    {
        if ( m_Preview ) { ClosePreview(); return; }
        if ( !Q_strncmp( command, "map_", 4 ) && State().phase == MAP_BALLOT && State().canVote )
        { int choice = atoi( command + 4 ); if ( choice >= 0 && choice < State().mapCount && State().choice != choice ) { Send( "cast", choice ); Sound( "hl2mp/vote_cast.wav" ); } return; }
        BaseClass::OnCommand( command );
    }
    const VoteState &State() const { return m_Preview ? m_PreviewState : s_State; }
    bool IsPreview() const { return m_Preview; }
    void Preview()
    {
        if ( m_Preview ) { ClosePreview(); return; }
        memset( &m_PreviewState, 0, sizeof( m_PreviewState ) ); m_PreviewState.phase = KICK; m_PreviewState.eligible = 6; m_PreviewState.yes = 2; m_PreviewState.no = 1;
        m_PreviewState.canVote = true; m_PreviewState.choice = -1; m_PreviewState.deadline = Plat_FloatTime() + KICK_SECONDS;
        Q_strncpy( m_PreviewState.caller, HL2MPText( "#HL2MP_HudVotePreview" ), sizeof( m_PreviewState.caller ) ); Q_strncpy( m_PreviewState.target, HL2MPText( "#HL2MP_VoteExamplePlayer" ), sizeof( m_PreviewState.target ) );
        m_PreviousModal = vgui::input()->GetAppModalSurface(); m_Preview = true; SetParent( enginevgui->GetPanel( PANEL_GAMEUIDLL ) ); SetZPos( 900 ); SetMouseInputEnabled( true ); SetKeyBoardInputEnabled( true );
        SetVisible( true ); InvalidateLayout( true ); MoveToFront(); RequestFocus(); vgui::input()->SetAppModalSurface( GetVPanel() );
    }
    void ClosePreview()
    {
        if ( !m_Preview ) return; m_Preview = false; SetZPos( 250 ); SetMouseInputEnabled( false ); SetKeyBoardInputEnabled( false ); SetVisible( false );
        if ( vgui::input()->GetAppModalSurface() == GetVPanel() ) { if ( m_PreviousModal ) { vgui::input()->SetAppModalSurface( m_PreviousModal ); vgui::ipanel()->RequestFocus( m_PreviousModal ); } else vgui::input()->ReleaseAppModalSurface(); }
        m_PreviousModal = NULL; SetParent( g_pClientMode->GetViewport() );
    }
    void OnKeyCodeTyped( vgui::KeyCode code ) OVERRIDE
    {
        if ( !m_Preview ) { BaseClass::OnKeyCodeTyped( code ); return; }
        if ( code == KEY_ESCAPE || code == KEY_ENTER ) ClosePreview();
        else if ( m_PreviewState.choice < 0 )
        {
            const char *binding = engine->Key_BindingForKey( (ButtonCode_t)code );
            if ( binding && ( !Q_strcmp( binding, "vote_yes" ) || !Q_strcmp( binding, "vote_no" ) ) ) { m_PreviewState.choice = !Q_strcmp( binding, "vote_no" ); if ( m_PreviewState.choice ) ++m_PreviewState.no; else ++m_PreviewState.yes; }
        }
    }
private:
    bool m_Preview; VoteState m_PreviewState; vgui::VPANEL m_PreviousModal; CHL2MPSettingsButton *m_Close;
    int m_LastPhase; double m_Started, m_NextPoll; int m_FontTall; vgui::HFont m_Font, m_Title; vgui::Label *m_Body; CHL2MPSettingsButton *m_Maps[MAX_MAPS];
};
vgui::DHANDLE<CVotePanel> s_Panel;
void StateMessage( bf_read &r )
{
    VoteState s = s_State;
    int generation = r.ReadLong(); if ( generation != s.generation ) memset( &s, 0, sizeof( s ) ); s.generation = generation;
    s.phase = r.ReadByte(); float remaining = r.ReadFloat(); if ( !IsFinite( remaining ) ) return;
    s.deadline = Plat_FloatTime() + clamp( remaining, 0.0f, 60.0f );
    s.eligible = r.ReadShort(); s.yes = r.ReadShort(); s.no = r.ReadShort(); s.canVote = r.ReadByte() != 0; s.choice = r.ReadChar(); s.winner = r.ReadChar();
    s.callerUser = r.ReadLong(); s.targetUser = r.ReadLong(); r.ReadString( s.caller, sizeof( s.caller ) ); r.ReadString( s.target, sizeof( s.target ) ); s.mapCount = r.ReadByte();
    if ( r.IsOverflowed() || s.phase < NONE || s.phase > MAP_RESULT || s.mapCount > MAX_MAPS || s.eligible < 0 || s.eligible > MAX_PLAYERS ) return;
    s_State = s;
}
void MapMessage( bf_read &r )
{
    int generation = r.ReadLong(), index = r.ReadByte(), count = r.ReadShort(); char map[64]; r.ReadString( map, sizeof( map ) );
    if ( r.IsOverflowed() || generation != s_State.generation || index >= s_State.mapCount || index >= MAX_MAPS || count < 0 || count > MAX_PLAYERS ) return;
    Q_strncpy( s_State.maps[index], map, sizeof( map ) ); s_State.counts[index] = count;
}
void Cast( int choice )
{
    if ( ( s_Panel && s_Panel->IsPreview() ) || s_State.phase != KICK || !s_State.canVote || s_State.choice >= 0 ) return;
    Send( "cast", choice ); Sound( "hl2mp/vote_cast.wav" );
}
}
void HL2MPVoteUI_Init() { usermessages->HookMessage( "HL2MPVote", StateMessage ); usermessages->HookMessage( "HL2MPVoteMap", MapMessage ); if ( !s_Panel ) s_Panel = new CVotePanel; }
void HL2MPVoteUI_Preview() { if ( s_Panel ) s_Panel->Preview(); }
void HL2MPVoteUI_ClosePreview() { if ( s_Panel ) s_Panel->ClosePreview(); }
void HL2MPVoteUI_Reset() { memset( &s_State, 0, sizeof( s_State ) ); }
void HL2MPVoteUI_CallKick( int userid ) { Send( "kick", userid ); }
bool HL2MPVoteUI_IsMapVote() { return s_State.phase == MAP_BALLOT || s_State.phase == MAP_RESULT; }
CON_COMMAND_F( vote_yes, "Vote yes in the current player vote.", FCVAR_CLIENTDLL ) { Cast( 0 ); }
CON_COMMAND_F( vote_no, "Vote no in the current player vote.", FCVAR_CLIENTDLL ) { Cast( 1 ); }
CON_COMMAND_F( cl_vote_status, "Show the current vote and local ballot state.", FCVAR_CLIENTDLL )
{
    Msg( "Vote: phase=%d generation=%d eligible=%d yes=%d no=%d can_vote=%d choice=%d maps=%d winner=%d remaining=%.1f\n", s_State.phase, s_State.generation, s_State.eligible, s_State.yes, s_State.no, s_State.canVote, s_State.choice, s_State.mapCount, s_State.winner, MAX( 0.0, s_State.deadline - Plat_FloatTime() ) );
    for ( int i = 0; i < s_State.mapCount; ++i ) Msg( "  %d: %s (%d votes)\n", i, s_State.maps[i], s_State.counts[i] );
}
