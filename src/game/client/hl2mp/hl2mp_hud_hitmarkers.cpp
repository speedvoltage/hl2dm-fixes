#include "cbase.h"
#include "hudelement.h"
#include "hud_macros.h"
#include "iclientmode.h"
#include "view.h"
#include <vgui/ISurface.h>
#include <vgui_controls/Panel.h>

#include "tier0/memdbgon.h"

static ConVar cl_hitmarkers( "cl_hitmarkers", "1", FCVAR_ARCHIVE, "Show damage numbers when hitting enemy players, if allowed by the server." );
extern ConVar sv_hitmarkers;

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
};

DECLARE_HUDELEMENT( CHudHitMarkers );
DECLARE_HUD_MESSAGE( CHudHitMarkers, DamageHit );

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

	if ( !m_hFont )
		m_hFont = vgui::surface()->CreateFont();

	vgui::surface()->SetFontGlyphSet( m_hFont, "HalfLife2", MAX( 12, YRES( 14 ) ), 700, 0, 0,
		vgui::ISurface::FONTFLAG_ANTIALIAS | vgui::ISurface::FONTFLAG_DROPSHADOW | vgui::ISurface::FONTFLAG_CUSTOM );
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
		int alpha = (int)( 255.0f * RemapValClamped( flAge, 0.4f, 0.8f, 1.0f, 0.0f ) );
		vgui::surface()->DrawSetTextColor( 255, 0, 0, alpha );
		vgui::surface()->DrawSetTextPos( x, y );
		vgui::surface()->DrawPrintText( text, V_wcslen( text ) );
	}
}
