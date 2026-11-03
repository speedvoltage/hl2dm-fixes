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
	if ( !Q_strcmp( mode, "custom" ) ) return "#HL2MP_UI_20b3f3627b47";
	const char *names[] = { "#HL2MP_UI_ff2fb9e38104", "#HL2MP_UI_52046b6a11c7", "#HL2MP_UI_8bcd949595e0", "#HL2MP_UI_bbf0d9176234", "#HL2MP_UI_50cbc1f839ea", "#HL2MP_UI_f9dd708e0a1c" };
	const int size = HL2MPMatchTeamSize( mode );
	return size >= 0 ? names[size] : "#HL2MP_UI_eaa42a15398b";
}
#endif
