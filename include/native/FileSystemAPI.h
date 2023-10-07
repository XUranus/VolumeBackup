#ifndef VOLUMEBACKUP_NATIVE_FILESYSTEM_API_HEADER
#define VOLUMEBACKUP_NATIVE_FILESYSTEM_API_HEADER

#include "VolumeProtectMacros.h"

namespace volumeprotect {

class SystemApiException : public std::exception {
public:
    explicit SystemApiException(ErrCodeType errorCode);
    SystemApiException(const char* message, ErrCodeType errorCode);
    const char* what() const noexcept override;
private:
    std::string m_message;
};

namespace fsapi {

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

#ifdef __linux__
uint64_t    ReadSectorSizeLinux(const std::string& devicePath);

bool        IsMountPoint(const std::string& dirPath);
// get the block device path of the mount point
std::string GetMountDevicePath(const std::string& mountTargetPath);
#endif

}
}

#endif