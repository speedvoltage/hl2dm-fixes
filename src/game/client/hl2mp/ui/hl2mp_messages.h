#ifndef HL2MP_MESSAGES_H
#define HL2MP_MESSAGES_H
#ifdef _WIN32
#pragma once
#endif

void HL2MPMessages_Init();
void HL2MPMessageText( const char *token, char *text, int size );
// Resolve only localization tokens. Player names, addresses and free text pass through.
const char *HL2MPText( const char *token );
const wchar_t *HL2MPWideText( const char *token );
const char *HL2MPServiceError( const char *reason );
const char *HL2MPKeyName( const char *binding );
// Formats are trusted source strings; translations contain indexed placeholders,
// never executable printf directives. Returned strings live in a small ring buffer.
const char *HL2MPFormat( const char *englishFormat, ... );
void HL2MPFormatInto( char *text, int size, const char *englishFormat, ... );
// Service reason identifiers are mapped to player-facing localized text.
const char *HL2MPMatchReasonToken( const char *reason );

#endif
