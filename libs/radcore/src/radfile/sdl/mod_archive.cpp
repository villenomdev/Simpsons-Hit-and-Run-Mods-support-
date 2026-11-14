#include "mod_archive.hpp"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <unistd.h>
#include <string>
#include <vector>

#if defined(RAD_ANDROID)
#include <android/log.h>
#define LOG_TAG "SHAR_ModArchive"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#else
#define LOGI(...) printf(__VA_ARGS__)
#define LOGE(...) fprintf(stderr, __VA_ARGS__)
#endif

// Include miniz for ZIP extraction
#define MINIZ_NO_TIME
#include "miniz.h"

namespace ModArchive
{

static void EnsureDirectoryRecursive( const std::string& path )
{
    if( path.empty() ) return;
    char tmp[1024];
    snprintf( tmp, sizeof(tmp), "%s", path.c_str() );
    size_t len = strlen( tmp );
    if( tmp[len - 1] == '/' || tmp[len - 1] == '\\' )
    {
        tmp[len - 1] = '\0';
    }
    for( char* p = tmp + 1; *p; p++ )
    {
        if( *p == '/' || *p == '\\' )
        {
            char save = *p;
            *p = '\0';
            mkdir( tmp, 0777 );
            *p = save;
        }
    }
    mkdir( tmp, 0777 );
}

static std::string GetParentDir( const std::string& filePath )
{
    size_t pos = filePath.find_last_of( "/\\" );
    if( pos == std::string::npos ) return "";
    return filePath.substr( 0, pos );
}

bool IsLSPArchive( const char* filePath )
{
    if( filePath == NULL || filePath[0] == '\0' ) return false;
    FILE* f = fopen( filePath, "rb" );
    if( !f ) return false;
    char magic[4];
    size_t read = fread( magic, 1, 4, f );
    fclose( f );
    return ( read == 4 && memcmp( magic, "LSPA", 4 ) == 0 );
}

bool IsZipArchive( const char* filePath )
{
    if( filePath == NULL || filePath[0] == '\0' ) return false;
    FILE* f = fopen( filePath, "rb" );
    if( !f ) return false;
    unsigned char magic[4];
    size_t read = fread( magic, 1, 4, f );
    fclose( f );
    return ( read == 4 && magic[0] == 'P' && magic[1] == 'K' && magic[2] == 0x03 && magic[3] == 0x04 );
}

bool IsSupportedArchive( const char* filePath )
{
    if( filePath == NULL || filePath[0] == '\0' ) return false;
    const char* dot = strrchr( filePath, '.' );
    if( !dot ) return false;
    if( strcasecmp( dot, ".lmlm" ) == 0 || strcasecmp( dot, ".zip" ) == 0 )
    {
        return IsLSPArchive( filePath ) || IsZipArchive( filePath );
    }
    return false;
}

bool IsExtractionUpToDate( const char* archivePath, const char* targetDir )
{
    if( archivePath == NULL || targetDir == NULL ) return false;
    struct stat archSt;
    if( stat( archivePath, &archSt ) != 0 ) return false;

    char stampPath[1024];
    snprintf( stampPath, sizeof(stampPath), "%s/.extracted_stamp", targetDir );
    FILE* sf = fopen( stampPath, "r" );
    if( !sf ) return false;

    long long savedSize = 0;
    long long savedMtime = 0;
    if( fscanf( sf, "%lld %lld", &savedSize, &savedMtime ) != 2 )
    {
        fclose( sf );
        return false;
    }
    fclose( sf );

    return ( savedSize == (long long)archSt.st_size && savedMtime == (long long)archSt.st_mtime );
}

static void WriteExtractionStamp( const char* archivePath, const char* targetDir )
{
    struct stat archSt;
    if( stat( archivePath, &archSt ) != 0 ) return;

    char stampPath[1024];
    snprintf( stampPath, sizeof(stampPath), "%s/.extracted_stamp", targetDir );
    FILE* sf = fopen( stampPath, "w" );
    if( sf )
    {
        fprintf( sf, "%lld %lld\n", (long long)archSt.st_size, (long long)archSt.st_mtime );
        fclose( sf );
    }
}

// ---------------------------------------------------------------------------
// LSPA Extraction Implementation
// ---------------------------------------------------------------------------
static bool ExtractLSPAInternal( const char* archivePath, const char* targetDir, ArchiveProgressCallback pfnProgress, void* userData )
{
    FILE* f = fopen( archivePath, "rb" );
    if( !f )
    {
        LOGE( "[ModArchive] Failed to open LSPA file '%s'\n", archivePath );
        return false;
    }

    fseek( f, 0, SEEK_END );
    long fileLength = ftell( f );
    fseek( f, 0, SEEK_SET );

    if( fileLength < 1536 )
    {
        fclose( f );
        return false;
    }

    unsigned char rootBlock[512];
    // Root directory block is block 2 (offset 1024)
    if( fseek( f, 1024, SEEK_SET ) != 0 || fread( rootBlock, 1, 512, f ) != 512 )
    {
        fclose( f );
        return false;
    }

    uint16_t numRootChildren = *(uint16_t*)(rootBlock + 2);
    LOGI( "[ModArchive] Extracting LSPA archive '%s': root children = %u\n", archivePath, (unsigned int)numRootChildren );

    // Check if timestamped format
    unsigned char sampleInfo[512];
    if( fseek( f, 4 * 512, SEEK_SET ) != 0 || fread( sampleInfo, 1, 512, f ) != 512 )
    {
        fclose( f );
        return false;
    }
    uint16_t tag10 = *(uint16_t*)(sampleInfo + 10);
    bool timestamped = ( tag10 >= 1 && tag10 <= 3 );

    uint32_t currentBlock = 3;

    struct ExtractorState {
        FILE* fileHandle;
        long totalLength;
        const char* baseDir;
        bool isTimestamped;
        uint32_t curBlock;
        ArchiveProgressCallback pfnProgress;
        void* userData;

        bool ExtractNode( const std::string& parentPath )
        {
            // Read name block
            unsigned char nameBlock[512];
            if( fseek( fileHandle, curBlock * 512, SEEK_SET ) != 0 || fread( nameBlock, 1, 512, fileHandle ) != 512 )
            {
                return false;
            }
            curBlock++;

            // Decode UTF-16LE name
            std::string nodeName;
            for( int i = 2; i < 256; i += 2 )
            {
                if( nameBlock[i] == 0 && nameBlock[i + 1] == 0 ) break;
                if( nameBlock[i + 1] != 0 ) break; // ASCII only
                nodeName.push_back( (char)nameBlock[i] );
            }
            if( nodeName.empty() ) return false;

            // Read info block
            unsigned char infoBlock[512];
            if( fseek( fileHandle, curBlock * 512, SEEK_SET ) != 0 || fread( infoBlock, 1, 512, fileHandle ) != 512 )
            {
                return false;
            }
            curBlock++;

            uint32_t offset = 0;
            uint32_t val = 0;

            if( isTimestamped )
            {
                offset = *(uint32_t*)(infoBlock + 20);
                val = *(uint32_t*)(infoBlock + 12);
            }
            else
            {
                offset = *(uint32_t*)(infoBlock + 10);
                val = *(uint32_t*)(infoBlock + 2);
            }

            std::string fullRelPath = parentPath.empty() ? nodeName : ( parentPath + "/" + nodeName );
            std::string outDiskPath = std::string( baseDir ) + "/" + fullRelPath;

            if( offset == 0 )
            {
                // Directory
                uint16_t numChildren = (uint16_t)(val & 0xffff);
                EnsureDirectoryRecursive( outDiskPath );
                for( uint16_t c = 0; c < numChildren; ++c )
                {
                    if( !ExtractNode( fullRelPath ) ) return false;
                }
            }
            else
            {
                // File
                curBlock++; // Skip extra block for files
                uint32_t fileSize = val;
                EnsureDirectoryRecursive( GetParentDir( outDiskPath ) );

                if( (long)(offset + fileSize) > totalLength )
                {
                    LOGE( "[ModArchive] LSPA file offset 0x%x + size %u exceeds total size %ld for '%s'\n",
                          offset, fileSize, totalLength, fullRelPath.c_str() );
                    return false;
                }

                FILE* outFile = fopen( outDiskPath.c_str(), "wb" );
                if( !outFile )
                {
                    LOGE( "[ModArchive] Failed to write extracted file '%s'\n", outDiskPath.c_str() );
                    return false;
                }

                if( pfnProgress != NULL && totalLength > 0 )
                {
                    float prog = (float)offset / (float)totalLength;
                    if( prog > 1.0f ) prog = 1.0f;
                    pfnProgress( prog, fullRelPath.c_str(), userData );
                }

                if( fileSize > 0 )
                {
                    if( fseek( fileHandle, offset, SEEK_SET ) == 0 )
                    {
                        char buf[8192];
                        uint32_t remaining = fileSize;
                        while( remaining > 0 )
                        {
                            size_t toRead = remaining > sizeof(buf) ? sizeof(buf) : remaining;
                            size_t rd = fread( buf, 1, toRead, fileHandle );
                            if( rd > 0 )
                            {
                                fwrite( buf, 1, rd, outFile );
                                remaining -= (uint32_t)rd;
                            }
                            else
                            {
                                break;
                            }
                        }
                    }
                }
                fclose( outFile );
            }
            return true;
        }
    } state;

    state.fileHandle = f;
    state.totalLength = fileLength;
    state.baseDir = targetDir;
    state.isTimestamped = timestamped;
    state.curBlock = currentBlock;
    state.pfnProgress = pfnProgress;
    state.userData = userData;

    EnsureDirectoryRecursive( targetDir );

    for( uint16_t i = 0; i < numRootChildren; ++i )
    {
        if( !state.ExtractNode( "" ) )
        {
            LOGE( "[ModArchive] Failed extracting node %u of %u\n", (unsigned int)i, (unsigned int)numRootChildren );
            fclose( f );
            return false;
        }
    }

    fclose( f );
    if( pfnProgress != NULL )
    {
        pfnProgress( 1.0f, "Completed", userData );
    }
    LOGI( "[ModArchive] Successfully extracted LSPA archive '%s' to '%s'\n", archivePath, targetDir );
    return true;
}

// ---------------------------------------------------------------------------
// ZIP Extraction Implementation
// ---------------------------------------------------------------------------
static bool ExtractZipInternal( const char* archivePath, const char* targetDir, ArchiveProgressCallback pfnProgress, void* userData )
{
    mz_zip_archive zip;
    memset( &zip, 0, sizeof(zip) );

    if( !mz_zip_reader_init_file( &zip, archivePath, 0 ) )
    {
        LOGE( "[ModArchive] miniz failed to open zip '%s'\n", archivePath );
        return false;
    }

    EnsureDirectoryRecursive( targetDir );
    int numFiles = (int)mz_zip_reader_get_num_files( &zip );
    LOGI( "[ModArchive] Extracting ZIP archive '%s': %d files\n", archivePath, numFiles );

    for( int i = 0; i < numFiles; ++i )
    {
        mz_zip_archive_file_stat file_stat;
        if( !mz_zip_reader_file_stat( &zip, i, &file_stat ) )
        {
            continue;
        }

        if( pfnProgress != NULL && numFiles > 0 )
        {
            float prog = (float)i / (float)numFiles;
            pfnProgress( prog, file_stat.m_filename, userData );
        }

        char outFilePath[1024];
        snprintf( outFilePath, sizeof(outFilePath), "%s/%s", targetDir, file_stat.m_filename );

        if( mz_zip_reader_is_file_a_directory( &zip, i ) )
        {
            EnsureDirectoryRecursive( outFilePath );
        }
        else
        {
            EnsureDirectoryRecursive( GetParentDir( outFilePath ) );
            if( !mz_zip_reader_extract_to_file( &zip, i, outFilePath, 0 ) )
            {
                LOGE( "[ModArchive] Failed extracting '%s' from zip\n", file_stat.m_filename );
            }
        }
    }

    mz_zip_reader_end( &zip );
    if( pfnProgress != NULL )
    {
        pfnProgress( 1.0f, "Completed", userData );
    }
    LOGI( "[ModArchive] Successfully extracted ZIP archive '%s' to '%s'\n", archivePath, targetDir );
    return true;
}

bool ExtractArchive( const char* archivePath, const char* targetDir, ArchiveProgressCallback pfnProgress, void* userData )
{
    if( archivePath == NULL || targetDir == NULL ) return false;

    bool success = false;
    if( IsLSPArchive( archivePath ) )
    {
        success = ExtractLSPAInternal( archivePath, targetDir, pfnProgress, userData );
    }
    else if( IsZipArchive( archivePath ) )
    {
        success = ExtractZipInternal( archivePath, targetDir, pfnProgress, userData );
    }
    else
    {
        LOGE( "[ModArchive] Unknown archive format for '%s'\n", archivePath );
        return false;
    }

    if( success )
    {
        WriteExtractionStamp( archivePath, targetDir );
    }
    return success;
}

std::string FindEffectiveModRoot( const char* targetDir )
{
    if( targetDir == NULL || targetDir[0] == '\0' ) return "";

    // 1. Direct check: Meta.ini or CustomFiles exists directly in targetDir
    char testPath[1024];
    snprintf( testPath, sizeof(testPath), "%s/Meta.ini", targetDir );
    if( access( testPath, R_OK ) == 0 ) return targetDir;

    snprintf( testPath, sizeof(testPath), "%s/CustomFiles", targetDir );
    if( access( testPath, R_OK ) == 0 ) return targetDir;

    snprintf( testPath, sizeof(testPath), "%s/CustomFiles.lua", targetDir );
    if( access( testPath, R_OK ) == 0 ) return targetDir;

    // 2. Check if there is a single subfolder containing the mod
    DIR* d = opendir( targetDir );
    if( !d ) return targetDir;

    std::vector<std::string> subdirs;
    struct dirent* ent;
    while( ( ent = readdir( d ) ) != NULL )
    {
        if( ent->d_name[0] == '.' ) continue;
        char subPath[1024];
        snprintf( subPath, sizeof(subPath), "%s/%s", targetDir, ent->d_name );
        struct stat st;
        if( stat( subPath, &st ) == 0 && S_ISDIR( st.st_mode ) )
        {
            subdirs.push_back( ent->d_name );
        }
    }
    closedir( d );

    if( subdirs.size() == 1 )
    {
        std::string candidate = std::string( targetDir ) + "/" + subdirs[0];
        snprintf( testPath, sizeof(testPath), "%s/Meta.ini", candidate.c_str() );
        if( access( testPath, R_OK ) == 0 ) return candidate;

        snprintf( testPath, sizeof(testPath), "%s/CustomFiles", candidate.c_str() );
        if( access( testPath, R_OK ) == 0 ) return candidate;

        return candidate;
    }

    return targetDir;
}

} // namespace ModArchive
