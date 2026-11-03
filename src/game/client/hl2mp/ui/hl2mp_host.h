#ifndef HL2MP_HOST_UI_H
#define HL2MP_HOST_UI_H
namespace vgui { class Panel; }
vgui::Panel *HL2MPHost_Create( vgui::Panel *parent );
void HL2MPHost_Open( vgui::Panel *panel );
#endif
