//=============================================================================
// Copyright (c) 2026 Radical Games Ltd. / Simpsons Android Port
//=============================================================================
//
// File:        gamelog.cpp
//
// Subsystem:   Debug Services / Execution Logging
//
// Description: Implementation of the centralized execution logger for the game.
//
//=============================================================================

#include <debug/gamelog.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdarg>
#include <ctime>
#include <string>
#include <vector>
#include <mutex>
#include <sys/stat.h>
#include <unistd.h>

#if defined(RAD_ANDROID)
    #include <android/log.h>
    #include <SDL.h>
#elif defined(RAD_WIN32)
    #include <SDL.h>
#endif

namespace
{
    static bool s_enabled = false;
    static bool s_initialized = false;
    static bool s_hasSaved = false;
    static std::mutex s_mutex;
    static std::vector<std::string> s_entries;
    static const size_t MAX_LOG_ENTRIES = 60000;
    static char s_savedPath[ 512 ] = { 0 };
    static time_t s_startTime = 0;

    void FormatTimestamp( char* outBuffer, size_t bufferSize )
    {
        time_t rawtime;
        time( &rawtime );
        struct tm* timeinfo = localtime( &rawtime );

        if( timeinfo != NULL )
        {
            strftime( outBuffer, bufferSize, "%H:%M:%S", timeinfo );
        }
        else
        {
            snprintf( outBuffer, bufferSize, "00:00:00" );
        }
    }
}

void GameLog::Initialize()
{
    std::lock_guard<std::mutex> lock( s_mutex );
    if( s_initialized )
    {
        return;
    }

    s_enabled = false; // Off by default as required
    s_hasSaved = false;
    s_savedPath[ 0 ] = '\0';
    time( &s_startTime );

    s_entries.reserve( 2048 );

    char timeStr[ 64 ];
    FormatTimestamp( timeStr, sizeof( timeStr ) );

    char initMsg[ 256 ];
    snprintf( initMsg, sizeof( initMsg ), "[%s] [GameLog] Initialized execution log system (default: Off)", timeStr );
    s_entries.push_back( initMsg );

    s_initialized = true;

    atexit( &GameLog::Shutdown );
}

void GameLog::Shutdown()
{
    SaveToFile();
}

bool GameLog::IsEnabled()
{
    std::lock_guard<std::mutex> lock( s_mutex );
    return s_enabled;
}

void GameLog::SetEnabled( bool enable )
{
    std::lock_guard<std::mutex> lock( s_mutex );
    s_enabled = enable;

    char timeStr[ 64 ];
    FormatTimestamp( timeStr, sizeof( timeStr ) );

    char statusMsg[ 256 ];
    snprintf( statusMsg, sizeof( statusMsg ), "[%s] [GameLog] Gamelog option changed to %s by user",
              timeStr, s_enabled ? "ON" : "OFF" );
    s_entries.push_back( statusMsg );

#if defined(RAD_ANDROID)
    __android_log_print( ANDROID_LOG_INFO, "SimpsonsHitAndRun", "%s", statusMsg );
#endif
}

bool GameLog::Toggle()
{
    std::lock_guard<std::mutex> lock( s_mutex );
    s_enabled = !s_enabled;

    char timeStr[ 64 ];
    FormatTimestamp( timeStr, sizeof( timeStr ) );

    char statusMsg[ 256 ];
    snprintf( statusMsg, sizeof( statusMsg ), "[%s] [GameLog] Gamelog option toggled to %s by user",
              timeStr, s_enabled ? "ON" : "OFF" );
    s_entries.push_back( statusMsg );

#if defined(RAD_ANDROID)
    __android_log_print( ANDROID_LOG_INFO, "SimpsonsHitAndRun", "%s", statusMsg );
#endif

    return s_enabled;
}

void GameLog::Log( const char* message )
{
    LogTagged( "Game", message );
}

void GameLog::LogTagged( const char* tag, const char* message )
{
    if( message == NULL || message[ 0 ] == '\0' )
    {
        return;
    }

    std::lock_guard<std::mutex> lock( s_mutex );

    if( !s_initialized )
    {
        time( &s_startTime );
        s_initialized = true;
    }

    char timeStr[ 64 ];
    FormatTimestamp( timeStr, sizeof( timeStr ) );

    // Trim trailing newline if present for uniform formatting
    size_t msgLen = strlen( message );
    while( msgLen > 0 && ( message[ msgLen - 1 ] == '\n' || message[ msgLen - 1 ] == '\r' ) )
    {
        msgLen--;
    }

    if( msgLen == 0 )
    {
        return;
    }

    std::string line;
    line.reserve( 32 + ( tag ? strlen( tag ) : 4 ) + msgLen );
    line += "[";
    line += timeStr;
    line += "] [";
    line += ( tag && tag[ 0 ] != '\0' ) ? tag : "Game";
    line += "] ";
    line.append( message, msgLen );

    if( s_entries.size() >= MAX_LOG_ENTRIES )
    {
        // Drop oldest entries if buffer limit is reached
        s_entries.erase( s_entries.begin(), s_entries.begin() + 1000 );
    }

    s_entries.push_back( line );
}

void GameLog::Logf( const char* tag, const char* fmt, ... )
{
    if( fmt == NULL )
    {
        return;
    }

    char buffer[ 2048 ];
    va_list args;
    va_start( args, fmt );
    vsnprintf( buffer, sizeof( buffer ), fmt, args );
    va_end( args );

    LogTagged( tag, buffer );
}

void GameLog::Flush()
{
    // Save current buffer if enabled
    if( IsEnabled() )
    {
        SaveToFile();
    }
}

const char* GameLog::GetSavedPath()
{
    std::lock_guard<std::mutex> lock( s_mutex );
    return s_savedPath;
}

void GameLog::SaveToFile()
{
    std::lock_guard<std::mutex> lock( s_mutex );

    if( !s_enabled )
    {
        return;
    }

    if( s_hasSaved )
    {
        return;
    }

    std::vector<std::string> targetPaths;

#if defined(RAD_ANDROID)
    targetPaths.push_back( "/storage/emulated/0/Simpsons Hit and Run/gamelog.txt" );
    targetPaths.push_back( "/storage/emulated/0/Simpsons Hit and Run/files/gamelog.txt" );

    const char* extBase = SDL_AndroidGetExternalStoragePath();
    if( extBase != NULL && extBase[ 0 ] != '\0' )
    {
        char p[ 512 ];
        snprintf( p, sizeof( p ), "%s/gamelog.txt", extBase );
        targetPaths.push_back( p );
    }

    const char* intBase = SDL_AndroidGetInternalStoragePath();
    if( intBase != NULL && intBase[ 0 ] != '\0' )
    {
        char p[ 512 ];
        snprintf( p, sizeof( p ), "%s/gamelog.txt", intBase );
        targetPaths.push_back( p );
    }
#endif

    targetPaths.push_back( "gamelog.txt" );

    FILE* fp = NULL;
    std::string chosenPath;

    for( size_t i = 0; i < targetPaths.size(); i++ )
    {
        fp = fopen( targetPaths[ i ].c_str(), "w" );
        if( fp != NULL )
        {
            chosenPath = targetPaths[ i ];
            break;
        }
    }

    if( fp == NULL )
    {
#if defined(RAD_ANDROID)
        __android_log_print( ANDROID_LOG_ERROR, "SimpsonsHitAndRun",
                             "[GameLog] ERROR: Failed to open gamelog.txt for writing in any path!" );
#endif
        return;
    }

    strncpy( s_savedPath, chosenPath.c_str(), sizeof( s_savedPath ) - 1 );
    s_savedPath[ sizeof( s_savedPath ) - 1 ] = '\0';

    time_t rawEndTime;
    time( &rawEndTime );
    struct tm* startInfo = localtime( &s_startTime );
    char startStr[ 64 ] = "N/A";
    if( startInfo != NULL )
    {
        strftime( startStr, sizeof( startStr ), "%Y-%m-%d %H:%M:%S", startInfo );
    }

    struct tm* endInfo = localtime( &rawEndTime );
    char endStr[ 64 ] = "N/A";
    if( endInfo != NULL )
    {
        strftime( endStr, sizeof( endStr ), "%Y-%m-%d %H:%M:%S", endInfo );
    }

    fprintf( fp, "================================================================================\n" );
    fprintf( fp, "The Simpsons: Hit & Run - Android Game Log\n" );
    fprintf( fp, "Saved file: %s\n", chosenPath.c_str() );
    fprintf( fp, "Session started: %s\n", startStr );
    fprintf( fp, "Session ended:   %s\n", endStr );
    fprintf( fp, "Total entries:   %zu\n", s_entries.size() );
    fprintf( fp, "================================================================================\n\n" );

    for( size_t i = 0; i < s_entries.size(); i++ )
    {
        fprintf( fp, "%s\n", s_entries[ i ].c_str() );
    }

#if defined(RAD_ANDROID)
    // Attempt to gather system logcat diagnostics for this session if accessible
    FILE* pipe = popen( "logcat -d -v time -s SimpsonsHitAndRun:V SDL:V DEBUG:V AndroidRuntime:E 2>/dev/null", "r" );
    if( pipe != NULL )
    {
        fprintf( fp, "\n================================================================================\n" );
        fprintf( fp, "System & Logcat Diagnostics Capture\n" );
        fprintf( fp, "================================================================================\n" );

        char buffer[ 512 ];
        size_t logcatLines = 0;
        while( fgets( buffer, sizeof( buffer ), pipe ) != NULL && logcatLines < 5000 )
        {
            fputs( buffer, fp );
            logcatLines++;
        }
        pclose( pipe );
    }
#endif

    fprintf( fp, "\n================================================================================\n" );
    fprintf( fp, "End of Game Log (Clean Shutdown)\n" );
    fprintf( fp, "================================================================================\n" );

    fflush( fp );
    fsync( fileno( fp ) );
    fclose( fp );

    s_hasSaved = true;

#if defined(RAD_ANDROID)
    __android_log_print( ANDROID_LOG_INFO, "SimpsonsHitAndRun",
                         "[GameLog] Saved complete game log (%zu entries) to '%s'",
                         s_entries.size(), chosenPath.c_str() );
#endif
}

extern "C" void GameLog_Record( const char* tag, const char* message )
{
    GameLog::LogTagged( tag, message );
}

extern "C" void GameLog_Recordf( const char* tag, const char* fmt, ... )
{
    if( fmt == NULL )
    {
        return;
    }

    char buffer[ 2048 ];
    va_list args;
    va_start( args, fmt );
    vsnprintf( buffer, sizeof( buffer ), fmt, args );
    va_end( args );

    GameLog::LogTagged( tag, buffer );
}
