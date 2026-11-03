#include "cbase.h"
#include "hl2mp_dialogs.h"
#include "hl2mp_menucontrols.h"
#include "GameUI/IGameUI.h"
#include "ienginevgui.h"
#include "vgui/IScheme.h"
#include "vgui_controls/Frame.h"
#include "tier0/memdbgon.h"

namespace
{
class CQuitDialog : public vgui::Frame
{
    DECLARE_CLASS_SIMPLE( CQuitDialog, vgui::Frame );
public:
    explicit CQuitDialog( IGameUI *gameUI ) : BaseClass( NULL, "HL2MPQuitDialog" ), m_GameUI( gameUI )
    {
        SetParent( enginevgui->GetPanel( PANEL_GAMEUIDLL ) );
        SetProportional( false ); SetTitleBarVisible( false ); SetCloseButtonVisible( false );
        SetSizeable( false ); SetMoveable( false ); SetDeleteSelfOnClose( false );
        SetPaintBorderEnabled( false );
        m_Title = new vgui::Label( this, "Title", "#HL2MP_QuitTitle" );
        m_Body = new vgui::Label( this, "Body", "#HL2MP_QuitBody" ); m_Body->SetWrap( true );
        m_Yes = new CHL2MPSettingsButton( this, "#HL2MP_Quit", "confirm" ); m_Yes->ServerStyle( true );
        m_No = new CHL2MPSettingsButton( this, "#HL2MP_Cancel", "cancel" ); m_No->ServerStyle();
        m_Title->SetContentAlignment( vgui::Label::a_northwest ); m_Body->SetContentAlignment( vgui::Label::a_northwest );
        SetVisible( false );
    }
    void Open( bool keepEscapeBlocked )
    {
        if ( IsVisible() ) return;
        m_KeepEscapeBlocked = keepEscapeBlocked;
        engine->ClientCmd_Unrestricted( "gameui_preventescape" );
        InvalidateLayout( true, true ); DoModal(); m_No->RequestFocus();
    }
    void OnClose() OVERRIDE
    {
        engine->ClientCmd_Unrestricted( m_KeepEscapeBlocked ? "gameui_preventescape" : "gameui_allowescape" );
        BaseClass::OnClose();
    }
    void ApplySchemeSettings( vgui::IScheme *scheme ) OVERRIDE
    {
        BaseClass::ApplySchemeSettings( scheme ); SetBorder( NULL ); DisableFadeEffect();
        m_Title->SetFont( scheme->GetFont( "Trebuchet24", false ) ); m_Title->SetFgColor( HL2MPServerTheme::Text() );
        m_Body->SetFont( scheme->GetFont( "DefaultLarge", false ) ); m_Body->SetFgColor( Color( 211, 211, 208, 255 ) );
        m_Yes->SetFont( scheme->GetFont( "DefaultLarge", false ) ); m_No->SetFont( scheme->GetFont( "DefaultLarge", false ) );
    }
    void PerformLayout() OVERRIDE
    {
        BaseClass::PerformLayout();
        int w, h; vgui::surface()->GetScreenSize( w, h ); SetBounds( 0, 0, w, h );
        const int width = MIN( 620, w - 48 ), pad = 28;
        m_X = ( w - width ) / 2; m_W = width;
        m_Body->SetSize( width - pad * 2, 150 );
        m_Body->InvalidateLayout( true );
        int tw, th; m_Body->GetContentSize( tw, th );
        m_H = MAX( 218, th + 154 ); m_Y = ( h - m_H ) / 2;
        m_Title->SetBounds( m_X + pad, m_Y + 23, width - pad * 2, 32 );
        m_Body->SetBounds( m_X + pad, m_Y + 76, width - pad * 2, th + 12 );
        const int button = MIN( 160, ( width - pad * 2 - 12 ) / 2 );
        m_No->SetBounds( m_X + width - pad - button * 2 - 12, m_Y + m_H - 66, button, 40 );
        m_Yes->SetBounds( m_X + width - pad - button, m_Y + m_H - 66, button, 40 );
    }
    void PaintBackground() OVERRIDE
    {
        vgui::surface()->DrawSetColor( Color( 0, 0, 0, 145 ) ); vgui::surface()->DrawFilledRect( 0, 0, GetWide(), GetTall() );
        HL2MPServerTheme::Rounded( m_X, m_Y, m_W, m_H, Color( 39, 39, 39, 255 ), 5 );
        vgui::surface()->DrawSetColor( HL2MPServerTheme::Orange() );
        vgui::surface()->DrawFilledRect( m_X + 28, m_Y + 59, m_X + 66, m_Y + 61 );
        vgui::surface()->DrawSetColor( Color( 255, 255, 255, 18 ) );
        vgui::surface()->DrawFilledRect( m_X + 66, m_Y + 60, m_X + m_W - 28, m_Y + 61 );
    }
    void OnCommand( const char *command ) OVERRIDE
    {
        if ( !Q_strcmp( command, "confirm" ) ) { CloseModal(); m_GameUI->SendMainMenuCommand( "QuitNoConfirm" ); }
        else if ( !Q_strcmp( command, "cancel" ) ) CloseModal();
        else BaseClass::OnCommand( command );
    }
    void OnKeyCodeTyped( vgui::KeyCode code ) OVERRIDE
    {
        if ( code == KEY_ESCAPE ) { CloseModal(); return; }
        BaseClass::OnKeyCodeTyped( code );
    }
private:
    IGameUI *m_GameUI;
    vgui::Label *m_Title, *m_Body;
    CHL2MPSettingsButton *m_Yes, *m_No;
    bool m_KeepEscapeBlocked = false;
    int m_X = 0, m_Y = 0, m_W = 0, m_H = 0;
};
CQuitDialog *s_Quit = NULL;
}
void HL2MPDialogs_ShowQuit( IGameUI *gameUI, bool keepEscapeBlocked ) { if ( !s_Quit ) s_Quit = new CQuitDialog( gameUI ); s_Quit->Open( keepEscapeBlocked ); }
void HL2MPDialogs_Update( IGameUI *gameUI, bool keepEscapeBlocked )
{
    // Window-close and the engine's quit prompt can bypass our menu command.
    // Close the known stock dialog through its public message interface so it
    // releases modal/paint restrictions, then use the same localized prompt.
    for ( int i = 0; i < vgui::surface()->GetPopupCount(); ++i )
    {
        const vgui::VPANEL panel = vgui::surface()->GetPopup( i );
        if ( vgui::ipanel()->IsVisible( panel ) && !Q_strcmp( vgui::ipanel()->GetClassName( panel ), "CQuitQueryBox" ) )
        {
            KeyValuesAD close( "Close" ); vgui::ipanel()->SendMessage( panel, close, 0 );
            vgui::ipanel()->SetVisible( panel, false );
            HL2MPDialogs_ShowQuit( gameUI, keepEscapeBlocked );
            break;
        }
    }
}
void HL2MPDialogs_Destroy() { delete s_Quit; s_Quit = NULL; }
