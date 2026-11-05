#include "cbase.h"
#include "hl2mp_messages.h"
#include "hl2mp_host.h"
#include "hl2mp_loading.h"
#include "hl2mp_menucontrols.h"
#include "hl2mp_serverform.h"
#include "hl2mp_coordinator.h"
#include "filesystem.h"
#include "vgui_controls/EditablePanel.h"
#include "vgui_controls/TextEntry.h"
#include "vgui_controls/CheckButton.h"
#include "tier0/memdbgon.h"

namespace
{
bool HostMapName( const char *map )
{
    if ( !map[0] || Q_strlen( map ) > 63 ) return false;
    for ( const char *c = map; *c; ++c )
        if ( !( ( *c >= 'a' && *c <= 'z' ) || ( *c >= 'A' && *c <= 'Z' ) || ( *c >= '0' && *c <= '9' ) || *c == '_' || *c == '-' ) ) return false;
    char path[80]; Q_snprintf( path, sizeof( path ), "maps/%s.bsp", map ); return filesystem->FileExists( path, "GAME" );
}
bool Number( vgui::TextEntry *entry, int low, int high, int &number )
{
    char text[16]; entry->GetText( text, sizeof( text ) ); number = 0;
    if ( !text[0] || Q_strlen( text ) > 6 ) return false;
    for ( const char *c = text; *c; ++c ) { if ( *c < '0' || *c > '9' ) return false; number = number * 10 + *c - '0'; }
    return number >= low && number <= high;
}
void ReadUTF8( vgui::TextEntry *entry, char *text, int size )
{
    wchar_t wide[128]; entry->GetText( wide, sizeof( wide ) ); V_UnicodeToUTF8( wide, text, size );
}

class CHostConfirmation : public vgui::EditablePanel
{
    DECLARE_CLASS_SIMPLE(CHostConfirmation,vgui::EditablePanel);
public:
    CHostConfirmation(vgui::Panel *parent) : BaseClass(parent,"StartServerConfirmation") {
        SetVisible(false);SetZPos(200);
        m_Title=new vgui::Label(this,"Title","#HL2MP_UI_7ae41c475592");
        m_Body=new vgui::Label(this,"Body","#HL2MP_HostDisconnectConfirm");m_Body->SetWrap(true);
        m_Yes=new CHL2MPSettingsButton(parent,"#HL2MP_UI_485ffc6dcae0","confirm_start");m_Yes->SetParent(this);m_Yes->ServerStyle(true);
        m_No=new CHL2MPSettingsButton(parent,"#HL2MP_UI_ad8d698573fc","cancel_start");m_No->SetParent(this);m_No->ServerStyle();
    }
    ~CHostConfirmation() { Hide(); }
    void Show() { SetVisible(true);MoveToFront();m_No->RequestFocus();vgui::input()->SetAppModalSurface(GetVPanel()); }
    void Hide() { if(vgui::input()->GetAppModalSurface()==GetVPanel())vgui::input()->ReleaseAppModalSurface();SetVisible(false); }
    void Layout(int w,int h,float scale,vgui::HFont title,vgui::HFont font) {
        SetBounds(0,0,w,h);const int pad=MAX(16,int(24*scale)),gap=MAX(12,int(16*scale));
        m_W=MIN(w-32,int(700*scale));m_X=(w-m_W)/2;
        m_Title->SetFont(title);m_Title->SetFgColor(HL2MPServerTheme::Orange());
        m_Body->SetFont(font);m_Body->SetFgColor(HL2MPServerTheme::Text());
        m_Body->GetTextImage()->SetSize(m_W-2*pad,1024);m_Body->GetTextImage()->RecalculateNewLinePositions();
        int tw,th;m_Body->GetTextImage()->GetContentSize(tw,th);
        const int heading=vgui::surface()->GetFontTall(title)+8,bh=MAX(36,int(42*scale));
        m_H=3*pad+heading+th+gap+bh;m_Y=(h-m_H)/2;
        m_Title->SetBounds(m_X+pad,m_Y+pad,m_W-2*pad,heading);
        m_Body->SetBounds(m_X+pad,m_Y+pad+heading+gap,m_W-2*pad,th);m_Body->InvalidateLayout(true);
        const int bw=(m_W-2*pad-gap)/2; m_Yes->SetFont(font);m_No->SetFont(font);
        m_Yes->SetBounds(m_X+pad,m_Y+m_H-pad-bh,bw,bh);m_No->SetBounds(m_X+pad+bw+gap,m_Y+m_H-pad-bh,bw,bh);
    }
    void PaintBackground() OVERRIDE {
        vgui::surface()->DrawSetColor(Color(0,0,0,190));vgui::surface()->DrawFilledRect(0,0,GetWide(),GetTall());
        HL2MPServerTheme::Rounded(m_X,m_Y,m_W,m_H,Color(42,42,42,255),6);
    }
    void OnKeyCodeTyped(vgui::KeyCode code) OVERRIDE { if(code==KEY_ESCAPE)GetParent()->OnCommand("cancel_start");else BaseClass::OnKeyCodeTyped(code); }
private:
    int m_X=0,m_Y=0,m_W=0,m_H=0;vgui::Label *m_Title,*m_Body;CHL2MPSettingsButton *m_Yes,*m_No;
};

class CHostServer : public vgui::EditablePanel
{
    DECLARE_CLASS_SIMPLE( CHostServer, vgui::EditablePanel );
public:
    CHostServer( vgui::Panel *parent );
    ~CHostServer() { if(IsVisible())engine->ClientCmd_Unrestricted("gameui_allowescape"); }
    void Open();
    void PerformLayout() OVERRIDE;
    void PaintBackground() OVERRIDE;
    void OnThink() OVERRIDE;
    void OnCommand( const char *command ) OVERRIDE;
    void OnKeyCodeTyped( vgui::KeyCode code ) OVERRIDE;
private:
    void Close(); void Start(bool confirmed=false); void Page(int page);
    enum { NAME, PASSWORD, SLOTS, TIME, FRAGS, GRAVITY, BOTS, ENTRIES };
    enum { MAP, MODE, NETWORK, DIFFICULTY, DROPS };
    enum { FRIENDLY, STAY, RESPAWN, FALLDAMAGE, BRIGHT, HITMARKERS, HITSOUNDS, CHEATS, PAUSABLE, FOOTSTEPS, CHECKS };
    vgui::Label *m_Title, *m_Description, *m_Status, *m_NetworkHint, *m_BotHint, *m_Caption[ENTRIES + DROPS];
    vgui::TextEntry *m_Entry[ENTRIES]; CHL2MPDropdown *m_Drop[DROPS]; vgui::CheckButton *m_Check[CHECKS];
    CHL2MPSettingsButton *m_Back, *m_Start, *m_Tab[2];
    CHL2MPServerForm *m_Form[4];CHostConfirmation *m_Confirm;int m_Page=0;bool m_WaitingDisconnect=false;int m_HintNetwork=-1,m_HintNav=-1;
    vgui::HFont m_HeadingFont, m_Font, m_Small;
    int m_Width, m_Height; float m_Scale;
};

CHostServer::CHostServer( vgui::Panel *parent ) : BaseClass( parent, "CreateServer" ), m_Width( 0 ), m_Height( 0 ), m_Scale( 1 )
{
    SetVisible( false ); SetMouseInputEnabled( true ); SetKeyBoardInputEnabled( true );
    m_HeadingFont = vgui::surface()->CreateFont(); m_Font = vgui::surface()->CreateFont(); m_Small = vgui::surface()->CreateFont();
    m_Title = new vgui::Label( this, "Title", "#HL2MP_UI_6e142bc97c2d" );
    m_Description = new vgui::Label( this, "Description", "#HL2MP_UI_c78d3e50b5f0" );
    m_Status = new vgui::Label( this, "Status", "" ); m_Status->SetWrap( true );
    m_NetworkHint = new vgui::Label( this, "NetworkHint", "" ); m_NetworkHint->SetWrap( true );
    m_BotHint = new vgui::Label( this, "BotHint", "" ); m_BotHint->SetWrap( true );
    const char *captions[] = { "#HL2MP_UI_21babfed6c38", "#HL2MP_UI_54c44337615a", "#HL2MP_UI_651dc42c69e4", "#HL2MP_UI_a092ca8db875", "#HL2MP_UI_475bba55cc00", "#HL2MP_UI_7071825d9a0d", "#HL2MP_UI_f17c0acbd8e5", "#HL2MP_UI_be176b0015c4", "#HL2MP_UI_ede76ceab6f1", "#HL2MP_UI_639a40e82b9a", "#HL2MP_UI_f7f4f5eba6f8" };
    const char *defaults[] = { "Half-Life 2 Deathmatch", "", "8", "20", "0", "600", "0" };
    for ( int i = 0; i < ENTRIES + DROPS; ++i ) m_Caption[i] = new vgui::Label( this, captions[i], captions[i] );
    for ( int i = 0; i < ENTRIES; ++i )
    {
        m_Entry[i] = new vgui::TextEntry( this, VarArgs( "Value%d", i ) ); m_Entry[i]->SetMaximumCharCount( i <= PASSWORD ? 63 : 4 );
        m_Entry[i]->SetAllowNumericInputOnly( i > PASSWORD ); m_Entry[i]->SetText( defaults[i] );
    }
    m_Entry[PASSWORD]->SetTextHidden( true );
    for ( int i = 0; i < DROPS; ++i ) { m_Drop[i] = new CHL2MPDropdown( this, VarArgs( "Choice%d", i ) ); m_Drop[i]->ServerStyle(); }
    m_Drop[MODE]->AddItem( "#HL2MP_UI_127aad574b83", NULL ); m_Drop[MODE]->AddItem( "#HL2MP_UI_f756ee9002fc", NULL );
    m_Drop[NETWORK]->AddItem( "#HL2MP_UI_b4bba182e55e", NULL ); m_Drop[NETWORK]->AddItem( "#HL2MP_UI_2a47e28ec0cf", NULL ); m_Drop[NETWORK]->AddItem( "#HL2MP_UI_7c8356578e20", NULL );
    const char *skills[] = { "#HL2MP_UI_27e88b861fa9", "#HL2MP_UI_db2cb3fe28e2", "#HL2MP_UI_0e24076baf99", "#HL2MP_UI_884981556c79" };
    for ( int i = 0; i < 4; ++i ) m_Drop[DIFFICULTY]->AddItem( skills[i], NULL );
    m_Drop[MODE]->SilentActivateItemByRow( 0 ); m_Drop[NETWORK]->SilentActivateItemByRow( 0 ); m_Drop[DIFFICULTY]->SilentActivateItemByRow( 1 );
    const char *checks[] = { "#HL2MP_UI_d5134a5bdfcc", "#HL2MP_UI_18d384fbe9ba", "#HL2MP_UI_d8205bf54d70", "#HL2MP_UI_a16fd61ee2e6", "#HL2MP_AllowBrightModels", "#HL2MP_AllowHitmarkers", "#HL2MP_AllowHitSounds", "#HL2MP_EnableCheats", "#HL2MP_PausableGame", "#HL2MP_Footsteps" };
    for ( int i = 0; i < CHECKS; ++i ) m_Check[i] = new CHL2MPCheckButton( this, checks[i], checks[i] );
    m_Check[RESPAWN]->SetSelected( true );
    m_Check[BRIGHT]->SetSelected( false );
    m_Check[FOOTSTEPS]->SetSelected(true);
    m_Back = new CHL2MPSettingsButton( this, "#HL2MP_UI_24c76252a90c", "close" ); m_Start = new CHL2MPSettingsButton( this, "#HL2MP_UI_485ffc6dcae0", "start" );
    m_Back->ServerStyle(); m_Start->ServerStyle( true );
    const char *titles[]={"#HL2MP_UI_830ef117e138","#HL2MP_UI_bbed1da098ec","#HL2MP_UI_d898b7f024f0","#HL2MP_ServerPermissions"};
    for(int i=0;i<4;++i)m_Form[i]=new CHL2MPServerForm(this,titles[i]);
    m_Tab[0]=new CHL2MPSettingsButton(this,"#HL2MP_UI_830ef117e138","page_server");m_Tab[0]->ServerStyle();
    m_Tab[1]=new CHL2MPSettingsButton(this,"#HL2MP_UI_d898b7f024f0","page_rules");m_Tab[1]->ServerStyle();
    auto entry=[&](int card,int id) { m_Form[card]->Add(m_Entry[id],m_Caption[id]); };
    auto drop=[&](int card,int id) { m_Form[card]->Add(m_Drop[id],m_Caption[ENTRIES+id]); };
    entry(0,NAME);drop(0,MAP);drop(0,MODE);entry(0,SLOTS);entry(0,PASSWORD);
    drop(1,NETWORK);m_Form[1]->Add(m_NetworkHint,NULL,NULL,true);entry(1,BOTS);drop(1,DIFFICULTY);m_Form[1]->Add(m_BotHint,NULL,NULL,true);
    entry(2,TIME);entry(2,FRAGS);entry(2,GRAVITY);
    for(int i=FRIENDLY;i<=FALLDAMAGE;++i)m_Form[2]->Add(m_Check[i]);
    for(int i=BRIGHT;i<CHECKS;++i)m_Form[3]->Add(m_Check[i]);
    m_Confirm=new CHostConfirmation(this);Page(0);
}
void CHostServer::Open()
{
    if ( HL2MPCoordinator().Busy() ) return;
    engine->ClientCmd_Unrestricted("gameui_preventescape");
    char selected[64]; m_Drop[MAP]->GetText( selected, sizeof( selected ) ); m_Drop[MAP]->DeleteAllItems();
    FileFindHandle_t handle;
    for ( const char *file = filesystem->FindFirstEx( "maps/*.bsp", "GAME", &handle ); file; file = filesystem->FindNext( handle ) )
    {
        char map[64]; Q_StripExtension( file, map, sizeof( map ) ); if ( !HostMapName( map ) ) continue;
        bool duplicate = false;
        for ( int i = 0; i < m_Drop[MAP]->GetItemCount(); ++i ) { char existing[64]; m_Drop[MAP]->GetItemText( m_Drop[MAP]->GetItemIDFromRow( i ), existing, sizeof( existing ) ); if ( !Q_stricmp( existing, map ) ) duplicate = true; }
        if ( !duplicate ) m_Drop[MAP]->AddItem( map, NULL );
    }
    filesystem->FindClose( handle ); m_Drop[MAP]->SilentActivateItemByRow( 0 );
    for ( int i = 0; i < m_Drop[MAP]->GetItemCount(); ++i )
    {
        char map[64]; m_Drop[MAP]->GetItemText( m_Drop[MAP]->GetItemIDFromRow( i ), map, sizeof( map ) );
        if ( !Q_stricmp( map, selected[0] ? selected : "dm_lockdown" ) ) m_Drop[MAP]->SilentActivateItemByRow( i );
    }
    m_Status->SetText( m_Drop[MAP]->GetItemCount() ? "#HL2MP_UI_9ec2843d0184" : "#HL2MP_UI_368c797c01d2" );
    SetVisible( true ); MoveToFront(); InvalidateLayout( true, true ); m_Back->RequestFocus();
}
void CHostServer::Page(int page)
{
    m_Page=page;for(int i=0;i<DROPS;++i)m_Drop[i]->HideMenu();
    for(int i=0;i<4;++i)m_Form[i]->SetVisible(i/2==page);
    for(int i=0;i<2;++i)m_Tab[i]->Active(i==page);
    InvalidateLayout(true);
}
void CHostServer::Close()
{
    m_Confirm->Hide();m_WaitingDisconnect=false;
    engine->ClientCmd_Unrestricted("gameui_allowescape");
    for ( int i = 0; i < DROPS; ++i ) m_Drop[i]->HideMenu();
    m_Entry[PASSWORD]->SetText( "" ); SetVisible( false ); GetParent()->OnCommand( "host_closed" );
}
void CHostServer::PerformLayout()
{
    BaseClass::PerformLayout(); int w, h; vgui::surface()->GetScreenSize( w, h ); SetBounds( 0, 0, w, h );
    const float s = m_Scale = MIN( w / 1280.0f, h / 720.0f ); const int left = ( w - int( 1180 * s ) ) / 2;
    if ( w != m_Width || h != m_Height )
    {
        m_HeadingFont = vgui::surface()->CreateFont(); m_Font = vgui::surface()->CreateFont(); m_Small = vgui::surface()->CreateFont();
        vgui::surface()->SetFontGlyphSet( m_HeadingFont, "GorDIN Bold", MAX( 20, int( 34 * s ) ), 600, 0, 0, vgui::ISurface::FONTFLAG_ANTIALIAS );
        vgui::surface()->SetFontGlyphSet( m_Font, "GorDIN", MAX( 12, int( 18 * s ) ), 400, 0, 0, vgui::ISurface::FONTFLAG_ANTIALIAS );
        vgui::surface()->SetFontGlyphSet( m_Small, "GorDIN", MAX( 11, int( 15 * s ) ), 400, 0, 0, vgui::ISurface::FONTFLAG_ANTIALIAS );
    }
    m_Width = w; m_Height = h;
    const int pad=MAX(12,int(20*s)),bodyWide=int(1180*s),gap=MAX(12,int(20*s));
    m_Title->SetFont(m_HeadingFont);m_Title->SetFgColor(HL2MPServerTheme::Orange());
    const int titleH=vgui::surface()->GetFontTall(m_HeadingFont)+8;
    m_Title->SetBounds(left,pad,bodyWide,titleH);
    m_Description->SetFont(m_Small);m_Description->SetFgColor(HL2MPServerTheme::Muted());m_Description->SetWrap(true);
    const int descY=pad+titleH+pad/2,descH=MAX(26,int(30*s));m_Description->SetBounds(left,descY,bodyWide,descH);
    const int tabsY=descY+descH+pad,tabsH=MAX(32,int(38*s)),cardW=(bodyWide-gap)/2;
    for(int i=0;i<2;++i) { m_Tab[i]->SetFont(m_Font);m_Tab[i]->SetBounds(left+i*(cardW+gap),tabsY,cardW,tabsH); }
    const int buttonH=MAX(32,int(40*s)),footerY=h-pad-buttonH,statusH=MAX(30,int(36*s)),statusY=footerY-pad-statusH;
    const int top=tabsY+tabsH+pad;
    for(int i=0;i<4;++i) { m_Form[i]->SetBounds(left+(i%2)*(cardW+gap),top,cardW,MAX(64,statusY-pad-top));m_Form[i]->Style(m_Font,m_Small,s); }
    m_Status->SetFont(m_Small);m_Status->SetFgColor(HL2MPServerTheme::Text());m_Status->SetBounds(left,statusY,bodyWide,statusH);
    m_Back->SetFont(m_Font);m_Start->SetFont(m_Font);
    m_Back->SetBounds(left,footerY,MAX(100,int(160*s)),buttonH);const int startW=MAX(150,int(264*s));m_Start->SetBounds(w-left-startW,footerY,startW,buttonH);
    m_Confirm->Layout(w,h,s,m_HeadingFont,m_Font);
    m_Description->InvalidateLayout(true);m_Status->InvalidateLayout(true);
}
void CHostServer::PaintBackground()
{
    vgui::surface()->DrawSetColor(Color(32,32,32,250));vgui::surface()->DrawFilledRect(0,0,GetWide(),GetTall());
}
void CHostServer::OnThink()
{
    BaseClass::OnThink();
    if(m_WaitingDisconnect) { if(!engine->IsConnected() && !engine->IsInGame()) { m_WaitingDisconnect=false;Start(true); } return; }
    if(!IsVisible())return;
    int w, h; vgui::surface()->GetScreenSize( w, h ); if ( w != m_Width || h != m_Height ) InvalidateLayout( true );
    m_Start->SetEnabled(m_Drop[MAP]->GetItemCount() && !HL2MPCoordinator().Busy());
    const int network = m_Drop[NETWORK]->GetActiveItem();
    char map[64], nav[80]; m_Drop[MAP]->GetText(map,sizeof(map));Q_snprintf(nav,sizeof(nav),"maps/%s.nav",map);
    const int hasNav=filesystem->FileExists(nav,"GAME")?1:0;
    if(m_HintNetwork!=network || m_HintNav!=hasNav)
    {
        m_HintNetwork=network;m_HintNav=hasNav;
        m_NetworkHint->SetText(network==0?"#HL2MP_UI_45529828398d":network==1?"#HL2MP_UI_ba39f3cb790f":"#HL2MP_UI_6e034a4223ca");
        m_BotHint->SetText(hasNav?"#HL2MP_UI_3561de434fea":"#HL2MP_UI_97d791749210");m_Form[1]->Refresh();
    }
}
void CHostServer::Start(bool confirmed)
{
    if(m_WaitingDisconnect || HL2MPCoordinator().Busy())return;
    int slots, time, frags, gravity, bots; char map[64], name[256], password[256];
    m_Drop[MAP]->GetText( map, sizeof( map ) ); ReadUTF8( m_Entry[NAME], name, sizeof( name ) ); ReadUTF8( m_Entry[PASSWORD], password, sizeof( password ) );
    if ( !HostMapName( map ) ) { m_Status->SetText( "#HL2MP_UI_920d0aebe963" ); return; }
    if ( !name[0] ) { m_Status->SetText( "#HL2MP_UI_ced454c5bc44" ); return; }
    if ( !Number( m_Entry[SLOTS], 2, 32, slots ) || !Number( m_Entry[BOTS], 0, 31, bots ) || bots >= slots ) { m_Status->SetText( "#HL2MP_UI_c0e69bfa44ae" ); return; }
    if ( !Number( m_Entry[TIME], 0, 1440, time ) || !Number( m_Entry[FRAGS], 0, 9999, frags ) || !Number( m_Entry[GRAVITY], 0, 4000, gravity ) ) { m_Status->SetText( "#HL2MP_UI_cbb80014bfee" ); return; }
    const char *required[] = { "hostname", "sv_password", "mp_teamplay", "mp_timelimit", "mp_fraglimit", "sv_gravity", "sv_lan", "sv_use_steam_networking", "hl2mp_bot_quota", "hl2mp_bot_difficulty", "sv_bright_models", "sv_hitmarkers", "sv_hitsounds", "sv_cheats", "sv_pausable", "sv_footsteps" };
    for ( int i = 0; i < ARRAYSIZE( required ); ++i ) if ( !ConVarRef( required[i], true ).IsValid() ) { m_Status->SetText( "#HL2MP_UI_63f97c4274f7" ); return; }
    if(engine->IsConnected() && !engine->IsLevelMainMenuBackground())
    {
        if(!confirmed) { for(int i=0;i<DROPS;++i)m_Drop[i]->HideMenu();m_Confirm->Show();return; }
        // Wait for the old server to shut down before changing local server cvars.
        m_WaitingDisconnect=true;engine->ClientCmd_Unrestricted("disconnect");return;
    }
    m_Confirm->Hide();
    // Player-entered names/passwords go directly to cvars, never through the command parser.
    ConVarRef( "hostname" ).SetValue( name ); ConVarRef( "sv_password" ).SetValue( password );
    ConVarRef( "mp_teamplay" ).SetValue( m_Drop[MODE]->GetActiveItem() == 1 );
    ConVarRef( "mp_timelimit" ).SetValue( time ); ConVarRef( "mp_fraglimit" ).SetValue( frags ); ConVarRef( "sv_gravity" ).SetValue( gravity );
    ConVarRef( "mp_friendlyfire" ).SetValue( m_Check[FRIENDLY]->IsSelected() ); ConVarRef( "mp_weaponstay" ).SetValue( m_Check[STAY]->IsSelected() );
    ConVarRef( "mp_forcerespawn" ).SetValue( m_Check[RESPAWN]->IsSelected() ); ConVarRef( "mp_falldamage" ).SetValue( m_Check[FALLDAMAGE]->IsSelected() );
    // This grants server permission; each player's own visual preference remains separate.
    const char *rules[]={"sv_bright_models","sv_hitmarkers","sv_hitsounds","sv_cheats","sv_pausable","sv_footsteps"};
    for(int i=0;i<ARRAYSIZE(rules);++i)ConVarRef(rules[i]).SetValue(m_Check[BRIGHT+i]->IsSelected());
    ConVarRef( "sv_lan" ).SetValue( m_Drop[NETWORK]->GetActiveItem() == 2 ); ConVarRef( "sv_use_steam_networking" ).SetValue( m_Drop[NETWORK]->GetActiveItem() == 0 );
    ConVarRef( "hl2mp_bot_quota_mode" ).SetValue( "normal" ); ConVarRef( "hl2mp_bot_auto_vacate" ).SetValue( 0 );
    ConVarRef( "hl2mp_bot_difficulty" ).SetValue( m_Drop[DIFFICULTY]->GetActiveItem() ); ConVarRef( "hl2mp_bot_quota" ).SetValue( bots );
    // Only the validated map token and bounded integer enter the engine command buffer.
    // Like the engine's Create Server flow, enable real progress before loading
    // the local map. Otherwise GameUI leaves the main menu up during startup.
    HL2MPLoading_SetContext( map, m_Drop[MODE]->GetActiveItem() == 1 ? "listen_team" : "listen", name );
    char command[160]; Q_snprintf( command, sizeof( command ), "disconnect\nmaxplayers %d\nprogress_enable\nmap %s\n", slots, map );
    Close(); engine->ClientCmd_Unrestricted( command );
}
void CHostServer::OnCommand( const char *command )
{
    if(!Q_strcmp(command,"page_server"))Page(0);
    else if(!Q_strcmp(command,"page_rules"))Page(1);
    else if(!Q_strcmp(command,"cancel_start")) { m_Confirm->Hide();m_Start->RequestFocus(); }
    else if(!Q_strcmp(command,"confirm_start"))Start(true);
    else if ( !Q_strcmp( command, "close" ) ) Close();
    else if ( !Q_strcmp( command, "start" ) ) Start();
    else if ( !Q_strcmp( command, "refresh_fonts" ) ) { m_Width = 0; InvalidateLayout( true ); }
    else BaseClass::OnCommand( command );
}
void CHostServer::OnKeyCodeTyped( vgui::KeyCode code )
{
    if(code==KEY_ESCAPE && m_Confirm->IsVisible()) { OnCommand("cancel_start");return; }
    if ( code == KEY_ESCAPE ) { for ( int i = 0; i < DROPS; ++i ) if ( m_Drop[i]->IsDropdownVisible() ) { m_Drop[i]->HideMenu(); return; } Close(); return; }
    BaseClass::OnKeyCodeTyped( code );
}
}
vgui::Panel *HL2MPHost_Create( vgui::Panel *parent ) { return new CHostServer( parent ); }
void HL2MPHost_Open( vgui::Panel *panel ) { static_cast<CHostServer *>( panel )->Open(); }
