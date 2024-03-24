/**
 * @file FileSystemAPI.h
 * @brief Provide unified filesystem api for mutiple OS.
 * @copyright Copyright 2023-2024 XUranus. All rights reserved.
 * @license This project is released under the Apache License.
 * @author XUranus(2257238649wdx@gmail.com)
 */

#ifndef VOLUMEBACKUP_NATIVE_FILESYSTEM_API_HEADER
#define VOLUMEBACKUP_NATIVE_FILESYSTEM_API_HEADER

#include "common/VolumeProtectMacros.h"

namespace volumeprotect {
/**
 * @brief native filesystem api wrapper
 */
namespace fsapi {

class SystemApiException : public std::exception {
public:
    explicit SystemApiException(ErrCodeType errorCode);
    SystemApiException(const char* message, ErrCodeType errorCode);
    const char* what() const noexcept override;
private:
    std::string m_message;
};

bool        IsFileExists(const std::string& path);

uint64_t    GetFileSize(const std::string& path);

bool        IsDirectoryExists(const std::string& path);

uint8_t*    ReadBinaryBuffer(const std::string& filepath, uint64_t length);

bool        WriteBinaryBuffer(const std::string& filepath, const uint8_t* buffer, uint64_t length);

bool        IsVolumeExists(const std::string& volumePath);

uint64_t    ReadVolumeSize(const std::string& volumePath);

uint32_t    ProcessorsNum();

bool        CreateEmptyFile(const std::string& dirPath, const std::string& filename);

bool        RemoveFile(const std::string& dirPath, const std::string& filename);

bool        RemoveFile(const std::string& filepath);

#ifdef __linux__
uint64_t    ReadSectorSizeLinux(const std::string& devicePath);
#endif

}
}

#endif