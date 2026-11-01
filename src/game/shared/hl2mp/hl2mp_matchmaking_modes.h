#ifndef HL2MP_MATCHMAKING_MODES_H
#define HL2MP_MATCHMAKING_MODES_H
#ifdef _WIN32
#pragma once
#endif

enum { HL2MP_MAX_PARTY_MEMBERS = 10 };

// Preserve the original wire names for duel and 2v2 clients.
inline int HL2MPMatchTeamSize( const char *mode )
{
	if ( !Q_strcmp( mode, "duel" ) ) return 1;
	if ( !Q_strcmp( mode, "team" ) ) return 2;
	if ( !Q_strcmp( mode, "team3" ) ) return 3;
	if ( !Q_strcmp( mode, "team4" ) ) return 4;
	if ( !Q_strcmp( mode, "team5" ) ) return 5;
	return !Q_strcmp( mode, "casual" ) || !Q_strcmp( mode, "custom" ) ? 0 : -1;
}

inline const char *HL2MPMatchModeName( const char *mode )
{
	if ( !Q_strcmp( mode, "custom" ) ) return "PRIVATE MATCH";
	const char *names[] = { "CASUAL", "1v1 DUEL", "2v2 TEAM DEATHMATCH", "3v3 TEAM DEATHMATCH", "4v4 TEAM DEATHMATCH", "5v5 TEAM DEATHMATCH" };
	const int size = HL2MPMatchTeamSize( mode );
	return size >= 0 ? names[size] : "MATCH";
}
#endif
