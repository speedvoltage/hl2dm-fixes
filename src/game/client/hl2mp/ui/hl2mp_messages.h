#ifndef HL2MP_MESSAGES_H
#define HL2MP_MESSAGES_H
#ifdef _WIN32
#pragma once
#endif

void HL2MPMessages_Init();
void HL2MPMessageText( const char *token, char *text, int size );
// Service reason identifiers are mapped to player-facing localized text.
const char *HL2MPMatchReasonToken( const char *reason );

#endif
