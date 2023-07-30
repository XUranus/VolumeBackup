#ifndef VOLUMEBACKUP_BACKUP_UTIL_H
#define VOLUMEBACKUP_BACKUP_UTIL_H

#include <cstdint>
#include <string>
#include <vector>

#include "VolumeProtectMacros.h"
// external logger/json library
#include "Logger.h"
#include "Json.h"
#include "VolumeProtector.h"
#include "NativeIOInterface.h"

namespace volumeprotect {

struct VOLUMEPROTECT_API VolumeCopyMeta {
    using Range = std::vector<std::pair<uint64_t, uint64_t>>;

    int         copyType;
    uint64_t    volumeSize;
    uint32_t    blockSize;
    std::string volumePath;
    Range       copySlices;

    SERIALIZE_SECTION_BEGIN
    SERIALIZE_FIELD(copyType, copyType);
    SERIALIZE_FIELD(volumeSize, volumeSize);
    SERIALIZE_FIELD(volumePath, volumePath);
    SERIALIZE_FIELD(blockSize, blockSize);
    SERIALIZE_FIELD(copySlices, copySlices);
    SERIALIZE_SECTION_END
};

namespace util {

VOLUMEPROTECT_API std::string GetChecksumBinPath(
    const std::string&          copyMetaDirPath,
    uint64_t                    sessionOffset,
    uint64_t                    sessionSize
);

VOLUMEPROTECT_API std::string GetCopyFilePath(
    const std::string&          copyDataDirPath,
    uint64_t                    sessionOffset,
    uint64_t                    sessionSize
);

VOLUMEPROTECT_API std::string GetWriterBitmapFilePath(
    const std::string&          copyMetaDirPath,
    uint64_t                    sessionOffset,
    uint64_t                    sessionSize
);


VOLUMEPROTECT_API bool WriteVolumeCopyMeta(const std::string& copyMetaDirPath, const VolumeCopyMeta& volumeCopyMeta);

VOLUMEPROTECT_API bool ReadVolumeCopyMeta(const std::string& copyMetaDirPath, VolumeCopyMeta& volumeCopyMeta);

VOLUMEPROTECT_API void SaveSessionWriterBitmap(std::shared_ptr<VolumeTaskSession> session);

bool 

}
}

#endif