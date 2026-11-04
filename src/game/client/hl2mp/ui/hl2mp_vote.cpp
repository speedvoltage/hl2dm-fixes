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
    CVotePanel( vgui::Panel *owner = NULL, bool previewOnly = false, bool editorSample = false ) : BaseClass( owner ? owner : g_pClientMode->GetViewport(), "VotePanel" ), m_LastPhase( NONE ), m_Started( 0 ), m_NextPoll( 0 ), m_FontTall( 0 ), m_Preview( false ), m_PreviewOnly( previewOnly ), m_EditorSample( editorSample ), m_PreviewGameUI(false), m_LastYes(0), m_LastNo(0), m_LastGeneration(-1), m_LastChoice(-1), m_YesFlash(0), m_NoFlash(0)
    {
        // An editor preview must be a child of its modal owner. A sibling popup
        // can capture input while the engine paints it behind the editor.
        m_PreviewOwner = owner;
        if ( previewOnly && !owner ) SetParent( enginevgui->GetPanel( PANEL_GAMEUIDLL ) );
        if ( !owner ) MakePopup( false );
        SetZPos( previewOnly ? 1000 : 250 ); SetVisible( false ); SetMouseInputEnabled( previewOnly ); SetKeyBoardInputEnabled( previewOnly ); SetCursor( vgui::dc_arrow );
        m_Font = m_Title = vgui::INVALID_FONT;
        m_Close = new CHL2MPSettingsButton( this, "X", "close_preview" ); m_Close->ServerStyle(); m_Close->SetVisible( false ); m_Close->SetMouseInputEnabled(true);m_Close->SetKeyBoardInputEnabled(true);
        const char *testLabels[]={"#HL2MP_VoteTestAgain","#HL2MP_VoteTestPass","#HL2MP_VoteTestFail"}; const char *testCmds[]={"preview_restart","preview_pass","preview_fail"};
        for(int i=0;i<3;++i) { m_Test[i]=new CHL2MPSettingsButton(this,testLabels[i],testCmds[i]);m_Test[i]->ServerStyle();m_Test[i]->SetVisible(false);m_Test[i]->SetMouseInputEnabled(true);m_Test[i]->SetKeyBoardInputEnabled(true); }
        m_Header=new vgui::Label(this,"VoteHeader",""); m_Header->SetContentAlignment(vgui::Label::a_west);m_Header->SetMouseInputEnabled(false);
        m_Body = new vgui::Label( this, "VoteQuestion", "" ); m_Body->SetWrap( true ); m_Body->SetContentAlignment( vgui::Label::a_northwest ); m_Body->SetMouseInputEnabled( false );
        for ( int i = 0; i < MAX_MAPS; ++i ) { m_Maps[i] = new CHL2MPSettingsButton( this, "", VarArgs( "map_%d", i ) ); m_Maps[i]->ServerStyle(); m_Maps[i]->SetMouseInputEnabled( true ); }
        if ( editorSample )
        {
            // A passive child of the editor: never acquires focus, sends votes,
            // changes the real vote state or plays notification sounds.
            SetZPos( -1 ); SetMouseInputEnabled( false ); SetKeyBoardInputEnabled( false );
            memset( &m_PreviewState, 0, sizeof(m_PreviewState) );
            m_PreviewState.phase=KICK; m_PreviewState.eligible=6; m_PreviewState.yes=2; m_PreviewState.no=1;
            m_PreviewState.choice=-1; m_PreviewState.canVote=true;
            Q_strncpy(m_PreviewState.caller,HL2MPText("#HL2MP_HudVotePreview"),sizeof(m_PreviewState.caller));
            Q_strncpy(m_PreviewState.target,HL2MPText("#HL2MP_VoteExamplePlayer"),sizeof(m_PreviewState.target));
        }
        vgui::ivgui()->AddTickSignal( GetVPanel(), 50 );
    }
    ~CVotePanel()
    {
        vgui::ivgui()->RemoveTickSignal( GetVPanel() );
        RestoreModal();
    }
    void OnTick() OVERRIDE
    {
        const double now = Plat_FloatTime();
        if ( m_EditorSample )
        {
            SetVisible( HL2MPHudEditor_IsLive() );
            if ( IsVisible() ) { m_PreviewState.deadline=now+18; InvalidateLayout(true); }
            return;
        }
        if ( m_Preview )
        {
            if ( m_PreviewOwner && !m_PreviewOwner->IsVisible() ) { ClosePreview(); return; }
            if (now>=m_PreviewState.deadline && m_PreviewState.phase==KICK) Result(false);
            Feedback(); SetVisible( true ); Repaint(); return;
        }
        if ( m_PreviewOnly ) return;
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
        SetVisible( State().phase != NONE && State().phase != MAP_WAIT && !HL2MPHudEditor_IsLive() && !enginevgui->IsGameUIVisible() && !engine->IsDrawingLoadingImage() );
        SetMouseInputEnabled( map && IsVisible() );
        Feedback();
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
        else { HL2MPHudBounds( HUD_VOTE, sw, sh, x, y, w, h ); w = MIN(sw-16,MAX(320,MAX( int( 400 * scale ), w ))); h = MIN(sh-16,MAX(210,MAX( int( 240 * scale ), h ))); x = MIN( x, sw - w ); y = MIN( y, sh - h ); }
        if ( m_Preview ) { x = ( sw - w ) / 2; y = ( sh - h ) / 2; }
        int contentH=h; if(m_Preview) { h=MIN(sh-16,h+48); y=(sh-h)/2; } m_ContentH=contentH;
        SetBounds( x, y, w, h );
        m_Close->SetVisible( m_Preview ); m_Close->SetBounds( w - 32, 6, 26, 26 );
        for(int i=0;i<3;++i) { m_Test[i]->SetVisible(m_Preview);m_Test[i]->SetBounds(10+i*(w-20)/3,contentH+8,(w-20)/3-4,30); }
        float textScale = map ? 1.0f : HL2MPHudStyle( HUD_VOTE ).textScale;
        const int fontTall = MAX( 11, MIN(int(17*scale*textScale),int(contentH*.09f)) );
        m_FontTall = fontTall; m_Font = HL2MPHudFont( HUD_VOTE, fontTall ); m_Title = HL2MPHudFont( HUD_VOTE, int( fontTall * 1.3f ), true );
        SetAlpha( !m_Preview && !m_EditorSample && cl_vote_animations.GetBool() ? int( 255 * clamp( ( Plat_FloatTime() - m_Started ) / .18, 0.0, 1.0 ) ) : 255 );
        m_Body->SetFont( m_Title ); m_Body->SetFgColor( Color( 238, 238, 232, 255 ) );
        const int pad=MAX(10,int(16*scale)), header=map?int(40*scale):MAX(36,int(contentH*.21f)), tile=MAX(44,int(contentH*.29f));
        m_Header->SetBounds(map?pad:header+4,4,w-(map?pad:header+4)-(m_Preview?38:pad),header-8);m_Header->SetFont(m_Font);
        m_Header->SetFgColor(State().phase==KICK_PASSED?Color(118,204,113,255):State().phase==KICK_FAILED?Color(240,92,85,255):Color(215,217,214,255));
        m_Header->SetText(map?HL2MPText("#HL2MP_VoteMapTitle"):State().phase==KICK_PASSED?HL2MPText("#HL2MP_VotePassed"):State().phase==KICK_FAILED?HL2MPText("#HL2MP_VoteFailed"):HL2MPFormat("Vote by: %s",State().caller));
        m_Body->SetBounds( pad, map?int(50*scale):header+pad, map?w-pad*2:w-tile-int(38*scale)-pad*3, map ? int(35*scale) : contentH-header-pad*2-m_FontTall*3 );
        for(int i=0;i<3;++i)m_Test[i]->SetFont(m_Font);
        m_Body->SetText( map ? HL2MPText( State().phase == MAP_RESULT ? "#HL2MP_VoteNextMap" : "#HL2MP_VoteChooseMap" ) : State().phase==KICK_PASSED ? HL2MPFormat("Vote passed to remove %s.",State().target) : State().phase==KICK_FAILED ? HL2MPFormat("%s stays in the game.",State().target) : HL2MPFormat( "Kick player: %s?", State().target ) );
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
    void Line(int x1,int y1,int x2,int y2,int thick,Color color)
    {
        int n=MAX(abs(x2-x1),abs(y2-y1));vgui::surface()->DrawSetColor(color);
        for(int i=0;i<=n;++i) { int x=x1+(x2-x1)*i/MAX(1,n),y=y1+(y2-y1)*i/MAX(1,n);vgui::surface()->DrawFilledRect(x-thick/2,y-thick/2,x+(thick+1)/2,y+(thick+1)/2); }
    }
    void Mark(int x,int y,int size,bool yes,Color color)
    {
        int t=MAX(2,size/8);
        if(yes) { Line(x+size/6,y+size/2,x+size*2/5,y+size*3/4,t,color);Line(x+size*2/5,y+size*3/4,x+size*5/6,y+size/4,t,color); }
        else { Line(x+size/4,y+size/4,x+size*3/4,y+size*3/4,t,color);Line(x+size*3/4,y+size/4,x+size/4,y+size*3/4,t,color); }
    }
    void KickIcon(int x,int y,int size,Color color)
    {
        HL2MPServerTheme::Rounded(x+size/3,y,size/3,size/3,color,size/6);
        HL2MPServerTheme::Rounded(x+size/6,y+size*2/5,size*2/3,size/2,color,size/5);
        Line(x,y,x+size,y+size,MAX(4,size/5),Color(30,32,33,255));Line(x,y,x+size,y+size,MAX(2,size/9),color);
    }
    float Flash(double began) const
    {
        if(!cl_vote_animations.GetBool())return 0; float age=float(Plat_FloatTime()-began); return age>=0&&age<.65f ? (1-age/.65f)*(.5f+.5f*cosf(age*24)) : 0;
    }
    void PaintBackground() OVERRIDE
    {
        const bool map=State().phase>=MAP_WAIT; Color bg=map?Color(25,26,27,240):HL2MPHudStyle(HUD_VOTE).background;
        HL2MPServerTheme::Rounded(0,0,GetWide(),GetTall(),bg);
        const float scale=MIN(ScreenWidth()/1280.f,ScreenHeight()/720.f); int pad=MAX(10,int(16*scale)),header=map?int(40*scale):MAX(36,int(m_ContentH*.21f));
        const Color white(231,231,228,255),muted(178,181,178,255),green(118,204,113,255),red(240,92,85,255),accent=HL2MPHudStyle(HUD_VOTE).foreground;
        HL2MPServerTheme::Rounded(0,0,GetWide(),header,Color(12,14,15,110));
        int foot=m_ContentH-pad-m_FontTall, seconds=MAX(0,int(ceil(State().deadline-Plat_FloatTime())));
        if(map) Text(m_Font,State().phase==MAP_RESULT?(State().winner<0?HL2MPFormat("No votes. Following the mapcycle in %d seconds.",seconds):HL2MPFormat("Changing map in %d seconds.",seconds)):HL2MPFormat("Select a map. %d seconds remaining.",seconds),pad,foot,white);
        else
        {
            bool result=State().phase==KICK_PASSED||State().phase==KICK_FAILED;
            if(result) Mark(pad,8,header-16,State().phase==KICK_PASSED,State().phase==KICK_PASSED?green:red); else KickIcon(pad,8,header-16,white);
            int tile=MAX(44,int(m_ContentH*.29f)),countW=MAX(34,int(38*scale)),x=GetWide()-pad-tile-countW;
            for(int i=0;i<2;++i)
            {
                int y=header+pad+i*(tile+MAX(6,pad/2));Color c=i?red:green;
                bool chosen=State().choice==i; float flash=Flash(i?m_NoFlash:m_YesFlash);
                HL2MPServerTheme::Rounded(x,y,tile+countW,tile,Color(50,53,54,235));
                Color border(c.r(),c.g(),c.b(),int(100+155*MAX(chosen?1.f:0.f,flash))); HL2MPServerTheme::Rounded(x,y,tile,tile,border);
                HL2MPServerTheme::Rounded(x+2,y+2,tile-4,tile-4,Color(int(c.r()*(.18f+.22f*flash)),int(c.g()*(.18f+.22f*flash)),int(c.b()*(.18f+.22f*flash)),255));Mark(x+4,y+4,tile-8,i==0,c);
                Text(m_Title,VarArgs("%d",i?State().no:State().yes),x+tile+6,y+4,c);
                Text(m_Font,Key(i?"vote_no":"vote_yes"),x+tile+4,y+tile-m_FontTall-5,chosen?white:muted);
            }
            const char *status=State().choice==0?"#HL2MP_VoteYouYes":State().choice==1?"#HL2MP_VoteYouNo":result?"#HL2MP_VoteComplete":!State().canVote?"#HL2MP_VoteWatching":"#HL2MP_VoteChooseHint";
            Text(m_Font,status,pad,foot,State().choice==0?green:State().choice==1?red:white);
            if(!result)Text(m_Font,HL2MPFormat("%d needed / %ds",RequiredYes(State().eligible),seconds),pad,foot-m_FontTall-7,muted);
            if(result && Flash(m_Started)>0) { Color c=State().phase==KICK_PASSED?green:red; c[3]=int(60*Flash(m_Started));HL2MPServerTheme::Rounded(0,0,GetWide(),m_ContentH,c); }
        }
        const float duration=State().phase==KICK?KICK_SECONDS:State().phase==MAP_BALLOT?MAP_SECONDS:RESULT_SECONDS;
        float progress=clamp(float((State().deadline-Plat_FloatTime())/duration),0.f,1.f);
        vgui::surface()->DrawSetColor(accent);vgui::surface()->DrawFilledRect(pad,m_ContentH-4,pad+int((GetWide()-pad*2)*progress),m_ContentH-2);
    }
    void Feedback()
    {
        const VoteState &s=State(); double now=Plat_FloatTime(); bool fresh=s.generation!=m_LastGeneration;
        if(fresh || s.phase!=m_LastPhase)
        {
            m_Started=now;
            if(s.phase==KICK||s.phase==MAP_BALLOT)Sound("hl2mp/vote_start.wav");
            else if(s.phase==KICK_PASSED||s.phase==MAP_RESULT)Sound("hl2mp/vote_pass.wav");
            else if(s.phase==KICK_FAILED)Sound("hl2mp/vote_fail.wav");
        }
        if(!fresh && s.yes!=m_LastYes)m_YesFlash=now;
        if(!fresh && s.no!=m_LastNo)m_NoFlash=now;
        if(!fresh && s.choice!=m_LastChoice && s.choice>=0 && (s.phase==KICK || m_LastPhase==KICK))Sound(s.choice?"hl2mp/vote_no.wav":"hl2mp/vote_yes.wav");
        m_LastGeneration=s.generation;m_LastPhase=s.phase;m_LastYes=s.yes;m_LastNo=s.no;m_LastChoice=s.choice;
    }
    void Result(bool pass)
    {
        if(!m_Preview)return;
        m_LastPhase=KICK;m_PreviewState.phase=pass?KICK_PASSED:KICK_FAILED;m_PreviewState.yes=pass?4:2;m_PreviewState.no=pass?2:4;m_PreviewState.deadline=Plat_FloatTime()+RESULT_SECONDS; Feedback();InvalidateLayout(true);
    }
    void PreviewChoice(int choice)
    {
        if(!m_Preview || m_PreviewState.phase!=KICK || m_PreviewState.choice>=0)return;
        m_PreviewState.choice=choice;if(choice)++m_PreviewState.no;else ++m_PreviewState.yes;Feedback();InvalidateLayout(true);
    }
    void OnCommand( const char *command ) OVERRIDE
    {
        if ( m_EditorSample ) return;
        if(m_Preview)
        {
            if(!Q_strcmp(command,"preview_pass"))Result(true);else if(!Q_strcmp(command,"preview_fail"))Result(false);
            else if(!Q_strcmp(command,"preview_restart"))ResetPreview();else ClosePreview();return;
        }
        if ( !Q_strncmp( command, "map_", 4 ) && State().phase == MAP_BALLOT && State().canVote )
        { int choice = atoi( command + 4 ); if ( choice >= 0 && choice < State().mapCount && State().choice != choice ) { Send( "cast", choice ); Sound( "hl2mp/vote_cast.wav" ); } return; }
        BaseClass::OnCommand( command );
    }
    const VoteState &State() const { return m_Preview || m_EditorSample ? m_PreviewState : s_State; }
    bool IsPreview() const { return m_Preview; }
    void ResetPreview()
    {
        memset(&m_PreviewState,0,sizeof(m_PreviewState));m_PreviewState.phase=KICK;m_PreviewState.generation=-2;m_PreviewState.eligible=6;m_PreviewState.yes=2;m_PreviewState.no=1;
        m_PreviewState.canVote=true;m_PreviewState.choice=-1;m_PreviewState.deadline=Plat_FloatTime()+KICK_SECONDS;
        Q_strncpy(m_PreviewState.caller,HL2MPText("#HL2MP_HudVotePreview"),sizeof(m_PreviewState.caller));
        player_info_t player;const char *name=engine->GetPlayerInfo(engine->GetLocalPlayer(),&player)?player.name:ConVarRef("name").GetString();
        Q_strncpy(m_PreviewState.target,name&&*name?name:HL2MPText("#HL2MP_VoteExamplePlayer"),sizeof(m_PreviewState.target));
        m_LastGeneration=-1;Feedback();InvalidateLayout(true);
    }
    void Preview()
    {
        if ( !m_PreviewOnly || m_Preview ) return;
        m_PreviousModal=vgui::input()->GetAppModalSurface();m_PreviewGameUI=enginevgui->IsGameUIVisible();m_Preview=true;
        if(!m_PreviewGameUI)engine->ClientCmd_Unrestricted("gameui_activate;gameui_preventescape");
        ResetPreview(); SetVisible( true ); InvalidateLayout( true );
        vgui::input()->SetAppModalSurface( GetVPanel() ); RequestFocus(); MoveToFront();
    }
    void RestoreModal()
    {
        vgui::VPANEL capture = vgui::input()->GetMouseCapture();
        if ( capture && ( capture == GetVPanel() || vgui::ipanel()->HasParent( capture, GetVPanel() ) ) )
            vgui::input()->SetMouseCapture( NULL );
        if ( vgui::input()->GetAppModalSurface() == GetVPanel() )
        {
            vgui::VPANEL previous = m_PreviousModal.Get();
            if ( previous && vgui::ipanel()->IsVisible( previous ) )
            {
                vgui::input()->SetAppModalSurface( previous );
                vgui::ipanel()->RequestFocus( previous );
            }
            else vgui::input()->ReleaseAppModalSurface();
        }
        m_PreviousModal = NULL;
    }
    void ClosePreview()
    {
        if ( !m_Preview ) return;
        m_Preview = false; SetMouseInputEnabled( false ); SetKeyBoardInputEnabled( false ); SetVisible( false );
        RestoreModal();
        if(!m_PreviewGameUI)engine->ClientCmd_Unrestricted("gameui_allowescape;gameui_hide");
        MarkForDeletion();
    }
    void OnKeyCodeTyped( vgui::KeyCode code ) OVERRIDE
    {
        if ( !m_Preview ) { BaseClass::OnKeyCodeTyped( code ); return; }
        if ( code == KEY_ESCAPE || code == KEY_ENTER ) ClosePreview();
        else if ( m_PreviewState.choice < 0 )
        {
            const char *binding = engine->Key_BindingForKey( (ButtonCode_t)code );
            if ( binding && ( !Q_strcmp( binding, "vote_yes" ) || !Q_strcmp( binding, "vote_no" ) ) ) PreviewChoice(!Q_strcmp(binding,"vote_no"));
        }
    }
private:
    bool m_PreviewGameUI; int m_ContentH,m_LastYes,m_LastNo,m_LastGeneration,m_LastChoice; double m_YesFlash,m_NoFlash; vgui::Label *m_Header; CHL2MPSettingsButton *m_Test[3];
    bool m_Preview, m_PreviewOnly, m_EditorSample; VoteState m_PreviewState; vgui::VPanelHandle m_PreviousModal; vgui::PHandle m_PreviewOwner; CHL2MPSettingsButton *m_Close;
    int m_LastPhase; double m_Started, m_NextPoll; int m_FontTall; vgui::HFont m_Font, m_Title; vgui::Label *m_Body; CHL2MPSettingsButton *m_Maps[MAX_MAPS];
};
vgui::DHANDLE<CVotePanel> s_Panel;
vgui::DHANDLE<CVotePanel> s_Preview;
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
    if(s_Preview&&s_Preview->IsPreview()) { s_Preview->PreviewChoice(choice);return; }
    if ( s_State.phase != KICK || !s_State.canVote || s_State.choice >= 0 ) return;
    Send( "cast", choice );
}
}
void HL2MPVoteUI_Init() { usermessages->HookMessage( "HL2MPVote", StateMessage ); usermessages->HookMessage( "HL2MPVoteMap", MapMessage ); if ( !s_Panel ) s_Panel = new CVotePanel; }
void HL2MPVoteUI_Preview( vgui::Panel *owner )
{
    if ( s_Preview && s_Preview->IsPreview() ) { s_Preview->ClosePreview(); return; }
    s_Preview = new CVotePanel( owner, true );
    s_Preview->Preview();
}
void HL2MPVoteUI_ClosePreview() { if ( s_Preview ) s_Preview->ClosePreview(); }
void HL2MPVoteUI_CreateEditorSample( vgui::Panel *owner ) { new CVotePanel( owner, true, true ); }
void HL2MPVoteUI_Reset() { memset( &s_State, 0, sizeof( s_State ) ); }
void HL2MPVoteUI_CallKick( int userid )
{
    player_info_t p;if(ConVarRef("developer").GetInt()>0 && engine->GetPlayerInfo(engine->GetLocalPlayer(),&p) && p.userID==userid) { HL2MPVoteUI_Preview();return; }
    Send("kick",userid);
}
bool HL2MPVoteUI_IsMapVote() { return s_State.phase == MAP_BALLOT || s_State.phase == MAP_RESULT; }
CON_COMMAND_F( vote_yes, "Vote yes in the current player vote.", FCVAR_CLIENTDLL ) { Cast( 0 ); }
CON_COMMAND_F( vote_no, "Vote no in the current player vote.", FCVAR_CLIENTDLL ) { Cast( 1 ); }
CON_COMMAND_F( cl_vote_status, "Show the current vote and local ballot state.", FCVAR_CLIENTDLL )
{
    Msg( "Vote: phase=%d generation=%d eligible=%d yes=%d no=%d can_vote=%d choice=%d maps=%d winner=%d remaining=%.1f\n", s_State.phase, s_State.generation, s_State.eligible, s_State.yes, s_State.no, s_State.canVote, s_State.choice, s_State.mapCount, s_State.winner, MAX( 0.0, s_State.deadline - Plat_FloatTime() ) );
    for ( int i = 0; i < s_State.mapCount; ++i ) Msg( "  %d: %s (%d votes)\n", i, s_State.maps[i], s_State.counts[i] );
}

CON_COMMAND_F(cl_vote_preview,"Local self-kick simulation: cl_vote_preview [self|pass|fail|close]. Requires developer 1.",FCVAR_CLIENTDLL)
{
    if(!ConVarRef("developer").GetInt())return;
    const char *action=args.ArgC()>1?args[1]:"self";
    if(!Q_stricmp(action,"close")) { HL2MPVoteUI_ClosePreview();return; }
    if(!s_Preview || !s_Preview->IsPreview())HL2MPVoteUI_Preview();
    if(!Q_stricmp(action,"pass"))s_Preview->Result(true);else if(!Q_stricmp(action,"fail"))s_Preview->Result(false);else s_Preview->ResetPreview();
}
