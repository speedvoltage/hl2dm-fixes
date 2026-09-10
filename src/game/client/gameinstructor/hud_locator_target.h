//====== Copyright © 1996-2008, Valve Corporation, All rights reserved. =======
//
// Purpose: Add entities to this system, and the Locator will maintain an arrow
//			on the HUD that points to the entities when they are offscreen.
//
//=============================================================================

#ifndef L4D_HUD_LOCATOR_H
#define L4D_HUD_LOCATOR_H
#ifdef _WIN32
#pragma once
#endif

#include "vgui_controls/PHandle.h"

#define MAX_LOCATOR_BINDINGS_SHOWN 8
#define MAX_LOCATOR_TARGETS 10
#include "gameinstructor_shared.h"

#include "tier1/utlsymbol.h"

// See comments in UtlSymbol on why this is useful
class CGameInstructorSymbol
{
public:
    CGameInstructorSymbol() {}
    CGameInstructorSymbol(const char *pString) : m_SymbolId(m_SymbolTable.AddString(pString ? pString : "")) {}
    bool operator==(const CGameInstructorSymbol &other) const { return m_SymbolId == other.m_SymbolId; }
    const char *String() const { return m_SymbolTable.String(m_SymbolId); }

private:
    CUtlSymbol m_SymbolId;
    static CUtlSymbolTable m_SymbolTable;
};

//-----------------------------------------------------------------------------
// This class represents a single target to be tracked by the locator
//-----------------------------------------------------------------------------
class CLocatorTarget
{
  public:
    bool m_bOriginInScreenspace;
    Vector m_vecOrigin; // The location in the world to draw on the locator

    // ONLY the locator panel should fiddle with these fields.
    bool m_isActive;
    int m_serialNumber;
    int m_frameLastUpdated;
    bool m_bOnscreen;
    bool m_bOccluded;
    bool m_bVisible;
    bool m_bIsDrawing;
    float m_distFromPlayer;
    CHudTexture *m_pIcon_onscreen;
    CHudTexture *m_pIcon_offscreen;
    int m_iBindingTick;
    float m_flNextBindingTick;
    float m_flNextOcclusionTest;
    int m_iBindingChoicesCount;
    char m_szBindingChoices[MAX_LOCATOR_BINDINGS_SHOWN][64];
    float m_flNextBindingUpdate;
    int m_iBindChoicesOriginalToken[MAX_LOCATOR_BINDINGS_SHOWN];

    // Fields for drawing
    int m_targetX; // screen X position of the actual target
    int m_targetY; // screen Y position of the actual target
    int m_iconX;   // screen X position (top)
    int m_iconY;   // screen Y position (left)
    int m_centerX; // screen X position (center)
    int m_centerY; // screen Y position (center)
    int m_wide; // draw width of icon (may be different from frame to frame as the icon's size animates, for instance)
    int m_tall; // draw height of icon  ''			''
    float m_widthScale_onscreen; // for icons that are wider than standard
    int m_alpha;                 //
    float m_fadeStart;           // time stamp when fade out started
    float m_lerpStart;           // time stamp when lerping started
    float m_pulseStart;          // time stamp when pulsing started
    int m_declutterIndex;        // sort order from the declutterer
    int m_lastDeclutterIndex;    // last sort order from the declutterer
    int m_drawArrowDirection;    // Whether to draw an arrow indicating this target is off-screen, also tells us which
                                 // arrow to draw (left, up, etc.)
    int m_captionWide;           // How wide (pixels) my caption is.
    bool m_bDrawControllerButton;
    bool m_bDrawControllerButtonOffscreen;
    int m_offsetX; // User-specified X offset which is applied in screenspace
    int m_offsetY; // User-specified Y offset which is applied in screenspace

    // Fields for interpolating icon position
    float m_flTimeLerpDone; // How much time left before this icon arrives where it is supposed to be.
    int m_lastXPos;         // screen X position last frame
    int m_lastYPos;         // ''     Y

    CLocatorTarget(void);
    void Activate(int serialNumber);
    void Deactivate(bool bNoFade = false);
    void Update();

    int GetIconX(void);
    int GetIconY(void);
    int GetIconCenterX(void);
    int GetIconCenterY(void);
    int GetIconWidth(void);
    int GetIconHeight(void);

    void AddIconEffects(int add)
    {
        m_iEffectsFlags |= add;
    }
    void RemoveIconEffects(int remove)
    {
        m_iEffectsFlags &= ~remove;
    }
    int GetIconEffectsFlags()
    {
        return m_iEffectsFlags;
    }
    void SetCaptionColor(Color col)
    {
        m_captionColor = col;
    }
    void SetCaptionColor(const char *pszCaptionColor);
    bool IsStatic();
    bool IsPresenting();
    void StartTimedLerp();
    void StartPresent();
    void EndPresent();

    void UpdateVguiTarget(void);
    vgui::Panel *GetVguiTarget(void);
    void SetVguiTargetName(const char *pchVguiTargetName);
    const char *GetVguiTargetName(void)
    {
        return m_szVguiTargetName.String();
    }
    void SetVguiTargetLookup(const char *pchVguiTargetLookup);
    const char *GetVguiTargetLookup(void)
    {
        return m_szVguiTargetLookup.String();
    }
    void SetVguiTargetEdge(int nVguiEdge);
    int GetVguiTargetEdge(void) const
    {
        return m_nVguiTargetEdge;
    }

    void SetOnscreenIconTextureName(const char *pszTexture);
    void SetOffscreenIconTextureName(const char *pszTexture);
    void SetBinding(const char *pszBinding);
    const char *UseBindingImage(char *pchIconTextureName, size_t bufSize);

    const char *GetOnscreenIconTextureName()
    {
        return m_szOnscreenTexture.String();
    }
    const char *GetOffscreenIconTextureName()
    {
        return m_szOffscreenTexture.String();
    }
    const char *GetBinding()
    {
        return m_szBinding.String();
    }

    void SetVisible(bool bVisible);
    bool IsVisible(void);

    void SetCaptionText(const char *pszText, const char *pszParam);
    const wchar_t *GetCaptionText(void)
    {
        return (const wchar_t *)m_wszCaption.Base();
    }
    bool HasCaptionText(void)
    {
        return m_wszCaption.Count() > 1;
    }

    void DrawBindingName(const char *pchDrawName)
    {
        m_pchDrawBindingName = pchDrawName;
    }
    void DrawBindingNameOffscreen(const char *pchDrawName)
    {
        m_pchDrawBindingNameOffscreen = pchDrawName;
    }

    const char *DrawBindingName(void)
    {
        return m_pchDrawBindingName;
    }
    const char *DrawBindingNameOffscreen(void)
    {
        return m_pchDrawBindingNameOffscreen;
    }

    bool IsOnScreen()
    {
        return m_bOnscreen;
    }
    bool IsOccluded()
    {
        return m_bOccluded;
    }

  private:
    CGameInstructorSymbol m_szVguiTargetName;
    CGameInstructorSymbol m_szVguiTargetLookup;
    vgui::DHANDLE<vgui::Panel> m_hVguiTarget;
    int m_nVguiTargetEdge;

    CGameInstructorSymbol m_szOnscreenTexture;
    CGameInstructorSymbol m_szOffscreenTexture;
    CGameInstructorSymbol m_szBinding;

    bool m_bWasControllerLast;
    const char *m_pchDrawBindingName;
    const char *m_pchDrawBindingNameOffscreen;
    int m_iEffectsFlags;
    CUtlVector<wchar_t> m_wszCaption;

  public:
    Color m_captionColor;
};

void Locator_ResetTargets();
extern int Locator_AddTarget();
extern void Locator_RemoveTarget(int hTarget);
CLocatorTarget *Locator_GetTargetFromHandle(int hTarget);
void Locator_ComputeTargetIconPositionFromHandle(int hTarget);

#endif // L4D_HUD_LOCATOR_H
