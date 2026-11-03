#ifndef HL2MP_DIALOGS_H
#define HL2MP_DIALOGS_H
class IGameUI;
void HL2MPDialogs_ShowQuit( IGameUI *gameUI, bool keepEscapeBlocked );
void HL2MPDialogs_Update( IGameUI *gameUI, bool keepEscapeBlocked );
void HL2MPDialogs_Destroy();
#endif
