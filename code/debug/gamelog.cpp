//=============================================================================
// Copyright (c) 2026 Radical Games Ltd. / Simpsons Android Port
//=============================================================================
//
// File:        gamelog.cpp
//
// Subsystem:   Debug Services / Execution Logging
//
// Description: Centralized in-memory and on-disk execution logger for the game.
//              Records detailed game operation logs, subsystem diagnostics and
//              crash traces to gamelog.txt when enabled from Options -> Gamelog.
//
//=============================================================================

#include "gamelog.h"

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <vector>
#include <string>
#include <mutex>
#include <signal.h>
#include <unistd.h>
#include <sys/stat.h>

#if defined(RAD_ANDROID)
    #include <android/log.h>
    #include <SDL.h>
    #include <jni.h>
#endif

namespace
{
    const size_t MAX_LOG_ENTRIES = 60000;

    std::mutex s_mutex;
    bool s_initialized = false;
    bool s_enabled = false;
    time_t s_startTime = 0;
    std::vector<std::string> s_entries;
    std::vector<FILE*> s_openFiles;
    char s_savedPath[ 512 ] = { 0 };
    char s_activeModName[ 256 ] = "None";
    char s_activeModsDir[ 512 ] = "/storage/emulated/0/Simpsons Hit and Run/mods";

    void EnsureDirectoryExists( const std::string& path )
    {
        if( path.empty() || path == "." ) return;
        char temp[ 512 ];
        strncpy( temp, path.c_str(), sizeof( temp ) - 1 );
        temp[ sizeof( temp ) - 1 ] = '\0';
        for( char* p = temp + 1; *p != '\0'; p++ )
        {
            if( *p == '/' )
            {
                *p = '\0';
                mkdir( temp, 0777 );
                *p = '/';
            }
        }
        mkdir( temp, 0777 );
    }

    void FormatTimestamp( char* buffer, size_t bufferSize )
    {
        time_t rawTime;
        time( &rawTime );
        struct tm* timeInfo = localtime( &rawTime );
        if( timeInfo != NULL )
        {
            strftime( buffer, bufferSize, "%H:%M:%S", timeInfo );
        }
        else
        {
            snprintf( buffer, bufferSize, "00:00:00" );
        }
    }

    const char* ClassifyLogLevel( const char* tag, const char* message )
    {
        if( tag != NULL )
        {
            if( strcasecmp( tag, "ERROR" ) == 0 ) return "ERROR ";
            if( strcasecmp( tag, "FATAL" ) == 0 || strcasecmp( tag, "CRASH" ) == 0 ) return "FATAL ";
            if( strcasecmp( tag, "ASSERT" ) == 0 ) return "ASSERT";
            if( strcasecmp( tag, "WARN" ) == 0 || strcasecmp( tag, "WARNING" ) == 0 ) return "WARN  ";
        }

        if( message != NULL )
        {
            if( strstr( message, "ASSERT FAILED" ) != NULL || strstr( message, "Assertion failed" ) != NULL )
            {
                return "ASSERT";
            }
            if( strstr( message, "FATAL" ) != NULL || strstr( message, "Fatal" ) != NULL )
            {
                return "FATAL ";
            }
            if( strstr( message, "ERROR" ) != NULL || strstr( message, "Error" ) != NULL || strstr( message, "FAIL" ) != NULL )
            {
                return "ERROR ";
            }
            if( strstr( message, "WARN" ) != NULL || strstr( message, "Warning" ) != NULL )
            {
                return "WARN  ";
            }
        }

        return "INFO  ";
    }

    void CloseOpenFilesInternal()
    {
        for( size_t i = 0; i < s_openFiles.size(); i++ )
        {
            if( s_openFiles[ i ] != NULL )
            {
                fflush( s_openFiles[ i ] );
                fsync( fileno( s_openFiles[ i ] ) );
                fclose( s_openFiles[ i ] );
            }
        }
        s_openFiles.clear();
    }

    void OpenLogFilesInternal()
    {
        CloseOpenFilesInternal();

        std::vector<std::string> targetDirs;

#if defined(RAD_ANDROID)
        // STRICT USER REQUIREMENT: Only "Simpsons Hit and Run" on shared internal storage
        targetDirs.push_back( "/storage/emulated/0/Simpsons Hit and Run" );
        targetDirs.push_back( "/storage/emulated/0/Simpsons Hit and Run/files" );
        targetDirs.push_back( "/sdcard/Simpsons Hit and Run" );

        const char* extBase = SDL_AndroidGetExternalStoragePath();
        if( extBase != NULL && extBase[ 0 ] != '\0' )
        {
            targetDirs.push_back( extBase );
        }
#endif
        targetDirs.push_back( "." );

        char startStr[ 64 ] = "N/A";
        struct tm* startInfo = localtime( &s_startTime );
        if( startInfo != NULL )
        {
            strftime( startStr, sizeof( startStr ), "%Y-%m-%d %H:%M:%S", startInfo );
        }

        const char* abi = "arm64-v8a / armeabi-v7a";
#if defined(__aarch64__)
        abi = "arm64-v8a (64-bit)";
#elif defined(__arm__)
        abi = "armeabi-v7a (32-bit)";
#elif defined(__x86_64__)
        abi = "x86_64";
#elif defined(__i386__)
        abi = "x86";
#endif

        for( size_t i = 0; i < targetDirs.size(); i++ )
        {
            EnsureDirectoryExists( targetDirs[ i ] );
            std::string filePath = targetDirs[ i ] + "/gamelog.txt";

            FILE* fp = fopen( filePath.c_str(), "w" );
            if( fp != NULL )
            {
                fprintf( fp, "================================================================================\n" );
                fprintf( fp, "The Simpsons: Hit & Run - Diagnostic Game Log\n" );
                fprintf( fp, "Target file:     %s\n", filePath.c_str() );
                fprintf( fp, "App Version:     v0.93.20 (versionCode 113)\n" );
                fprintf( fp, "Architecture:    %s\n", abi );
                fprintf( fp, "Session started: %s\n", startStr );
                fprintf( fp, "Active Mod:      %s\n", s_activeModName );
                fprintf( fp, "Mods Directory:  %s\n", s_activeModsDir );
                fprintf( fp, "================================================================================\n\n" );

                // Dump all prior entries
                for( size_t j = 0; j < s_entries.size(); j++ )
                {
                    fputs( s_entries[ j ].c_str(), fp );
                    fputc( '\n', fp );
                }

                fflush( fp );
                fsync( fileno( fp ) );

                s_openFiles.push_back( fp );

                if( s_savedPath[ 0 ] == '\0' )
                {
                    strncpy( s_savedPath, filePath.c_str(), sizeof( s_savedPath ) - 1 );
                }

#if defined(RAD_ANDROID)
                __android_log_print( ANDROID_LOG_INFO, "SimpsonsHitAndRun",
                                     "[GameLog] Active diagnostic log stream opened at: '%s'", filePath.c_str() );
#endif
            }
        }
    }

    void SignalHandler( int sig )
    {
        GameLog::Shutdown();
        _exit( 0 );
    }

    void FatalCrashHandler( int sig, siginfo_t* info, void* /*ucontext*/ )
    {
        const char* sigName = "UNKNOWN";
        switch( sig )
        {
            case SIGSEGV: sigName = "SIGSEGV (Segmentation Fault / Invalid Memory Access)"; break;
            case SIGABRT: sigName = "SIGABRT (Aborted / Assertion Failure)"; break;
            case SIGFPE:  sigName = "SIGFPE (Arithmetic Exception / Division by Zero)"; break;
            case SIGBUS:  sigName = "SIGBUS (Bus Error / Misaligned Memory Access)"; break;
            case SIGILL:  sigName = "SIGILL (Illegal Instruction)"; break;
            default:      sigName = "Fatal Signal"; break;
        }

        char crashHeader[ 512 ];
        snprintf( crashHeader, sizeof( crashHeader ),
                  "\n================================================================================\n"
                  "[FATAL CRASH DETECTED]\n"
                  "Signal:          %d (%s)\n"
                  "Fault Address:   %p\n"
                  "Description:     The game process encountered a critical fatal error and crashed.\n"
                  "================================================================================\n",
                  sig, sigName, info ? info->si_addr : NULL );

        // Write directly to open log streams
        for( size_t i = 0; i < s_openFiles.size(); i++ )
        {
            if( s_openFiles[ i ] != NULL )
            {
                fputs( crashHeader, s_openFiles[ i ] );
                fflush( s_openFiles[ i ] );
                fsync( fileno( s_openFiles[ i ] ) );
            }
        }

#if defined(RAD_ANDROID)
        __android_log_print( ANDROID_LOG_ERROR, "SimpsonsHitAndRun", "%s", crashHeader );
#endif

        // Reset signal to default and re-raise so system crash handler can take over
        signal( sig, SIG_DFL );
        raise( sig );
    }
}

void GameLog::Initialize()
{
    std::lock_guard<std::mutex> lock( s_mutex );

    if( s_initialized )
    {
        return;
    }

    time( &s_startTime );
    s_entries.reserve( 2048 );

    // Clean up old incorrect directory on internal storage if leftover
    rmdir( "/storage/emulated/0/Simpsons and Run/files" );
    rmdir( "/storage/emulated/0/Simpsons and Run/mods" );
    rmdir( "/storage/emulated/0/Simpsons and Run" );

    char timeStr[ 64 ];
    FormatTimestamp( timeStr, sizeof( timeStr ) );

    char initMsg[ 256 ];
    snprintf( initMsg, sizeof( initMsg ),
              "[%s] [INFO  ] [GameLog   ] Initialized diagnostic execution log (v0.93.20)", timeStr );
    s_entries.push_back( initMsg );

    s_initialized = true;

    // Normal termination signals
    struct sigaction sa;
    memset( &sa, 0, sizeof( sa ) );
    sa.sa_handler = &SignalHandler;
    sigaction( SIGTERM, &sa, NULL );
    sigaction( SIGINT, &sa, NULL );
    sigaction( SIGHUP, &sa, NULL );

    // Fatal crash interception
    struct sigaction saFatal;
    memset( &saFatal, 0, sizeof( saFatal ) );
    saFatal.sa_flags = SA_SIGINFO;
    saFatal.sa_sigaction = &FatalCrashHandler;
    sigaction( SIGSEGV, &saFatal, NULL );
    sigaction( SIGABRT, &saFatal, NULL );
    sigaction( SIGFPE,  &saFatal, NULL );
    sigaction( SIGBUS,  &saFatal, NULL );
    sigaction( SIGILL,  &saFatal, NULL );

    atexit( &GameLog::Shutdown );
}

void GameLog::SetActiveMod( const char* modName, const char* modsDir )
{
    std::lock_guard<std::mutex> lock( s_mutex );
    if( modName != NULL && modName[ 0 ] != '\0' )
    {
        strncpy( s_activeModName, modName, sizeof( s_activeModName ) - 1 );
        s_activeModName[ sizeof( s_activeModName ) - 1 ] = '\0';
    }
    if( modsDir != NULL && modsDir[ 0 ] != '\0' )
    {
        strncpy( s_activeModsDir, modsDir, sizeof( s_activeModsDir ) - 1 );
        s_activeModsDir[ sizeof( s_activeModsDir ) - 1 ] = '\0';
    }
}

void GameLog::Shutdown()
{
    std::lock_guard<std::mutex> lock( s_mutex );
    if( !s_enabled )
    {
        return;
    }

    char timeStr[ 64 ];
    FormatTimestamp( timeStr, sizeof( timeStr ) );

    for( size_t i = 0; i < s_openFiles.size(); i++ )
    {
        if( s_openFiles[ i ] != NULL )
        {
            fprintf( s_openFiles[ i ], "\n================================================================================\n" );
            fprintf( s_openFiles[ i ], "End of Game Log (Clean Session Termination at %s | Total entries: %zu)\n", timeStr, s_entries.size() );
            fprintf( s_openFiles[ i ], "================================================================================\n" );
            fflush( s_openFiles[ i ] );
            fsync( fileno( s_openFiles[ i ] ) );
            fclose( s_openFiles[ i ] );
        }
    }
    s_openFiles.clear();

#if defined(RAD_ANDROID)
    __android_log_print( ANDROID_LOG_INFO, "SimpsonsHitAndRun",
                         "[GameLog] Diagnostic gamelog.txt successfully closed and synced." );
#endif
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
    snprintf( statusMsg, sizeof( statusMsg ), "[%s] [INFO  ] [GameLog   ] Gamelog option changed to %s by user",
              timeStr, s_enabled ? "ON" : "OFF" );
    s_entries.push_back( statusMsg );

#if defined(RAD_ANDROID)
    __android_log_print( ANDROID_LOG_INFO, "SimpsonsHitAndRun", "%s", statusMsg );
#endif

    if( s_enabled )
    {
        OpenLogFilesInternal();
    }
    else
    {
        CloseOpenFilesInternal();
    }
}

bool GameLog::Toggle()
{
    std::lock_guard<std::mutex> lock( s_mutex );
    s_enabled = !s_enabled;

    char timeStr[ 64 ];
    FormatTimestamp( timeStr, sizeof( timeStr ) );

    char statusMsg[ 256 ];
    snprintf( statusMsg, sizeof( statusMsg ), "[%s] [INFO  ] [GameLog   ] Gamelog option toggled to %s by user",
              timeStr, s_enabled ? "ON" : "OFF" );
    s_entries.push_back( statusMsg );

#if defined(RAD_ANDROID)
    __android_log_print( ANDROID_LOG_INFO, "SimpsonsHitAndRun", "%s", statusMsg );
#endif

    if( s_enabled )
    {
        OpenLogFilesInternal();
    }
    else
    {
        CloseOpenFilesInternal();
    }

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

    const char* level = ClassifyLogLevel( tag, message );
    const char* safeTag = ( tag && tag[ 0 ] != '\0' ) ? tag : "Game";

    char lineHeader[ 128 ];
    snprintf( lineHeader, sizeof( lineHeader ), "[%s] [%-6s] [%-10s] ", timeStr, level, safeTag );

    std::string line;
    line.reserve( strlen( lineHeader ) + msgLen );
    line += lineHeader;
    line.append( message, msgLen );

    if( s_entries.size() >= MAX_LOG_ENTRIES )
    {
        // Drop oldest entries if buffer limit is reached
        s_entries.erase( s_entries.begin(), s_entries.begin() + 1000 );
    }

    s_entries.push_back( line );

    if( s_enabled )
    {
        if( s_openFiles.empty() )
        {
            OpenLogFilesInternal();
        }

        for( size_t i = 0; i < s_openFiles.size(); i++ )
        {
            if( s_openFiles[ i ] != NULL )
            {
                fputs( line.c_str(), s_openFiles[ i ] );
                fputc( '\n', s_openFiles[ i ] );
                fflush( s_openFiles[ i ] );
            }
        }
    }
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

void GameLog::SaveToFile()
{
    Flush();
}

void GameLog::Flush()
{
    std::lock_guard<std::mutex> lock( s_mutex );
    if( !s_enabled )
    {
        return;
    }

    if( s_openFiles.empty() )
    {
        OpenLogFilesInternal();
    }

    char timeStr[ 64 ];
    FormatTimestamp( timeStr, sizeof( timeStr ) );

    for( size_t i = 0; i < s_openFiles.size(); i++ )
    {
        if( s_openFiles[ i ] != NULL )
        {
            fprintf( s_openFiles[ i ], "[%s] [INFO  ] [GameLog   ] -- Log buffer synced to disk --\n", timeStr );
            fflush( s_openFiles[ i ] );
            fsync( fileno( s_openFiles[ i ] ) );
        }
    }
}

const char* GameLog::GetSavedPath()
{
    std::lock_guard<std::mutex> lock( s_mutex );
    return s_savedPath;
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

#if defined(RAD_ANDROID)
extern "C" JNIEXPORT void JNICALL
Java_com_c4rlox_simpsons_SimpsonsActivity_nativeFlushGameLog( JNIEnv* /*env*/, jclass /*clazz*/ )
{
    GameLog::Flush();
}

extern "C" JNIEXPORT void JNICALL
Java_org_libsdl_app_SDLActivity_nativeFlushGameLog( JNIEnv* /*env*/, jclass /*clazz*/ )
{
    GameLog::Flush();
}
#endif
