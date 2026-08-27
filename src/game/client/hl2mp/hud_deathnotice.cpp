//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Draws CSPort's death notices
//
// $NoKeywords: $
//=============================================================================//
#include "cbase.h"
#include "hudelement.h"
#include "hud_macros.h"
#include "c_playerresource.h"
#include "clientmode_hl2mpnormal.h"
#include <vgui_controls/Controls.h>
#include <vgui_controls/Panel.h>
#include <vgui/ISurface.h>
#include <vgui/ILocalize.h>
#include <KeyValues.h>
#include "c_baseplayer.h"
#include "c_team.h"


// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

static ConVar hud_deathnotice_time( "hud_deathnotice_time", "6", 0 );

// Player entries in a death notice
struct DeathNoticePlayer
{
	char		szName[MAX_PLAYER_NAME_LENGTH];
	int			iTeamNumber;
};

// Contents of each entry in our list of death notices
struct DeathNoticeItem 
{
	DeathNoticePlayer	Killer;
	DeathNoticePlayer   Victim;
	CHudTexture *iconDeath;
	int			iSuicide;
	float		flDisplayTime;
	bool		bLocalPlayerKiller;
	bool		bLocalPlayerVictim;
	bool		bHeadshot;
};

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
class CHudDeathNotice : public CHudElement, public vgui::Panel
{
	DECLARE_CLASS_SIMPLE( CHudDeathNotice, vgui::Panel );
public:
	CHudDeathNotice( const char *pElementName );

	void Init( void );
	void VidInit( void );
	virtual bool ShouldDraw( void );
	virtual void Paint( void );
	virtual void ApplySchemeSettings( vgui::IScheme *scheme );

	void SetColorForNoticePlayer( int iTeamNumber, float flAlpha );
	void RetireExpiredDeathNotices( void );
	void GetIconSize( CHudTexture *icon, int iMaxTall, int &iWide, int &iTall );
	void DrawIcon( CHudTexture *icon, int x, int y, int iWide, int iTall, const Color &color );
	void GetBackgroundPolygonVerts( int x0, int y0, int x1, int y1, vgui::Vertex_t vert[] );
	void CalcRoundedCorners( void );
	Color GetFadedColor( const Color &color, float flAlpha );

	virtual void FireGameEvent( IGameEvent * event );

private:

	enum
	{
		NUM_CORNER_COORD = 10,
		NUM_BACKGROUND_COORD = NUM_CORNER_COORD * 4,
	};

	CPanelAnimationVarAliasType( float, m_flLineHeight, "LineHeight", "16", "proportional_float" );

	CPanelAnimationVarAliasType( float, m_flLineSpacing, "LineSpacing", "3", "proportional_float" );

	CPanelAnimationVarAliasType( float, m_flCornerRadius, "CornerRadius", "3", "proportional_float" );

	CPanelAnimationVar( float, m_flMaxDeathNotices, "MaxDeathNotices", "4" );

	CPanelAnimationVar( bool, m_bRightJustify, "RightJustify", "1" );

	CPanelAnimationVar( vgui::HFont, m_hTextFont, "TextFont", "Default" );

	CPanelAnimationVar( Color, m_clrIcon, "IconColor", "255 255 255 255" );

	CPanelAnimationVar( Color, m_clrBackground, "BackgroundColor", "24 24 24 220" );

	CPanelAnimationVar( Color, m_clrLocalPlayerDeathBackground, "LocalPlayerDeathBackgroundColor", "72 16 16 220" );

	CPanelAnimationVar( Color, m_clrLocalPlayerOutline, "LocalPlayerOutlineColor", "255 0 0 255" );

	CPanelAnimationVar( float, m_flFadeOutTime, "FadeOutTime", "1.0" );

	// Texture for skull symbol
	CHudTexture		*m_iconD_skull;  
	CHudTexture		*m_iconD_headshot;  

	CUtlVector<DeathNoticeItem> m_DeathNotices;
	Vector2D m_CornerCoord[NUM_CORNER_COORD];
};

using namespace vgui;

DECLARE_HUDELEMENT( CHudDeathNotice );

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
CHudDeathNotice::CHudDeathNotice( const char *pElementName ) :
	CHudElement( pElementName ), BaseClass( NULL, "HudDeathNotice" )
{
	vgui::Panel *pParent = g_pClientMode->GetViewport();
	SetParent( pParent );

	m_iconD_headshot = NULL;
	m_iconD_skull = NULL;

	SetHiddenBits( HIDEHUD_MISCSTATUS );

	SetSize( ScreenWidth(), ScreenHeight() );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CHudDeathNotice::ApplySchemeSettings( IScheme *scheme )
{
	BaseClass::ApplySchemeSettings( scheme );
	SetPaintBackgroundEnabled( false );

	SetSize( ScreenWidth(), ScreenHeight() );
	CalcRoundedCorners();
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CHudDeathNotice::Init( void )
{
	ListenForGameEvent( "player_death" );	
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CHudDeathNotice::VidInit( void )
{
	m_iconD_skull = gHUD.GetIcon( "d_skull" );
	m_iconD_headshot = m_iconD_skull;
	m_DeathNotices.Purge();
}

//-----------------------------------------------------------------------------
// Purpose: Draw if we've got at least one death notice in the queue
//-----------------------------------------------------------------------------
bool CHudDeathNotice::ShouldDraw( void )
{
	return ( CHudElement::ShouldDraw() && ( m_DeathNotices.Count() ) );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CHudDeathNotice::SetColorForNoticePlayer( int iTeamNumber, float flAlpha )
{
	surface()->DrawSetTextColor( GetFadedColor( GameResources()->GetTeamColor( iTeamNumber ), flAlpha ) );
}

void CHudDeathNotice::GetIconSize( CHudTexture *icon, int iMaxTall, int &iWide, int &iTall )
{
	iWide = 0;
	iTall = 0;

	if ( !icon )
		return;

	if ( icon->bRenderUsingFont )
	{
		float flWide = surface()->GetCharacterWidth( icon->hFont, icon->cCharacterInFont );
		float flTall = surface()->GetFontTall( icon->hFont );
		if ( flWide <= 0.0f || flTall <= 0.0f )
			return;

		float flScale = MIN( 1.0f, (float)iMaxTall / flTall );
		iWide = MAX( 1, (int)( flWide * flScale ) );
		iTall = MAX( 1, (int)( flTall * flScale ) );
		return;
	}

	float flWide = icon->EffectiveWidth( 1.0f );
	float flTall = icon->EffectiveHeight( 1.0f );
	if ( flWide <= 0.0f || flTall <= 0.0f )
		return;

	float flScale = (float)iMaxTall / flTall;
	iWide = MAX( 1, (int)( flWide * flScale ) );
	iTall = MAX( 1, (int)( flTall * flScale ) );
}

void CHudDeathNotice::DrawIcon( CHudTexture *icon, int x, int y, int iWide, int iTall, const Color &color )
{
	if ( !icon )
		return;

	if ( !icon->bRenderUsingFont )
	{
		icon->DrawSelf( x, y, iWide, iTall, color );
		return;
	}

	int iFontTall = surface()->GetFontTall( icon->hFont );
	if ( iFontTall <= 0 )
		return;

	surface()->DrawSetTextFont( icon->hFont );
	surface()->DrawSetTextColor( color );
	surface()->DrawSetTextPos( x, y );

	CharRenderInfo info;
	if ( surface()->DrawGetUnicodeCharRenderInfo( icon->cCharacterInFont, info ) )
	{
		float flScale = (float)iTall / iFontTall;
		for ( int i = 0; i < 2; i++ )
		{
			info.verts[i].m_Position.x = x + ( info.verts[i].m_Position.x - x ) * flScale;
			info.verts[i].m_Position.y = y + ( info.verts[i].m_Position.y - y ) * flScale;
		}
		surface()->DrawRenderCharFromInfo( info );
	}
}

void CHudDeathNotice::GetBackgroundPolygonVerts( int x0, int y0, int x1, int y1, Vertex_t vert[] )
{
	for ( int i = 0; i < NUM_CORNER_COORD; i++ )
	{
		int j = NUM_CORNER_COORD - 1 - i;
		vert[i].Init( Vector2D( x0 + m_CornerCoord[i].x, y0 + m_CornerCoord[i].y ) );
		vert[i + NUM_CORNER_COORD].Init( Vector2D( x1 - m_CornerCoord[j].x, y0 + m_CornerCoord[j].y ) );
		vert[i + NUM_CORNER_COORD * 2].Init( Vector2D( x1 - m_CornerCoord[i].x, y1 - m_CornerCoord[i].y ) );
		vert[i + NUM_CORNER_COORD * 3].Init( Vector2D( x0 + m_CornerCoord[j].x, y1 - m_CornerCoord[j].y ) );
	}
}

void CHudDeathNotice::CalcRoundedCorners( void )
{
	for ( int i = 0; i < NUM_CORNER_COORD; i++ )
	{
		m_CornerCoord[i].x = m_flCornerRadius * ( 1.0f - cos( ( (float)i / ( NUM_CORNER_COORD - 1 ) ) * ( M_PI / 2.0f ) ) );
		m_CornerCoord[i].y = m_flCornerRadius * ( 1.0f - sin( ( (float)i / ( NUM_CORNER_COORD - 1 ) ) * ( M_PI / 2.0f ) ) );
	}
}

Color CHudDeathNotice::GetFadedColor( const Color &color, float flAlpha )
{
	return Color( color.r(), color.g(), color.b(), (int)( color.a() * flAlpha ) );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CHudDeathNotice::Paint()
{
	if ( !m_iconD_skull )
		return;

	RetireExpiredDeathNotices();

	int yStart = GetClientModeHL2MPNormal()->GetDeathMessageStartHeight();

	surface()->DrawSetTextFont( m_hTextFont );
	int iLineTall = MAX( 1, (int)m_flLineHeight );
	int iLineSpacing = MAX( 0, (int)m_flLineSpacing );
	int iTextTall = surface()->GetFontTall( m_hTextFont );
	int iHorizontalPadding = scheme()->GetProportionalScaledValue( 6 );
	int iItemSpacing = scheme()->GetProportionalScaledValue( 3 );
	int iScreenPadding = scheme()->GetProportionalScaledValue( 16 );
	int iIconMaxTall = MAX( 1, iLineTall - scheme()->GetProportionalScaledValue( 2 ) );
	int iOutlineWide = MAX( 1, scheme()->GetProportionalScaledValue( 1 ) );

	int iCount = m_DeathNotices.Count();
	for ( int i = 0; i < iCount; i++ )
	{
		DeathNoticeItem &notice = m_DeathNotices[i];
		CHudTexture *icon = notice.iconDeath;
		if ( !icon )
			continue;

		float flAlpha = 1.0f;
		if ( m_flFadeOutTime > 0.0f )
		{
			flAlpha = clamp( ( notice.flDisplayTime - gpGlobals->curtime ) / m_flFadeOutTime, 0.0f, 1.0f );
		}

		wchar_t victim[ 256 ];
		wchar_t killer[ 256 ];

		g_pVGuiLocalize->ConvertANSIToUnicode( notice.Victim.szName, victim, sizeof( victim ) );
		g_pVGuiLocalize->ConvertANSIToUnicode( notice.Killer.szName, killer, sizeof( killer ) );

		int iVictimWide = UTIL_ComputeStringWidth( m_hTextFont, victim );
		int iKillerWide = notice.iSuicide ? 0 : UTIL_ComputeStringWidth( m_hTextFont, killer );
		int iIconWide;
		int iIconTall;
		GetIconSize( icon, iIconMaxTall, iIconWide, iIconTall );

		bool bDrawHeadshot = notice.bHeadshot && m_iconD_headshot && icon != m_iconD_skull;
		int iHeadshotWide = 0;
		int iHeadshotTall = 0;
		if ( bDrawHeadshot )
		{
			GetIconSize( m_iconD_headshot, iIconMaxTall, iHeadshotWide, iHeadshotTall );
		}

		int iContentWide = iVictimWide + iIconWide + iItemSpacing;
		if ( !notice.iSuicide )
		{
			iContentWide += iKillerWide + iItemSpacing;
		}
		if ( bDrawHeadshot )
		{
			iContentWide += iHeadshotWide + iItemSpacing;
		}

		int iTotalWide = iContentWide + iHorizontalPadding * 2;
		int y = yStart + ( iLineTall + iLineSpacing ) * i;
		int yText = y + ( iLineTall - iTextTall ) / 2;
		int x = m_bRightJustify ? GetWide() - iTotalWide - iScreenPadding : iScreenPadding;

		Vertex_t background[NUM_BACKGROUND_COORD];
		GetBackgroundPolygonVerts( x, y, x + iTotalWide, y + iLineTall, background );
		surface()->DrawSetTexture( -1 );
		Color backgroundColor = notice.bLocalPlayerVictim ? m_clrLocalPlayerDeathBackground : m_clrBackground;
		surface()->DrawSetColor( GetFadedColor( backgroundColor, flAlpha ) );
		surface()->DrawTexturedPolygon( NUM_BACKGROUND_COORD, background );

		if ( notice.bLocalPlayerKiller || notice.bLocalPlayerVictim )
		{
			int outlineX[NUM_BACKGROUND_COORD + 1];
			int outlineY[NUM_BACKGROUND_COORD + 1];
			surface()->DrawSetColor( GetFadedColor( m_clrLocalPlayerOutline, flAlpha ) );
			for ( int j = 0; j < iOutlineWide; j++ )
			{
				GetBackgroundPolygonVerts( x + j, y + j, x + iTotalWide - j, y + iLineTall - j, background );
				for ( int k = 0; k < NUM_BACKGROUND_COORD; k++ )
				{
					outlineX[k] = (int)background[k].m_Position.x;
					outlineY[k] = (int)background[k].m_Position.y;
				}
				outlineX[NUM_BACKGROUND_COORD] = outlineX[0];
				outlineY[NUM_BACKGROUND_COORD] = outlineY[0];
				surface()->DrawPolyLine( outlineX, outlineY, NUM_BACKGROUND_COORD + 1 );
			}
		}

		x += iHorizontalPadding;

		if ( !notice.iSuicide )
		{
			SetColorForNoticePlayer( notice.Killer.iTeamNumber, flAlpha );
			surface()->DrawSetTextPos( x, yText );
			surface()->DrawSetTextFont( m_hTextFont );
			surface()->DrawUnicodeString( killer, FONT_DRAW_NONADDITIVE );
			x += iKillerWide + iItemSpacing;
		}

		Color iconColor = GetFadedColor( m_clrIcon, flAlpha );
		int yIcon = y + ( iLineTall - iIconTall ) / 2;
		DrawIcon( icon, x, yIcon, iIconWide, iIconTall, iconColor );
		x += iIconWide;

		if ( bDrawHeadshot )
		{
			x += iItemSpacing;
			int yHeadshot = y + ( iLineTall - iHeadshotTall ) / 2;
			DrawIcon( m_iconD_headshot, x, yHeadshot, iHeadshotWide, iHeadshotTall, iconColor );
			x += iHeadshotWide;
		}

		x += iItemSpacing;

		SetColorForNoticePlayer( notice.Victim.iTeamNumber, flAlpha );
		surface()->DrawSetTextPos( x, yText );
		surface()->DrawSetTextFont( m_hTextFont );
		surface()->DrawUnicodeString( victim, FONT_DRAW_NONADDITIVE );
	}
}

//-----------------------------------------------------------------------------
// Purpose: This message handler may be better off elsewhere
//-----------------------------------------------------------------------------
void CHudDeathNotice::RetireExpiredDeathNotices( void )
{
	// Loop backwards because we might remove one
	int iSize = m_DeathNotices.Size();
	for ( int i = iSize-1; i >= 0; i-- )
	{
		if ( m_DeathNotices[i].flDisplayTime < gpGlobals->curtime )
		{
			m_DeathNotices.Remove(i);
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: Server's told us that someone's died
//-----------------------------------------------------------------------------
void CHudDeathNotice::FireGameEvent( IGameEvent * event )
{
	if (!g_PR)
		return;

	if ( hud_deathnotice_time.GetFloat() == 0 )
		return;

	// the event should be "player_death"
	int killer = engine->GetPlayerForUserID( event->GetInt("attacker") );
	int victim = engine->GetPlayerForUserID( event->GetInt("userid") );
	const char *killedwith = event->GetString( "weapon" );

	char fullkilledwith[128];
	if ( killedwith && *killedwith )
	{
		Q_snprintf( fullkilledwith, sizeof(fullkilledwith), "death_%s", killedwith );
	}
	else
	{
		fullkilledwith[0] = 0;
	}

	// Do we have too many death messages in the queue?
	if ( m_DeathNotices.Count() > 0 &&
		m_DeathNotices.Count() >= (int)m_flMaxDeathNotices )
	{
		// Remove the oldest one in the queue, which will always be the first
		m_DeathNotices.Remove(0);
	}

	// Get the names of the players
	const char *killer_name = g_PR->GetPlayerName( killer );
	const char *victim_name = g_PR->GetPlayerName( victim );

	if ( !killer_name )
		killer_name = "";
	if ( !victim_name )
		victim_name = "";

	// Make a new death notice
	DeathNoticeItem deathMsg;
	deathMsg.Killer.iTeamNumber = killer > 0 ? g_PR->GetTeam( killer ) : 0;
	deathMsg.Victim.iTeamNumber = victim > 0 ? g_PR->GetTeam( victim ) : 0;
	Q_strncpy( deathMsg.Killer.szName, killer_name, MAX_PLAYER_NAME_LENGTH );
	Q_strncpy( deathMsg.Victim.szName, victim_name, MAX_PLAYER_NAME_LENGTH );
	deathMsg.flDisplayTime = gpGlobals->curtime + hud_deathnotice_time.GetFloat();
	deathMsg.iSuicide = ( !killer || killer == victim );
	int iLocalPlayerIndex = GetLocalPlayerIndex();
	deathMsg.bLocalPlayerKiller = iLocalPlayerIndex > 0 && !deathMsg.iSuicide && killer == iLocalPlayerIndex;
	deathMsg.bLocalPlayerVictim = iLocalPlayerIndex > 0 && victim == iLocalPlayerIndex;
	deathMsg.bHeadshot = !deathMsg.iSuicide && event->GetBool( "headshot" );

	// Try and find the death identifier in the icon list
	deathMsg.iconDeath = gHUD.GetIcon( fullkilledwith );

	if ( !deathMsg.iconDeath || deathMsg.iSuicide )
	{
		// Can't find it, so use the default skull & crossbones icon
		deathMsg.iconDeath = m_iconD_skull;
	}

	// Add it to our list of death notices
	m_DeathNotices.AddToTail( deathMsg );

	char sDeathMsg[512];

	// Record the death notice in the console
	if ( deathMsg.iSuicide )
	{
		if ( !strcmp( fullkilledwith, "d_worldspawn" ) )
		{
			Q_snprintf( sDeathMsg, sizeof( sDeathMsg ), "%s died.\n", deathMsg.Victim.szName );
		}
		else	//d_world
		{
			Q_snprintf( sDeathMsg, sizeof( sDeathMsg ), "%s suicided.\n", deathMsg.Victim.szName );
		}
	}
	else
	{
		Q_snprintf( sDeathMsg, sizeof( sDeathMsg ), "%s killed %s", deathMsg.Killer.szName, deathMsg.Victim.szName );

		if ( fullkilledwith && *fullkilledwith && (*fullkilledwith > 13 ) )
		{
			Q_strncat( sDeathMsg, VarArgs( " with %s.\n", fullkilledwith+6 ), sizeof( sDeathMsg ), COPY_ALL_CHARACTERS );
		}
	}

	Msg( "%s", sDeathMsg );
}
