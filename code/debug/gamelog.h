//=============================================================================
// Copyright (c) 2026 Radical Games Ltd. / Simpsons Android Port
//=============================================================================
//
// File:        gamelog.h
//
// Subsystem:   Debug Services / Execution Logging
//
// Description: Centralized in-memory and on-disk execution logger for the game.
//              Records game operation logs and outputs gamelog.txt on exit when
//              enabled from Options -> Gamelog.
//
//=============================================================================

#ifndef GAMELOG_H
#define GAMELOG_H

#include <stddef.h>

class GameLog
{
public:
    static void Initialize();
    static void Shutdown();

    static bool IsEnabled();
    static void SetEnabled( bool enable );
    static bool Toggle();

    static void Log( const char* message );
    static void LogTagged( const char* tag, const char* message );
    static void Logf( const char* tag, const char* fmt, ... );

    static void SaveToFile();
    static void Flush();

    static const char* GetSavedPath();

private:
    GameLog();
    ~GameLog();
};

#ifdef __cplusplus
extern "C" {
#endif

void GameLog_Record( const char* tag, const char* message );
void GameLog_Recordf( const char* tag, const char* fmt, ... );

#ifdef __cplusplus
}
#endif

#endif // GAMELOG_H
