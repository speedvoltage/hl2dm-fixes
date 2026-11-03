#include "cbase.h"
#include "hl2mp/ui/hl2mp_messages.h"
#include "hl2mp_integrity.h"
#include "filesystem.h"
#include "usermessages.h"
#include "tier0/memdbgon.h"

namespace
{
const char *const PROBE_PATH_ID = "HL2MP_INTEGRITY";
const char *const PROBE_FILE = "hl2mp_integrity_probe.txt";
bool s_ProbeMounted = false;

bool Reject( char *reason, int size, const char *text )
{
    Q_strncpy( reason, HL2MPText( text ), size );
    return false;
}

void Challenge( bf_read &reader )
{
    int protocol = reader.ReadByte();
    int first = reader.ReadLong(), second = reader.ReadLong();
    if ( reader.IsOverflowed() || protocol != HL2MP_INTEGRITY_PROTOCOL ) return;
    char reason[256];
    bool ready = HL2MPIntegrity_Check( reason, sizeof( reason ) );
    if ( !ready ) Warning( "Multiplayer integrity check failed: %s\n", reason );
    KeyValues *reply = new KeyValues( "HL2MPIntegrity" );
    reply->SetInt( "protocol", HL2MP_INTEGRITY_PROTOCOL );
    reply->SetInt( "first", first ); reply->SetInt( "second", second );
    reply->SetBool( "ready", ready );
    engine->ServerCmdKeyValues( reply );
}
}

bool HL2MPIntegrity_Check( char *reason, int reasonSize )
{
    char absolute[MAX_PATH];
    Q_snprintf( absolute, sizeof( absolute ), "%s/gameinfo.txt", engine->GetGameDirectory() );
    KeyValuesAD root( "GameInfo" ), effective( "GameInfo" );
    if ( !root->LoadFromFile( filesystem, absolute ) || Q_stricmp( root->GetString( "type" ), "multiplayer_only" ) )
        return Reject( reason, reasonSize, "#HL2MP_UI_9188d052ad3b" );
    // Match the engine's unrestricted search, including custom search paths.
    if ( !effective->LoadFromFile( filesystem, "gameinfo.txt" ) || Q_stricmp( effective->GetString( "type" ), "multiplayer_only" ) )
        return Reject( reason, reasonSize, "#HL2MP_UI_b6b0dcafdd2c" );

    if ( !s_ProbeMounted )
    {
        Q_snprintf( absolute, sizeof( absolute ), "%s/resource/hl2mp_integrity_dir.vpk", engine->GetGameDirectory() );
        if ( !filesystem->FileExists( absolute ) )
            return Reject( reason, reasonSize, "#HL2MP_UI_c2229dafd698" );
        filesystem->AddSearchPath( absolute, PROBE_PATH_ID, PATH_ADD_TO_HEAD );
        s_ProbeMounted = true;
    }

    // A freshly accessed VPK file is recorded only when the engine enabled
    // tracking at startup. This checks that live state, not a cvar/disk claim.
    // Use a private path ID so permitted custom assets cannot shadow the probe.
    FileHandle_t file = filesystem->Open( PROBE_FILE, "rb", PROBE_PATH_ID );
    if ( !file ) return Reject( reason, reasonSize, "#HL2MP_UI_5b0421ca65ef" );
    char data[64];
    int count = filesystem->Read( data, sizeof( data ), file );
    filesystem->Close( file );
    FileHash_t hash;
    if ( count <= 0 || filesystem->CheckCachedFileHash( PROBE_PATH_ID, PROBE_FILE, 0, &hash ) != k_eFileCRCStatus_FileInVPK )
        return Reject( reason, reasonSize, "#HL2MP_UI_1b5b0cf89b78" );
    reason[0] = 0;
    return true;
}

void HL2MPIntegrity_Init()
{
    usermessages->HookMessage( "PureChallenge", Challenge );
}
