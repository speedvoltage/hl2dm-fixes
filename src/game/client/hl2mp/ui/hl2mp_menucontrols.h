#ifndef HL2MP_MENUCONTROLS_H
#define HL2MP_MENUCONTROLS_H
#include "vgui/ISurface.h"
#include "vgui/IInput.h"
#include "vgui/IPanel.h"
#include "vgui_controls/ComboBox.h"
#include "vgui_controls/MenuItem.h"
#include "vgui_controls/Slider.h"
#include "vgui_controls/ScrollBar.h"
#include "vgui_controls/ScrollBarSlider.h"
#include "vgui_controls/TextImage.h"
#include "vgui_controls/CheckButton.h"

namespace HL2MPServerTheme
{
inline Color Orange() { return Color( 232, 112, 27, 255 ); }
inline Color Text() { return Color( 239, 238, 235, 255 ); }
inline Color Muted() { return Color( 170, 170, 167, 255 ); }
inline void Rounded( int x, int y, int w, int h, Color color, int radius = 4 )
{
    radius = MIN( radius, MIN( w, h ) / 2 );
    vgui::ISurface *surface = vgui::surface(); surface->DrawSetColor( color );
    surface->DrawFilledRect( x, y + radius, x + w, y + h - radius );
    for ( int row = 0; row < radius; ++row )
    {
        const float dy = radius - row - 0.5f;
        const int inset = int( radius - sqrtf( radius * radius - dy * dy ) + 0.5f );
        surface->DrawFilledRect( x + inset, y + row, x + w - inset, y + row + 1 );
        surface->DrawFilledRect( x + inset, y + h - row - 1, x + w - inset, y + h - row );
    }
}
}

// Use VGUI's hit-tested panel, so a console/dialog covering a control cannot
// trigger its hover state just because the mouse shares the same coordinates.
inline bool HL2MPControlHighlighted( vgui::Panel *control )
{
    if ( !control->IsEnabled() ) return false;
    const vgui::VPANEL panel = control->GetVPanel();
    const vgui::VPANEL hover = vgui::input()->GetMouseOver(), focus = vgui::input()->GetFocus();
    return hover == panel || ( hover && vgui::ipanel()->HasParent( hover, panel ) ) ||
        focus == panel || ( focus && vgui::ipanel()->HasParent( focus, panel ) );
}

inline void HL2MPPaintSettingRow( vgui::Panel *row, bool highlighted, float scale )
{
    if ( highlighted )
    {
        vgui::surface()->DrawSetColor( Color( 215, 225, 225, 22 ) );
        vgui::surface()->DrawFilledRect( 0, 0, row->GetWide(), row->GetTall() );
        vgui::surface()->DrawSetColor( Color( 235, 164, 79, 255 ) );
        vgui::surface()->DrawFilledRect( 0, 0, MAX( 2, int( 2 * scale ) ), row->GetTall() );
    }
    vgui::surface()->DrawSetColor( Color( 212, 223, 225, highlighted ? 70 : 32 ) );
    vgui::surface()->DrawFilledRect( 0, row->GetTall() - 1, row->GetWide(), row->GetTall() );
}

// Shared by matchmaking and Options so native popups use the same visual language.
class CHL2MPCheckButton : public vgui::CheckButton
{
    DECLARE_CLASS_SIMPLE(CHL2MPCheckButton, vgui::CheckButton);
public:
    CHL2MPCheckButton(vgui::Panel *parent, const char *name, const char *text) : BaseClass(parent,name,text) {}
    void ApplySchemeSettings(vgui::IScheme *scheme) OVERRIDE
    {
        BaseClass::ApplySchemeSettings(scheme);
        SetImageAtIndex(0,NULL,0); SetPaintBackgroundEnabled(false);
        SetHighlightColor(HL2MPServerTheme::Orange());
    }
    void PerformLayout() OVERRIDE
    {
        SetImageAtIndex(0,NULL,0);
        SetTextInset(BoxSize()+10,0);
        BaseClass::PerformLayout();
    }
    void Paint() OVERRIDE
    {
        BaseClass::Paint();
        const int size=BoxSize(), x=2, y=(GetTall()-size)/2;
        vgui::surface()->DrawSetColor(Color(22,24,25,255)); vgui::surface()->DrawFilledRect(x,y,x+size,y+size);
        vgui::surface()->DrawSetColor(IsEnabled() && HL2MPControlHighlighted(this) ? HL2MPServerTheme::Orange() : Color(140,145,145,255));
        vgui::surface()->DrawOutlinedRect(x,y,x+size,y+size);
        if (IsSelected())
        {
            vgui::surface()->DrawSetColor(IsEnabled() ? HL2MPServerTheme::Orange() : Color(110,110,110,255));
            for (int n=0;n<MAX(2,size/9);++n)
            {
                vgui::surface()->DrawLine(x+size/5,y+size/2+n,x+size*2/5,y+size*3/4+n);
                vgui::surface()->DrawLine(x+size*2/5,y+size*3/4+n,x+size*4/5,y+size/4+n);
            }
        }
    }
private:
    int BoxSize() { return clamp(vgui::surface()->GetFontTall(GetFont()),18,30); }
};

class CHL2MPDropdown : public vgui::ComboBox
{
    DECLARE_CLASS_SIMPLE( CHL2MPDropdown, vgui::ComboBox );
public:
    CHL2MPDropdown( vgui::Panel *parent, const char *name ) : BaseClass( parent, name, 8, false ), m_Font( 0 ), m_Scale( 1 ), m_Highlighted( false ), m_ServerStyle( false )
    {
        SetDropdownButtonVisible( false ); SetPaintBorderEnabled( false );
    }
    void ServerStyle() { m_ServerStyle = true; InvalidateLayout( false, true ); }
    void Style( vgui::HFont font, float scale )
    {
        m_Font = font; m_Scale = scale; SetFont( font );
        GetMenu()->SetFont( font ); GetMenu()->SetMenuItemHeight( MAX( 24, int( 34 * scale ) ) );
    }
    void ApplySchemeSettings( vgui::IScheme *scheme ) OVERRIDE
    {
        BaseClass::ApplySchemeSettings( scheme ); SetBorder( NULL ); m_Highlighted = false;
        SetFgColor( Color( 229, 232, 229, 255 ) ); SetBgColor( Color( 25, 31, 34, 180 ) );
        SetSelectionBgColor( Color( 25, 31, 34, 180 ) ); SetSelectionTextColor( Color( 229, 232, 229, 255 ) );
        if ( m_ServerStyle ) { SetBgColor( Color( 29, 29, 29, 255 ) ); SetSelectionBgColor( Color( 29, 29, 29, 255 ) ); }
        GetComboButton()->SetArmedSound( NULL ); GetComboButton()->SetDepressedSound( NULL ); GetComboButton()->SetReleasedSound( NULL );
        if ( m_Font ) Style( m_Font, m_Scale );
    }
    void OnCursorEntered() OVERRIDE
    {
        BaseClass::OnCursorEntered(); if ( IsEnabled() ) vgui::surface()->PlaySound( "ui/buttonrollover.wav" );
    }
    void OnThink() OVERRIDE
    {
        BaseClass::OnThink();
        const bool highlighted = IsEnabled() && ( HL2MPControlHighlighted( this ) || IsDropdownVisible() );
        if ( highlighted != m_Highlighted )
        {
            m_Highlighted = highlighted;
            const Color text = m_ServerStyle ? ( highlighted ? HL2MPServerTheme::Orange() : HL2MPServerTheme::Text() ) : highlighted ? Color( 245, 184, 104, 255 ) : Color( 229, 232, 229, 255 );
            const Color background = m_ServerStyle ? Color( highlighted ? 48 : 29, highlighted ? 48 : 29, highlighted ? 48 : 29, 255 ) : highlighted ? Color( 53, 60, 61, 235 ) : Color( 25, 31, 34, 180 );
            SetFgColor( text ); SetSelectionTextColor( text ); SetBgColor( background ); SetSelectionBgColor( background );
            Repaint();
        }
    }
    void OnKeyCodeTyped( vgui::KeyCode code ) OVERRIDE
    {
        if ( code == KEY_ESCAPE && IsDropdownVisible() ) { HideMenu(); return; }
        BaseClass::OnKeyCodeTyped( code );
        // ComboBox updates its text on arrow keys but otherwise leaves the active item's data stale.
        if ( code == KEY_UP || code == KEY_DOWN || code == KEY_HOME || code == KEY_END || code == KEY_PAGEUP || code == KEY_PAGEDOWN )
        {
            const int item = GetMenu()->GetCurrentlyHighlightedItem();
            if ( item >= 0 ) SilentActivateItem( item );
        }
    }
    void OnShowMenu( vgui::Menu *menu ) OVERRIDE
    {
        menu->InvalidateLayout( true, true ); menu->SetBorder( NULL ); menu->SetBgColor( Color( 25, 31, 34, 255 ) );
        if ( m_Font ) menu->SetFont( m_Font );
        menu->SetMenuItemHeight( MAX( 24, int( 34 * m_Scale ) ) );
        if ( m_ServerStyle ) menu->SetBgColor( Color( 29, 29, 29, 255 ) );
        for ( int i = 0; i < menu->GetItemCount(); ++i )
        {
            vgui::MenuItem *item = menu->GetMenuItem( menu->GetMenuID( i ) );
            item->SetDefaultColor( Color( 225, 229, 226, 255 ), Color( 25, 31, 34, 255 ) );
            item->SetArmedColor( Color( 245, 184, 104, 255 ), Color( 53, 60, 61, 255 ) );
            item->SetDepressedColor( Color( 245, 184, 104, 255 ), Color( 53, 60, 61, 255 ) );
            if ( m_ServerStyle )
            {
                item->SetDefaultColor( HL2MPServerTheme::Text(), Color( 29, 29, 29, 255 ) );
                item->SetArmedColor( HL2MPServerTheme::Text(), HL2MPServerTheme::Orange() );
                item->SetDepressedColor( HL2MPServerTheme::Text(), HL2MPServerTheme::Orange() );
            }
            item->SetArmedSound( "ui/buttonrollover.wav" ); item->SetDepressedSound( "ui/buttonclick.wav" ); item->SetReleasedSound( NULL );
        }
        vgui::surface()->PlaySound( "ui/buttonclick.wav" );
    }
    void Paint() OVERRIDE
    {
        BaseClass::Paint();
        const int x = GetWide() - int( 15 * m_Scale ), y = GetTall() / 2, d = MAX( 3, int( 4 * m_Scale ) );
        vgui::surface()->DrawSetColor( !IsEnabled() ? Color( 104, 111, 113, 255 ) : m_Highlighted ? Color( 245, 184, 104, 255 ) : Color( 225, 229, 226, 255 ) );
        vgui::surface()->DrawLine( x - d, y - 2, x, y + d - 2 ); vgui::surface()->DrawLine( x, y + d - 2, x + d, y - 2 );
        if ( m_Highlighted ) vgui::surface()->DrawFilledRect( 0, GetTall() - 1, GetWide(), GetTall() );
    }
private:
    vgui::HFont m_Font; float m_Scale; bool m_Highlighted, m_ServerStyle;
};

class CHL2MPSettingsButton : public vgui::Button
{
    DECLARE_CLASS_SIMPLE( CHL2MPSettingsButton, vgui::Button );
public:
    CHL2MPSettingsButton( vgui::Panel *parent, const char *text, const char *command ) : BaseClass( parent, command, text, parent, command ), m_Active( false ), m_ServerStyle( false ), m_Primary( false )
    {
        SetCenterWrap( true ); SetTextInset( 6, 0 );
        SetContentAlignment( vgui::Label::a_center ); SetButtonBorderEnabled( false ); DrawFocusBox( false );
    }
    void Active( bool active ) { m_Active = active; Repaint(); }
    void PerformLayout() OVERRIDE
    {
        // VGUI otherwise measures wrapped text using the previous width, which
        // can leave a stale height and clip the first line after a resize.
        GetTextImage()->SetSize( MAX( 1, GetWide() - 12 ), GetTall() );
        GetTextImage()->RecalculateNewLinePositions();
        BaseClass::PerformLayout();
    }
    void ServerStyle( bool primary = false ) { m_ServerStyle = true; m_Primary = primary; InvalidateLayout( false, true ); }
    void SetFont( vgui::HFont font ) OVERRIDE
    {
        BaseClass::SetFont( font ); InvalidateLayout( true );
    }
    void ApplySchemeSettings( vgui::IScheme *scheme ) OVERRIDE
    {
        BaseClass::ApplySchemeSettings( scheme ); SetButtonBorderEnabled( false ); DrawFocusBox( false );
        SetDefaultColor( Color( 204, 211, 210, 255 ), Color( 0, 0, 0, 0 ) );
        SetArmedColor( Color( 255, 235, 199, 255 ), Color( 0, 0, 0, 0 ) );
        SetDepressedColor( Color( 245, 184, 104, 255 ), Color( 0, 0, 0, 0 ) );
        if ( m_ServerStyle )
        {
            SetDefaultColor( HL2MPServerTheme::Text(), Color( 0, 0, 0, 0 ) );
            SetArmedColor( HL2MPServerTheme::Text(), Color( 0, 0, 0, 0 ) );
            SetDepressedColor( HL2MPServerTheme::Text(), Color( 0, 0, 0, 0 ) );
            SetDisabledFgColor1( Color( 106, 106, 104, 255 ) ); SetDisabledFgColor2( Color( 0, 0, 0, 0 ) );
        }
        SetArmedSound( "ui/buttonrollover.wav" ); SetDepressedSound( "ui/buttonclick.wav" ); SetReleasedSound( NULL );
    }
    void PaintBackground() OVERRIDE
    {
        if ( m_ServerStyle )
        {
            const bool hover = IsEnabled() && HL2MPControlHighlighted( this );
            HL2MPServerTheme::Rounded( 0, 0, GetWide(), GetTall(), IsEnabled() && ( m_Active || m_Primary ) ? ( hover ? Color( 248, 132, 39, 255 ) : HL2MPServerTheme::Orange() ) : Color( hover ? 65 : 38, hover ? 65 : 38, hover ? 65 : 38, 255 ) );
            return;
        }
        vgui::surface()->DrawSetColor( Color( 215, 225, 225, IsArmed() || HasFocus() ? 30 : m_Active ? 20 : 0 ) );
        vgui::surface()->DrawFilledRect( 0, 0, GetWide(), GetTall() );
        if ( m_Active ) { vgui::surface()->DrawSetColor( Color( 235, 164, 79, 255 ) ); vgui::surface()->DrawFilledRect( 0, GetTall() - 2, GetWide(), GetTall() ); }
    }
    void PaintBorder() OVERRIDE {}
private:
    bool m_Active, m_ServerStyle, m_Primary;
};

class CHL2MPSettingsSlider : public vgui::Slider
{
    DECLARE_CLASS_SIMPLE( CHL2MPSettingsSlider, vgui::Slider );
public:
    CHL2MPSettingsSlider( vgui::Panel *parent ) : BaseClass( parent, "Value" )
    { SetNumTicks( 0 ); SetThumbWidth( 7 ); SetDragOnRepositionNob( true ); }
    void PaintBackground() OVERRIDE {}
    void Paint() OVERRIDE
    {
        int x, y, w, h, lo, hi; GetTrackRect( x, y, w, h ); GetNobPos( lo, hi );
        const int center = GetTall() / 2;
        vgui::surface()->DrawSetColor( Color( 91, 102, 106, 180 ) ); vgui::surface()->DrawFilledRect( x, center - 2, x + w, center + 2 );
        vgui::surface()->DrawSetColor( !IsEnabled() ? Color( 104, 111, 113, 255 ) : HL2MPControlHighlighted( this ) ? Color( 245, 184, 104, 255 ) : Color( 220, 224, 219, 255 ) );
        vgui::surface()->DrawFilledRect( lo, center - 5, hi, center + 5 );
    }
};

class CHL2MPScrollThumb : public vgui::ScrollBarSlider
{
    DECLARE_CLASS_SIMPLE( CHL2MPScrollThumb, vgui::ScrollBarSlider );
public:
    CHL2MPScrollThumb( vgui::Panel *parent ) : BaseClass( parent, "ScrollThumb", true ) {}
    void PaintBackground() OVERRIDE {}
    void PaintBorder() OVERRIDE {}
    void Paint() OVERRIDE
    {
        int lo, hi; GetNobPos( lo, hi );
        vgui::surface()->DrawSetColor( Color( 185, 201, 206, 35 ) ); vgui::surface()->DrawFilledRect( GetWide() / 2 - 1, 0, GetWide() / 2 + 1, GetTall() );
        vgui::surface()->DrawSetColor( Color( 185, 201, 206, IsCursorOver() ? 220 : 125 ) ); vgui::surface()->DrawFilledRect( GetWide() / 2 - 2, lo, GetWide() / 2 + 2, hi );
    }
};
inline void HL2MPStyleScrollBar( vgui::ScrollBar *bar )
{
    vgui::ScrollBarSlider *old = bar->GetSlider(); bar->SetSlider( new CHL2MPScrollThumb( bar ) ); old->MarkForDeletion();
    bar->SetScrollbarButtonsVisible( false ); bar->SetPaintBackgroundEnabled( false ); bar->SetPaintBorderEnabled( false );
    for ( int i = 0; i < 2; ++i ) bar->GetButton( i )->SetPaintBorderEnabled( false );
}
#endif
