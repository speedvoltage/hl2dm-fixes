#ifndef HL2MP_MENUCONTROLS_H
#define HL2MP_MENUCONTROLS_H
#include "vgui/ISurface.h"
#include "vgui_controls/ComboBox.h"
#include "vgui_controls/MenuItem.h"
#include "vgui_controls/Slider.h"
#include "vgui_controls/ScrollBar.h"
#include "vgui_controls/ScrollBarSlider.h"

// Shared by matchmaking and Options so native popups use the same visual language.
class CHL2MPDropdown : public vgui::ComboBox
{
    DECLARE_CLASS_SIMPLE( CHL2MPDropdown, vgui::ComboBox );
public:
    CHL2MPDropdown( vgui::Panel *parent, const char *name ) : BaseClass( parent, name, 8, false ), m_Font( 0 ), m_Scale( 1 )
    {
        SetDropdownButtonVisible( false ); SetPaintBorderEnabled( false );
    }
    void Style( vgui::HFont font, float scale )
    {
        m_Font = font; m_Scale = scale; SetFont( font );
        GetMenu()->SetFont( font ); GetMenu()->SetMenuItemHeight( MAX( 24, int( 34 * scale ) ) );
    }
    void ApplySchemeSettings( vgui::IScheme *scheme ) OVERRIDE
    {
        BaseClass::ApplySchemeSettings( scheme ); SetBorder( NULL );
        SetFgColor( Color( 229, 232, 229, 255 ) ); SetBgColor( Color( 25, 31, 34, 180 ) );
        SetSelectionBgColor( Color( 25, 31, 34, 180 ) ); SetSelectionTextColor( Color( 229, 232, 229, 255 ) );
        GetComboButton()->SetArmedSound( NULL ); GetComboButton()->SetDepressedSound( NULL ); GetComboButton()->SetReleasedSound( NULL );
        if ( m_Font ) Style( m_Font, m_Scale );
    }
    void OnCursorEntered() OVERRIDE
    {
        BaseClass::OnCursorEntered(); if ( IsEnabled() ) vgui::surface()->PlaySound( "ui/buttonrollover.wav" );
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
        for ( int i = 0; i < menu->GetItemCount(); ++i )
        {
            vgui::MenuItem *item = menu->GetMenuItem( menu->GetMenuID( i ) );
            item->SetDefaultColor( Color( 225, 229, 226, 255 ), Color( 25, 31, 34, 255 ) );
            item->SetArmedColor( Color( 245, 184, 104, 255 ), Color( 53, 60, 61, 255 ) );
            item->SetDepressedColor( Color( 245, 184, 104, 255 ), Color( 53, 60, 61, 255 ) );
            item->SetArmedSound( "ui/buttonrollover.wav" ); item->SetDepressedSound( "ui/buttonclick.wav" ); item->SetReleasedSound( NULL );
        }
        vgui::surface()->PlaySound( "ui/buttonclick.wav" );
    }
    void Paint() OVERRIDE
    {
        BaseClass::Paint();
        const int x = GetWide() - int( 15 * m_Scale ), y = GetTall() / 2, d = MAX( 3, int( 4 * m_Scale ) );
        vgui::surface()->DrawSetColor( IsEnabled() ? Color( 225, 229, 226, 255 ) : Color( 104, 111, 113, 255 ) );
        vgui::surface()->DrawLine( x - d, y - 2, x, y + d - 2 ); vgui::surface()->DrawLine( x, y + d - 2, x + d, y - 2 );
    }
private:
    vgui::HFont m_Font; float m_Scale;
};

class CHL2MPSettingsButton : public vgui::Button
{
    DECLARE_CLASS_SIMPLE( CHL2MPSettingsButton, vgui::Button );
public:
    CHL2MPSettingsButton( vgui::Panel *parent, const char *text, const char *command ) : BaseClass( parent, command, text, parent, command ), m_Active( false )
    {
        SetContentAlignment( vgui::Label::a_center ); SetButtonBorderEnabled( false ); DrawFocusBox( false );
    }
    void Active( bool active ) { m_Active = active; Repaint(); }
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
        SetArmedSound( "ui/buttonrollover.wav" ); SetDepressedSound( "ui/buttonclick.wav" ); SetReleasedSound( NULL );
    }
    void PaintBackground() OVERRIDE
    {
        vgui::surface()->DrawSetColor( Color( 215, 225, 225, IsArmed() || HasFocus() ? 30 : m_Active ? 20 : 0 ) );
        vgui::surface()->DrawFilledRect( 0, 0, GetWide(), GetTall() );
        if ( m_Active ) { vgui::surface()->DrawSetColor( Color( 235, 164, 79, 255 ) ); vgui::surface()->DrawFilledRect( 0, GetTall() - 2, GetWide(), GetTall() ); }
    }
    void PaintBorder() OVERRIDE {}
private:
    bool m_Active;
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
        vgui::surface()->DrawSetColor( IsEnabled() ? Color( 220, 224, 219, 255 ) : Color( 104, 111, 113, 255 ) );
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
