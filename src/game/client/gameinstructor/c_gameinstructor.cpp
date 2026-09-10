//========= Copyright © 1996-2008, Valve Corporation, All rights reserved. ============//
//
// Purpose:		Client handler implementations for instruction players how to play
//
//=============================================================================//

#include "cbase.h"

#include "c_gameinstructor.h"
#include "c_baselesson.h"
#include "filesystem.h"
#include "vprof.h"
#include "tier0/icommandline.h"
#include "iclientmode.h"
#include "ienginevgui.h"

#ifdef INSOURCE
#include "in_player_shared.h"
#endif

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

//=========================================================
// Configuración
//=========================================================

#define MOD_DIR "MOD"
#define GAMEINSTRUCTOR_SCRIPT_FILE "scripts/instructor_lessons.txt"
#define GAMEINSTRUCTOR_MOD_SCRIPT_FILE "scripts/mod_lessons.txt"

// Game instructor auto game system instantiation
C_GameInstructor g_GameInstructor;
C_GameInstructor &GetGameInstructor()
{
    return g_GameInstructor;
}

extern ConVar sv_gameinstructor_disable;

ConVar gameinstructor_verbose("gameinstructor_verbose", "0", FCVAR_CHEAT,
    "Set to 1 for standard debugging or 2 for lesson updates.");
ConVar gameinstructor_verbose_lesson("gameinstructor_verbose_lesson", "", FCVAR_CHEAT,
    "Restrict verbose output to the named lesson.");
ConVar gameinstructor_find_errors("gameinstructor_find_errors", "0", FCVAR_CHEAT,
    "Run all scripted lesson actions to check for errors.");
ConVar gameinstructor_enable("gameinstructor_enable", "1", FCVAR_CLIENTDLL | FCVAR_ARCHIVE,
    "Display game instructor hints.");
ConVar gameinstructor_start_sound_cooldown("gameinstructor_start_sound_cooldown", "4.0", FCVAR_NONE,
    "Minimum interval between matching lesson sounds.", true, 0.0f, false, 0.0f);

bool C_GameInstructor::IsEnabled() const
{
    return m_bInitialized && gameinstructor_enable.GetBool() && !sv_gameinstructor_disable.GetBool();
}

//=========================================================
// Inicializa al Instructor
//=========================================================
bool C_GameInstructor::Init()
{
    if (m_bInitialized)
        return true;

    m_bInitialized = true;

    if (gameinstructor_verbose.GetInt() > 0)
    {
        ConColorMsg(CBaseLesson::m_rgbaVerboseHeader, "[INSTRUCTOR]: ");
        ConColorMsg(CBaseLesson::m_rgbaVerbosePlain, "Initializing...\n");
    }

    m_bNoDraw = false;
    m_bHiddenDueToOtherElements = false;

    m_iCurrentPriority = 0;
    m_hLastSpectatedPlayer = NULL;
    m_bSpectatedPlayerChanged = false;

    m_szPreviousStartSound[0] = '\0';
    m_fNextStartSoundTime = 0;

    ReadLessonsFromFile(GAMEINSTRUCTOR_MOD_SCRIPT_FILE);
    ReadLessonsFromFile(GAMEINSTRUCTOR_SCRIPT_FILE);

    InitLessonPrerequisites();
    ReadSaveData();

    ListenForGameEvent("gameinstructor_draw");
    ListenForGameEvent("gameinstructor_nodraw");

    ListenForGameEvent("round_end");
    ListenForGameEvent("round_start");
    ListenForGameEvent("player_death");
    ListenForGameEvent("player_team");
    ListenForGameEvent("player_disconnect");
    ListenForGameEvent("map_transition");
    ListenForGameEvent("game_newmap");
    ListenForGameEvent("set_instructor_group_enabled");

    EvaluateLessonsForGameRules();
    return true;
}

//=========================================================
// Apaga al instructor
//=========================================================
void C_GameInstructor::Shutdown()
{
    if (!m_bInitialized)
        return;

    if (gameinstructor_verbose.GetInt() > 0)
    {
        ConColorMsg(CBaseLesson::m_rgbaVerboseHeader, "[INSTRUCTOR]: ");
        ConColorMsg(CBaseLesson::m_rgbaVerbosePlain, "Shutting down...\n");
    }

    CloseAllOpenOpportunities();
    WriteSaveData();

    // Removemos todas las lecciones.
    for (int i = 0; i < m_Lessons.Count(); ++i)
    {
        if (m_Lessons[i])
        {
            m_Lessons[i]->StopListeningForAllEvents();
            delete m_Lessons[i];
            m_Lessons[i] = NULL;
        }
    }

    m_Lessons.RemoveAll();
    m_LessonGroupConVarToggles.RemoveAll();

    // Paramos de escuchar eventos.
    StopListeningForAllEvents();
    Locator_ResetTargets();
    m_bInitialized = false;
    m_bHasLoadedSaveData = false;
}

//=========================================================
//=========================================================
void C_GameInstructor::UpdateHiddenByOtherElements()
{
    bool bHidden = enginevgui->IsGameUIVisible() || engine->IsDrawingLoadingImage();

    if (bHidden && !m_bHiddenDueToOtherElements)
        StopAllLessons();

    m_bHiddenDueToOtherElements = bHidden;
}

//=========================================================
//=========================================================
void C_GameInstructor::LevelShutdownPreEntity()
{
    CloseAllOpenOpportunities();
    Locator_ResetTargets();
    WriteSaveData();
    m_bNoDraw = false;
    m_iCurrentPriority = 0;
    m_hLastSpectatedPlayer = NULL;
    m_bSpectatedPlayerChanged = false;
    m_szPreviousStartSound[0] = '\0';
    m_fNextStartSoundTime = 0.0f;
}

void C_GameInstructor::Update(float frametime)
{
    VPROF_BUDGET("C_GameInstructor::Update", "GameInstructor");

    if (!IsEnabled())
    {
        CloseAllOpenOpportunities();
        Locator_ResetTargets();
        return;
    }

    UpdateHiddenByOtherElements();

    // Instructor desactivado.
    if (m_bNoDraw || m_bHiddenDueToOtherElements)
        return;

    if (gameinstructor_find_errors.GetBool())
    {
        FindErrors();
        gameinstructor_find_errors.SetValue(0);
    }

    if (IsConsole())
    {
        // On X360 we want to save when they're not connected
        // They aren't in game
        if (!engine->IsInGame())
            WriteSaveData();
        else
        {
            const char *levelName = engine->GetLevelName();

            // The are in game, but it's a background map
            if (levelName && levelName[0] && engine->IsLevelMainMenuBackground())
                WriteSaveData();
        }
    }

    if (m_bSpectatedPlayerChanged)
    {
        // Safe spot to clean out stale lessons if spectator changed
        if (gameinstructor_verbose.GetInt() > 0)
        {
            ConColorMsg(CBaseLesson::m_rgbaVerboseHeader, "[INSTRUCTOR]: ");
            ConColorMsg(CBaseLesson::m_rgbaVerbosePlain, "Spectated player changed...\n");
        }

        CloseAllOpenOpportunities();
        m_bSpectatedPlayerChanged = false;
    }

    // Loop through all the lesson roots and reset their active status
    for (int i = m_OpenOpportunities.Count() - 1; i >= 0; --i)
    {
        CBaseLesson *pLesson = m_OpenOpportunities[i];
        CBaseLesson *pRootLesson = pLesson->GetRoot();

        if (pRootLesson->InstanceType() == LESSON_INSTANCE_SINGLE_ACTIVE)
            pRootLesson->SetInstanceActive(false);
    }

    int iCurrentPriority = 0;

    // Loop through all the open lessons
    for (int i = m_OpenOpportunities.Count() - 1; i >= 0; --i)
    {
        CBaseLesson *pLesson = m_OpenOpportunities[i];

        // This opportunity has closed
        if (!pLesson->IsOpenOpportunity() || pLesson->IsTimedOut())
        {
            CloseOpportunity(pLesson);
            continue;
        }

        // Lesson should be displayed, so it can affect priority
        CBaseLesson *pRootLesson = pLesson->GetRoot();
        bool bShouldDisplay = pLesson->ShouldDisplay();
        bool bIsLocked = pLesson->IsLocked();

        if ((bShouldDisplay || bIsLocked) &&
            (pLesson->GetPriority() >= m_iCurrentPriority || pLesson->NoPriority() || bIsLocked) &&
            (pRootLesson &&
             (pRootLesson->InstanceType() != LESSON_INSTANCE_SINGLE_ACTIVE || !pRootLesson->IsInstanceActive())))
        {
            // Lesson is at the highest priority level, isn't violating instance rules, and has met all the
            // prerequisites
            if (UpdateActiveLesson(pLesson, pRootLesson) || pRootLesson->IsLearned())
            {
                // Lesson is active
                if (pLesson->IsVisible() || pRootLesson->IsLearned())
                {
                    pRootLesson->SetInstanceActive(true);

                    // This active or learned lesson has the highest priority so far
                    if (iCurrentPriority < pLesson->GetPriority() && !pLesson->NoPriority())
                        iCurrentPriority = pLesson->GetPriority();
                }
            }
            else
            {
                // On second thought, this shouldn't have been displayed
                bShouldDisplay = false;
            }
        }
        else
        {
            // Lesson shouldn't be displayed right now
            UpdateInactiveLesson(pLesson);
        }
    }

    // Set the priority for next frame
    if (gameinstructor_verbose.GetInt() > 1 && m_iCurrentPriority != iCurrentPriority)
    {
        ConColorMsg(CBaseLesson::m_rgbaVerboseHeader, "[INSTRUCTOR]: ");
        ConColorMsg(CBaseLesson::m_rgbaVerbosePlain, "Priority changed from ");
        ConColorMsg(CBaseLesson::m_rgbaVerboseClose, "%i ", m_iCurrentPriority);
        ConColorMsg(CBaseLesson::m_rgbaVerbosePlain, "to ");
        ConColorMsg(CBaseLesson::m_rgbaVerboseOpen, "%i", iCurrentPriority);
        ConColorMsg(CBaseLesson::m_rgbaVerbosePlain, ".\n");
    }

    m_iCurrentPriority = iCurrentPriority;
}

//=========================================================
//=========================================================
void C_GameInstructor::FireGameEvent(IGameEvent *event)
{
    VPROF_BUDGET("C_GameInstructor::FireGameEvent", "GameInstructor");
    const char *name = event->GetName();

    if (Q_strcmp(name, "gameinstructor_draw") == 0)
    {
        if (m_bNoDraw)
        {
            if (gameinstructor_verbose.GetInt() > 0)
            {
                ConColorMsg(CBaseLesson::m_rgbaVerboseHeader, "[INSTRUCTOR]: ");
                ConColorMsg(CBaseLesson::m_rgbaVerbosePlain, "Set to draw...\n");
            }

            m_bNoDraw = false;
        }
    }
    else if (Q_strcmp(name, "gameinstructor_nodraw") == 0)
    {
        if (!m_bNoDraw)
        {
            if (gameinstructor_verbose.GetInt() > 0)
            {
                ConColorMsg(CBaseLesson::m_rgbaVerboseHeader, "[INSTRUCTOR]: ");
                ConColorMsg(CBaseLesson::m_rgbaVerbosePlain, "Set to not draw...\n");
            }

            m_bNoDraw = true;
            StopAllLessons();
        }
    }
    else if (Q_strcmp(name, "round_end") == 0)
    {
        if (gameinstructor_verbose.GetInt() > 0)
        {
            ConColorMsg(CBaseLesson::m_rgbaVerboseHeader, "[INSTRUCTOR]: ");
            ConColorMsg(CBaseLesson::m_rgbaVerbosePlain, "Round ended...\n");
        }

        CloseAllOpenOpportunities();

        if (IsPC())
        {
            // Good place to backup our counts
            WriteSaveData();
        }
    }
    else if (Q_strcmp(name, "round_start") == 0)
    {
        if (gameinstructor_verbose.GetInt() > 0)
        {
            ConColorMsg(CBaseLesson::m_rgbaVerboseHeader, "[INSTRUCTOR]: ");
            ConColorMsg(CBaseLesson::m_rgbaVerbosePlain, "Round started...\n");
        }

        CloseAllOpenOpportunities();

        EvaluateLessonsForGameRules();
    }
    else if (Q_strcmp(name, "player_death") == 0)
    {
#if !defined(NO_STEAM) && defined(USE_CEG)
        Steamworks_TestSecret();
        Steamworks_SelfCheck();
#endif

        C_BasePlayer *pLocalPlayer = GetLocalPlayer();

        if (pLocalPlayer && pLocalPlayer == UTIL_PlayerByUserId(event->GetInt("userid")))
        {
            if (gameinstructor_verbose.GetInt() > 0)
            {
                ConColorMsg(CBaseLesson::m_rgbaVerboseHeader, "[INSTRUCTOR]: ");
                ConColorMsg(CBaseLesson::m_rgbaVerbosePlain, "Local player died...\n");
            }

            for (int i = m_OpenOpportunities.Count() - 1; i >= 0; --i)
            {
                CBaseLesson *pLesson = m_OpenOpportunities[i];
                CBaseLesson *pRootLesson = pLesson->GetRoot();

                if (!pRootLesson->CanOpenWhenDead())
                    CloseOpportunity(pLesson);
            }
        }
    }
    else if (Q_strcmp(name, "player_team") == 0)
    {
        C_BasePlayer *pLocalPlayer = GetLocalPlayer();

        if (pLocalPlayer && pLocalPlayer == UTIL_PlayerByUserId(event->GetInt("userid")) &&
            (event->GetInt("team") != event->GetInt("oldteam") || event->GetBool("disconnect")))
        {
            if (gameinstructor_verbose.GetInt() > 0)
            {
                ConColorMsg(CBaseLesson::m_rgbaVerboseHeader, "[INSTRUCTOR]: ");
                ConColorMsg(CBaseLesson::m_rgbaVerbosePlain, "Local player changed team (or disconnected)...\n");
            }

            CloseAllOpenOpportunities();
        }

        EvaluateLessonsForGameRules();
    }
    else if (Q_strcmp(name, "player_disconnect") == 0)
    {
        C_BasePlayer *pLocalPlayer = GetLocalPlayer();
        if (pLocalPlayer && pLocalPlayer == UTIL_PlayerByUserId(event->GetInt("userid")))
        {
            if (gameinstructor_verbose.GetInt() > 0)
            {
                ConColorMsg(CBaseLesson::m_rgbaVerboseHeader, "[INSTRUCTOR]: ");
                ConColorMsg(CBaseLesson::m_rgbaVerbosePlain, "Local player disconnected...\n");
            }

            CloseAllOpenOpportunities();
        }
    }
    else if (Q_strcmp(name, "map_transition") == 0)
    {
        if (gameinstructor_verbose.GetInt() > 0)
        {
            ConColorMsg(CBaseLesson::m_rgbaVerboseHeader, "[INSTRUCTOR]: ");
            ConColorMsg(CBaseLesson::m_rgbaVerbosePlain, "Map transition...\n");
        }

        CloseAllOpenOpportunities();

        if (m_bNoDraw)
        {
            if (gameinstructor_verbose.GetInt() > 0)
            {
                ConColorMsg(Color(255, 128, 64, 255), "[INSTRUCTOR]: ");
                ConColorMsg(Color(64, 128, 255, 255), "Set to draw...\n");
            }

            m_bNoDraw = false;
        }

        if (IsPC())
        {
            // Good place to backup our counts
            WriteSaveData();
        }
    }
    else if (Q_strcmp(name, "game_newmap") == 0)
    {
        if (gameinstructor_verbose.GetInt() > 0)
        {
            ConColorMsg(CBaseLesson::m_rgbaVerboseHeader, "[INSTRUCTOR]: ");
            ConColorMsg(CBaseLesson::m_rgbaVerbosePlain, "New map...\n");
        }

        CloseAllOpenOpportunities();

        if (m_bNoDraw)
        {
            if (gameinstructor_verbose.GetInt() > 0)
            {
                ConColorMsg(Color(255, 128, 64, 255), "[INSTRUCTOR]: ");
                ConColorMsg(Color(64, 128, 255, 255), "Set to draw...\n");
            }

            m_bNoDraw = false;
        }

        if (IsPC())
        {
            // Good place to backup our counts
            WriteSaveData();
        }
    }

    else if (Q_strcmp(name, "set_instructor_group_enabled") == 0)
    {
        const char *pszGroup = event->GetString("group");
        bool bEnabled = event->GetInt("enabled") != 0;

        if (pszGroup && pszGroup[0])
            SetLessonGroupEnabled(pszGroup, bEnabled);
    }
}

//=========================================================
//=========================================================
void C_GameInstructor::DefineLesson(CBaseLesson *pLesson)
{
    if (gameinstructor_verbose.GetInt() > 0)
    {
        ConColorMsg(CBaseLesson::m_rgbaVerboseHeader, "[INSTRUCTOR]: ");
        ConColorMsg(CBaseLesson::m_rgbaVerbosePlain, "Lesson ");
        ConColorMsg(CBaseLesson::m_rgbaVerboseName, "\"%s\" ", pLesson->GetName());
        ConColorMsg(CBaseLesson::m_rgbaVerbosePlain, "defined.\n");
    }

    m_Lessons.AddToTail(pLesson);
}

//=========================================================
//=========================================================
const CBaseLesson *C_GameInstructor::GetLesson(const char *pchLessonName)
{
    return GetLesson_Internal(pchLessonName);
}

//=========================================================
//=========================================================
bool C_GameInstructor::IsLessonOfSameTypeOpen(const CBaseLesson *pLesson) const
{
    for (int i = 0; i < m_OpenOpportunities.Count(); ++i)
    {
        CBaseLesson *pOpenOpportunity = m_OpenOpportunities[i];

        if (pOpenOpportunity->GetNameSymbol() == pLesson->GetNameSymbol())
            return true;
    }

    return false;
}

//=========================================================
//=========================================================
bool C_GameInstructor::ReadSaveData()
{
    if (m_bHasLoadedSaveData)
        return true;
    m_bHasLoadedSaveData = true;

    KeyValues *data = new KeyValues("Game Instructor Counts");
    KeyValues::AutoDelete autoDelete(data);
    bool loaded = !CommandLine()->FindParm("-playtest") &&
        data->LoadFromFile(g_pFullFileSystem, "save/game_instructor_counts.txt", MOD_DIR);

    ResetDisplaysAndSuccesses();
    if (loaded)
    {
        for (KeyValues *key = data->GetFirstTrueSubKey(); key; key = key->GetNextTrueSubKey())
        {
            CBaseLesson *lesson = GetLesson_Internal(key->GetName());
            if (lesson)
            {
                lesson->SetDisplayCount(MAX(0, key->GetInt("display")));
                lesson->SetSuccessCount(MAX(0, key->GetInt("success")));
            }
        }
    }

    CBaseLesson *version = GetLesson_Internal("version number");
    if (version)
    {
        if (!loaded || data->GetInt("version number/success") != version->GetSuccessLimit())
            ResetDisplaysAndSuccesses();
        version->SetSuccessCount(version->GetSuccessLimit());
    }
    return loaded;
}

bool C_GameInstructor::WriteSaveData()
{
    if (!m_bInitialized || !engine || engine->IsPlayingDemo() || CommandLine()->FindParm("-playtest"))
        return false;
    if (!m_bDirtySaveData)
        return true;

    KeyValues *data = new KeyValues("Game Instructor Counts");
    KeyValues::AutoDelete autoDelete(data);
    for (int i = 0; i < m_Lessons.Count(); ++i)
    {
        CBaseLesson *lesson = m_Lessons[i];
        if (!lesson->GetDisplayCount() && !lesson->GetSuccessCount())
            continue;
        KeyValues *counts = data->FindKey(lesson->GetName(), true);
        counts->SetInt("display", lesson->GetDisplayCount());
        counts->SetInt("success", lesson->GetSuccessCount());
    }

    CUtlBuffer buffer(0, 0, CUtlBuffer::TEXT_BUFFER);
    data->RecursiveSaveToFile(buffer, 0);
    filesystem->CreateDirHierarchy("save", MOD_DIR);
    bool saved = filesystem->WriteFile("save/game_instructor_counts.txt", MOD_DIR, buffer);
    m_bDirtySaveData = !saved;
    return saved;
}

void C_GameInstructor::RefreshDisplaysAndSuccesses()
{
    m_bHasLoadedSaveData = false;
    ReadSaveData();
}

//=========================================================
//=========================================================
void C_GameInstructor::ResetDisplaysAndSuccesses()
{
    if (gameinstructor_verbose.GetInt() > 0)
    {
        ConColorMsg(CBaseLesson::m_rgbaVerboseHeader, "[INSTRUCTOR]: ");
        ConColorMsg(CBaseLesson::m_rgbaVerbosePlain, "Reset all lesson display and success counts.\n");
    }

    for (int i = 0; i < m_Lessons.Count(); ++i)
    {
        m_Lessons[i]->ResetDisplaysAndSuccesses();
    }

    CBaseLesson *version = GetLesson_Internal("version number");
    if (version)
        version->SetSuccessCount(version->GetSuccessLimit());
    m_bDirtySaveData = true;
}

//=========================================================
//=========================================================
void C_GameInstructor::MarkDisplayed(const char *pchLessonName)
{
    CBaseLesson *pLesson = GetLesson_Internal(pchLessonName);

    if (!pLesson)
        return;

    if (gameinstructor_verbose.GetInt() > 0)
    {
        ConColorMsg(CBaseLesson::m_rgbaVerboseHeader, "[INSTRUCTOR]: ");
        ConColorMsg(CBaseLesson::m_rgbaVerbosePlain, "Lesson ");
        ConColorMsg(CBaseLesson::m_rgbaVerboseOpen, "\"%s\" ", pLesson->GetName());
        ConColorMsg(CBaseLesson::m_rgbaVerbosePlain, "marked as displayed.\n");
    }

    if (pLesson->IncDisplayCount())
        m_bDirtySaveData = true;
}

//=========================================================
//=========================================================
void C_GameInstructor::MarkSucceeded(const char *pchLessonName)
{
    CBaseLesson *pLesson = GetLesson_Internal(pchLessonName);

    if (!pLesson)
        return;

    if (gameinstructor_verbose.GetInt() > 0)
    {
        ConColorMsg(CBaseLesson::m_rgbaVerboseHeader, "[INSTRUCTOR]: ");
        ConColorMsg(CBaseLesson::m_rgbaVerbosePlain, "Lesson ");
        ConColorMsg(CBaseLesson::m_rgbaVerboseSuccess, "\"%s\" ", pLesson->GetName());
        ConColorMsg(CBaseLesson::m_rgbaVerbosePlain, "marked as succeeded.\n");
    }

    if (pLesson->IncSuccessCount())
        m_bDirtySaveData = true;
}

//=========================================================
//=========================================================
void C_GameInstructor::PlaySound(const char *pchSoundName)
{
    // emit alert sound
    C_BasePlayer *pLocalPlayer = C_BasePlayer::GetLocalPlayer();

    if (pLocalPlayer)
    {
        // Local player exists
        if (pchSoundName[0] != '\0' && Q_strcmp(m_szPreviousStartSound, pchSoundName) != 0)
        {
            Q_strncpy(m_szPreviousStartSound, pchSoundName, sizeof(m_szPreviousStartSound));
            m_fNextStartSoundTime = 0.0f;
        }

        if (gpGlobals->curtime >= m_fNextStartSoundTime && pchSoundName[0] != '\0')
        {
            // A sound was specified, so play it!
            pLocalPlayer->EmitSound(pchSoundName);
            m_fNextStartSoundTime = gpGlobals->curtime + gameinstructor_start_sound_cooldown.GetFloat();
        }
    }
}

//=========================================================
//=========================================================
bool C_GameInstructor::OpenOpportunity(CBaseLesson *pLesson)
{
    if (!IsEnabled())
    {
        delete pLesson;
        return false;
    }

    // Get the root lesson
    CBaseLesson *pRootLesson = pLesson->GetRoot();

    if (!pRootLesson)
    {
        if (gameinstructor_verbose.GetInt() > 0)
        {
            ConColorMsg(CBaseLesson::m_rgbaVerboseHeader, "[INSTRUCTOR]: ");
            ConColorMsg(CBaseLesson::m_rgbaVerbosePlain, "Opportunity ");
            ConColorMsg(CBaseLesson::m_rgbaVerboseClose, "\"%s\" ", pLesson->GetName());
            ConColorMsg(CBaseLesson::m_rgbaVerbosePlain, "NOT opened (because root lesson could not be found).\n");
        }

        delete pLesson;
        return false;
    }

    C_BasePlayer *pLocalPlayer = GetLocalPlayer();

    if (!pRootLesson->CanOpenWhenDead() && (!pLocalPlayer || !pLocalPlayer->IsAlive()))
    {
        // If the player is dead don't allow lessons that can't be opened when dead
        if (gameinstructor_verbose.GetInt() > 0)
        {
            ConColorMsg(CBaseLesson::m_rgbaVerboseHeader, "[INSTRUCTOR]: ");
            ConColorMsg(CBaseLesson::m_rgbaVerbosePlain, "Opportunity ");
            ConColorMsg(CBaseLesson::m_rgbaVerboseClose, "\"%s\" ", pLesson->GetName());
            ConColorMsg(CBaseLesson::m_rgbaVerbosePlain,
                        "NOT opened (because player is dead and can_open_when_dead not set).\n");
        }

        delete pLesson;
        return false;
    }

    if (!pRootLesson->PrerequisitesHaveBeenMet())
    {
        // If the prereqs haven't been met, don't open it
        if (gameinstructor_verbose.GetInt() > 0)
        {
            ConColorMsg(CBaseLesson::m_rgbaVerboseHeader, "[INSTRUCTOR]: ");
            ConColorMsg(CBaseLesson::m_rgbaVerbosePlain, "Opportunity ");
            ConColorMsg(CBaseLesson::m_rgbaVerboseClose, "\"%s\" ", pLesson->GetName());
            ConColorMsg(CBaseLesson::m_rgbaVerbosePlain, "NOT opened (because prereqs haven't been met).\n");
        }

        delete pLesson;
        return false;
    }

    if (pRootLesson->InstanceType() == LESSON_INSTANCE_FIXED_REPLACE)
    {
        CBaseLesson *pLessonToReplace = NULL;
        CBaseLesson *pLastReplacableLesson = NULL;

        int iInstanceCount = 0;

        // Check how many are already open
        for (int i = m_OpenOpportunities.Count() - 1; i >= 0; --i)
        {
            CBaseLesson *pOpenOpportunity = m_OpenOpportunities[i];

            if (pOpenOpportunity->GetNameSymbol() == pLesson->GetNameSymbol() &&
                pOpenOpportunity->GetReplaceKeySymbol() == pLesson->GetReplaceKeySymbol())
            {
                iInstanceCount++;

                if (pRootLesson->ShouldReplaceOnlyWhenStopped())
                {
                    if (!pOpenOpportunity->IsInstructing())
                    {
                        pLastReplacableLesson = pOpenOpportunity;
                    }
                }
                else
                {
                    pLastReplacableLesson = pOpenOpportunity;
                }

                if (iInstanceCount >= pRootLesson->GetFixedInstancesMax())
                {
                    pLessonToReplace = pLastReplacableLesson;
                    break;
                }
            }
        }

        if (pLessonToReplace)
        {
            // Take the place of the previous instance
            if (gameinstructor_verbose.GetInt() > 0)
            {
                ConColorMsg(CBaseLesson::m_rgbaVerboseHeader, "GAME INSTRUCTOR: ");
                ConColorMsg(CBaseLesson::m_rgbaVerbosePlain, "Opportunity ");
                ConColorMsg(CBaseLesson::m_rgbaVerboseOpen, "\"%s\" ", pLesson->GetName());
                ConColorMsg(CBaseLesson::m_rgbaVerbosePlain, "replacing open lesson of same type.\n");
            }

            pLesson->TakePlaceOf(pLessonToReplace);
            CloseOpportunity(pLessonToReplace);
        }
        else if (iInstanceCount >= pRootLesson->GetFixedInstancesMax())
        {
            // Don't add another lesson of this type
            if (gameinstructor_verbose.GetInt() > 0)
            {
                ConColorMsg(CBaseLesson::m_rgbaVerboseHeader, "GAME INSTRUCTOR: ");
                ConColorMsg(CBaseLesson::m_rgbaVerbosePlain, "Opportunity ");
                ConColorMsg(CBaseLesson::m_rgbaVerboseClose, "\"%s\" ", pLesson->GetName());
                ConColorMsg(CBaseLesson::m_rgbaVerbosePlain,
                            "NOT opened (there is too many started lessons of this type).\n");
            }

            delete pLesson;
            return false;
        }
    }

    if (gameinstructor_verbose.GetInt() > 0)
    {
        ConColorMsg(CBaseLesson::m_rgbaVerboseHeader, "GAME INSTRUCTOR: ");
        ConColorMsg(CBaseLesson::m_rgbaVerbosePlain, "Opportunity ");
        ConColorMsg(CBaseLesson::m_rgbaVerboseOpen, "\"%s\" ", pLesson->GetName());
        ConColorMsg(CBaseLesson::m_rgbaVerbosePlain, "opened.\n");
    }

    m_OpenOpportunities.AddToTail(pLesson);

    return true;
}

//=========================================================
//=========================================================
void C_GameInstructor::DumpOpenOpportunities()
{
    ConColorMsg(CBaseLesson::m_rgbaVerboseHeader, "GAME INSTRUCTOR: ");
    ConColorMsg(CBaseLesson::m_rgbaVerbosePlain, "Open lessons...\n");

    for (int i = m_OpenOpportunities.Count() - 1; i >= 0; --i)
    {
        CBaseLesson *pLesson = m_OpenOpportunities[i];
        CBaseLesson *pRootLesson = pLesson->GetRoot();

        Color color;

        if (pLesson->IsInstructing())
        {
            // Green
            color = CBaseLesson::m_rgbaVerboseOpen;
        }
        else if (pRootLesson->IsLearned() && pLesson->GetPriority() >= m_iCurrentPriority)
        {
            // Yellow
            color = CBaseLesson::m_rgbaVerboseSuccess;
        }
        else
        {
            // Red
            color = CBaseLesson::m_rgbaVerboseClose;
        }

        ConColorMsg(color, "\t%s\n", pLesson->GetName());
    }
}

//=========================================================
//=========================================================
KeyValues *C_GameInstructor::GetScriptKeys()
{
    return m_pScriptKeys;
}

//=========================================================
//=========================================================
C_BasePlayer *C_GameInstructor::GetLocalPlayer()
{
    C_BasePlayer *pLocalPlayer = C_BasePlayer::GetLocalPlayer();

    // If we're not a developer, don't do the special spectator hook ups
    if (!developer.GetBool())
        return pLocalPlayer;

    // If there is no local player and we're not spectating, just return that
    if (!pLocalPlayer || pLocalPlayer->GetTeamNumber() != TEAM_SPECTATOR)
        return pLocalPlayer;

    // We're purely a spectator let's get lessons of the person we're spectating
    C_BasePlayer *pSpectatedPlayer = NULL;

    if (pLocalPlayer->GetObserverMode() == OBS_MODE_IN_EYE || pLocalPlayer->GetObserverMode() == OBS_MODE_CHASE)
        pSpectatedPlayer = ToBasePlayer(pLocalPlayer->GetObserverTarget());

    if (m_hLastSpectatedPlayer != pSpectatedPlayer)
    {
        // We're spectating someone new! Close all the stale lessons!
        m_bSpectatedPlayerChanged = true;
        m_hLastSpectatedPlayer = pSpectatedPlayer;
    }

    return pSpectatedPlayer;
}

//=========================================================
//=========================================================
void C_GameInstructor::EvaluateLessonsForGameRules()
{
    // Enable everything by default
    for (int i = 0; i < m_Lessons.Count(); ++i)
        m_Lessons[i]->SetEnabled(true);

    // Then see if we should disable anything
    for (int nConVar = 0; nConVar < m_LessonGroupConVarToggles.Count(); ++nConVar)
    {
        LessonGroupConVarToggle_t *pLessonGroupConVarToggle = &(m_LessonGroupConVarToggles[nConVar]);

        if (pLessonGroupConVarToggle->var.IsValid())
        {
            if (pLessonGroupConVarToggle->var.GetBool())
                SetLessonGroupEnabled(pLessonGroupConVarToggle->szLessonGroupName, false);
        }
    }
}

//=========================================================
//=========================================================
void C_GameInstructor::SetLessonGroupEnabled(const char *pszGroup, bool bEnabled)
{
    for (int i = 0; i < m_Lessons.Count(); ++i)
    {
        if (!Q_stricmp(pszGroup, m_Lessons[i]->GetGroup()))
            m_Lessons[i]->SetEnabled(bEnabled);
    }
}

//=========================================================
//=========================================================
void C_GameInstructor::FindErrors()
{
    // Loop through all the lesson and run all their scripted actions
    for (int i = 0; i < m_Lessons.Count(); ++i)
    {
        CScriptedIconLesson *pLesson = dynamic_cast<CScriptedIconLesson *>(m_Lessons[i]);
        if (pLesson)
        {
            // Process all open events
            for (int iLessonEvent = 0; iLessonEvent < pLesson->GetOpenEvents().Count(); ++iLessonEvent)
            {
                const LessonEvent_t *pLessonEvent = &(pLesson->GetOpenEvents()[iLessonEvent]);
                pLesson->ProcessElements(NULL, &(pLessonEvent->elements));
            }

            // Process all close events
            for (int iLessonEvent = 0; iLessonEvent < pLesson->GetCloseEvents().Count(); ++iLessonEvent)
            {
                const LessonEvent_t *pLessonEvent = &(pLesson->GetCloseEvents()[iLessonEvent]);
                pLesson->ProcessElements(NULL, &(pLessonEvent->elements));
            }

            // Process all success events
            for (int iLessonEvent = 0; iLessonEvent < pLesson->GetSuccessEvents().Count(); ++iLessonEvent)
            {
                const LessonEvent_t *pLessonEvent = &(pLesson->GetSuccessEvents()[iLessonEvent]);
                pLesson->ProcessElements(NULL, &(pLessonEvent->elements));
            }

            // Process all on open events
            for (int iLessonEvent = 0; iLessonEvent < pLesson->GetOnOpenEvents().Count(); ++iLessonEvent)
            {
                const LessonEvent_t *pLessonEvent = &(pLesson->GetOnOpenEvents()[iLessonEvent]);
                pLesson->ProcessElements(NULL, &(pLessonEvent->elements));
            }

            // Process all update events
            for (int iLessonEvent = 0; iLessonEvent < pLesson->GetUpdateEvents().Count(); ++iLessonEvent)
            {
                const LessonEvent_t *pLessonEvent = &(pLesson->GetUpdateEvents()[iLessonEvent]);
                pLesson->ProcessElements(NULL, &(pLessonEvent->elements));
            }
        }
    }
}

//=========================================================
//=========================================================
bool C_GameInstructor::UpdateActiveLesson(CBaseLesson *pLesson, const CBaseLesson *pRootLesson)
{
    VPROF_BUDGET("C_GameInstructor::UpdateActiveLesson", "GameInstructor");

    bool bIsOpen = pLesson->IsInstructing();

    if (!bIsOpen && !pRootLesson->IsLearned())
    {
        pLesson->SetStartTime();
        pLesson->Start();

        // Check to see if it successfully started
        bIsOpen = (pLesson->IsOpenOpportunity() && pLesson->ShouldDisplay());

        if (bIsOpen)
        {
            // Lesson hasn't been started and hasn't been learned
            if (gameinstructor_verbose.GetInt() > 0)
            {
                ConColorMsg(CBaseLesson::m_rgbaVerboseHeader, "GAME INSTRUCTOR: ");
                ConColorMsg(CBaseLesson::m_rgbaVerbosePlain, "Started lesson ");
                ConColorMsg(CBaseLesson::m_rgbaVerboseOpen, "\"%s\"", pLesson->GetName());
                ConColorMsg(CBaseLesson::m_rgbaVerbosePlain, ".\n");
            }
        }
        else
        {
            pLesson->Stop();
            pLesson->ResetStartTime();
        }
    }

    if (bIsOpen)
    {
        // Update the running lesson
        pLesson->Update();
        return true;
    }
    else
    {
        pLesson->UpdateInactive();
        return false;
    }
}

//=========================================================
//=========================================================
void C_GameInstructor::UpdateInactiveLesson(CBaseLesson *pLesson)
{
    VPROF_BUDGET("C_GameInstructor::UpdateInactiveLesson", "GameInstructor");

    if (pLesson->IsInstructing())
    {
        // Lesson hasn't been stopped
        if (gameinstructor_verbose.GetInt() > 0)
        {
            ConColorMsg(CBaseLesson::m_rgbaVerboseHeader, "GAME INSTRUCTOR: ");
            ConColorMsg(CBaseLesson::m_rgbaVerbosePlain, "Stopped lesson ");
            ConColorMsg(CBaseLesson::m_rgbaVerboseClose, "\"%s\"", pLesson->GetName());
            ConColorMsg(CBaseLesson::m_rgbaVerbosePlain, ".\n");
        }

        pLesson->Stop();
        pLesson->ResetStartTime();
    }

    pLesson->UpdateInactive();
}

//=========================================================
//=========================================================
CBaseLesson *C_GameInstructor::GetLesson_Internal(const char *pchLessonName)
{
    for (int i = 0; i < m_Lessons.Count(); ++i)
    {
        CBaseLesson *pLesson = m_Lessons[i];

        if (Q_strcmp(pLesson->GetName(), pchLessonName) == 0)
        {
            return pLesson;
        }
    }

    return NULL;
}

//=========================================================
//=========================================================
void C_GameInstructor::StopAllLessons()
{
    // Stop all the current lessons
    for (int i = m_OpenOpportunities.Count() - 1; i >= 0; --i)
    {
        CBaseLesson *pLesson = m_OpenOpportunities[i];
        UpdateInactiveLesson(pLesson);
    }
}

//=========================================================
//=========================================================
void C_GameInstructor::CloseAllOpenOpportunities()
{
    // Clear out all the open opportunities
    for (int i = m_OpenOpportunities.Count() - 1; i >= 0; --i)
    {
        CBaseLesson *pLesson = m_OpenOpportunities[i];
        CloseOpportunity(pLesson);
    }

    Assert(m_OpenOpportunities.Count() == 0);
}

//=========================================================
//=========================================================
void C_GameInstructor::CloseOpportunity(CBaseLesson *pLesson)
{
    UpdateInactiveLesson(pLesson);

    if (pLesson->WasDisplayed())
        MarkDisplayed(pLesson->GetName());

    if (gameinstructor_verbose.GetInt() > 0)
    {
        ConColorMsg(CBaseLesson::m_rgbaVerboseHeader, "GAME INSTRUCTOR: ");
        ConColorMsg(CBaseLesson::m_rgbaVerbosePlain, "Opportunity ");
        ConColorMsg(CBaseLesson::m_rgbaVerboseClose, "\"%s\" ", pLesson->GetName());
        ConColorMsg(CBaseLesson::m_rgbaVerbosePlain, "closed for reason: ");
        ConColorMsg(CBaseLesson::m_rgbaVerboseClose, "%s\n", pLesson->GetCloseReason());
    }

    pLesson->StopListeningForAllEvents();

    m_OpenOpportunities.FindAndRemove(pLesson);
    delete pLesson;
}

//=========================================================
//=========================================================
void C_GameInstructor::ReadLessonsFromFile(const char *pchFileName)
{
    // Static init function
    CScriptedIconLesson::PreReadLessonsFromFile();
    MEM_ALLOC_CREDIT();

    KeyValues *pLessonKeys = new KeyValues("instructor_lessons");
    KeyValues::AutoDelete autoDelete(pLessonKeys);

    pLessonKeys->LoadFromFile(g_pFullFileSystem, pchFileName, NULL);

    for (m_pScriptKeys = pLessonKeys->GetFirstTrueSubKey(); m_pScriptKeys;
         m_pScriptKeys = m_pScriptKeys->GetNextTrueSubKey())
    {
        if (Q_stricmp(m_pScriptKeys->GetName(), "GroupConVarToggle") == 0)
        {
            // Add convar group toggler to the list
            int nLessonGroupConVarToggle =
                m_LessonGroupConVarToggles.AddToTail(LessonGroupConVarToggle_t(m_pScriptKeys->GetString("convar")));
            LessonGroupConVarToggle_t *pLessonGroupConVarToggle =
                &(m_LessonGroupConVarToggles[nLessonGroupConVarToggle]);

            Q_strncpy(pLessonGroupConVarToggle->szLessonGroupName, m_pScriptKeys->GetString("group"),
                      sizeof(pLessonGroupConVarToggle->szLessonGroupName));
            continue;
        }

        // Ensure that lessons aren't added twice
        if (GetLesson_Internal(m_pScriptKeys->GetName()))
        {
            DevWarning("Lesson \"%s\" defined twice!\n", m_pScriptKeys->GetName());
            continue;
        }

        CScriptedIconLesson *pNewLesson = new CScriptedIconLesson(m_pScriptKeys->GetName(), false, false);
        GetGameInstructor().DefineLesson(pNewLesson);
    }

    m_pScriptKeys = NULL;
}

//=========================================================
//=========================================================
void C_GameInstructor::InitLessonPrerequisites()
{
    for (int i = 0; i < m_Lessons.Count(); ++i)
        m_Lessons[i]->InitPrerequisites();
}

//=========================================================
// Comandos
//=========================================================

CON_COMMAND_F(gameinstructor_reload_lessons, "Shuts down all open lessons and reloads them from the script file.",
              FCVAR_CHEAT)
{
    GetGameInstructor().Shutdown();
    GetGameInstructor().Init();
}

CON_COMMAND_F(gameinstructor_reset_counts, "Resets all display and success counts to zero.", FCVAR_NONE)
{
    GetGameInstructor().ResetDisplaysAndSuccesses();
}

CON_COMMAND_F(gameinstructor_dump_open_lessons, "Gives a list of all currently open lessons.", FCVAR_CHEAT)
{
    GetGameInstructor().DumpOpenOpportunities();
}
