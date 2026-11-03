#include "cbase.h"
#include "hl2mp_messages.h"
#include "vgui/ILocalize.h"
#include "vgui_controls/Controls.h"
#include "steam/steam_api.h"
#include "tier0/icommandline.h"
#include "filesystem.h"
#include "tier0/memdbgon.h"

void HL2MPMessages_Init()
{
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
}

void HL2MPMessageText( const char *token, char *text, int size )
{
    const wchar_t *localized = g_pVGuiLocalize->Find( token );
    V_UnicodeToUTF8( localized ? localized : L"The match has ended", text, size );
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
