#ifndef _VOLUME_SYSTEM_IO_INTERFACE_HEADER_
#define _VOLUME_SYSTEM_IO_INTERFACE_HEADER_


#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN 1
#define UNICODE /* foring using WCHAR on windows */
#define NOGDI
#include <locale>
#include <codecvt>
#include <Windows.h>
#include <winioctl.h>
#endif

#include <string>
#include <cstdint>
#include <exception>
#include <stdexcept>

#include "VolumeProtectMacros.h"

/**
 * @brief this module is used to shield system I/O interface differences to provide a unified I/O layer
 */
namespace volumeprotect {
namespace system {

#ifdef __linux__
using IOHandle = int;
#endif

#ifdef _WIN32
using IOHandle = HANDLE;
#endif

class SystemApiException : public std::exception {
public:
    // Constructor
    SystemApiException(uint32_t errorCode);
    SystemApiException(const char* message, uint32_t errorCode);
    const char* what() const noexcept override;
private:
    std::string m_message;
};

VOLUMEPROTECT_API bool IsValidIOHandle(IOHandle handle);

VOLUMEPROTECT_API void SetHandleInvalid(IOHandle& handle);

VOLUMEPROTECT_API IOHandle OpenVolumeForRead(const std::string& volumePath);

VOLUMEPROTECT_API IOHandle OpenVolumeForWrite(const std::string& volumePath);

VOLUMEPROTECT_API void CloseVolume(IOHandle handle);

VOLUMEPROTECT_API bool ReadVolumeData(
    IOHandle    handle,
    uint64_t    offset,
    char*       buffer,
    int         length,
    uint32_t&   errorCode
);

VOLUMEPROTECT_API bool WriteVolumeData(
    IOHandle    handle,
    uint64_t    offset,
    char*       buffer,
    int         length, 
    uint32_t&   errorCode
);

VOLUMEPROTECT_API bool SetIOPointer(IOHandle handle, uint64_t offset);

VOLUMEPROTECT_API uint32_t GetLastError();

VOLUMEPROTECT_API bool TruncateCreateFile(const std::string& path, uint64_t size);

VOLUMEPROTECT_API bool IsFileExists(const std::string& path);

VOLUMEPROTECT_API uint64_t GetFileSize(const std::string& path);

VOLUMEPROTECT_API bool IsDirectoryExists(const std::string& path);

VOLUMEPROTECT_API bool IsVolumeExists(const std::string& volumePath);

VOLUMEPROTECT_API uint64_t ReadVolumeSize(const std::string& volumePath);

VOLUMEPROTECT_API uint32_t ProcessorsNum();

}
};

#endif