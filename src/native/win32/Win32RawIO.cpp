#ifdef _WIN32

#define UNICODE /* foring using WCHAR on windows */
#define NOGDI

#include <locale>
#include <codecvt>

#include <Windows.h>
#include <VirtDisk.h>
#include <winioctl.h>
#include <sddl.h>
#include <setupapi.h>
#include <devguid.h>
#include <initguid.h>
#include <strsafe.h>

#include "Logger.h"
#include "win32/Win32RawIO.h"

DEFINE_GUID(GUID_NULL,
    0x00000000, 0x0000, 0x0000, 0x0000, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00);

DEFINE_GUID(VIRTUAL_STORAGE_TYPE_VENDOR_UNKNOWN,
    0x00000000, 0x0000, 0x0000, 0x0000, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00);

DEFINE_GUID(PARTITION_BASIC_DATA_GUID,
    0xebd0a0a2, 0xb9e5, 0x4433, 0x87, 0xc0, 0x68, 0xb6, 0xb7, 0x26, 0x99, 0xc7);

using namespace volumeprotect;
using namespace rawio;
using namespace rawio::win32;

namespace {
    constexpr uint64_t ONE_MB = 1024LLU * 1024LLU;
    constexpr uint64_t VIRTUAL_DISK_BLOCK_SIZE_PADDING = 2 * ONE_MB;
    // windows GPT partition header take at least 17KB for at both header and footer
    constexpr uint64_t VIRTUAL_DISK_GPT_PARTITION_TABLE_SIZE_MININUM = 34 * 512;
    // reserved 32MB for latter use
    constexpr uint64_t VIRTUAL_DISK_RESERVED_PARTITION_SIZE = 32 * ONE_MB;
    // sparse size for reserved GPT (2 GPT partition table header and footer, 1 reserved 32MB for latter use)
    constexpr uint64_t VIRTUAL_DISK_COPY_RESERVED_SIZE =
        VIRTUAL_DISK_GPT_PARTITION_TABLE_SIZE_MININUM * 2 + VIRTUAL_DISK_RESERVED_PARTITION_SIZE;
    // This is in compliance with the EFI specification
    constexpr int VIRTUAL_DISK_MAX_GPT_PARTITION_COUNT = 128;

    // NT kernel space device path starts with "\Device" while user space device path starts with "\\."
    constexpr auto WKERNEL_SPACE_DEVICE_PATH_PREFIX = LR"(\Device)";
    constexpr auto WUSER_SPACE_DEVICE_PATH_PREFIX = LR"(\\.)";
    constexpr auto WDEVICE_PHYSICAL_DRIVE_PREFIX = LR"(\\.\PhysicalDrive)";
    constexpr auto WDEVICE_HARDDISK_VOLUME_PREFIX = LR"(\\.\HarddiskVolume)";

    constexpr wchar_t* VIRTUAL_DISK_GPT_PARTITION_NAMEW = L"Win32VolumeBackupCopy";
    constexpr int NUM0 = 0;
    constexpr int NUM1 = 1;
    constexpr int NUM2 = 2;
    constexpr int NUM3 = 3;
    constexpr int NUM4 = 4;
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

inline uint64_t VirtualDiskSizePadding2MB(uint64_t lengthInBytes)
{
    if (lengthInBytes % VIRTUAL_DISK_BLOCK_SIZE_PADDING == 0) {
        return lengthInBytes;
    }
    return (lengthInBytes / VIRTUAL_DISK_BLOCK_SIZE_PADDING + (uint64_t)NUM1) * VIRTUAL_DISK_BLOCK_SIZE_PADDING;
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
static bool AttachVirtualDiskAndGetVolumeDevicePath(
    const std::string& virtualDiskFilePath,
    std::string& volumeDevicePath)
{
    std::string physicalDrivePath;
    ErrCodeType errorCode = ERROR_SUCCESS;
    if (!rawio::win32::AttachVirtualDiskCopy(
        virtualDiskFilePath,
        physicalDrivePath,
        errorCode)) {
        ::SetLastError(errorCode);
        return false;
    }
    if (!rawio::win32::GetCopyVolumeDevicePath(
        physicalDrivePath,
        volumeDevicePath,
        errorCode)) {
        ::SetLastError(errorCode);
        return false;
    }
    return true;
}

Win32VirtualDiskVolumeRawDataReader::Win32VirtualDiskVolumeRawDataReader(
    const std::string& virtualDiskFilePath,
    bool autoDetach)
    : m_volumeReader(nullptr), m_virtualDiskFilePath(virtualDiskFilePath), m_autoDetach(autoDetach)
{
    std::string volumeDevicePath;
    if (!AttachVirtualDiskAndGetVolumeDevicePath(virtualDiskFilePath, volumeDevicePath) || volumeDevicePath.empty()) {
        return;
    }
    m_volumeReader = std::make_shared<Win32RawDataReader>(volumeDevicePath, 0, 0);
}

Win32VirtualDiskVolumeRawDataReader::~Win32VirtualDiskVolumeRawDataReader()
{
    m_volumeReader.reset();
    if (!m_autoDetach) {
        return;
    }
    // detach copy when reader is no more used
    ErrCodeType errorCode = ERROR_SUCCESS;
    if (!rawio::win32::DetachVirtualDiskCopy(m_virtualDiskFilePath, errorCode)) {
        ERRLOG("failed to detach virtual disk copy, error %d", errorCode);
    }
}

bool Win32VirtualDiskVolumeRawDataReader::Read(uint64_t offset, uint8_t* buffer, int length, ErrCodeType& errorCode)
{
    return (m_volumeReader == nullptr) ? false : m_volumeReader->Read(offset, buffer, length, errorCode);
}

bool Win32VirtualDiskVolumeRawDataReader::Ok()
{
    return (m_volumeReader == nullptr) ? false : m_volumeReader->Ok();
}

ErrCodeType Win32VirtualDiskVolumeRawDataReader::Error()
{
    return (m_volumeReader == nullptr) ? ::GetLastError() : m_volumeReader->Error();
}

// implement Win32VirtualDiskVolumeRawDataWriter methods...
Win32VirtualDiskVolumeRawDataWriter::Win32VirtualDiskVolumeRawDataWriter(
    const std::string& virtualDiskFilePath,
    bool autoDetach)
    : m_volumeWriter(nullptr), m_virtualDiskFilePath(virtualDiskFilePath), m_autoDetach(autoDetach)
{
    std::string volumeDevicePath;
    if (!AttachVirtualDiskAndGetVolumeDevicePath(virtualDiskFilePath, volumeDevicePath) || volumeDevicePath.empty()) {
        return;
    }
    m_volumeWriter = std::make_shared<Win32RawDataWriter>(volumeDevicePath, 0, 0);
}

Win32VirtualDiskVolumeRawDataWriter::~Win32VirtualDiskVolumeRawDataWriter()
{
    m_volumeWriter.reset();
    if (!m_autoDetach) {
        return;
    }
    // detach copy when reader is no more used
    ErrCodeType errorCode = ERROR_SUCCESS;
    if (!rawio::win32::DetachVirtualDiskCopy(m_virtualDiskFilePath, errorCode)) {
        ERRLOG("failed to detach virtual disk copy, error %d", errorCode);
    }
}

bool Win32VirtualDiskVolumeRawDataWriter::Write(uint64_t offset, uint8_t* buffer, int length, ErrCodeType& errorCode)
{
    return (m_volumeWriter == nullptr) ? false : m_volumeWriter->Write(offset, buffer, length, errorCode);
}

bool Win32VirtualDiskVolumeRawDataWriter::Ok()
{
    return (m_volumeWriter == nullptr) ? false : m_volumeWriter->Ok();
}

bool Win32VirtualDiskVolumeRawDataWriter::Flush()
{
    return (m_volumeWriter == nullptr) ? false : m_volumeWriter->Flush();
}

ErrCodeType Win32VirtualDiskVolumeRawDataWriter::Error()
{
    return (m_volumeWriter == nullptr) ? ::GetLastError() : m_volumeWriter->Error();
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
        VirtualDiskSizePadding2MB(volumeSize + VIRTUAL_DISK_COPY_RESERVED_SIZE),
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
        VirtualDiskSizePadding2MB(volumeSize + VIRTUAL_DISK_COPY_RESERVED_SIZE),
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
        VirtualDiskSizePadding2MB(volumeSize + VIRTUAL_DISK_COPY_RESERVED_SIZE),
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
        VirtualDiskSizePadding2MB(volumeSize + VIRTUAL_DISK_COPY_RESERVED_SIZE),
        VIRTUAL_STORAGE_TYPE_DEVICE_VHDX,
        true);
    errorCode = result;
    return errorCode == ERROR_SUCCESS;
}

// 
bool rawio::win32::AttachVirtualDiskCopy(
    const std::string&  virtualDiskFilePath,
    std::string&        physicalDrivePath,
    ErrCodeType&        errorCode)
{
    // TODO:: check extension *.vhd or *.vhdx
    HANDLE hVirtualDiskFile = INVALID_HANDLE_VALUE;

    // Specify UNKNOWN for both device and vendor so the system will use the
    // file extension to determine the correct VHD format.
    VIRTUAL_STORAGE_TYPE storageType;
    storageType.DeviceId = VIRTUAL_STORAGE_TYPE_DEVICE_UNKNOWN;
    storageType.VendorId = VIRTUAL_STORAGE_TYPE_VENDOR_UNKNOWN;

    OPEN_VIRTUAL_DISK_PARAMETERS openParameters = { 0 };
    openParameters.Version = OPEN_VIRTUAL_DISK_VERSION_1;
    openParameters.Version1.RWDepth = 1024;
    // VIRTUAL_DISK_ACCESS_NONE is the only acceptable access mask for V2 handle opens.
    VIRTUAL_DISK_ACCESS_MASK accessMask = VIRTUAL_DISK_ACCESS_ALL;

    ATTACH_VIRTUAL_DISK_PARAMETERS attachParameters = { 0 };
    attachParameters.Version = ATTACH_VIRTUAL_DISK_VERSION_1;

    ATTACH_VIRTUAL_DISK_FLAG attachFlags = ATTACH_VIRTUAL_DISK_FLAG_PERMANENT_LIFETIME | ATTACH_VIRTUAL_DISK_FLAG_NO_DRIVE_LETTER;

    DWORD opStatus = ERROR_SUCCESS;

    PSECURITY_DESCRIPTOR pSecurityDescriptor = NULL;

    std::wstring wVirtualDiskFilePath = Utf8ToUtf16(virtualDiskFilePath);
    opStatus = ::OpenVirtualDisk(
        &storageType,
        wVirtualDiskFilePath.c_str(),
        accessMask,
        OPEN_VIRTUAL_DISK_FLAG_NONE,
        &openParameters,
        &hVirtualDiskFile
    );

    if (opStatus != ERROR_SUCCESS) {
        errorCode = opStatus;
        return false;
    }

    // Create the world-RW SD, granting "Generic All" permissions to everyone
    if (!::ConvertStringSecurityDescriptorToSecurityDescriptorW(
        L"O:BAG:BAD:(A;;GA;;;WD)",
        SDDL_REVISION_1,
        &pSecurityDescriptor,
        NULL)) {
        errorCode = ::GetLastError();
        ::CloseHandle(hVirtualDiskFile);
        return false;
    }

    opStatus = ::AttachVirtualDisk(
        hVirtualDiskFile,
        pSecurityDescriptor,//sd
        attachFlags,
        0,
        &attachParameters,
        NULL);

    if (opStatus != ERROR_SUCCESS && opStatus != ERROR_SHARING_VIOLATION) {
        errorCode = opStatus;
        ::LocalFree(pSecurityDescriptor);
        ::CloseHandle(hVirtualDiskFile);
        return false;
    }

    // Now we need to grab the device name \\.\PhysicalDrive#
    WCHAR wPhysicalDriveName[MAX_PATH];
    ::ZeroMemory(wPhysicalDriveName, sizeof(wPhysicalDriveName));
    DWORD wPhysicalDriveNameLength = sizeof(wPhysicalDriveName) / sizeof(WCHAR);

    opStatus = ::GetVirtualDiskPhysicalPath(hVirtualDiskFile, &wPhysicalDriveNameLength, wPhysicalDriveName);
    if(opStatus != ERROR_SUCCESS) {
        // Unable to retrieve virtual disk path
        ::LocalFree(pSecurityDescriptor);
        ::CloseHandle(hVirtualDiskFile);
        return false;
    }
    physicalDrivePath = Utf16ToUtf8(wPhysicalDriveName);

    ::LocalFree(pSecurityDescriptor);
    ::CloseHandle(hVirtualDiskFile);
    return true;
}

bool rawio::win32::DetachVirtualDiskCopy(const std::string& virtualDiskFilePath, ErrCodeType& errorCode)
{
    // TODO:: check extension *.vhd or *.vhdx
    HANDLE hVirtualDiskFile = INVALID_HANDLE_VALUE;

    // Specify UNKNOWN for both device and vendor so the system will use the
    // file extension to determine the correct VHD format.
    VIRTUAL_STORAGE_TYPE storageType;
    storageType.DeviceId = VIRTUAL_STORAGE_TYPE_DEVICE_UNKNOWN;
    storageType.VendorId = VIRTUAL_STORAGE_TYPE_VENDOR_UNKNOWN;

    OPEN_VIRTUAL_DISK_PARAMETERS openParameters = { 0 };
    openParameters.Version = OPEN_VIRTUAL_DISK_VERSION_1;
    openParameters.Version1.RWDepth = 1024;
    // VIRTUAL_DISK_ACCESS_NONE is the only acceptable access mask for V2 handle opens.
    VIRTUAL_DISK_ACCESS_MASK accessMask = VIRTUAL_DISK_ACCESS_ALL;

    ATTACH_VIRTUAL_DISK_FLAG attachFlags = ATTACH_VIRTUAL_DISK_FLAG_PERMANENT_LIFETIME | ATTACH_VIRTUAL_DISK_FLAG_NO_DRIVE_LETTER;

    DWORD opStatus = ERROR_SUCCESS;

    std::wstring wVirtualDiskFilePath = Utf8ToUtf16(virtualDiskFilePath);
    opStatus = ::OpenVirtualDisk(
        &storageType,
        wVirtualDiskFilePath.c_str(),
        accessMask,
        OPEN_VIRTUAL_DISK_FLAG_NONE,
        &openParameters,
        &hVirtualDiskFile
    );

    if (opStatus != ERROR_SUCCESS) {
        errorCode = opStatus;
        return false;
    }

    opStatus = ::DetachVirtualDisk(
        hVirtualDiskFile,
        DETACH_VIRTUAL_DISK_FLAG_NONE,
        0
    );
    if (opStatus != ERROR_SUCCESS) {
        // failed to detach
        errorCode = opStatus;
        ::CloseHandle(hVirtualDiskFile);
        return false;
    }
    return true;
}

// init partition table GPT create create single GPT partition for the VHD/VHDX copy
bool rawio::win32::InitVirtualDiskGPT(
    const std::string&  physicalDrivePath,
    uint64_t            volumeSize,
    ErrCodeType&        errorCode)
{
    DWORD opStatus = ERROR_SUCCESS;
    std::wstring wPhysicalDrivePath = Utf8ToUtf16(physicalDrivePath);
    HANDLE hDevice = ::CreateFileW(
        wPhysicalDrivePath.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        0,
        NULL);
    if (hDevice == INVALID_HANDLE_VALUE) {
        // failed to open physical drive
        return false;
    }
 
    GET_VIRTUAL_DISK_INFO diskInfo = { 0 };
    diskInfo.Version = GET_VIRTUAL_DISK_INFO_IDENTIFIER;
    ULONG diskInfoSize = sizeof(GET_VIRTUAL_DISK_INFO);

    GUID diskIdentifier = GUID_NULL;
    if (!::UuidCreate(&diskIdentifier) == RPC_S_OK) {
        std::cerr << "Failed to generate Disk GUID." << std::endl;
    }

    if (opStatus != ERROR_SUCCESS) {
        std::cerr << "failed to get vhd GUID identifier " << opStatus << std::endl;
        return false;
    }

    // Prepare the CREATE_DISK structure
    CREATE_DISK createDisk = {0};
    createDisk.PartitionStyle = PARTITION_STYLE_GPT;
    // Set other necessary GPT parameters
    createDisk.Gpt = { 0 }; // Initialize GPT parameters
    createDisk.Gpt.DiskId = diskIdentifier; // Provide a unique disk identifier
    createDisk.Gpt.MaxPartitionCount = VIRTUAL_DISK_MAX_GPT_PARTITION_COUNT;

    // Send the IOCTL_DISK_CREATE_DISK control code
    DWORD bytesReturned;
    if (!::DeviceIoControl(
        hDevice,
        IOCTL_DISK_CREATE_DISK,
        &createDisk,
        sizeof(createDisk),
        NULL,
        0,
        &bytesReturned,
        NULL)) {
        // Error: IOCTL_DISK_CREATE_DISK failed
        ::CloseHandle(hDevice);
        return false;
    }

    // TODO:: hack, wait for MSR partition arrival
    std::this_thread::sleep_for(std::chrono::seconds(2));


    GUID partitionGUID = GUID_NULL;
    if (!::UuidCreate(&partitionGUID) == RPC_S_OK) {
        // Failed to generate partitionGUID
        return false;
    }

    // Start init GPT partition
    DRIVE_LAYOUT_INFORMATION_EX layout = { 0 };
    ZeroMemory(&layout, sizeof(DRIVE_LAYOUT_INFORMATION_EX));
    layout.PartitionStyle = PARTITION_STYLE_GPT;
    layout.PartitionCount = NUM1; // Create only one NTFS/FAT32/ExFAT GPT partition
    layout.Gpt.DiskId = diskIdentifier;
    layout.Gpt.StartingUsableOffset.QuadPart = 0;
    layout.Gpt.UsableLength.QuadPart =
        VIRTUAL_DISK_GPT_PARTITION_TABLE_SIZE_MININUM + VIRTUAL_DISK_RESERVED_PARTITION_SIZE + volumeSize;
    layout.Gpt.MaxPartitionCount = VIRTUAL_DISK_MAX_GPT_PARTITION_COUNT;
    layout.PartitionEntry[0].PartitionStyle = PARTITION_STYLE_GPT;
    layout.PartitionEntry[0].StartingOffset.QuadPart = VIRTUAL_DISK_GPT_PARTITION_TABLE_SIZE_MININUM;
    layout.PartitionEntry[0].PartitionLength.QuadPart = volumeSize;
    layout.PartitionEntry[0].PartitionNumber = NUM1; // 1st partition
    layout.PartitionEntry[0].RewritePartition = FALSE; // do not allow rewrite partition
    layout.PartitionEntry[0].IsServicePartition = FALSE;
    layout.PartitionEntry[0].Gpt.PartitionType = PARTITION_BASIC_DATA_GUID;
    layout.PartitionEntry[0].Gpt.PartitionId = partitionGUID;
    layout.PartitionEntry[0].Gpt.Attributes = GPT_BASIC_DATA_ATTRIBUTE_NO_DRIVE_LETTER;
    wcscpy_s(layout.PartitionEntry[0].Gpt.Name, VIRTUAL_DISK_GPT_PARTITION_NAMEW);

    if (!DeviceIoControl(
        hDevice,
        IOCTL_DISK_SET_DRIVE_LAYOUT_EX,
        &layout,
        sizeof(layout),
        NULL,
        0,
        &bytesReturned,
        NULL)) {
        // Error: IOCTL_DISK_SET_DRIVE_LAYOUT_EX failed
        ::CloseHandle(hDevice);
        return false;
    }

    ::CloseHandle(hDevice);
    return true;
}

// list all win32 volume paths and convert from kernel path to user path : "\Device\HarddiskVolume1"  => \\.\HarddiskVolume1
static bool ListWin32LocalVolumePathW(std::vector<std::wstring>& wVolumeDevicePaths)
{
    WCHAR wVolumeNameBuffer[MAX_PATH] = L"";
    std::vector<std::wstring> wVolumesNames;
    HANDLE handle = ::FindFirstVolumeW(wVolumeNameBuffer, MAX_PATH);
    if (handle == INVALID_HANDLE_VALUE) {
        ::FindVolumeClose(handle);
        /* find failed */
        return false;
    }
    wVolumesNames.push_back(std::wstring(wVolumeNameBuffer));
    while (::FindNextVolumeW(handle, wVolumeNameBuffer, MAX_PATH)) {
        wVolumesNames.push_back(std::wstring(wVolumeNameBuffer));        
    }
    ::FindVolumeClose(handle);
    handle = INVALID_HANDLE_VALUE;

    for (const std::wstring& wVolumeName : wVolumesNames) {
        if (wVolumeName.size() < NUM4 ||
            wVolumeName[NUM0] != L'\\' ||
            wVolumeName[NUM1] != L'\\' ||
            wVolumeName[NUM2] != L'?' ||
            wVolumeName[NUM3] != L'\\' ||
            wVolumeName.back() != L'\\') { // illegal volume name
            continue;
        }
        std::wstring wVolumeParam = wVolumeName;
        wVolumeParam.pop_back(); // QueryDosDeviceW does not allow a trailing backslash
        wVolumeParam = wVolumeParam.substr(NUM4);
        WCHAR wDeviceNameBuffer[MAX_PATH] = L"";
        DWORD charCount = ::QueryDosDeviceW(wVolumeParam.c_str(), wDeviceNameBuffer, ARRAYSIZE(wDeviceNameBuffer));
        if (charCount == 0) {
            continue;
        }
        // convert kernel path to user path
        std::wstring wVolumeDevicePath = wDeviceNameBuffer;
        auto pos = wVolumeDevicePath.find(WKERNEL_SPACE_DEVICE_PATH_PREFIX);
        if (pos == 0) {
            wVolumeDevicePath = WUSER_SPACE_DEVICE_PATH_PREFIX + wVolumeDevicePath.substr(std::wstring(WKERNEL_SPACE_DEVICE_PATH_PREFIX).length());
        }
        wVolumeDevicePaths.emplace_back(wVolumeDevicePath);
    }
    return true;
}

// Get path like \\.\PhysicalDriveX from \\.\HarddiskVolumeX
static bool GetPhysicalDrivePathFromVolumePathW(const std::wstring& wVolumePath, std::wstring& wPhysicalDrivePath)
{
    DWORD bytesReturned = 0;
    HANDLE hDevice = ::CreateFileW(
        wVolumePath.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        0,
        NULL);
    if (hDevice == INVALID_HANDLE_VALUE) {
        return false;
    }
    STORAGE_DEVICE_NUMBER deviceNumber;
    if (!::DeviceIoControl(
        hDevice,
        IOCTL_STORAGE_GET_DEVICE_NUMBER,
        NULL,
        0,
        &deviceNumber,
        sizeof(deviceNumber),
        &bytesReturned,
        NULL)) {
        // failed to execute IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS
        return false;
    }
    wPhysicalDrivePath = std::wstring(WDEVICE_PHYSICAL_DRIVE_PREFIX) + std::to_wstring(deviceNumber.DeviceNumber);
    return true;
}

// Get path like \\.\HarddiskVolumeX from \\.\PhysicalDriveX 
static bool GetVolumePathsFromPhysicalDrivePathW(const std::wstring& wPhysicalDrive, std::vector<std::wstring>& wVolumePathList)
{
    std::vector<std::wstring> wAllVolumePaths;
    if (!ListWin32LocalVolumePathW(wAllVolumePaths)) {
        return false;
    }
    for (const std::wstring wVolumePathTmp: wAllVolumePaths) {
        std::wstring wPhysicalDriveTmp;
        if (GetPhysicalDrivePathFromVolumePathW(wVolumePathTmp, wPhysicalDriveTmp) && wPhysicalDriveTmp == wPhysicalDrive) {
            wVolumePathList.emplace_back(wVolumePathTmp);
        }
    }
    return true;
}

bool rawio::win32::GetCopyVolumeDevicePath(
    const std::string& physicalDrivePath,
    std::string& volumeDevicePath,
    ErrCodeType& errorCode)
{
    std::vector<std::wstring> wVolumePathList;
    wVolumePathList.clear();
    if (GetVolumePathsFromPhysicalDrivePathW(Utf8ToUtf16(physicalDrivePath), wVolumePathList)
        && !wVolumePathList.empty()) {
        volumeDevicePath = Utf16ToUtf8(wVolumePathList.front());
        return true;
    }
    return false;
}

#endif