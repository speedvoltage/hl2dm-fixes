#include "cbase.h"
#include "hl2mp_messages.h"
#include "vgui/ILocalize.h"
#include "vgui_controls/Controls.h"
#include "steam/steam_api.h"
#include "tier0/icommandline.h"
#include "filesystem.h"
#include <stdarg.h>
#include "tier1/utlstring.h"
#include "tier0/memdbgon.h"

namespace
{
struct Message { const char *key, *english, *format; };
// Generated from the same catalogs as the resource files. Missing resources
// therefore still produce readable English, including during early startup.
#include "hl2mp_messages_fallback.inc"
bool s_Initialized = false;
const Message *FindMessage( const char *key )
{
    if ( key && *key == '#' ) ++key;
    for ( int i = 0; i < ARRAYSIZE( s_Messages ); ++i )
        if ( !Q_stricmp( key, s_Messages[i].key ) ) return &s_Messages[i];
    return NULL;
}
char *TextBuffer()
{
    static char buffers[32][8192]; static unsigned next = 0;
    char *buffer = buffers[next++ % ARRAYSIZE( buffers )]; buffer[0] = 0; return buffer;
}
void Format( char *out, int size, const char *format, va_list args )
{
    const Message *message = NULL;
    for ( int i = 0; i < ARRAYSIZE( s_Messages ); ++i )
        if ( s_Messages[i].format && !Q_strcmp( s_Messages[i].format, format ) ) { message = &s_Messages[i]; break; }
    if ( !message ) { Q_vsnprintf( out, size, format, args ); return; }
    CUtlString values[16]; int count = 0;
    for ( const char *p = format; *p && count < ARRAYSIZE( values ); ++p )
    {
        if ( *p != '%' ) continue;
        const char *start = p++;
        if ( *p == '%' ) continue;
        while ( *p && strchr( "-+0 #.0123456789", *p ) ) ++p;
        char spec[32], value[4096];
        const int length = p - start + 1;
        if ( !*p || length >= sizeof( spec ) ) { Q_strncpy( out, message->english, size ); return; }
        Q_strncpy( spec, start, length + 1 );
        switch ( *p )
        {
        case 's': { const char *text = va_arg( args, const char * ); Q_strncpy( value, text ? text : "", sizeof( value ) ); break; }
        case 'd': case 'i': case 'c': Q_snprintf( value, sizeof( value ), spec, va_arg( args, int ) ); break;
        case 'u': case 'x': case 'X': case 'o': Q_snprintf( value, sizeof( value ), spec, va_arg( args, unsigned int ) ); break;
        case 'f': case 'g': case 'e': Q_snprintf( value, sizeof( value ), spec, va_arg( args, double ) ); break;
        default: Q_strncpy( out, message->english, size ); return;
        }
        values[count++] = value;
    }
    char token[128]; Q_snprintf( token, sizeof( token ), "#%s", message->key );
    const char *pattern = HL2MPText( token ); int used = 0;
    for ( const char *p = pattern; *p && used + 1 < size; )
    {
        if ( *p == '{' && p[1] >= '0' && p[1] <= '9' )
        {
            char *end; const long index = strtol( p + 1, &end, 10 );
            if ( *end == '}' && index >= 0 && index < count )
            {
                const char *value = values[index].String();
                while ( *value && used + 1 < size ) out[used++] = *value++;
                p = end + 1; continue;
            }
        }
        out[used++] = *p++;
    }
    out[used] = 0;
}
}

void HL2MPMessages_Init()
{
    if ( !g_pVGuiLocalize ) return;
    for ( int i = 0; !s_Initialized && i < ARRAYSIZE( s_Messages ); ++i )
    {
        wchar_t text[8192]; V_UTF8ToUnicode( s_Messages[i].english, text, sizeof( text ) );
        g_pVGuiLocalize->AddString( s_Messages[i].key, text, "hl2mp_builtin" );
    }
    s_Initialized = true;
    // Load English first, then the selected game language. Explicit filenames
    // also support Steam languages newer than the engine's language enum.
    g_pVGuiLocalize->AddFile( "resource/hl2mp_messages_english.txt", "GAME", true );
    char language[64]; engine->GetUILanguage( language, sizeof( language ) );
    if ( steamapicontext && steamapicontext->SteamApps() )
        Q_strncpy( language, steamapicontext->SteamApps()->GetCurrentGameLanguage(), sizeof( language ) );
    Q_strncpy( language, CommandLine()->ParmValue( "-language", language ), sizeof( language ) );
    for ( const char *p = language; *p; ++p ) if ( *p < 'a' || *p > 'z' ) return;
    char path[MAX_PATH]; Q_snprintf( path, sizeof( path ), "resource/hl2mp_messages_%s.txt", language );
    if ( Q_strcmp( language, "english" ) && filesystem->FileExists( path, "GAME" ) )
        g_pVGuiLocalize->AddFile( path, "GAME", true );
    g_pVGuiLocalize->AddFile( "resource/hl2mp_ui_english.txt", "GAME", true );
    Q_snprintf( path, sizeof( path ), "resource/hl2mp_ui_%s.txt", language );
    if ( Q_strcmp( language, "english" ) && filesystem->FileExists( path, "GAME" ) )
        g_pVGuiLocalize->AddFile( path, "GAME", true );
}

void HL2MPMessageText( const char *token, char *text, int size )
{
    Q_strncpy( text, HL2MPText( token ), size );
}

const char *HL2MPText( const char *token )
{
    if ( !token ) return "";
    if ( *token != '#' ) return token;
    if ( g_pVGuiLocalize )
    {
        const wchar_t *text = g_pVGuiLocalize->Find( token );
        if ( text ) { char *buffer = TextBuffer(); V_UnicodeToUTF8( text, buffer, 8192 ); return buffer; }
    }
    const Message *message = FindMessage( token );
    if ( message ) return message->english;
    // Unknown external tokens should never expose internal identifiers as UI copy.
    return "Message unavailable.";
}

const wchar_t *HL2MPWideText( const char *token )
{
    static wchar_t buffers[16][8192]; static unsigned next = 0;
    wchar_t *buffer = buffers[next++ % ARRAYSIZE( buffers )];
    V_UTF8ToUnicode( HL2MPText( token ), buffer, sizeof( buffers[0] ) ); return buffer;
}

const char *HL2MPServiceError( const char *reason )
{
    // Protocol errors are matched against owned copy. Unknown backend identifiers
    // fall back to a clear sentence instead of leaking raw service strings.
    for ( int i = 0; i < ARRAYSIZE( s_Messages ); ++i )
        if ( reason && !Q_strcmp( reason, s_Messages[i].english ) )
        {
            char token[128]; Q_snprintf( token, sizeof( token ), "#%s", s_Messages[i].key );
            return HL2MPText( token );
        }
    return HL2MPText( "#HL2MP_ServiceUnavailable" );
}

const char *HL2MPKeyName( const char *binding )
{
    char token[128]; Q_snprintf( token, sizeof( token ), "#HL2MP_Key_%s", binding );
    // Only the displayed key name changes. Bind/unbind still use engine codes.
    return FindMessage( token ) ? HL2MPText( token ) : binding;
}

const char *HL2MPFormat( const char *englishFormat, ... )
{
    // Format uses HL2MPText's ring too, so reserve the result before substitution.
    char *text = TextBuffer(); va_list args; va_start( args, englishFormat );
    Format( text, 8192, englishFormat, args ); va_end( args ); return text;
}

void HL2MPFormatInto( char *text, int size, const char *englishFormat, ... )
{
    va_list args; va_start( args, englishFormat ); Format( text, size, englishFormat, args ); va_end( args );
}

const char *HL2MPMatchReasonToken( const char *reason )
{
    if ( !reason || !reason[0] ) return "#HL2MP_MatchEnded";
    // Legacy engine text can include a "Disconnect:" prefix and punctuation.
    if ( Q_stristr( reason, "reservation_ended" ) ) return "#HL2MP_MatchEnded";
    if ( Q_stristr( reason, "no_show" ) || Q_stristr( reason, "no-show" ) || Q_stristr( reason, "did not reach" ) ) return "#HL2MP_NoShow";
    if ( Q_stristr( reason, "left" ) || Q_stristr( reason, "abandon" ) || Q_stristr( reason, "player_disconnect" ) ) return "#HL2MP_PlayerLeft";
    if ( Q_stristr( reason, "server" ) || Q_stristr( reason, "coordinator" ) || Q_stristr( reason, "service" ) ) return "#HL2MP_ServiceUnavailable";
    if ( Q_stristr( reason, "cancel" ) || Q_stristr( reason, "declin" ) || Q_stristr( reason, "ready" ) ) return "#HL2MP_MatchCancelled";
    if ( Q_stristr( reason, "reserv" ) || Q_stristr( reason, "grant" ) ) return "#HL2MP_ReservationInvalid";
    return "#HL2MP_MatchEnded";
}

CON_COMMAND_F( mm_localization_status, "Check loaded localization keys and formatted messages.", FCVAR_CLIENTDLL )
{
    int missing = 0;
    for ( int i = 0; i < ARRAYSIZE( s_Messages ); ++i )
    {
        if ( !g_pVGuiLocalize || !g_pVGuiLocalize->Find( s_Messages[i].key ) )
        { ++missing; Msg( "Missing localization: %s\n", s_Messages[i].key ); }
    }
    Msg( "Localization: %d keys, %d missing\n", ARRAYSIZE( s_Messages ), missing );
    Msg( "%s\n", HL2MPFormat( "Load %s in %s mode? This ends the current round.", "dm_lockdown", HL2MPText( "#HL2MP_UI_7c04d94e2260" ) ) );
    Msg( "%s\n", HL2MPText( "#HL2MP_IntegrityFailed" ) );
}
