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

bool TranscodeWavToRsd( const char* wavPath, const char* rsdPath )
{
    if( wavPath == NULL || rsdPath == NULL || wavPath[0] == '\0' || rsdPath[0] == '\0' )
    {
        return false;
    }

    struct stat st;
    if( stat( rsdPath, &st ) == 0 && st.st_size > 2048 )
    {
        return true;
    }

    FILE* in = fopen( wavPath, "rb" );
    if( !in )
    {
        LOGE( "[WavTranscoder] Failed to open WAV file: '%s'\n", wavPath );
        return false;
    }

    char riffHeader[12];
    if( fread( riffHeader, 1, 12, in ) != 12 ||
        memcmp( riffHeader, "RIFF", 4 ) != 0 ||
        memcmp( riffHeader + 8, "WAVE", 4 ) != 0 )
    {
        LOGE( "[WavTranscoder] Invalid WAV header: '%s'\n", wavPath );
        fclose( in );
        return false;
    }

    unsigned short audioFormat = 0;
    unsigned short numChannels = 0;
    unsigned int sampleRate = 0;
    unsigned short bitsPerSample = 0;
    bool fmtFound = false;

    long dataOffset = -1;
    unsigned int dataBytes = 0;

    char chunkHdr[8];
    while( fread( chunkHdr, 1, 8, in ) == 8 )
    {
        unsigned int chunkSize = *(unsigned int*)(chunkHdr + 4);
        if( memcmp( chunkHdr, "fmt ", 4 ) == 0 )
        {
            if( chunkSize >= 16 )
            {
                unsigned char fmtBuf[16];
                if( fread( fmtBuf, 1, 16, in ) == 16 )
                {
                    audioFormat = *(unsigned short*)(fmtBuf);
                    numChannels = *(unsigned short*)(fmtBuf + 2);
                    sampleRate = *(unsigned int*)(fmtBuf + 4);
                    bitsPerSample = *(unsigned short*)(fmtBuf + 14);
                    fmtFound = true;
                    if( chunkSize > 16 )
                    {
                        fseek( in, chunkSize - 16, SEEK_CUR );
                    }
                    continue;
                }
            }
        }
        else if( memcmp( chunkHdr, "data", 4 ) == 0 )
        {
            dataOffset = ftell( in );
            dataBytes = chunkSize;
            break;
        }

        fseek( in, chunkSize, SEEK_CUR );
    }

    if( !fmtFound || dataOffset < 0 || numChannels == 0 || sampleRate == 0 ||
        ( audioFormat != 1 && audioFormat != 3 ) )
    {
        LOGE( "[WavTranscoder] Unsupported or missing fmt/data chunks in '%s' (format=%u, ch=%u, rate=%u, bits=%u)\n",
              wavPath, audioFormat, numChannels, sampleRate, bitsPerSample );
        fclose( in );
        return false;
    }

    short* pcmSamples = NULL;
    size_t outDataBytes = 0;

    fseek( in, dataOffset, SEEK_SET );

    if( audioFormat == 1 && bitsPerSample == 16 )
    {
        pcmSamples = (short*)malloc( dataBytes );
        if( !pcmSamples )
        {
            fclose( in );
            return false;
        }
        if( fread( pcmSamples, 1, dataBytes, in ) != dataBytes )
        {
            free( pcmSamples );
            fclose( in );
            return false;
        }
        outDataBytes = dataBytes;
    }
    else if( audioFormat == 1 && bitsPerSample == 8 )
    {
        unsigned char* u8Buf = (unsigned char*)malloc( dataBytes );
        if( !u8Buf ) { fclose( in ); return false; }
        if( fread( u8Buf, 1, dataBytes, in ) != dataBytes ) { free( u8Buf ); fclose( in ); return false; }
        pcmSamples = (short*)malloc( dataBytes * sizeof(short) );
        if( !pcmSamples ) { free( u8Buf ); fclose( in ); return false; }
        for( size_t i = 0; i < dataBytes; ++i )
        {
            pcmSamples[i] = (short)( ( (int)u8Buf[i] - 128 ) << 8 );
        }
        free( u8Buf );
        outDataBytes = dataBytes * sizeof(short);
    }
    else if( audioFormat == 3 && bitsPerSample == 32 )
    {
        size_t floatCount = dataBytes / sizeof(float);
        float* fBuf = (float*)malloc( dataBytes );
        if( !fBuf ) { fclose( in ); return false; }
        if( fread( fBuf, 1, dataBytes, in ) != dataBytes ) { free( fBuf ); fclose( in ); return false; }
        pcmSamples = (short*)malloc( floatCount * sizeof(short) );
        if( !pcmSamples ) { free( fBuf ); fclose( in ); return false; }
        for( size_t i = 0; i < floatCount; ++i )
        {
            float v = fBuf[i];
            if( v > 1.0f ) v = 1.0f;
            else if( v < -1.0f ) v = -1.0f;
            pcmSamples[i] = (short)( v * 32767.0f );
        }
        free( fBuf );
        outDataBytes = floatCount * sizeof(short);
    }
    else
    {
        LOGE( "[WavTranscoder] Unsupported bitsPerSample: %u in '%s'\n", bitsPerSample, wavPath );
        fclose( in );
        return false;
    }

    fclose( in );

    EnsureParentDirectory( rsdPath );
    FILE* out = fopen( rsdPath, "wb" );
    if( !out )
    {
        LOGE( "[WavTranscoder] Failed to open output RSD for writing: '%s'\n", rsdPath );
        free( pcmSamples );
        return false;
    }

    Rsd4PcmHeader hdr;
    memset( &hdr, 0, sizeof(hdr) );
    memcpy( hdr.m_RsdDataTag, "RSD4", 4 );
    memcpy( hdr.m_SoundDataType, "PCM ", 4 );
    hdr.m_Channels = (unsigned int)numChannels;
    hdr.m_BitResolution = 16;
    hdr.m_SamplingRate = sampleRate;

    if( fwrite( &hdr, 1, sizeof(hdr), out ) != sizeof(hdr) ||
        fwrite( pcmSamples, 1, outDataBytes, out ) != outDataBytes )
    {
        LOGE( "[WavTranscoder] Failed writing RSD to '%s'\n", rsdPath );
        fclose( out );
        unlink( rsdPath );
        free( pcmSamples );
        return false;
    }

    fclose( out );
    free( pcmSamples );

    LOGI( "[WavTranscoder] Successfully transcoded: '%s' -> '%s' (%u ch, %u Hz, %zu bytes)\n",
          wavPath, rsdPath, numChannels, sampleRate, sizeof(hdr) + outDataBytes );
    return true;
}

bool TranscodeAudioToRsd( const char* srcAudioPath, const char* rsdPath )
{
    if( srcAudioPath == NULL || rsdPath == NULL ) return false;

    size_t len = strlen( srcAudioPath );
    if( len > 4 && strcasecmp( srcAudioPath + len - 4, ".ogg" ) == 0 )
    {
        return TranscodeOggToRsd( srcAudioPath, rsdPath );
    }
    if( len > 4 && strcasecmp( srcAudioPath + len - 4, ".wav" ) == 0 )
    {
        return TranscodeWavToRsd( srcAudioPath, rsdPath );
    }

    // Header probe fallback
    FILE* f = fopen( srcAudioPath, "rb" );
    if( f )
    {
        char magic[4];
        if( fread( magic, 1, 4, f ) == 4 )
        {
            fclose( f );
            if( memcmp( magic, "OggS", 4 ) == 0 )
            {
                return TranscodeOggToRsd( srcAudioPath, rsdPath );
            }
            if( memcmp( magic, "RIFF", 4 ) == 0 )
            {
                return TranscodeWavToRsd( srcAudioPath, rsdPath );
            }
        }
        else
        {
            fclose( f );
        }
    }

    return false;
}
