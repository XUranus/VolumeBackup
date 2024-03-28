/*
 * ================================================================
 *   Copyright (C) 2023-2024 XUranus All rights reserved.
 *
 *   File:         vshow.cpp
 *   Author:       XUranus
 *   Date:         2023-07-01
 *   Description:  a command line tool to display volume info
 * ==================================================================
 */

#include "GetOption.h"

#include <cstdio>
#include <iostream>
#include <cstdint>
#include <vector>
#include <map>
#include <string>
#include <stdexcept>
#include <exception>

#ifdef _WIN32
#define UNICODE
#include <locale>
#include <codecvt>
#include <Windows.h>
#include <devpkey.h>
#include <SetupAPI.h>
#endif

#ifdef __linux__
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/fs.h>
#include <unistd.h>

#include "native/linux/BlockProbeUtils.h"

using namespace volumeprotect;
using namespace volumeprotect::linuxmountutil;

#endif

using namespace xuranus::getopt;

namespace {
    // NT kernel space device path starts with "\Device" while user space device path starts with "\\."
    constexpr auto WKERNEL_SPACE_DEVICE_PATH_PREFIX = LR"(\Device)";
    constexpr auto WUSER_SPACE_DEVICE_PATH_PREFIX = LR"(\\.)";
    constexpr auto WDEVICE_PHYSICAL_DRIVE_PREFIX = LR"(\\.\PhysicalDrive)";
    constexpr auto WDEVICE_HARDDISK_VOLUME_PREFIX = LR"(\\.\HarddiskVolume)";
}

static const char* g_helpMessage =
    "vshow [options...]    util for getting local volume information\n"
    "[ -v | --volume= ]     query specified volume information\n"
    "[ -l | --list ]        list all local volumes\n"
    "[ -h | --help ]        show help\n";

class SystemApiException : public std::exception {
public:
    // Constructor
    SystemApiException(const char* message, uint32_t errorCode) {
        m_message = std::string(message) + " , error code = " + std::to_string(errorCode);
    }

    // Override the what() method to provide a description of the exception
    const char* what() const noexcept override {
        return m_message.c_str();
    }

private:
    std::string m_message;
};

/**
 * @brief logic volume info
 * https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-getvolumeinformationbyhandlew
 *
 */
struct VolumeInfo {
    std::string     volumeName;             // volume 'name' or 'label'
    uint64_t        volumeSize;
    uint32_t        serialNumber;           // msdos vfat 8 bytes serial number
    std::string     uuid;
    uint32_t        maximumComponentLength; // maximum length of filename the fs support
    std::string     fileSystemName;         // 'fileSystemName' or 'fs_type' for *nix
    uint32_t        fileSystemFlags;        // ntfs filesystem flags
};

#ifdef _WIN32
std::wstring GUID2WStr(GUID guid)
{
    LPOLESTR wguidBuf = nullptr;
    HRESULT hr = ::StringFromIID(guid, &wguidBuf);
    std::wstring wGuidStr(wguidBuf);
    return wGuidStr;
}

std::wstring Utf8ToUtf16(const std::string& str)
{
    using ConvertTypeX = std::codecvt_utf8_utf16<wchar_t>;
    std::wstring_convert<ConvertTypeX> converterX;
    std::wstring wstr = converterX.from_bytes(str);
    return wstr;
}

std::string Utf16ToUtf8(const std::wstring& wstr)
{
    using ConvertTypeX = std::codecvt_utf8_utf16<wchar_t>;
    std::wstring_convert<ConvertTypeX> converterX;
    return converterX.to_bytes(wstr);
}

uint64_t GetVolumeSizeWin32(const std::string& devicePath)
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

VolumeInfo GetVolumeInfoWin32(const std::string& volumePath)
{
    std::wstring wDevicePath = Utf8ToUtf16(volumePath);
    VolumeInfo volumeInfo {};
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
        throw SystemApiException("failed to open volume", ::GetLastError());
        return volumeInfo;
    }
    // Query the partition information
    WCHAR volumeNameBuffer[MAX_PATH] = { 0 };
    DWORD nVolumeNameSize = MAX_PATH;
    DWORD volumeSerialNumber = 0;
    DWORD maximumComponentLength = 0;
    DWORD fileSystemFlags = 0;
    WCHAR fileSystemNameBuffer[MAX_PATH] = { 0 };
    DWORD nFileSystemNameSize = MAX_PATH;
    if (!::GetVolumeInformationByHandleW(
        hDevice,
        volumeNameBuffer,
        nVolumeNameSize,
        &volumeSerialNumber,
        &maximumComponentLength,
        &fileSystemFlags,
        fileSystemNameBuffer,
        nFileSystemNameSize)) {
        ::CloseHandle(hDevice);
        throw SystemApiException("failed to call GetVolumeInformationByHandleW", ::GetLastError());
        return volumeInfo;
    }
    // assign volume info struct
    volumeInfo.label = Utf16ToUtf8(volumeNameBuffer);
    volumeInfo.serialNumber = volumeSerialNumber;
    volumeInfo.maximumComponentLength = maximumComponentLength;
    volumeInfo.fileSystemFlags = fileSystemFlags;
    volumeInfo.fileSystemName = Utf16ToUtf8(fileSystemNameBuffer);
    try {
        volumeInfo.volumeSize = GetVolumeSizeWin32(volumePath);
    } catch (const SystemApiException& e) {
        ::CloseHandle(hDevice);
        throw e;
        return volumeInfo;
    }
    ::CloseHandle(hDevice);
    return volumeInfo;
}
#endif


std::vector<std::string> ParseFileSystemFlagsOfVolume(uint32_t flag)
{
    std::vector<std::pair<std::string, uint32_t>> flagsSet = {
        { "FILE_CASE_SENSITIVE_SEARCH", 0x00000001 },
        { "FILE_CASE_PRESERVED_NAMES", 0x00000002 },
        { "FILE_UNICODE_ON_DISK", 0x00000004 },
        { "FILE_PERSISTENT_ACLS", 0x00000008 },
        { "FILE_FILE_COMPRESSION", 0x00000010 },
        { "FILE_VOLUME_QUOTAS", 0x00000020 },
        { "FILE_SUPPORTS_SPARSE_FILES", 0x00000040 },
        { "FILE_SUPPORTS_REPARSE_POINTS", 0x00000080 },
        { "FILE_VOLUME_IS_COMPRESSED", 0x00008000 },
        { "FILE_SUPPORTS_OBJECT_IDS", 0x00010000 },
        { "FILE_SUPPORTS_ENCRYPTION", 0x00020000 },
        { "FILE_NAMED_STREAMS", 0x00040000 },
        { "FILE_READ_ONLY_VOLUME", 0x00080000 },
        { "FILE_SEQUENTIAL_WRITE_ONCE", 0x00100000 },
        { "FILE_SUPPORTS_TRANSACTIONS", 0x00200000 },
        { "FILE_SUPPORTS_HARD_LINKS", 0x00400000 },
        { "FILE_SUPPORTS_EXTENDED_ATTRIBUTES", 0x00800000 },
        { "FILE_SUPPORTS_OPEN_BY_FILE_ID", 0x01000000 },
        { "FILE_SUPPORTS_USN_JOURNAL", 0x02000000 },
        { "FILE_SUPPORTS_BLOCK_REFCOUNTING", 0x08000000 }
    };
    std::vector<std::string> res;
    for (const std::pair<std::string, uint32_t>& p : flagsSet) {
        if ((p.second & flag) != 0) {
            res.emplace_back(p.first);
        }
    }
    return res;
}

#ifdef __linux__
uint64_t GetVolumeSizeLinux(const std::string& devicePath) {
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

VolumeInfo GetVolumeInfoLinux(const std::string& volumePath)
{
    VolumeInfo volumeInfo {};
    try {
        volumeInfo.volumeSize = GetVolumeSizeLinux(volumePath);
    } catch (const SystemApiException& e) {
        throw e;
    }
    std::vector<std::string> tags {
        linuxmountutil::BLKID_PROBE_TAG_LABEL,
        linuxmountutil::BLKID_PROBE_TAG_TYPE,
        linuxmountutil::BLKID_PROBE_TAG_UUID
    };
    std::map<std::string, std::string> blkidResult = linuxmountutil::BlockProbeLookup(volumePath, tags);
    volumeInfo.fileSystemName = blkidResult[linuxmountutil::BLKID_PROBE_TAG_TYPE];
    volumeInfo.uuid = blkidResult[linuxmountutil::BLKID_PROBE_TAG_UUID];
    volumeInfo.volumeName = blkidResult[linuxmountutil::BLKID_PROBE_TAG_LABEL];
    return volumeInfo;
}
#endif


int PrintHelp()
{
    ::printf("%s\n", g_helpMessage);
    return 0;
}

int PrintVolumeInfo(const std::string& volumePath)
{
    VolumeInfo volumeInfo;
    try {
#ifdef _WIN32
        volumeInfo = GetVolumeInfoWin32(volumePath);
#endif
#ifdef __linux__
        volumeInfo = GetVolumeInfoLinux(volumePath);
#endif
    } catch (const SystemApiException& e) {
        std::cerr << e.what() << std::endl;
        return 1;
    }
    std::cout << "VolumeName: " << volumeInfo.volumeName << std::endl;
    std::cout << "Volume UUID: " << volumeInfo.uuid << std::endl;
    std::cout << "Volume Size: " << volumeInfo.volumeSize << std::endl;
    std::cout << "Volume Serial Number: " << volumeInfo.serialNumber << std::endl;
    std::cout << "Maximum Component Length: " << volumeInfo.maximumComponentLength << std::endl;
    std::cout << "Filesystem Name: " << volumeInfo.fileSystemName << std::endl;
    std::cout << "Filesystem Flags: " << volumeInfo.fileSystemFlags << std::endl;
    for (const std::string& flagStr : ParseFileSystemFlagsOfVolume(volumeInfo.fileSystemFlags)) {
        std::cout << flagStr << std::endl;
    }
    std::cout << std::endl;
    return 0;
}

#ifdef _WIN32
std::vector<std::wstring> GetWin32VolumePathListW(const std::wstring& wVolumeName)
{
    /* https://learn.microsoft.com/en-us/windows/_WIN32/fileio/displaying-volume-paths */
    if (wVolumeName.size() < 4 ||
        wVolumeName[0] != L'\\' ||
        wVolumeName[1] != L'\\' ||
        wVolumeName[2] != L'?' ||
        wVolumeName[3] != L'\\' ||
        wVolumeName.back() != L'\\') { /* illegal volume name */
        return {};
    }
    std::vector<std::wstring> wPathList;
    PWCHAR devicePathNames = nullptr;
    DWORD charCount = MAX_PATH + 1;
    bool success = false;
    while (true) {
        devicePathNames = (PWCHAR) new BYTE[charCount * sizeof(WCHAR)];
        if (!devicePathNames) { /* failed to malloc on heap */
            return {};
        }
        success = ::GetVolumePathNamesForVolumeNameW(
            wVolumeName.c_str(),
            devicePathNames,
            charCount,
            &charCount);
        if (success || ::GetLastError() != ERROR_MORE_DATA) {
            break;
        }
        delete[] devicePathNames;
        devicePathNames = nullptr;
    }
    if (success) {
        for (PWCHAR nameIdx = devicePathNames;
            nameIdx[0] != L'\0';
            nameIdx += ::wcslen(nameIdx) + 1) {
            wPathList.push_back(std::wstring(nameIdx));
        }
    }
    if (devicePathNames != nullptr) {
        delete[] devicePathNames;
        devicePathNames = nullptr;
    }
    return wPathList;
}


int PrintWin32VolumeList()
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
    int i = 0;
    for (const std::wstring& wVolumeName : wVolumesNames) {
        if (wVolumeName.size() < 4 ||
            wVolumeName[0] != L'\\' ||
            wVolumeName[1] != L'\\' ||
            wVolumeName[2] != L'?' ||
            wVolumeName[3] != L'\\' ||
            wVolumeName.back() != L'\\') { // illegal volume name
            continue;
        }
        std::wcout << L"Name: " << wVolumeName << std::endl;
        std::wstring wVolumeParam = wVolumeName;
        wVolumeParam.pop_back(); // QueryDosDeviceW does not allow a trailing backslash
        wVolumeParam = wVolumeParam.substr(4);
        WCHAR wDeviceNameBuffer[MAX_PATH] = L"";
        DWORD charCount = ::QueryDosDeviceW(wVolumeParam.c_str(), wDeviceNameBuffer, ARRAYSIZE(wDeviceNameBuffer));
        if (charCount == 0) {
            std::wcout << std::endl;
            continue;
        }
        // convert kernel path to user path
        std::wstring wVolumeDevicePath = wDeviceNameBuffer;
        auto pos = wVolumeDevicePath.find(WKERNEL_SPACE_DEVICE_PATH_PREFIX);
        if (pos == 0) {
            wVolumeDevicePath = WUSER_SPACE_DEVICE_PATH_PREFIX +
                wVolumeDevicePath.substr(std::wstring(WKERNEL_SPACE_DEVICE_PATH_PREFIX).length());
        }
        std::wcout << L"Path: " << wVolumeDevicePath << std::endl;
        std::vector<std::wstring> wVolumePathList = GetWin32VolumePathListW(wVolumeName);
        for (const std::wstring& wPath : wVolumePathList) {
            std::wcout << wPath << std::endl;
        }
        std::wcout << std::endl;
    }
    return true;
}
#endif

int PrintVolumeList()
{
#ifdef _WIN32
    return PrintWin32VolumeList();
#else
    return 0;
#endif
}

int main(int argc, char** argv)
{
    GetOptionResult result = GetOption(
        const_cast<const char**>(argv) + 1,
        argc - 1,
        "v:hl",
        { "volume=", "help", "list"});
    for (const OptionResult opt: result.opts) {
        if (opt.option == "h" || opt.option == "help") {
            return PrintHelp();
        } else if (opt.option == "l" || opt.option == "list") {
            return PrintVolumeList();
        } else if (opt.option == "v" || opt.option == "volume") {
            return PrintVolumeInfo(opt.value);
        }
    }
    PrintHelp();
    return 0;
}