#ifndef HL2MP_CLIENT_VOTE_H
#define HL2MP_CLIENT_VOTE_H
void HL2MPVoteUI_Init();
void HL2MPVoteUI_Reset();
void HL2MPVoteUI_Preview();
void HL2MPVoteUI_ClosePreview();
void HL2MPVoteUI_CallKick( int userid );
bool HL2MPVoteUI_IsMapVote();
#endif
