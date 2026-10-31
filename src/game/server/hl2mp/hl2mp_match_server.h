#ifndef HL2MP_MATCH_SERVER_H
#define HL2MP_MATCH_SERVER_H
#ifdef _WIN32
#pragma once
#endif

class CBasePlayer;
class CHL2MP_Player;
class KeyValues;

bool HL2MPMatchServerEnabled();
bool HL2MPMatchServerPlaying();
int HL2MPMatchServerTeam( CBasePlayer *player );
void HL2MPMatchServerSpawn( CHL2MP_Player *player );
void HL2MPMatchServerDisconnect( CBasePlayer *player );
void HL2MPMatchServerJoin( CBasePlayer *player, KeyValues *message );
void HL2MPMatchServerFinished();

#endif
