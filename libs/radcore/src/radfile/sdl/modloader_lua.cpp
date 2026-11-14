#include "modloader_lua.hpp"

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

#include <fnmatch.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <vector>

#if defined(RAD_ANDROID)
#include <android/log.h>
#define MOD_LOGI(...) __android_log_print(ANDROID_LOG_INFO, "SimpsonsModsLua", __VA_ARGS__)
#define MOD_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "SimpsonsModsLua", __VA_ARGS__)
#else
#define MOD_LOGI(...) printf(__VA_ARGS__)
#define MOD_LOGE(...) fprintf(stderr, __VA_ARGS__)
#endif

// Per-invocation execution state
static std::string s_CurrentOutput;
static std::string s_CurrentVirtualPath;
static std::string s_CurrentRedirection;

//------------------------------------------------------------------------------
// Lua Host Callbacks
//------------------------------------------------------------------------------

static int Lua_Output( lua_State* L )
{
    int nargs = lua_gettop( L );
    for( int i = 1; i <= nargs; i++ )
    {
        size_t len = 0;
        const char* s = lua_tolstring( L, i, &len );
        if( s != NULL && len > 0 )
        {
            s_CurrentOutput.append( s, len );
        }
    }
    if( !s_CurrentOutput.empty() && s_CurrentOutput.back() != '\n' )
    {
        s_CurrentOutput.push_back( '\n' );
    }
    return 0;
}

static int Lua_GetPath( lua_State* L )
{
    lua_pushstring( L, s_CurrentVirtualPath.c_str() );
    return 1;
}

static int Lua_GetModPath( lua_State* L )
{
    lua_pushstring( L, ModLoaderLua::GetInstance().GetActiveModPath().c_str() );
    return 1;
}

static int Lua_GetModName( lua_State* L )
{
    lua_pushstring( L, ModLoaderLua::GetInstance().GetActiveModName().c_str() );
    return 1;
}

static int Lua_GetSetting( lua_State* L )
{
    luaL_checkstring( L, 1 );
    lua_pushnumber( L, 0 );
    return 1;
}

static int Lua_GetSettings( lua_State* L )
{
    lua_newtable( L );
    return 1;
}

static int Lua_IsHackLoaded( lua_State* L )
{
    lua_pushboolean( L, 1 );
    return 1;
}

static int Lua_IsModEnabled( lua_State* L )
{
    const char* name = luaL_checkstring( L, 1 );
    bool enabled = ( strcasecmp( name, ModLoaderLua::GetInstance().GetActiveModName().c_str() ) == 0 );
    lua_pushboolean( L, enabled ? 1 : 0 );
    return 1;
}

static int Lua_GetEnabledMods( lua_State* L )
{
    if( lua_isfunction( L, 1 ) )
    {
        lua_pushvalue( L, 1 );
        lua_pushstring( L, ModLoaderLua::GetInstance().GetActiveModName().c_str() );
        lua_pcall( L, 1, 0, 0 );
    }
    return 0;
}

static int Lua_Redirect( lua_State* L )
{
    const char* p = luaL_checkstring( L, 1 );
    if( p != NULL )
    {
        s_CurrentRedirection = p;
    }
    return 0;
}

static int Lua_Confirm( lua_State* L )
{
    lua_pushboolean( L, 1 );
    return 1;
}

static int Lua_Alert( lua_State* L )
{
    const char* msg = luaL_optstring( L, 1, "" );
    MOD_LOGI("[Lua Alert] %s", msg);
    return 0;
}

static int Lua_ReadFile( lua_State* L )
{
    const char* path = luaL_checkstring( L, 1 );
    std::string fullPath = ModLoaderLua::GetInstance().ResolveGameDataPath( path );
    FILE* f = fopen( fullPath.c_str(), "rb" );
    if( f == NULL )
    {
        lua_pushnil( L );
        return 1;
    }
    fseek( f, 0, SEEK_END );
    long sz = ftell( f );
    fseek( f, 0, SEEK_SET );
    if( sz < 0 )
    {
        fclose( f );
        lua_pushnil( L );
        return 1;
    }
    std::vector<char> buf( sz );
    if( sz > 0 )
    {
        fread( buf.data(), 1, sz, f );
    }
    fclose( f );
    lua_pushlstring( L, buf.data(), sz );
    return 1;
}

static int Lua_ReadFileOffset( lua_State* L )
{
    const char* path = luaL_checkstring( L, 1 );
    long offset = (long)luaL_checknumber( L, 2 );
    long length = (long)luaL_checknumber( L, 3 );
    std::string fullPath = ModLoaderLua::GetInstance().ResolveGameDataPath( path );
    FILE* f = fopen( fullPath.c_str(), "rb" );
    if( f == NULL )
    {
        lua_pushnil( L );
        return 1;
    }
    fseek( f, offset, SEEK_SET );
    std::vector<char> buf( length > 0 ? length : 0 );
    size_t rd = 0;
    if( length > 0 )
    {
        rd = fread( buf.data(), 1, length, f );
    }
    fclose( f );
    lua_pushlstring( L, buf.data(), rd );
    return 1;
}

static int Lua_WriteFile( lua_State* L )
{
    const char* path = luaL_checkstring( L, 1 );
    size_t len = 0;
    const char* data = luaL_checklstring( L, 2, &len );
    FILE* f = fopen( path, "wb" );
    if( f != NULL )
    {
        if( len > 0 ) fwrite( data, 1, len, f );
        fclose( f );
        lua_pushboolean( L, 1 );
    }
    else
    {
        lua_pushboolean( L, 0 );
    }
    return 1;
}

static int Lua_PathExists( lua_State* L )
{
    const char* path = luaL_checkstring( L, 1 );
    std::string fullPath = ModLoaderLua::GetInstance().ResolveGameDataPath( path );
    lua_pushboolean( L, ( access( fullPath.c_str(), F_OK ) == 0 ) ? 1 : 0 );
    return 1;
}

static int Lua_DirectoryGetEntries( lua_State* L )
{
    const char* dir = luaL_checkstring( L, 1 );
    if( !lua_isfunction( L, 2 ) )
    {
        return 0;
    }
    std::string fullDir = ModLoaderLua::GetInstance().ResolveGameDataPath( dir );
    DIR* d = opendir( fullDir.c_str() );
    if( d == NULL )
    {
        return 0;
    }
    struct dirent* ent;
    while( ( ent = readdir( d ) ) != NULL )
    {
        if( strcmp( ent->d_name, "." ) == 0 || strcmp( ent->d_name, ".." ) == 0 )
        {
            continue;
        }
        lua_pushvalue( L, 2 );
        lua_pushstring( L, ent->d_name );
        lua_pushboolean( L, ( ent->d_type == DT_DIR ) ? 1 : 0 );
        if( lua_pcall( L, 2, 1, 0 ) == LUA_OK )
        {
            int cont = lua_toboolean( L, -1 );
            lua_pop( L, 1 );
            if( !cont )
            {
                break;
            }
        }
        else
        {
            lua_pop( L, 1 );
            break;
        }
    }
    closedir( d );
    return 0;
}

static int Lua_GetFileName( lua_State* L )
{
    const char* p = luaL_checkstring( L, 1 );
    std::string s( p );
    size_t pos = s.find_last_of( "/\\" );
    if( pos != std::string::npos ) s = s.substr( pos + 1 );
    lua_pushstring( L, s.c_str() );
    return 1;
}

static int Lua_GetPathParent( lua_State* L )
{
    const char* p = luaL_checkstring( L, 1 );
    std::string s( p );
    size_t pos = s.find_last_of( "/\\" );
    if( pos != std::string::npos ) s = s.substr( 0, pos );
    else s = "";
    lua_pushstring( L, s.c_str() );
    return 1;
}

static int Lua_GetFileExtension( lua_State* L )
{
    const char* p = luaL_checkstring( L, 1 );
    std::string s( p );
    size_t pos = s.find_last_of( '.' );
    if( pos != std::string::npos ) s = s.substr( pos + 1 );
    else s = "";
    lua_pushstring( L, s.c_str() );
    return 1;
}

static int Lua_RemoveFileExtension( lua_State* L )
{
    const char* p = luaL_checkstring( L, 1 );
    std::string s( p );
    size_t pos = s.find_last_of( '.' );
    if( pos != std::string::npos ) s = s.substr( 0, pos );
    lua_pushstring( L, s.c_str() );
    return 1;
}

static int Lua_FixSlashes( lua_State* L )
{
    const char* p = luaL_checkstring( L, 1 );
    std::string s( p );
    for( size_t i = 0; i < s.size(); i++ )
    {
        if( s[i] == '\\' ) s[i] = '/';
    }
    lua_pushstring( L, s.c_str() );
    return 1;
}

static int Lua_ComparePaths( lua_State* L )
{
    const char* a = luaL_checkstring( L, 1 );
    const char* b = luaL_checkstring( L, 2 );
    lua_pushboolean( L, ( strcasecmp( a, b ) == 0 ) ? 1 : 0 );
    return 1;
}

static int Lua_WildcardMatch( lua_State* L )
{
    const char* pat = luaL_checkstring( L, 1 );
    const char* str = luaL_checkstring( L, 2 );
    lua_pushboolean( L, ( fnmatch( pat, str, FNM_CASEFOLD ) == 0 ) ? 1 : 0 );
    return 1;
}

//------------------------------------------------------------------------------
// ModLoaderLua Class Implementation
//------------------------------------------------------------------------------

ModLoaderLua& ModLoaderLua::GetInstance()
{
    static ModLoaderLua s_instance;
    return s_instance;
}

ModLoaderLua::ModLoaderLua()
    : m_pLuaState( NULL )
    , m_initialized( false )
{
}

ModLoaderLua::~ModLoaderLua()
{
    CloseLuaState();
}

void ModLoaderLua::CloseLuaState()
{
    if( m_pLuaState != NULL )
    {
        lua_close( (lua_State*)m_pLuaState );
        m_pLuaState = NULL;
    }
    m_initialized = false;
}

void ModLoaderLua::Reset()
{
    CloseLuaState();
    m_rules.clear();
    m_modsDir.clear();
    m_activeModName.clear();
    m_activeModPath.clear();
    m_gameDirPath.clear();
    m_cacheDir.clear();
}

void ModLoaderLua::AddPathHandler( const std::string& pattern, const std::string& scriptPath )
{
    ModPathHandlerRule rule;
    rule.pattern = pattern;
    rule.scriptPath = scriptPath;
    m_rules.push_back( rule );
    MOD_LOGI("[ModLoaderLua] Registered PathHandler: '%s' -> '%s'", pattern.c_str(), scriptPath.c_str());
}

void ModLoaderLua::ClearPathHandlers()
{
    m_rules.clear();
}

bool ModLoaderLua::EnsureDirectoryExists( const std::string& path )
{
    if( path.empty() ) return false;
    std::string current;
    for( size_t i = 0; i < path.size(); i++ )
    {
        if( path[i] == '/' || path[i] == '\\' )
        {
            if( !current.empty() )
            {
                mkdir( current.c_str(), 0777 );
            }
        }
        current.push_back( path[i] );
    }
    mkdir( current.c_str(), 0777 );
    return true;
}

bool ModLoaderLua::InitLuaState()
{
    if( m_pLuaState != NULL )
    {
        return true;
    }

    lua_State* L = luaL_newstate();
    if( L == NULL )
    {
        MOD_LOGE("[ModLoaderLua] Failed to create Lua state!");
        return false;
    }
    luaL_openlibs( L );

    // Register all Lucas Mod Launcher global functions
    lua_register( L, "Output", Lua_Output );
    lua_register( L, "GetPath", Lua_GetPath );
    lua_register( L, "GetModPath", Lua_GetModPath );
    lua_register( L, "GetModName", Lua_GetModName );
    lua_register( L, "GetSetting", Lua_GetSetting );
    lua_register( L, "GetSettings", Lua_GetSettings );
    lua_register( L, "IsHackLoaded", Lua_IsHackLoaded );
    lua_register( L, "IsModEnabled", Lua_IsModEnabled );
    lua_register( L, "GetEnabledMods", Lua_GetEnabledMods );
    lua_register( L, "Redirect", Lua_Redirect );
    lua_register( L, "Confirm", Lua_Confirm );
    lua_register( L, "Alert", Lua_Alert );
    lua_register( L, "ReadFile", Lua_ReadFile );
    lua_register( L, "ReadFileOffset", Lua_ReadFileOffset );
    lua_register( L, "WriteFile", Lua_WriteFile );
    lua_register( L, "PathExists", Lua_PathExists );
    lua_register( L, "DirectoryGetEntries", Lua_DirectoryGetEntries );
    lua_register( L, "GetFileName", Lua_GetFileName );
    lua_register( L, "GetPathParent", Lua_GetPathParent );
    lua_register( L, "GetFileExtension", Lua_GetFileExtension );
    lua_register( L, "RemoveFileExtension", Lua_RemoveFileExtension );
    lua_register( L, "FixSlashes", Lua_FixSlashes );
    lua_register( L, "ComparePaths", Lua_ComparePaths );
    lua_register( L, "WildcardMatch", Lua_WildcardMatch );

    m_pLuaState = L;

    // Run CustomFiles.lua if present
    std::string cfLua = m_activeModPath + "/CustomFiles.lua";
    if( access( cfLua.c_str(), R_OK ) != 0 )
    {
        cfLua = m_activeModPath + "/" + m_activeModName + "/CustomFiles.lua";
    }

    if( access( cfLua.c_str(), R_OK ) == 0 )
    {
        MOD_LOGI("[ModLoaderLua] Executing CustomFiles.lua: '%s'", cfLua.c_str());
        int res = luaL_dofile( L, cfLua.c_str() );
        if( res != LUA_OK )
        {
            const char* err = lua_tostring( L, -1 );
            MOD_LOGE("[ModLoaderLua] CustomFiles.lua error: %s", err ? err : "unknown");
            lua_pop( L, 1 );
        }
        else
        {
            MOD_LOGI("[ModLoaderLua] CustomFiles.lua executed successfully");
        }
    }

    return true;
}

void ModLoaderLua::Initialize( const char* modsDir, const char* activeMod, const char* gameDir )
{
    Reset();

    if( modsDir != NULL ) m_modsDir = modsDir;
    if( activeMod != NULL ) m_activeModName = activeMod;
    if( gameDir != NULL ) m_gameDirPath = gameDir;

    m_activeModPath = m_modsDir + "/" + m_activeModName;
    m_cacheDir = "/storage/emulated/0/Simpsons Hit and Run/files/.mod_cache";
    EnsureDirectoryExists( m_cacheDir );

    MOD_LOGI("[ModLoaderLua] Initialized for mod '%s' at '%s'", m_activeModName.c_str(), m_activeModPath.c_str());

    InitLuaState();
    m_initialized = true;
}

std::string ModLoaderLua::ResolveGameDataPath( const std::string& path )
{
    std::string clean = path;
    if( clean.rfind( "/GameData/", 0 ) == 0 )
    {
        clean = clean.substr( 10 );
    }
    else if( clean.rfind( "GameData/", 0 ) == 0 )
    {
        clean = clean.substr( 9 );
    }

    // Check in active mod CustomFiles
    std::string p1 = m_activeModPath + "/CustomFiles/" + clean;
    if( access( p1.c_str(), F_OK ) == 0 ) return p1;

    // Check in active mod AdditionalFiles
    std::string p2 = m_activeModPath + "/AdditionalFiles/" + clean;
    if( access( p2.c_str(), F_OK ) == 0 ) return p2;

    // Check in active mod root
    std::string p3 = m_activeModPath + "/" + clean;
    if( access( p3.c_str(), F_OK ) == 0 ) return p3;

    // Check in game directory
    if( !m_gameDirPath.empty() )
    {
        std::string p4 = m_gameDirPath + "/" + clean;
        if( access( p4.c_str(), F_OK ) == 0 ) return p4;
    }

    // Default to CustomFiles
    return p1;
}

bool ModLoaderLua::MatchPattern( const char* pattern, const char* path )
{
    if( pattern == NULL || path == NULL ) return false;
    if( strcasecmp( pattern, path ) == 0 ) return true;
    return ( fnmatch( pattern, path, FNM_CASEFOLD ) == 0 );
}

bool ModLoaderLua::HandleFileRequest( const char* cleanFileName, std::string& outResolvedPath )
{
    if( !m_initialized || cleanFileName == NULL || cleanFileName[0] == '\0' )
    {
        return false;
    }

    for( size_t i = 0; i < m_rules.size(); i++ )
    {
        const ModPathHandlerRule& rule = m_rules[i];
        if( MatchPattern( rule.pattern.c_str(), cleanFileName ) )
        {
            MOD_LOGI("[ModLoaderLua] File '%s' matched PathHandler '%s' -> '%s'",
                     cleanFileName, rule.pattern.c_str(), rule.scriptPath.c_str());

            // Resolve full script path
            std::string scriptFile = m_activeModPath + "/" + rule.scriptPath;
            if( access( scriptFile.c_str(), R_OK ) != 0 )
            {
                scriptFile = m_activeModPath + "/CustomFiles/" + rule.scriptPath;
            }
            if( access( scriptFile.c_str(), R_OK ) != 0 )
            {
                scriptFile = m_activeModPath + "/" + m_activeModName + "/" + rule.scriptPath;
            }

            if( access( scriptFile.c_str(), R_OK ) != 0 )
            {
                MOD_LOGE("[ModLoaderLua] Script not found: '%s'", scriptFile.c_str());
                continue;
            }

            InitLuaState();
            lua_State* L = (lua_State*)m_pLuaState;
            if( L == NULL ) return false;

            s_CurrentOutput.clear();
            s_CurrentVirtualPath = cleanFileName;
            s_CurrentRedirection.clear();

            int status = luaL_dofile( L, scriptFile.c_str() );
            if( status != LUA_OK )
            {
                const char* err = lua_tostring( L, -1 );
                MOD_LOGE("[ModLoaderLua] Execution error in '%s': %s", scriptFile.c_str(), err ? err : "unknown");
                lua_pop( L, 1 );
                continue;
            }

            // Check if handler called Redirect(...)
            if( !s_CurrentRedirection.empty() )
            {
                std::string redirected = ResolveGameDataPath( s_CurrentRedirection );
                MOD_LOGI("[ModLoaderLua] Handler redirected '%s' -> '%s'", cleanFileName, redirected.c_str());
                outResolvedPath = redirected;
                return true;
            }

            // Check if handler called Output(...)
            if( !s_CurrentOutput.empty() )
            {
                std::string cacheFile = m_cacheDir + "/" + cleanFileName;
                size_t slash = cacheFile.find_last_of( "/\\" );
                if( slash != std::string::npos )
                {
                    EnsureDirectoryExists( cacheFile.substr( 0, slash ) );
                }

                FILE* cf = fopen( cacheFile.c_str(), "wb" );
                if( cf != NULL )
                {
                    fwrite( s_CurrentOutput.data(), 1, s_CurrentOutput.size(), cf );
                    fclose( cf );
                    MOD_LOGI("[ModLoaderLua] Generated cached virtual file: '%s' (%zu bytes)",
                             cacheFile.c_str(), s_CurrentOutput.size());
                    outResolvedPath = cacheFile;
                    return true;
                }
            }
        }
    }

    return false;
}
