//========= Copyright Valve Corporation, All rights reserved. ============//

#include "cbase.h"
#include "hl2mpclientscoreboard.h"
#include "c_team.h"
#include "c_playerresource.h"
#include "hl2mp_gamerules.h"

#include <KeyValues.h>

#include <vgui/IScheme.h>
#include <vgui/ILocalize.h>
#include <vgui/ISurface.h>
#include <vgui_controls/Label.h>
#include <vgui_controls/SectionedListPanel.h>

using namespace vgui;

enum EScoreboardSections
{
	SCORESECTION_HEADER = 0,
	SCORESECTION_COMBINE,
	SCORESECTION_REBELS,
	SCORESECTION_FREEFORALL,
	SCORESECTION_SPECTATOR
};

CHL2MPClientScoreBoardDialog::CHL2MPClientScoreBoardDialog( IViewPort *pViewPort ) :
	CClientScoreBoardDialog( pViewPort ),
	m_pServerName( NULL ),
	m_pMatchInfo( NULL ),
	m_pTimeLeft( NULL ),
	m_backgroundColor( 18, 19, 18, 238 ),
	m_borderColor( 92, 88, 78, 190 ),
	m_accentColor( 232, 112, 32, 255 ),
	m_playerTextColor( 232, 232, 226, 255 ),
	m_secondaryTextColor( 174, 176, 170, 255 ),
	m_spectatorTextColor( 148, 151, 146, 255 ),
	m_localPlayerBackgroundColor( 232, 112, 32, 48 )
{
	SetProportional( true );
	m_bAllowGrowth = false;

	m_pServerName = dynamic_cast<Label *>( FindChildByName( "ServerName" ) );
	m_pMatchInfo = new Label( this, "MatchInfo", "" );
	m_pMatchInfo->SetContentAlignment( Label::a_west );
	m_pTimeLeft = new Label( this, "TimeLeft", "" );
	m_pTimeLeft->SetContentAlignment( Label::a_east );
}

CHL2MPClientScoreBoardDialog::~CHL2MPClientScoreBoardDialog()
{
}

void CHL2MPClientScoreBoardDialog::Update()
{
	BaseClass::Update();
	UpdateMatchInfo();
	InvalidateLayout();
}

void CHL2MPClientScoreBoardDialog::PaintBackground()
{
	int wide, tall;
	GetSize( wide, tall );

	surface()->DrawSetColor( m_backgroundColor );
	surface()->DrawFilledRect( 0, 0, wide, tall );

	surface()->DrawSetColor( m_accentColor );
	surface()->DrawFilledRect( 0, 0, wide, 2 );
}

void CHL2MPClientScoreBoardDialog::PaintBorder()
{
	int wide, tall;
	GetSize( wide, tall );

	surface()->DrawSetColor( m_borderColor );
	surface()->DrawOutlinedRect( 0, 0, wide, tall );
}

void CHL2MPClientScoreBoardDialog::ApplySchemeSettings( IScheme *pScheme )
{
	BaseClass::ApplySchemeSettings( pScheme );

	SetBgColor( Color( 0, 0, 0, 0 ) );
	SetBorder( NULL );

	m_pPlayerList->SetProportional( true );
	m_pPlayerList->SetBgColor( Color( 0, 0, 0, 0 ) );
	m_pPlayerList->SetBorder( NULL );

	HFont headerFont = pScheme->GetFont( "DefaultSmall", true );
	if ( headerFont == INVALID_FONT )
	{
		headerFont = pScheme->GetFont( "Default", true );
	}

	HFont rowFont = pScheme->GetFont( "Default", true );
	HFont titleFont = pScheme->GetFont( "DefaultLarge", true );
	if ( titleFont == INVALID_FONT )
	{
		titleFont = rowFont;
	}

	m_pPlayerList->SetHeaderFont( headerFont );
	m_pPlayerList->SetRowFont( rowFont );

	if ( !m_pServerName )
	{
		m_pServerName = dynamic_cast<Label *>( FindChildByName( "ServerName" ) );
	}

	if ( m_pServerName )
	{
		m_pServerName->SetFont( titleFont );
		m_pServerName->SetFgColor( m_playerTextColor );
		m_pServerName->SetPaintBackgroundEnabled( false );
		m_pServerName->SetContentAlignment( Label::a_west );
	}

	if ( m_pMatchInfo )
	{
		m_pMatchInfo->SetFont( headerFont );
		m_pMatchInfo->SetFgColor( m_secondaryTextColor );
		m_pMatchInfo->SetPaintBackgroundEnabled( false );
		m_pMatchInfo->MoveToFront();
	}

	if ( m_pTimeLeft )
	{
		m_pTimeLeft->SetFont( headerFont );
		m_pTimeLeft->SetFgColor( m_playerTextColor );
		m_pTimeLeft->SetPaintBackgroundEnabled( false );
		m_pTimeLeft->MoveToFront();
	}
}

void CHL2MPClientScoreBoardDialog::PerformLayout()
{
	BaseClass::PerformLayout();

	int workspaceX, workspaceY, workspaceWide, workspaceTall;
	surface()->GetWorkspaceBounds( workspaceX, workspaceY, workspaceWide, workspaceTall );

	const int targetWide = (int)( workspaceWide * 0.66f );
	const int minimumWide = MIN( (int)( workspaceWide * 0.90f ), scheme()->GetProportionalScaledValueEx( GetScheme(), 460 ) );
	const int maximumWide = MIN( (int)( workspaceWide * 0.70f ), (int)( workspaceTall * 1.50f ) );
	const int panelWide = MIN( MAX( targetWide, minimumWide ), maximumWide );

	const int horizontalPadding = scheme()->GetProportionalScaledValueEx( GetScheme(), 12 );
	const int topPadding = scheme()->GetProportionalScaledValueEx( GetScheme(), 12 );
	const int titleTall = scheme()->GetProportionalScaledValueEx( GetScheme(), 20 );
	const int matchInfoTall = scheme()->GetProportionalScaledValueEx( GetScheme(), 14 );
	const int titleGap = scheme()->GetProportionalScaledValueEx( GetScheme(), 2 );
	const int listGap = scheme()->GetProportionalScaledValueEx( GetScheme(), 8 );
	const int matchInfoGap = scheme()->GetProportionalScaledValueEx( GetScheme(), 8 );
	const int timeLeftWide = scheme()->GetProportionalScaledValueEx( GetScheme(), 52 );
	const int bottomPadding = scheme()->GetProportionalScaledValueEx( GetScheme(), 10 );
	const int listTop = topPadding + titleTall + titleGap + matchInfoTall + listGap;
	const int listWide = panelWide - horizontalPadding * 2;
	const int maximumPanelTall = (int)( workspaceTall * 0.88f );
	const int maximumListTall = MAX( scheme()->GetProportionalScaledValueEx( GetScheme(), 80 ), maximumPanelTall - listTop - bottomPadding );

	if ( m_pServerName )
	{
		m_pServerName->SetBounds( horizontalPadding, topPadding, listWide, titleTall );
		m_pServerName->MoveToFront();
	}

	if ( m_pMatchInfo )
	{
		m_pMatchInfo->SetBounds( horizontalPadding, topPadding + titleTall + titleGap, MAX( 0, listWide - timeLeftWide - matchInfoGap ), matchInfoTall );
	}

	if ( m_pTimeLeft )
	{
		m_pTimeLeft->SetBounds( horizontalPadding + listWide - timeLeftWide, topPadding + titleTall + titleGap, timeLeftWide, matchInfoTall );
	}

	m_pPlayerList->SetBounds( horizontalPadding, listTop, listWide, maximumListTall );
	UpdateColumnWidths( listWide );

	int contentWide, contentTall;
	m_pPlayerList->GetContentSize( contentWide, contentTall );

	const int minimumListTall = scheme()->GetProportionalScaledValueEx( GetScheme(), 80 );
	const int listTall = MIN( MAX( contentTall, minimumListTall ), maximumListTall );
	const int panelTall = listTop + listTall + bottomPadding;

	SetSize( panelWide, panelTall );
	m_pPlayerList->SetBounds( horizontalPadding, listTop, listWide, listTall );
	SetPos( workspaceX + ( workspaceWide - panelWide ) / 2, workspaceY + ( workspaceTall - panelTall ) / 2 );
}

void CHL2MPClientScoreBoardDialog::InitScoreboardSections()
{
	m_pPlayerList->SetBgColor( Color( 0, 0, 0, 0 ) );
	m_pPlayerList->SetBorder( NULL );
	m_pPlayerList->ClearSelection();

	AddHeader();

	if ( HL2MPRules()->IsTeamplay() )
	{
		AddSection( TYPE_TEAM, TEAM_COMBINE );
		AddSection( TYPE_TEAM, TEAM_REBELS );
	}
	else
	{
		AddSection( TYPE_TEAM, TEAM_UNASSIGNED );
	}

	AddSection( TYPE_SPECTATORS, TEAM_SPECTATOR );
}

void CHL2MPClientScoreBoardDialog::SetSectionHeader( int teamNumber, const wchar_t *teamName, int playerCount, int score, bool showScore )
{
	const int sectionID = GetSectionFromTeamNumber( teamNumber );
	wchar_t sectionText[256];
	V_snwprintf( sectionText, ARRAYSIZE( sectionText ), L"%ls  \x2022  %d", teamName, playerCount );

	m_pPlayerList->ModifyColumn( sectionID, "name", sectionText );
	if ( teamNumber == TEAM_SPECTATOR )
	{
		return;
	}

	m_pPlayerList->ModifyColumn( sectionID, "deaths", L"" );
	m_pPlayerList->ModifyColumn( sectionID, "ping", L"" );

	if ( showScore )
	{
		wchar_t scoreText[16];
		V_snwprintf( scoreText, ARRAYSIZE( scoreText ), L"%d", score );
		m_pPlayerList->ModifyColumn( sectionID, "frags", scoreText );
	}
	else
	{
		m_pPlayerList->ModifyColumn( sectionID, "frags", L"" );
	}
}

void CHL2MPClientScoreBoardDialog::UpdateTeamInfo()
{
	if ( !g_PR )
	{
		return;
	}

	int activePlayers = 0;
	int spectatorPlayers = 0;

	for ( int playerIndex = 1; playerIndex <= gpGlobals->maxClients; ++playerIndex )
	{
		if ( !g_PR->IsConnected( playerIndex ) )
		{
			continue;
		}

		if ( g_PR->GetTeam( playerIndex ) == TEAM_SPECTATOR )
		{
			++spectatorPlayers;
		}
		else
		{
			++activePlayers;
		}
	}

	if ( HL2MPRules()->IsTeamplay() )
	{
		for ( int teamNumber = TEAM_COMBINE; teamNumber <= TEAM_REBELS; ++teamNumber )
		{
			C_Team *team = GetGlobalTeam( teamNumber );
			if ( !team )
			{
				continue;
			}

			wchar_t teamName[64];
			g_pVGuiLocalize->ConvertANSIToUnicode( team->Get_Name(), teamName, sizeof( teamName ) );
			SetSectionHeader( teamNumber, teamName, team->Get_Number_Players(), team->Get_Score(), true );
		}
	}
	else
	{
		const wchar_t *deathmatchName = g_pVGuiLocalize->Find( "#ScoreBoard_Deathmatch" );
		wchar_t fallbackName[64];
		if ( !deathmatchName )
		{
			g_pVGuiLocalize->ConvertANSIToUnicode( "Deathmatch", fallbackName, sizeof( fallbackName ) );
			deathmatchName = fallbackName;
		}
		SetSectionHeader( TEAM_UNASSIGNED, deathmatchName, activePlayers, 0, false );
	}

	C_Team *spectatorTeam = GetGlobalTeam( TEAM_SPECTATOR );
	wchar_t spectatorName[64];
	if ( spectatorTeam )
	{
		g_pVGuiLocalize->ConvertANSIToUnicode( spectatorTeam->Get_Name(), spectatorName, sizeof( spectatorName ) );
	}
	else
	{
		g_pVGuiLocalize->ConvertANSIToUnicode( "Spectators", spectatorName, sizeof( spectatorName ) );
	}
	SetSectionHeader( TEAM_SPECTATOR, spectatorName, spectatorPlayers, 0, false );
}

void CHL2MPClientScoreBoardDialog::AddHeader()
{
	m_pPlayerList->AddSection( SCORESECTION_HEADER, "" );
	m_pPlayerList->SetSectionAlwaysVisible( SCORESECTION_HEADER );
	m_pPlayerList->SetSectionFgColor( SCORESECTION_HEADER, m_secondaryTextColor );
	m_pPlayerList->SetSectionDrawDividerBar( SCORESECTION_HEADER, false );

	if ( ShowAvatars() )
	{
		m_pPlayerList->AddColumnToSection( SCORESECTION_HEADER, "avatar", "", 0, m_iAvatarWidth * 2 );
	}
	m_pPlayerList->AddColumnToSection( SCORESECTION_HEADER, "name", "#PlayerName", SectionedListPanel::COLUMN_BRIGHT, scheme()->GetProportionalScaledValueEx( GetScheme(), SCOREBOARD_NAME_WIDTH ) );
	m_pPlayerList->AddColumnToSection( SCORESECTION_HEADER, "frags", "#PlayerScore", SectionedListPanel::COLUMN_BRIGHT | SectionedListPanel::COLUMN_RIGHT, scheme()->GetProportionalScaledValueEx( GetScheme(), SCOREBOARD_SCORE_WIDTH ) );
	m_pPlayerList->AddColumnToSection( SCORESECTION_HEADER, "deaths", "#PlayerDeath", SectionedListPanel::COLUMN_BRIGHT | SectionedListPanel::COLUMN_RIGHT, scheme()->GetProportionalScaledValueEx( GetScheme(), SCOREBOARD_DEATH_WIDTH ) );
	m_pPlayerList->AddColumnToSection( SCORESECTION_HEADER, "ping", "#PlayerPing", SectionedListPanel::COLUMN_BRIGHT | SectionedListPanel::COLUMN_RIGHT, scheme()->GetProportionalScaledValueEx( GetScheme(), SCOREBOARD_PING_WIDTH ) );
}

void CHL2MPClientScoreBoardDialog::AddSection( int teamType, int teamNumber )
{
	const int sectionID = GetSectionFromTeamNumber( teamNumber );
	m_pPlayerList->AddSection( sectionID, "", StaticPlayerSortFunc );
	m_pPlayerList->SetSectionDividerColor( sectionID, Color( 255, 255, 255, 30 ) );
	m_pPlayerList->SetSectionDrawDividerBar( sectionID, true );

	if ( teamType == TYPE_SPECTATORS )
	{
		m_pPlayerList->AddColumnToSection( sectionID, "name", "", 0, scheme()->GetProportionalScaledValueEx( GetScheme(), SCOREBOARD_NAME_WIDTH ) );
		m_pPlayerList->SetSectionFgColor( sectionID, m_spectatorTextColor );
		m_pPlayerList->SetSectionAlwaysVisible( sectionID, false );
		return;
	}

	if ( ShowAvatars() )
	{
		m_pPlayerList->AddColumnToSection( sectionID, "avatar", "", SectionedListPanel::COLUMN_IMAGE, m_iAvatarWidth * 2 );
	}

	m_pPlayerList->AddColumnToSection( sectionID, "name", "", 0, scheme()->GetProportionalScaledValueEx( GetScheme(), SCOREBOARD_NAME_WIDTH ) );
	m_pPlayerList->AddColumnToSection( sectionID, "frags", "", SectionedListPanel::COLUMN_RIGHT, scheme()->GetProportionalScaledValueEx( GetScheme(), SCOREBOARD_SCORE_WIDTH ) );
	m_pPlayerList->AddColumnToSection( sectionID, "deaths", "", SectionedListPanel::COLUMN_RIGHT, scheme()->GetProportionalScaledValueEx( GetScheme(), SCOREBOARD_DEATH_WIDTH ) );
	m_pPlayerList->AddColumnToSection( sectionID, "ping", "", SectionedListPanel::COLUMN_RIGHT, scheme()->GetProportionalScaledValueEx( GetScheme(), SCOREBOARD_PING_WIDTH ) );

	if ( teamNumber == TEAM_COMBINE || teamNumber == TEAM_REBELS )
	{
		IGameResources *resources = GameResources();
		m_pPlayerList->SetSectionFgColor( sectionID, resources ? resources->GetTeamColor( teamNumber ) : m_accentColor );
	}
	else
	{
		m_pPlayerList->SetSectionFgColor( sectionID, m_accentColor );
	}

	m_pPlayerList->SetSectionAlwaysVisible( sectionID, false );
}

int CHL2MPClientScoreBoardDialog::GetSectionFromTeamNumber( int teamNumber )
{
	switch ( teamNumber )
	{
	case TEAM_COMBINE:
		return SCORESECTION_COMBINE;
	case TEAM_REBELS:
		return SCORESECTION_REBELS;
	case TEAM_SPECTATOR:
		return SCORESECTION_SPECTATOR;
	default:
		return SCORESECTION_FREEFORALL;
	}
}

bool CHL2MPClientScoreBoardDialog::GetPlayerScoreInfo( int playerIndex, KeyValues *kv )
{
	kv->SetInt( "playerIndex", playerIndex );
	kv->SetInt( "team", g_PR->GetTeam( playerIndex ) );
	kv->SetString( "name", g_PR->GetPlayerName( playerIndex ) );
	kv->SetInt( "deaths", g_PR->GetDeaths( playerIndex ) );
	kv->SetInt( "frags", g_PR->GetPlayerScore( playerIndex ) );

	if ( g_PR->GetPing( playerIndex ) < 1 )
	{
		kv->SetString( "ping", g_PR->IsFakePlayer( playerIndex ) ? "BOT" : "" );
	}
	else
	{
		kv->SetInt( "ping", g_PR->GetPing( playerIndex ) );
	}

	return true;
}

void CHL2MPClientScoreBoardDialog::UpdatePlayerInfo()
{
	CBasePlayer *localPlayer = C_BasePlayer::GetLocalPlayer();
	if ( !localPlayer || !g_PR )
	{
		return;
	}

	m_pPlayerList->ClearSelection();

	for ( int playerIndex = 1; playerIndex <= gpGlobals->maxClients; ++playerIndex )
	{
		if ( !g_PR->IsConnected( playerIndex ) )
		{
			const int itemID = FindItemIDForPlayerIndex( playerIndex );
			if ( itemID != -1 )
			{
				m_pPlayerList->RemoveItem( itemID );
			}
			continue;
		}

		KeyValues *playerData = new KeyValues( "data" );
		GetPlayerScoreInfo( playerIndex, playerData );

		const bool spectator = g_PR->GetTeam( playerIndex ) == TEAM_SPECTATOR;
		if ( !spectator )
		{
			UpdatePlayerAvatar( playerIndex, playerData );
		}

		int itemID = FindItemIDForPlayerIndex( playerIndex );
		const int sectionID = GetSectionFromTeamNumber( g_PR->GetTeam( playerIndex ) );

		if ( itemID == -1 )
		{
			itemID = m_pPlayerList->AddItem( sectionID, playerData );
		}
		else
		{
			m_pPlayerList->ModifyItem( itemID, sectionID, playerData );
		}

		m_pPlayerList->SetItemFgColor( itemID, spectator ? m_spectatorTextColor : m_playerTextColor );
		m_pPlayerList->SetItemBgColor( itemID, playerIndex == localPlayer->entindex() ? m_localPlayerBackgroundColor : Color( 0, 0, 0, 0 ) );
		playerData->deleteThis();
	}
}

int CHL2MPClientScoreBoardDialog::GetConnectedPlayerCount() const
{
	if ( !g_PR )
	{
		return 0;
	}

	int playerCount = 0;
	for ( int playerIndex = 1; playerIndex <= gpGlobals->maxClients; ++playerIndex )
	{
		if ( g_PR->IsConnected( playerIndex ) )
		{
			++playerCount;
		}
	}
	return playerCount;
}

void CHL2MPClientScoreBoardDialog::UpdateMatchInfo()
{
	if ( !m_pMatchInfo || !m_pTimeLeft )
	{
		return;
	}

	wchar_t mapName[128];
	const char *levelName = engine->GetLevelNameShort();
	g_pVGuiLocalize->ConvertANSIToUnicode( levelName && levelName[0] ? levelName : "-", mapName, sizeof( mapName ) );

	const bool teamplay = HL2MPRules() && HL2MPRules()->IsTeamplay();
	const wchar_t *modeName = g_pVGuiLocalize->Find( teamplay ? "#ScoreBoard_TeamDeathmatch" : "#ScoreBoard_Deathmatch" );
	wchar_t fallbackMode[64];
	if ( !modeName )
	{
		g_pVGuiLocalize->ConvertANSIToUnicode( teamplay ? "Team Deathmatch" : "Deathmatch", fallbackMode, sizeof( fallbackMode ) );
		modeName = fallbackMode;
	}

	wchar_t timeValue[16];
	static ConVarRef mpTimeLimit( "mp_timelimit" );
	if ( mpTimeLimit.IsValid() && mpTimeLimit.GetFloat() > 0.0f )
	{
		const float remainingTime = HL2MPRules() ? MAX( 0.0f, HL2MPRules()->GetMapRemainingTime() ) : 0.0f;
		int remainingSeconds = (int)remainingTime;
		if ( (float)remainingSeconds < remainingTime )
		{
			++remainingSeconds;
		}
		V_snwprintf( timeValue, ARRAYSIZE( timeValue ), L"%d:%02d", remainingSeconds / 60, remainingSeconds % 60 );
	}
	else
	{
		V_wcsncpy( timeValue, L"--:--", sizeof( timeValue ) );
	}

	wchar_t matchInfo[512];
	V_snwprintf(
		matchInfo,
		ARRAYSIZE( matchInfo ),
		L"%ls  \x2022  %ls  \x2022  %d/%d",
		mapName,
		modeName,
		GetConnectedPlayerCount(),
		gpGlobals->maxClients );
	m_pMatchInfo->SetText( matchInfo );
	m_pTimeLeft->SetText( timeValue );
}

void CHL2MPClientScoreBoardDialog::UpdateColumnWidths( int listWide )
{
	const int scoreWide = scheme()->GetProportionalScaledValueEx( GetScheme(), SCOREBOARD_SCORE_WIDTH );
	const int deathWide = scheme()->GetProportionalScaledValueEx( GetScheme(), SCOREBOARD_DEATH_WIDTH );
	const int pingWide = scheme()->GetProportionalScaledValueEx( GetScheme(), SCOREBOARD_PING_WIDTH );
	const int avatarWide = ShowAvatars() ? m_iAvatarWidth * 2 : 0;
	const int nameWide = MAX( scheme()->GetProportionalScaledValueEx( GetScheme(), 180 ), listWide - avatarWide - scoreWide - deathWide - pingWide );

	const int activeSections[] =
	{
		SCORESECTION_HEADER,
		SCORESECTION_COMBINE,
		SCORESECTION_REBELS,
		SCORESECTION_FREEFORALL
	};

	for ( int i = 0; i < ARRAYSIZE( activeSections ); ++i )
	{
		const int sectionID = activeSections[i];
		if ( ShowAvatars() )
		{
			m_pPlayerList->SetColumnWidthBySection( sectionID, "avatar", avatarWide );
		}
		m_pPlayerList->SetColumnWidthBySection( sectionID, "name", nameWide );
		m_pPlayerList->SetColumnWidthBySection( sectionID, "frags", scoreWide );
		m_pPlayerList->SetColumnWidthBySection( sectionID, "deaths", deathWide );
		m_pPlayerList->SetColumnWidthBySection( sectionID, "ping", pingWide );
	}

	m_pPlayerList->SetColumnWidthBySection( SCORESECTION_SPECTATOR, "name", listWide );
	m_pPlayerList->InvalidateLayout();
}
