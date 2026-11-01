#include "cbase.h"
#include "cdll_client_int.h"
#include "hl2mp_options.h"
#include "hl2mp_menucontrols.h"
#include "hl2mp_playermodels.h"
#include "matsys_controls/mdlpanel.h"
#include "animation.h"
#include "IGameUIFuncs.h"
#include "inputsystem/iinputsystem.h"
#include "materialsystem/materialsystem_config.h"
#include "vgui/IVGui.h"
#include "vgui/IInput.h"
#include "vgui/IScheme.h"
#include "vgui_controls/EditablePanel.h"
#include "vgui_controls/PanelListPanel.h"
#include "vgui_controls/TextEntry.h"
#include "tier1/utlstring.h"
#include "tier0/memdbgon.h"

namespace
{
enum Page { DISPLAY, ADVANCED, AUDIO, GAME, MOUSE, BINDINGS, MULTIPLAYER };
enum Kind { CHOICE, SLIDER, TEXT, RESOLUTION, WINDOW_MODE };
struct SettingDef
{
    Page page; Kind kind; const char *label, *cvar, *choices; float low, high, step;
};
static const SettingDef s_Settings[] = {
    { DISPLAY, RESOLUTION, "Resolution", "", "", 0, 0, 0 },
    { DISPLAY, WINDOW_MODE, "Display mode", "", "FULLSCREEN=0|WINDOWED=1", 0, 0, 0 },
    { DISPLAY, SLIDER, "Display gamma", "mat_monitorgamma", "", 1.6f, 2.6f, 0.01f },
    { DISPLAY, CHOICE, "Frame rate limit", "fps_max", "60 FPS=60|120 FPS=120|144 FPS=144|165 FPS=165|240 FPS=240|300 FPS=300|UNLIMITED=0", 0, 0, 0 },
    { DISPLAY, CHOICE, "Menu camera motion", "mm_menu_motion", "DISABLED=0|ENABLED=1", 0, 0, 0 },
    { ADVANCED, CHOICE, "Vertical sync", "mat_vsync", "DISABLED=0|ENABLED=1", 0, 0, 0 },
    { ADVANCED, CHOICE, "Anti-aliasing", "mat_antialias", "NONE=0|2x MSAA=2|4x MSAA=4|8x MSAA=8", 0, 0, 0 },
    { ADVANCED, CHOICE, "Texture detail", "mat_picmip", "LOW=2|MEDIUM=1|HIGH=0|VERY HIGH=-1", 0, 0, 0 },
    { ADVANCED, CHOICE, "Model detail", "r_rootlod", "LOW=2|MEDIUM=1|HIGH=0", 0, 0, 0 },
    { ADVANCED, CHOICE, "Texture filtering", "mat_forceaniso", "BILINEAR / TRILINEAR=1|2x ANISOTROPIC=2|4x ANISOTROPIC=4|8x ANISOTROPIC=8|16x ANISOTROPIC=16", 0, 0, 0 },
    { ADVANCED, CHOICE, "Trilinear filtering", "mat_trilinear", "DISABLED=0|ENABLED=1", 0, 0, 0 },
    { ADVANCED, CHOICE, "High dynamic range", "mat_hdr_level", "NONE=0|BLOOM ONLY=1|FULL=2", 0, 0, 0 },
    { ADVANCED, CHOICE, "Detailed shadows", "r_shadowrendertotexture", "DISABLED=0|ENABLED=1", 0, 0, 0 },
    { ADVANCED, CHOICE, "Water reflections", "r_waterforceexpensive", "SIMPLE=0|REFLECT WORLD=1", 0, 0, 0 },
    { ADVANCED, CHOICE, "Reflect objects in water", "r_waterforcereflectentities", "DISABLED=0|ENABLED=1", 0, 0, 0 },
    { ADVANCED, CHOICE, "Bump mapping", "mat_bumpmap", "DISABLED=0|ENABLED=1", 0, 0, 0 },
    { ADVANCED, CHOICE, "Specular reflections", "mat_specular", "DISABLED=0|ENABLED=1", 0, 0, 0 },
    { ADVANCED, CHOICE, "Color correction", "mat_colorcorrection", "DISABLED=0|ENABLED=1", 0, 0, 0 },
    { ADVANCED, CHOICE, "Motion blur", "mat_motion_blur_enabled", "DISABLED=0|ENABLED=1", 0, 0, 0 },
    { AUDIO, SLIDER, "Master volume", "volume", "%", 0, 1, 0.01f },
    { AUDIO, SLIDER, "Music volume", "snd_musicvolume", "%", 0, 1, 0.01f },
    { AUDIO, CHOICE, "Speaker configuration", "snd_surround_speakers", "HEADPHONES=0|STEREO=2|QUAD=4|5.1 SURROUND=5|7.1 SURROUND=7", 0, 0, 0 },
    { AUDIO, CHOICE, "High quality sound", "snd_pitchquality", "DISABLED=0|ENABLED=1", 0, 0, 0 },
    { AUDIO, CHOICE, "Mute when unfocused", "snd_mute_losefocus", "NO=0|YES=1", 0, 0, 0 },
    { AUDIO, CHOICE, "Voice chat", "voice_enable", "DISABLED=0|ENABLED=1", 0, 0, 0 },
    { AUDIO, SLIDER, "Incoming voice volume", "voice_scale", "%", 0, 1, 0.01f },
    { AUDIO, CHOICE, "Closed captions", "closecaption", "DISABLED=0|ENABLED=1", 0, 0, 0 },
    { GAME, CHOICE, "Developer console", "con_enable", "DISABLED=0|ENABLED=1", 0, 0, 0 },
    { GAME, CHOICE, "Crosshair", "crosshair", "HIDDEN=0|VISIBLE=1", 0, 0, 0 },
    { GAME, CHOICE, "Fast weapon switching", "hud_fastswitch", "DISABLED=0|ENABLED=1", 0, 0, 0 },
    { GAME, CHOICE, "Switch to picked-up weapons", "cl_autowepswitch", "NO=0|YES=1", 0, 0, 0 },
    { GAME, CHOICE, "Frame rate counter", "cl_showfps", "HIDDEN=0|SIMPLE=1|SMOOTHED=2", 0, 0, 0 },
    { GAME, CHOICE, "Network graph", "net_graph", "HIDDEN=0|BASIC=1|DETAILED=2|TEXT=3", 0, 0, 0 },
    { MOUSE, SLIDER, "Mouse sensitivity", "sensitivity", "", 0.1f, 10, 0.01f },
    { MOUSE, CHOICE, "Raw mouse input", "m_rawinput", "DISABLED=0|ENABLED=1", 0, 0, 0 },
    { MOUSE, CHOICE, "Invert mouse", "m_pitch", "NO=0.022|YES=-0.022", 0, 0, 0 },
    { MOUSE, CHOICE, "Mouse filtering", "m_filter", "DISABLED=0|ENABLED=1", 0, 0, 0 },
    { MOUSE, CHOICE, "Mouse acceleration", "m_customaccel", "DISABLED=0|ENABLED=3", 0, 0, 0 },
    { MOUSE, SLIDER, "Zoom sensitivity", "zoom_sensitivity_ratio", "", 0.1f, 3, 0.01f },
    { MULTIPLAYER, TEXT, "Player name", "name", "", 0, 0, 0 },
    { MULTIPLAYER, CHOICE, "Player model", "cl_playermodel", "", 0, 0, 0 },
    { MULTIPLAYER, SLIDER, "Field of view", "fov_desired", "", 75, 90, 1 },
    { MULTIPLAYER, CHOICE, "Starting weapon", "cl_defaultweapon", "GRAVITY GUN=weapon_physcannon|PISTOL=weapon_pistol|SMG=weapon_smg1|FRAG GRENADE=weapon_frag|CROWBAR=weapon_crowbar|STUNSTICK=weapon_stunstick", 0, 0, 0 },
    { MULTIPLAYER, CHOICE, "Server downloads", "cl_downloadfilter", "ALL FILES=all|EXCLUDE SOUNDS=nosounds|MAPS ONLY=mapsonly|NONE=none", 0, 0, 0 },
    { MULTIPLAYER, CHOICE, "Upload my spray", "cl_allowupload", "NO=0|YES=1", 0, 0, 0 },
};
struct BindingDef { const char *label, *command, *key; };
static const BindingDef s_Bindings[] = {
    { "Move forward", "+forward", "w" }, { "Move backward", "+back", "s" },
    { "Move left", "+moveleft", "a" }, { "Move right", "+moveright", "d" },
    { "Jump", "+jump", "SPACE" }, { "Duck", "+duck", "CTRL" },
    { "Sprint", "+speed", "SHIFT" }, { "Walk", "+walk", "ALT" },
    { "Use", "+use", "e" }, { "Primary attack", "+attack", "MOUSE1" },
    { "Secondary attack", "+attack2", "MOUSE2" }, { "Reload", "+reload", "r" },
    { "Gravity gun", "phys_swap", "g" }, { "Last weapon", "lastinv", "q" },
    { "Next weapon", "invnext", "MWHEELDOWN" }, { "Previous weapon", "invprev", "MWHEELUP" },
    { "Weapon slot 1", "slot1", "1" }, { "Weapon slot 2", "slot2", "2" },
    { "Weapon slot 3", "slot3", "3" }, { "Weapon slot 4", "slot4", "4" },
    { "Weapon slot 5", "slot5", "5" }, { "Weapon slot 6", "slot6", "6" },
    { "Flashlight", "impulse 100", "f" }, { "Suit zoom", "+zoom", "z" },
    { "Scoreboard", "+showscores", "TAB" }, { "Voice chat", "+voicerecord", "k" },
    { "Public chat", "messagemode", "y" }, { "Team chat", "messagemode2", "u" },
    { "Spray", "impulse 201", "t" }, { "Screenshot", "jpeg", "F5" },
    { "Developer console", "toggleconsole", "`" }, { "Change team", "changeteam", "F2" }
};

static bool SameValue( const char *a, const char *b )
{
    if ( !Q_strcmp( a, b ) ) return true;
    char *ae, *be; double av = strtod( a, &ae ), bv = strtod( b, &be );
    return ae != a && be != b && !*ae && !*be && fabs( av - bv ) < 0.00001;
}
static vgui::Label *Label( vgui::Panel *parent, const char *text )
{
    vgui::Label *label = new vgui::Label( parent, "Label", text );
    label->SetMouseInputEnabled( false ); label->SetKeyBoardInputEnabled( false );
    label->SetContentAlignment( vgui::Label::a_west ); return label;
}
static void AddChoice( CHL2MPDropdown *combo, const char *label, const char *value )
{
    KeyValues *data = new KeyValues( "Choice", "value", value ); combo->AddItem( label, data ); data->deleteThis();
}
struct Setting { CUtlString original, draft; bool available; };

class CSettingRow : public vgui::EditablePanel
{
    DECLARE_CLASS_SIMPLE( CSettingRow, vgui::EditablePanel );
public:
    CSettingRow( vgui::Panel *parent, int index, const Setting &value ) : BaseClass( parent, "Setting" ), m_Index( index ), m_Combo( NULL ), m_Slider( NULL ), m_Text( NULL ), m_Amount( NULL ), m_Scale( 1 ), m_InitialSlider( 0 ), m_InitialValue( value.draft )
    {
        SetPaintBackgroundEnabled( true ); const SettingDef &def = s_Settings[index]; m_Label = Label( this, def.label );
        if ( def.kind == SLIDER )
        {
            m_Slider = new CHL2MPSettingsSlider( this ); m_Slider->SetRange( int( def.low / def.step + 0.5f ), int( def.high / def.step + 0.5f ) );
            m_Slider->SetValue( int( atof( value.draft.String() ) / def.step + 0.5f ), false ); m_InitialSlider = m_Slider->GetValue();
            m_Amount = Label( this, "" ); m_Slider->SetEnabled( value.available );
        }
        else if ( def.kind == TEXT )
        {
            m_Text = new vgui::TextEntry( this, "Value" ); m_Text->SetMaximumCharCount( 63 ); m_Text->SetText( value.draft.String() ); m_Text->SetEnabled( value.available );
        }
        else
        {
            m_Combo = new CHL2MPDropdown( this, "Value" );
            if ( def.kind == RESOLUTION )
            {
                CUtlVector<MaterialVideoMode_t> modes;
                for ( int i = 0; i < materials->GetModeCount( materials->GetCurrentAdapter() ); ++i )
                {
                    MaterialVideoMode_t mode; materials->GetModeInfo( materials->GetCurrentAdapter(), i, mode );
                    if ( mode.m_Width < 640 || mode.m_Height < 480 ) continue;
                    bool duplicate = false; FOR_EACH_VEC( modes, j ) if ( modes[j].m_Width == mode.m_Width && modes[j].m_Height == mode.m_Height ) duplicate = true;
                    if ( !duplicate ) modes.AddToTail( mode );
                }
                FOR_EACH_VEC( modes, i )
                {
                    char name[64], size[32]; Q_snprintf( size, sizeof( size ), "%dx%d", modes[i].m_Width, modes[i].m_Height );
                    const float aspect = float( modes[i].m_Width ) / modes[i].m_Height;
                    Q_snprintf( name, sizeof( name ), "%s   %s", size, fabs( aspect - 16.0f / 9 ) < .02f ? "16:9" : fabs( aspect - 16.0f / 10 ) < .02f ? "16:10" : fabs( aspect - 4.0f / 3 ) < .02f ? "4:3" : "" );
                    AddChoice( m_Combo, name, size );
                }
            }
            else if ( !Q_strcmp( def.cvar, "cl_playermodel" ) )
            {
                for ( int i = 0; i < ARRAYSIZE( s_PlayerModels ); ++i ) AddChoice( m_Combo, s_PlayerModels[i].name, s_PlayerModels[i].path );
            }
            else
            {
                char choices[1024]; Q_strncpy( choices, def.choices, sizeof( choices ) ); char *part = choices;
                while ( part && *part )
                {
                    char *next = strchr( part, '|' ); if ( next ) *next++ = 0;
                    char *equal = strchr( part, '=' );
                    if ( equal )
                    {
                        *equal++ = 0;
                        if ( Q_strcmp( def.cvar, "mat_antialias" ) || atoi( equal ) == 0 || materials->SupportsMSAAMode( atoi( equal ) ) ) AddChoice( m_Combo, part, equal );
                    }
                    part = next;
                }
            }
            int selected = -1;
            for ( int i = 0; i < m_Combo->GetItemCount(); ++i )
            {
                const char *choice = m_Combo->GetItemUserData( m_Combo->GetItemIDFromRow( i ) )->GetString( "value" );
                if ( SameValue( choice, value.draft.String() ) || ( !Q_strcmp( def.cvar, "cl_playermodel" ) && !Q_stricmp( choice, value.draft.String() ) ) ) selected = i;
            }
            // Keep custom/unsupported current values intact until explicitly changed.
            if ( selected < 0 ) { selected = m_Combo->GetItemCount(); AddChoice( m_Combo, value.available ? value.draft.String() : "UNAVAILABLE", value.draft.String() ); }
            m_Combo->SilentActivateItemByRow( selected ); m_Combo->SetEnabled( value.available );
        }
    }
    int Index() const { return m_Index; }
    void CloseDropdown() { if ( m_Combo ) m_Combo->HideMenu(); }
    bool DropdownOpen() const { return m_Combo && m_Combo->GetMenu()->IsVisible(); }
    CUtlString Value()
    {
        if ( m_Combo )
        {
            KeyValues *data = m_Combo->GetActiveItemUserData(); const char *value = data ? data->GetString( "value" ) : m_InitialValue.String();
            // Model paths are case-insensitive; merely opening this page must not dirty the settings.
            if ( !Q_strcmp( s_Settings[m_Index].cvar, "cl_playermodel" ) && !Q_stricmp( value, m_InitialValue.String() ) ) return m_InitialValue;
            return value;
        }
        char value[256];
        if ( m_Text ) { m_Text->GetText( value, sizeof( value ) ); return value; }
        if ( m_Slider->GetValue() == m_InitialSlider ) return m_InitialValue;
        Q_snprintf( value, sizeof( value ), "%.4f", m_Slider->GetValue() * s_Settings[m_Index].step ); return value;
    }
    void Style( float scale, vgui::HFont font, vgui::HFont small )
    {
        m_Scale = scale; SetTall( int( 52 * scale ) ); m_Label->SetFont( font );
        m_Label->SetFgColor( Color( 206, 214, 213, 255 ) );
        if ( m_Combo ) m_Combo->Style( small, scale );
        if ( m_Text ) { m_Text->SetFont( font ); m_Text->SetBgColor( Color( 24, 31, 34, 160 ) ); m_Text->SetFgColor( Color( 236, 237, 230, 255 ) ); m_Text->SetBorder( NULL ); }
        if ( m_Amount ) { m_Amount->SetFont( small ); m_Amount->SetFgColor( Color( 221, 227, 225, 255 ) ); }
        InvalidateLayout( true );
    }
    void PerformLayout() OVERRIDE
    {
        BaseClass::PerformLayout(); const int w = GetWide(), h = GetTall(), valueW = int( w * .43f ), left = w - valueW - int( 12 * m_Scale );
        m_Label->SetBounds( int( 8 * m_Scale ), 0, left - int( 16 * m_Scale ), h );
        if ( m_Combo ) m_Combo->SetBounds( left, int( 9 * m_Scale ), valueW, h - int( 18 * m_Scale ) );
        if ( m_Text ) m_Text->SetBounds( left, int( 9 * m_Scale ), valueW, h - int( 18 * m_Scale ) );
        if ( m_Slider )
        {
            m_Slider->SetBounds( left, 0, valueW - int( 56 * m_Scale ), h );
            m_Amount->SetBounds( w - int( 60 * m_Scale ), 0, int( 54 * m_Scale ), h );
        }
    }
    void OnThink() OVERRIDE
    {
        if ( m_Amount )
        {
            const float value = atof( Value().String() );
            m_Amount->SetText( s_Settings[m_Index].choices[0] == '%' ? VarArgs( "%.0f%%", value * 100 ) : s_Settings[m_Index].step < 1 ? VarArgs( "%.2f", value ) : VarArgs( "%.0f", value ) );
        }
    }
    void PaintBackground() OVERRIDE
    {
        vgui::surface()->DrawSetColor( Color( 212, 223, 225, 32 ) ); vgui::surface()->DrawFilledRect( 0, GetTall() - 1, GetWide(), GetTall() );
    }
private:
    int m_Index; CHL2MPDropdown *m_Combo; CHL2MPSettingsSlider *m_Slider; vgui::TextEntry *m_Text;
    vgui::Label *m_Label, *m_Amount; float m_Scale; int m_InitialSlider; CUtlString m_InitialValue;
};

class CBindingRow : public vgui::EditablePanel
{
    DECLARE_CLASS_SIMPLE( CBindingRow, vgui::EditablePanel );
public:
    CBindingRow( vgui::Panel *parent, vgui::Panel *target, int action ) : BaseClass( parent, "Binding" ), m_Scale( 1 )
    {
        m_Label = Label( this, s_Bindings[action].label );
        for ( int slot = 0; slot < 2; ++slot )
        {
            m_Keys[slot] = new CHL2MPSettingsButton( this, "", VarArgs( "bind_%d_%d", action, slot ) );
            m_Keys[slot]->RemoveActionSignalTarget( this );
            m_Keys[slot]->AddActionSignalTarget( target );
        }
    }
    void Style( float scale, vgui::HFont font, vgui::HFont small, const char *first, const char *second )
    {
        m_Scale = scale; SetTall( int( 48 * scale ) ); m_Label->SetFont( font ); m_Label->SetFgColor( Color( 206, 214, 213, 255 ) );
        m_Keys[0]->SetText( first ); m_Keys[1]->SetText( second );
        for ( int i = 0; i < 2; ++i ) m_Keys[i]->SetFont( small ); InvalidateLayout( true );
    }
    void PerformLayout() OVERRIDE
    {
        const int valueW = GetWide() * 43 / 100, left = GetWide() - valueW;
        m_Label->SetBounds( int( 8 * m_Scale ), 0, left - 12, GetTall() );
        for ( int i = 0; i < 2; ++i ) m_Keys[i]->SetBounds( left + i * valueW / 2, 2, valueW / 2 - 6, GetTall() - 4 );
    }
    void PaintBackground() OVERRIDE
    {
        vgui::surface()->DrawSetColor( Color( 212, 223, 225, 32 ) ); vgui::surface()->DrawFilledRect( 0, GetTall() - 1, GetWide(), GetTall() );
    }
private:
    float m_Scale; vgui::Label *m_Label; CHL2MPSettingsButton *m_Keys[2];
};

class COptionsOverlay : public vgui::EditablePanel
{
    DECLARE_CLASS_SIMPLE( COptionsOverlay, vgui::EditablePanel );
public:
    COptionsOverlay( vgui::Panel *parent ) : BaseClass( parent, "Confirm" ), capture( false )
    {
        title = Label( this, "" ); body = Label( this, "" ); body->SetWrap( true );
        yes = new CHL2MPSettingsButton( this, "", "confirm_yes" ); no = new CHL2MPSettingsButton( this, "", "confirm_no" );
        discard = new CHL2MPSettingsButton( this, "DISCARD AND CLOSE", "confirm_discard" );
        SetVisible( false ); SetZPos( 200 );
    }
    void OnCommand( const char *command ) OVERRIDE { GetParent()->OnCommand( command ); }
    void SendKey( ButtonCode_t code ) { PostMessage( GetParent(), new KeyValues( "CaptureKey", "code", int( code ) ) ); }
    void OnKeyCodeTyped( vgui::KeyCode code ) OVERRIDE
    {
        if ( capture ) SendKey( code ); else if ( code == KEY_ESCAPE ) OnCommand( "confirm_no" ); else BaseClass::OnKeyCodeTyped( code );
    }
    void OnMousePressed( vgui::MouseCode code ) OVERRIDE { if ( capture ) SendKey( code ); else BaseClass::OnMousePressed( code ); }
    void OnMouseWheeled( int delta ) OVERRIDE { if ( capture ) SendKey( delta > 0 ? MOUSE_WHEEL_UP : MOUSE_WHEEL_DOWN ); }
    void PaintBackground() OVERRIDE
    {
        vgui::surface()->DrawSetColor( Color( 3, 7, 10, 238 ) ); vgui::surface()->DrawFilledRect( 0, 0, GetWide(), GetTall() );
    }
    vgui::Label *title, *body; CHL2MPSettingsButton *yes, *no, *discard; bool capture;
};

class CPlayerPreview : public CMDLPanel
{
    DECLARE_CLASS_SIMPLE( CPlayerPreview, CMDLPanel );
public:
    CPlayerPreview( vgui::Panel *parent ) : BaseClass( parent, "PlayerPreview" ), m_Model( -1 ), m_SequencePending( false )
    {
        SetVisible( false ); SetGroundGrid( false ); SetIgnoreDoubleClick( true ); SetKeyBoardInputEnabled( false );
        SetCameraFOV( 35 ); SetLookAtCamera( true );
    }
    void ApplySchemeSettings( vgui::IScheme *scheme ) OVERRIDE
    {
        BaseClass::ApplySchemeSettings( scheme ); SetBorder( NULL ); SetBackgroundColor( Color( 18, 26, 30, 255 ) );
    }
    void SelectModel( int index )
    {
        if ( index == m_Model ) return;
        m_Model = index; ClearMergeMDLs(); SetMDL( s_PlayerModels[index].path );
        SetMergeMDL( "models/weapons/w_smg1.mdl" ); m_SequencePending = true;
        SetModelAnglesAndPosition( QAngle( 0, 170, 0 ), vec3_origin );
        ResetCameraPivot(); LookAt( Vector( 0, 0, 37 ), 30 );
    }
    void OnTick() OVERRIDE
    {
        if ( !IsVisible() || !GetParent()->IsVisible() ) return;
        BaseClass::OnTick();
        // Included animation models may finish loading a frame after SetMDL.
        if ( m_SequencePending && m_RootMDL.m_pStudioHdr && m_RootMDL.m_pStudioHdr->SequencesAvailable() )
        {
            int sequence = LookupSequence( m_RootMDL.m_pStudioHdr, "idle_smg1" );
            if ( sequence < 0 ) sequence = SelectWeightedSequence( m_RootMDL.m_pStudioHdr, ACT_IDLE );
            SetSequence( MAX( sequence, 0 ), true ); m_SequencePending = false;
            DevMsg( "Player preview: %s, sequence %d (%s)\n", s_PlayerModels[m_Model].path, sequence, GetSequenceName( m_RootMDL.m_pStudioHdr, MAX( sequence, 0 ) ) );
        }
    }
private:
    int m_Model; bool m_SequencePending;
};

class CHL2MPOptions : public vgui::EditablePanel
{
    DECLARE_CLASS_SIMPLE( CHL2MPOptions, vgui::EditablePanel );
public:
    CHL2MPOptions( vgui::Panel *parent );
    ~CHL2MPOptions();
    void Open();
    void OnCommand( const char *command ) OVERRIDE;
    void OnTick() OVERRIDE;
    void OnKeyCodeTyped( vgui::KeyCode code ) OVERRIDE;
    void PerformLayout() OVERRIDE;
    void PaintBackground() OVERRIDE;
    bool Dirty();
    int CurrentPage() const { return m_Page; }
private:
    void ReadCurrent(); void ReadRows(); void BuildRows(); void LayoutRows(); void Apply(); void Commit(); void Close(); void Confirm( int type ); void RevertDisplay();
    void ResetPage(); void CloseDropdowns();
    const char *KeyName( int action, int slot ); int KeyCode( int action, int slot );
    MESSAGE_FUNC_INT( OnCaptureKey, "CaptureKey", code );
    Setting m_Settings[ARRAYSIZE( s_Settings )];
    CUtlString m_BindingOriginal[BUTTON_CODE_LAST], m_BindingDraft[BUTTON_CODE_LAST];
    CUtlVector<CSettingRow *> m_Rows; CUtlVector<CBindingRow *> m_KeyRows;
    vgui::PanelListPanel *m_List; vgui::Label *m_Heading, *m_Brand, *m_Status, *m_Hint, *m_PreviewTitle, *m_PreviewBody;
    CHL2MPSettingsButton *m_Back, *m_Tabs[5], *m_Subtabs[2], *m_Apply, *m_Discard, *m_Reset;
    CPlayerPreview *m_PlayerPreview;
    COptionsOverlay *m_Overlay; vgui::HFont m_Font, m_SmallFont, m_TitleFont, m_BrandFont;
    Page m_Page; float m_Scale; int m_Width, m_Height, m_Confirmation, m_CaptureAction, m_CaptureSlot;
    int m_OldWidth, m_OldHeight, m_OldWindowed; double m_DisplayDeadline, m_FontRefreshAt; bool m_CloseAfterApply;
};
static CHL2MPOptions *s_Options = NULL;

CHL2MPOptions::CHL2MPOptions( vgui::Panel *parent ) : BaseClass( parent, "Options" ), m_Page( DISPLAY ), m_Scale( 1 ), m_Width( 0 ), m_Height( 0 ), m_Confirmation( 0 ), m_CaptureAction( -1 ), m_CaptureSlot( 0 ), m_OldWidth( 0 ), m_OldHeight( 0 ), m_OldWindowed( 0 ), m_DisplayDeadline( 0 ), m_FontRefreshAt( 0 ), m_CloseAfterApply( false )
{
    SetVisible( false ); SetZPos( 100 ); SetKeyBoardInputEnabled( true ); SetMouseInputEnabled( true );
    m_Font = vgui::surface()->CreateFont(); m_SmallFont = vgui::surface()->CreateFont(); m_TitleFont = vgui::surface()->CreateFont(); m_BrandFont = vgui::surface()->CreateFont();
    m_Heading = Label( this, "SETTINGS" ); m_Brand = Label( this, "deathmatch" ); m_Status = Label( this, "" ); m_Hint = Label( this, "" ); m_Hint->SetWrap( true );
    m_PlayerPreview = new CPlayerPreview( this );
    m_PreviewTitle = Label( this, "PLAYER MODEL" ); m_PreviewBody = Label( this, "Drag to rotate / scroll to zoom" );
    m_Back = new CHL2MPSettingsButton( this, "<  BACK", "back" );
    const char *tabs[] = { "VIDEO", "AUDIO", "GAME", "KEYBOARD / MOUSE", "MULTIPLAYER" };
    for ( int i = 0; i < 5; ++i ) m_Tabs[i] = new CHL2MPSettingsButton( this, tabs[i], VarArgs( "tab_%d", i ) );
    for ( int i = 0; i < 2; ++i ) m_Subtabs[i] = new CHL2MPSettingsButton( this, "", VarArgs( "sub_%d", i ) );
    m_Apply = new CHL2MPSettingsButton( this, "APPLY CHANGES", "apply" ); m_Discard = new CHL2MPSettingsButton( this, "DISCARD CHANGES", "discard" ); m_Reset = new CHL2MPSettingsButton( this, "RESET PAGE", "reset" );
    m_List = new vgui::PanelListPanel( this, "Settings" ); m_List->SetFirstColumnWidth( 0 ); m_List->SetVerticalBufferPixels( 0 );
    HL2MPStyleScrollBar( m_List->GetScrollbar() );
    KeyValues *list = new KeyValues( "List" ); list->SetInt( "autohide_scrollbar", 1 ); m_List->ApplySettings( list ); list->deleteThis();
    m_Overlay = new COptionsOverlay( this ); vgui::ivgui()->AddTickSignal( GetVPanel(), 100 ); s_Options = this;
}
CHL2MPOptions::~CHL2MPOptions()
{
    if ( m_DisplayDeadline ) RevertDisplay();
    if ( vgui::input()->GetAppModalSurface() == m_Overlay->GetVPanel() ) vgui::input()->ReleaseAppModalSurface();
    vgui::ivgui()->RemoveTickSignal( GetVPanel() ); if ( s_Options == this ) s_Options = NULL;
}
void CHL2MPOptions::ReadCurrent()
{
    const MaterialSystem_Config_t &config = materials->GetCurrentConfigForVideoCard();
    for ( int i = 0; i < ARRAYSIZE( s_Settings ); ++i )
    {
        const SettingDef &def = s_Settings[i]; Setting &value = m_Settings[i]; value.available = true;
        if ( def.kind == RESOLUTION ) value.original = VarArgs( "%dx%d", config.m_VideoMode.m_Width, config.m_VideoMode.m_Height );
        else if ( def.kind == WINDOW_MODE ) value.original = config.Windowed() ? "1" : "0";
        else { ConVarRef var( def.cvar, true ); value.available = var.IsValid() && !var.IsFlagSet( FCVAR_CHEAT | FCVAR_DEVELOPMENTONLY ); value.original = var.IsValid() ? var.GetString() : ""; }
        value.draft = value.original;
    }
    for ( int i = 0; i < BUTTON_CODE_LAST; ++i )
    {
        const char *binding = engine->Key_BindingForKey( ButtonCode_t( i ) ); m_BindingOriginal[i] = binding ? binding : ""; m_BindingDraft[i] = m_BindingOriginal[i];
    }
}
void CHL2MPOptions::Open()
{
    if ( IsVisible() ) return; ReadCurrent(); SetVisible( true ); MoveToFront(); BuildRows(); InvalidateLayout( true ); m_Back->RequestFocus();
}
void CHL2MPOptions::CloseDropdowns()
{
    FOR_EACH_VEC( m_Rows, i ) m_Rows[i]->CloseDropdown();
}
void CHL2MPOptions::ReadRows() { FOR_EACH_VEC( m_Rows, i ) m_Settings[m_Rows[i]->Index()].draft = m_Rows[i]->Value(); }
bool CHL2MPOptions::Dirty()
{
    if ( IsVisible() ) ReadRows();
    for ( int i = 0; i < ARRAYSIZE( s_Settings ); ++i ) if ( !SameValue( m_Settings[i].original.String(), m_Settings[i].draft.String() ) ) return true;
    for ( int i = 0; i < BUTTON_CODE_LAST; ++i ) if ( m_BindingOriginal[i] != m_BindingDraft[i] ) return true;
    return false;
}
int CHL2MPOptions::KeyCode( int action, int slot )
{
    for ( int i = 0; i < BUTTON_CODE_LAST; ++i ) if ( !Q_stricmp( m_BindingDraft[i].String(), s_Bindings[action].command ) && slot-- == 0 ) return i;
    return BUTTON_CODE_NONE;
}
const char *CHL2MPOptions::KeyName( int action, int slot )
{
    int code = KeyCode( action, slot ); return code ? inputsystem->ButtonCodeToString( ButtonCode_t( code ) ) : "UNBOUND";
}
void CHL2MPOptions::BuildRows()
{
    CloseDropdowns(); m_List->DeleteAllItems(); m_Rows.Purge(); m_KeyRows.Purge();
    if ( m_Page == BINDINGS )
    {
        for ( int i = 0; i < ARRAYSIZE( s_Bindings ); ++i ) { CBindingRow *row = new CBindingRow( m_List, this, i ); m_KeyRows.AddToTail( row ); m_List->AddItem( NULL, row ); }
    }
    else for ( int i = 0; i < ARRAYSIZE( s_Settings ); ++i ) if ( s_Settings[i].page == m_Page )
    {
        CSettingRow *row = new CSettingRow( m_List, i, m_Settings[i] ); m_Rows.AddToTail( row ); m_List->AddItem( NULL, row );
    }
    m_List->MoveScrollBarToTop(); InvalidateLayout( true ); LayoutRows();
}
void CHL2MPOptions::LayoutRows()
{
    FOR_EACH_VEC( m_Rows, i ) m_Rows[i]->Style( m_Scale, m_Font, m_SmallFont );
    FOR_EACH_VEC( m_KeyRows, i ) m_KeyRows[i]->Style( m_Scale, m_Font, m_SmallFont, KeyName( i, 0 ), KeyName( i, 1 ) );
    m_List->SetPaintBackgroundEnabled( false ); m_List->SetBorder( NULL ); m_List->InvalidateLayout( true );
    m_List->GetScrollbar()->SetWide( MAX( 10, int( 12 * m_Scale ) ) );
}
void CHL2MPOptions::PerformLayout()
{
    BaseClass::PerformLayout(); int w, h; vgui::surface()->GetScreenSize( w, h ); SetBounds( 0, 0, w, h );
    if ( w != m_Width || h != m_Height )
    {
        m_Font = vgui::surface()->CreateFont(); m_SmallFont = vgui::surface()->CreateFont(); m_TitleFont = vgui::surface()->CreateFont(); m_BrandFont = vgui::surface()->CreateFont();
    }
    m_Width = w; m_Height = h;
    const float s = m_Scale = MAX( .65f, MIN( h / 720.0f, w / 1024.0f ) );
    vgui::surface()->SetFontGlyphSet( m_Font, "GorDIN", MAX( 13, int( 18 * s ) ), 400, 0, 0, vgui::ISurface::FONTFLAG_ANTIALIAS );
    vgui::surface()->SetFontGlyphSet( m_SmallFont, "GorDIN", MAX( 11, int( 15 * s ) ), 400, 0, 0, vgui::ISurface::FONTFLAG_ANTIALIAS );
    vgui::surface()->SetFontGlyphSet( m_TitleFont, "GorDIN", int( 28 * s ), 400, 0, 0, vgui::ISurface::FONTFLAG_ANTIALIAS );
    vgui::surface()->SetFontGlyphSet( m_BrandFont, "HL2MP", int( 36 * s ), 400, 0, 0, vgui::ISurface::FONTFLAG_ANTIALIAS );
    const int margin = int( 42 * s ), tabW = MIN( w - margin * 2, int( 800 * s ) ), tabX = ( w - tabW ) / 2;
    m_Brand->SetFont( m_BrandFont ); m_Brand->SetFgColor( Color( 233, 236, 229, 255 ) ); m_Brand->SetBounds( margin + int( 110 * s ), int( 18 * s ), int( 300 * s ), int( 40 * s ) );
    m_Heading->SetFont( m_TitleFont ); m_Heading->SetFgColor( Color( 229, 232, 225, 255 ) ); m_Heading->SetBounds( w - margin - int( 150 * s ), int( 22 * s ), int( 150 * s ), int( 34 * s ) );
    m_Back->SetFont( m_SmallFont ); m_Back->SetBounds( margin - int( 10 * s ), int( 20 * s ), int( 95 * s ), int( 35 * s ) );
    const int tabWidth[] = { 110, 110, 100, 230, 160 }; const int sum = 710; int x = tabX;
    const int category = m_Page <= ADVANCED ? 0 : m_Page == AUDIO ? 1 : m_Page == GAME ? 2 : m_Page <= BINDINGS ? 3 : 4;
    for ( int i = 0; i < 5; ++i ) { int width = tabW * tabWidth[i] / sum; m_Tabs[i]->SetFont( m_SmallFont ); m_Tabs[i]->SetBounds( x, int( 78 * s ), width, int( 35 * s ) ); m_Tabs[i]->Active( category == i ); x += width; }
    for ( int i = 0; i < 2; ++i )
    {
        m_Subtabs[i]->SetVisible( category == 0 || category == 3 ); m_Subtabs[i]->SetFont( m_SmallFont );
        m_Subtabs[i]->SetBounds( w / 2 + int( ( i * 180 - 180 ) * s ), int( 126 * s ), int( 180 * s ), int( 30 * s ) );
        m_Subtabs[i]->SetText( category == 0 ? i ? "ADVANCED VIDEO" : "DISPLAY" : i ? "KEY BINDINGS" : "MOUSE" );
        m_Subtabs[i]->Active( ( m_Page == ADVANCED || m_Page == BINDINGS ) == ( i == 1 ) );
    }
    const bool preview = m_Page == MULTIPLAYER;
    int listW = MIN( w - margin * 2, int( 850 * s ) ), listX = ( w - listW ) / 2;
    const int previewW = MIN( int( 300 * s ), ( w - margin * 2 ) / 3 ), previewH = MAX( 130, h - int( 360 * s ) );
    if ( preview ) { listX = margin + previewW + int( 34 * s ); listW = w - listX - margin; }
    m_List->SetBounds( listX, int( 190 * s ), listW, MAX( 120, h - int( 318 * s ) ) );
    m_PlayerPreview->SetVisible( preview ); m_PlayerPreview->SetBounds( margin, int( 190 * s ), previewW, previewH );
    m_PreviewTitle->SetVisible( preview ); m_PreviewBody->SetVisible( preview );
    m_PreviewTitle->SetFont( m_Font ); m_PreviewTitle->SetFgColor( Color( 238, 231, 213, 255 ) ); m_PreviewTitle->SetBounds( margin, int( 198 * s ) + previewH, previewW, int( 28 * s ) );
    m_PreviewBody->SetFont( m_SmallFont ); m_PreviewBody->SetFgColor( Color( 161, 178, 184, 255 ) ); m_PreviewBody->SetBounds( margin, int( 228 * s ) + previewH, previewW, int( 30 * s ) );
    m_Hint->SetFont( m_SmallFont ); m_Hint->SetFgColor( Color( 161, 178, 184, 255 ) ); m_Hint->SetBounds( listX + 8, h - int( 120 * s ), listW - 16, int( 40 * s ) );
    m_Hint->SetText( m_Page == DISPLAY ? "Display changes have a 15-second confirmation. Gamma is controlled by the desktop in windowed mode." : m_Page == ADVANCED ? "Lighting and texture changes may take a moment. Some settings take effect on the next map." : m_Page == BINDINGS ? "Select a key to rebind it. Esc cancels; Backspace clears. Changes are saved with Apply." : m_Page == MULTIPLAYER ? "Server rules may limit your field of view, model and starting weapon." : "Changes remain pending until you select Apply. Reset Page restores this page's defaults." );
    const int footerY = h - int( 61 * s ), buttonW = int( 165 * s );
    CHL2MPSettingsButton *footer[] = { m_Reset, m_Discard, m_Apply };
    for ( int i = 0; i < 3; ++i ) { footer[i]->SetFont( m_SmallFont ); footer[i]->SetBounds( w - margin - ( 3 - i ) * buttonW, footerY, buttonW - int( 8 * s ), int( 38 * s ) ); }
    m_Status->SetFont( m_SmallFont ); m_Status->SetFgColor( Color( 235, 174, 98, 255 ) ); m_Status->SetBounds( margin, footerY, MAX( 10, w - margin * 2 - buttonW * 3 ), int( 38 * s ) );
    LayoutRows();
    m_Overlay->SetBounds( 0, 0, w, h ); const int dialogW = MIN( w - margin * 2, int( 680 * s ) ), dialogX = ( w - dialogW ) / 2, dialogY = h / 2 - int( 110 * s );
    m_Overlay->title->SetFont( m_TitleFont ); m_Overlay->title->SetFgColor( Color( 238, 231, 213, 255 ) ); m_Overlay->title->SetBounds( dialogX, dialogY, dialogW, int( 44 * s ) );
    m_Overlay->body->SetFont( m_Font ); m_Overlay->body->SetFgColor( Color( 201, 212, 213, 255 ) ); m_Overlay->body->SetBounds( dialogX, dialogY + int( 60 * s ), dialogW, int( 90 * s ) );
    m_Overlay->yes->SetFont( m_SmallFont ); m_Overlay->no->SetFont( m_SmallFont ); m_Overlay->discard->SetFont( m_SmallFont );
    const int columns = m_Confirmation == 2 ? 3 : 2, dialogButtonW = dialogW / columns;
    m_Overlay->yes->SetBounds( dialogX, dialogY + int( 170 * s ), dialogButtonW - 8, int( 42 * s ) );
    m_Overlay->discard->SetBounds( dialogX + dialogButtonW, dialogY + int( 170 * s ), dialogButtonW - 8, int( 42 * s ) );
    m_Overlay->no->SetBounds( dialogX + dialogButtonW * ( columns - 1 ), dialogY + int( 170 * s ), dialogButtonW - 8, int( 42 * s ) );
}
void CHL2MPOptions::PaintBackground()
{
    vgui::ISurface *surface = vgui::surface(); const int w = GetWide(), h = GetTall();
    surface->DrawSetColor( Color( 21, 30, 35, 232 ) );
    surface->DrawFilledRect( 0, 0, w, h );
    surface->DrawSetColor( Color( 8, 14, 18, 150 ) ); surface->DrawFilledRect( 0, 0, w, int( 68 * m_Scale ) ); surface->DrawFilledRect( 0, h - int( 76 * m_Scale ), w, h );
    surface->DrawSetColor( Color( 204, 221, 223, 30 ) ); surface->DrawFilledRect( 0, int( 68 * m_Scale ), w, int( 68 * m_Scale ) + 1 ); surface->DrawFilledRect( 0, h - int( 76 * m_Scale ), w, h - int( 76 * m_Scale ) + 1 );
}
void CHL2MPOptions::Confirm( int type )
{
    CloseDropdowns(); m_Confirmation = type; m_Overlay->capture = type == 3; m_Overlay->SetVisible( true ); m_Overlay->MoveToFront();
    m_Overlay->yes->SetVisible( type != 3 ); m_Overlay->no->SetVisible( type != 3 );
    m_Overlay->discard->SetVisible( type == 2 );
    m_Overlay->title->SetText( type == 1 ? "KEEP THESE DISPLAY SETTINGS?" : type == 2 ? "UNAPPLIED CHANGES" : VarArgs( "BIND: %s", s_Bindings[m_CaptureAction].label ) );
    m_Overlay->body->SetText( type == 2 ? "Apply your changes, discard them, or keep editing." : type == 3 ? "Press a keyboard key or mouse button. Esc cancels. Backspace clears this binding. An existing use of the selected key will be reassigned." : "" );
    m_Overlay->yes->SetText( type == 1 ? "KEEP CHANGES" : "APPLY AND CLOSE" ); m_Overlay->no->SetText( type == 1 ? "REVERT DISPLAY" : "KEEP EDITING" );
    vgui::input()->SetAppModalSurface( m_Overlay->GetVPanel() ); m_Overlay->RequestFocus();
    InvalidateLayout( true );
}
void CHL2MPOptions::Close()
{
    CloseDropdowns(); SetVisible( false ); GetParent()->OnCommand( "options_closed" );
}
void CHL2MPOptions::Apply()
{
    ReadRows();
    const MaterialSystem_Config_t &config = materials->GetCurrentConfigForVideoCard();
    int width = config.m_VideoMode.m_Width, height = config.m_VideoMode.m_Height, windowed = config.Windowed() ? 1 : 0;
    for ( int i = 0; i < ARRAYSIZE( s_Settings ); ++i )
    {
        if ( s_Settings[i].kind == RESOLUTION ) sscanf( m_Settings[i].draft.String(), "%dx%d", &width, &height );
        if ( s_Settings[i].kind == WINDOW_MODE ) windowed = atoi( m_Settings[i].draft.String() );
    }
    if ( width != config.m_VideoMode.m_Width || height != config.m_VideoMode.m_Height || windowed != int( config.Windowed() ) )
    {
        m_OldWidth = config.m_VideoMode.m_Width; m_OldHeight = config.m_VideoMode.m_Height; m_OldWindowed = config.Windowed() ? 1 : 0;
        m_DisplayDeadline = Plat_FloatTime() + 15; m_FontRefreshAt = Plat_FloatTime() + 1; Confirm( 1 ); engine->ClientCmd_Unrestricted( VarArgs( "mat_setvideomode %d %d %d\n", width, height, windowed ) );
    }
    else Commit();
}
void CHL2MPOptions::Commit()
{
    for ( int i = 0; i < ARRAYSIZE( s_Settings ); ++i )
    {
        if ( !s_Settings[i].cvar[0] || !m_Settings[i].available || SameValue( m_Settings[i].original.String(), m_Settings[i].draft.String() ) ) continue;
        ConVarRef var( s_Settings[i].cvar, true ); if ( var.IsValid() ) var.SetValue( m_Settings[i].draft.String() );
    }
    for ( int i = 1; i < BUTTON_CODE_LAST; ++i ) if ( m_BindingOriginal[i] != m_BindingDraft[i] )
    {
        const char *key = inputsystem->ButtonCodeToString( ButtonCode_t( i ) );
        if ( !key || strchr( key, '"' ) || strchr( key, ';' ) || strchr( key, '\n' ) ) continue;
        // New commands come exclusively from the fixed action list above.
        if ( m_BindingDraft[i].IsEmpty() ) engine->ClientCmd_Unrestricted( VarArgs( "unbind \"%s\"\n", key ) );
        else for ( int j = 0; j < ARRAYSIZE( s_Bindings ); ++j ) if ( !Q_strcmp( m_BindingDraft[i].String(), s_Bindings[j].command ) )
            engine->ClientCmd_Unrestricted( VarArgs( "bind \"%s\" \"%s\"\n", key, s_Bindings[j].command ) );
        m_BindingOriginal[i] = m_BindingDraft[i];
    }
    engine->ClientCmd_Unrestricted( "mat_savechanges; host_writeconfig\n" );
    for ( int i = 0; i < ARRAYSIZE( s_Settings ); ++i ) m_Settings[i].original = m_Settings[i].draft;
    m_DisplayDeadline = 0; m_Status->SetText( "Changes applied" );
    if ( m_CloseAfterApply ) { m_CloseAfterApply = false; Close(); }
}
void CHL2MPOptions::RevertDisplay()
{
    engine->ClientCmd_Unrestricted( VarArgs( "mat_setvideomode %d %d %d\n", m_OldWidth, m_OldHeight, m_OldWindowed ) );
    m_DisplayDeadline = 0; m_CloseAfterApply = false; m_FontRefreshAt = Plat_FloatTime() + 1;
    for ( int i = 0; i < ARRAYSIZE( s_Settings ); ++i )
        if ( s_Settings[i].kind == RESOLUTION || s_Settings[i].kind == WINDOW_MODE ) m_Settings[i].draft = m_Settings[i].original;
    m_Status->SetText( "Display reverted" ); BuildRows();
}
void CHL2MPOptions::ResetPage()
{
    ReadRows();
    for ( int i = 0; i < ARRAYSIZE( s_Settings ); ++i ) if ( s_Settings[i].page == m_Page && s_Settings[i].cvar[0] && m_Settings[i].available )
    {
        ConVarRef var( s_Settings[i].cvar, true ); if ( var.IsValid() ) m_Settings[i].draft = var.GetDefault();
    }
    if ( m_Page == BINDINGS )
    {
        for ( int i = 1; i < BUTTON_CODE_LAST; ++i ) for ( int j = 0; j < ARRAYSIZE( s_Bindings ); ++j )
            if ( !Q_strcmp( m_BindingDraft[i].String(), s_Bindings[j].command ) ) m_BindingDraft[i] = "";
        for ( int i = 0; i < ARRAYSIZE( s_Bindings ); ++i )
        {
            ButtonCode_t key = inputsystem->StringToButtonCode( s_Bindings[i].key ); if ( key > BUTTON_CODE_NONE && key < BUTTON_CODE_LAST ) m_BindingDraft[key] = s_Bindings[i].command;
        }
    }
    BuildRows(); m_Status->SetText( "Defaults staged" );
}
void CHL2MPOptions::OnCaptureKey( int code )
{
    if ( m_Confirmation != 3 ) return;
    if ( code != KEY_ESCAPE && code > BUTTON_CODE_NONE && code < BUTTON_CODE_LAST )
    {
        int previous = KeyCode( m_CaptureAction, m_CaptureSlot ); if ( previous ) m_BindingDraft[previous] = "";
        if ( code != KEY_BACKSPACE ) m_BindingDraft[code] = s_Bindings[m_CaptureAction].command;
    }
    m_Confirmation = 0; m_Overlay->SetVisible( false ); vgui::input()->ReleaseAppModalSurface(); LayoutRows(); m_Back->RequestFocus();
}
void CHL2MPOptions::OnCommand( const char *command )
{
    if ( !Q_strcmp( command, "refresh_fonts" ) ) { m_Width = 0; InvalidateLayout( true ); return; }
    if ( !Q_strcmp( command, "confirm_discard" ) && m_Confirmation == 2 )
    {
        m_Overlay->SetVisible( false ); vgui::input()->ReleaseAppModalSurface(); m_Confirmation = 0; ReadCurrent(); Close(); return;
    }
    if ( !Q_strcmp( command, "confirm_yes" ) || !Q_strcmp( command, "confirm_no" ) )
    {
        bool yes = !Q_strcmp( command, "confirm_yes" ); int type = m_Confirmation;
        m_Overlay->SetVisible( false ); vgui::input()->ReleaseAppModalSurface(); m_Confirmation = 0;
        if ( type == 1 ) { if ( yes ) Commit(); else RevertDisplay(); }
        if ( type == 2 && yes ) { m_CloseAfterApply = true; Apply(); }
        return;
    }
    if ( m_Confirmation ) return;
    if ( !Q_strncmp( command, "tab_", 4 ) )
    {
        const Page pages[] = { DISPLAY, AUDIO, GAME, MOUSE, MULTIPLAYER }; int tab = atoi( command + 4 );
        if ( tab >= 0 && tab < 5 ) { ReadRows(); m_Page = pages[tab]; BuildRows(); }
    }
    else if ( !Q_strncmp( command, "sub_", 4 ) ) { ReadRows(); m_Page = m_Page <= ADVANCED ? ( command[4] == '1' ? ADVANCED : DISPLAY ) : ( command[4] == '1' ? BINDINGS : MOUSE ); BuildRows(); }
    else if ( !Q_strncmp( command, "bind_", 5 ) )
    {
        if ( sscanf( command + 5, "%d_%d", &m_CaptureAction, &m_CaptureSlot ) == 2 && m_CaptureAction >= 0 && m_CaptureAction < ARRAYSIZE( s_Bindings ) && m_CaptureSlot >= 0 && m_CaptureSlot < 2 ) Confirm( 3 );
    }
    else if ( !Q_strcmp( command, "back" ) ) { if ( Dirty() ) Confirm( 2 ); else Close(); }
    else if ( !Q_strcmp( command, "apply" ) ) Apply();
    else if ( !Q_strcmp( command, "discard" ) ) { ReadCurrent(); BuildRows(); m_Status->SetText( "Changes discarded" ); }
    else if ( !Q_strcmp( command, "reset" ) ) ResetPage();
    else BaseClass::OnCommand( command );
}
void CHL2MPOptions::OnKeyCodeTyped( vgui::KeyCode code )
{
    if ( code == KEY_ESCAPE ) FOR_EACH_VEC( m_Rows, i ) if ( m_Rows[i]->DropdownOpen() ) { CloseDropdowns(); return; }
    if ( code == KEY_ESCAPE ) OnCommand( "back" ); else BaseClass::OnKeyCodeTyped( code );
}
void CHL2MPOptions::OnTick()
{
    if ( m_FontRefreshAt && Plat_FloatTime() >= m_FontRefreshAt )
    {
        // GameUI reloads its scheme after switching video modes; rebuild our fonts after that reload settles.
        m_FontRefreshAt = 0; m_Width = 0; InvalidateLayout( true ); GetParent()->OnCommand( "refresh_menu_fonts" );
    }
    if ( m_DisplayDeadline )
    {
        m_Overlay->body->SetText( VarArgs( "The previous display mode will be restored in %d seconds unless you keep these changes.", MAX( 0, int( ceil( m_DisplayDeadline - Plat_FloatTime() ) ) ) ) );
        if ( Plat_FloatTime() >= m_DisplayDeadline ) OnCommand( "confirm_no" );
    }
    if ( !IsVisible() ) return;
    int w, h; vgui::surface()->GetScreenSize( w, h ); if ( w != m_Width || h != m_Height ) InvalidateLayout( true );
    const bool dirty = Dirty();
    if ( m_Page == MULTIPLAYER )
    {
        FOR_EACH_VEC( m_Rows, row ) if ( !Q_strcmp( s_Settings[m_Rows[row]->Index()].cvar, "cl_playermodel" ) )
        {
            const CUtlString model = m_Rows[row]->Value(); bool found = false;
            for ( int i = 0; i < ARRAYSIZE( s_PlayerModels ); ++i ) if ( !Q_stricmp( model.String(), s_PlayerModels[i].path ) )
            {
                m_PlayerPreview->SelectModel( i ); m_PreviewTitle->SetText( s_PlayerModels[i].name ); found = true; break;
            }
            m_PlayerPreview->SetVisible( found );
            if ( !found ) m_PreviewTitle->SetText( "MODEL UNAVAILABLE" );
        }
    }
    m_Apply->SetEnabled( dirty ); m_Apply->Active( dirty ); m_Discard->SetEnabled( dirty );
    if ( dirty ) m_Status->SetText( "Unapplied changes" );
    else
    {
        char status[64]; m_Status->GetText( status, sizeof( status ) );
        if ( !Q_strcmp( status, "Unapplied changes" ) ) m_Status->SetText( "" );
    }
}
}

vgui::Panel *HL2MPOptions_Create( vgui::Panel *parent ) { return new CHL2MPOptions( parent ); }
void HL2MPOptions_Open( vgui::Panel *panel ) { static_cast<CHL2MPOptions *>( panel )->Open(); }
CON_COMMAND_F( mm_options_status, "Show Options page and pending changes.", FCVAR_CLIENTDLL )
{
    if ( s_Options ) Msg( "Options: visible=%d page=%d dirty=%d\n", s_Options->IsVisible(), s_Options->CurrentPage(), s_Options->Dirty() );
}
