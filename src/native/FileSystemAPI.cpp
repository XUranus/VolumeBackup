#include <cerrno>
#include <cstdio>

#ifdef __linux__
#include <sys/mount.h>
#include <mntent.h>

#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <linux/fs.h>
#include <unistd.h>
#include <dirent.h>
#endif

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN 1
#define UNICODE /* foring using WCHAR on windows */
#define NOGDI
#include <locale>
#include <codecvt>
#include <Windows.h>
#include <winioctl.h>
#endif

#include <iostream>
#include <fstream>
#include "Logger.h"
#include "native/FileSystemAPI.h"
#include "native/RawIO.h"

using namespace volumeprotect;
using namespace volumeprotect::fsapi;

namespace {
    constexpr auto DEFAULT_PROCESSORS_NUM = 4;
    constexpr auto DEFAULT_MKDIR_MASK = 0755;

#ifdef _WIN32
    constexpr auto SEPARATOR = "\\";
#else
    constexpr auto SEPARATOR = "/";
#endif
    const std::string SYS_MOUNTS_ENTRY_PATH = "/proc/mounts";
}

#ifdef _WIN32
// Implement common WIN32 API utils
static std::wstring Utf8ToUtf16(const std::string& str)
{
    using ConvertTypeX = std::codecvt_utf8_utf16<wchar_t>;
    std::wstring_convert<ConvertTypeX> converterX;
    std::wstring wstr = converterX.from_bytes(str);
    return wstr;
}

static std::string Utf16ToUtf8(const std::wstring& wstr)
{
    using ConvertTypeX = std::codecvt_utf8_utf16<wchar_t>;
    std::wstring_convert<ConvertTypeX> converterX;
    return converterX.to_bytes(wstr);
}
#endif

SystemApiException::SystemApiException(ErrCodeType errorCode)
{
    m_message = std::string("error code = ") + std::to_string(errorCode);
}

SystemApiException::SystemApiException(const char* message, ErrCodeType errorCode)
{
    m_message = std::string(message) + " , error code = " + std::to_string(errorCode);
}

const char* SystemApiException::what() const noexcept
{
    return m_message.c_str();
}

bool fsapi::IsFileExists(const std::string& path)
{
#ifdef __linux__
    struct stat st;
    return ::stat(path.c_str(), &st) == 0;
#endif
#ifdef _WIN32
    std::wstring wpath = Utf8ToUtf16(path);
    DWORD attributes = ::GetFileAttributesW(wpath.c_str());
    return (attributes != INVALID_FILE_ATTRIBUTES) && ((attributes & FILE_ATTRIBUTE_DIRECTORY) == 0);
#endif
}

uint64_t fsapi::GetFileSize(const std::string& path)
{
#ifdef __linux__
    struct stat st;
    return ::stat(path.c_str(), &st) == 0 ? st.st_size : 0;
#endif
#ifdef _WIN32
    std::wstring wpath = Utf8ToUtf16(path);
    WIN32_FILE_ATTRIBUTE_DATA fileInfo;
    if (::GetFileAttributesExW(wpath.c_str(), GetFileExInfoStandard, &fileInfo)) {
        LARGE_INTEGER sizeEx {};
        sizeEx.HighPart = fileInfo.nFileSizeHigh;
        sizeEx.LowPart = fileInfo.nFileSizeLow;
        return sizeEx.QuadPart;
    } else {
        return 0;
    }
#endif
}

bool fsapi::IsDirectoryExists(const std::string& path)
{
#ifdef _WIN32
    std::wstring wpath = Utf8ToUtf16(path);
    DWORD attribute = ::GetFileAttributesW(wpath.c_str());
    if (attribute != INVALID_FILE_ATTRIBUTES && (attribute & FILE_ATTRIBUTE_DIRECTORY)) {
        return true;
    }
    return ::CreateDirectoryW(wpath.c_str(), nullptr) != 0;
#else
    DIR* dir = ::opendir(path.c_str());
    if (dir) {
        closedir(dir);
        return true;
    }
    return ::mkdir(path.c_str(), DEFAULT_MKDIR_MASK) == 0;
#endif
}

/**
 * @brief read bytes from file
 * @return uint8_t* ptr to data
 */
uint8_t* fsapi::ReadBinaryBuffer(const std::string& filepath, uint64_t length)
{
    if (length == 0) {
        WARNLOG("read empty binary file %s", filepath.c_str());
        return nullptr;
    }
    try {
        std::ifstream binFile(filepath, std::ios::binary);
        if (!binFile.is_open()) {
            ERRLOG("bin file %s open failed, errno: %d", filepath.c_str(), errno);
            return nullptr;
        }
        uint8_t* buffer = new (std::nothrow) uint8_t[length];
        memset(buffer, 0, sizeof(uint8_t) * length);
        if (buffer == nullptr) {
            ERRLOG("failed to malloc buffer, size = %llu", length);
            binFile.close();
            return nullptr;
        }
        binFile.read(reinterpret_cast<char*>(buffer), length);
        if (binFile.fail()) {
            ERRLOG("failed to read %llu bytes from %s", length, filepath.c_str());
            delete[] buffer;
            binFile.close();
            return nullptr;
        }
        binFile.close();
        return buffer;
    } catch (const std::exception& e) {
        ERRLOG("failed to read checksum bin %s, exception %s", filepath.c_str(), e.what());
        return nullptr;
    }
    return nullptr;
}

/**
 * @brief write n bytes from file
 * @return if success
 */
bool fsapi::WriteBinaryBuffer(const std::string& filepath, const uint8_t* buffer, uint64_t length)
{
    try {
        std::ofstream file(filepath, std::ios::binary | std::ios::trunc);
        if (!file.is_open()) {
            ERRLOG("failed to open binary file %s, errno: %d", filepath.c_str(), errno);
            return false;
        }
        file.write(reinterpret_cast<const char*>(buffer), length);
        if (file.fail()) {
            file.close();
            ERRLOG("failed to write binary file %s, size %llu, errno: %d", filepath.c_str(), length, errno);
            return false;
        }
        file.close();
    } catch (const std::exception& e) {
        ERRLOG("failed to save binary file %s, exception: %s", filepath.c_str(), e.what());
        return false;
    } catch (...) {
        ERRLOG("failed to save binary file %s, exception caught", filepath.c_str());
        return false;
    }
    return true;
}

#ifdef _WIN32
static uint64_t GetVolumeSizeWin32(const std::string& devicePath)
{
    std::wstring wDevicePath = Utf8ToUtf16(devicePath);
    // Open the device
    HANDLE hDevice = ::CreateFileW(
        wDevicePath.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS,
        nullptr);
    if (hDevice == INVALID_HANDLE_VALUE) {
        // Failed to open handle
        throw SystemApiException("failed to open volume", ::GetLastError());
        return 0;
    }
    // Query the length information
    GET_LENGTH_INFORMATION lengthInfo {};
    DWORD bytesReturned = 0;
    if (!::DeviceIoControl(
        hDevice,
        IOCTL_DISK_GET_LENGTH_INFO,
        nullptr,
        0,
        &lengthInfo,
        sizeof(GET_LENGTH_INFORMATION),
        &bytesReturned,
        nullptr)) {
        // Failed to query length
        ::CloseHandle(hDevice);
        throw SystemApiException("failed to call IOCTL_DISK_GET_LENGTH_INFO", ::GetLastError());
        return 0;
    }
    ::CloseHandle(hDevice);
    return lengthInfo.Length.QuadPart;
}
#endif

#ifdef __linux__
static uint64_t GetVolumeSizeLinux(const std::string& devicePath)
{
    int fd = ::open(devicePath.c_str(), O_RDONLY);
    if (fd < 0) {
        throw SystemApiException("failed to open device", errno);
        return 0;
    }
    uint64_t size = 0;
    if (::ioctl(fd, BLKGETSIZE64, &size) < 0) {
        close(fd);
        throw SystemApiException("failed to execute ioctl BLKGETSIZE64", errno);
        return 0;
    }
    ::close(fd);
    return size;
}

#endif

uint64_t fsapi::ReadVolumeSize(const std::string& volumePath)
{
    uint64_t size = 0;
    try {
#ifdef _WIN32
        size = GetVolumeSizeWin32(volumePath);
#endif
#ifdef __linux__
        size = GetVolumeSizeLinux(volumePath);
#endif
    } catch (const SystemApiException& e) {
        throw e;
        return 0;
    }
    return size;
}

bool fsapi::IsVolumeExists(const std::string& volumePath)
{
    try {
        ReadVolumeSize(volumePath);
    } catch (...) {
        return false;
    }
    return true;
}

bool fsapi::CreateEmptyFile(const std::string& dirPath, const std::string& filename)
{
    std::string fullpath = dirPath + SEPARATOR + filename;
#ifdef __linux__
    int fd = ::open(fullpath.c_str(), O_CREAT | O_WRONLY, 0644);
    if (fd == -1) {
        return false;
    }
    ::close(fd);
    return true;
#endif
#ifdef _WIN32
    HANDLE hFile = CreateFileW(
        Utf8ToUtf16(fullpath).c_str(),
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        CREATE_ALWAYS,
        FILE_FLAG_BACKUP_SEMANTICS,
        NULL
    );
    if (hFile == INVALID_HANDLE_VALUE) {
        return false;
    }
    ::CloseHandle(hFile);
    return true;
#endif
}

bool fsapi::RemoveFile(const std::string& dirPath, const std::string& filename)
{
    std::string fullpath = dirPath + SEPARATOR + filename;
#ifdef __linux__
    if (::access(fullpath.c_str(), F_OK) == 0 && ::unlink(fullpath.c_str()) < 0) {
        return false;
    }
    return true;
#endif
#ifdef _WIN32
    return ::DeleteFileW(Utf8ToUtf16(fullpath).c_str());
#endif
}

uint32_t fsapi::ProcessorsNum()
{
#ifdef __linux
    auto processorCount = sysconf(_SC_NPROCESSORS_ONLN);
    return processorCount <= 0 ? DEFAULT_PROCESSORS_NUM : processorCount;
#endif
#ifdef _WIN32
    SYSTEM_INFO systemInfo;
    ::GetSystemInfo(&systemInfo);
    DWORD processorCount = systemInfo.dwNumberOfProcessors;
    return processorCount <= 0 ? DEFAULT_PROCESSORS_NUM : processorCount;
#endif
}

#ifdef __linux__
uint64_t fsapi::ReadSectorSizeLinux(const std::string& devicePath)
{
    int fd = ::open(devicePath.c_str(), O_RDONLY);
    if (fd == -1) {
        throw SystemApiException("failed to open block device", errno);
        return 0;
    }

    uint64_t sectorSize = 0;
    if (::ioctl(fd, BLKSSZGET, &sectorSize) == -1) {
        throw SystemApiException("failed to execute ioctl BLKSSZGET", errno);
        ::close(fd);
        return 0;
    }
    ::close(fd);
    return sectorSize;
}

bool fsapi::IsMountPoint(const std::string& dirPath)
{
	bool mounted = false;
    FILE* mountsFile = ::setmntent(SYS_MOUNTS_ENTRY_PATH.c_str(), "r");
    if (mountsFile == nullptr) {
        ERRLOG("failed to open /proc/mounts, errno %u", errno);
        return false;
    }
    struct mntent* entry = nullptr;
    while ((entry = ::getmntent(mountsFile)) != nullptr) {
        if (std::string(entry->mnt_dir) == dirPath) {
            mounted = true;
            break;
        } 
    }
    ::endmntent(mountsFile);
    return mounted;
}

std::string fsapi::GetMountDevicePath(const std::string& mountTargetPath)
{
	std::string devicePath;
    FILE* mountsFile = ::setmntent(SYS_MOUNTS_ENTRY_PATH.c_str(), "r");
    if (mountsFile == nullptr) {
        ERRLOG("failed to open /proc/mounts, errno %u", errno);
        return "";
    }
    struct mntent* entry = nullptr;
    while ((entry = ::getmntent(mountsFile)) != nullptr) {
        if (std::string(entry->mnt_dir) == dirPath) {
            devicePath = entry->mnt_device;
            break;
        } 
    }
    ::endmntent(mountsFile);
    return devicePath;
}
#endif