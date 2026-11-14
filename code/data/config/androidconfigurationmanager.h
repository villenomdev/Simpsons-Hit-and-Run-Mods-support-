#ifndef ANDROID_CONFIGURATION_MANAGER_H
#define ANDROID_CONFIGURATION_MANAGER_H

class AndroidConfigurationManager
{
public:
    static AndroidConfigurationManager& GetInstance();

    void Initialize();
    void Reload();
    void Save();
    void SaveIfDirty();

    bool IsInvertCameraEnabled() const;
    bool IsGamepadVibrationEnabled() const;
    bool IsPhoneVibrationEnabled() const;
    bool IsModsEnabled() const;
    bool IsSafeFrontendEnabled() const;
    bool IsGamelogEnabled() const;
    const char* GetModsPath() const;
    const char* GetActiveMod() const;

    void SetInvertCameraEnabled( bool enabled );
    void SetGamepadVibrationEnabled( bool enabled );
    void SetPhoneVibrationEnabled( bool enabled );
    void SetModsEnabled( bool enabled );
    void SetSafeFrontendEnabled( bool enabled );
    void SetGamelogEnabled( bool enabled );
    void SetModsPath( const char* path );
    void SetActiveMod( const char* modName );

private:
    AndroidConfigurationManager();

    void SetDefaults();

#if defined(RAD_ANDROID)
    bool BuildPaths();
    bool EnsureDirectory();
    bool LoadConfigurationFile();
    int  LoadVersionFile();
    void WriteConfigurationFile();
    void WriteVersionFile();
    

    bool ParseBool( const char* value, bool defaultValue ) const;
    void TrimLine( char* text ) const;
    void ParseLine( const char* line );
#endif

private:
    bool m_initialized;

    bool m_invertCamera;
    bool m_gamepadVibration;
    bool m_phoneVibration;
    bool m_enableMods;
    bool m_safeFrontend;
    bool m_enableGamelog;
    bool m_dirty;

    char m_modsPath[ 512 ];
    char m_activeMod[ 256 ];
    

#if defined(RAD_ANDROID)
    char m_directoryPath[ 512 ];
    char m_configurationFilePath[ 512 ];
    char m_versionFilePath[ 512 ];
#endif
};

AndroidConfigurationManager* GetAndroidConfigurationManager();

#endif // ANDROID_CONFIGURATION_MANAGER_H