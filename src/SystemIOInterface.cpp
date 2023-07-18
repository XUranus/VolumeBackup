#ifdef _WIN32
#include <locale>
#include <codecvt>
#endif

#ifdef __linux__
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/fs.h>
#include <unistd.h>
#endif

#include "SystemIOInterface.h"

using namespace volumeprotect::system;

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

inline void SetOverlappedStructOffset(OVERLAPPED& ov, uint64_t offset)
{
    DWORD *ptr = reinterpret_cast<DWORD*>(&offset);
    ov.Offset = *ptr;
    ov.OffsetHigh = *(ptr + 1);
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
    // Set the file size to the desired size using DeviceIoControl
    DWORD dwDummy;
    if (!::DeviceIoControl(hFile, FSCTL_SET_SPARSE, nullptr, 0, nullptr, 0, &dwDummy, nullptr)) {
        ::CloseHandle(hFile);
        return false;
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