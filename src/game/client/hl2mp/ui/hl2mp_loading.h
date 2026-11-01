#ifndef HL2MP_LOADING_H
#define HL2MP_LOADING_H
#ifdef _WIN32
#pragma once
#endif

class IGameUI;
void HL2MPLoading_Create( IGameUI *gameUI );
void HL2MPLoading_Destroy();
bool HL2MPLoading_IsVisible();
// Hints are consumed by the next load; the actual level name supersedes them.
void HL2MPLoading_SetContext( const char *map, const char *mode = "", const char *server = "" );
void HL2MPLoading_LevelInit( const char *map );

#endif
