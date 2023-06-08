#ifndef VOLUME_BACKUP_UTIL_H
#define VOLUME_BACKUP_UTIL_H

#include <cstdint>
#include <stdexcept>
#include <string>
#include <exception>
#include <vector>

#include "VolumeBackupContext.h"

namespace volumebackup {
namespace util {

std::runtime_error BuildRuntimeException(
    const std::string& message,
    const std::string& blockDevice,
    uint32_t errcode);

uint64_t ReadVolumeSize(const std::string& blockDevice);

bool CheckDirectoryExistence(const std::string& path);

uint32_t ProcessorsNum();

std::string ReadVolumeUUID(const std::string& blockDevicePath);

std::string ReadVolumeType(const std::string& blockDevicePath);

std::string ReadVolumeLabel(const std::string& blockDevicePath);

std::vector<VolumePartitionTableEntry> ReadVolumePartitionTable(const std::string& blockDevicePath);

std::string GetChecksumBinPath(const std::string& copyMetaDirPath, uint64_t sessionOffset, uint64_t sessionSize);

std::string GetCopyFilePath(const std::string& copyDataDirPath, uint64_t sessionOffset, uint64_t sessionSize);

bool WriteVolumeCopyMeta(const std::string& copyMetaDirPath, const VolumeCopyMeta& volumeCopyMeta);

bool ReadVolumeCopyMeta(const std::string& copyMetaDirPath, VolumeCopyMeta& volumeCopyMeta);

}
}

#endif