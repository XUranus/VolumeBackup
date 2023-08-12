#include <cstdio>
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

#include <iostream>
#include <fstream>
#include "Logger.h"
#include "NativeIOInterface.h"

using namespace volumeprotect;
using namespace volumeprotect::native;

namespace {
    constexpr auto DEFAULT_PROCESSORS_NUM = 4;
    constexpr auto DEFAULT_MKDIR_MASK = 0755;
#ifdef _WIN32
    constexpr auto SEPARATOR = "\\";
    const IOHandle SYSTEM_IO_INVALID_HANDLE = INVALID_HANDLE_VALUE;
#else
    constexpr auto SEPARATOR = "/";
    const IOHandle SYSTEM_IO_INVALID_HANDLE = -1;
#endif
}

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

inline bool IsIOHandleValid(IOHandle handle)
{
#ifdef __linux__
    return handle > 0;
#endif
#ifdef _WIN32
    return handle != INVALID_HANDLE_VALUE;
#endif
}

inline bool LastErrorCode()
{
#ifdef __linux__
    return static_cast<ErrCodeType>(errno);
#endif
#ifdef _WIN32
    return static_cast<ErrCodeType>(::GetLastError());
#endif
}

inline void CloseIOHandle(IOHandle handle)
{
    if (!IsIOHandleValid(handle)) {
        return;
    }
#ifdef __linux__
    ::close(handle);
#endif
#ifdef _WIN32
    ::CloseHandle(handle);
#endif
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

SystemDataReader::SystemDataReader(const std::string& path)
{
#ifdef __linux__
    m_handle = ::open(path.c_str(), O_RDONLY);
#endif
#ifdef _WIN32
    std::wstring wpath = Utf8ToUtf16(path);
    m_handle = ::CreateFileW(
        wpath.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS,
        nullptr
    );
#endif
}

bool SystemDataReader::Read(uint64_t offset, uint8_t* buffer, int length, ErrCodeType& errorCode)
{
#ifdef __linux__
    ::lseek(m_handle, offset, SEEK_SET);
    int ret = ::read(m_handle, buffer, length);
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
    if (!::ReadFile(m_handle, buffer, length, &bytesReaded, &ov) || bytesReaded != length) {
        errorCode = ::GetLastError();
        return false;
    }
    return true;
#endif
}

bool SystemDataReader::Ok()
{
    return IsIOHandleValid(m_handle);
}

ErrCodeType SystemDataReader::Error()
{
    return LastErrorCode();
}

SystemDataReader::~SystemDataReader()
{
    CloseIOHandle(m_handle);
    m_handle = SYSTEM_IO_INVALID_HANDLE;
}

SystemDataWriter::SystemDataWriter(const std::string& path)
{
#ifdef __linux__
    m_handle = ::open(path.c_str(), O_RDWR | O_EXCL, S_IRUSR | S_IWUSR);
#endif
#ifdef _WIN32
    std::wstring wpath = Utf8ToUtf16(path);
    m_handle = ::CreateFileW(
        wpath.c_str(),
        GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS,
        nullptr
    );
#endif
}

bool SystemDataWriter::Write(uint64_t offset, uint8_t* buffer, int length, ErrCodeType& errorCode)
{
#ifdef __linux__
    ::lseek(m_handle, offset, SEEK_SET);
    int ret = ::write(m_handle, buffer, length);
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
    if (!::WriteFile(m_handle, buffer, length, &bytesWrited, &ov) && bytesWrited != length) {
        errorCode = ::GetLastError();
        return false;
    }
    return true;
#endif
}

bool SystemDataWriter::Ok()
{
    return IsIOHandleValid(m_handle);
}

bool SystemDataWriter::Flush()
{
#ifdef __linux__
    if (!Ok()) {
        return false;
    }
    ::fsync(m_handle);
    return true;
#endif
#ifdef _WIN32
    if (!Ok()) {
        return false;
    }
    return ::FlushFileBuffers(m_handle);
#endif
}

ErrCodeType SystemDataWriter::Error()
{
    return LastErrorCode();
}

SystemDataWriter::~SystemDataWriter()
{
    CloseIOHandle(m_handle);
    m_handle = SYSTEM_IO_INVALID_HANDLE;
}

bool native::TruncateCreateFile(const std::string& path, uint64_t size, ErrCodeType& errorCode)
{
#ifdef __linux__
    int fd = ::open(path.c_str(), O_RDWR | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR);
    if (fd < 0) {
        return false;
    }
    ::close(fd);
    if (::truncate(path.c_str(), size) < 0) {
        errorCode = static_cast<ErrCodeType>(errno);
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
        errorCode = static_cast<ErrCodeType>(::GetLastError());
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
        errorCode = static_cast<ErrCodeType>(::GetLastError());
        ::CloseHandle(hFile);
        return false;
    }
    if (!SetEndOfFile(hFile)) {
        errorCode = static_cast<ErrCodeType>(::GetLastError());
        ::CloseHandle(hFile);
        return false;
    }
    ::CloseHandle(hFile);
    return true;
#endif
}

bool native::IsFileExists(const std::string& path)
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

uint64_t native::GetFileSize(const std::string& path)
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

bool native::IsDirectoryExists(const std::string& path)
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
uint8_t* native::ReadBinaryBuffer(const std::string& filepath, uint64_t length)
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
bool native::WriteBinaryBuffer(const std::string& filepath, const uint8_t* buffer, uint64_t length)
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

uint64_t native::ReadVolumeSize(const std::string& volumePath)
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

bool native::IsVolumeExists(const std::string& volumePath)
{
    try {
        ReadVolumeSize(volumePath);
    } catch (...) {
        return false;
    }
    return true;
}

uint32_t native::ProcessorsNum()
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