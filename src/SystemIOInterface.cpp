#ifdef __linux__
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

#include "SystemIOInterface.h"

using namespace volumeprotect::system;

namespace {
    constexpr auto DEFAULT_PROCESSORS_NUM = 4;
    constexpr auto DEFAULT_MKDIR_MASK = 0755;
#ifdef _WIN32
    constexpr auto SEPARATOR = "\\";
#else
    constexpr auto SEPARATOR = "/";
#endif
}

SystemApiException::SystemApiException(uint32_t errorCode)
{
    m_message = std::string("error code = ") + std::to_string(errorCode);
}

SystemApiException::SystemApiException(const char* message, uint32_t errorCode)
{
    m_message = std::string(message) + " , error code = " + std::to_string(errorCode);
}

const char* SystemApiException::what() const noexcept
{
    return m_message.c_str();
}

#ifdef _WIN32
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

inline void SetOverlappedStructOffset(OVERLAPPED& ov, uint64_t offset)
{
    DWORD *ptr = reinterpret_cast<DWORD*>(&offset);
    ov.Offset = *ptr;
    ov.OffsetHigh = *(ptr + 1);
}
#endif

bool volumeprotect::system::IsValidIOHandle(IOHandle handle)
{
#ifdef __linux__
    return handle > 0;
#endif
#ifdef _WIN32
    return handle != INVALID_HANDLE_VALUE;
#endif
}

void volumeprotect::system::SetHandleInvalid(IOHandle& handle)
{
#ifdef __linux__
    handle = -1;
#endif
#ifdef _WIN32
    handle = INVALID_HANDLE_VALUE;
#endif
}

IOHandle volumeprotect::system::OpenVolumeForRead(const std::string& volumePath)
{
#ifdef __linux__
    int fd = ::open(volumePath.c_str(), O_RDONLY);
    return fd;
#endif
#ifdef _WIN32
    std::wstring wvolumePath = Utf8ToUtf16(volumePath);
    HANDLE hDevice = ::CreateFileW(
        wvolumePath.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS,
        nullptr);
    return hDevice;
#endif
}

IOHandle volumeprotect::system::OpenVolumeForWrite(const std::string& volumePath)
{
#ifdef __linux__
    int fd = ::open(volumePath.c_str(), O_RDWR | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR);
    return fd;
#endif
#ifdef _WIN32
    std::wstring wvolumePath = Utf8ToUtf16(volumePath);
    HANDLE hDevice = ::CreateFileW(
        wvolumePath.c_str(),
        GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS,
        nullptr);
    return hDevice;
#endif
}

void volumeprotect::system::CloseVolume(IOHandle handle)
{
    if (!IsValidIOHandle(handle)) {
        return;
    }
#ifdef __linux__
    ::close(handle);
#endif
#ifdef _WIN32
    ::CloseHandle(handle);
#endif
}

bool volumeprotect::system::ReadVolumeData(IOHandle handle, uint64_t offset, char* buffer, int length, uint32_t& errorCode)
{
#ifdef __linux__
    ::lseek(handle, offset, SEEK_SET);
    int ret = ::read(handle, buffer, length);
    if (ret <= 0 || ret != length) {
        errorCode = errno;
        return false;
    }
    return true;
#endif
#ifdef _WIN32
    OVERLAPPED ov {};
    DWORD bytesReaded = 0;
    SetOverlappedStructOffset(ov, offset);
    if (!::ReadFile(handle, buffer, length, &bytesReaded, &ov) || bytesReaded != length) {
        errorCode = ::GetLastError();
        return false;
    }
    return true;
#endif
}

bool volumeprotect::system::WriteVolumeData(IOHandle handle, uint64_t offset, char* buffer, int length, uint32_t& errorCode)
{
#ifdef __linux__
    ::lseek(handle, offset, SEEK_SET);
    int ret = ::write(handle, buffer, length);
    if (ret <= 0 || ret != length) {
        errorCode = errno;
        return false;
    }
    return true;
#endif
#ifdef _WIN32
    OVERLAPPED ov {};
    DWORD bytesWrited = 0;
    SetOverlappedStructOffset(ov, offset);
    if (!::WriteFile(handle, buffer, length, &bytesWrited, &ov) && bytesWrited != length) {
        errorCode = ::GetLastError();
        return false;
    }
    return true;
#endif
}

bool volumeprotect::system::SetIOPointer(IOHandle handle, uint64_t offset)
{
    if (!IsValidIOHandle(handle)) {
        return false;
    }
#ifdef __linux__
    ::lseek(handle, offset, SEEK_SET);
    return true;
#endif
#ifdef _WIN32
    LARGE_INTEGER liOffset;
    liOffset.QuadPart = offset;
    LARGE_INTEGER liNewPos {};
    if (!::SetFilePointerEx(handle, liOffset, &liNewPos, FILE_BEGIN)) {
        return false;
    }
    return true;
#endif
}

uint32_t volumeprotect::system::GetLastError()
{
#ifdef __linux__
    return errno;
#endif
#ifdef _WIN32
    return ::GetLastError();
#endif
}

bool volumeprotect::system::TruncateCreateFile(const std::string& path, uint64_t size)
{
#ifdef __linux__
    if (::truncate(path.c_str(), size) < 0) {
        return false;
    }
    return true;
#endif
#ifdef _WIN32
    std::wstring wPath = Utf8ToUtf16(path);
    HANDLE hFile = ::CreateFileW(
        wPath.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        CREATE_ALWAYS,
        FILE_FLAG_BACKUP_SEMANTICS,
        nullptr
    );
    if (hFile == INVALID_HANDLE_VALUE) {
        return false;
    }
    
    // check if filesystem support sparse file
    DWORD fileSystemFlags = 0;
    if (::GetVolumeInformationByHandleW(hFile, nullptr, 0, nullptr, nullptr, &fileSystemFlags, nullptr, 0) &&
        (fileSystemFlags & FILE_SUPPORTS_SPARSE_FILES) != 0) {
        // Set the file size to the desired size using DeviceIoControl
        DWORD dwDummy;
        ::DeviceIoControl(hFile, FSCTL_SET_SPARSE, nullptr, 0, nullptr, 0, &dwDummy, nullptr);
        // if truncate sparse file failed, fallback to truncate common file
    }

    LARGE_INTEGER li {};
    li.QuadPart = size;
    if (!::SetFilePointerEx(hFile, li, nullptr, FILE_BEGIN)) {
        ::CloseHandle(hFile);
        return false;
    }
    if (!SetEndOfFile(hFile)) {
        ::CloseHandle(hFile);
        return false;
    }
    ::CloseHandle(hFile);
    return true;
#endif
}

bool volumeprotect::system::IsFileExists(const std::string& path)
{
#ifdef __linux__
    struct stat st;
    return ::stat(path.c_str(), &st) == 0;
#endif
#ifdef _WIN32
    std::wstring wpath = Utf8ToUtf16(path);
    DWORD attributes = ::GetFileAttributesW(wpath.c_str());
    return (attributes != INVALID_FILE_ATTRIBUTES) && ((attributes & FILE_ATTRIBUTE_DIRECTORY) != 0);
#endif
}

uint64_t volumeprotect::system::GetFileSize(const std::string& path)
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

bool volumeprotect::system::IsDirectoryExists(const std::string& path)
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
static uint64_t GetVolumeSizeLinux(const std::string& devicePath) {
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

uint64_t volumeprotect::system::ReadVolumeSize(const std::string& volumePath)
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

bool volumeprotect::system::IsVolumeExists(const std::string& volumePath)
{
    try {
        ReadVolumeSize(volumePath);
    } catch (...) {
        return false;
    }
    return true;
}

uint32_t volumeprotect::system::ProcessorsNum()
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