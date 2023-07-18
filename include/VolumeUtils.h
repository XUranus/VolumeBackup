#ifndef VOLUME_BACKUP_UTIL_H
#define VOLUME_BACKUP_UTIL_H

#include <cstdint>
#include <stdexcept>
#include <string>
#include <exception>
#include <vector>

// external logger/json library
#include "Logger.h"
#include "Json.h"
#include "VolumeProtector.h"

namespace volumeprotect {

class SystemApiException : public std::exception {
public:
    // Constructor
    SystemApiException(uint32_t errorCode);
    SystemApiException(const char* message, uint32_t errorCode);
    const char* what() const noexcept override;
private:
    std::string m_message;
};

struct VolumeCopyMeta {
    using Range = std::vector<std::pair<uint64_t, uint64_t>>;

    int         copyType;
    uint64_t    volumeSize;
    uint32_t    blockSize;
    Range       copySlices;

    SERIALIZE_SECTION_BEGIN
    SERIALIZE_FIELD(copyType, copyType);
    SERIALIZE_FIELD(volumeSize, volumeSize);
    SERIALIZE_FIELD(blockSize, blockSize);
    SERIALIZE_FIELD(copySlices, copySlices);
    SERIALIZE_SECTION_END
};

namespace util {

uint64_t ReadVolumeSize(const std::string& blockDevice);

bool IsBlockDeviceExists(const std::string& blockDevicePath);

bool CheckDirectoryExistence(const std::string& path);

uint32_t ProcessorsNum();

std::string GetChecksumBinPath(
    const std::string&          copyMetaDirPath,
    uint64_t                    sessionOffset,
    uint64_t                    sessionSize
);

std::string GetCopyFilePath(
    const std::string&          copyDataDirPath,
    volumeprotect::CopyType     copyType,
    uint64_t                    sessionOffset,
    uint64_t                    sessionSize
);

bool WriteVolumeCopyMeta(const std::string& copyMetaDirPath, const VolumeCopyMeta& volumeCopyMeta);

bool ReadVolumeCopyMeta(const std::string& copyMetaDirPath, VolumeCopyMeta& volumeCopyMeta);

}
}

#endif