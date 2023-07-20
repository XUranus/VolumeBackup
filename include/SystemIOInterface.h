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

}
};

#endif