#ifndef VOLUMEBACKUP_BACKUP_UTIL_H
#define VOLUMEBACKUP_BACKUP_UTIL_H

#include "VolumeProtectMacros.h"
// external logger/json library
#include "Json.h"
#include "VolumeProtector.h"
#include <cstdint>
#include <string>

namespace volumeprotect {

// volume data in [offset, offset + length) store in the file
struct CopySegment {
    std::string                 copyDataFile;           // name of the copy file
    std::string                 checksumBinFile;        // name of checksum binary file
    int                         index;                  // session index
    uint64_t                    offset;                 // volume offset
    uint64_t                    length;

    SERIALIZE_SECTION_BEGIN
    SERIALIZE_FIELD(copyDataFile, copyDataFile);
    SERIALIZE_FIELD(checksumBinFile, checksumBinFile);
    SERIALIZE_FIELD(index, index);
    SERIALIZE_FIELD(offset, offset);
    SERIALIZE_FIELD(length, length);
    SERIALIZE_SECTION_END
};

struct VolumeCopyMeta {
    std::string                 copyName;
    int                         backupType;       // cast BackupType to int
    int                         copyFormat;     // cast CopyFormat to int
    uint64_t                    volumeSize;     // volume size in bytes
    uint32_t                    blockSize;      // block size in bytes
    std::string                 volumePath;
    std::vector<CopySegment>    segments;

    SERIALIZE_SECTION_BEGIN
    SERIALIZE_FIELD(backupType, backupType);
    SERIALIZE_FIELD(volumeSize, volumeSize);
    SERIALIZE_FIELD(volumePath, volumePath);
    SERIALIZE_FIELD(blockSize, blockSize);
    SERIALIZE_FIELD(segments, segments);
    SERIALIZE_SECTION_END
};

namespace util {

std::string GetChecksumBinPath(
    const std::string&  copyMetaDirPath,
    const std::string&  copyName,
    int                 sessionIndex
);

std::string GetCopyDataFilePath(
    const std::string&  copyDataDirPath,
    const std::string&  copyName,
    CopyFormat          copyFormat,
    int                 sessionIndex
);

std::string GetWriterBitmapFilePath(
    const std::string&  copyMetaDirPath,
    const std::string&  copyName,
    int                 sessionIndex
);

std::string GetFileName(const std::string& fullpath);

bool WriteVolumeCopyMeta(
    const std::string& copyMetaDirPath,
    const std::string& copyName,
    const VolumeCopyMeta& volumeCopyMeta);

bool ReadVolumeCopyMeta(
    const std::string& copyMetaDirPath,
    const std::string& copyName,
    VolumeCopyMeta& volumeCopyMeta);
}
}

#endif