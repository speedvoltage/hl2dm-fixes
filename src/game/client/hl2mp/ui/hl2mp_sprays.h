#ifndef HL2MP_SPRAYS_H
#define HL2MP_SPRAYS_H
#include "vgui_controls/EditablePanel.h"
#include "tier1/utlstring.h"

class CHL2MPDropdown;
class CHL2MPSettingsButton;
namespace vgui { class FileOpenDialog; class Label; }
bool HL2MPSprays_Apply(const char *path);

class CHL2MPSprayPicker : public vgui::EditablePanel
{
    DECLARE_CLASS_SIMPLE(CHL2MPSprayPicker, vgui::EditablePanel);
public:
    CHL2MPSprayPicker(vgui::Panel *parent, const char *value);
    ~CHL2MPSprayPicker();
    CUtlString Value() const;
    void Style(float scale, vgui::HFont font);
    void CloseDropdown();
    bool DropdownOpen() const;
    void PerformLayout() OVERRIDE;
    void OnCommand(const char *command) OVERRIDE;
    void Paint() OVERRIDE;
private:
    void Select(const char *path);
    void Preview();
    MESSAGE_FUNC_PARAMS(OnFileSelected, "FileSelected", data);
    MESSAGE_FUNC(OnSelectionChanged, "TextChanged") { Preview(); }
    CHL2MPDropdown *m_Choices;
    CHL2MPSettingsButton *m_Import;
    vgui::Label *m_Status;
    vgui::DHANDLE<vgui::FileOpenDialog> m_Dialog;
    CUtlString m_Initial, m_PreviewPath;
    int m_Texture;
    bool m_HasPreview;
    float m_Scale;
};
#endif
