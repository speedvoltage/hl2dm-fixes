//========= Copyright Valve Corporation, All rights reserved. ============//

#ifndef CHL2MPCLIENTSCOREBOARDDIALOG_H
#define CHL2MPCLIENTSCOREBOARDDIALOG_H
#ifdef _WIN32
#pragma once
#endif

#include <clientscoreboarddialog.h>

namespace vgui
{
class Label;
}

class CHL2MPClientScoreBoardDialog : public CClientScoreBoardDialog
{
private:
	DECLARE_CLASS_SIMPLE( CHL2MPClientScoreBoardDialog, CClientScoreBoardDialog );

public:
	CHL2MPClientScoreBoardDialog( IViewPort *pViewPort );
	~CHL2MPClientScoreBoardDialog() OVERRIDE;

	void Update() OVERRIDE;

protected:
	void InitScoreboardSections() OVERRIDE;
	void UpdateTeamInfo() OVERRIDE;
	bool GetPlayerScoreInfo( int playerIndex, KeyValues *outPlayerInfo ) OVERRIDE;
	void UpdatePlayerInfo() OVERRIDE;
	void PaintBackground() OVERRIDE;
	void PaintBorder() OVERRIDE;
	void ApplySchemeSettings( vgui::IScheme *pScheme ) OVERRIDE;
	void PerformLayout() OVERRIDE;

private:
	void AddHeader() OVERRIDE;
	void AddSection( int teamType, int teamNumber ) OVERRIDE;
	int GetSectionFromTeamNumber( int teamNumber );
	void SetSectionHeader( int teamNumber, const wchar_t *teamName, int playerCount, int score, bool showScore );
	void UpdateMatchInfo();
	void UpdateColumnWidths( int listWide );
	int GetConnectedPlayerCount() const;

	enum
	{
		SCOREBOARD_NAME_WIDTH = 320,
		SCOREBOARD_SCORE_WIDTH = 52,
		SCOREBOARD_DEATH_WIDTH = 58,
		SCOREBOARD_PING_WIDTH = 54,
	};

	vgui::Label *m_pServerName;
	vgui::Label *m_pMatchInfo;
	vgui::Label *m_pTimeLeft;
	Color m_backgroundColor;
	Color m_borderColor;
	Color m_accentColor;
	Color m_playerTextColor;
	Color m_secondaryTextColor;
	Color m_spectatorTextColor;
	Color m_localPlayerBackgroundColor;
};

#endif
