#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN 1
#define UNICODE /* foring using WCHAR on windows */
#define NOGDI

#include <locale>
#include <codecvt>
#include <Windows.h>
#include <winioctl.h>

#include "win32/Win32RawIO.h"

#include <Windows.h>
#include <VirtDisk.h>
#include <winioctl.h>
#include <sddl.h>

#include <setupapi.h>
#include <devguid.h>
#include <initguid.h>
#include <strsafe.h>

DEFINE_GUID(GUID_NULL,
    0x00000000, 0x0000, 0x0000, 0x0000, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00);

DEFINE_GUID(VIRTUAL_STORAGE_TYPE_VENDOR_UNKNOWN,
    0x00000000, 0x0000, 0x0000, 0x0000, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00);

using namespace volumeprotect;
using namespace rawio;
using namespace rawio::win32;

namespace {
    constexpr auto VIRTUAL_DISK_COPY_SPARE_SIZE = 16 * 512; // sparse size for reserved GPT
}

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
    DWORD bytesReturn = 0;
    m_handle = ::CreateFileW(
        wpath.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS,
        NULL
    );
    if (m_handle == INVALID_HANDLE_VALUE) {
        return;
    } 
    if (!::DeviceIoControl(
        m_handle,
        FSCTL_ALLOW_EXTENDED_DASD_IO,
        NULL,
        0,
        NULL,
        0,
        &bytesReturn,
        NULL)) {
        ::CloseHandle(m_handle);
        m_handle = INVALID_HANDLE_VALUE;
        return;
    }
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
    DWORD bytesReturn = 0;
    m_handle = ::CreateFileW(
        wpath.c_str(),
        GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS,
        NULL
    );
    if (m_handle == INVALID_HANDLE_VALUE) {
        return;
    } 
    if (!::DeviceIoControl(
        m_handle,
        FSCTL_ALLOW_EXTENDED_DASD_IO,
        NULL,
        0,
        NULL,
        0,
        &bytesReturn,
        NULL)) {
        ::CloseHandle(m_handle);
        m_handle = INVALID_HANDLE_VALUE;
        return;
    }
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

Win32VirtualDiskVolumeRawDataReader::~Win32VirtualDiskVolumeRawDataReader()
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

Win32VirtualDiskVolumeRawDataWriter::~Win32VirtualDiskVolumeRawDataWriter()
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
        NULL,
        CREATE_ALWAYS,
        FILE_FLAG_BACKUP_SEMANTICS,
        NULL
    );
    if (hFile == INVALID_HANDLE_VALUE) {
        errorCode = static_cast<ErrCodeType>(::GetLastError());
        return false;
    }

    // check if filesystem support sparse file
    DWORD fileSystemFlags = 0;
    if (::GetVolumeInformationByHandleW(hFile, NULL, 0, NULL, NULL, &fileSystemFlags, NULL, 0) &&
        (fileSystemFlags & FILE_SUPPORTS_SPARSE_FILES) != 0) {
        // Set the file size to the desired size using DeviceIoControl
        DWORD dwDummy;
        ::DeviceIoControl(hFile, FSCTL_SET_SPARSE, NULL, 0, NULL, 0, &dwDummy, NULL);
        // if truncate sparse file failed, fallback to truncate common file
    }

    LARGE_INTEGER li {};
    li.QuadPart = size;
    if (!::SetFilePointerEx(hFile, li, NULL, FILE_BEGIN)) {
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

static DWORD CreateVirtualDiskFile(const std::string& filePath, uint64_t maxinumSize, DWORD deviceID, bool dynamic)
{
    VIRTUAL_STORAGE_TYPE virtualStorageType;
    virtualStorageType.DeviceId = deviceID;;
    virtualStorageType.VendorId = VIRTUAL_STORAGE_TYPE_VENDOR_UNKNOWN;

    // Specify the VHD parameters
    CREATE_VIRTUAL_DISK_PARAMETERS createParams = { 0 };
    createParams.Version = CREATE_VIRTUAL_DISK_VERSION_1;
    createParams.Version1.UniqueId = GUID_NULL;
    createParams.Version1.MaximumSize = maxinumSize;
    createParams.Version1.BlockSizeInBytes = CREATE_VIRTUAL_DISK_PARAMETERS_DEFAULT_BLOCK_SIZE;
    createParams.Version1.SectorSizeInBytes = CREATE_VIRTUAL_DISK_PARAMETERS_DEFAULT_SECTOR_SIZE;
    createParams.Version1.ParentPath = nullptr;
    createParams.Version1.SourcePath = nullptr;
    /*
     * Specify the desired VHD type (fixed or dynamic)
     * for dynamic VHD, it and can be created at once
     * for fixed VHD, it and may take a lot of time to response
     */
    CREATE_VIRTUAL_DISK_FLAG createVirtualDiskFlags = dynamic ? CREATE_VIRTUAL_DISK_FLAG_NONE
        : CREATE_VIRTUAL_DISK_FLAG_FULL_PHYSICAL_ALLOCATION;
    VIRTUAL_DISK_ACCESS_MASK accessMask = VIRTUAL_DISK_ACCESS_ALL;

    std::wstring wVhdPath = Utf8ToUtf16(filePath);
    HANDLE hVhdFile = INVALID_HANDLE_VALUE;
    // Create the VHD
    DWORD result = ::CreateVirtualDisk(
        &virtualStorageType,
        wVhdPath.c_str(),
        accessMask,
        nullptr,
        createVirtualDiskFlags,
        0,
        &createParams,
        nullptr,
        &hVhdFile
    );
    if (hVhdFile != INVALID_HANDLE_VALUE) {
        ::CloseHandle(hVhdFile);
    }
    return result;
}

bool rawio::win32::CreateFixedVHDFile(
    const std::string&  filePath,
    uint64_t            volumeSize,
    ErrCodeType&        errorCode)
{
    DWORD result = CreateVirtualDiskFile(
        filePath,
        volumeSize + VIRTUAL_DISK_COPY_SPARE_SIZE,
        VIRTUAL_STORAGE_TYPE_DEVICE_VHD,
        false);
    errorCode = result;
    return errorCode == ERROR_SUCCESS;
}


bool rawio::win32::CreateFixedVHDXFile(
    const std::string&  filePath,
    uint64_t            volumeSize,
    ErrCodeType&        errorCode)
{
    DWORD result = CreateVirtualDiskFile(
        filePath,
        volumeSize + VIRTUAL_DISK_COPY_SPARE_SIZE,
        VIRTUAL_STORAGE_TYPE_DEVICE_VHDX,
        false);
    errorCode = result;
    return errorCode == ERROR_SUCCESS;
}

bool rawio::win32::CreateDynamicVHDFile(
    const std::string&  filePath,
    uint64_t            volumeSize,
    ErrCodeType&        errorCode)
{
    DWORD result = CreateVirtualDiskFile(
        filePath,
        volumeSize + VIRTUAL_DISK_COPY_SPARE_SIZE,
        VIRTUAL_STORAGE_TYPE_DEVICE_VHD,
        true);
    errorCode = result;
    return errorCode == ERROR_SUCCESS;
}

bool rawio::win32::CreateDynamicVHDXFile(
    const std::string&  filePath,
    uint64_t            volumeSize,
    ErrCodeType&        errorCode)
{
    DWORD result = CreateVirtualDiskFile(
        filePath,
        volumeSize + VIRTUAL_DISK_COPY_SPARE_SIZE,
        VIRTUAL_STORAGE_TYPE_DEVICE_VHDX,
        true);
    errorCode = result;
    return errorCode == ERROR_SUCCESS;
}

bool rawio::win32::InitVirtualDiskGPT(
    const std::string&  filePath,
    uint64_t            volumeSize,
    ErrCodeType&        errorCode)
{
    // TODO
    return false;
}

bool rawio::win32::AttachVirtualDisk(
    const std::string&  virtualDiskFilePath,
    std::string&        mountedDevicePath,
    ErrCodeType&        errorCode)
{
    // TODO
    return false;
}

#endif