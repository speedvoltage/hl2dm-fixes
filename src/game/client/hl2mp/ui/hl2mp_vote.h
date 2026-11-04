#ifndef HL2MP_CLIENT_VOTE_H
#define HL2MP_CLIENT_VOTE_H
namespace vgui { class Panel; }
void HL2MPVoteUI_Init();
void HL2MPVoteUI_Reset();
void HL2MPVoteUI_Preview( vgui::Panel *owner = NULL );
void HL2MPVoteUI_ClosePreview();
void HL2MPVoteUI_CreateEditorSample( vgui::Panel *owner );
void HL2MPVoteUI_CallKick( int userid );
bool HL2MPVoteUI_IsMapVote();
#endif
