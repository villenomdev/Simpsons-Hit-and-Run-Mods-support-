#include <data/config/androidconfigurationmanager.h>
#include <input/touch/touchcameracontroller.h>
#include <stdio.h>
#include <string.h>

#if defined(RAD_ANDROID)
    #include <SDL.h>
    #include <sys/stat.h>
    #include <errno.h>

extern void radSdlDriveSetModsConfig( bool enabled, const char* modsDir, const char* activeMod );
#else
void radSdlDriveSetModsConfig( bool, const char*, const char* ) {}
#endif

namespace
{
    const int CURRENT_CONFIGURATION_VERSION = 1;

#if defined(RAD_ANDROID)
    const char* CONFIGURATION_FOLDER_NAME = "Simpsons_configuration";
    const char* CONFIGURATION_FILE_NAME   = "Simpsons_configuration.txt";
    const char* VERSION_FILE_NAME         = ".versionSimpsons_configuration";
#endif
}



AndroidConfigurationManager& AndroidConfigurationManager::GetInstance()
{
    static AndroidConfigurationManager sInstance;
    return sInstance;
}

AndroidConfigurationManager* GetAndroidConfigurationManager()
{
    return &AndroidConfigurationManager::GetInstance();
}

AndroidConfigurationManager::AndroidConfigurationManager()
:
    m_initialized( false ),
    m_invertCamera( true ),
    m_gamepadVibration( true ),
    m_phoneVibration( false ),
    m_enableMods( true ),
    m_dirty( false )
{
    strncpy( m_modsPath, "/storage/emulated/0/simpsons_mods", sizeof( m_modsPath ) );
    m_modsPath[ sizeof( m_modsPath ) - 1 ] = '\0';
    m_activeMod[ 0 ] = '\0';

#if defined(RAD_ANDROID)
    m_directoryPath[ 0 ] = '\0';
    m_configurationFilePath[ 0 ] = '\0';
    m_versionFilePath[ 0 ] = '\0';
#endif
}

void AndroidConfigurationManager::SetDefaults()
{
    m_invertCamera = true;
    m_gamepadVibration = true;
    m_phoneVibration = false;
    m_enableMods = true;
    strncpy( m_modsPath, "/storage/emulated/0/simpsons_mods", sizeof( m_modsPath ) );
    m_modsPath[ sizeof( m_modsPath ) - 1 ] = '\0';
    m_activeMod[ 0 ] = '\0';
    m_dirty = false;
}

void AndroidConfigurationManager::Initialize()
{
    if( m_initialized )
    {
        return;
    }

    this->SetDefaults();

#if defined(RAD_ANDROID)
    if( !this->BuildPaths() )
    {
        m_initialized = true;
        return;
    }

    this->EnsureDirectory();

    const bool loadedConfiguration = this->LoadConfigurationFile();
    const int loadedVersion = this->LoadVersionFile();

    // Ensure external mods folder exists
    mkdir( m_modsPath, 0777 );

    // Update radSdlDrive with mods settings
    radSdlDriveSetModsConfig( m_enableMods, m_modsPath, m_activeMod );

    //
    // If the file does not exist, or the version is old/missing,
    // rewrite the configuration while preserving any values already loaded.
    //
    if( !loadedConfiguration || loadedVersion != CURRENT_CONFIGURATION_VERSION )
    {
        this->WriteConfigurationFile();
        this->WriteVersionFile();
    }
#endif

    m_initialized = true;
}

void AndroidConfigurationManager::Reload()
{
    this->SetDefaults();

#if defined(RAD_ANDROID)
    if( !this->BuildPaths() )
    {
        return;
    }

    this->EnsureDirectory();
    this->LoadConfigurationFile();
#endif
}

void AndroidConfigurationManager::Save()
{
#if defined(RAD_ANDROID)
    if( !this->BuildPaths() )
    {
        return;
    }

    this->EnsureDirectory();
    this->WriteConfigurationFile();
    this->WriteVersionFile();
    m_dirty=false;
#endif
}

void AndroidConfigurationManager::SaveIfDirty()
{
#if defined(RAD_ANDROID)
    if( !m_dirty )
    {
        return;
    }

    this->Save();
#endif
}

bool AndroidConfigurationManager::IsInvertCameraEnabled() const
{
    return m_invertCamera;
}

bool AndroidConfigurationManager::IsGamepadVibrationEnabled() const
{
    return m_gamepadVibration;
}

bool AndroidConfigurationManager::IsPhoneVibrationEnabled() const
{
    return m_phoneVibration;
}

void AndroidConfigurationManager::SetInvertCameraEnabled( bool enabled )
{
    if( m_invertCamera == enabled )
    {
        return;
    }

    m_invertCamera = enabled;
    m_dirty = true;
}

void AndroidConfigurationManager::SetGamepadVibrationEnabled( bool enabled )
{
    if( m_gamepadVibration == enabled )
    {
        return;
    }

    m_gamepadVibration = enabled;
    m_dirty = true;
}

void AndroidConfigurationManager::SetPhoneVibrationEnabled( bool enabled )
{
    m_phoneVibration = enabled;
}

bool AndroidConfigurationManager::IsModsEnabled() const
{
    return m_enableMods;
}

const char* AndroidConfigurationManager::GetModsPath() const
{
    return m_modsPath;
}

const char* AndroidConfigurationManager::GetActiveMod() const
{
    return m_activeMod;
}

void AndroidConfigurationManager::SetModsEnabled( bool enabled )
{
    if( m_enableMods == enabled )
    {
        return;
    }
    m_enableMods = enabled;
    m_dirty = true;
    radSdlDriveSetModsConfig( m_enableMods, m_modsPath, m_activeMod );
}

void AndroidConfigurationManager::SetModsPath( const char* path )
{
    if( path == NULL )
    {
        return;
    }
    strncpy( m_modsPath, path, sizeof( m_modsPath ) - 1 );
    m_modsPath[ sizeof( m_modsPath ) - 1 ] = '\0';
    m_dirty = true;
    radSdlDriveSetModsConfig( m_enableMods, m_modsPath, m_activeMod );
}

void AndroidConfigurationManager::SetActiveMod( const char* modName )
{
    if( modName == NULL )
    {
        return;
    }
    strncpy( m_activeMod, modName, sizeof( m_activeMod ) - 1 );
    m_activeMod[ sizeof( m_activeMod ) - 1 ] = '\0';
    m_dirty = true;
    radSdlDriveSetModsConfig( m_enableMods, m_modsPath, m_activeMod );
}

#if defined(RAD_ANDROID)

bool AndroidConfigurationManager::BuildPaths()
{
    const char* basePath = SDL_AndroidGetExternalStoragePath();

    if( basePath == NULL || basePath[ 0 ] == '\0' )
    {
        basePath = SDL_AndroidGetInternalStoragePath();
    }

    if( basePath == NULL || basePath[ 0 ] == '\0' )
    {
        return false;
    }

    snprintf( m_directoryPath,
              sizeof( m_directoryPath ),
              "%s/%s",
              basePath,
              CONFIGURATION_FOLDER_NAME );

    snprintf( m_configurationFilePath,
              sizeof( m_configurationFilePath ),
              "%s/%s",
              m_directoryPath,
              CONFIGURATION_FILE_NAME );

    snprintf( m_versionFilePath,
              sizeof( m_versionFilePath ),
              "%s/%s",
              m_directoryPath,
              VERSION_FILE_NAME );

    return true;
}

bool AndroidConfigurationManager::EnsureDirectory()
{
    if( m_directoryPath[ 0 ] == '\0' )
    {
        return false;
    }

    const int result = mkdir( m_directoryPath, 0777 );

    if( result == 0 )
    {
        return true;
    }

    return errno == EEXIST;
}

bool AndroidConfigurationManager::LoadConfigurationFile()
{
    FILE* file = fopen( m_configurationFilePath, "r" );

    if( file == NULL )
    {
        return false;
    }

    char line[ 256 ];

    while( fgets( line, sizeof( line ), file ) != NULL )
    {
        this->TrimLine( line );

        if( line[ 0 ] == '\0' || line[ 0 ] == '#' )
        {
            continue;
        }

        this->ParseLine( line );
    }

    fclose( file );

    return true;
}

int AndroidConfigurationManager::LoadVersionFile()
{
    FILE* file = fopen( m_versionFilePath, "r" );

    if( file == NULL )
    {
        return 0;
    }

    int version = 0;
    fscanf( file, "%d", &version );

    fclose( file );

    return version;
}

void AndroidConfigurationManager::WriteConfigurationFile()
{
    FILE* file = fopen( m_configurationFilePath, "w" );

    if( file == NULL )
    {
        return;
    }

    fprintf( file, "# Simpsons Hit & Run Android - Configuration\n" );
    fprintf( file, "# Restart the game after changing this file.\n" );
    fprintf( file, "\n" );

    fprintf( file, "invert_camera=%s\n", m_invertCamera ? "true" : "false" );
    fprintf( file, "gamepad_vibration=%s\n", m_gamepadVibration ? "true" : "false" );
    fprintf( file, "\n" );
    fprintf( file, "# ------------------------------------------------------------\n" );
    fprintf( file, "# Mod Loader Settings\n" );
    fprintf( file, "# ------------------------------------------------------------\n" );
    fprintf( file, "# enable_mods: true/false (load mods and loose files)\n" );
    fprintf( file, "enable_mods=%s\n", m_enableMods ? "true" : "false" );
    fprintf( file, "# mods_directory: Folder path on internal storage for mods\n" );
    fprintf( file, "mods_directory=%s\n", m_modsPath );
    fprintf( file, "# active_mod: Name of active mod folder (leave empty to load files directly from mods_directory)\n" );
    fprintf( file, "active_mod=%s\n", m_activeMod );

    fclose( file );
}

void AndroidConfigurationManager::WriteVersionFile()
{
    FILE* file = fopen( m_versionFilePath, "w" );

    if( file == NULL )
    {
        return;
    }

    fprintf( file, "%d\n", CURRENT_CONFIGURATION_VERSION );

    fclose( file );
}

bool AndroidConfigurationManager::ParseBool( const char* value, bool defaultValue ) const
{
    if( value == NULL )
    {
        return defaultValue;
    }

    if( strcmp( value, "true" ) == 0 ||
        strcmp( value, "1" ) == 0 ||
        strcmp( value, "on" ) == 0 ||
        strcmp( value, "yes" ) == 0 )
    {
        return true;
    }

    if( strcmp( value, "false" ) == 0 ||
        strcmp( value, "0" ) == 0 ||
        strcmp( value, "off" ) == 0 ||
        strcmp( value, "no" ) == 0 )
    {
        return false;
    }

    return defaultValue;
}

void AndroidConfigurationManager::TrimLine( char* text ) const
{
    if( text == NULL )
    {
        return;
    }

    int length = static_cast<int>( strlen( text ) );

    while( length > 0 &&
           ( text[ length - 1 ] == '\n' ||
             text[ length - 1 ] == '\r' ||
             text[ length - 1 ] == ' '  ||
             text[ length - 1 ] == '\t' ) )
    {
        text[ length - 1 ] = '\0';
        length--;
    }

    char* start = text;

    while( *start == ' ' || *start == '\t' )
    {
        start++;
    }

    if( start != text )
    {
        memmove( text, start, strlen( start ) + 1 );
    }
}

void AndroidConfigurationManager::ParseLine( const char* line )
{
    char buffer[ 256 ];
    strncpy( buffer, line, sizeof( buffer ) );
    buffer[ sizeof( buffer ) - 1 ] = '\0';

    char* separator = strchr( buffer, '=' );

    if( separator == NULL )
    {
        return;
    }

    *separator = '\0';

    char* key = buffer;
    char* value = separator + 1;

    this->TrimLine( key );
    this->TrimLine( value );

    if( strcmp( key, "invert_camera" ) == 0 )
    {
        m_invertCamera = this->ParseBool( value, m_invertCamera );
    }
    else if( strcmp( key, "gamepad_vibration" ) == 0 )
    {
        m_gamepadVibration = this->ParseBool( value, m_gamepadVibration );
    }
    else if( strcmp( key, "phone_vibration" ) == 0 )
    {
        m_phoneVibration = this->ParseBool( value, m_phoneVibration );
    }
    else if( strcmp( key, "enable_mods" ) == 0 )
    {
        m_enableMods = this->ParseBool( value, m_enableMods );
    }
    else if( strcmp( key, "mods_directory" ) == 0 || strcmp( key, "mods_path" ) == 0 )
    {
        strncpy( m_modsPath, value, sizeof( m_modsPath ) - 1 );
        m_modsPath[ sizeof( m_modsPath ) - 1 ] = '\0';
    }
    else if( strcmp( key, "active_mod" ) == 0 || strcmp( key, "activate_mods" ) == 0 || strcmp( key, "active_mods" ) == 0 )
    {
        strncpy( m_activeMod, value, sizeof( m_activeMod ) - 1 );
        m_activeMod[ sizeof( m_activeMod ) - 1 ] = '\0';
    }
}

#endif // RAD_ANDROID