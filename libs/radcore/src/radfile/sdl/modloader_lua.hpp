#ifndef MODLOADER_LUA_HPP
#define MODLOADER_LUA_HPP

#include <string>
#include <vector>
#include <map>

struct ModPathHandlerRule
{
    std::string pattern;
    std::string scriptPath;
};

struct ModSetting
{
    std::string name;
    std::string type;
    std::string defaultValue;
    std::string currentValue;
};

class ModLoaderLua
{
public:
    static ModLoaderLua& GetInstance();

    void Initialize( const char* modsDir, const char* activeMod, const char* gameDir );
    void Reset();

    void AddPathHandler( const std::string& pattern, const std::string& scriptPath );
    void ClearPathHandlers();

    bool HandleFileRequest( const char* cleanFileName, std::string& outResolvedPath );

    std::string ResolveGameDataPath( const std::string& path );

    const std::string& GetActiveModPath() const { return m_activeModPath; }
    const std::string& GetGameDirPath() const { return m_gameDirPath; }
    const std::string& GetActiveModName() const { return m_activeModName; }
    const std::string& GetModTitle() const { return m_modTitle; }
    const std::string& GetModVersion() const { return m_modVersion; }

    bool GetSetting( const std::string& name, ModSetting& outSetting ) const;
    bool EnsureDirectoryExists( const std::string& path );

private:
    ModLoaderLua();
    ~ModLoaderLua();

    bool InitLuaState();
    void CloseLuaState();
    bool MatchPattern( const char* pattern, const char* path );
    void ParseMetaIni( const std::string& metaPath );

    void* m_pLuaState;
    bool m_initialized;
    std::string m_modsDir;
    std::string m_activeModName;
    std::string m_activeModPath;
    std::string m_gameDirPath;
    std::string m_cacheDir;

    std::string m_modTitle;
    std::string m_modVersion;
    std::map<std::string, ModSetting> m_settings;

    std::vector<ModPathHandlerRule> m_rules;
};

#endif // MODLOADER_LUA_HPP
