#ifndef HL2MP_COMMUNITY_UI_H
#define HL2MP_COMMUNITY_UI_H
namespace vgui { class Panel; }
void HL2MPCommunity_Init();
bool HL2MPCommunity_Available();
vgui::Panel *HL2MPCommunity_Create( vgui::Panel *parent );
void HL2MPCommunity_Open( vgui::Panel *panel );
void HL2MPCommunity_Toggle( vgui::Panel *panel );
#endif
