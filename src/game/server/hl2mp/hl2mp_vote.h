#ifndef HL2MP_SERVER_VOTE_H
#define HL2MP_SERVER_VOTE_H
class CBasePlayer;
class KeyValues;
void HL2MPVoteRequest( CBasePlayer *player, KeyValues *message );
bool HL2MPVoteBeginIntermission();
bool HL2MPVoteHoldsIntermission();
void HL2MPVoteReset();
#endif
