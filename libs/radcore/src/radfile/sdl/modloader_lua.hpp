#ifndef MODLOADER_LUA_HPP
#define MODLOADER_LUA_HPP

#include <string>
#include <vector>

struct ModPathHandlerRule
{
    std::string pattern;
    std::string scriptPath;
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

private:
    ModLoaderLua();
    ~ModLoaderLua();

    bool InitLuaState();
    void CloseLuaState();
    bool MatchPattern( const char* pattern, const char* path );
    bool EnsureDirectoryExists( const std::string& path );

    void* m_pLuaState;
    bool m_initialized;
    std::string m_modsDir;
    std::string m_activeModName;
    std::string m_activeModPath;
    std::string m_gameDirPath;
    std::string m_cacheDir;

    std::vector<ModPathHandlerRule> m_rules;
};

#endif // MODLOADER_LUA_HPP
