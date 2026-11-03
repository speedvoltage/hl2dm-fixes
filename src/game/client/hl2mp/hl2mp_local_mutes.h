#ifndef HL2MP_LOCAL_MUTES_H
#define HL2MP_LOCAL_MUTES_H
enum { HL2MP_MUTE_CHAT = 1, HL2MP_MUTE_VOICE = 2 };
int HL2MPLocalMuteFlags( int playerIndex );
void HL2MPSetLocalMute( int playerIndex, int flags );
void HL2MPClearLocalMutes();
#endif
