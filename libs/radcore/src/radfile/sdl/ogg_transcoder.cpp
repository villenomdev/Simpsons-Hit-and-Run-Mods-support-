#include "ogg_transcoder.hpp"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <string>

#if defined(RAD_ANDROID)
#include <android/log.h>
#define LOG_TAG "SHAR_OggTranscoder"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#else
#define LOGI(...) printf(__VA_ARGS__)
#define LOGE(...) fprintf(stderr, __VA_ARGS__)
#endif

// Forward declaration from stb_vorbis.c
extern "C" int stb_vorbis_decode_filename( const char* filename, int* channels, int* sample_rate, short** output );

struct Rsd4PcmHeader
{
    char m_RsdDataTag[4];    // "RSD4"
    char m_SoundDataType[4]; // "PCM "
    unsigned int m_Channels;
    unsigned int m_BitResolution;
    unsigned int m_SamplingRate;
    char m_Padding[2048 - 20]; // Padded to 2048 bytes (RSD_FILE_DATA_OFFSET)
};

static void EnsureParentDirectory( const char* filePath )
{
    if( filePath == NULL || filePath[0] == '\0' ) return;
    std::string path = filePath;
    size_t lastSlash = path.rfind( '/' );
    if( lastSlash == std::string::npos ) return;

    std::string dir = path.substr( 0, lastSlash );
    char tmp[1024];
    snprintf( tmp, sizeof(tmp), "%s", dir.c_str() );
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

bool TranscodeOggToRsd( const char* oggPath, const char* rsdPath )
{
    if( oggPath == NULL || rsdPath == NULL || oggPath[0] == '\0' || rsdPath[0] == '\0' )
    {
        return false;
    }

    // Fast check: if rsd already exists and has valid size (> 2048 bytes), skip transcoding
    struct stat st;
    if( stat( rsdPath, &st ) == 0 && st.st_size > 2048 )
    {
        return true;
    }

    int channels = 0;
    int sampleRate = 0;
    short* samples = NULL;
    int numSamples = stb_vorbis_decode_filename( oggPath, &channels, &sampleRate, &samples );

    if( numSamples <= 0 || samples == NULL || channels <= 0 || sampleRate <= 0 )
    {
        LOGE( "[OggTranscoder] Failed to decode OGG: '%s'\n", oggPath );
        if( samples != NULL )
        {
            free( samples );
        }
        return false;
    }

    EnsureParentDirectory( rsdPath );

    FILE* f = fopen( rsdPath, "wb" );
    if( !f )
    {
        LOGE( "[OggTranscoder] Failed to open output RSD for writing: '%s'\n", rsdPath );
        free( samples );
        return false;
    }

    Rsd4PcmHeader hdr;
    memset( &hdr, 0, sizeof(hdr) );
    memcpy( hdr.m_RsdDataTag, "RSD4", 4 );
    memcpy( hdr.m_SoundDataType, "PCM ", 4 );
    hdr.m_Channels = (unsigned int)channels;
    hdr.m_BitResolution = 16;
    hdr.m_SamplingRate = (unsigned int)sampleRate;

    // Write 2048-byte header
    if( fwrite( &hdr, 1, sizeof(hdr), f ) != sizeof(hdr) )
    {
        LOGE( "[OggTranscoder] Failed to write RSD header to '%s'\n", rsdPath );
        fclose( f );
        unlink( rsdPath );
        free( samples );
        return false;
    }

    // Write 16-bit PCM samples
    size_t dataBytes = (size_t)numSamples * (size_t)channels * sizeof(short);
    if( fwrite( samples, 1, dataBytes, f ) != dataBytes )
    {
        LOGE( "[OggTranscoder] Failed to write PCM data to '%s'\n", rsdPath );
        fclose( f );
        unlink( rsdPath );
        free( samples );
        return false;
    }

    fclose( f );
    free( samples );

    LOGI( "[OggTranscoder] Successfully transcoded: '%s' -> '%s' (%d ch, %d Hz, %d samples, %zu bytes)\n",
          oggPath, rsdPath, channels, sampleRate, numSamples, sizeof(hdr) + dataBytes );

    return true;
}
