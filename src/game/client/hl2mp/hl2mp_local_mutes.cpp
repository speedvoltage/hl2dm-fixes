#include "cbase.h"
#include "hl2mp_local_mutes.h"
#include "voice_status.h"
#include "igamesystem.h"
#include "GameEventListener.h"
#include "tier0/memdbgon.h"

namespace
{
struct Mute { uint32 account; int userid, flags; };
CUtlVector<Mute> s_Mutes;
class CSessionMutes : public CAutoGameSystemPerFrame, public CGameEventListener
{
public:
    CSessionMutes() : CAutoGameSystemPerFrame( "SessionMutes" ) {}
    bool Init() OVERRIDE { ListenForGameEvent( "client_disconnect" ); return true; }
    void FireGameEvent( IGameEvent * ) OVERRIDE { HL2MPClearLocalMutes(); }
    void Update( float ) OVERRIDE { if ( !engine->IsConnected() ) HL2MPClearLocalMutes(); }
} s_System;
}
int HL2MPLocalMuteFlags( int index )
{
    player_info_t p; if ( index <= 0 || !engine->GetPlayerInfo( index, &p ) ) return 0;
    FOR_EACH_VEC( s_Mutes, i ) if ( p.friendsID ? s_Mutes[i].account == p.friendsID : !s_Mutes[i].account && s_Mutes[i].userid == p.userID ) return s_Mutes[i].flags;
    return 0;
}
void HL2MPSetLocalMute( int index, int flags )
{
    player_info_t p; if ( index <= 0 || index == engine->GetLocalPlayer() || !engine->GetPlayerInfo( index, &p ) ) return;
    FOR_EACH_VEC( s_Mutes, i ) if ( p.friendsID ? s_Mutes[i].account == p.friendsID : !s_Mutes[i].account && s_Mutes[i].userid == p.userID )
    { s_Mutes[i].flags = flags & 3; return; }
    Mute mute = { p.friendsID, p.userID, flags & 3 }; s_Mutes.AddToTail( mute );
}
void HL2MPClearLocalMutes() { s_Mutes.Purge(); }
CON_COMMAND_F( cl_local_mutes, "Show session-only chat and voice mutes.", FCVAR_CLIENTDLL )
{
    Msg( "Local mutes: %d stored identities (cleared on disconnect).\n", s_Mutes.Count() );
    for ( int i = 1; i <= gpGlobals->maxClients; ++i ) { int flags = HL2MPLocalMuteFlags( i ); if ( flags ) Msg( "  player %d: chat=%d voice=%d\n", i, ( flags & HL2MP_MUTE_CHAT ) != 0, ( flags & HL2MP_MUTE_VOICE ) != 0 ); }
}
