#ifndef OGG_TRANSCODER_HPP
#define OGG_TRANSCODER_HPP

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Transcodes an OGG Vorbis file to an RSD4 PCM file with a 2048-byte header.
// If rsdPath already exists and is non-empty (> 2048 bytes), skips decoding and returns true.
bool TranscodeOggToRsd( const char* oggPath, const char* rsdPath );

// Transcodes a standard WAV (PCM 8/16-bit or float) file to an RSD4 PCM file with a 2048-byte header.
bool TranscodeWavToRsd( const char* wavPath, const char* rsdPath );

// Transcodes either OGG or WAV to RSD4 PCM file based on extension or header.
bool TranscodeAudioToRsd( const char* srcAudioPath, const char* rsdPath );

#ifdef __cplusplus
}
#endif

#endif // OGG_TRANSCODER_HPP
