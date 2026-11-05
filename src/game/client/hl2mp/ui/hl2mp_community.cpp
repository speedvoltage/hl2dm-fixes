#include "cbase.h"
#include "hl2mp_messages.h"
#include "c_playerresource.h"
#include "hl2mp_community.h"
#include "hl2mp_gamerules.h"
#include "hl2mp_brightmodels.h"
#include "hl2mp_menucontrols.h"
#include "hl2mp_serverform.h"
#include "hl2mp_colorpicker.h"
#include "usermessages.h"
#include "ienginevgui.h"
#include "vgui_controls/EditablePanel.h"
#include "vgui_controls/CheckButton.h"
#include "vgui_controls/TextEntry.h"
#include "tier0/memdbgon.h"

ConVar cl_bright_player_models( "cl_bright_player_models", "0", FCVAR_ARCHIVE | FCVAR_USERINFO, "Use evenly lit, team-colored super Combine player models on community servers." );

bool HL2MPCommunity_Available()
{
    return engine->IsInGame() && !engine->IsLevelMainMenuBackground() && HL2MPRules() && HL2MPRules()->IsCommunityServer();
}
bool HL2MPBrightModels_Allowed() { return HL2MPCommunity_Available() && sv_bright_models.GetBool(); }
bool HL2MPBrightModels_Active() { return cl_bright_player_models.GetBool() && HL2MPBrightModels_Allowed(); }

namespace
{
class CServerControls;
CServerControls *s_Panel = NULL;
struct ServerState
{
    int generation, minutes, frags;
    bool enabled, admin, paused, teamplay, friendly, stay, respawn, brightAllowed, hitmarkers, hitsounds, cheats, pausable, footsteps, extendedRules;
    Color bright[3];
    char map[64], next[64], status[80];
};

class CCommunityConfirmation : public vgui::EditablePanel
{
    DECLARE_CLASS_SIMPLE( CCommunityConfirmation, vgui::EditablePanel );
public:
    CCommunityConfirmation( vgui::Panel *parent ) : BaseClass( parent, "Confirmation" )
    {
        SetVisible( false ); SetZPos( 200 ); SetMouseInputEnabled( true ); SetKeyBoardInputEnabled( true );
        heading = new vgui::Label( this, "Heading", "#HL2MP_UI_7ae41c475592" );
        body = new vgui::Label( this, "Body", "" ); body->SetWrap( true );
        yes = new CHL2MPSettingsButton( this, "#HL2MP_UI_8adb7edfc88f", "confirm" ); no = new CHL2MPSettingsButton( this, "#HL2MP_UI_ad8d698573fc", "cancel" );
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

class CBrightColors : public vgui::EditablePanel
{
    DECLARE_CLASS_SIMPLE(CBrightColors,vgui::EditablePanel);
public:
    CBrightColors(vgui::Panel *parent) : BaseClass(parent,"BrightColors")
    {
        SetVisible(false);SetZPos(200);SetMouseInputEnabled(true);SetKeyBoardInputEnabled(true);
        m_Title = new vgui::Label(this,"Title","#HL2MP_BrightColors");
        const char *labels[]={"#HL2MP_UI_127aad574b83","#HL2MP_UI_f185ee05cab6","#HL2MP_UI_46f29c2f51a0"};
        for(int i=0;i<3;++i) { m_Label[i]=new vgui::Label(this,"Group",labels[i]);m_Picker[i]=new CHL2MPColorPicker(this,"Picker"); }
        m_Save=new CHL2MPSettingsButton(this,"#HL2MP_UI_8a629300668e","bright_colors");m_Save->ServerStyle(true);
        m_Cancel=new CHL2MPSettingsButton(this,"#HL2MP_UI_ad8d698573fc","colors_cancel");m_Cancel->ServerStyle();
    }
    ~CBrightColors() { Hide(); }
    void Open(const Color *colors)
    {
        for(int i=0;i<3;++i)m_Picker[i]->SetColor(colors[i]);
        SetVisible(true);MoveToFront();m_Cancel->RequestFocus();vgui::input()->SetAppModalSurface(GetVPanel());
    }
    void Hide() { if(vgui::input()->GetAppModalSurface()==GetVPanel())vgui::input()->ReleaseAppModalSurface();SetVisible(false); }
    Color GetColor(int i) { return m_Picker[i]->GetColor(); }
    void Layout(int w,int h,float s,vgui::HFont title,vgui::HFont font)
    {
        SetBounds(0,0,w,h);m_W=int(940*s);m_H=int(420*s);m_X=(w-m_W)/2;m_Y=(h-m_H)/2;
        const int pad=int(24*s), col=(m_W-pad*4)/3;
        m_Title->SetBounds(m_X+pad,m_Y+pad,m_W-pad*2,int(40*s));m_Title->SetFont(title);m_Title->SetFgColor(HL2MPServerTheme::Orange());
        for(int i=0;i<3;++i)
        {
            int x=m_X+pad+i*(col+pad);
            m_Label[i]->SetBounds(x,m_Y+int(80*s),col,int(30*s));m_Label[i]->SetFont(font);m_Label[i]->SetFgColor(HL2MPServerTheme::Text());
            m_Picker[i]->SetBounds(x,m_Y+int(116*s),col,int(220*s));m_Picker[i]->Style(font);
        }
        m_Save->SetFont(font);m_Cancel->SetFont(font);
        m_Save->SetBounds(m_X+pad,m_Y+m_H-pad-int(40*s),(m_W-pad*3)/2,int(40*s));
        m_Cancel->SetBounds(m_X+(m_W+pad)/2,m_Y+m_H-pad-int(40*s),(m_W-pad*3)/2,int(40*s));
    }
    void PaintBackground() OVERRIDE
    {
        vgui::surface()->DrawSetColor(Color(0,0,0,190));vgui::surface()->DrawFilledRect(0,0,GetWide(),GetTall());
        HL2MPServerTheme::Rounded(m_X,m_Y,m_W,m_H,Color(42,42,42,255),6);
    }
    void OnCommand(const char *command) OVERRIDE { GetParent()->OnCommand(command); }
    void OnKeyCodeTyped(vgui::KeyCode code) OVERRIDE
    {
        if(code==KEY_ESCAPE) OnCommand("colors_cancel"); else GetParent()->OnKeyCodeTyped(code);
    }
private:
    int m_X=0,m_Y=0,m_W=0,m_H=0;
    vgui::Label *m_Title,*m_Label[3];CHL2MPColorPicker *m_Picker[3];CHL2MPSettingsButton *m_Save,*m_Cancel;
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
        if (!m_Received || !m_State.admin || m_Pending) return;
        vgui::CheckButton *checks[] = {m_Bright,m_Hitmarkers,m_Hitsounds,m_Cheats,m_Pausable,m_Footsteps};
        const bool values[] = {m_State.brightAllowed,m_State.hitmarkers,m_State.hitsounds,m_State.cheats,m_State.pausable,m_State.footsteps};
        for (int i=0;i<ARRAYSIZE(checks);++i) if(message->GetPtr("panel") == checks[i])
        {
            const bool value=message->GetInt("state")!=0;
            if(value==values[i])return;
            KeyValues *request=Message(i<3?"visual_rule":"server_rule");request->SetInt("rule",i<3?i:i-3);request->SetInt("enabled",value);
            engine->ServerCmdKeyValues(request);m_NextPoll=Plat_FloatTime()+0.5;return;
        }
    }
    void Close( bool resume = false );
    void RefreshPlayers();
    void Page(int page);
    void Request( const char *action );
    KeyValues *Message( const char *action );
    void Confirm( const char *action, const char *text, KeyValues *message );
    enum { MODE, MAP_NOW, MAP_NEXT, TEAM, PLAYER, DURATION, DROPDOWNS };
    enum { CHANGE, NEXT, PAUSE, RESTART, APPLY, JOIN, SPECTATE, KICK, BAN, CLOSE, RESUME, BUTTONS };
    vgui::Label *m_Title, *m_Access, *m_Info, *m_Status, *m_Captions[9];
    CHL2MPServerForm *m_Form[4];CHL2MPSettingsButton *m_Tab[2];int m_Page=0;
    CCommunityConfirmation *m_Confirm;
    CBrightColors *m_Colors;
    CHL2MPSettingsButton *m_ModeApply, *m_ColorsOpen;
    CHL2MPDropdown *m_Drop[DROPDOWNS];
    CHL2MPSettingsButton *m_Button[BUTTONS];
    vgui::TextEntry *m_Time, *m_Frags, *m_BanMinutes;
    vgui::CheckButton *m_Friendly, *m_Stay, *m_Respawn, *m_Bright, *m_Hitmarkers, *m_Hitsounds, *m_Cheats, *m_Pausable, *m_Footsteps;
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
    m_Title = new vgui::Label( this, "Title", "#HL2MP_UI_0bff1b6f6df3" );
    m_Access = new vgui::Label( this, "Access", "" ); m_Info = new vgui::Label( this, "Info", "" );
    m_Status = new vgui::Label( this, "Status", "" ); m_Confirm = new CCommunityConfirmation( this );
    m_Colors = new CBrightColors(this);
    m_ModeApply = new CHL2MPSettingsButton(this,"#HL2MP_UI_33bf9eeb20a9","teamplay");m_ModeApply->ServerStyle();
    m_ColorsOpen = new CHL2MPSettingsButton(this,"#HL2MP_BrightColors","colors_open");m_ColorsOpen->ServerStyle();
    const char *captions[] = { "#HL2MP_UI_ede76ceab6f1", "#HL2MP_UI_a6e14d931afd", "#HL2MP_UI_24b274dda498", "#HL2MP_UI_a092ca8db875", "#HL2MP_UI_475bba55cc00", "#HL2MP_UI_6a275336d8c3", "#HL2MP_UI_52327f55d2ec", "#HL2MP_UI_e6862630c208", "#HL2MP_UI_5902193cc0d9" };
    for ( int i = 0; i < 9; ++i ) m_Captions[i] = new vgui::Label( this, captions[i], captions[i] );
    for ( int i = 0; i < DROPDOWNS; ++i ) { m_Drop[i] = new CHL2MPDropdown( this, VarArgs( "Choice%d", i ) ); m_Drop[i]->ServerStyle(); }
    Choice( m_Drop[MODE], "#HL2MP_UI_127aad574b83", 0 ); Choice( m_Drop[MODE], "#HL2MP_UI_f756ee9002fc", 1 );
    Choice( m_Drop[DURATION], "#HL2MP_UI_cad356dd83d4", 5 ); Choice( m_Drop[DURATION], "#HL2MP_UI_6c112b4e8512", 15 ); Choice( m_Drop[DURATION], "#HL2MP_UI_7883d6a6ae23", 30 );
    Choice( m_Drop[DURATION], "#HL2MP_UI_f164fcdfc441", 60 ); Choice( m_Drop[DURATION], "#HL2MP_UI_1ed8853d3908", 240 ); Choice( m_Drop[DURATION], "#HL2MP_UI_d53d8ffbe3f1", 1440 );
    Choice( m_Drop[DURATION], "#HL2MP_UI_b872640f8c86", 10080 ); Choice( m_Drop[DURATION], "#HL2MP_UI_c915e80e682b", 43200 ); Choice( m_Drop[DURATION], "#HL2MP_UI_ae2081bb8d46", 0 ); Choice( m_Drop[DURATION], "#HL2MP_UI_7cd5885327fd", -1 );
    const char *texts[] = { "#HL2MP_UI_f51418e18b22", "#HL2MP_UI_defdaf4434fc", "#HL2MP_UI_ca6456daeccf", "#HL2MP_UI_dfcc426df3d9", "#HL2MP_UI_8a629300668e", "#HL2MP_UI_7042306b802b", "#HL2MP_UI_d5c2cd835c14", "#HL2MP_UI_b880b38fa7b1", "#HL2MP_UI_52465d569d82", "#HL2MP_UI_24c76252a90c", "#HL2MP_UI_d485c9082df8" };
    const char *commands[] = { "changelevel", "nextmap", "pause", "restart", "settings", "join", "spectate", "kick", "ban", "close", "resume" };
    for ( int i = 0; i < BUTTONS; ++i ) { m_Button[i] = new CHL2MPSettingsButton( this, texts[i], commands[i] ); m_Button[i]->ServerStyle( i == APPLY ); }
    m_Time = new vgui::TextEntry( this, "Time" ); m_Frags = new vgui::TextEntry( this, "Frags" );
    m_Time->SetAllowNumericInputOnly( true ); m_Frags->SetAllowNumericInputOnly( true ); m_Time->SetMaximumCharCount( 4 ); m_Frags->SetMaximumCharCount( 4 );
    m_BanMinutes = new vgui::TextEntry( this, "BanMinutes" ); m_BanMinutes->SetAllowNumericInputOnly( true ); m_BanMinutes->SetMaximumCharCount( 6 ); m_BanMinutes->SetText( "60" );
    m_Friendly = new CHL2MPCheckButton( this, "Friendly", "#HL2MP_UI_d5134a5bdfcc" );
    m_Stay = new CHL2MPCheckButton( this, "Stay", "#HL2MP_UI_18d384fbe9ba" );
    m_Respawn = new CHL2MPCheckButton( this, "Respawn", "#HL2MP_UI_d8205bf54d70" );
    m_Bright = new CHL2MPCheckButton(this,"Bright","#HL2MP_AllowBrightModels");
    m_Hitmarkers = new CHL2MPCheckButton(this,"Hitmarkers","#HL2MP_AllowHitmarkers");
    m_Hitsounds = new CHL2MPCheckButton(this,"Hitsounds","#HL2MP_AllowHitSounds");
    m_Hitmarkers->AddActionSignalTarget(this);m_Hitsounds->AddActionSignalTarget(this);
    m_Bright->AddActionSignalTarget( this );
    m_Cheats=new CHL2MPCheckButton(this,"Cheats","#HL2MP_EnableCheats");m_Cheats->AddActionSignalTarget(this);
    m_Pausable=new CHL2MPCheckButton(this,"Pausable","#HL2MP_PausableGame");m_Pausable->AddActionSignalTarget(this);
    m_Footsteps=new CHL2MPCheckButton(this,"Footsteps","#HL2MP_Footsteps");m_Footsteps->AddActionSignalTarget(this);
    const char *titles[]={"#HL2MP_UI_eaa42a15398b","#HL2MP_UI_fd49e5bc2a30","#HL2MP_UI_5a4effe2cffe","#HL2MP_ServerPermissions"};
    for(int i=0;i<4;++i)m_Form[i]=new CHL2MPServerForm(this,titles[i]);
    m_Tab[0]=new CHL2MPSettingsButton(this,"#HL2MP_UI_830ef117e138","page_server");m_Tab[0]->ServerStyle();
    m_Tab[1]=new CHL2MPSettingsButton(this,"#HL2MP_UI_5a4effe2cffe","page_rules");m_Tab[1]->ServerStyle();
    m_Form[0]->Add(m_Drop[MODE],m_Captions[0],m_ModeApply);
    m_Form[0]->Add(m_Drop[MAP_NOW],m_Captions[1]);m_Form[0]->Add(m_Button[CHANGE]);
    m_Form[0]->Add(m_Drop[MAP_NEXT],m_Captions[2]);m_Form[0]->Add(m_Button[NEXT]);m_Form[0]->Add(m_Button[PAUSE],NULL,m_Button[RESTART]);
    m_Form[1]->Add(m_Drop[TEAM],m_Captions[5]);m_Form[1]->Add(m_Button[JOIN],NULL,m_Button[SPECTATE]);
    m_Form[1]->Add(m_Drop[PLAYER],m_Captions[6]);m_Form[1]->Add(m_Drop[DURATION],m_Captions[7]);
    m_Form[1]->Add(m_BanMinutes,m_Captions[8]);m_Form[1]->Add(m_Button[KICK],NULL,m_Button[BAN]);
    m_Form[2]->Add(m_Time,m_Captions[3]);m_Form[2]->Add(m_Frags,m_Captions[4]);
    vgui::CheckButton *rules[]={m_Friendly,m_Stay,m_Respawn};
    for(int i=0;i<ARRAYSIZE(rules);++i)m_Form[2]->Add(rules[i]);
    m_Form[3]->Add(m_Bright);m_Form[3]->Add(m_Hitmarkers);m_Form[3]->Add(m_Hitsounds);m_Form[3]->Add(m_Cheats);m_Form[3]->Add(m_Pausable);m_Form[3]->Add(m_Footsteps);m_Form[3]->Add(m_ColorsOpen);
    Page(0);
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
    const int left=(w-int(1180*s))/2,bodyWide=int(1180*s),pad=MAX(12,int(20*s)),gap=MAX(12,int(20*s));
    m_Title->SetFont(m_TitleFont);m_Title->SetFgColor(HL2MPServerTheme::Orange());
    const int titleH=vgui::surface()->GetFontTall(m_TitleFont)+8;
    m_Title->SetBounds(left,pad,bodyWide,titleH);
    m_Info->SetFont(m_SmallFont);m_Info->SetFgColor(HL2MPServerTheme::Muted());
    const int infoY=pad+titleH+pad/2,infoH=MAX(24,int(26*s));m_Info->SetBounds(left,infoY,bodyWide,infoH);
    m_Access->SetFont(m_SmallFont);m_Access->SetFgColor(HL2MPServerTheme::Muted());m_Access->SetBounds(left,infoY+infoH,bodyWide,infoH);
    const int tabsY=infoY+2*infoH+pad/2,tabsH=MAX(32,int(38*s)),cardW=(bodyWide-gap)/2;
    for(int i=0;i<2;++i) { m_Tab[i]->SetFont(m_BodyFont);m_Tab[i]->SetBounds(left+i*(cardW+gap),tabsY,cardW,tabsH); }
    const int buttonH=MAX(32,int(40*s)),footerY=h-pad-buttonH,statusH=MAX(30,int(36*s)),statusY=footerY-pad-statusH,top=tabsY+tabsH+pad;
    for(int i=0;i<4;++i) { m_Form[i]->SetBounds(left+(i%2)*(cardW+gap),top,cardW,MAX(64,statusY-pad-top));m_Form[i]->Style(m_BodyFont,m_SmallFont,s); }
    m_Status->SetFont(m_SmallFont);m_Status->SetWrap(true);m_Status->SetBounds(left,statusY,bodyWide,statusH);
    m_Button[CLOSE]->SetFont(m_BodyFont);m_Button[RESUME]->SetFont(m_BodyFont);m_Button[APPLY]->SetFont(m_BodyFont);
    const int closeW=MAX(90,int(160*s)),resumeW=MAX(120,int(220*s)),applyW=MIN(cardW,MAX(140,int(260*s)));
    m_Button[CLOSE]->SetBounds(left,footerY,closeW,buttonH);
    m_Button[RESUME]->SetBounds(w-left-resumeW,footerY,resumeW,buttonH);
    m_Button[APPLY]->SetBounds((w-applyW)/2,footerY,applyW,buttonH);
    m_Confirm->Layout(w,h,s,m_TitleFont,m_BodyFont);m_Colors->Layout(w,h,s,m_TitleFont,m_BodyFont);
}

void CServerControls::Page(int page)
{
    m_Page=page;for(int i=0;i<DROPDOWNS;++i)m_Drop[i]->HideMenu();
    for(int i=0;i<4;++i)m_Form[i]->SetVisible(i/2==page);
    for(int i=0;i<2;++i)m_Tab[i]->Active(i==page);
    m_Button[APPLY]->SetVisible(page==1);InvalidateLayout(true);
}
void CServerControls::PaintBackground()
{
    vgui::surface()->DrawSetColor(Color(32,32,32,250));vgui::surface()->DrawFilledRect(0,0,GetWide(),GetTall());
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
    Choice( m_Drop[TEAM], "#HL2MP_UI_3cced94f86b9", 0 );
    if ( HL2MPRules()->IsTeamplay() ) { Choice( m_Drop[TEAM], "#HL2MP_UI_46f29c2f51a0", TEAM_COMBINE ); Choice( m_Drop[TEAM], "#HL2MP_UI_f185ee05cab6", TEAM_REBELS ); }
    m_Drop[TEAM]->SilentActivateItemByRow( 0 );
    m_Status->SetText( "#HL2MP_UI_a4999ebc061c" ); SetVisible( true ); MoveToFront(); RequestFocus(); InvalidateLayout( true, true ); Request( "open" );
}
void CServerControls::Close( bool resume )
{
    m_Colors->Hide();
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
    const bool modeChanged = !m_Received || m_State.teamplay != state.teamplay;
    m_State = state;
    m_Bright->SetSelected(state.brightAllowed);m_Hitmarkers->SetSelected(state.hitmarkers);m_Hitsounds->SetSelected(state.hitsounds);
    m_Cheats->SetSelected(state.cheats);m_Pausable->SetSelected(state.pausable);m_Footsteps->SetSelected(state.footsteps);
    if(modeChanged)
    {
        m_Drop[TEAM]->DeleteAllItems();Choice(m_Drop[TEAM],"#HL2MP_UI_3cced94f86b9",0);
        if(state.teamplay) { Choice(m_Drop[TEAM],"#HL2MP_UI_46f29c2f51a0",TEAM_COMBINE);Choice(m_Drop[TEAM],"#HL2MP_UI_f185ee05cab6",TEAM_REBELS); }
        m_Drop[TEAM]->SilentActivateItemByRow(0);m_Drop[MODE]->SilentActivateItemByRow(state.teamplay?1:0);
    }
    if ( !state.enabled ) { Close(); return; }
    if ( !m_Received || !Q_strcmp( state.status, "#HL2MP_UI_35cf85d0b20a" ) )
    {
        m_Time->SetText( VarArgs( "%d", state.minutes ) ); m_Frags->SetText( VarArgs( "%d", state.frags ) );
        m_Friendly->SetSelected( state.friendly ); m_Stay->SetSelected( state.stay ); m_Respawn->SetSelected( state.respawn );
        m_Drop[MODE]->SilentActivateItemByRow( state.teamplay ? 1 : 0 );
    }
    m_Received = true; m_Access->SetText( state.admin ? "#HL2MP_UI_986e85ebcad9" : "#HL2MP_UI_87419fb92681" );
    m_Info->SetText( HL2MPFormat( "%s  /  %s  /  NEXT: %s", state.map, state.teamplay ? HL2MPText( "#HL2MP_UI_f756ee9002fc" ) : HL2MPText( "#HL2MP_UI_127aad574b83" ), state.next[0] ? state.next : HL2MPText( "#HL2MP_UI_541e1279cdfe" ) ) );
    if ( state.status[0] ) m_Status->SetText( state.status );
    else if ( !state.admin ) m_Status->SetText( "#HL2MP_UI_f3ca27a9487d" );
    else m_Status->SetText( state.paused ? "#HL2MP_UI_0f84a5f7ba54" : "" );
    m_Button[PAUSE]->SetText( state.paused ? "#HL2MP_UI_0747c96a23db" : "#HL2MP_UI_ca6456daeccf" );
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
        Choice( m_Drop[PLAYER], g_PR ? g_PR->GetPlayerName( i ) : info.name, info.userID ); if ( info.userID == selected ) chosen = row; ++row;
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
    m_ModeApply->SetEnabled(admin && Value(m_Drop[MODE]) != int(m_State.teamplay));m_ColorsOpen->SetEnabled(admin);
    if(!admin && m_Colors->IsVisible())m_Colors->Hide();
    for ( int i = CHANGE; i <= APPLY; ++i ) m_Button[i]->SetEnabled( admin && ( i > NEXT || m_MapsDone ) );
    m_Button[KICK]->SetEnabled( admin && Value( m_Drop[PLAYER] ) > 0 ); m_Button[BAN]->SetEnabled( admin && Value( m_Drop[PLAYER] ) > 0 );
    m_Time->SetEnabled( admin ); m_Frags->SetEnabled( admin ); m_Friendly->SetEnabled( admin ); m_Stay->SetEnabled( admin ); m_Respawn->SetEnabled( admin );
    m_BanMinutes->SetEnabled( admin && Value( m_Drop[DURATION] ) == -1 );
    for ( int i = 0; i < DROPDOWNS; ++i ) m_Drop[i]->SetEnabled( i == TEAM ? m_Received && !m_Pending : admin );
    m_Button[JOIN]->SetEnabled( m_Received && !m_Pending ); m_Button[SPECTATE]->SetEnabled( m_Received && !m_Pending ); m_Bright->SetEnabled(admin);m_Hitmarkers->SetEnabled(admin);m_Hitsounds->SetEnabled(admin);
    m_Cheats->SetEnabled(admin && m_State.extendedRules);m_Pausable->SetEnabled(admin && m_State.extendedRules);m_Footsteps->SetEnabled(admin && m_State.extendedRules);
    m_Button[APPLY]->SetVisible(m_Page==1 && !m_Pending);
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
    if(!Q_strcmp(command,"page_server")) { Page(0);return; }
    if(!Q_strcmp(command,"page_rules")) { Page(1);return; }
    if(!Q_strcmp(command,"colors_cancel")) { m_Colors->Hide();m_ColorsOpen->RequestFocus();return; }
    if(!Q_strcmp(command,"colors_open"))
    {
        if(m_Received && m_State.admin && !m_Pending) { for(int i=0;i<DROPDOWNS;++i)m_Drop[i]->HideMenu();m_Colors->Open(m_State.bright); }return;
    }
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
    else if(!Q_strcmp(command,"bright_colors"))
    {
        for(int i=0;i<3;++i) { Color c=m_Colors->GetColor(i);for(int channel=0;channel<3;++channel)message->SetInt(VarArgs("color%d_%d",i,channel),c[channel]); }
        m_Colors->Hide();m_ColorsOpen->RequestFocus();
    }
    else if(!Q_strcmp(command,"teamplay"))
    {
        message->SetInt("teamplay",Value(m_Drop[MODE]));Confirm(command,"#HL2MP_ModeChangeConfirm",message);return;
    }
    else if ( !Q_strcmp( command, "pause" ) ) message->SetInt( "paused", !m_State.paused );
    else if ( !Q_strcmp( command, "join" ) || !Q_strcmp( command, "spectate" ) ) { message->SetString( "action", "join" ); message->SetInt( "team", !Q_strcmp( command, "spectate" ) ? TEAM_SPECTATOR : Value( m_Drop[TEAM] ) ); }
    else if ( !Q_strcmp( command, "changelevel" ) || !Q_strcmp( command, "nextmap" ) )
    {
        m_Drop[!Q_strcmp( command, "changelevel" ) ? MAP_NOW : MAP_NEXT]->GetText( text, sizeof( text ) ); message->SetString( "map", text ); message->SetInt( "teamplay", Value( m_Drop[MODE] ) );
        if ( !Q_strcmp( command, "changelevel" ) ) { Confirm( command, HL2MPFormat( "Load %s in %s mode? This ends the current round.", text, Value( m_Drop[MODE] ) ? HL2MPText( "#HL2MP_UI_5733010b622d" ) : HL2MPText( "#HL2MP_UI_7c04d94e2260" ) ), message ); return; }
    }
    else if ( !Q_strcmp( command, "restart" ) ) { Confirm( command, "#HL2MP_UI_149fa905b451", message ); return; }
    else if ( !Q_strcmp( command, "kick" ) || !Q_strcmp( command, "ban" ) )
    {
        int minutes = Value( m_Drop[DURATION] );
        if ( !Q_strcmp( command, "ban" ) && minutes < 0 )
        {
            char custom[16]; m_BanMinutes->GetText( custom, sizeof( custom ) ); minutes = 0;
            bool valid = custom[0] != 0;
            for ( const char *c = custom; *c; ++c ) { if ( *c < '0' || *c > '9' ) { valid = false; break; } minutes = minutes * 10 + *c - '0'; }
            if ( !valid || minutes < 1 || minutes > 525600 ) { message->deleteThis(); m_Status->SetText( "#HL2MP_UI_1f327e750845" ); return; }
        }
        message->SetInt( "userid", Value( m_Drop[PLAYER] ) ); message->SetInt( "minutes", minutes ); m_Drop[PLAYER]->GetText( text, sizeof( text ) );
        char duration[512]; m_Drop[DURATION]->GetText( duration, sizeof( duration ) );
        if ( Value( m_Drop[DURATION] ) < 0 ) HL2MPFormatInto( duration, sizeof( duration ), "%d MINUTES", minutes );
        Confirm( command, !Q_strcmp( command, "ban" ) ? HL2MPFormat( "Ban %s from this server (%s)?", text, duration ) : HL2MPFormat( "Kick %s from this server?", text ), message ); return;
    }
    else { message->deleteThis(); BaseClass::OnCommand( command ); return; }
    engine->ServerCmdKeyValues( message ); m_NextPoll = Plat_FloatTime() + 2;
}

void StateMessage( bf_read &reader )
{
    ServerState state = {}; state.generation = reader.ReadLong(); state.enabled = reader.ReadByte() != 0; state.admin = reader.ReadByte() != 0;
    state.paused = reader.ReadByte() != 0; state.teamplay = reader.ReadByte() != 0; state.minutes = reader.ReadShort(); state.frags = reader.ReadShort();
    state.friendly = reader.ReadByte() != 0; state.stay = reader.ReadByte() != 0; state.respawn = reader.ReadByte() != 0;
    state.brightAllowed=reader.ReadByte()!=0;state.hitmarkers=reader.ReadByte()!=0;state.hitsounds=reader.ReadByte()!=0;
    for(int i=0;i<3;++i) { const int r=reader.ReadByte(),g=reader.ReadByte(),b=reader.ReadByte();state.bright[i]=Color(r,g,b,255); }
    reader.ReadString( state.map, sizeof( state.map ) ); reader.ReadString( state.next, sizeof( state.next ) ); reader.ReadString( state.status, sizeof( state.status ) );
    // Appended fields keep the existing state format readable by older clients.
    state.extendedRules=reader.GetNumBytesLeft()>=3;
    if(state.extendedRules) { state.cheats=reader.ReadByte()!=0;state.pausable=reader.ReadByte()!=0;state.footsteps=reader.ReadByte()!=0; }
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
