#include "cbase.h"
#include "hud.h"
#include "hudelement.h"
#include "iclientmode.h"
#include "c_playerresource.h"
#include "c_team.h"
#include "hl2mp_gamerules.h"
#include "vgui/ILocalize.h"
#include "vgui/ISurface.h"
#include "vgui_controls/Panel.h"
#include <limits.h>
#include "tier0/memdbgon.h"

extern ConVar fraglimit;

static ConVar cl_show_server_info( "cl_show_server_info", "1", FCVAR_ARCHIVE, "Show the server time left, frag limit progress and team scores.", true, 0, true, 1 );

class CHudServerInfo : public CHudElement, public vgui::Panel
{
	DECLARE_CLASS_SIMPLE( CHudServerInfo, vgui::Panel );

public:
	CHudServerInfo( const char *pElementName );
	virtual bool ShouldDraw();
	virtual void OnThink();
	virtual void ApplySchemeSettings( vgui::IScheme *pScheme );
	virtual void Paint();

private:
	void DrawCenteredText( const wchar_t *pText, vgui::HFont hFont, int x, int y, const Color &color );

	vgui::HFont m_hFont;
	vgui::HFont m_hSmallFont;
	int m_iScreenWidth;
	int m_iScreenHeight;
	int m_iSpacing;
};

DECLARE_HUDELEMENT( CHudServerInfo );

CHudServerInfo::CHudServerInfo( const char *pElementName ) : CHudElement( pElementName ), BaseClass( NULL, "HudServerInfo" )
{
	SetParent( g_pClientMode->GetViewport() );
	SetHiddenBits( HIDEHUD_MISCSTATUS );
	SetMouseInputEnabled( false );
	SetKeyBoardInputEnabled( false );
	SetPaintBackgroundEnabled( false );
	SetPaintBorderEnabled( false );
	m_hFont = vgui::surface()->CreateFont();
	m_hSmallFont = vgui::surface()->CreateFont();
	m_iScreenWidth = 0;
	m_iScreenHeight = 0;
	m_iSpacing = 1;
}

bool CHudServerInfo::ShouldDraw()
{
	if ( mp_timelimit.GetInt() <= 0 && fraglimit.GetFloat() <= 0.0f )
		return false;

	return cl_show_server_info.GetBool() && HL2MPRules() && g_PR && CHudElement::ShouldDraw();
}

void CHudServerInfo::ApplySchemeSettings( vgui::IScheme *pScheme )
{
	BaseClass::ApplySchemeSettings( pScheme );
	SetPaintBackgroundEnabled( false );
	SetPaintBorderEnabled( false );
	m_iScreenWidth = 0;
	m_iScreenHeight = 0;
	OnThink();
}

void CHudServerInfo::OnThink()
{
	int wide, tall;
	GetParent()->GetSize( wide, tall );
	if ( wide <= 0 || tall <= 0 || ( wide == m_iScreenWidth && tall == m_iScreenHeight ) )
		return;

	m_iScreenWidth = wide;
	m_iScreenHeight = tall;
	float flScale = MIN( wide / 640.0f, tall / 480.0f );
	int iFontTall = MAX( 10, RoundFloatToInt( 12.0f * flScale ) );
	int iSmallFontTall = MAX( 9, RoundFloatToInt( 10.0f * flScale ) );
	m_iSpacing = MAX( 1, RoundFloatToInt( 2.0f * flScale ) );

	vgui::surface()->SetFontGlyphSet( m_hFont, "Tahoma", iFontTall, 500, 0, 0, vgui::ISurface::FONTFLAG_ANTIALIAS | vgui::ISurface::FONTFLAG_DROPSHADOW );
	vgui::surface()->SetFontGlyphSet( m_hSmallFont, "Tahoma", iSmallFontTall, 400, 0, 0, vgui::ISurface::FONTFLAG_ANTIALIAS | vgui::ISurface::FONTFLAG_DROPSHADOW );
	SetBounds( 0, m_iSpacing * 4, wide, vgui::surface()->GetFontTall( m_hFont ) + vgui::surface()->GetFontTall( m_hSmallFont ) + m_iSpacing * 2 );
}

void CHudServerInfo::DrawCenteredText( const wchar_t *pText, vgui::HFont hFont, int x, int y, const Color &color )
{
	int wide, tall;
	vgui::surface()->GetTextSize( hFont, pText, wide, tall );
	vgui::surface()->DrawSetTextFont( hFont );
	vgui::surface()->DrawSetTextColor( color );
	vgui::surface()->DrawSetTextPos( x - wide / 2, y );
	vgui::surface()->DrawPrintText( pText, V_wcslen( pText ) );
}

void CHudServerInfo::Paint()
{
	CHL2MPRules *pRules = HL2MPRules();
	C_BasePlayer *pPlayer = C_BasePlayer::GetLocalPlayer();
	if ( !pRules || !pPlayer || !g_PR )
		return;

	wchar_t wszTime[32] = L"--:--";
	float flTimeLeft = pRules->GetMapRemainingTime();
	if ( mp_timelimit.GetInt() > 0 && IsFinite( flTimeLeft ) )
	{
		int iSeconds = (int)ceil( clamp( (double)flTimeLeft, 0.0, (double)INT_MAX ) );
		if ( iSeconds >= 3600 )
			V_snwprintf( wszTime, ARRAYSIZE( wszTime ), L"%02d:%02d:%02d", iSeconds / 3600, ( iSeconds / 60 ) % 60, iSeconds % 60 );
		else
			V_snwprintf( wszTime, ARRAYSIZE( wszTime ), L"%02d:%02d", iSeconds / 60, iSeconds % 60 );
	}

	int iPlayer = pPlayer->entindex();
	int iObserverMode = pPlayer->GetObserverMode();
	if ( iObserverMode == OBS_MODE_IN_EYE || iObserverMode == OBS_MODE_CHASE )
	{
		C_BasePlayer *pTarget = ToBasePlayer( pPlayer->GetObserverTarget() );
		if ( pTarget )
			iPlayer = pTarget->entindex();
	}

	bool bTeamplay = pRules->IsTeamplay();
	bool bHasPlayer = g_PR->IsConnected( iPlayer ) && !g_PR->IsHLTV( iPlayer ) && g_PR->GetTeam( iPlayer ) != TEAM_SPECTATOR;
	wchar_t wszFrags[64] = L"";
	float flFragLimit = fraglimit.GetFloat();
	if ( flFragLimit > 0.0f && IsFinite( flFragLimit ) )
	{
		int iFragLimit = (int)ceil( MIN( (double)flFragLimit, (double)INT_MAX ) );
		C_Team *pTeam = bHasPlayer && bTeamplay ? GetGlobalTeam( g_PR->GetTeam( iPlayer ) ) : NULL;
		if ( bHasPlayer && ( !bTeamplay || ( pTeam && pTeam->GetTeamNumber() >= TEAM_COMBINE ) ) )
		{
			int iScore = bTeamplay ? pTeam->Get_Score() : g_PR->GetPlayerScore( iPlayer );
			V_snwprintf( wszFrags, ARRAYSIZE( wszFrags ), L"%d / %d", iScore, iFragLimit );
		}
		else
		{
			V_snwprintf( wszFrags, ARRAYSIZE( wszFrags ), L"-- / %d", iFragLimit );
		}
	}

	Color color = COLOR_YELLOW;
	C_Team *pCombine = bTeamplay ? GetGlobalTeam( TEAM_COMBINE ) : NULL;
	C_Team *pRebels = bTeamplay ? GetGlobalTeam( TEAM_REBELS ) : NULL;
	if ( pCombine && pRebels )
	{
		if ( pCombine->Get_Score() > pRebels->Get_Score() )
			color = g_PR->GetTeamColor( TEAM_COMBINE );
		else if ( pRebels->Get_Score() > pCombine->Get_Score() )
			color = g_PR->GetTeamColor( TEAM_REBELS );
	}
	color[3] = 230;

	int iCenter = GetWide() / 2;
	int iDetailY = vgui::surface()->GetFontTall( m_hFont ) + m_iSpacing;
	DrawCenteredText( wszTime, m_hFont, iCenter, 0, color );
	DrawCenteredText( wszFrags, m_hSmallFont, iCenter, iDetailY, color );

	if ( bTeamplay )
	{
		wchar_t wszScores[2][32] = { L"--", L"--" };
		wchar_t wszNames[2][32] = { L"Combine", L"Rebels" };
		C_Team *pTeams[2] = { pCombine, pRebels };
		int iSideWide = 0;
		int iCenterWide, iWide, iTall;
		vgui::surface()->GetTextSize( m_hFont, L"00:00", iCenterWide, iTall );
		vgui::surface()->GetTextSize( m_hFont, wszTime, iWide, iTall );
		iCenterWide = MAX( iCenterWide, iWide );
		vgui::surface()->GetTextSize( m_hSmallFont, wszFrags, iWide, iTall );
		iCenterWide = MAX( iCenterWide, iWide );

		for ( int i = 0; i < ARRAYSIZE( pTeams ); ++i )
		{
			if ( pTeams[i] )
			{
				V_snwprintf( wszScores[i], ARRAYSIZE( wszScores[i] ), L"%d", pTeams[i]->Get_Score() );
				g_pVGuiLocalize->ConvertANSIToUnicode( pTeams[i]->Get_Name(), wszNames[i], sizeof( wszNames[i] ) );
			}
			vgui::surface()->GetTextSize( m_hFont, wszScores[i], iWide, iTall );
			iSideWide = MAX( iSideWide, iWide );
			vgui::surface()->GetTextSize( m_hSmallFont, wszNames[i], iWide, iTall );
			iSideWide = MAX( iSideWide, iWide );
		}

		int iOffset = ( iCenterWide + iSideWide ) / 2 + m_iSpacing * 6;
		for ( int i = 0; i < ARRAYSIZE( pTeams ); ++i )
		{
			int x = iCenter + ( i == 0 ? -iOffset : iOffset );
			DrawCenteredText( wszScores[i], m_hFont, x, 0, color );
			DrawCenteredText( wszNames[i], m_hSmallFont, x, iDetailY, color );
		}
	}
}
