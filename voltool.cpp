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
#include "VolumeUtils.h"

#include <iostream>
#include <cstdint>
#include <vector>
#include <string>
#include <optional>

#ifdef _WIN32
#define UNICODE
#include <locale>
#include <codecvt>
#include <Windows.h>
#include <devpkey.h>
#include <SetupAPI.h>
#endif

using namespace volumeprotect;
using namespace xuranus::getopt;

#ifdef _WIN32
enum class PartitionStyle {
    GPT,
    MBR,
    RAW
};

struct MBRPartition {
    unsigned char   partitionType;
    bool            bootIndicator;
    bool            recognized;
    uint32_t        hiddenSectors;
    std::string     uuid;               // MBR GUID
};

struct GPTPartition {
    std::string     partitionType;
    std::string     uuid;               // GPT uuid
    uint64_t        attribute;
    std::string     name;
};

struct PartitionEntry {
    PartitionStyle  partitionStyle;
    uint64_t        startingOffset;
    uint64_t        partitionLength;
    uint32_t        partitionNumber;
    bool            rewritePartition;
    bool            isServicePartition;
    MBRPartition    mbr;
    GPTPartition    gpt;
};
#endif


void PrintHelp()
{
    std::cout << "usage: voltool -v [device path]" << std::endl;
}

#ifdef _WIN32
#define FAILED(hr) (((HRESULT)(hr)) < 0)

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

std::vector<std::wstring> GetStorageDevicesW()
{
    std::vector<std::wstring> wDevices;
    HANDLE hFindVolume;
    TCHAR volumeName[MAX_PATH];
    TCHAR volumePathNames[MAX_PATH];
    DWORD bufferSize;
    DWORD error;

    hFindVolume = FindFirstVolume(volumeName, ARRAYSIZE(volumeName));
    if (hFindVolume == INVALID_HANDLE_VALUE) {
        error = GetLastError();
        std::cout << "Error: " << error << std::endl;
        return wDevices;
    }

    std::cout << "Volume paths:" << std::endl;

    do {
        std::cout << "Volume: " << volumeName << std::endl;

        if (!GetVolumePathNamesForVolumeName(volumeName, volumePathNames, ARRAYSIZE(volumePathNames), &bufferSize)) {
            error = GetLastError();
            std::cout << "Error: " << error << std::endl;
        } else {
            TCHAR* currentPath = volumePathNames;
            while (*currentPath) {
                std::cout << "Path: " << currentPath << std::endl;
                currentPath += lstrlen(currentPath) + 1; // Move to the next path
            }
        }
    } while (FindNextVolume(hFindVolume, volumeName, ARRAYSIZE(volumeName)));

    FindVolumeClose(hFindVolume);

    return wDevices;
}

void PrintVolumeListWin32()
{
    std::cout << "PrintVolumeListWin32" << std::endl;
    int deviceNo = 0;
    for (const std::wstring& wDevicePath : GetStorageDevicesW()) {
        std::wcout << L"[" << ++deviceNo << L"] " << wDevicePath << std::endl;
    }
    return;
}

void PrintPartitionStructWin32(const PARTITION_INFORMATION_EX& partition)
{
    std::cout << "Starting Offset: " << partition.StartingOffset.QuadPart << " bytes" << std::endl;
    std::cout << "Partition Length: " << partition.PartitionLength.QuadPart << " bytes" << std::endl;
    std::cout << "Partition Number: " << partition.PartitionNumber << std::endl;
    std::cout << "Partition Rewrite: " << partition.RewritePartition << std::endl;
    // Add more information as needed
    if (partition.PartitionStyle == PARTITION_STYLE_RAW) {
        std::cout << "Partition Style: RAW" << std::endl;
    } else if (partition.PartitionStyle == PARTITION_STYLE_GPT) {
        std::cout << "Partition Style: GPT" << std::endl;
        std::wcout << L"Partition Name: " << partition.Gpt.Name << std::endl;
        std::wcout << L"Partition Type: " << GUID2WStr(partition.Gpt.PartitionType) << std::endl;
        std::wcout << L"Partition Id: " << GUID2WStr(partition.Gpt.PartitionId) << std::endl;
        std::wcout << L"Partition Attributes: " << partition.Gpt.Attributes << std::endl;
    } else if (partition.PartitionStyle == PARTITION_STYLE_MBR) {
        std::cout << "Partition Style: MBR" << std::endl;
        std::wcout << L"Partition Type: " << partition.Mbr.PartitionType << std::endl;
        std::wcout << L"Partition BootIndicator: " << partition.Mbr.BootIndicator << std::endl;
        std::wcout << L"Partition RecognizedPartition: " << partition.Mbr.RecognizedPartition << std::endl;
        std::wcout << L"Partition HiddenSectors: " << partition.Mbr.HiddenSectors << std::endl;
    }
}

void PrintVolumeInfoWin32(const std::string& volumePath)
{
    std::cout << "PrintVolumeInfoWin32 " << volumePath << std::endl;
    std::wstring wDevicePath = Utf8ToUtf16(volumePath);
    // Open the device
    HANDLE hDevice = ::CreateFileW(
        wDevicePath.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS,
        NULL);
    if (hDevice != INVALID_HANDLE_VALUE) {
        // Query the partition information
        WCHAR volumeNameBuffer[MAX_PATH] = { 0 };
        DWORD nVolumeNameSize = MAX_PATH;
        DWORD volumeSerialNumber = 0;
        DWORD maximumComponentLength = 0;
        DWORD fileSystemFlags = 0;
        WCHAR fileSystemNameBuffer[MAX_PATH] = { 0 };
        DWORD nFileSystemNameSize = MAX_PATH;
        BOOL result = ::GetVolumeInformationByHandleW(
            hDevice,
            volumeNameBuffer,
            nVolumeNameSize,
            &volumeSerialNumber,
            &maximumComponentLength,
            &fileSystemFlags,
            fileSystemNameBuffer,
            nFileSystemNameSize
        );
        if (result) {
            std::wcout << L"VolumeName: " << volumeNameBuffer << std::endl;
            std::wcout << L"VolumeSerialNumber: " << volumeSerialNumber << std::endl;
            std::wcout << L"MaximumComponentLength: " << maximumComponentLength << std::endl;
            std::wcout << L"FileSystemFlags: " << fileSystemFlags << std::endl;
            std::wcout << L"FileSystemName: " << fileSystemNameBuffer << std::endl;
            // Get Volume Size
            DWORD bytesReturned;
            DISK_GEOMETRY diskGeometry;
            if (!::DeviceIoControl(hDevice,
                    IOCTL_DISK_GET_DRIVE_GEOMETRY,
                    NULL,
                    0,
                    &diskGeometry,
                    sizeof(DISK_GEOMETRY),
                    &bytesReturned,
                    NULL)) {
                std::cout << "Failed to read volume size" << std::endl;
            } else {
                LONGLONG volumeSize = diskGeometry.Cylinders.QuadPart *
                    diskGeometry.TracksPerCylinder *
                    diskGeometry.SectorsPerTrack *
                    diskGeometry.BytesPerSector;
                std::cout << "VolumeSize: " << volumeSize << std::endl;
            }
        } else {
            std::wcout
                << L"Failed to retrieve volume information for device: " << wDevicePath.c_str()
                << ". Error code: " << ::GetLastError() << std::endl;
            return;
        }
        ::CloseHandle(hDevice);
    } else {
        std::wcout << L"Failed to open device: " << wDevicePath.c_str() << L". Error code: " << ::GetLastError() << std::endl;
        return;
    }
    return;
}

void PrintPartitionInfoWin32(const std::string& devicePath)
{
    std::cout << "PrintPartitionInfoWin32 " << devicePath << std::endl;
    std::wstring wDevicePath = Utf8ToUtf16(devicePath);
    // Open the device
    HANDLE hDevice = ::CreateFileW(
        wDevicePath.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS,
        NULL);
    if (hDevice != INVALID_HANDLE_VALUE) {
        // Query the partition information
        PARTITION_INFORMATION_EX partition = { 0 };
        DWORD bytesReturned = 0;

        BOOL result = ::DeviceIoControl(
            hDevice,
            IOCTL_DISK_GET_PARTITION_INFO_EX,
            NULL,
            0,
            &partition,
            sizeof(PARTITION_INFORMATION_EX),
            &bytesReturned,
            NULL);

        if (result) {
            PrintPartitionStructWin32(partition);
        } else {
            std::wcout
                << L"Failed to retrieve partition information for device: " << wDevicePath.c_str()
                << ". Error code: " << ::GetLastError() << std::endl;
            return;
        }
        ::CloseHandle(hDevice);
    } else {
        std::wcout << L"Failed to open device: " << wDevicePath.c_str() << L". Error code: " << ::GetLastError() << std::endl;
        return;
    }
    return;
}



#endif

#ifdef __linux__
void PrintVolumeListLinux()
{
    std::cout << "TODO:: PrintVolumeListLinux" << std::endl;
}

void PrintPartitionInfoLinux(const std::string& volumePath) {

}

void PrintVolumeInfoLinux(const std::string& volumePath)
{
    std::cout << "UUID:  " << util::ReadVolumeUUID(volumePath) << std::endl;
    std::cout << "Type:  " << util::ReadVolumeType(volumePath) << std::endl;
    std::cout << "Label: " << util::ReadVolumeLabel(volumePath) << std::endl;

    int partitionNumber = 0;
    for (const auto& partition : util::ReadVolumePartitionTable(volumePath)) {
        std::cout << "======= partition[" << ++partitionNumber << "] =======" << std::endl;
        std::cout << "filesystem:       " << partition.filesystem << std::endl;
        std::cout << "patitionNumber:   " << partition.patitionNumber << std::endl;
        std::cout << "firstSector:      " << partition.firstSector << std::endl;
        std::cout << "lastSector:       " << partition.lastSector << std::endl;
        std::cout << "totalSectors:     " << partition.totalSectors << std::endl;
        std::cout << std::endl;
    }
}
#endif

int main(int argc, char** argv)
{
    GetOptionResult result = GetOption(
        const_cast<const char**>(argv) + 1,
        argc - 1,
        "v:p:lh",
        { "volume=", "partition=", "list", "help" });
    for (const OptionResult opt: result.opts) {
        if (opt.option == "h" || opt.option == "help") {
            PrintHelp();
            return 0;
        } else if (opt.option == "v" || opt.option == "volume") {
            std::string devicePath = opt.value;
#ifdef __linux__
            PrintVolumeInfoLinux(devicePath);
#endif
#ifdef _WIN32
            PrintVolumeInfoWin32(devicePath);
#endif
            return 0;
        } else if (opt.option == "p" || opt.option == "partition") {
            std::string devicePath = opt.value;
#ifdef __linux__
            PrintPartitionInfoLinux(devicePath);
#endif
#ifdef _WIN32
            PrintPartitionInfoWin32(devicePath);
#endif
            return 0;
        } else if (opt.option == "l" || opt.option == "list") {
            std::string volumePath = opt.value;
#ifdef __linux__
            PrintVolumeListLinux();
#endif
#ifdef _WIN32
            PrintVolumeListWin32();
#endif
            return 0;
        }
    }
    PrintHelp();
    return 0;
}