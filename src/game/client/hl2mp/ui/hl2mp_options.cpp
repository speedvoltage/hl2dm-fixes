#include "cbase.h"
#include "hl2mp_messages.h"
#include "hl2mp_hud_customization.h"
#include "cdll_client_int.h"
#include "hl2mp_options.h"
#include "hl2mp_community.h"
#include "hl2mp_menucontrols.h"
#include "hl2mp_colorpicker.h"
#include "hl2mp_hitmarker_style.h"
#include "hl2mp_hitsounds.h"
extern ConVar sv_hitsounds, sv_footsteps;
#include "vgui_controls/Tooltip.h"
#include "hl2mp_playermodels.h"
#include "hl2mp_sprays.h"
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
enum Kind { CHOICE, SLIDER, TEXT, RESOLUTION, WINDOW_MODE, SPRAY, COLOR };
struct SettingDef
{
    Page page; Kind kind; const char *label, *cvar, *choices; float low, high, step;
};
static const SettingDef s_Settings[] = {
    { GAME, CHOICE, "#HL2MP_Hitmarkers", "cl_hitmarkers", "#HL2MP_UI_0c7ef33e451e=0|#HL2MP_UI_616f73fbace9=1", 0, 0, 0 },
    { GAME, CHOICE, "#HL2MP_HitmarkerFont", "cl_hitmarker_font", "Half-Life 2=0|GorDIN=1|GorDIN Bold=2|Tahoma=3|TF2=4|TF2 Build=5|TF2 Secondary=6|TF2 Professor=7", 0, 0, 0 },
    { GAME, COLOR, "#HL2MP_HitmarkerColor", "cl_hitmarker_color", "", 0, 0, 0 },
    { AUDIO, CHOICE, "#HL2MP_HitSounds", "cl_hitsounds", "#HL2MP_UI_0c7ef33e451e=0|#HL2MP_UI_616f73fbace9=1", 0,0,0 },
    { AUDIO, CHOICE, "#HL2MP_HitSoundChoice", "cl_hitsound", "#HL2MP_SoundBell=0|#HL2MP_SoundClick=1|#HL2MP_SoundPop=2|#HL2MP_SoundChime=3|#HL2MP_SoundGlass=4|#HL2MP_SoundWood=5|#HL2MP_SoundMetal=6|#HL2MP_SoundDigital=7|#HL2MP_SoundPulse=8|#HL2MP_SoundPluck=9|#HL2MP_SoundBell=10|#HL2MP_SoundClick=11|#HL2MP_SoundPop=12|#HL2MP_SoundChime=13|#HL2MP_SoundGlass=14|#HL2MP_SoundDigital=15", 0,0,0 },
    { AUDIO, SLIDER, "#HL2MP_HitSoundVolume", "cl_hitsound_volume", "%", 0,1,0.01f },
    { AUDIO, CHOICE, "#HL2MP_FootstepsSelf", "cl_footsteps_self", "#HL2MP_FootstepsModel=0|#HL2MP_FootstepsCombine=1|#HL2MP_FootstepsPolice=2|#HL2MP_FootstepsRebel=3|#HL2MP_FootstepsMaterial=4", 0, 0, 0 },
    { AUDIO, CHOICE, "#HL2MP_FootstepsTeammates", "cl_footsteps_teammates", "#HL2MP_FootstepsModel=0|#HL2MP_FootstepsCombine=1|#HL2MP_FootstepsPolice=2|#HL2MP_FootstepsRebel=3|#HL2MP_FootstepsMaterial=4", 0, 0, 0 },
    { AUDIO, CHOICE, "#HL2MP_FootstepsEnemies", "cl_footsteps_enemies", "#HL2MP_FootstepsModel=0|#HL2MP_FootstepsCombine=1|#HL2MP_FootstepsPolice=2|#HL2MP_FootstepsRebel=3|#HL2MP_FootstepsMaterial=4", 0, 0, 0 },
    { GAME, CHOICE, "#HL2MP_BrightModelsOption", "cl_bright_player_models", "#HL2MP_UI_0c7ef33e451e=0|#HL2MP_UI_616f73fbace9=1", 0, 0, 0 },
    { AUDIO, CHOICE, "#HL2MP_VoteSounds", "cl_vote_sounds", "#HL2MP_UI_0c7ef33e451e=0|#HL2MP_UI_616f73fbace9=1", 0, 0, 0 },
    { GAME, CHOICE, "#HL2MP_VoteAnimations", "cl_vote_animations", "#HL2MP_UI_0c7ef33e451e=0|#HL2MP_UI_616f73fbace9=1", 0, 0, 0 },
    { DISPLAY, RESOLUTION, "#HL2MP_UI_d4055fafa379", "", "", 0, 0, 0 },
    { DISPLAY, WINDOW_MODE, "#HL2MP_UI_8e84f008a644", "", "#HL2MP_UI_1f8c000f9634=0|#HL2MP_UI_576290d91ca2=1", 0, 0, 0 },
    { DISPLAY, SLIDER, "#HL2MP_UI_7047dfe63f23", "mat_monitorgamma", "", 1.6f, 2.6f, 0.01f },
    { DISPLAY, CHOICE, "#HL2MP_UI_3217f73e9359", "fps_max", "#HL2MP_UI_8de37d25b589=60|#HL2MP_UI_e3f90dba7a06=120|#HL2MP_UI_5a0539d38b23=144|#HL2MP_UI_c8b7db9a9f83=165|#HL2MP_UI_d9974ecf7e02=240|#HL2MP_UI_9af4b5b70855=300|#HL2MP_UI_5b43530641e7=0", 0, 0, 0 },
    { DISPLAY, CHOICE, "#HL2MP_UI_f0be9af548e6", "mm_menu_motion", "#HL2MP_UI_0c7ef33e451e=0|#HL2MP_UI_616f73fbace9=1", 0, 0, 0 },
    { ADVANCED, CHOICE, "#HL2MP_UI_0d0feef89d87", "mat_vsync", "#HL2MP_UI_0c7ef33e451e=0|#HL2MP_UI_616f73fbace9=1", 0, 0, 0 },
    { ADVANCED, CHOICE, "#HL2MP_UI_c77c3af22c64", "mat_antialias", "#HL2MP_UI_c627c09c14e5=0|#HL2MP_UI_dd3d0252a272=2|#HL2MP_UI_093551bb6d5a=4|#HL2MP_UI_8d27abe1615b=8", 0, 0, 0 },
    { ADVANCED, CHOICE, "#HL2MP_UI_a71699b3f84f", "mat_picmip", "#HL2MP_UI_5fbdda1b8c72=2|#HL2MP_UI_7c48dd672a68=1|#HL2MP_UI_b43e72eed048=0|#HL2MP_UI_8307aa354b9c=-1", 0, 0, 0 },
    { ADVANCED, CHOICE, "#HL2MP_UI_39693b11904b", "r_rootlod", "#HL2MP_UI_5fbdda1b8c72=2|#HL2MP_UI_7c48dd672a68=1|#HL2MP_UI_b43e72eed048=0", 0, 0, 0 },
    { ADVANCED, CHOICE, "#HL2MP_UI_527a443989b5", "mat_forceaniso", "#HL2MP_UI_25c640705d2e=1|#HL2MP_UI_bad304e4870b=2|#HL2MP_UI_0120b9f39efd=4|#HL2MP_UI_c6c65e26d25a=8|#HL2MP_UI_8b96b679cd52=16", 0, 0, 0 },
    { ADVANCED, CHOICE, "#HL2MP_UI_0fcd7191313b", "mat_trilinear", "#HL2MP_UI_0c7ef33e451e=0|#HL2MP_UI_616f73fbace9=1", 0, 0, 0 },
    { ADVANCED, CHOICE, "#HL2MP_UI_ebf85a2d90c4", "mat_hdr_level", "#HL2MP_UI_c627c09c14e5=0|#HL2MP_UI_77bb64a32051=1|#HL2MP_UI_cb6839cad021=2", 0, 0, 0 },
    { ADVANCED, CHOICE, "#HL2MP_UI_3bed4ed47a7a", "r_shadowrendertotexture", "#HL2MP_UI_0c7ef33e451e=0|#HL2MP_UI_616f73fbace9=1", 0, 0, 0 },
    { ADVANCED, CHOICE, "#HL2MP_UI_b3ed8df3c63f", "r_waterforceexpensive", "#HL2MP_UI_699f1a05a909=0|#HL2MP_UI_6256748e2556=1", 0, 0, 0 },
    { ADVANCED, CHOICE, "#HL2MP_UI_8931060940fb", "r_waterforcereflectentities", "#HL2MP_UI_0c7ef33e451e=0|#HL2MP_UI_616f73fbace9=1", 0, 0, 0 },
    { ADVANCED, CHOICE, "#HL2MP_UI_8d7eb34db686", "mat_bumpmap", "#HL2MP_UI_0c7ef33e451e=0|#HL2MP_UI_616f73fbace9=1", 0, 0, 0 },
    { ADVANCED, CHOICE, "#HL2MP_UI_6ca5b122fb88", "mat_specular", "#HL2MP_UI_0c7ef33e451e=0|#HL2MP_UI_616f73fbace9=1", 0, 0, 0 },
    { ADVANCED, CHOICE, "#HL2MP_UI_fed656fa774a", "mat_colorcorrection", "#HL2MP_UI_0c7ef33e451e=0|#HL2MP_UI_616f73fbace9=1", 0, 0, 0 },
    { ADVANCED, CHOICE, "#HL2MP_UI_d74639ba02f0", "mat_motion_blur_enabled", "#HL2MP_UI_0c7ef33e451e=0|#HL2MP_UI_616f73fbace9=1", 0, 0, 0 },
    { AUDIO, SLIDER, "#HL2MP_UI_b041a7d44291", "volume", "%", 0, 1, 0.01f },
    { AUDIO, SLIDER, "#HL2MP_UI_d8773f37c57e", "snd_musicvolume", "%", 0, 1, 0.01f },
    { AUDIO, CHOICE, "#HL2MP_UI_d0a9712be5c7", "snd_surround_speakers", "#HL2MP_UI_7e7b997153ea=0|#HL2MP_UI_453cdbf55416=2|#HL2MP_UI_f08148e1d00b=4|#HL2MP_UI_a052ceded18c=5|#HL2MP_UI_b044c3b8e547=7", 0, 0, 0 },
    { AUDIO, CHOICE, "#HL2MP_UI_1703c7185ed1", "snd_pitchquality", "#HL2MP_UI_0c7ef33e451e=0|#HL2MP_UI_616f73fbace9=1", 0, 0, 0 },
    { AUDIO, CHOICE, "#HL2MP_UI_38f4ccfb71ad", "snd_mute_losefocus", "#HL2MP_UI_23794d91c53a=0|#HL2MP_UI_f91711645937=1", 0, 0, 0 },
    { AUDIO, CHOICE, "#HL2MP_UI_9bdbc177e89d", "voice_enable", "#HL2MP_UI_0c7ef33e451e=0|#HL2MP_UI_616f73fbace9=1", 0, 0, 0 },
    { AUDIO, SLIDER, "#HL2MP_UI_36020013a785", "voice_scale", "%", 0, 1, 0.01f },
    { AUDIO, CHOICE, "#HL2MP_UI_b111974dec44", "closecaption", "#HL2MP_UI_0c7ef33e451e=0|#HL2MP_UI_616f73fbace9=1", 0, 0, 0 },
    { GAME, SLIDER, "#HL2MP_UI_1fab91bbb0d3", "fov_desired", "", 70, 110, 1 },
    { GAME, CHOICE, "#HL2MP_UI_2ef15ac2866f", "con_enable", "#HL2MP_UI_0c7ef33e451e=0|#HL2MP_UI_616f73fbace9=1", 0, 0, 0 },
    { GAME, CHOICE, "#HL2MP_UI_8b3214b0cef9", "crosshair", "#HL2MP_UI_84dc5c33ad8b=0|#HL2MP_UI_5cea256d1b64=1", 0, 0, 0 },
    { GAME, CHOICE, "#HL2MP_HudQuickInfo", "hud_quickinfo", "#HL2MP_UI_0c7ef33e451e=0|#HL2MP_UI_616f73fbace9=1", 0, 0, 0 },
    { GAME, CHOICE, "#HL2MP_ShowUnownedAmmoPickups", "cl_hud_show_unowned_ammo", "#HL2MP_UI_0c7ef33e451e=0|#HL2MP_UI_616f73fbace9=1", 0, 0, 0 },
    { GAME, CHOICE, "#HL2MP_UI_408dcdeb70d1", "hud_fastswitch", "#HL2MP_UI_0c7ef33e451e=0|#HL2MP_UI_616f73fbace9=1", 0, 0, 0 },
    { GAME, CHOICE, "#HL2MP_UI_7c9008467e04", "cl_wpn_sway_enabled", "#HL2MP_UI_38cca6bea010=0|#HL2MP_UI_e8a01133b135=1", 0, 0, 0 },
    { GAME, CHOICE, "#HL2MP_UI_7be992fd080d", "cl_autowepswitch", "#HL2MP_UI_23794d91c53a=0|#HL2MP_UI_f91711645937=1", 0, 0, 0 },
    { GAME, CHOICE, "#HL2MP_UI_8d697ccaf256", "cl_showfps", "#HL2MP_UI_84dc5c33ad8b=0|#HL2MP_UI_699f1a05a909=1|#HL2MP_UI_d0fa8409c4d2=2", 0, 0, 0 },
    { GAME, CHOICE, "#HL2MP_UI_673913077c1e", "net_graph", "#HL2MP_UI_84dc5c33ad8b=0|#HL2MP_UI_ed85253568ad=1|#HL2MP_UI_9f103c905145=2|#HL2MP_UI_e4ba216b53e6=3", 0, 0, 0 },
    { MOUSE, SLIDER, "#HL2MP_UI_73ba9f3d3b7f", "sensitivity", "", 0.1f, 10, 0.01f },
    { MOUSE, CHOICE, "#HL2MP_UI_71f3192afcf4", "m_rawinput", "#HL2MP_UI_0c7ef33e451e=0|#HL2MP_UI_616f73fbace9=1", 0, 0, 0 },
    { MOUSE, CHOICE, "#HL2MP_UI_05d3bc41d49c", "m_pitch", "#HL2MP_UI_23794d91c53a=0.022|#HL2MP_UI_f91711645937=-0.022", 0, 0, 0 },
    { MOUSE, CHOICE, "#HL2MP_UI_62c2c23d2939", "m_filter", "#HL2MP_UI_0c7ef33e451e=0|#HL2MP_UI_616f73fbace9=1", 0, 0, 0 },
    { MOUSE, CHOICE, "#HL2MP_UI_c87c19ec9ea0", "m_customaccel", "#HL2MP_UI_0c7ef33e451e=0|#HL2MP_UI_616f73fbace9=3", 0, 0, 0 },
    { MOUSE, SLIDER, "#HL2MP_UI_3e7f5befa927", "zoom_sensitivity_ratio", "", 0.1f, 3, 0.01f },
    { MULTIPLAYER, TEXT, "#HL2MP_UI_2c885aae8c11", "cl_player_name", "", 0, 0, 0 },
    { MULTIPLAYER, CHOICE, "#HL2MP_UI_bea1ab02338b", "cl_playermodel", "", 0, 0, 0 },
    { MULTIPLAYER, CHOICE, "#HL2MP_UI_88982a293a87", "cl_defaultweapon", "#HL2MP_UI_0a3cd20ffde0=weapon_physcannon|#HL2MP_UI_a1c6c25aa56c=weapon_pistol|#HL2MP_UI_3dba79845196=weapon_smg1|#HL2MP_UI_efd31cc3da3f=weapon_frag|#HL2MP_UI_2fe1555090e1=weapon_crowbar|#HL2MP_UI_25abc6029146=weapon_stunstick", 0, 0, 0 },
    { MULTIPLAYER, SPRAY, "#HL2MP_Spray", "cl_logofile", "", 0, 0, 0 },
    { MULTIPLAYER, CHOICE, "#HL2MP_UI_6973064b1e2d", "cl_downloadfilter", "#HL2MP_UI_00ff6750e125=all|#HL2MP_UI_62f137e4275f=nosounds|#HL2MP_UI_3676cce01a6f=mapsonly|#HL2MP_UI_c627c09c14e5=none", 0, 0, 0 },
    { MULTIPLAYER, CHOICE, "#HL2MP_UI_c92f116269de", "cl_allowupload", "#HL2MP_UI_23794d91c53a=0|#HL2MP_UI_f91711645937=1", 0, 0, 0 },
};
struct BindingDef { const char *label, *command, *key; };
static const BindingDef s_Bindings[] = {
    { "#HL2MP_VoteYesBinding", "vote_yes", "F1" },
    { "#HL2MP_VoteNoBinding", "vote_no", "F2" },
    { "#HL2MP_HudEditor", "hud_editor", "" },
    { "#HL2MP_HudLiveEditor", "hud_editor_live", "" },
    { "#HL2MP_UI_68d2943ecb14", "community_menu", "m" },
    { "#HL2MP_UI_03244d3bb2b0", "+forward", "w" }, { "#HL2MP_UI_3a1635febaec", "+back", "s" },
    { "#HL2MP_UI_6c94221edd26", "+moveleft", "a" }, { "#HL2MP_UI_8d60decd44f3", "+moveright", "d" },
    { "#HL2MP_UI_9ee0ca3e24d6", "+jump", "SPACE" }, { "#HL2MP_UI_3b67f1c91f4f", "+duck", "CTRL" },
    { "#HL2MP_UI_fd04788626e5", "+speed", "SHIFT" }, { "#HL2MP_UI_08ee52ae125a", "+walk", "ALT" },
    { "#HL2MP_UI_c36d819e7bc6", "+use", "e" }, { "#HL2MP_UI_5ecdbaec7148", "+attack", "MOUSE1" },
    { "#HL2MP_UI_275b159b41a5", "+attack2", "MOUSE2" }, { "#HL2MP_UI_bdc090ec61e3", "+reload", "r" },
    { "#HL2MP_UI_0d21659ad887", "phys_swap", "g" }, { "#HL2MP_UI_6a89521ef03a", "lastinv", "q" },
    { "#HL2MP_UI_ece56e44bc6e", "invnext", "MWHEELDOWN" }, { "#HL2MP_UI_72a97e60da7a", "invprev", "MWHEELUP" },
    { "#HL2MP_UI_80ffed93a6ee", "slot1", "1" }, { "#HL2MP_UI_41d071a98b9c", "slot2", "2" },
    { "#HL2MP_UI_e8ce41424fab", "slot3", "3" }, { "#HL2MP_UI_b6a0a795d009", "slot4", "4" },
    { "#HL2MP_UI_4926cd49b167", "slot5", "5" }, { "#HL2MP_UI_c774f6787d3b", "slot6", "6" },
    { "#HL2MP_UI_7b242d99506d", "use weapon_crowbar;use weapon_stunstick", "" },
    { "#HL2MP_UI_4de640566f2e", "use weapon_physcannon", "" }, { "#HL2MP_UI_af93da41d026", "use weapon_pistol", "" },
    { "#HL2MP_UI_b142c03ce41d", "use weapon_357", "" }, { "#HL2MP_UI_3dba79845196", "use weapon_smg1", "" },
    { "#HL2MP_UI_2328547e2a46", "use weapon_ar2", "" }, { "#HL2MP_UI_050991792ba0", "use weapon_shotgun", "" },
    { "#HL2MP_UI_44f05d349059", "use weapon_crossbow", "" }, { "#HL2MP_UI_e3cc8ec3b3f8", "use weapon_frag", "" },
    { "#HL2MP_UI_9871087b1b18", "use weapon_rpg", "" }, { "#HL2MP_UI_4ebe6dcee2b2", "use weapon_SLAM", "" },
    { "#HL2MP_UI_e2ed3c69bc28", "impulse 100", "f" }, { "#HL2MP_UI_be09a7860cc2", "+zoom", "z" },
    { "#HL2MP_UI_f48570f44b23", "+showscores", "TAB" }, { "#HL2MP_UI_9bdbc177e89d", "+voicerecord", "k" },
    { "#HL2MP_UI_0acb3af87661", "messagemode", "y" }, { "#HL2MP_UI_87ba4c564992", "messagemode2", "u" },
    { "#HL2MP_UI_7be5610a028c", "impulse 201", "t" }, { "#HL2MP_UI_029320ad166c", "jpeg", "F5" },
    { "#HL2MP_UI_2ef15ac2866f", "toggleconsole", "`" }, { "#HL2MP_UI_9e716cb3bff3", "changeteam", "F3" }
};

static bool SameValue( const char *a, const char *b, bool numeric = true )
{
    if ( !Q_strcmp( a, b ) ) return true;
    if ( !numeric ) return false;
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

class CInfoButton : public vgui::Button
{
    DECLARE_CLASS_SIMPLE(CInfoButton,vgui::Button);
public:
    CInfoButton(vgui::Panel *parent,const char *hint) : BaseClass(parent,"SettingInfo","i"), m_Hint(hint),m_Warning(false)
    {
        SetPaintBackgroundEnabled(false); SetPaintBorderEnabled(false);
        SetContentAlignment(vgui::Label::a_center);SetTextInset(0,0);
        GetTooltip()->SetEnabled(false);
        m_Tip = new vgui::Label(this,"SettingHint",hint);
        m_Tip->MakePopup(false,true);m_Tip->SetMouseInputEnabled(false);m_Tip->SetKeyBoardInputEnabled(false);
        m_Tip->SetWrap(true);m_Tip->SetContentAlignment(vgui::Label::a_northwest);m_Tip->SetTextInset(12,10);m_Tip->SetVisible(false);
    }
    void SetFont(vgui::HFont font) OVERRIDE { BaseClass::SetFont(font);m_Tip->SetFont(font); }
    void SetWarning(bool warning,const char *warningText="#HL2MP_ServerVisualDisabled")
    {
        m_Warning=warning;SetText(warning ? "" : "i");
        m_Tip->SetText(warning ? warningText : m_Hint);Repaint();
    }
    void Paint() OVERRIDE
    {
        BaseClass::Paint(); vgui::surface()->DrawSetColor(m_Warning ? Color(255,107,97,255) : HL2MPServerTheme::Orange());
        if(m_Warning)
        {
            const int mid=GetWide()/2,top=2,bottom=GetTall()-3,half=MIN(mid-2,(GetWide()-1)/2-2);
            vgui::surface()->DrawLine(mid,top,mid-half,bottom);vgui::surface()->DrawLine(mid-half,bottom,mid+half,bottom);vgui::surface()->DrawLine(mid+half,bottom,mid,top);
            // Center the mark in the triangle's interior, independently of font bearings/baselines.
            const int height=bottom-top,stroke=MAX(2,GetWide()/12),left=mid-stroke/2;
            vgui::surface()->DrawFilledRect(left,top+height*38/100,left+stroke,top+height*67/100);
            const int dot=top+height*77/100;
            vgui::surface()->DrawFilledRect(left,dot,left+stroke,dot+stroke);
        }
        else vgui::surface()->DrawOutlinedCircle(GetWide()/2,GetTall()/2,MIN(GetWide(),GetTall())/2-2,32);
    }
    void OnThink() OVERRIDE
    {
        BaseClass::OnThink();
        const bool show=vgui::ipanel()->IsFullyVisible(GetVPanel()) && HL2MPControlHighlighted(this);
        m_Tip->SetVisible(show);if(!show)return;
        int sw,sh;vgui::surface()->GetScreenSize(sw,sh);const int w=MIN(sw-32,MAX(280,sw/4));
        m_Tip->SetSize(w,sh/2);m_Tip->GetTextImage()->SetSize(w-24,sh/2);m_Tip->GetTextImage()->RecalculateNewLinePositions();
        int tw,th;m_Tip->GetTextImage()->GetContentSize(tw,th);m_Tip->SetTall(th+20);
        int sx=0,sy=0;LocalToScreen(sx,sy);int x=clamp(sx,8,sw-w-8),y=MIN(sy+GetTall()+8,sh-th-28);m_Tip->SetPos(x,y);
        m_Tip->SetPaintBackgroundEnabled(true);m_Tip->SetBgColor(Color(35,35,35,255));m_Tip->SetFgColor(HL2MPServerTheme::Text());m_Tip->InvalidateLayout(true);m_Tip->MoveToFront();
    }
    void DoClick() OVERRIDE { RequestFocus(); }
private:
    vgui::Label *m_Tip;
    const char *m_Hint;bool m_Warning;
};

class CHitmarkerPreview : public vgui::Panel
{
    DECLARE_CLASS_SIMPLE(CHitmarkerPreview,vgui::Panel);
public:
    CHitmarkerPreview(vgui::Panel *parent) : BaseClass(parent,"HitmarkerPreview"), m_Font(0),m_Choice(-1),m_Height(0),m_Color(255,0,0,255)
    { SetMouseInputEnabled(false); SetKeyBoardInputEnabled(false); }
    void Style(int choice,int height,Color color)
    {
        if(m_Choice!=choice || m_Height!=height) { m_Choice=choice;m_Height=height;m_Font=HL2MPGetHitmarkerFont(choice,height); }
        m_Color=color; Repaint();
    }
    void PaintBackground() OVERRIDE { HL2MPServerTheme::Rounded(0,0,GetWide(),GetTall(),Color(24,24,24,235),6); }
    void Paint() OVERRIDE
    {
        if(!m_Font)return;
        const float age=fmod(Plat_FloatTime(),.8);
        const wchar_t *text=L"-32";int w,h;vgui::surface()->GetTextSize(m_Font,text,w,h);
        vgui::surface()->DrawSetTextFont(m_Font);
        vgui::surface()->DrawSetTextColor(m_Color.r(),m_Color.g(),m_Color.b(),HL2MPHitmarkerAlpha(age));
        vgui::surface()->DrawSetTextPos((GetWide()-w)/2,GetTall()/2-int(age*m_Height*24/14));
        vgui::surface()->DrawPrintText(text,3);
    }
private:
    vgui::HFont m_Font;int m_Choice,m_Height;Color m_Color;
};

class CSettingRow : public vgui::EditablePanel
{
    DECLARE_CLASS_SIMPLE( CSettingRow, vgui::EditablePanel );
public:
    CSettingRow( vgui::Panel *parent, vgui::Panel *owner, int index, const Setting &value ) : BaseClass( parent, "Setting" ), m_Index( index ), m_Combo( NULL ), m_Slider( NULL ), m_Text( NULL ), m_Amount( NULL ), m_Scale( 1 ), m_InitialSlider( 0 ), m_InitialValue( value.draft ), m_Highlighted( false ), m_Available( value.available )
    {
        SetPaintBackgroundEnabled( true ); const SettingDef &def = s_Settings[index]; m_Label = Label( this, def.label ); m_Label->SetWrap( true );
        m_Spray = NULL; m_Color = NULL; m_Preview = NULL; m_Info = NULL; m_PreviewLabel = NULL;
        m_Warning=NULL;m_ServerBlocked=false;m_ChoiceBlocked=false;m_FootstepsBlocked=false;m_Audition=NULL;
        m_RemoteFootsteps=!Q_strcmp(def.cvar,"cl_footsteps_teammates") || !Q_strcmp(def.cvar,"cl_footsteps_enemies");
        m_Footsteps=m_RemoteFootsteps || !Q_strcmp(def.cvar,"cl_footsteps_self");
        if(m_RemoteFootsteps)m_Info=new CInfoButton(this,"#HL2MP_FootstepsRequirement");
        else if(m_Footsteps)m_Info=new CInfoButton(this,"#HL2MP_FootstepsServerHint");
        if(!Q_strcmp(def.cvar,"cl_hitsounds"))m_Info=new CInfoButton(this,"#HL2MP_HitSoundsHint");
        if(!Q_strcmp(def.cvar,"cl_hitsound"))
        {
            m_Audition=new CHL2MPSettingsButton(this,"#HL2MP_PreviewSound","hitsound_preview");
            m_Audition->RemoveActionSignalTarget(this);m_Audition->AddActionSignalTarget(owner);
        }
        if (!Q_strcmp(def.cvar,"cl_hitmarkers")) m_Info = new CInfoButton(this,"#HL2MP_HitmarkerHint");
        if (!Q_strcmp(def.cvar,"cl_bright_player_models")) m_Info = new CInfoButton(this,"#HL2MP_BrightModelsInfo");
        if(m_Info) { m_Warning=Label(this,"#HL2MP_ServerVisualDisabled");m_Warning->SetWrap(true);m_Warning->SetVisible(false); }
        if (def.kind == COLOR)
        {
            m_Color = new CHL2MPColorPicker(this,"Color"); m_Color->SetRGB(value.draft.String());
            m_Preview = new CHitmarkerPreview(this); m_PreviewLabel = Label(this,"#HL2MP_HitmarkerPreview");
        }
        else if ( def.kind == SPRAY ) m_Spray = new CHL2MPSprayPicker(this, value.draft.String());
        else if ( def.kind == SLIDER )
        {
            m_Slider = new CHL2MPSettingsSlider( this ); m_Slider->SetRange( int( def.low / def.step + 0.5f ), int( def.high / def.step + 0.5f ) );
            m_Slider->SetValue( int( atof( value.draft.String() ) / def.step + 0.5f ), false ); m_InitialSlider = m_Slider->GetValue();
            m_Amount = Label( this, "" ); m_Slider->SetEnabled( value.available );
        }
        else if ( def.kind == TEXT )
        {
            m_Text = new vgui::TextEntry( this, "Value" ); m_Text->SetMaximumCharCount( MAX_PLAYER_NAME_LENGTH - 1 ); m_Text->SetText( value.draft.String() ); m_Text->SetEnabled( value.available );
        }
        else
        {
            m_Combo = new CHL2MPDropdown( this, "Value" );
            m_Combo->ServerStyle();
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
                    HL2MPFormatInto( name, sizeof( name ), "%s   %s", size, fabs( aspect - 16.0f / 9 ) < .02f ? "16:9" : fabs( aspect - 16.0f / 10 ) < .02f ? "16:10" : fabs( aspect - 4.0f / 3 ) < .02f ? "4:3" : "" );
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
            if(!Q_strcmp(def.cvar,"cl_hitsound"))
            {
                for(int row=0;row<m_Combo->GetItemCount();++row)
                {
                    int id=m_Combo->GetItemIDFromRow(row);char name[256];m_Combo->GetItemText(id,name,sizeof(name));
                    char label[300];Q_snprintf(label,sizeof(label),"%02d  %s",row+1,HL2MPText(name));
                    KeyValues *data=m_Combo->GetItemUserData(id)->MakeCopy();
                    m_Combo->UpdateItem(id,label,data);data->deleteThis();
                }
            }
            // Keep custom/unsupported current values intact until explicitly changed.
            if ( selected < 0 ) { selected = m_Combo->GetItemCount(); AddChoice( m_Combo, value.available ? value.draft.String() : "#HL2MP_UI_eb0be6d636d5", value.draft.String() ); }
            m_Combo->SilentActivateItemByRow( selected ); m_Combo->SetEnabled( value.available );
        }
    }
    int Index() const { return m_Index; }
    void CloseDropdown() { if ( m_Combo ) m_Combo->HideMenu(); if (m_Spray) m_Spray->CloseDropdown(); }
    bool DropdownOpen() const { return (m_Combo && m_Combo->GetMenu()->IsVisible()) || (m_Spray && m_Spray->DropdownOpen()); }
    void EnableChoice(bool enabled)
    {
        if(m_Combo)m_Combo->SetEnabled(m_Available && enabled);m_Label->SetAlpha(enabled ? 255 : 110);
        if(m_RemoteFootsteps && m_ChoiceBlocked==enabled)
        {
            m_ChoiceBlocked=!enabled;RefreshWarning();
        }
    }
    void FocusChoice() { if(m_Combo)m_Combo->RequestFocus(); }
    CUtlString Value()
    {
        if (m_Color) return m_Color->Value();
        if (m_Spray) return m_Spray->Value();
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
        m_Scale = scale; SetTall( int( (m_Color ? 238 : m_Spray ? 196 : 52) * scale ) ); m_Label->SetFont( font );
        if(m_Color) { m_Color->Style(small);m_PreviewLabel->SetFont(small);m_PreviewLabel->SetFgColor(HL2MPServerTheme::Muted()); }
        if(m_Info) m_Info->SetFont(small);
        if(m_Audition)m_Audition->SetFont(small);
        if(m_Warning) { m_Warning->SetFont(small);m_Warning->SetFgColor(m_ServerBlocked ? Color(255,150,140,255) : HL2MPServerTheme::Muted()); }
        if (m_Spray) m_Spray->Style(scale, small);
        m_Label->SetFgColor( m_Highlighted ? Color( 244, 239, 226, 255 ) : Color( 206, 214, 213, 255 ) );
        if ( m_Combo ) m_Combo->Style( small, scale );
        if ( m_Text ) { m_Text->SetFont( font ); m_Text->SetBgColor( Color( 29, 29, 29, 255 ) ); m_Text->SetFgColor( HL2MPServerTheme::Text() ); m_Text->SetBorder( NULL ); }
        if ( m_Amount ) { m_Amount->SetFont( small ); m_Amount->SetFgColor( Color( 221, 227, 225, 255 ) ); }
        InvalidateLayout( true );
    }
    void PerformLayout() OVERRIDE
    {
        BaseClass::PerformLayout(); const int w = GetWide(), h = m_Info ? int(52*m_Scale) : GetTall(), valueW = int( w * .43f ), left = w - valueW - int( 12 * m_Scale );
        if(m_Warning)
        {
            const int pad=int(12*m_Scale);int th=0,tw=0;
            m_Warning->SetBounds(pad,h,w-pad*2,int(100*m_Scale));
            m_Warning->GetTextImage()->SetSize(w-pad*2,int(100*m_Scale));m_Warning->GetTextImage()->RecalculateNewLinePositions();
            m_Warning->GetTextImage()->GetContentSize(tw,th);m_Warning->SetTall(th);m_Warning->InvalidateLayout(true);
            const int tall=h+((m_ServerBlocked || m_ChoiceBlocked) ? th+pad : 0);
            if(tall!=GetTall())
            {
                SetTall(tall);
                // AddItem reparents rows to the list's embedded scrolling panel.
                for(vgui::Panel *parent=GetParent();parent;parent=parent->GetParent())
                    if(dynamic_cast<vgui::PanelListPanel *>(parent)) { parent->InvalidateLayout();break; }
            }
        }
        if(m_Color)
        {
            const int pad=int(12*m_Scale), split=w/2;
            m_Label->SetBounds(pad,0,split-pad,int(36*m_Scale));
            m_Label->GetTextImage()->SetSize(m_Label->GetWide(),m_Label->GetTall());m_Label->GetTextImage()->RecalculateNewLinePositions();m_Label->InvalidateLayout(true);
            m_Color->SetBounds(pad,int(40*m_Scale),split-2*pad,h-int(50*m_Scale));
            m_PreviewLabel->SetBounds(split+pad,0,w-split-2*pad,int(36*m_Scale));
            m_Preview->SetBounds(split+pad,int(40*m_Scale),w-split-2*pad,h-int(50*m_Scale));
            return;
        }
        if (m_Spray)
        {
            m_Label->SetBounds(int(8*m_Scale), 0, w-int(24*m_Scale), int(40*m_Scale));
            m_Spray->SetBounds(int(8*m_Scale), int(44*m_Scale), w-int(24*m_Scale), h-int(52*m_Scale));
            return;
        }
        m_Label->SetBounds( int( 8 * m_Scale ), 0, left - int( 16 * m_Scale ), h );
        if(m_Info) { const int size=int(26*m_Scale);m_Info->SetBounds(left-size-int(12*m_Scale),(h-size)/2,size,size);m_Label->SetWide(left-size-int(32*m_Scale)); }
        m_Label->GetTextImage()->SetSize( m_Label->GetWide(), h );
        m_Label->GetTextImage()->RecalculateNewLinePositions();
        m_Label->InvalidateLayout( true );
        if ( m_Combo ) m_Combo->SetBounds( left, int( 9 * m_Scale ), valueW, h - int( 18 * m_Scale ) );
        if(m_Audition) { m_Combo->SetWide(valueW-int(118*m_Scale));m_Audition->SetBounds(w-int(122*m_Scale),int(9*m_Scale),int(110*m_Scale),h-int(18*m_Scale)); }
        if ( m_Text ) m_Text->SetBounds( left, int( 9 * m_Scale ), valueW, h - int( 18 * m_Scale ) );
        if ( m_Slider )
        {
            m_Slider->SetBounds( left, 0, valueW - int( 56 * m_Scale ), h );
            m_Amount->SetBounds( w - int( 60 * m_Scale ), 0, int( 54 * m_Scale ), h );
        }
    }
    void OnThink() OVERRIDE
    {
        BaseClass::OnThink();
        if(m_Info)
        {
            const bool connected=engine->IsInGame() && !engine->IsLevelMainMenuBackground();
            const bool footstepsBlocked=connected && m_Footsteps && !sv_footsteps.GetBool();
            const bool blocked=footstepsBlocked || (connected &&
                (!Q_strcmp(s_Settings[m_Index].cvar,"cl_hitmarkers") ? !sv_hitmarkers.GetBool() :
                 !Q_strcmp(s_Settings[m_Index].cvar,"cl_hitsounds") ? !sv_hitsounds.GetBool() :
                 (!m_Footsteps || m_RemoteFootsteps) && !HL2MPBrightModels_Allowed()));
            if(blocked!=m_ServerBlocked || footstepsBlocked!=m_FootstepsBlocked)
            {
                m_ServerBlocked=blocked;m_FootstepsBlocked=footstepsBlocked;RefreshWarning();
            }
        }
        const bool highlighted = m_Available && ( HL2MPControlHighlighted( this ) || DropdownOpen() );
        if ( highlighted != m_Highlighted )
        {
            m_Highlighted = highlighted;
            m_Label->SetFgColor( highlighted ? Color( 244, 239, 226, 255 ) : Color( 206, 214, 213, 255 ) ); Repaint();
        }
        if ( m_Amount )
        {
            const float value = atof( Value().String() );
            m_Amount->SetText( s_Settings[m_Index].choices[0] == '%' ? VarArgs( "%.0f%%", value * 100 ) : s_Settings[m_Index].step < 1 ? VarArgs( "%.2f", value ) : VarArgs( "%.0f", value ) );
        }
    }
    void PaintBackground() OVERRIDE
    {
        HL2MPPaintSettingRow( this, m_Highlighted, m_Scale );
        if(m_ServerBlocked)
        {
            vgui::surface()->DrawSetColor(Color(135,30,25,m_Highlighted ? 115 : 70));vgui::surface()->DrawFilledRect(0,0,GetWide(),GetTall());
            vgui::surface()->DrawSetColor(Color(255,107,97,255));vgui::surface()->DrawFilledRect(0,0,MAX(2,int(2*m_Scale)),GetTall());
        }
    }
    void PreviewFont(int choice) { if(m_Preview)m_Preview->Style(choice,MAX(12,int(21*m_Scale)),m_Color->GetColor()); }
private:
    void RefreshWarning()
    {
        const char *warning=m_FootstepsBlocked ? "#HL2MP_FootstepsServerDisabled" : "#HL2MP_ServerVisualDisabled";
        m_Info->SetWarning(m_ServerBlocked,warning);
        m_Warning->SetText(m_RemoteFootsteps && !m_ServerBlocked ? "#HL2MP_FootstepsRequirement" : warning);
        m_Warning->SetFgColor(m_ServerBlocked ? Color(255,150,140,255) : HL2MPServerTheme::Muted());
        m_Warning->SetVisible(m_ServerBlocked || m_ChoiceBlocked);
        InvalidateLayout(true);Repaint();
    }
    int m_Index; CHL2MPDropdown *m_Combo; CHL2MPSettingsSlider *m_Slider; vgui::TextEntry *m_Text;
    vgui::Label *m_Label, *m_Amount; float m_Scale; int m_InitialSlider; CUtlString m_InitialValue;
    bool m_Highlighted, m_Available;
    CHL2MPSprayPicker *m_Spray;
    CHL2MPColorPicker *m_Color; CHitmarkerPreview *m_Preview; CInfoButton *m_Info; vgui::Label *m_PreviewLabel;
    vgui::Label *m_Warning;bool m_ServerBlocked,m_ChoiceBlocked,m_RemoteFootsteps,m_Footsteps,m_FootstepsBlocked;CHL2MPSettingsButton *m_Audition;
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
        HL2MPPaintSettingRow( this, HL2MPControlHighlighted( this ), m_Scale );
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
        discard = new CHL2MPSettingsButton( this, "#HL2MP_UI_f07a6caf7491", "confirm_discard" );
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
        vgui::surface()->DrawSetColor( Color( 24, 24, 24, 250 ) ); vgui::surface()->DrawFilledRect( 0, 0, GetWide(), GetTall() );
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
        BaseClass::ApplySchemeSettings( scheme ); SetBorder( NULL ); SetBackgroundColor( Color( 24, 24, 24, 255 ) );
    }
    void Paint() OVERRIDE
    {
        BaseClass::Paint();
        vgui::surface()->DrawSetColor( HL2MPServerTheme::Orange() );
        vgui::surface()->DrawOutlinedRect( 0, 0, GetWide(), GetTall() );
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
    void ResetPage(); void CloseDropdowns();bool HitmarkerDetails() const;
    const char *KeyName( int action, int slot ); int KeyCode( int action, int slot );
    MESSAGE_FUNC_INT( OnCaptureKey, "CaptureKey", code );
    Setting m_Settings[ARRAYSIZE( s_Settings )];
    CUtlString m_BindingOriginal[BUTTON_CODE_LAST], m_BindingDraft[BUTTON_CODE_LAST];
    CUtlVector<CSettingRow *> m_Rows; CUtlVector<CBindingRow *> m_KeyRows;
    vgui::PanelListPanel *m_List; vgui::Label *m_Heading, *m_Brand, *m_Status, *m_Hint, *m_PreviewTitle, *m_PreviewBody;
    CHL2MPSettingsButton *m_Back, *m_HudEditor, *m_Tabs[5], *m_Subtabs[2], *m_Apply, *m_Discard, *m_Reset;
    CPlayerPreview *m_PlayerPreview;
    COptionsOverlay *m_Overlay; vgui::HFont m_Font, m_SmallFont, m_TitleFont, m_BrandFont;
    Page m_Page; float m_Scale; int m_Width, m_Height, m_Confirmation, m_CaptureAction, m_CaptureSlot;
    int m_OldWidth, m_OldHeight, m_OldWindowed; double m_DisplayDeadline, m_FontRefreshAt; bool m_CloseAfterApply;
    bool m_HitmarkerDetails=true;
};
static CHL2MPOptions *s_Options = NULL;

CHL2MPOptions::CHL2MPOptions( vgui::Panel *parent ) : BaseClass( parent, "Options" ), m_Page( DISPLAY ), m_Scale( 1 ), m_Width( 0 ), m_Height( 0 ), m_Confirmation( 0 ), m_CaptureAction( -1 ), m_CaptureSlot( 0 ), m_OldWidth( 0 ), m_OldHeight( 0 ), m_OldWindowed( 0 ), m_DisplayDeadline( 0 ), m_FontRefreshAt( 0 ), m_CloseAfterApply( false )
{
    SetVisible( false ); SetZPos( 100 ); SetKeyBoardInputEnabled( true ); SetMouseInputEnabled( true );
    m_Font = vgui::surface()->CreateFont(); m_SmallFont = vgui::surface()->CreateFont(); m_TitleFont = vgui::surface()->CreateFont(); m_BrandFont = vgui::surface()->CreateFont();
    m_Heading = Label( this, "#HL2MP_UI_02f6ea12816a" ); m_Brand = Label( this, "deathmatch" ); m_Status = Label( this, "" ); m_Hint = Label( this, "" ); m_Hint->SetWrap( true );
    m_PlayerPreview = new CPlayerPreview( this );
    m_PreviewTitle = Label( this, "#HL2MP_UI_e40d2cc4d4b8" ); m_PreviewBody = Label( this, "#HL2MP_UI_3f22979928a9" );
    m_Back = new CHL2MPSettingsButton( this, "#HL2MP_UI_24c76252a90c", "back" );
    m_HudEditor = new CHL2MPSettingsButton( this, "#HL2MP_HudEditor", "hud_editor" );
    const char *tabs[] = { "#HL2MP_UI_65b2f9ec331f", "#HL2MP_UI_859e89a729c2", "#HL2MP_UI_1fd5f5a768d3", "#HL2MP_UI_e3afec8a791d", "#HL2MP_UI_d730947f78c9" };
    for ( int i = 0; i < 5; ++i ) m_Tabs[i] = new CHL2MPSettingsButton( this, tabs[i], VarArgs( "tab_%d", i ) );
    for ( int i = 0; i < 2; ++i ) m_Subtabs[i] = new CHL2MPSettingsButton( this, "", VarArgs( "sub_%d", i ) );
    m_Apply = new CHL2MPSettingsButton( this, "#HL2MP_UI_33bf9eeb20a9", "apply" ); m_Discard = new CHL2MPSettingsButton( this, "#HL2MP_UI_90ffc153ae42", "discard" ); m_Reset = new CHL2MPSettingsButton( this, "#HL2MP_UI_1897247b6a8e", "reset" );
    m_List = new vgui::PanelListPanel( this, "Settings" ); m_List->SetFirstColumnWidth( 0 ); m_List->SetVerticalBufferPixels( 0 );
    HL2MPStyleScrollBar( m_List->GetScrollbar() );
    KeyValues *list = new KeyValues( "List" ); list->SetInt( "autohide_scrollbar", 1 ); m_List->ApplySettings( list ); list->deleteThis();
    m_Overlay = new COptionsOverlay( this ); vgui::ivgui()->AddTickSignal( GetVPanel(), 100 ); s_Options = this;
}
CHL2MPOptions::~CHL2MPOptions()
{
    if ( IsVisible() ) engine->ClientCmd_Unrestricted( "gameui_allowescape" );
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
    if ( IsVisible() ) return;
    // GameUI handles Escape independently of VGUI's modal key capture.
    engine->ClientCmd_Unrestricted( "gameui_preventescape" );
    ReadCurrent(); SetVisible( true ); MoveToFront(); BuildRows(); InvalidateLayout( true ); m_Back->RequestFocus();
}
void CHL2MPOptions::CloseDropdowns()
{
    FOR_EACH_VEC( m_Rows, i ) m_Rows[i]->CloseDropdown();
}
void CHL2MPOptions::ReadRows() { FOR_EACH_VEC( m_Rows, i ) m_Settings[m_Rows[i]->Index()].draft = m_Rows[i]->Value(); }
bool CHL2MPOptions::Dirty()
{
    if ( IsVisible() ) ReadRows();
    for ( int i = 0; i < ARRAYSIZE( s_Settings ); ++i ) if ( !SameValue( m_Settings[i].original.String(), m_Settings[i].draft.String(), s_Settings[i].kind != TEXT ) ) return true;
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
    int code = KeyCode( action, slot ); return code ? HL2MPKeyName( inputsystem->ButtonCodeToString( ButtonCode_t( code ) ) ) : "#HL2MP_UI_d16f3e00e8e4";
}
bool CHL2MPOptions::HitmarkerDetails() const
{
    for(int i=0;i<ARRAYSIZE(s_Settings);++i) if(!Q_strcmp(s_Settings[i].cvar,"cl_hitmarkers")) return atoi(m_Settings[i].draft.String())!=0;
    return false;
}
void CHL2MPOptions::BuildRows()
{
    CloseDropdowns(); m_List->DeleteAllItems(); m_Rows.Purge(); m_KeyRows.Purge();
    m_HitmarkerDetails=HitmarkerDetails();
    if ( m_Page == BINDINGS )
    {
        for ( int i = 0; i < ARRAYSIZE( s_Bindings ); ++i ) { CBindingRow *row = new CBindingRow( m_List, this, i ); m_KeyRows.AddToTail( row ); m_List->AddItem( NULL, row ); }
    }
    else for ( int i = 0; i < ARRAYSIZE( s_Settings ); ++i ) if ( s_Settings[i].page == m_Page )
    {
        if(!m_HitmarkerDetails && (!Q_strcmp(s_Settings[i].cvar,"cl_hitmarker_font") || !Q_strcmp(s_Settings[i].cvar,"cl_hitmarker_color")))continue;
        CSettingRow *row = new CSettingRow( m_List, this, i, m_Settings[i] ); m_Rows.AddToTail( row ); m_List->AddItem( NULL, row );
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
    m_HudEditor->SetFont( m_SmallFont ); m_HudEditor->SetBounds( int( w * .43f ), int( 20 * s ), int( 200 * s ), int( 35 * s ) );
    const int tabWidth[] = { 110, 110, 100, 230, 160 }; const int sum = 710; int x = tabX;
    const int category = m_Page <= ADVANCED ? 0 : m_Page == AUDIO ? 1 : m_Page == GAME ? 2 : m_Page <= BINDINGS ? 3 : 4;
    for ( int i = 0; i < 5; ++i ) { int width = tabW * tabWidth[i] / sum; m_Tabs[i]->SetFont( m_SmallFont ); m_Tabs[i]->SetBounds( x, int( 78 * s ), width, int( 35 * s ) ); m_Tabs[i]->Active( category == i ); x += width; }
    for ( int i = 0; i < 2; ++i )
    {
        m_Subtabs[i]->SetVisible( category == 0 || category == 3 ); m_Subtabs[i]->SetFont( m_SmallFont );
        m_Subtabs[i]->SetBounds( w / 2 + int( ( i * 180 - 180 ) * s ), int( 126 * s ), int( 180 * s ), int( 30 * s ) );
        m_Subtabs[i]->SetText( category == 0 ? i ? "#HL2MP_UI_4abe99dcd8fd" : "#HL2MP_UI_27cd427750d2" : i ? "#HL2MP_UI_9c30292123d6" : "#HL2MP_UI_92297327e6e2" );
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
    m_Hint->SetText( m_Page == AUDIO || m_Page == GAME ? "" : m_Page == DISPLAY ? "#HL2MP_UI_e9808de2e7db" : m_Page == ADVANCED ? "#HL2MP_UI_cd58f604c67e" : m_Page == BINDINGS ? "#HL2MP_UI_766afd9b32b4" : m_Page == MULTIPLAYER ? "#HL2MP_UI_fec06a334932" : "#HL2MP_UI_16a1cb4185e4" );
    const int footerY = h - int( 61 * s ), buttonW = int( 210 * s );
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
    surface->DrawSetColor( Color( 32, 32, 32, 250 ) );
    surface->DrawFilledRect( 0, 0, w, h );
    surface->DrawSetColor( Color( 26, 26, 26, 255 ) ); surface->DrawFilledRect( 0, 0, w, int( 68 * m_Scale ) ); surface->DrawFilledRect( 0, h - int( 76 * m_Scale ), w, h );
    surface->DrawSetColor( Color( 220, 220, 220, 30 ) ); surface->DrawFilledRect( 0, int( 68 * m_Scale ), w, int( 68 * m_Scale ) + 1 ); surface->DrawFilledRect( 0, h - int( 76 * m_Scale ), w, h - int( 76 * m_Scale ) + 1 );
}
void CHL2MPOptions::Confirm( int type )
{
    CloseDropdowns(); m_Confirmation = type; m_Overlay->capture = type == 3; m_Overlay->SetVisible( true ); m_Overlay->MoveToFront();
    m_Overlay->yes->SetVisible( type != 3 ); m_Overlay->no->SetVisible( type != 3 );
    m_Overlay->discard->SetVisible( type == 2 );
    m_Overlay->title->SetText( type == 1 ? "#HL2MP_UI_e9f18dd287a2" : type == 2 ? "#HL2MP_UI_1b5e21310a85" : HL2MPFormat( "BIND: %s", HL2MPText( s_Bindings[m_CaptureAction].label ) ) );
    m_Overlay->body->SetText( type == 2 ? "#HL2MP_UI_d49587ce0499" : type == 3 ? "#HL2MP_UI_3ca6ad236ca5" : "" );
    m_Overlay->yes->SetText( type == 1 ? "#HL2MP_UI_efcae5aba181" : "#HL2MP_UI_568026c7f7b8" ); m_Overlay->no->SetText( type == 1 ? "#HL2MP_UI_d93ce6b97ed7" : "#HL2MP_UI_5425cd70bd52" );
    vgui::input()->SetAppModalSurface( m_Overlay->GetVPanel() ); m_Overlay->RequestFocus();
    InvalidateLayout( true );
}
void CHL2MPOptions::Close()
{
    engine->ClientCmd_Unrestricted( "gameui_allowescape" );
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
    for (int i=0; i<ARRAYSIZE(s_Settings); ++i)
        if (s_Settings[i].kind==SPRAY && m_Settings[i].draft!=m_Settings[i].original &&
            !HL2MPSprays_Apply(m_Settings[i].draft.String()))
        { m_Status->SetText("#HL2MP_SprayImportFailed"); return; }
    for ( int i = 0; i < ARRAYSIZE( s_Settings ); ++i )
    {
        if ( !s_Settings[i].cvar[0] || !m_Settings[i].available || SameValue( m_Settings[i].original.String(), m_Settings[i].draft.String(), s_Settings[i].kind != TEXT ) ) continue;
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
    m_DisplayDeadline = 0; m_Status->SetText( "#HL2MP_UI_9cf6dbd76c0c" );
    if ( m_CloseAfterApply ) { m_CloseAfterApply = false; Close(); }
}
void CHL2MPOptions::RevertDisplay()
{
    engine->ClientCmd_Unrestricted( VarArgs( "mat_setvideomode %d %d %d\n", m_OldWidth, m_OldHeight, m_OldWindowed ) );
    m_DisplayDeadline = 0; m_CloseAfterApply = false; m_FontRefreshAt = Plat_FloatTime() + 1;
    for ( int i = 0; i < ARRAYSIZE( s_Settings ); ++i )
        if ( s_Settings[i].kind == RESOLUTION || s_Settings[i].kind == WINDOW_MODE ) m_Settings[i].draft = m_Settings[i].original;
    m_Status->SetText( "#HL2MP_UI_982a12acf6fe" ); BuildRows();
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
            if ( !s_Bindings[i].key[0] ) continue;
            ButtonCode_t key = inputsystem->StringToButtonCode( s_Bindings[i].key ); if ( key > BUTTON_CODE_NONE && key < BUTTON_CODE_LAST ) m_BindingDraft[key] = s_Bindings[i].command;
        }
    }
    BuildRows(); m_Status->SetText( "#HL2MP_UI_5a0f171a40a2" );
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
    if(!Q_strcmp(command,"hitsound_preview"))
    {
        ReadRows();int sound=0;float volume=0.5f;
        for(int i=0;i<ARRAYSIZE(s_Settings);++i)
        {
            if(!Q_strcmp(s_Settings[i].cvar,"cl_hitsound"))sound=atoi(m_Settings[i].draft.String());
            if(!Q_strcmp(s_Settings[i].cvar,"cl_hitsound_volume"))volume=atof(m_Settings[i].draft.String());
        }
        HL2MPPlayHitSound(sound,volume);return;
    }
    if ( !Q_strcmp( command, "hud_editor" ) ) { HL2MPHudEditor_Open(); return; }
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
    else if ( !Q_strcmp( command, "discard" ) ) { ReadCurrent(); BuildRows(); m_Status->SetText( "#HL2MP_UI_6fb7a4651054" ); }
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
        m_Overlay->body->SetText( HL2MPFormat( "The previous display mode will be restored in %d seconds unless you keep these changes.", MAX( 0, int( ceil( m_DisplayDeadline - Plat_FloatTime() ) ) ) ) );
        if ( Plat_FloatTime() >= m_DisplayDeadline ) OnCommand( "confirm_no" );
    }
    if ( !IsVisible() ) return;
    int w, h; vgui::surface()->GetScreenSize( w, h ); if ( w != m_Width || h != m_Height ) InvalidateLayout( true );
    const bool dirty = Dirty();
    if(m_Page == GAME)
    {
        // Dirty() captured every visible row above. Rebuilding only changes
        // visibility; hidden font/color drafts remain part of Apply/Discard.
        if(m_HitmarkerDetails!=HitmarkerDetails()) { BuildRows();if(m_Rows.Count())m_Rows[0]->FocusChoice(); }
        int font=0; for(int i=0;i<ARRAYSIZE(s_Settings);++i) if(!Q_strcmp(s_Settings[i].cvar,"cl_hitmarker_font")) font=atoi(m_Settings[i].draft.String());
        FOR_EACH_VEC(m_Rows,i) m_Rows[i]->PreviewFont(font);
    }
    if (m_Page == AUDIO)
    {
        bool bright = false;
        for (int i=0; i<ARRAYSIZE(s_Settings); ++i)
            if (!Q_strcmp(s_Settings[i].cvar,"cl_bright_player_models")) bright = atoi(m_Settings[i].draft.String()) != 0;
        FOR_EACH_VEC(m_Rows, i)
        {
            const char *cvar = s_Settings[m_Rows[i]->Index()].cvar;
            if (!Q_strcmp(cvar,"cl_footsteps_teammates") || !Q_strcmp(cvar,"cl_footsteps_enemies")) m_Rows[i]->EnableChoice(bright && (!engine->IsInGame() || (HL2MPBrightModels_Allowed() && sv_footsteps.GetBool())));
        }
    }
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
            if ( !found ) m_PreviewTitle->SetText( "#HL2MP_UI_c8154474aee0" );
        }
    }
    m_Apply->SetEnabled( dirty ); m_Apply->Active( dirty ); m_Discard->SetEnabled( dirty );
    if ( dirty ) m_Status->SetText( "#HL2MP_UI_5695b150c3dd" );
    else
    {
        char status[1024]; m_Status->GetText( status, sizeof( status ) );
        if ( !Q_strcmp( status, HL2MPText( "#HL2MP_UI_5695b150c3dd" ) ) ) m_Status->SetText( "" );
    }
}
}

vgui::Panel *HL2MPOptions_Create( vgui::Panel *parent ) { return new CHL2MPOptions( parent ); }
void HL2MPOptions_Open( vgui::Panel *panel ) { static_cast<CHL2MPOptions *>( panel )->Open(); }
bool HL2MPOptions_RestoreFocus()
{
    if ( !s_Options || !s_Options->IsVisible() ) return false;
    s_Options->RequestFocus(); engine->ClientCmd_Unrestricted( "gameui_preventescape" ); return true;
}
CON_COMMAND_F( mm_options_status, "Show Options page and pending changes.", FCVAR_CLIENTDLL )
{
    if ( s_Options ) Msg( "Options: visible=%d page=%d dirty=%d\n", s_Options->IsVisible(), s_Options->CurrentPage(), s_Options->Dirty() );
}
