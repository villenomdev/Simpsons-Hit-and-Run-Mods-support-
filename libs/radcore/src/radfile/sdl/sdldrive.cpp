//=============================================================================
// Copyright (c) 2002 Radical Games Ltd.  All rights reserved.
//=============================================================================


//=============================================================================
//
// File:        sdldrive.cpp
//
// Subsystem:   Radical Drive System
//
// Description:	This file contains the implementation of the radSdlDrive class.
//
// Revisions:
//
// Notes:       We keep a serial number when the first file is opened. Then if the
//              media is removed, we don't allow ops until the original serial number
//              is detected, or all files are closed.
//=============================================================================

//=============================================================================
// Include Files
//=============================================================================

#include "pch.hpp"
#include <algorithm>
#include <limits.h>
#include "sdldrive.hpp"
#include <string>
#include <SDL.h>
#if SDL_MAJOR_VERSION < 3
#ifdef WIN32
#include <direct.h>
#else
#include <unistd.h>
#endif
#endif

#ifdef RAD_ANDROID
#include <android/log.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>     // strerror()
#include <sys/stat.h>   // stat()
#include <SDL_system.h>
#include <dirent.h>
#include <vector>
#include <strings.h>
#include <fnmatch.h>
#include <fstream>
#include <algorithm>
#include "modloader_lua.hpp"
#include "mod_archive.hpp"
#include "mod_manager.hpp"
#endif



#if defined(RAD_ANDROID)
    typedef void (*RadSdlDriveLogHookFn)( const char* tag, const char* message );
    static RadSdlDriveLogHookFn s_pRadSdlDriveLogHook = NULL;

    extern "C" void RadSdlDrive_SetLogHook( RadSdlDriveLogHookFn hook )
    {
        s_pRadSdlDriveLogHook = hook;
    }

    static void SdlDriveLog( const char* fmt, ... )
    {
        char buffer[ 1024 ];
        va_list args;
        va_start( args, fmt );
        vsnprintf( buffer, sizeof( buffer ), fmt, args );
        va_end( args );

        __android_log_print( ANDROID_LOG_INFO, "SimpsonsHitAndRun", "%s", buffer );
        if( s_pRadSdlDriveLogHook != NULL )
        {
            s_pRadSdlDriveLogHook( "Mods", buffer );
        }
    }
    #define LOGI(...) SdlDriveLog(__VA_ARGS__)
#endif

#ifdef RAD_ANDROID
static void LogPathStatus(const char* label, const char* path)
{
    struct stat st;
    int s = stat(path, &st);
    if (s == 0) {
        LOGI("%s: stat('%s') OK isDir=%d size=%lld",
             label, path, S_ISDIR(st.st_mode) ? 1 : 0, (long long)st.st_size);
    } else {
        LOGI("%s: stat('%s') FAIL errno=%d (%s)",
             label, path, errno, strerror(errno));
    }
}

//=============================================================================
// Android Mod Loader Subsystem & Fallback Paths
//=============================================================================
static bool s_ModsEnabled = true;
static char s_ModsDirectory[512] = "/storage/emulated/0/Simpsons Hit and Run/mods";
static char s_ActiveMod[256] = "";
static char s_MainGameDir[512] = "";
static char s_FallbackGameDir[512] = "";
static std::vector<std::string> s_DiscoveredModFoldersPrimary;
static std::vector<std::string> s_DiscoveredModFoldersFallback;
static bool s_PrimaryModFoldersScanned = false;
static bool s_FallbackModFoldersScanned = false;

struct ModPathRedirection
{
    std::string pattern;
    std::string target;
};

static std::vector<ModPathRedirection> s_ModPathRedirections;
static std::vector<std::string> s_ModOccludedPaths;
static bool s_ModIniParsed = false;
static int s_ModIniScanAttempts = 0;

static std::string Trim( const std::string& str )
{
    size_t first = str.find_first_not_of( " \t\r\n" );
    if( first == std::string::npos ) return "";
    size_t last = str.find_last_not_of( " \t\r\n" );
    return str.substr( first, ( last - first + 1 ) );
}

static std::string NormalizeModPath( const std::string& p )
{
    std::string res;
    for( size_t i = 0; i < p.size(); ++i )
    {
        if( p[i] == '\\' ) res.push_back( '/' );
        else res.push_back( p[i] );
    }
    while( !res.empty() && ( res[0] == '/' || res[0] == '.' ) )
    {
        res.erase( 0, 1 );
    }
    return res;
}

static void ParseCustomFilesIni( const char* iniPath )
{
    std::ifstream file( iniPath );
    if( !file.is_open() )
    {
        return;
    }

    std::string line;
    int currentSection = 0; // 1: PathRedirections, 2: Miscellaneous, 3: PathHandlers

    while( std::getline( file, line ) )
    {
        std::string trimmed = Trim( line );
        if( trimmed.empty() || trimmed[0] == ';' || trimmed[0] == '#' )
        {
            continue;
        }

        if( trimmed[0] == '[' && trimmed[trimmed.size() - 1] == ']' )
        {
            std::string sectionName = trimmed.substr( 1, trimmed.size() - 2 );
            if( strcasecmp( sectionName.c_str(), "PathRedirections" ) == 0 )
            {
                currentSection = 1;
            }
            else if( strcasecmp( sectionName.c_str(), "Miscellaneous" ) == 0 )
            {
                currentSection = 2;
            }
            else if( strcasecmp( sectionName.c_str(), "PathHandlers" ) == 0 )
            {
                currentSection = 3;
            }
            else
            {
                currentSection = 0;
            }
            continue;
        }

        if( currentSection == 1 ) // [PathRedirections]
        {
            size_t eqPos = trimmed.find( '=' );
            if( eqPos != std::string::npos )
            {
                std::string key = Trim( trimmed.substr( 0, eqPos ) );
                std::string val = Trim( trimmed.substr( eqPos + 1 ) );
                size_t cPos = val.find( ';' );
                if( cPos != std::string::npos ) val = Trim( val.substr( 0, cPos ) );
                cPos = val.find( '#' );
                if( cPos != std::string::npos ) val = Trim( val.substr( 0, cPos ) );

                if( !key.empty() && !val.empty() )
                {
                    ModPathRedirection red;
                    red.pattern = NormalizeModPath( key );
                    red.target = NormalizeModPath( val );
                    s_ModPathRedirections.push_back( red );
                    LOGI("[Mods] Loaded redirection: '%s' -> '%s'", red.pattern.c_str(), red.target.c_str());
                }
            }
        }
        else if( currentSection == 2 ) // [Miscellaneous]
        {
            size_t eqPos = trimmed.find( '=' );
            if( eqPos != std::string::npos )
            {
                std::string key = Trim( trimmed.substr( 0, eqPos ) );
                std::string val = Trim( trimmed.substr( eqPos + 1 ) );
                size_t cPos = val.find( ';' );
                if( cPos != std::string::npos ) val = Trim( val.substr( 0, cPos ) );
                cPos = val.find( '#' );
                if( cPos != std::string::npos ) val = Trim( val.substr( 0, cPos ) );

                if( strcasecmp( key.c_str(), "OccludedPath" ) == 0 && !val.empty() )
                {
                    std::string occ = NormalizeModPath( val );
                    s_ModOccludedPaths.push_back( occ );
                    LOGI("[Mods] Loaded occluded path: '%s'", occ.c_str());
                }
            }
        }
        else if( currentSection == 3 ) // [PathHandlers]
        {
            size_t eqPos = trimmed.find( '=' );
            if( eqPos != std::string::npos )
            {
                std::string key = Trim( trimmed.substr( 0, eqPos ) );
                std::string val = Trim( trimmed.substr( eqPos + 1 ) );
                size_t cPos = val.find( ';' );
                if( cPos != std::string::npos ) val = Trim( val.substr( 0, cPos ) );
                cPos = val.find( '#' );
                if( cPos != std::string::npos ) val = Trim( val.substr( 0, cPos ) );

                if( !key.empty() && !val.empty() )
                {
                    std::string pat = NormalizeModPath( key );
                    std::string script = NormalizeModPath( val );
                    ModLoaderLua::GetInstance().AddPathHandler( pat, script );
                    LOGI("[Mods] Loaded PathHandler: '%s' -> '%s'", pat.c_str(), script.c_str());
                }
            }
        }
    }
}

static bool CheckPathRedirection( const char* cleanFileName, std::string& outRedirected )
{
    std::string current = cleanFileName;
    bool anyRedirected = false;

    for( int hop = 0; hop < 8; ++hop )
    {
        std::string norm = NormalizeModPath( current );
        bool hopFound = false;
        std::string nextTarget;

        for( size_t i = 0; i < s_ModPathRedirections.size(); ++i )
        {
            const auto& r = s_ModPathRedirections[i];
            if( strcasecmp( norm.c_str(), r.pattern.c_str() ) == 0 )
            {
                nextTarget = r.target;
                hopFound = true;
                break;
            }
            if( r.pattern.find( '*' ) != std::string::npos || r.pattern.find( '?' ) != std::string::npos )
            {
                if( fnmatch( r.pattern.c_str(), norm.c_str(), FNM_CASEFOLD ) == 0 )
                {
                    size_t patStar = r.pattern.find( '*' );
                    size_t tgtStar = r.target.find( '*' );
                    if( tgtStar == std::string::npos || patStar == std::string::npos )
                    {
                        nextTarget = r.target;
                        hopFound = true;
                        break;
                    }
                    std::string prefix = r.pattern.substr( 0, patStar );
                    std::string suffix = r.pattern.substr( patStar + 1 );
                    if( norm.size() >= prefix.size() + suffix.size() )
                    {
                        std::string matched = norm.substr( prefix.size(), norm.size() - prefix.size() - suffix.size() );
                        nextTarget = r.target.substr( 0, tgtStar ) + matched + r.target.substr( tgtStar + 1 );
                        hopFound = true;
                        break;
                    }
                }
            }
        }

        if( hopFound && nextTarget != current )
        {
            current = nextTarget;
            anyRedirected = true;
        }
        else
        {
            break;
        }
    }

    if( anyRedirected )
    {
        outRedirected = current;
        return true;
    }
    return false;
}

static bool CheckModCandidate( const char* basePath, const char* subPath, const char* cleanFileName, char* outPath, size_t outSize );

static void ScanModFolderForIni( const char* modsDir, const char* folder )
{
    if( modsDir == NULL || modsDir[0] == '\0' ) return;
    char iniPath[1024];
    if( folder && folder[0] != '\0' )
    {
        if( CheckModCandidate( modsDir, folder, "CustomFiles.ini", iniPath, sizeof(iniPath) ) )
        {
            LOGI("[Mods] Found CustomFiles.ini: '%s'", iniPath);
            ParseCustomFilesIni( iniPath );
            return;
        }
        char nested[512];
        snprintf( nested, sizeof(nested), "%s/%s", folder, folder );
        if( CheckModCandidate( modsDir, nested, "CustomFiles.ini", iniPath, sizeof(iniPath) ) )
        {
            LOGI("[Mods] Found nested CustomFiles.ini: '%s'", iniPath);
            ParseCustomFilesIni( iniPath );
            return;
        }
    }
    else
    {
        if( CheckModCandidate( modsDir, NULL, "CustomFiles.ini", iniPath, sizeof(iniPath) ) )
        {
            LOGI("[Mods] Found root CustomFiles.ini: '%s'", iniPath);
            ParseCustomFilesIni( iniPath );
            return;
        }
    }
}

static void EnsureModIniLoaded()
{
    if( s_ModIniParsed || !s_ModsEnabled )
    {
        return;
    }
    s_ModIniParsed = true;

    if( s_ActiveMod[0] != '\0' && strcasecmp( s_ActiveMod, "none" ) != 0 )
    {
        const char* effDir = ModManager::GetInstance().GetEffectiveActiveModDir();
        if( effDir && effDir[0] != '\0' )
        {
            ScanModFolderForIni( effDir, NULL );
        }
        ScanModFolderForIni( s_ModsDirectory, s_ActiveMod );
    }
    else
    {
        ScanModFolderForIni( s_ModsDirectory, NULL );
    }
}

bool radSdlDriveGetRedirectedPath( const char* rawFileName, char* outRedirected, size_t outSize )
{
    if( !s_ModsEnabled || rawFileName == NULL || rawFileName[0] == '\0' || outRedirected == NULL || outSize == 0 )
    {
        return false;
    }

    EnsureModIniLoaded();

    if( s_ModPathRedirections.empty() )
    {
        return false;
    }

    std::string redirected;
    if( CheckPathRedirection( rawFileName, redirected ) )
    {
        strncpy( outRedirected, redirected.c_str(), outSize - 1 );
        outRedirected[ outSize - 1 ] = '\0';
        LOGI("[Mods] radSdlDriveGetRedirectedPath: '%s' -> '%s'", rawFileName, outRedirected);
        return true;
    }

    return false;
}

bool radSdlDriveIsOccluded( const char* rawFileName )
{
    if( !s_ModsEnabled || rawFileName == NULL || rawFileName[0] == '\0' )
    {
        return false;
    }

    EnsureModIniLoaded();

    if( s_ModOccludedPaths.empty() )
    {
        return false;
    }

    std::string norm = NormalizeModPath( rawFileName );
    for( size_t i = 0; i < s_ModOccludedPaths.size(); ++i )
    {
        const auto& occ = s_ModOccludedPaths[i];
        if( strcasecmp( norm.c_str(), occ.c_str() ) == 0 ||
            fnmatch( occ.c_str(), norm.c_str(), FNM_CASEFOLD ) == 0 )
        {
            LOGI("[Mods] radSdlDriveIsOccluded: BLOCKED '%s' (matched '%s')", rawFileName, occ.c_str());
            return true;
        }
    }

    return false;
}

static bool s_SafeFrontend = false;

void radSdlDriveSetModsConfig( bool enabled, const char* modsDir, const char* activeMod, bool safeFrontend )
{
    const char* safeDir = ( modsDir != NULL ) ? modsDir : s_ModsDirectory;
    const char* safeMod = ( activeMod != NULL ) ? activeMod : s_ActiveMod;

    if( s_ModsEnabled == enabled &&
        s_SafeFrontend == safeFrontend &&
        strcmp( s_ModsDirectory, safeDir ) == 0 &&
        strcmp( s_ActiveMod, safeMod ) == 0 &&
        s_ModIniParsed )
    {
        return;
    }

    s_ModsEnabled = enabled;
    s_SafeFrontend = safeFrontend;
    if( modsDir != NULL && modsDir[ 0 ] != '\0' )
    {
        strncpy( s_ModsDirectory, modsDir, sizeof( s_ModsDirectory ) - 1 );
        s_ModsDirectory[ sizeof( s_ModsDirectory ) - 1 ] = '\0';
    }
    if( activeMod != NULL )
    {
        strncpy( s_ActiveMod, activeMod, sizeof( s_ActiveMod ) - 1 );
        s_ActiveMod[ sizeof( s_ActiveMod ) - 1 ] = '\0';
    }
    s_PrimaryModFoldersScanned = false;
    s_FallbackModFoldersScanned = false;
    s_ModIniParsed = false;
    s_ModIniScanAttempts = 0;
    s_ModPathRedirections.clear();
    s_ModOccludedPaths.clear();
    ModLoaderLua::GetInstance().Reset();
    if( s_ModsEnabled && s_ActiveMod[0] != '\0' && strcasecmp( s_ActiveMod, "none" ) != 0 )
    {
        const char* effDir = ModManager::GetInstance().GetEffectiveActiveModDir();
        const char* dirToUse = ( effDir && effDir[0] != '\0' ) ? effDir : s_ModsDirectory;
        ModLoaderLua::GetInstance().Initialize( dirToUse, s_ActiveMod, s_FallbackGameDir );
    }
    LOGI("[Mods] Config updated: enabled=%d, dir='%s', active_mod='%s', safe_frontend=%d",
         s_ModsEnabled ? 1 : 0, s_ModsDirectory, s_ActiveMod, s_SafeFrontend ? 1 : 0);
}

static bool CheckModCandidate( const char* basePath, const char* subPath, const char* cleanFileName, char* outPath, size_t outSize )
{
    char candidate[1024];
    if( subPath && subPath[0] != '\0' )
    {
        snprintf( candidate, sizeof(candidate), "%s/%s/%s", basePath, subPath, cleanFileName );
    }
    else
    {
        snprintf( candidate, sizeof(candidate), "%s/%s", basePath, cleanFileName );
    }

    // 1. Fast path: direct exact case access
    if( access( candidate, R_OK ) == 0 )
    {
        struct stat st;
        if( stat( candidate, &st ) == 0 && !S_ISDIR( st.st_mode ) )
        {
            strncpy( outPath, candidate, outSize - 1 );
            outPath[ outSize - 1 ] = '\0';
            return true;
        }
    }

    // 2. Fallback: Case-insensitive path resolution for Android Linux filesystem
    std::string current = basePath;
    if( subPath && subPath[0] != '\0' )
    {
        current += "/";
        current += subPath;
    }

    std::string rel = cleanFileName;
    size_t start = 0;
    while( start < rel.size() )
    {
        size_t next = rel.find( '/', start );
        std::string part = ( next == std::string::npos ) ? rel.substr( start ) : rel.substr( start, next - start );
        start = ( next == std::string::npos ) ? rel.size() : next + 1;
        if( part.empty() || part == "." ) continue;

        std::string nextCandidate = current + "/" + part;
        struct stat st;
        if( stat( nextCandidate.c_str(), &st ) == 0 )
        {
            current = nextCandidate;
            continue;
        }

        // Search directory current case-insensitively
        DIR* dir = opendir( current.c_str() );
        if( !dir )
        {
            return false;
        }

        struct dirent* entry;
        bool found = false;
        while( ( entry = readdir( dir ) ) != NULL )
        {
            if( strcasecmp( entry->d_name, part.c_str() ) == 0 )
            {
                current += "/";
                current += entry->d_name;
                found = true;
                break;
            }
        }
        closedir( dir );

        if( !found )
        {
            return false;
        }
    }

    struct stat finalSt;
    if( stat( current.c_str(), &finalSt ) == 0 && !S_ISDIR( finalSt.st_mode ) )
    {
        strncpy( outPath, current.c_str(), outSize - 1 );
        outPath[ outSize - 1 ] = '\0';
        return true;
    }

    return false;
}

static bool IsScriptOrTextFile( const char* filename )
{
    if( filename == NULL ) return false;
    const char* dot = strrchr( filename, '.' );
    if( !dot ) return false;
    return ( strcasecmp( dot, ".mfk" ) == 0 ||
             strcasecmp( dot, ".con" ) == 0 ||
             strcasecmp( dot, ".spt" ) == 0 ||
             strcasecmp( dot, ".ini" ) == 0 ||
             strcasecmp( dot, ".txt" ) == 0 ||
             strcasecmp( dot, ".cfg" ) == 0 ||
             strcasecmp( dot, ".dat" ) == 0 );
}

static bool MergeAndServeAdditionalFile( const char* effDir,
                                         const char* cname,
                                         const char* additionalFilePath,
                                         char* outPath,
                                         size_t outSize )
{
    char basePath[1024];
    basePath[0] = '\0';

    if( s_MainGameDir[0] != '\0' )
    {
        char candidate[1024];
        snprintf( candidate, sizeof(candidate), "%s/%s", s_MainGameDir, cname );
        struct stat st;
        if( stat( candidate, &st ) == 0 && !S_ISDIR( st.st_mode ) )
        {
            strncpy( basePath, candidate, sizeof(basePath) - 1 );
            basePath[sizeof(basePath) - 1] = '\0';
        }
    }

    if( basePath[0] == '\0' && s_FallbackGameDir[0] != '\0' )
    {
        char candidate[1024];
        snprintf( candidate, sizeof(candidate), "%s/%s", s_FallbackGameDir, cname );
        struct stat st;
        if( stat( candidate, &st ) == 0 && !S_ISDIR( st.st_mode ) )
        {
            strncpy( basePath, candidate, sizeof(basePath) - 1 );
            basePath[sizeof(basePath) - 1] = '\0';
        }
    }

    // Also check standard public folder
    if( basePath[0] == '\0' )
    {
        const char* pub = "/storage/emulated/0/Simpsons Hit and Run/files";
        char candidate[1024];
        snprintf( candidate, sizeof(candidate), "%s/%s", pub, cname );
        struct stat st;
        if( stat( candidate, &st ) == 0 && !S_ISDIR( st.st_mode ) )
        {
            strncpy( basePath, candidate, sizeof(basePath) - 1 );
            basePath[sizeof(basePath) - 1] = '\0';
        }
    }

    // If base game file does NOT exist, AdditionalFiles provides the file directly!
    if( basePath[0] == '\0' )
    {
        strncpy( outPath, additionalFilePath, outSize - 1 );
        outPath[ outSize - 1 ] = '\0';
        return true;
    }

    // If not a text/script file (e.g. .p3d), do not merge bytes:
    if( !IsScriptOrTextFile( cname ) )
    {
        strncpy( outPath, additionalFilePath, outSize - 1 );
        outPath[ outSize - 1 ] = '\0';
        return true;
    }

    // Both base file and additional file exist, and it's a script/text file.
    // Merge them into <effDir>/_merged/<cname>
    char mergedPath[1024];
    snprintf( mergedPath, sizeof(mergedPath), "%s/_merged/%s", effDir, cname );

    struct stat baseSt, addSt, mergedSt;
    bool needsMerge = true;
    if( stat( basePath, &baseSt ) == 0 &&
        stat( additionalFilePath, &addSt ) == 0 &&
        stat( mergedPath, &mergedSt ) == 0 )
    {
        if( mergedSt.st_mtime >= baseSt.st_mtime && mergedSt.st_mtime >= addSt.st_mtime )
        {
            needsMerge = false;
        }
    }

    if( needsMerge )
    {
        std::string parentDir = mergedPath;
        size_t lastSlash = parentDir.rfind( '/' );
        if( lastSlash != std::string::npos )
        {
            parentDir = parentDir.substr( 0, lastSlash );
            char tmp[1024];
            snprintf( tmp, sizeof(tmp), "%s", parentDir.c_str() );
            for( char* p = tmp + 1; *p; p++ )
            {
                if( *p == '/' )
                {
                    *p = '\0';
                    mkdir( tmp, 0777 );
                    *p = '/';
                }
            }
            mkdir( tmp, 0777 );
        }

        FILE* fBase = fopen( basePath, "rb" );
        FILE* fAdd = fopen( additionalFilePath, "rb" );
        FILE* fOut = fopen( mergedPath, "wb" );

        if( fBase && fAdd && fOut )
        {
            char buf[8192];
            size_t rd;
            while( ( rd = fread( buf, 1, sizeof(buf), fBase ) ) > 0 )
            {
                fwrite( buf, 1, rd, fOut );
            }
            const char* separator = "\n\n// --- AdditionalFiles ---\n";
            fwrite( separator, 1, strlen(separator), fOut );
            while( ( rd = fread( buf, 1, sizeof(buf), fAdd ) ) > 0 )
            {
                fwrite( buf, 1, rd, fOut );
            }
            LOGI( "[Mods] Merged base game script '%s' with AdditionalFiles '%s' -> '%s'",
                  basePath, additionalFilePath, mergedPath );
        }
        else
        {
            LOGE( "[Mods] Failed to merge '%s' and '%s' to '%s'", basePath, additionalFilePath, mergedPath );
        }

        if( fBase ) fclose( fBase );
        if( fAdd ) fclose( fAdd );
        if( fOut ) fclose( fOut );
    }

    strncpy( outPath, mergedPath, outSize - 1 );
    outPath[ outSize - 1 ] = '\0';
    return true;
}

static bool SearchModInDirectory( const char* modsDir,
                                  std::vector<std::string>& discoveredFolders,
                                  bool& scannedFlag,
                                  const char* cname,
                                  char* outPath,
                                  size_t outSize )
{
    if( modsDir == NULL || modsDir[0] == '\0' )
    {
        return false;
    }

    struct stat st;
    if( stat( modsDir, &st ) != 0 || !S_ISDIR( st.st_mode ) )
    {
        return false;
    }

    // 1. If an active mod is configured, check inside that mod folder or cached extracted folder
    if( s_ActiveMod[0] != '\0' && strcmp( s_ActiveMod, "none" ) != 0 )
    {
        const char* effDir = ModManager::GetInstance().GetEffectiveActiveModDir();
        if( effDir != NULL && effDir[0] != '\0' )
        {
            if( CheckModCandidate( effDir, "CustomFiles", cname, outPath, outSize ) )
            {
                return true;
            }
            char addCand[1024];
            if( CheckModCandidate( effDir, "AdditionalFiles", cname, addCand, sizeof(addCand) ) )
            {
                return MergeAndServeAdditionalFile( effDir, cname, addCand, outPath, outSize );
            }
            if( CheckModCandidate( effDir, NULL, cname, outPath, outSize ) )
            {
                return true;
            }
        }

        // 1a. Check <mods_dir>/<active_mod>/CustomFiles/<file> (LMLM format)
        char sub[512];
        snprintf( sub, sizeof(sub), "%s/CustomFiles", s_ActiveMod );
        if( CheckModCandidate( modsDir, sub, cname, outPath, outSize ) )
        {
            return true;
        }

        // 1b. Check <mods_dir>/<active_mod>/AdditionalFiles/<file>
        char addSub[512];
        snprintf( addSub, sizeof(addSub), "%s/AdditionalFiles", s_ActiveMod );
        char addCand[1024];
        if( CheckModCandidate( modsDir, addSub, cname, addCand, sizeof(addCand) ) )
        {
            char modDir[1024];
            snprintf( modDir, sizeof(modDir), "%s/%s", modsDir, s_ActiveMod );
            return MergeAndServeAdditionalFile( modDir, cname, addCand, outPath, outSize );
        }

        // 1c. Check <mods_dir>/<active_mod>/<file>
        if( CheckModCandidate( modsDir, s_ActiveMod, cname, outPath, outSize ) )
        {
            return true;
        }

        // Single active mod rule: when an active mod is configured,
        // do not bleed into other mod directories. Return false to allow fallback to vanilla assets.
        return false;
    }

    // 2. Check root of mods_directory with CustomFiles
    if( CheckModCandidate( modsDir, "CustomFiles", cname, outPath, outSize ) )
    {
        return true;
    }

    // 3. Check root of mods_directory with AdditionalFiles
    char rootAddCand[1024];
    if( CheckModCandidate( modsDir, "AdditionalFiles", cname, rootAddCand, sizeof(rootAddCand) ) )
    {
        return MergeAndServeAdditionalFile( modsDir, cname, rootAddCand, outPath, outSize );
    }

    // 4. Check root of mods_directory directly
    if( CheckModCandidate( modsDir, NULL, cname, outPath, outSize ) )
    {
        return true;
    }

    // 5. Auto-discover mod subfolders in mods_directory (e.g. unzipped folders)
    if( !scannedFlag )
    {
        discoveredFolders.clear();
        DIR* dir = opendir( modsDir );
        if( dir != NULL )
        {
            struct dirent* ent;
            while( ( ent = readdir( dir ) ) != NULL )
            {
                if( ent->d_name[0] == '.' )
                {
                    continue;
                }
                char fullDir[1024];
                snprintf( fullDir, sizeof(fullDir), "%s/%s", modsDir, ent->d_name );
                struct stat entSt;
                if( stat( fullDir, &entSt ) == 0 && S_ISDIR( entSt.st_mode ) )
                {
                    if( strcasecmp( ent->d_name, "CustomFiles" ) != 0 && strcasecmp( ent->d_name, "AdditionalFiles" ) != 0 )
                    {
                        discoveredFolders.push_back( ent->d_name );
                        LOGI("[Mods] Auto-discovered mod directory in '%s': '%s'", modsDir, ent->d_name);
                    }
                }
            }
            closedir( dir );
        }
        scannedFlag = true;
    }

    for( size_t i = 0; i < discoveredFolders.size(); ++i )
    {
        const char* folder = discoveredFolders[i].c_str();

        // Check <mods_dir>/<folder>/CustomFiles/<file>
        char sub[512];
        snprintf( sub, sizeof(sub), "%s/CustomFiles", folder );
        if( CheckModCandidate( modsDir, sub, cname, outPath, outSize ) )
        {
            return true;
        }

        // Check <mods_dir>/<folder>/<folder>/CustomFiles/<file> (double folder extraction)
        char nestedSub[512];
        snprintf( nestedSub, sizeof(nestedSub), "%s/%s/CustomFiles", folder, folder );
        if( CheckModCandidate( modsDir, nestedSub, cname, outPath, outSize ) )
        {
            return true;
        }

        // Check <mods_dir>/<folder>/AdditionalFiles/<file>
        char addSub[512];
        snprintf( addSub, sizeof(addSub), "%s/AdditionalFiles", folder );
        if( CheckModCandidate( modsDir, addSub, cname, outPath, outSize ) )
        {
            return true;
        }

        // Check <mods_dir>/<folder>/<folder>/AdditionalFiles/<file>
        char nestedAddSub[512];
        snprintf( nestedAddSub, sizeof(nestedAddSub), "%s/%s/AdditionalFiles", folder, folder );
        if( CheckModCandidate( modsDir, nestedAddSub, cname, outPath, outSize ) )
        {
            return true;
        }

        // Check <mods_dir>/<folder>/<file>
        if( CheckModCandidate( modsDir, folder, cname, outPath, outSize ) )
        {
            return true;
        }
    }

    return false;
}

static bool FindModFile( const char* rawFileName, char* outPath, size_t outSize )
{
    if( !s_ModsEnabled || rawFileName == NULL || rawFileName[0] == '\0' )
    {
        return false;
    }

    EnsureModIniLoaded();

    // Normalize slashes and remove leading drive or slashes
    std::string cleanName( rawFileName );
    std::replace( cleanName.begin(), cleanName.end(), '\\', '/' );
    while( !cleanName.empty() && ( cleanName[0] == '/' || cleanName[0] == '.' ) )
    {
        cleanName.erase( 0, 1 );
    }

    if( cleanName.empty() )
    {
        return false;
    }

    if( s_SafeFrontend )
    {
        if( strcasecmp( cleanName.c_str(), "art/frontend/scrooby/frontend.p3d" ) == 0 ||
            strcasecmp( cleanName.c_str(), "art/frontend/frontend.p3d" ) == 0 ||
            strcasecmp( cleanName.c_str(), "art/frontend/scrooby/bootup.p3d" ) == 0 )
        {
            LOGI("[Mods] Safe Frontend: bypassing mod menu asset '%s'", cleanName.c_str());
            return false;
        }
    }

    if( radSdlDriveIsOccluded( cleanName.c_str() ) )
    {
        return false;
    }

    std::string redirected;
    std::string targetName = cleanName;
    if( CheckPathRedirection( cleanName.c_str(), redirected ) )
    {
        targetName = redirected;
        LOGI("[Mods] Path redirection in FindModFile: '%s' -> '%s'", cleanName.c_str(), targetName.c_str());
    }

    std::string luaResolved;
    if( ModLoaderLua::GetInstance().HandleFileRequest( cleanName.c_str(), luaResolved ) )
    {
        strncpy( outPath, luaResolved.c_str(), outSize - 1 );
        outPath[ outSize - 1 ] = '\0';
        LOGI("[Mods] Handled by Lua PathHandler: '%s' -> '%s'", cleanName.c_str(), outPath);
        return true;
    }
    if( targetName != cleanName && ModLoaderLua::GetInstance().HandleFileRequest( targetName.c_str(), luaResolved ) )
    {
        strncpy( outPath, luaResolved.c_str(), outSize - 1 );
        outPath[ outSize - 1 ] = '\0';
        LOGI("[Mods] Handled by Lua PathHandler (redirected): '%s' -> '%s'", targetName.c_str(), outPath);
        return true;
    }

    const char* cname = targetName.c_str();

    // 1. Check primary configured mods directory (default: /storage/emulated/0/Simpsons Hit and Run/mods)
    if( SearchModInDirectory( s_ModsDirectory, s_DiscoveredModFoldersPrimary, s_PrimaryModFoldersScanned, cname, outPath, outSize ) )
    {
        return true;
    }

    // 2. Check legacy fallback directory (/storage/emulated/0/simpsons_mods) if distinct
    const char* legacyDir = "/storage/emulated/0/simpsons_mods";
    if( strcmp( s_ModsDirectory, legacyDir ) != 0 )
    {
        if( SearchModInDirectory( legacyDir, s_DiscoveredModFoldersFallback, s_FallbackModFoldersScanned, cname, outPath, outSize ) )
        {
            return true;
        }
    }

    // 3. If redirected, also attempt searching the unredirected original cleanName as fallback
    if( targetName != cleanName )
    {
        const char* origName = cleanName.c_str();
        if( SearchModInDirectory( s_ModsDirectory, s_DiscoveredModFoldersPrimary, s_PrimaryModFoldersScanned, origName, outPath, outSize ) )
        {
            return true;
        }
        if( strcmp( s_ModsDirectory, legacyDir ) != 0 )
        {
            if( SearchModInDirectory( legacyDir, s_DiscoveredModFoldersFallback, s_FallbackModFoldersScanned, origName, outPath, outSize ) )
            {
                return true;
            }
        }
    }

    return false;
}

bool radSdlDriveFindModFile( const char* rawFileName, char* outPath, size_t outSize )
{
    return FindModFile( rawFileName, outPath, outSize );
}

static bool CheckFallbackGamePath( const char* rawFileName, char* outPath, size_t outSize )
{
    if( s_FallbackGameDir[0] == '\0' || rawFileName == NULL || rawFileName[0] == '\0' )
    {
        return false;
    }

    std::string cleanName( rawFileName );
    std::replace( cleanName.begin(), cleanName.end(), '\\', '/' );
    while( !cleanName.empty() && ( cleanName[0] == '/' || cleanName[0] == '.' ) )
    {
        cleanName.erase( 0, 1 );
    }

    if( cleanName.empty() )
    {
        return false;
    }

    return CheckModCandidate( s_FallbackGameDir, NULL, cleanName.c_str(), outPath, outSize );
}

static bool DirectoryHasGameFiles( const char* dirPath )
{
    if( dirPath == NULL || dirPath[0] == '\0' )
    {
        return false;
    }

    DIR* dir = opendir( dirPath );
    if( dir == NULL )
    {
        return false;
    }

    bool hasFiles = false;
    struct dirent* ent;
    while( ( ent = readdir( dir ) ) != NULL )
    {
        if( ent->d_name[0] == '.' )
        {
            continue;
        }

        const char* dot = strrchr( ent->d_name, '.' );
        if( dot != NULL && strcasecmp( dot, ".rcf" ) == 0 )
        {
            hasFiles = true;
            break;
        }

        if( strcasecmp( ent->d_name, "scripts" ) == 0 ||
            strcasecmp( ent->d_name, "art" ) == 0 ||
            strcasecmp( ent->d_name, "sound" ) == 0 )
        {
            hasFiles = true;
            break;
        }
    }
    closedir( dir );
    return hasFiles;
}
#else
void radSdlDriveSetModsConfig( bool, const char*, const char*, bool ) {}
#endif


//=============================================================================
// Public Functions 
//=============================================================================

//=============================================================================
// Function:    radSdlDriveFactory
//=============================================================================
// Description: This member is responsible for constructing a radSdlDriveObject.
//
// Parameters:  pointer to receive drive object
//              pointer to the drive name
//              allocator
//              
// Returns:     
//------------------------------------------------------------------------------

void radSdlDriveFactory
( 
    radDrive**         ppDrive, 
    const char*        pDriveName,
    radMemoryAllocator alloc
)
{
    //
    // Simply constuct the drive object.
    //
    *ppDrive = new( alloc ) radSdlDrive( pDriveName, alloc );
    rAssert( *ppDrive != NULL );
}


//=============================================================================
// Public Member Functions
//=============================================================================

//=============================================================================
// Function:    radSdlDrive::radSdlDrive
//=============================================================================

/*
radSdlDrive::radSdlDrive( const char* pdrivespec, radMemoryAllocator alloc )
    : 
    radDrive( ),
    m_OpenFiles( 0 ),
    m_pMutex( NULL )
{
    //
    // Create a mutex for lock/unlock
    //
    radThreadCreateMutex( &m_pMutex, alloc );
    rAssert( m_pMutex != NULL );

    //
    // Create the drive thread.
    //
    m_pDriveThread = new( alloc ) radDriveThread( m_pMutex, alloc );
    rAssert( m_pDriveThread != NULL );

    //
    // Copy the drivename
    //
    radGetDefaultDrive( m_DriveName );
    if ( strcmp(m_DriveName, pdrivespec ) != 0 )
    {
        strncpy( m_DriveName, pdrivespec, radFileDrivenameMax );
        strncpy( m_DrivePath, pdrivespec, radFileFilenameMax );
        m_DriveName[radFileDrivenameMax] = '\0';
        m_DrivePath[radFileFilenameMax] = '\0';
        SDL_strupr( m_DriveName );
        SDL_strlwr( m_DrivePath );
    }

    if(!m_DrivePath[0])
    {
#if SDL_MAJOR_VERSION < 3
#ifdef WIN32
        _getcwd( m_DrivePath, radFileFilenameMax );
        strncat(m_DrivePath, "/", radFileFilenameMax);
#else
        getcwd( m_DrivePath, radFileFilenameMax );
        strncat(m_DrivePath, "/", radFileFilenameMax);
#endif
#else
        char* cwd = SDL_GetCurrentDirectory();
        strncpy(m_DrivePath, cwd, radFileFilenameMax);
        SDL_free(cwd);
#endif
        m_DrivePath[radFileFilenameMax] = '\0';
    }

#if SDL_MAJOR_VERSION < 3
    m_Capabilities = ( radDriveWriteable | radDriveFile );
#else
    m_Capabilities = ( radDriveEnumerable | radDriveWriteable | radDriveDirectory | radDriveFile );
#endif
}
*/

//android change function
  /*
radSdlDrive::radSdlDrive(const char* pdrivespec, radMemoryAllocator alloc)
    :
    radDrive(),
    m_OpenFiles(0),
    m_pMutex(NULL)
{
    radThreadCreateMutex(&m_pMutex, alloc);
    rAssert(m_pMutex != NULL);

    m_pDriveThread = new(alloc) radDriveThread(m_pMutex, alloc);
    rAssert(m_pDriveThread != NULL);

    radGetDefaultDrive(m_DriveName);

    // ============================================================
    // CAMBIO AQUÍ: En Android IGNORAMOS pdrivespec para que NO pise
    // el root (a ti te llega "/" y te rompe m_DrivePath).
    // ============================================================
#if defined(RAD_ANDROID)
    (void)pdrivespec; // CAMBIO AQUÍ: evitar warning y NO usar pdrivespec
#else
    // Si te pasan drive spec (ej: "D:"), lo respetas
    if (pdrivespec && strcmp(m_DriveName, pdrivespec) != 0)
    {
        strncpy(m_DriveName, pdrivespec, radFileDrivenameMax);
        strncpy(m_DrivePath, pdrivespec, radFileFilenameMax);
        m_DriveName[radFileDrivenameMax] = '\0';
        m_DrivePath[radFileFilenameMax] = '\0';
        SDL_strupr(m_DriveName);

        // IMPORTANTE: solo lower-case en WIN32, nunca en Android
    #if defined(WIN32)
        SDL_strlwr(m_DrivePath);
    #endif
    }
#endif

    // ============================================================
    // CAMBIO AQUÍ: En Android FORZAMOS siempre el root a tu carpeta.
    // No dependemos de cwd, ni de pdrivespec, ni de nada.
    // ============================================================
#if defined(RAD_ANDROID)
    strncpy(m_DrivePath, "/data/data/org.libsdl.app/files/", radFileFilenameMax); // CAMBIO AQUÍ
    m_DrivePath[radFileFilenameMax] = '\0';                                      // CAMBIO AQUÍ
#else
    // Si no te han puesto drive path, defines el root por plataforma
    if (!m_DrivePath[0])
    {
    #if SDL_MAJOR_VERSION < 3
    #ifdef WIN32
        _getcwd(m_DrivePath, radFileFilenameMax);
    #else
        getcwd(m_DrivePath, radFileFilenameMax);
    #endif
        strncat(m_DrivePath, "/", radFileFilenameMax);
    #else
        char* cwd = SDL_GetCurrentDirectory();
        strncpy(m_DrivePath, cwd, radFileFilenameMax);
        SDL_free(cwd);
    #endif
        m_DrivePath[radFileFilenameMax] = '\0';
    }
#endif

    // ============================================================
    // CAMBIO AQUÍ: En Android NO hacemos strncat para añadir "/".
    // Ya viene con "/" en el literal; así evitas corrupción rara.
    // ============================================================
#if !defined(RAD_ANDROID)
    // (Opcional pero recomendable) asegurar "/" final siempre
    size_t len = strlen(m_DrivePath);
    if (len > 0 && m_DrivePath[len - 1] != '/' && len < radFileFilenameMax) {
        strncat(m_DrivePath, "/", radFileFilenameMax - len - 1);
    }
#endif

#if SDL_MAJOR_VERSION < 3
    m_Capabilities = (radDriveWriteable | radDriveFile);
#else
    m_Capabilities = (radDriveEnumerable | radDriveWriteable | radDriveDirectory | radDriveFile);
#endif

#ifdef RAD_ANDROID
    // Logs útiles para confirmar que ya NO sale "???p/"
    LOGI("radSdlDrive ctor: pdrivespec='%s'", pdrivespec ? pdrivespec : "(null)");
    LOGI("radSdlDrive ctor: m_DriveName='%s'", m_DriveName);
    LOGI("radSdlDrive ctor: m_DrivePath='%s'", m_DrivePath);

    // Comprueba que el root existe y que sound/ existe
    LogPathStatus("DrivePath", m_DrivePath);
    LogPathStatus("SoundDir", "/data/data/org.libsdl.app/files/sound");

    char cwd[512];
    if (getcwd(cwd, sizeof(cwd))) {
        LOGI("radSdlDrive ctor: getcwd()='%s'", cwd);
    } else {
        LOGI("radSdlDrive ctor: getcwd() FAIL errno=%d (%s)", errno, strerror(errno));
    }
#endif
}
*/
  // android change function
radSdlDrive::radSdlDrive(const char* pdrivespec, radMemoryAllocator alloc)
    :
    radDrive(),
    m_OpenFiles(0),
    m_pMutex(NULL)
{
    radThreadCreateMutex(&m_pMutex, alloc);
    rAssert(m_pMutex != NULL);

    m_pDriveThread = new(alloc) radDriveThread(m_pMutex, alloc);
    rAssert(m_pDriveThread != NULL);

    radGetDefaultDrive(m_DriveName);

    // ============================================================
    // CAMBIO AQUÍ: En Android IGNORAMOS pdrivespec para que NO pise
    // el root (a ti te llega "/" y te rompe m_DrivePath).
    // ============================================================
#if defined(RAD_ANDROID)
    (void)pdrivespec; // CAMBIO AQUÍ
#else
    // Si te pasan drive spec (ej: "D:"), lo respetas
    if (pdrivespec && strcmp(m_DriveName, pdrivespec) != 0)
    {
        strncpy(m_DriveName, pdrivespec, radFileDrivenameMax);
        strncpy(m_DrivePath, pdrivespec, radFileFilenameMax);
        m_DriveName[radFileDrivenameMax] = '\0';
        m_DrivePath[radFileFilenameMax] = '\0';
        SDL_strupr(m_DriveName);

    #if defined(WIN32)
        SDL_strlwr(m_DrivePath);
    #endif
    }
#endif

    // ============================================================
    // Android: Use unified shared storage "Simpsons Hit and Run/files/"
    // with automatic fallback to legacy "Android/data/<pkg>/files/".
    // ============================================================
#if defined(RAD_ANDROID)
    mkdir( "/storage/emulated/0/Simpsons Hit and Run", 0777 );
    mkdir( "/storage/emulated/0/Simpsons Hit and Run/files", 0777 );
    mkdir( "/storage/emulated/0/Simpsons Hit and Run/mods", 0777 );

    const char* publicFiles = "/storage/emulated/0/Simpsons Hit and Run/files/";
    const char* ext = SDL_AndroidGetExternalStoragePath();

    bool publicHasFiles = DirectoryHasGameFiles( "/storage/emulated/0/Simpsons Hit and Run/files" );
    bool legacyHasFiles = ( ext && ext[0] ) ? DirectoryHasGameFiles( ext ) : false;

    if( publicHasFiles )
    {
        strncpy( m_DrivePath, publicFiles, radFileFilenameMax );
        m_DrivePath[ radFileFilenameMax ] = '\0';

        if( ext && ext[0] )
        {
            strncpy( s_FallbackGameDir, ext, sizeof( s_FallbackGameDir ) - 1 );
            s_FallbackGameDir[ sizeof( s_FallbackGameDir ) - 1 ] = '\0';
        }
        LOGI("[Drive] Active root: public folder '%s' (fallback: '%s')", m_DrivePath, s_FallbackGameDir);
    }
    else if( legacyHasFiles )
    {
        strncpy( m_DrivePath, ext, radFileFilenameMax );
        m_DrivePath[ radFileFilenameMax ] = '\0';

        strncpy( s_FallbackGameDir, publicFiles, sizeof( s_FallbackGameDir ) - 1 );
        s_FallbackGameDir[ sizeof( s_FallbackGameDir ) - 1 ] = '\0';
        LOGI("[Drive] Active root: legacy external storage '%s' (fallback: '%s')", m_DrivePath, s_FallbackGameDir);
    }
    else
    {
        strncpy( m_DrivePath, publicFiles, radFileFilenameMax );
        m_DrivePath[ radFileFilenameMax ] = '\0';

        if( ext && ext[0] )
        {
            strncpy( s_FallbackGameDir, ext, sizeof( s_FallbackGameDir ) - 1 );
            s_FallbackGameDir[ sizeof( s_FallbackGameDir ) - 1 ] = '\0';
        }
        LOGI("[Drive] Defaulting root to public folder: '%s'", m_DrivePath);
    }

#else
    // Si no te han puesto drive path, defines el root por plataforma
    if (!m_DrivePath[0])
    {
    #if SDL_MAJOR_VERSION < 3
    #ifdef WIN32
        _getcwd(m_DrivePath, radFileFilenameMax);
    #else
        getcwd(m_DrivePath, radFileFilenameMax);
    #endif
        strncat(m_DrivePath, "/", radFileFilenameMax);
    #else
        char* cwd = SDL_GetCurrentDirectory();
        strncpy(m_DrivePath, cwd, radFileFilenameMax);
        SDL_free(cwd);
    #endif
        m_DrivePath[radFileFilenameMax] = '\0';
    }
#endif

    // ============================================================
    // CAMBIO AQUÍ: asegurar "/" final SOLO si no lo trae ya.
    // (En Android ya lo construimos con "/" al final, pero lo dejo
    //  defensivo por si cambias el snprintf en el futuro)
    // ============================================================
    size_t len = strlen(m_DrivePath);
    if (len > 0 && m_DrivePath[len - 1] != '/' && len < radFileFilenameMax) {
        strncat(m_DrivePath, "/", radFileFilenameMax - len - 1);
    }

#if defined(RAD_ANDROID)
    strncpy( s_MainGameDir, m_DrivePath, sizeof(s_MainGameDir) - 1 );
    s_MainGameDir[ sizeof(s_MainGameDir) - 1 ] = '\0';
    size_t mgLen = strlen(s_MainGameDir);
    if( mgLen > 0 && s_MainGameDir[mgLen - 1] == '/' ) {
        s_MainGameDir[mgLen - 1] = '\0';
    }

    size_t fLen = strlen(s_FallbackGameDir);
    if (fLen > 0 && s_FallbackGameDir[fLen - 1] != '/' && fLen < sizeof(s_FallbackGameDir)) {
        strncat(s_FallbackGameDir, "/", sizeof(s_FallbackGameDir) - fLen - 1);
    }
#endif

#if SDL_MAJOR_VERSION < 3
    m_Capabilities = (radDriveWriteable | radDriveFile);
#else
    m_Capabilities = (radDriveEnumerable | radDriveWriteable | radDriveDirectory | radDriveFile);
#endif

#ifdef RAD_ANDROID
    // CAMBIO AQUÍ: logs para verificar que ya apunta donde toca
    //LOGI("radSdlDrive ctor: ext='%s'", ext ? ext : "(null)");
    //LOGI("radSdlDrive ctor: m_DriveName='%s'", m_DriveName);
    //LOGI("radSdlDrive ctor: m_DrivePath='%s'", m_DrivePath);

    LogPathStatus("DrivePath", m_DrivePath);

    // CAMBIO AQUÍ: comprobar subcarpeta sound relativa a root
    // (evita hardcodear /data/data/..., ahora cuelga de m_DrivePath)
    char soundPath[radFileFilenameMax + 1];
    snprintf(soundPath, sizeof(soundPath), "%ssound", m_DrivePath);
    LogPathStatus("SoundDir", soundPath);
#endif
}

  
  
//=============================================================================
// Function:    radSdlDrive::~radSdlDrive
//=============================================================================

radSdlDrive::~radSdlDrive( void )
{
    m_pMutex->Release( );
    m_pDriveThread->Release( );
}

//=============================================================================
// Function:    radSdlDrive::Lock
//=============================================================================
// Description: Start a critical section
//
// Parameters:  
//
// Returns:     
//------------------------------------------------------------------------------

void radSdlDrive::Lock( void )
{
    m_pMutex->Lock( );
}

//=============================================================================
// Function:    radSdlDrive::Unlock
//=============================================================================
// Description: End a critical section
//
// Parameters:  
//
// Returns:     
//------------------------------------------------------------------------------

void radSdlDrive::Unlock( void )
{
    m_pMutex->Unlock( );
}

//=============================================================================
// Function:    radSdlDrive::GetCapabilities
//=============================================================================

unsigned int radSdlDrive::GetCapabilities( void )
{
    return m_Capabilities;
}

//=============================================================================
// Function:    radGcnDVDDrive::GetDriveName
//=============================================================================

const char* radSdlDrive::GetDriveName( void )
{
    return m_DriveName;
}

//=============================================================================
// Function:    radSdlDrive::Initialize
//=============================================================================

radDrive::CompletionStatus radSdlDrive::Initialize( void )
{
    SetMediaInfo();

    //
    // Success
    //
    m_LastError = Success;
    return Complete;
}

//=============================================================================
// Function:    radSdlDrive::OpenFile
//=============================================================================

radDrive::CompletionStatus radSdlDrive::OpenFile
( 
    const char*         fileName, 
    radFileOpenFlags    flags, 
    bool                writeAccess, 
    radFileHandle*      pHandle, 
    unsigned int*       pSize 
)
{
    //
    // Build the full filename
    //
    char fullName[ radFileFilenameMax + 1 ];
    BuildFileSpec( fileName, fullName, radFileFilenameMax + 1 );

#if defined(RAD_ANDROID)
    char modFileSpec[ 1024 ];
    if( FindModFile( fileName, modFileSpec, sizeof(modFileSpec) ) )
    {
        LOGI("[Mods] Intercepted file: '%s' -> '%s'", fileName, modFileSpec);
        strncpy( fullName, modFileSpec, radFileFilenameMax );
        fullName[ radFileFilenameMax ] = '\0';
    }
    else if( flags == OpenExisting && access( fullName, R_OK ) != 0 )
    {
        char fallbackSpec[ 1024 ];
        if( CheckFallbackGamePath( fileName, fallbackSpec, sizeof(fallbackSpec) ) )
        {
            LOGI("[Drive] Fallback game file: '%s' -> '%s'", fileName, fallbackSpec);
            strncpy( fullName, fallbackSpec, radFileFilenameMax );
            fullName[ radFileFilenameMax ] = '\0';
        }
        else if( ( fileName && strstr( fileName, "empty.spt" ) != NULL ) || strstr( fullName, "empty.spt" ) != NULL )
        {
            const char* dummyPath = "/data/local/tmp/shar_empty.spt";
            int fd = open( dummyPath, O_CREAT | O_RDWR, 0666 );
            if( fd < 0 )
            {
                dummyPath = "/tmp/shar_empty.spt";
                fd = open( dummyPath, O_CREAT | O_RDWR, 0666 );
            }
            if( fd >= 0 )
            {
                close( fd );
                LOGI("[Drive] Generated dummy empty script for '%s' -> '%s'", fileName, dummyPath);
                strncpy( fullName, dummyPath, radFileFilenameMax );
                fullName[ radFileFilenameMax ] = '\0';
            }
        }
    }
#endif

    //
    // Translate flags to SDL
    //
    const char* createFlags;
    switch( flags )
    {
    case OpenExisting:
        createFlags = writeAccess ? "rb+" : "rb";
        break;
    case OpenAlways:
        createFlags = "ab+";
        break;
    case CreateAlways:
        createFlags = "wb+";
        break;
    default:
        rAssertMsg( false, "radFileSystem: sdldrive: attempting to open file with unknown flag" );
        return Error;
    }
	
	
    #if defined(RAD_ANDROID) && defined(RAD_DEBUG)
    LOGI("OpenFile: fileName='%s' -> fullName='%s' mode='%s'",
         fileName ? fileName : "(null)",
         fullName,
         createFlags ? createFlags : "(null)");

    // ¿Existe? ¿se puede leer?
    errno = 0;
    int a = access(fullName, R_OK);
    LOGI("OpenFile: access(R_OK)=%d errno=%d (%s)", a, errno, strerror(errno));
#endif


#if SDL_MAJOR_VERSION < 3
    *pHandle = SDL_RWFromFile(fullName, createFlags);
#else
#if defined(RAD_ANDROID) && defined(RAD_DEBUG)
        LOGI("OpenFile FAILED: fullName='%s' errno=%d (%s) SDL_GetError='%s'",
             fullName, errno, strerror(errno), SDL_GetError());
#endif

    *pHandle = SDL_IOFromFile( fullName, createFlags );
#endif

    if ( *pHandle )
    {
        m_OpenFiles++;
#if SDL_MAJOR_VERSION < 3
        *pSize = SDL_RWsize( (SDL_RWops*)*pHandle );
#else
        *pSize = SDL_GetIOSize( (SDL_IOStream*)*pHandle );
#endif
        m_LastError = Success;
        return Complete;
    }
    else
    {
#if defined(RAD_ANDROID)
        if( s_pRadSdlDriveLogHook != NULL )
        {
            char failBuf[ 512 ];
            snprintf( failBuf, sizeof( failBuf ), "Failed to open file: '%s' (full path: '%s', mode: '%s', SDL_Error: '%s')",
                      fileName ? fileName : "(null)", fullName, createFlags ? createFlags : "(null)", SDL_GetError() );
            s_pRadSdlDriveLogHook( "FileIO", failBuf );
        }
#endif
        m_LastError = FileNotFound;
        return Error;
    }
}

//=============================================================================
// Function:    radSdlDrive::CloseFile
//=============================================================================

radDrive::CompletionStatus radSdlDrive::CloseFile( radFileHandle handle, const char* fileName )
{
#if SDL_MAJOR_VERSION < 3
    SDL_RWclose( (SDL_RWops*)handle );
#else
    SDL_CloseIO( (SDL_IOStream*)handle );
#endif
    m_OpenFiles--;
    return Complete;
}

//=============================================================================
// Function:    radSdlDrive::ReadFile
//=============================================================================

radDrive::CompletionStatus radSdlDrive::ReadFile
( 
    radFileHandle   handle, 
    const char*     fileName,
    IRadFile::BufferedReadState buffState,
    unsigned int    position, 
    void*           pData, 
    unsigned int    bytesToRead, 
    unsigned int*   bytesRead, 
    radMemorySpace  pDataSpace 
)
{
    rAssertMsg( pDataSpace == radMemorySpace_Local, 
                "radFileSystem: radSdlDrive: External memory not supported for reads." );

    //
    // set file pointer
    //
#if SDL_MAJOR_VERSION < 3
    if ( SDL_RWseek( (SDL_RWops*)handle, position, RW_SEEK_SET ) >= 0 )
    {
        if (SDL_RWread( (SDL_RWops*)handle, pData, 1, bytesToRead ) > 0 )
        {
#else
    if ( SDL_SeekIO( (SDL_IOStream*)handle, position, SDL_IO_SEEK_SET ) >= 0 )
    {
        if ( SDL_ReadIO( (SDL_IOStream*)handle, pData, bytesToRead ) > 0 )
        {
#endif
            //
            // Successful read!
            //
            
            //
            // Change this during buffered read!!
            //
            *bytesRead = bytesToRead;
            m_LastError = Success;
            return Complete;
        }
    }

    //
    // Failed!
    //
    m_LastError = FileNotFound;
    return Error;
}

//=============================================================================
// Function:    radSdlDrive::WriteFile
//=============================================================================

radDrive::CompletionStatus radSdlDrive::WriteFile
( 
    radFileHandle     handle,
    const char*       fileName,
    IRadFile::BufferedReadState buffState,
    unsigned int      position, 
    const void*       pData, 
    unsigned int      bytesToWrite, 
    unsigned int*     bytesWritten, 
    unsigned int*     pSize, 
    radMemorySpace    pDataSpace 
)
{
    if ( !( m_Capabilities & radDriveWriteable ) )
    {
        rWarningMsg( m_Capabilities & radDriveWriteable, "This drive does not support the WriteFile function." );
        return Error;
    }

    rAssertMsg( pDataSpace == radMemorySpace_Local, 
                "radFileSystem: radSdlDrive: External memory not supported for reads." );

    //
    // do the write
    //
#if SDL_MAJOR_VERSION < 3
    if ( SDL_RWseek( (SDL_RWops*)handle, position, RW_SEEK_SET ) >= 0 )
    {
        *bytesWritten = SDL_RWwrite( (SDL_RWops*)handle, pData, 1, bytesToWrite );
#else
    if ( SDL_SeekIO( (SDL_IOStream*)handle, position, SDL_IO_SEEK_SET ) >= 0 )
    {
        *bytesWritten = SDL_WriteIO( (SDL_IOStream*)handle, pData, bytesToWrite );
#endif
        if ( *bytesWritten == bytesToWrite )
        {
            //
            // Sucessful write
            //
#if SDL_MAJOR_VERSION < 3
            *pSize = SDL_RWsize( (SDL_RWops*)handle );
#else
            *pSize = SDL_GetIOSize( (SDL_IOStream*)handle );
#endif
            m_LastError = Success;
            return Complete;
        }
    }

    //
    // Failed!
    //
    m_LastError = FileNotFound;
    return Error;
}

#if SDL_MAJOR_VERSION > 2
//=============================================================================
// Function:    radSdlDrive::FindFirst
//=============================================================================

radDrive::CompletionStatus radSdlDrive::FindFirst
( 
    const char*                 searchSpec, 
    IRadDrive::DirectoryInfo*   pDirectoryInfo, 
    radFileDirHandle*           pHandle,
    bool                        firstSearch
)
{
    //
    // Find first
    //
    const char* pattern = strrchr(searchSpec, '\\');
    std::string path;
    if (!pattern)
        pattern = strrchr(searchSpec, '/');
    if (pattern)
        path = std::string(searchSpec, pattern - searchSpec);
    else
        pattern = searchSpec;
    path = m_DrivePath + path;
    std::replace(path.begin(), path.end(), '\\', '/');

    char** handle = SDL_GlobDirectory( path.c_str(), pattern, SDL_GLOB_CASEINSENSITIVE, NULL );
    if ( handle )
    {
        SDL_PathInfo info;
        if ( SDL_GetPathInfo( handle[0], &info))
            m_LastError = TranslateDirInfo( pDirectoryInfo, &info, pHandle );
        else
            m_LastError = TranslateDirInfo( pDirectoryInfo, NULL, pHandle );
        // HACK: We don't need the first element anymore, so use it to store the iterator
        ((char***)handle)[0] = handle;
    }
    else
    {
        m_LastError = FileNotFound;
    }

    //
    // Fill in our directory info structure
    //
    if ( m_LastError == Success )
    {
        return Complete;
    }
    else
    {
        return Error;
    }
}

//=============================================================================
// Function:    radSdlDrive::FindNext
//=============================================================================

radDrive::CompletionStatus radSdlDrive::FindNext( radFileDirHandle* pHandle, IRadDrive::DirectoryInfo* pDirectoryInfo )
{
    //
    // If we don't have a handle, return file not found.
    //
    if ( *pHandle == NULL )
    {
        m_LastError = FileNotFound;
        return Error;
    }

    //
    // Find the next entry
    //
    char*** handle = (char***)*pHandle;
    *handle++;
    SDL_PathInfo info;
    if ( SDL_GetPathInfo(  **handle, &info ))
        m_LastError = TranslateDirInfo( pDirectoryInfo, &info, pHandle );
    else
        m_LastError = TranslateDirInfo( pDirectoryInfo, NULL, pHandle );
    
    if ( m_LastError == Success )
    {
        m_LastError = Success;
        return Complete;
    }
    else
    {
        m_LastError = FileNotFound;
        return Error;
    }
}

//=============================================================================
// Function:    radSdlDrive::FindClose
//=============================================================================

radDrive::CompletionStatus radSdlDrive::FindClose( radFileDirHandle* pHandle )
{
    SDL_free( *pHandle );
    *pHandle = NULL;

    return Complete;
}

//=============================================================================
// Function:    radSdlDrive::CreateDir
//=============================================================================

radDrive::CompletionStatus radSdlDrive::CreateDir( const char* pName )
{
    rWarningMsg( m_Capabilities & radDriveDirectory, 
        "This drive does not support the CreateDir function." );

    //
    // Build the full filename
    //
    char fullSpec[ radFileFilenameMax + 1 ];
    BuildFileSpec( pName, fullSpec, radFileFilenameMax + 1 );

    if ( SDL_CreateDirectory( fullSpec ) )
    {
        m_LastError = Success;
        return Complete;
    }
    else
    {
        m_LastError = FileNotFound;
        return Error;
    }
}

//=============================================================================
// Function:    radSdlDrive::DestroyDir
//=============================================================================

radDrive::CompletionStatus radSdlDrive::DestroyDir( const char* pName )
{
    rWarningMsg( m_Capabilities & radDriveDirectory,
        "This drive does not support the DestroyDir function." );

    //
    // Someday check if pName is a dir!
    //

    //
    // Build the full filename
    //
    char fullSpec[ radFileFilenameMax + 1 ];
    BuildFileSpec( pName, fullSpec, radFileFilenameMax + 1 );

    if ( SDL_RemovePath( fullSpec ) )
    {
        m_LastError = Success;
        return Complete;
    }
    else
    {
        m_LastError = FileNotFound;
        return Error;
    }
}

//=============================================================================
// Function:    radSdlDrive::DestroyFile
//=============================================================================

radDrive::CompletionStatus radSdlDrive::DestroyFile( const char* filename )
{
    rWarningMsg( m_Capabilities & radDriveWriteable, "This drive does not support the DestroyFile function." );

    //
    // Someday check if the file is open!
    //

    //
    // Build the full filename
    //
    char fullSpec[ radFileFilenameMax + 1 ];
    BuildFileSpec( filename, fullSpec, radFileFilenameMax + 1 );

    if ( SDL_RemovePath( fullSpec ) )
    {
        m_LastError = Success;
        return Complete;
    }
    else
    {
        m_LastError = FileNotFound;
        return Error;
    }
}
#endif

//=============================================================================
// Private Member Functions
//=============================================================================

//=============================================================================
// Function:    radSdlDrive::SetMediaInfo
//=============================================================================

void radSdlDrive::SetMediaInfo( void )
{
    //
    // Get volume information.
    //
    const char* realDriveName = m_DriveName;

    //rAssert( strlen( realDriveName ) == 2 );
    strcpy(m_MediaInfo.m_VolumeName, realDriveName );
    //strcat(m_MediaInfo.m_VolumeName, "\\");

    m_MediaInfo.m_SectorSize = SDL_DEFAULT_SECTOR_SIZE;

    /*
    if(!error)
    {
        m_MediaInfo.m_MediaState = IRadDrive::MediaInfo::MediaPresent;
        m_MediaInfo.m_FreeSpace = space.free;

        //
        // No file limit, so set it to the available space
        //
        m_MediaInfo.m_FreeFiles = space.available / m_MediaInfo.m_SectorSize;
        m_LastError = Success;
    }
    else
    */
    {
        //
        // Don't have media info, so fill structure in with dummy info
        //
        m_MediaInfo.m_MediaState = IRadDrive::MediaInfo::MediaPresent;
        m_MediaInfo.m_FreeSpace = UINT_MAX;
        m_MediaInfo.m_FreeFiles = m_MediaInfo.m_FreeSpace / m_MediaInfo.m_SectorSize;
        m_LastError = Success;
    }
}

//=============================================================================
// Function:    radSdlDrive::BuildFileSpec
//=============================================================================

void radSdlDrive::BuildFileSpec( const char* fileName, char* fullName, unsigned int size )
{
    std::string path(m_DrivePath);
    path += fileName;
    std::replace(path.begin(), path.end(), '\\', '/');

    strncpy( fullName, path.c_str(), size - 1 );
    fullName[ size - 1 ] = '\0';
}

#if SDL_MAJOR_VERSION > 2
//=============================================================================
// Function:    radSdlDrive::TranslateDirInfo
//=============================================================================
// Description: Translate the directory info and return an error status. A handle
//              with value directory_iterator() means the find_first/next call
//              failed and needs to be checked if something went wrong or if the
//              search just ended.
//
// Parameters:  
//              
// Returns:     
//------------------------------------------------------------------------------

radFileError radSdlDrive::TranslateDirInfo
( 
    IRadDrive::DirectoryInfo*   pDirectoryInfo, 
    const SDL_PathInfo*         pPathInfo,
    const radFileDirHandle*     pHandle
)
{
    char*** handle = (char***)*pHandle;
    if ( !pPathInfo || !*handle )
    {
        //
        // Either we failed or we're out of games.
        //
        if ( !pPathInfo )
        {
            return FileNotFound;
        }
        else
        {
            pDirectoryInfo->m_Name[0] = '\0';
            pDirectoryInfo->m_Type = IRadDrive::DirectoryInfo::IsDone;
        }
    }
    else
    {
        strncpy( pDirectoryInfo->m_Name, **handle, radFileFilenameMax );
        pDirectoryInfo->m_Name[ radFileFilenameMax ] = '\0';

        if ( pPathInfo->type == SDL_PATHTYPE_DIRECTORY )
        {
            pDirectoryInfo->m_Type = IRadDrive::DirectoryInfo::IsDirectory;
        }
        else
        {
            pDirectoryInfo->m_Type = IRadDrive::DirectoryInfo::IsFile;
        }
    }
    return Success;
}
#endif
