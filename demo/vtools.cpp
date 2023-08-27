/*
 * ================================================================
 *   Copyright (C) 2023 XUranus All rights reserved.
 *
 *   File:         voltool.cpp
 *   Author:       XUranus
 *   Date:         2023-07-01
 *   Description:  a command line tool to display volume info
 * ==================================================================
 */

#include "GetOption.h"

#include <iostream>
#include <cstdint>
#include <vector>
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
#endif

using namespace xuranus::getopt;

class SystemApiException : public std::exception {
public:
    // Constructor
    SystemApiException(uint32_t errorCode) : m_message(nullptr), m_errorCode(errorCode) {}
    SystemApiException(const char* message, uint32_t errorCode) : m_message(message), m_errorCode(errorCode) {}

    // Override the what() method to provide a description of the exception
    const char* what() const noexcept override {
        return "TODO";
    }

private:
    const char* m_message;
    uint32_t    m_errorCode;
};

/**
 * @brief logic volume info
 * https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-getvolumeinformationbyhandlew
 *
 */
struct VolumeInfo {
    std::string     volumeName;
    uint64_t        volumeSize;
    uint32_t        serialNumber;
    uint32_t        maximumComponentLength; // maximum length of filename the fs support
    std::string     fileSystemName;
    uint32_t        fileSystemFlags;
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
    volumeInfo.volumeName = Utf16ToUtf8(volumeNameBuffer);
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
    // TODO:: implement reading linux volume info
    try {
        volumeInfo.volumeSize = GetVolumeSizeLinux(volumePath);
    } catch (const SystemApiException& e) {
        throw e;
        return volumeInfo;
    }
    return volumeInfo;
}
#endif


int PrintHelp()
{
    std::cout << "Usage: voltool -v [path]" << std::endl;
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
    std::cout << "VolumeSize: " << volumeInfo.volumeSize << std::endl;
    std::cout << "VolumeSerialNumber: " << volumeInfo.serialNumber << std::endl;
    std::cout << "MaximumComponentLength: " << volumeInfo.maximumComponentLength << std::endl;
    std::cout << "FileSystemName: " << volumeInfo.fileSystemName << std::endl;
    std::cout << "FileSystemFlags: " << volumeInfo.fileSystemFlags << std::endl;
    for (const std::string& flagStr : ParseFileSystemFlagsOfVolume(volumeInfo.fileSystemFlags)) {
        std::cout << flagStr << std::endl;
    }
    std::cout << std::endl;
    return 0;
}

int main(int argc, char** argv)
{
    GetOptionResult result = GetOption(
        const_cast<const char**>(argv) + 1,
        argc - 1,
        "v:h",
        { "volume=", "help" });
    for (const OptionResult opt: result.opts) {
        if (opt.option == "h" || opt.option == "help") {
            return PrintHelp();
        } else if (opt.option == "v" || opt.option == "volume") {
            return PrintVolumeInfo(opt.value);
        }
    }
    PrintHelp();
    return 0;
}