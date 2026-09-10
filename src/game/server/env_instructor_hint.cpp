//========= Copyright Valve Corporation, All rights reserved. ============//

#include "cbase.h"
#include "gameinstructor_shared.h"
#include "world.h"

#include "tier0/memdbgon.h"

class CEnvInstructorHint : public CPointEntity
{
public:
    DECLARE_CLASS(CEnvInstructorHint, CPointEntity);
    DECLARE_DATADESC();

    CEnvInstructorHint();
    void Spawn() OVERRIDE;

private:
    void InputShowHint(inputdata_t &inputdata);
    void InputEndHint(inputdata_t &inputdata);

    string_t m_iszReplace_Key;
    string_t m_iszHintTargetEntity;
    int m_iTimeout;
    string_t m_iszIcon_Onscreen;
    string_t m_iszIcon_Offscreen;
    string_t m_iszCaption;
    string_t m_iszActivatorCaption;
    color32 m_Color;
    float m_fIconOffset;
    float m_fRange;
    uint8 m_iPulseOption;
    uint8 m_iAlphaOption;
    uint8 m_iShakeOption;
    bool m_bStatic;
    bool m_bNoOffscreen;
    bool m_bForceCaption;
    string_t m_iszBinding;
    bool m_bAllowNoDrawTarget;
    bool m_bLocalPlayerOnly;
};

LINK_ENTITY_TO_CLASS(env_instructor_hint, CEnvInstructorHint);

BEGIN_DATADESC(CEnvInstructorHint)
    DEFINE_KEYFIELD(m_iszReplace_Key, FIELD_STRING, "hint_replace_key"),
    DEFINE_KEYFIELD(m_iszHintTargetEntity, FIELD_STRING, "hint_target"),
    DEFINE_KEYFIELD(m_iTimeout, FIELD_INTEGER, "hint_timeout"),
    DEFINE_KEYFIELD(m_iszIcon_Onscreen, FIELD_STRING, "hint_icon_onscreen"),
    DEFINE_KEYFIELD(m_iszIcon_Offscreen, FIELD_STRING, "hint_icon_offscreen"),
    DEFINE_KEYFIELD(m_iszCaption, FIELD_STRING, "hint_caption"),
    DEFINE_KEYFIELD(m_iszActivatorCaption, FIELD_STRING, "hint_activator_caption"),
    DEFINE_KEYFIELD(m_Color, FIELD_COLOR32, "hint_color"),
    DEFINE_KEYFIELD(m_fIconOffset, FIELD_FLOAT, "hint_icon_offset"),
    DEFINE_KEYFIELD(m_fRange, FIELD_FLOAT, "hint_range"),
    DEFINE_KEYFIELD(m_iPulseOption, FIELD_CHARACTER, "hint_pulseoption"),
    DEFINE_KEYFIELD(m_iAlphaOption, FIELD_CHARACTER, "hint_alphaoption"),
    DEFINE_KEYFIELD(m_iShakeOption, FIELD_CHARACTER, "hint_shakeoption"),
    DEFINE_KEYFIELD(m_bStatic, FIELD_BOOLEAN, "hint_static"),
    DEFINE_KEYFIELD(m_bNoOffscreen, FIELD_BOOLEAN, "hint_nooffscreen"),
    DEFINE_KEYFIELD(m_bForceCaption, FIELD_BOOLEAN, "hint_forcecaption"),
    DEFINE_KEYFIELD(m_iszBinding, FIELD_STRING, "hint_binding"),
    DEFINE_KEYFIELD(m_bAllowNoDrawTarget, FIELD_BOOLEAN, "hint_allow_nodraw_target"),
    DEFINE_KEYFIELD(m_bLocalPlayerOnly, FIELD_BOOLEAN, "hint_local_player_only"),
    DEFINE_INPUTFUNC(FIELD_STRING, "ShowHint", InputShowHint),
    DEFINE_INPUTFUNC(FIELD_VOID, "EndHint", InputEndHint),
END_DATADESC()

CEnvInstructorHint::CEnvInstructorHint()
    : m_iszReplace_Key(NULL_STRING), m_iszHintTargetEntity(NULL_STRING), m_iTimeout(0),
      m_iszIcon_Onscreen(NULL_STRING), m_iszIcon_Offscreen(NULL_STRING), m_iszCaption(NULL_STRING),
      m_iszActivatorCaption(NULL_STRING), m_fIconOffset(0.0f), m_fRange(0.0f),
      m_iPulseOption(0), m_iAlphaOption(0), m_iShakeOption(0), m_bStatic(false),
      m_bNoOffscreen(false), m_bForceCaption(false), m_iszBinding(NULL_STRING),
      m_bAllowNoDrawTarget(true), m_bLocalPlayerOnly(false)
{
    m_Color.r = m_Color.g = m_Color.b = m_Color.a = 255;
}

void CEnvInstructorHint::Spawn()
{
    BaseClass::Spawn();
    if (GetEntityName() == NULL_STRING)
        SetName(AllocPooledString(CFmtStr("instructor_hint_%d", entindex())));
    if (m_iszIcon_Onscreen == NULL_STRING)
        m_iszIcon_Onscreen = AllocPooledString("icon_tip");
    if (m_iszIcon_Offscreen == NULL_STRING)
        m_iszIcon_Offscreen = AllocPooledString("icon_tip");
}

void CEnvInstructorHint::InputShowHint(inputdata_t &inputdata)
{
    if (sv_gameinstructor_disable.GetBool())
        return;

    CBasePlayer *pActivator = ToBasePlayer(inputdata.pActivator);
    bool bFilterByActivator = m_bLocalPlayerOnly;
    if (inputdata.value.StringID() != NULL_STRING && inputdata.value.String()[0])
    {
        pActivator = ToBasePlayer(gEntList.FindEntityByName(NULL, inputdata.value.String(),
            this, inputdata.pActivator, inputdata.pCaller));
        bFilterByActivator = true;
    }
    if (bFilterByActivator && !pActivator)
    {
        Warning("env_instructor_hint %s: ShowHint requires a player activator or parameter.\n", GetDebugName());
        return;
    }

    CBaseEntity *pTarget = gEntList.FindEntityByName(NULL, m_iszHintTargetEntity,
        this, inputdata.pActivator, inputdata.pCaller);
    if (!pTarget && !m_bStatic)
        pTarget = inputdata.pActivator;
    if (!pTarget)
        pTarget = GetWorldEntity();
    if (!pTarget)
        return;

    IGameEvent *event = gameeventmanager->CreateEvent("instructor_server_hint_create");
    if (!event)
        return;

    int flags = m_bStatic ? LOCATOR_ICON_FX_STATIC : LOCATOR_ICON_FX_NONE;
    if (m_iPulseOption >= 1 && m_iPulseOption <= 3)
        flags |= LOCATOR_ICON_FX_PULSE_SLOW << (m_iPulseOption - 1);
    if (m_iAlphaOption >= 1 && m_iAlphaOption <= 3)
        flags |= LOCATOR_ICON_FX_ALPHA_SLOW << (m_iAlphaOption - 1);
    if (m_iShakeOption >= 1 && m_iShakeOption <= 2)
        flags |= LOCATOR_ICON_FX_SHAKE_NARROW << (m_iShakeOption - 1);

    char color[16];
    Q_snprintf(color, sizeof(color), "%u,%u,%u", m_Color.r, m_Color.g, m_Color.b);
    const char *caption = STRING(m_iszCaption);
    const char *activatorCaption = STRING(m_iszActivatorCaption);

    event->SetString("hint_name", STRING(GetEntityName()));
    event->SetString("hint_replace_key", m_iszReplace_Key != NULL_STRING ? STRING(m_iszReplace_Key) : STRING(GetEntityName()));
    event->SetInt("hint_target", pTarget->entindex());
    event->SetInt("hint_activator_userid", pActivator ? pActivator->GetUserID() : 0);
    event->SetInt("hint_timeout", clamp(m_iTimeout, 0, 32767));
    event->SetString("hint_icon_onscreen", STRING(m_iszIcon_Onscreen));
    event->SetString("hint_icon_offscreen", STRING(m_iszIcon_Offscreen));
    event->SetString("hint_caption", caption);
    event->SetString("hint_activator_caption", activatorCaption[0] ? activatorCaption : caption);
    event->SetString("hint_color", color);
    event->SetFloat("hint_icon_offset", m_fIconOffset);
    event->SetFloat("hint_range", MAX(0.0f, m_fRange));
    event->SetInt("hint_flags", flags);
    event->SetString("hint_binding", STRING(m_iszBinding));
    event->SetBool("hint_allow_nodraw_target", m_bAllowNoDrawTarget);
    event->SetBool("hint_nooffscreen", m_bNoOffscreen);
    event->SetBool("hint_forcecaption", m_bForceCaption);
    event->SetBool("hint_local_player_only", bFilterByActivator);
    gameeventmanager->FireEvent(event);
}

void CEnvInstructorHint::InputEndHint(inputdata_t &inputdata)
{
    IGameEvent *event = gameeventmanager->CreateEvent("instructor_server_hint_stop");
    if (event)
    {
        event->SetString("hint_name", STRING(GetEntityName()));
        gameeventmanager->FireEvent(event);
    }
}

class CInfoInstructorHintTarget : public CPointEntity
{
public:
    DECLARE_CLASS(CInfoInstructorHintTarget, CPointEntity);
    DECLARE_DATADESC();

    int UpdateTransmitState() OVERRIDE
    {
        return SetTransmitState(FL_EDICT_ALWAYS);
    }
};

LINK_ENTITY_TO_CLASS(info_target_instructor_hint, CInfoInstructorHintTarget);

BEGIN_DATADESC(CInfoInstructorHintTarget)
END_DATADESC()
