#include "mod_manager.hpp"
#include "mod_archive.hpp"
#include "modloader_lua.hpp"
#include <radfile.hpp>

extern void radSdlDriveSetModsConfig( bool enabled, const char* modsDir, const char* activeMod, bool safeFrontend );

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <algorithm>

#if defined(RAD_ANDROID)
#include <android/log.h>
#define LOG_TAG "SHAR_ModManager"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#else
#define LOGI(...) printf(__VA_ARGS__)
#define LOGE(...) fprintf(stderr, __VA_ARGS__)
#endif

ModManager& ModManager::GetInstance()
{
    static ModManager s_instance;
    return s_instance;
}

ModManager::ModManager()
:   m_activeModIndex( 0 ),
    m_scanned( false ),
    m_bootInitialized( false ),
    m_persistenceCallback( NULL ),
    m_logCallback( NULL )
{
    m_activeModName[ 0 ] = '\0';
    m_effectiveModDir[ 0 ] = '\0';
    m_lastError[ 0 ] = '\0';
    strncpy( m_baseModsDir, "/storage/emulated/0/Simpsons Hit and Run/mods", sizeof(m_baseModsDir) - 1 );
    m_baseModsDir[ sizeof(m_baseModsDir) - 1 ] = '\0';
}

ModManager::~ModManager()
{
}

void ModManager::NotifyLog( const char* category, const char* fmt, ... )
{
    char buf[512];
    va_list args;
    va_start( args, fmt );
    vsnprintf( buf, sizeof(buf), fmt, args );
    va_end( args );

    LOGI( "[%s] %s", category, buf );
    if( m_logCallback != NULL )
    {
        m_logCallback( category, buf );
    }
}

void ModManager::ParseMetaIniInfo( const std::string& folderOrArchive, ModEntry& entry )
{
    // Try to find Meta.ini in folder
    std::string metaPath = folderOrArchive + "/Meta.ini";
    FILE* f = fopen( metaPath.c_str(), "r" );
    if( !f )
    {
        // Try subfolder
        std::string subMeta = folderOrArchive + "/" + entry.name + "/Meta.ini";
        f = fopen( subMeta.c_str(), "r" );
    }

    if( f )
    {
        char line[512];
        while( fgets( line, sizeof(line), f ) )
        {
            char* p = line;
            while( *p == ' ' || *p == '\t' ) p++;
            char* eq = strchr( p, '=' );
            if( eq )
            {
                *eq = '\0';
                char* key = p;
                char* val = eq + 1;
                // trim val
                size_t vlen = strlen( val );
                while( vlen > 0 && ( val[vlen - 1] == '\r' || val[vlen - 1] == '\n' || val[vlen - 1] == ' ' ) )
                {
                    val[--vlen] = '\0';
                }
                if( strcasecmp( key, "Title" ) == 0 && entry.displayName == entry.name )
                {
                    entry.displayName = val;
                }
                else if( strcasecmp( key, "Description" ) == 0 )
                {
                    entry.description = val;
                }
                else if( strcasecmp( key, "Version" ) == 0 )
                {
                    entry.version = val;
                }
            }
        }
        fclose( f );
    }
}

void ModManager::ScanAvailableMods()
{
    m_mods.clear();

    // Entry 0 is always "None (Vanilla)"
    ModEntry noneEntry;
    noneEntry.name = "None";
    noneEntry.displayName = "None (Vanilla)";
    noneEntry.description = "Play original game without any mods loaded";
    noneEntry.version = "";
    noneEntry.fullPath = "";
    noneEntry.isArchive = false;
    noneEntry.isActive = ( m_activeModName[0] == '\0' || strcasecmp( m_activeModName, "None" ) == 0 );
    m_mods.push_back( noneEntry );

    DIR* dir = opendir( m_baseModsDir );
    if( dir != NULL )
    {
        std::vector<ModEntry> foundMods;
        struct dirent* ent;
        while( ( ent = readdir( dir ) ) != NULL )
        {
            if( ent->d_name[0] == '.' ) continue;
            if( strcasecmp( ent->d_name, "CustomFiles" ) == 0 ||
                strcasecmp( ent->d_name, "AdditionalFiles" ) == 0 ||
                strcasecmp( ent->d_name, ".mod_cache" ) == 0 )
            {
                continue;
            }

            char fullPath[1024];
            snprintf( fullPath, sizeof(fullPath), "%s/%s", m_baseModsDir, ent->d_name );
            struct stat st;
            if( stat( fullPath, &st ) != 0 ) continue;

            if( S_ISDIR( st.st_mode ) )
            {
                ModEntry entry;
                entry.name = ent->d_name;
                entry.displayName = ent->d_name;
                entry.fullPath = fullPath;
                entry.isArchive = false;
                entry.isActive = false;
                ParseMetaIniInfo( fullPath, entry );
                foundMods.push_back( entry );
            }
            else if( S_ISREG( st.st_mode ) )
            {
                const char* dot = strrchr( ent->d_name, '.' );
                if( dot != NULL && ( strcasecmp( dot, ".lmlm" ) == 0 || strcasecmp( dot, ".zip" ) == 0 ) )
                {
                    std::string baseName( ent->d_name, dot - ent->d_name );
                    ModEntry entry;
                    entry.name = baseName;
                    entry.displayName = baseName;
                    entry.fullPath = fullPath;
                    entry.isArchive = true;
                    entry.isActive = false;
                    // Check if already extracted in cache to parse Meta.ini
                    char cacheDir[1024];
                    snprintf( cacheDir, sizeof(cacheDir), "/storage/emulated/0/Simpsons Hit and Run/files/.mod_cache/%s", baseName.c_str() );
                    ParseMetaIniInfo( cacheDir, entry );
                    foundMods.push_back( entry );
                }
            }
        }
        closedir( dir );

        // Sort found mods alphabetically
        std::sort( foundMods.begin(), foundMods.end(), []( const ModEntry& a, const ModEntry& b ) {
            return strcasecmp( a.displayName.c_str(), b.displayName.c_str() ) < 0;
        });

        for( size_t i = 0; i < foundMods.size(); ++i )
        {
            m_mods.push_back( foundMods[i] );
        }
    }

    // Determine active index
    m_activeModIndex = 0;
    if( m_activeModName[0] != '\0' && strcasecmp( m_activeModName, "None" ) != 0 )
    {
        for( size_t i = 1; i < m_mods.size(); ++i )
        {
            if( strcasecmp( m_mods[i].name.c_str(), m_activeModName ) == 0 ||
                strcasecmp( m_mods[i].displayName.c_str(), m_activeModName ) == 0 )
            {
                m_activeModIndex = (int)i;
                m_mods[i].isActive = true;
                m_mods[0].isActive = false;
                break;
            }
        }
    }

    m_scanned = true;
    LOGI( "[ModManager] Discovered %u mods in '%s' (Active: '%s')",
          (unsigned int)m_mods.size() - 1, m_baseModsDir, m_activeModName[0] ? m_activeModName : "None" );
}

bool ModManager::SetActiveMod( const char* modName, ModProgressCallback pfnProgress, void* userData )
{
    m_lastError[ 0 ] = '\0';

    if( !m_scanned )
    {
        ScanAvailableMods();
    }

    if( modName == NULL || modName[0] == '\0' || strcasecmp( modName, "None" ) == 0 )
    {
        if( m_activeModIndex == 0 && m_activeModName[0] == '\0' && pfnProgress == NULL )
        {
            return true;
        }

        // Deactivate all mods (Vanilla mode)
        m_activeModIndex = 0;
        m_activeModName[0] = '\0';
        m_effectiveModDir[0] = '\0';

        for( size_t i = 0; i < m_mods.size(); ++i )
        {
            m_mods[i].isActive = ( i == 0 );
        }

        if( m_persistenceCallback != NULL )
        {
            m_persistenceCallback( "" );
        }

        ModLoaderLua::GetInstance().Reset();
        radSdlDriveSetModsConfig( true, m_baseModsDir, "", false );

        NotifyLog( "Mods", "Deactivated all mods (Vanilla mode active)" );
        return true;
    }

    // Look for matching mod
    int matchIdx = -1;
    for( size_t i = 1; i < m_mods.size(); ++i )
    {
        if( strcasecmp( m_mods[i].name.c_str(), modName ) == 0 ||
            strcasecmp( m_mods[i].displayName.c_str(), modName ) == 0 )
        {
            matchIdx = (int)i;
            break;
        }
    }

    if( matchIdx < 0 )
    {
        LOGE( "[ModManager] Mod '%s' not found in available mods list!\n", modName );
        snprintf( m_lastError, sizeof(m_lastError), "Mod '%s' not found in available mods list.", modName );
        return false;
    }

    ModEntry& selected = m_mods[matchIdx];

    // If this mod is already active, effectively mounted, and no re-extraction was requested via progress callback
    if( matchIdx == m_activeModIndex && m_activeModIndex > 0 &&
        m_effectiveModDir[0] != '\0' && pfnProgress == NULL &&
        access( m_effectiveModDir, R_OK ) == 0 )
    {
        return true;
    }

    if( selected.isArchive )
    {
        // Archive (.lmlm or .zip): extract to .mod_cache/<name>
        char cacheDir[1024];
        snprintf( cacheDir, sizeof(cacheDir), "/storage/emulated/0/Simpsons Hit and Run/files/.mod_cache/%s", selected.name.c_str() );

        if( !ModArchive::IsExtractionUpToDate( selected.fullPath.c_str(), cacheDir ) )
        {
            LOGI( "[ModManager] Unpacking mod archive '%s' to '%s'...\n", selected.fullPath.c_str(), cacheDir );
            NotifyLog( "Mods", "Unpacking mod archive '%s' to cache...", selected.name.c_str() );
            if( !ModArchive::ExtractArchive( selected.fullPath.c_str(), cacheDir, pfnProgress, userData ) )
            {
                LOGE( "[ModManager] Failed to extract archive '%s'!\n", selected.fullPath.c_str() );
                snprintf( m_lastError, sizeof(m_lastError), "Failed to unpack '%s'. Archive may be corrupt or storage is full.", selected.name.c_str() );
                NotifyLog( "Mods", "ERROR: %s", m_lastError );
                SetActiveMod( "None" );
                return false;
            }
        }
        else if( pfnProgress != NULL )
        {
            // Already up to date, signal 100%
            pfnProgress( 1.0f, "Cached", userData );
        }

        std::string effectiveRoot = ModArchive::FindEffectiveModRoot( cacheDir );
        if( effectiveRoot.empty() || access( effectiveRoot.c_str(), R_OK ) != 0 )
        {
            LOGE( "[ModManager] Effective mod root '%s' is invalid or unreadable!\n", effectiveRoot.c_str() );
            snprintf( m_lastError, sizeof(m_lastError), "Extracted files for '%s' are missing or unreadable.", selected.name.c_str() );
            NotifyLog( "Mods", "ERROR: %s", m_lastError );
            SetActiveMod( "None" );
            return false;
        }
        strncpy( m_effectiveModDir, effectiveRoot.c_str(), sizeof(m_effectiveModDir) - 1 );
        m_effectiveModDir[ sizeof(m_effectiveModDir) - 1 ] = '\0';
    }
    else
    {
        if( access( selected.fullPath.c_str(), R_OK ) != 0 )
        {
            LOGE( "[ModManager] Mod directory '%s' cannot be read!\n", selected.fullPath.c_str() );
            snprintf( m_lastError, sizeof(m_lastError), "Mod folder '%s' cannot be read.", selected.name.c_str() );
            NotifyLog( "Mods", "ERROR: %s", m_lastError );
            SetActiveMod( "None" );
            return false;
        }
        strncpy( m_effectiveModDir, selected.fullPath.c_str(), sizeof(m_effectiveModDir) - 1 );
        m_effectiveModDir[ sizeof(m_effectiveModDir) - 1 ] = '\0';
    }

    // Single active mod enforcement: reset all others
    for( size_t i = 0; i < m_mods.size(); ++i )
    {
        m_mods[i].isActive = ( (int)i == matchIdx );
    }

    m_activeModIndex = matchIdx;
    strncpy( m_activeModName, selected.name.c_str(), sizeof(m_activeModName) - 1 );
    m_activeModName[ sizeof(m_activeModName) - 1 ] = '\0';

    // Persist to configuration
    if( m_persistenceCallback != NULL )
    {
        m_persistenceCallback( m_activeModName );
    }

    // Reinitialize Lucas Mod Loader environment
    ModLoaderLua::GetInstance().Reset();
    ModLoaderLua::GetInstance().Initialize( m_effectiveModDir, m_activeModName, "/storage/emulated/0/Simpsons Hit and Run/files" );

    // Update SDL file drive redirection
    radSdlDriveSetModsConfig( true, m_baseModsDir, m_activeModName, false );

    NotifyLog( "Mods", "Active mod set to: '%s' (effective dir: '%s')", m_activeModName, m_effectiveModDir );
    return true;
}

void ModManager::CycleMod( int direction )
{
    if( !m_scanned )
    {
        ScanAvailableMods();
    }
    if( m_mods.empty() ) return;

    int newIdx = m_activeModIndex + direction;
    if( newIdx < 0 )
    {
        newIdx = (int)m_mods.size() - 1;
    }
    else if( newIdx >= (int)m_mods.size() )
    {
        newIdx = 0;
    }

    if( newIdx == 0 )
    {
        SetActiveMod( "None" );
    }
    else
    {
        SetActiveMod( m_mods[newIdx].name.c_str() );
    }
}

void ModManager::InitializeOnBoot( const char* baseModsDir, const char* activeModFromConfig )
{
    if( m_bootInitialized )
    {
        return;
    }
    m_bootInitialized = true;

    if( baseModsDir && baseModsDir[0] != '\0' )
    {
        strncpy( m_baseModsDir, baseModsDir, sizeof(m_baseModsDir) - 1 );
        m_baseModsDir[ sizeof(m_baseModsDir) - 1 ] = '\0';
    }

    if( activeModFromConfig && activeModFromConfig[0] != '\0' && strcasecmp( activeModFromConfig, "None" ) != 0 )
    {
        strncpy( m_activeModName, activeModFromConfig, sizeof(m_activeModName) - 1 );
        m_activeModName[ sizeof(m_activeModName) - 1 ] = '\0';
    }
    else
    {
        m_activeModName[0] = '\0';
    }

    ScanAvailableMods();

    if( m_activeModName[0] != '\0' )
    {
        SetActiveMod( m_activeModName );
    }
}

void ModManager::GetOptionsDisplayString( char* outBuf, size_t outSize ) const
{
    if( outBuf == NULL || outSize == 0 ) return;

    if( m_activeModIndex <= 0 || m_activeModName[0] == '\0' || strcasecmp( m_activeModName, "None" ) == 0 )
    {
        snprintf( outBuf, outSize, "Mods: [None]" );
    }
    else
    {
        const char* name = m_mods[m_activeModIndex].displayName.c_str();
        char shortName[32];
        if( strlen( name ) > 18 )
        {
            strncpy( shortName, name, 15 );
            shortName[15] = '.';
            shortName[16] = '.';
            shortName[17] = '.';
            shortName[18] = '\0';
            snprintf( outBuf, outSize, "Mods: [%s]", shortName );
        }
        else
        {
            snprintf( outBuf, outSize, "Mods: [%s]", name );
        }
    }
}
