#ifndef HL2MP_INTEGRITY_H
#define HL2MP_INTEGRITY_H
#ifdef _WIN32
#pragma once
#endif

enum { HL2MP_INTEGRITY_PROTOCOL = 1 };

#ifdef CLIENT_DLL
bool HL2MPIntegrity_Check( char *reason, int reasonSize );
void HL2MPIntegrity_Init();
#else
class CHL2MP_Player;
class KeyValues;
bool HL2MPIntegrity_Begin( CHL2MP_Player *player );
void HL2MPIntegrity_Reply( CHL2MP_Player *player, KeyValues *message );
#endif

#endif
