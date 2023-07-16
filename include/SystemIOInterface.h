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

namespace volumeprotect {
namespace system {

#ifdef __linux__
using IOHandle = int;
#endif

#ifdef _WIN32
using IOHandle = HANDLE;
#endif

bool IsValidIOHandle(IOHandle handle);

void SetHandleInvalid(IOHandle& handle);

IOHandle OpenVolumeForRead(const std::string& volumePath);

IOHandle OpenVolumeForWrite(const std::string& volumePath);

void CloseVolume(IOHandle handle);

bool ReadVolumeData(IOHandle handle, char* buffer, int length, uint32_t& errorCode);

bool WriteVolumeData(IOHandle handle, char* buffer, int length, uint32_t& errorCode);

bool SetIOPointer(IOHandle handle, uint64_t offset);

uint32_t GetLastError();

bool TruncateCreateFile(const std::string& path, uint64_t size);

}
};

#endif