#ifndef MOD_MANAGER_HPP
#define MOD_MANAGER_HPP

#include <string>
#include <vector>
#include <strings.h>
#include <cstring>

struct ModEntry
{
    std::string name;          // e.g. "Some Law Enforcement", "AllCars", "None"
    std::string displayName;   // Title from Meta.ini or friendly name
    std::string description;   // Description from Meta.ini
    std::string version;       // Version from Meta.ini
    std::string fullPath;      // Disk path to directory or archive
    bool isArchive;            // true if .lmlm or .zip
    bool isActive;             // true if currently active
};

typedef void (*ModPersistenceCallback)( const char* modName );
typedef void (*ModLogCallback)( const char* category, const char* message );
typedef void (*ModProgressCallback)( float progress, const char* currentFile, void* userData );

class ModManager
{
public:
    static ModManager& GetInstance();

    void SetPersistenceCallback( ModPersistenceCallback cb ) { m_persistenceCallback = cb; }
    void SetLogCallback( ModLogCallback cb ) { m_logCallback = cb; }
    void NotifyLog( const char* category, const char* fmt, ... );

    // Scan mods directory (/storage/emulated/0/Simpsons Hit and Run/mods/)
    // Discovers subdirectories, .lmlm files, and .zip files.
    void ScanAvailableMods();
    void RescanMods() { ScanAvailableMods(); }

    const std::vector<ModEntry>& GetAvailableMods() const { return m_mods; }
    int GetModCount() const { return (int)m_mods.size(); }
    const ModEntry* GetMod( int index ) const { if ( index >= 0 && index < (int)m_mods.size() ) return &m_mods[index]; return NULL; }
    int GetActiveModIndex() const { return m_activeModIndex; }
    const char* GetActiveModName() const { return m_activeModName; }
    const char* GetEffectiveActiveModDir() const { return m_effectiveModDir; }
    bool HasActiveMod() const { return m_activeModIndex > 0 && m_activeModName[0] != '\0' && strcasecmp(m_activeModName, "None") != 0; }
    const char* GetLastError() const { return m_lastError; }

    // Activates a mod by name (e.g. "Some Law Enforcement", or "" / "None" for Vanilla).
    // Deactivates any previous mod.
    // If it is an archive (.lmlm or .zip), extracts to .mod_cache/<name> if needed.
    // Persists to config.ini immediately and reinitializes Lucas Mod Loader Lua.
    bool SetActiveMod( const char* modName, ModProgressCallback pfnProgress = NULL, void* userData = NULL );
    bool SetActiveMod( int index, ModProgressCallback pfnProgress = NULL, void* userData = NULL ) { if ( index >= 0 && index < (int)m_mods.size() ) return SetActiveMod( m_mods[index].name.c_str(), pfnProgress, userData ); return false; }

    // Cycle to previous / next mod (-1 or +1)
    void CycleMod( int direction );

    // Called on boot to initialize mods from AndroidConfigurationManager
    void InitializeOnBoot( const char* baseModsDir, const char* activeModFromConfig );

    // Helper to get active mod display string for Options menu line
    // e.g. "Mods: [Some Law Enforcement]" or "Mods: [None]"
    void GetOptionsDisplayString( char* outBuf, size_t outSize ) const;

private:
    ModManager();
    ~ModManager();

    void ParseMetaIniInfo( const std::string& folderOrArchive, ModEntry& entry );

    std::vector<ModEntry> m_mods;
    int m_activeModIndex;
    char m_activeModName[256];
    char m_effectiveModDir[1024];
    char m_baseModsDir[512];
    char m_lastError[512];
    bool m_scanned;
    bool m_bootInitialized;
    ModPersistenceCallback m_persistenceCallback;
    ModLogCallback m_logCallback;
};

#endif // MOD_MANAGER_HPP
