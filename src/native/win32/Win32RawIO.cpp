#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN 1
#define UNICODE /* foring using WCHAR on windows */
#define NOGDI

#include <locale>
#include <codecvt>
#include <Windows.h>
#include <winioctl.h>

#include "win32/Win32RawIO.h"

using namespace volumeprotect;
using namespace volumeprotect::rawio;
using namespace volumeprotect::rawio::win32;

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

inline void SetOverlappedStructOffset(OVERLAPPED& ov, uint64_t offset)
{
    DWORD *ptr = reinterpret_cast<DWORD*>(&offset);
    ov.Offset = *ptr;
    ov.OffsetHigh = *(ptr + 1);
}

// implement Win32RawDataReader methods...
Win32RawDataReader::Win32RawDataReader(const std::string& path, int flag, uint64_t shiftOffset)
    : m_flag(flag), m_shiftOffset(shiftOffset)
{
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
}

bool Win32RawDataReader::Read(uint64_t offset, uint8_t* buffer, int length, ErrCodeType& errorCode)
{
    if (m_flag > 0) {
        offset += m_shiftOffset;
    } else if (m_flag < 0) {
        offset -= m_shiftOffset;
    }
    OVERLAPPED ov {};
    DWORD bytesReaded = 0;
    SetOverlappedStructOffset(ov, offset);
    if (!::ReadFile(m_handle, buffer, length, &bytesReaded, &ov) || bytesReaded != length) {
        errorCode = ::GetLastError();
        return false;
    }
    return true;
}

bool Win32RawDataReader::Ok()
{
    return m_handle != INVALID_HANDLE_VALUE;
}

ErrCodeType Win32RawDataReader::Error()
{
    return static_cast<ErrCodeType>(::GetLastError());
}

Win32RawDataReader::~Win32RawDataReader()
{
    if (m_handle == INVALID_HANDLE_VALUE) {
        return;
    }
    ::CloseHandle(m_handle);
    m_handle = INVALID_HANDLE_VALUE;
}

// implement Win32RawDataWriter methods
Win32RawDataWriter::Win32RawDataWriter(const std::string& path, int flag, uint64_t shiftOffset)
    : m_flag(flag), m_shiftOffset(shiftOffset)
{
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
}

bool Win32RawDataWriter::Write(uint64_t offset, uint8_t* buffer, int length, ErrCodeType& errorCode)
{
    if (m_flag > 0) {
        offset += m_shiftOffset;
    } else if (m_flag < 0) {
        offset -= m_shiftOffset;
    }
    OVERLAPPED ov {};
    DWORD bytesWrited = 0;
    SetOverlappedStructOffset(ov, offset);
    if (!::WriteFile(m_handle, buffer, length, &bytesWrited, &ov) && bytesWrited != length) {
        errorCode = ::GetLastError();
        return false;
    }
    return true;
}

bool Win32RawDataWriter::Ok()
{
    return m_handle != INVALID_HANDLE_VALUE;
}

bool Win32RawDataWriter::Flush()
{
    if (!Ok()) {
        return false;
    }
    return ::FlushFileBuffers(m_handle);
}

ErrCodeType Win32RawDataWriter::Error()
{
    return static_cast<ErrCodeType>(::GetLastError());
}

Win32RawDataWriter::~Win32RawDataWriter()
{
    if (m_handle == INVALID_HANDLE_VALUE) {
        return;
    }
    ::CloseHandle(m_handle);
    m_handle = INVALID_HANDLE_VALUE;
}


// implement Win32VirtualDiskVolumeRawDataReader methods...
Win32VirtualDiskVolumeRawDataReader::Win32VirtualDiskVolumeRawDataReader(const std::string& virtualDiskFilePath, bool autoDetach)
{
    // TODO
}

~Win32VirtualDiskVolumeRawDataReader::Win32VirtualDiskVolumeRawDataReader()
{
    // TODO
}

bool Win32VirtualDiskVolumeRawDataReader::Read(uint64_t offset, uint8_t* buffer, int length, ErrCodeType& errorCode)
{
    // TODO
    return true;
}

bool Win32VirtualDiskVolumeRawDataReader::Ok()
{
    // TODO
    return true;
}

ErrCodeType Win32VirtualDiskVolumeRawDataReader::Error()
{
    // TODO
    return 0;
}

// implement Win32VirtualDiskVolumeRawDataWriter methods...
Win32VirtualDiskVolumeRawDataWriter::Win32VirtualDiskVolumeRawDataWriter(const std::string& path, bool autoDetach)
{
    // TODO
}

~Win32VirtualDiskVolumeRawDataWriter::Win32VirtualDiskVolumeRawDataWriter()
{
    // TODO
}

bool Win32VirtualDiskVolumeRawDataWriter::Write(uint64_t offset, uint8_t* buffer, int length, ErrCodeType& errorCode)
{
    // TODO
    return true;
}

bool Win32VirtualDiskVolumeRawDataWriter::Ok()
{
    // TODO
    return true;
}

bool Win32VirtualDiskVolumeRawDataWriter::Flush()
{
    // TODO
    return true;
}

ErrCodeType Win32VirtualDiskVolumeRawDataWriter::Error()
{
    // TODO
    return 0;
}

// implement static functions...
bool rawio::TruncateCreateFile(const std::string& path, uint64_t size, ErrCodeType& errorCode)
{
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
}

static bool volumeprotect::rawio::win32::CreateFixedVHDFile(
    const std::string&  filePath,
    uint64_t            volumeSize,
    ErrCodeType&        errorCode)
{
    // TODO
    return false;
}

static bool volumeprotect::rawio::win32::CreateFixedVHDXFile(
    const std::string&  filePath,
    uint64_t            volumeSize,
    ErrCodeType&        errorCode)
{
    // TODO
    return false;
}

static bool volumeprotect::rawio::win32::CreateDynamicVHDFile(
    const std::string&  filePath,
    uint64_t            volumeSize,
    ErrCodeType&        errorCode)
{
    // TODO
    return false;
}

static bool volumeprotect::rawio::win32::CreateDynamicVHDXFile(
    const std::string&  filePath,
    uint64_t            volumeSize,
    ErrCodeType&        errorCode)
{
    // TODO
    return false;
}

static bool volumeprotect::rawio::win32::InitVirtualDiskGPT(
    const std::string&  filePath,
    uint64_t            volumeSize,
    ErrCodeType&        errorCode)
{
    // TODO
    return false;
}

static bool volumeprotect::rawio::win32::AttachVirtualDisk(
    const std::string&  virtualDiskFilePath,
    std::string&        mountedDevicePath,
    ErrorCodeType&      errorCode)
{
    // TODO
    return false;
}

#endif