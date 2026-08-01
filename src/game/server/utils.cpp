//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: General-purpose, project-agnostic helpers shared across the HL2MP
//          server code. See utils.h for the contract these functions follow.
//
// $NoKeywords: $
//=============================================================================//

#include "cbase.h"
#include "utils.h"

#include <stdio.h>
#include <string.h>

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

char *UTIL_TrimWhitespace( char *pszText )
{
	if ( !pszText )
		return pszText;

	char *pszStart = pszText;
	while ( *pszStart && V_isspace( (unsigned char)*pszStart ) )
	{
		++pszStart;
	}

	if ( pszStart != pszText )
	{
		memmove( pszText, pszStart, V_strlen( pszStart ) + 1 );
	}

	char *pszEnd = pszText + V_strlen( pszText );
	while ( pszEnd > pszText && V_isspace( (unsigned char)*( pszEnd - 1 ) ) )
	{
		--pszEnd;
		*pszEnd = '\0';
	}

	return pszText;
}

bool UTIL_StringEndsWithCaseInsensitive( const char *pszString, const char *pszSuffix )
{
	if ( pszString == NULL || pszSuffix == NULL )
		return false;

	const int iStringLen = Q_strlen( pszString );
	const int iSuffixLen = Q_strlen( pszSuffix );

	if ( iStringLen < iSuffixLen )
		return false;

	return Q_stricmp( pszString + iStringLen - iSuffixLen, pszSuffix ) == 0;
}

void UTIL_StripLineComment( char *pszLine )
{
	if ( !pszLine )
		return;

	char *pszComment = Q_strstr( pszLine, "//" );
	if ( pszComment )
	{
		*pszComment = '\0';
	}

	pszComment = Q_strstr( pszLine, "#" );
	if ( pszComment )
	{
		*pszComment = '\0';
	}
}

bool UTIL_IsQuoteChar( char ch )
{
	return ch == '"' || ch == '\'';
}

bool UTIL_IsURLSafeChar( unsigned char ch )
{
	if ( ( ch >= 'A' && ch <= 'Z' ) ||
		 ( ch >= 'a' && ch <= 'z' ) ||
		 ( ch >= '0' && ch <= '9' ) )
	{
		return true;
	}

	switch ( ch )
	{
	case '-':
	case '_':
	case '.':
	case '~':
		return true;
	default:
		return false;
	}
}

void UTIL_URLEncode( const char *pszInput, char *pszOut, int iOutSize )
{
	if ( iOutSize <= 0 )
		return;

	pszOut[ 0 ] = '\0';

	if ( pszInput == NULL )
		return;

	static const char s_Hex[] = "0123456789ABCDEF";
	int iOut = 0;

	for ( const unsigned char *p = ( const unsigned char * )pszInput; *p != '\0' && iOut < iOutSize - 1; ++p )
	{
		const unsigned char ch = *p;

		if ( UTIL_IsURLSafeChar( ch ) )
		{
			pszOut[ iOut++ ] = ( char )ch;
		}
		else
		{
			if ( iOut + 3 >= iOutSize )
				break;

			pszOut[ iOut++ ] = '%';
			pszOut[ iOut++ ] = s_Hex[ ( ch >> 4 ) & 0x0F ];
			pszOut[ iOut++ ] = s_Hex[ ch & 0x0F ];
		}
	}

	pszOut[ iOut ] = '\0';
}

byte UTIL_ClampColorComponent( int value )
{
	if ( value < 0 )
		return 0;

	if ( value > 255 )
		return 255;

	return static_cast< byte >( value );
}

color32 UTIL_MakeColor32( byte r, byte g, byte b )
{
	color32 color = { r, g, b, 255 };
	return color;
}

color32 UTIL_ParseColor32( const char *pszValue, byte rFallback, byte gFallback, byte bFallback )
{
	int r = rFallback;
	int g = gFallback;
	int b = bFallback;

	if ( pszValue && pszValue[ 0 ] )
	{
		int parsedR = 0;
		int parsedG = 0;
		int parsedB = 0;

		if ( sscanf( pszValue, "%d %d %d", &parsedR, &parsedG, &parsedB ) == 3 )
		{
			r = parsedR;
			g = parsedG;
			b = parsedB;
		}
	}

	return UTIL_MakeColor32(
		UTIL_ClampColorComponent( r ),
		UTIL_ClampColorComponent( g ),
		UTIL_ClampColorComponent( b ) );
}
