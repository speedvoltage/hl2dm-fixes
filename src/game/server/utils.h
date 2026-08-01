//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: General-purpose, project-agnostic helpers shared across the HL2MP
//          server code. Functions here must not depend on any game-specific
//          types or state so they remain freely reusable.
//
// $NoKeywords: $
//=============================================================================//

#ifndef HL2MP_UTILS_H
#define HL2MP_UTILS_H
#ifdef _WIN32
#pragma once
#endif

#include "tier0/basetypes.h" // byte, color32

//-----------------------------------------------------------------------------
// String helpers
//-----------------------------------------------------------------------------

// Trims leading/trailing whitespace in place and returns the buffer pointer.
char *UTIL_TrimWhitespace( char *pszText );

// Returns true if pszString ends with pszSuffix (case-insensitive).
bool UTIL_StringEndsWithCaseInsensitive( const char *pszString, const char *pszSuffix );

// Truncates pszLine at the first "//" or "#" comment marker.
void UTIL_StripLineComment( char *pszLine );

// Returns true if ch is a single or double quote character.
bool UTIL_IsQuoteChar( char ch );

//-----------------------------------------------------------------------------
// URL helpers
//-----------------------------------------------------------------------------

// Returns true if ch is an RFC 3986 unreserved character that needs no encoding.
bool UTIL_IsURLSafeChar( unsigned char ch );

// Percent-encodes pszInput into pszOut, never writing more than iOutSize bytes.
void UTIL_URLEncode( const char *pszInput, char *pszOut, int iOutSize );

//-----------------------------------------------------------------------------
// Color helpers
//-----------------------------------------------------------------------------

// Clamps an integer to the [0,255] byte range used by color components.
byte UTIL_ClampColorComponent( int value );

// Builds an opaque color32 from individual r/g/b components.
color32 UTIL_MakeColor32( byte r, byte g, byte b );

// Parses an "r g b" string into a color32, falling back to the given values.
color32 UTIL_ParseColor32( const char *pszValue, byte rFallback, byte gFallback, byte bFallback );

#endif // HL2MP_UTILS_H
