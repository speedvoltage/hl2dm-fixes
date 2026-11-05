#include "cbase.h"
#include "hl2mp_sprays.h"
#include "hl2mp_menucontrols.h"
#include "hl2mp_messages.h"
#include "filesystem.h"
#include "imageutils.h"
#include "vtf/vtf.h"
#include "tier1/checksum_crc.h"
#include "tier1/utlbuffer.h"
#include "vgui_controls/FileOpenDialog.h"
#include "vgui_controls/Label.h"
#include "tier0/memdbgon.h"

namespace
{
bool WriteSprayMaterial(const char *name)
{
    char path[MAX_PATH]; Q_snprintf(path, sizeof(path), "materials/vgui/logos/%s.vmt", name);
    CUtlBuffer material(0, 0, CUtlBuffer::TEXT_BUFFER);
    material.Printf("\"LightmappedGeneric\"\n{\n\t\"$basetexture\" \"vgui/logos/%s\"\n\t\"$translucent\" \"1\"\n\t\"$decal\" \"1\"\n\t\"$decalscale\" \"0.250\"\n}\n", name);
    return filesystem->WriteFile(path, "MOD", material);
}
// Never write alongside the source image. A content-addressed name also avoids
// replacing a previously imported spray when two files have the same name.
bool ImportSpray(const char *source, char *destination, int destinationSize)
{
    const int sourceSize = filesystem->Size(source);
    if (sourceSize <= 0 || sourceSize > 16 * 1024 * 1024) return false;
    int width = 0, height = 0;
    ConversionErrorType error;
    unsigned char *rgba = ImgUtl_ReadImageAsRGBA(source, width, height, error);
    if (!rgba) return false;
    if (width <= 0 || height <= 0 || width > 8192 || height > 8192) { free(rgba); return false; }

    const int size = 256;
    const float scale = MIN(float(size) / width, float(size) / height);
    const int scaledW = MAX(1, int(width * scale)), scaledH = MAX(1, int(height * scale));
    CUtlVector<unsigned char> scaled; scaled.SetCount(scaledW * scaledH * 4);
    error = ImgUtl_StretchRGBAImage(rgba, width, height, scaled.Base(), scaledW, scaledH);
    free(rgba);
    if (error != CE_SUCCESS) return false;
    IVTFTexture *texture = CreateVTFTexture();
    if (!texture->Init(size, size, 1, IMAGE_FORMAT_RGBA8888,
        TEXTUREFLAGS_CLAMPS | TEXTUREFLAGS_CLAMPT | TEXTUREFLAGS_EIGHTBITALPHA, 1))
    { DestroyVTFTexture(texture); return false; }
    memset(texture->ImageData(), 0, size * size * 4);
    for (int y = 0; y < scaledH; ++y)
        memcpy(texture->ImageData() + (((size - scaledH) / 2 + y) * size + (size - scaledW) / 2) * 4,
            scaled.Base() + y * scaledW * 4, scaledW * 4);
    texture->GenerateMipmaps();
    texture->ConvertImageFormat(IMAGE_FORMAT_DXT5, false);
    CUtlBuffer output;
    const bool serialized = texture->Serialize(output);
    DestroyVTFTexture(texture);
    if (!serialized || output.TellPut() > 128 * 1024) return false;

    // Already valid VTFs retain their animation and original compressed pixels.
    const char *ext = V_GetFileExtension(source);
    if (ext && !Q_stricmp(ext, "vtf") && sourceSize <= 128 * 1024 && width <= size && height <= size)
    {
        output.Clear();
        if (!filesystem->ReadFile(source, NULL, output)) return false;
    }
    CRC32_t crc; CRC32_Init(&crc); CRC32_ProcessBuffer(&crc, output.Base(), output.TellPut()); CRC32_Final(&crc);
    char stem[48]; V_FileBase(source, stem, sizeof(stem));
    for (char *c=stem; *c; ++c) if (!V_isalnum(*c) && *c!='-' && *c!='_') *c='_';
    char name[80]; Q_snprintf(name, sizeof(name), "custom_%s_%08x", stem, crc);
    Q_snprintf(destination, destinationSize, "materials/vgui/logos/%s.vtf", name);
    filesystem->CreateDirHierarchy("materials/vgui/logos", "MOD");
    if (!filesystem->WriteFile(destination, "MOD", output)) return false;
    return WriteSprayMaterial(name);
}
}

bool HL2MPSprays_Apply(const char *path)
{
    if (!path[0]) return true;
    // Source restores this canonical filename after loading config.cfg when
    // Steam Cloud settings are enabled, even if no cloud spray was downloaded.
    // Keep a local copy so selecting a spray also survives an offline restart.
    CUtlBuffer texture;
    if (!filesystem->ReadFile(path, "GAME", texture)) return false;
    filesystem->CreateDirHierarchy("materials/vgui/logos", "MOD");
    return filesystem->WriteFile("materials/vgui/logos/spray.vtf", "MOD", texture) && WriteSprayMaterial("spray");
}

CHL2MPSprayPicker::CHL2MPSprayPicker(vgui::Panel *parent, const char *value)
    : BaseClass(parent, "SprayPicker"), m_Initial(value), m_Texture(-1), m_HasPreview(false), m_Scale(1)
{
    SetPaintBackgroundEnabled(false);
    m_Choices = new CHL2MPDropdown(this, "Sprays"); m_Choices->AddActionSignalTarget(this);
    m_Import = new CHL2MPSettingsButton(this, "#HL2MP_SprayImport", "import");
    m_Status = new vgui::Label(this, "Status", "#HL2MP_SprayFormats");
    m_Status->SetWrap(true); m_Status->SetContentAlignment(vgui::Label::a_northwest);
    FileFindHandle_t handle;
    for (const char *file = filesystem->FindFirstEx("materials/vgui/logos/*.vtf", "GAME", &handle);
         file; file = filesystem->FindNext(handle))
    {
        if (filesystem->FindIsDirectory(handle)) continue;
        char path[MAX_PATH], material[MAX_PATH]; Q_snprintf(path, sizeof(path), "materials/vgui/logos/%s", file);
        Q_StripExtension(path, material, sizeof(material)); Q_strncat(material, ".vmt", sizeof(material));
        if (!filesystem->FileExists(material, "GAME")) continue;
        Select(path);
    }
    filesystem->FindClose(handle);
    Select(value); Preview();
}
CHL2MPSprayPicker::~CHL2MPSprayPicker()
{
    if (m_Dialog.Get()) m_Dialog->MarkForDeletion();
    if (m_Texture != -1) vgui::surface()->DestroyTextureID(m_Texture);
}
void CHL2MPSprayPicker::Select(const char *path)
{
    for (int i = 0; i < m_Choices->GetItemCount(); ++i)
    {
        const int id = m_Choices->GetItemIDFromRow(i);
        if (!Q_stricmp(m_Choices->GetItemUserData(id)->GetString("path"), path))
        { m_Choices->SilentActivateItemByRow(i); return; }
    }
    char title[MAX_PATH]; V_FileBase(path, title, sizeof(title));
    if (!Q_strnicmp(title,"custom_",7))
    {
        char *hash=strrchr(title,'_'); if (hash && Q_strlen(hash)==9) *hash=0;
        memmove(title,title+7,Q_strlen(title+7)+1);
    }
    else if (!Q_strnicmp(title,"spray_",6)) memmove(title,title+6,Q_strlen(title+6)+1);
    for (char *c=title; *c; ++c) if (*c=='_') *c=' ';
    KeyValues *data = new KeyValues("Spray", "path", path);
    const int id = m_Choices->AddItem(title[0] ? title : "#HL2MP_UI_c627c09c14e5", data); data->deleteThis();
    // Populating the list must not enqueue selections that later replace the
    // saved choice after the options page has opened.
    m_Choices->SilentActivateItem(id);
}
CUtlString CHL2MPSprayPicker::Value() const
{
    KeyValues *data = m_Choices->GetActiveItemUserData();
    return data ? data->GetString("path") : m_Initial.String();
}
void CHL2MPSprayPicker::Preview()
{
    CUtlString path = Value(); if (path == m_PreviewPath) return;
    m_PreviewPath = path; m_HasPreview = false;
    // VGUI texture uploads of a different size may only update a subrectangle.
    // Recreate the texture when changing images to avoid showing old pixels.
    if (m_Texture != -1) { vgui::surface()->DestroyTextureID(m_Texture); m_Texture = -1; }
    int w, h; ConversionErrorType error;
    unsigned char *rgba = ImgUtl_ReadVTFAsRGBA(path.String(), w, h, error);
    if (rgba)
    {
        if (m_Texture == -1) m_Texture = vgui::surface()->CreateNewTextureID(true);
        vgui::surface()->DrawSetTextureRGBA(m_Texture, rgba, w, h, true, true); free(rgba); m_HasPreview = true;
    }
    Repaint();
}
void CHL2MPSprayPicker::Style(float scale, vgui::HFont font)
{
    m_Scale = scale; m_Choices->Style(font, scale); m_Import->SetFont(font);
    m_Status->SetFont(font); m_Status->SetFgColor(Color(180, 188, 187, 255)); InvalidateLayout(true);
}
void CHL2MPSprayPicker::CloseDropdown() { m_Choices->HideMenu(); }
bool CHL2MPSprayPicker::DropdownOpen() const { return m_Choices->GetMenu()->IsVisible(); }
void CHL2MPSprayPicker::PerformLayout()
{
    BaseClass::PerformLayout();
    const int left = int(126 * m_Scale), w = MAX(40, GetWide() - left), row = int(34 * m_Scale);
    m_Choices->SetBounds(left, 0, w, row);
    m_Import->SetBounds(left, row + int(6 * m_Scale), w, row);
    m_Status->SetBounds(left, 2 * row + int(12 * m_Scale), w, GetTall() - 2 * row - int(12 * m_Scale));
}
void CHL2MPSprayPicker::OnCommand(const char *command)
{
    if (!Q_strcmp(command, "import"))
    {
        if (m_Dialog.Get()) { m_Dialog->MoveToFront(); return; }
        m_Dialog = new vgui::FileOpenDialog(this, HL2MPText("#HL2MP_SprayImport"), true);
        m_Dialog->AddActionSignalTarget(this);
#ifdef _WIN32
        m_Dialog->AddFilter("*.png,*.jpg,*.jpeg,*.bmp,*.tga,*.vtf", "#HL2MP_SprayFiles", true);
#else
        m_Dialog->AddFilter("*.png,*.jpg,*.jpeg,*.tga,*.vtf", "#HL2MP_SprayFiles", true);
#endif
        m_Dialog->AddFilter("*.png", "PNG (*.png)", false);
        m_Dialog->AddFilter("*.jpg,*.jpeg", "JPEG (*.jpg, *.jpeg)", false);
#ifdef _WIN32
        m_Dialog->AddFilter("*.bmp", "BMP (*.bmp)", false);
#endif
        m_Dialog->AddFilter("*.tga", "TGA (*.tga)", false);
        m_Dialog->AddFilter("*.vtf", "VTF (*.vtf)", false);
        m_Dialog->SetStartDirectoryContext("HL2MPSprays", ""); m_Dialog->DoModal();
        return;
    }
    BaseClass::OnCommand(command);
}
void CHL2MPSprayPicker::OnFileSelected(KeyValues *data)
{
    char path[MAX_PATH];
    if (ImportSpray(data->GetString("fullpath"), path, sizeof(path)))
    { Select(path); Preview(); m_Status->SetText("#HL2MP_SprayImported"); }
    else m_Status->SetText("#HL2MP_SprayImportFailed");
}
void CHL2MPSprayPicker::Paint()
{
    BaseClass::Paint();
    const int size = int(112 * m_Scale), tile = MAX(4, int(14 * m_Scale));
    for (int y = 0; y < size; y += tile) for (int x = 0; x < size; x += tile)
    {
        const int shade = ((x / tile + y / tile) % 2) ? 48 : 64;
        vgui::surface()->DrawSetColor(shade, shade, shade, 255);
        vgui::surface()->DrawFilledRect(x, y, MIN(x + tile, size), MIN(y + tile, size));
    }
    if (m_HasPreview)
    {
        vgui::surface()->DrawSetColor(255, 255, 255, 255); vgui::surface()->DrawSetTexture(m_Texture);
        vgui::surface()->DrawTexturedRect(0, 0, size, size);
    }
    vgui::surface()->DrawSetColor(HL2MPServerTheme::Orange());
    vgui::surface()->DrawOutlinedRect(0, 0, size, size);
}
