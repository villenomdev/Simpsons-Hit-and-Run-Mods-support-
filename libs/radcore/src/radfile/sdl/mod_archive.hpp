#ifndef MOD_ARCHIVE_HPP
#define MOD_ARCHIVE_HPP

#include <string>
#include <vector>
#include <stdint.h>

namespace ModArchive
{
    // Check if file starts with LSPA magic ("LSPA")
    bool IsLSPArchive( const char* filePath );

    // Check if file starts with ZIP magic ("PK\x03\x04")
    bool IsZipArchive( const char* filePath );

    // Check if file is any supported mod archive (.lmlm or .zip)
    bool IsSupportedArchive( const char* filePath );

    // Check if cache directory contains a valid stamp matching current archive size and mtime
    bool IsExtractionUpToDate( const char* archivePath, const char* targetDir );

    // Extract LSPA or ZIP archive to targetDir
    bool ExtractArchive( const char* archivePath, const char* targetDir );

    // Given targetDir, determine the effective root folder where Meta.ini or CustomFiles lives.
    // If the archive had a single wrapper directory, returns targetDir/WrapperDir, else targetDir.
    std::string FindEffectiveModRoot( const char* targetDir );
}

#endif // MOD_ARCHIVE_HPP
