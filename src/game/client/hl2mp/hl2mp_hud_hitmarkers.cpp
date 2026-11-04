#include "cbase.h"
#include "hudelement.h"
#include "hud_macros.h"
#include "iclientmode.h"
#include "view.h"
#include "hl2mp_hitmarker_style.h"
#include "hl2mp_hitsounds.h"
#include "engine/IEngineSound.h"
#include <vgui/ISurface.h>
#include <vgui_controls/Panel.h>
#include "tier1/utlstring.h"
#ifdef _WIN32
#include <windows.h>
#undef CreateFont
#pragma comment(lib,"gdi32.lib")
#endif

#include "tier0/memdbgon.h"

static ConVar cl_hitmarkers( "cl_hitmarkers", "0", FCVAR_ARCHIVE, "Show damage numbers when hitting enemy players, if allowed by the server." );
extern ConVar sv_hitmarkers, sv_hitsounds;
static ConVar cl_hitsounds("cl_hitsounds","0",FCVAR_ARCHIVE,"Play server-confirmed enemy hit sounds when permitted.");
static ConVar cl_hitsound("cl_hitsound","0",FCVAR_ARCHIVE,"Hit sound selection (0-15).",true,0,true,HL2MP_HIT_SOUND_COUNT-1);
static ConVar cl_hitsound_volume("cl_hitsound_volume","0.5",FCVAR_ARCHIVE,"Hit sound volume.",true,0,true,1);
void HL2MPPlayHitSound(int sound,float volume)
{
    char path[80];Q_snprintf(path,sizeof(path),"ui/hitsounds/hit_%02d.wav",clamp(sound,0,HL2MP_HIT_SOUND_COUNT-1));
    enginesound->PrecacheSound(path,true);
    enginesound->EmitAmbientSound(path,clamp(volume,0.f,1.f));
}

bool HL2MPHitmarkerRegisterFont(const char *face,const char *path)
{
    if(vgui::surface()->AddCustomFontFile(face,path))return true;
#ifdef _WIN32
    // Some SDK Windows runtimes reject dynamic font-file registration. Keep
    // these fonts private to the game; they are never installed system-wide.
    struct PrivateFonts
    {
        CUtlVector<CUtlString> paths;
        ~PrivateFonts() { FOR_EACH_VEC(paths,i) RemoveFontResourceExA(paths[i].String(),FR_PRIVATE,NULL); }
    };
    static PrivateFonts fonts;
    FOR_EACH_VEC(fonts.paths,i) if(!Q_stricmp(fonts.paths[i].String(),path))return true;
    if(!AddFontResourceExA(path,FR_PRIVATE,NULL))return false;
    fonts.paths.AddToTail(CUtlString(path));return true;
#else
    return false;
#endif
}
ConVar cl_hitmarker_font("cl_hitmarker_font","5",FCVAR_ARCHIVE,"Damage number font: 0 classic, 1 GorDIN, 2 GorDIN Bold, 3 Tahoma, 4 TF2, 5 TF2 Build, 6 TF2 Secondary, 7 TF2 Professor.",true,0,true,HL2MP_HITMARKER_FONTS-1);
ConVar cl_hitmarker_color("cl_hitmarker_color","255 0 0",FCVAR_ARCHIVE,"Damage number color as red green blue (0-255).");

class CHudHitMarkers : public CHudElement, public vgui::Panel
{
	DECLARE_CLASS_SIMPLE( CHudHitMarkers, vgui::Panel );

public:
	CHudHitMarkers( const char *pElementName );
	void Init() OVERRIDE;
	void VidInit() OVERRIDE;
	void Reset() OVERRIDE;
	void LevelShutdown() OVERRIDE;
	bool ShouldDraw() OVERRIDE;
	void ApplySchemeSettings( vgui::IScheme *pScheme ) OVERRIDE;
	void Paint() OVERRIDE;
	void MsgFunc_DamageHit( bf_read &msg );
	void MsgFunc_HitSound(bf_read &msg);

private:
	struct DamageNumber_t
	{
		Vector m_vecOrigin;
		int m_nDamage;
		float m_flStartTime;
		float m_flOffset;
	};

	CUtlVector<DamageNumber_t> m_DamageNumbers;
	vgui::HFont m_hFont;
	int m_FontChoice = -1;
};

DECLARE_HUDELEMENT( CHudHitMarkers );
DECLARE_HUD_MESSAGE( CHudHitMarkers, DamageHit );
DECLARE_HUD_MESSAGE(CHudHitMarkers, HitSound);

CHudHitMarkers::CHudHitMarkers( const char *pElementName ) :
	CHudElement( pElementName ), BaseClass( NULL, "HudHitMarkers" ), m_hFont( 0 )
{
	SetParent( g_pClientMode->GetViewport() );
	SetHiddenBits( HIDEHUD_MISCSTATUS | HIDEHUD_PLAYERDEAD );
	SetMouseInputEnabled( false );
	SetKeyBoardInputEnabled( false );
}

void CHudHitMarkers::Init()
{
	HOOK_HUD_MESSAGE( CHudHitMarkers, DamageHit );
	HOOK_HUD_MESSAGE(CHudHitMarkers, HitSound);
}

void CHudHitMarkers::VidInit()
{
	Reset();
}

void CHudHitMarkers::Reset()
{
	m_DamageNumbers.RemoveAll();
}

void CHudHitMarkers::LevelShutdown()
{
	Reset();
}

bool CHudHitMarkers::ShouldDraw()
{
	C_BasePlayer *pPlayer = C_BasePlayer::GetLocalPlayer();
	if ( !cl_hitmarkers.GetBool() || !sv_hitmarkers.GetBool() || !pPlayer || !pPlayer->IsAlive() )
	{
		Reset();
		return false;
	}

	FOR_EACH_VEC_BACK( m_DamageNumbers, i )
	{
		float flAge = gpGlobals->curtime - m_DamageNumbers[i].m_flStartTime;
		if ( flAge < 0.0f || flAge >= 0.8f )
			m_DamageNumbers.Remove( i );
	}

	return m_DamageNumbers.Count() > 0 && CHudElement::ShouldDraw();
}

void CHudHitMarkers::ApplySchemeSettings( vgui::IScheme *pScheme )
{
	BaseClass::ApplySchemeSettings( pScheme );
	SetPaintBackgroundEnabled( false );
	SetPaintBorderEnabled( false );
	SetBounds( 0, 0, ScreenWidth(), ScreenHeight() );

	m_FontChoice = cl_hitmarker_font.GetInt();
	m_hFont=HL2MPGetHitmarkerFont(m_FontChoice,MAX(12,YRES(14)));
}

void CHudHitMarkers::MsgFunc_HitSound(bf_read &msg)
{
    if (!msg.IsOverflowed() && engine->IsInGame() && sv_hitsounds.GetBool() && cl_hitsounds.GetBool())
    {
        DevMsg(2,"Server-confirmed hit sound: %d\n",cl_hitsound.GetInt());
        HL2MPPlayHitSound(cl_hitsound.GetInt(),cl_hitsound_volume.GetFloat());
    }
}

void CHudHitMarkers::MsgFunc_DamageHit( bf_read &msg )
{
	DamageNumber_t number;
	number.m_nDamage = msg.ReadLong();
	number.m_vecOrigin.x = msg.ReadFloat();
	number.m_vecOrigin.y = msg.ReadFloat();
	number.m_vecOrigin.z = msg.ReadFloat();

	C_BasePlayer *pPlayer = C_BasePlayer::GetLocalPlayer();
	if ( msg.IsOverflowed() || number.m_nDamage <= 0 || !number.m_vecOrigin.IsValid() ||
		!cl_hitmarkers.GetBool() || !sv_hitmarkers.GetBool() || !pPlayer || !pPlayer->IsAlive() )
		return;

	trace_t trace;
	UTIL_TraceLine( MainViewOrigin(), number.m_vecOrigin, MASK_SOLID_BRUSHONLY, pPlayer, COLLISION_GROUP_NONE, &trace );
	if ( trace.fraction < 1.0f || trace.startsolid )
		return;

	if ( m_DamageNumbers.Count() >= 32 )
		m_DamageNumbers.Remove( 0 );

	number.m_flStartTime = gpGlobals->curtime;
	number.m_flOffset = random->RandomFloat( -8.0f, 8.0f );
	m_DamageNumbers.AddToTail( number );
}

void CHudHitMarkers::Paint()
{
	if (m_FontChoice != cl_hitmarker_font.GetInt())
	{
		m_FontChoice = cl_hitmarker_font.GetInt();
		m_hFont=HL2MPGetHitmarkerFont(m_FontChoice,MAX(12,YRES(14)));
	}
	const Color color = HL2MPHitmarkerColor(cl_hitmarker_color.GetString());
	vgui::surface()->DrawSetTextFont( m_hFont );
	FOR_EACH_VEC( m_DamageNumbers, i )
	{
		const DamageNumber_t &number = m_DamageNumbers[i];
		float flAge = gpGlobals->curtime - number.m_flStartTime;
		if ( flAge < 0.0f || flAge >= 0.8f )
			continue;

		trace_t trace;
		UTIL_TraceLine( MainViewOrigin(), number.m_vecOrigin, MASK_SOLID_BRUSHONLY,
			C_BasePlayer::GetLocalPlayer(), COLLISION_GROUP_NONE, &trace );
		if ( trace.fraction < 1.0f || trace.startsolid )
			continue;

		int x, y;
		if ( !GetVectorInHudSpace( number.m_vecOrigin, x, y ) )
			continue;

		wchar_t text[16];
		V_swprintf_safe( text, L"-%d", number.m_nDamage );
		int wide, tall;
		vgui::surface()->GetTextSize( m_hFont, text, wide, tall );
		x += YRES( number.m_flOffset ) - wide / 2;
		y -= tall + YRES( 8 + flAge * 24 );
		vgui::surface()->DrawSetTextColor( color.r(), color.g(), color.b(), HL2MPHitmarkerAlpha(flAge) );
		vgui::surface()->DrawSetTextPos( x, y );
		vgui::surface()->DrawPrintText( text, V_wcslen( text ) );
	}
}
