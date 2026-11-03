#include "cbase.h"
#include "hl2mp_integrity.h"
#include "hl2mp_player.h"
#include "igamesystem.h"
#include "tier0/memdbgon.h"

extern void FinishClientPutInServer( CHL2MP_Player *player );

namespace
{
struct Admission
{
    EHANDLE player;
    int first, second;
    double deadline;
    bool sent;
    Admission() : first( 0 ), second( 0 ), deadline( 0 ), sent( false ) {}
};

void Reject( CHL2MP_Player *player, const char *reason )
{
    // Reason strings are fixed server-owned text, never client input.
    char command[256];
    Q_snprintf( command, sizeof( command ), "kickid %d %s\n", player->GetUserID(), reason );
    engine->ServerCommand( command );
    Msg( "Multiplayer integrity: rejected userid %d (%s)\n", player->GetUserID(), reason );
}

class CIntegrityAdmissions : public CAutoGameSystemPerFrame
{
public:
    CIntegrityAdmissions() : CAutoGameSystemPerFrame( "HL2MPIntegrity" ) {}
    Admission slots[MAX_PLAYERS_ARRAY_SAFE];
    void LevelShutdownPostEntity() OVERRIDE
    {
        for ( int i = 0; i < ARRAYSIZE( slots ); ++i ) slots[i] = Admission();
    }
    void FrameUpdatePostEntityThink() OVERRIDE
    {
        for ( int i = 1; i <= gpGlobals->maxClients; ++i )
        {
            Admission &slot = slots[i];
            CHL2MP_Player *player = ToHL2MPPlayer( slot.player.Get() );
            if ( !player || !player->IsConnected() ) { slot = Admission(); continue; }
            if ( Plat_FloatTime() >= slot.deadline )
            {
                Reject( player, "#HL2MP_IntegrityTimeout" );
                slot = Admission();
            }
            else if ( !slot.sent )
            {
                CSingleUserRecipientFilter filter( player ); filter.MakeReliable();
                UserMessageBegin( filter, "PureChallenge" );
                WRITE_BYTE( HL2MP_INTEGRITY_PROTOCOL ); WRITE_LONG( slot.first ); WRITE_LONG( slot.second );
                MessageEnd();
                slot.sent = true;
            }
        }
    }
} s_Admissions;
}

bool HL2MPIntegrity_Begin( CHL2MP_Player *player )
{
    if ( player->IsFakeClient() || player->IsHLTV() || gpGlobals->eLoadType == MapLoad_Background ) return true;
    player->m_bIntegrityVerified = false;
    Admission &slot = s_Admissions.slots[player->entindex()];
    slot = Admission(); slot.player = player;
    slot.first = RandomInt( 1, 0x7ffffffe ); slot.second = RandomInt( 1, 0x7ffffffe );
    slot.deadline = Plat_FloatTime() + 20.0;
    return false;
}

void HL2MPIntegrity_Reply( CHL2MP_Player *player, KeyValues *message )
{
    if ( !player || player->entindex() < 1 || player->entindex() >= MAX_PLAYERS_ARRAY_SAFE ) return;
    Admission &slot = s_Admissions.slots[player->entindex()];
    if ( slot.player.Get() != player || !slot.sent || Plat_FloatTime() >= slot.deadline ) return;
    // Bind the response to this connection; late/replayed replies cannot admit it.
    if ( message->GetInt( "first" ) != slot.first || message->GetInt( "second" ) != slot.second ) return;
    slot = Admission();
    if ( message->GetInt( "protocol" ) != HL2MP_INTEGRITY_PROTOCOL || !message->GetBool( "ready" ) )
    {
        Reject( player, "#HL2MP_IntegrityFailed" );
        return;
    }
    // This handshake enforces use of the checking client, not remote attestation:
    // arbitrary modified binaries can forge responses and require anti-cheat.
    player->m_bIntegrityVerified = true;
    player->CBasePlayer::ChangeTeam( TEAM_UNASSIGNED );
    player->StopObserverMode(); player->State_Transition( STATE_ACTIVE );
    player->RemoveEffects( EF_NOSHADOW ); player->SetPlayerModel();
    Msg( "Multiplayer integrity: admitted userid %d\n", player->GetUserID() );
    FinishClientPutInServer( player );
}
