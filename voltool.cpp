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

#ifdef _WIN32
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
// Function to retrieve the device path from a device information set and device data
std::wstring GetDevicePath(HDEVINFO deviceInfoSet, PSP_DEVINFO_DATA deviceInfoData)
{
    DWORD requiredSize = 0;

    // Retrieve the required size for the device path
    SetupDiGetDeviceInstanceIdW(deviceInfoSet, deviceInfoData, nullptr, 0, &requiredSize);

    std::vector<wchar_t> buffer(requiredSize);
    if (!SetupDiGetDeviceInstanceIdW(deviceInfoSet, deviceInfoData, buffer.data(), requiredSize, nullptr))
    {
        std::cout << "Failed to retrieve device instance ID. Error code: " << GetLastError() << std::endl;
        return L"";
    }

    // Extract the device path from the device instance ID
    std::wstring deviceInstanceID(buffer.data());
    size_t pos = deviceInstanceID.find(L'\\');
    if (pos != std::wstring::npos)
    {
        return deviceInstanceID.substr(pos + 1);
    }

    return L"";
}

// Function to retrieve the list of storage devices and their partition information
std::vector<PARTITION_INFORMATION_EX> GetStorageDevices()
{
    std::vector<PARTITION_INFORMATION_EX> partitionInfoList;

    // Create a device information set for storage devices
    HDEVINFO deviceInfoSet = SetupDiGetClassDevsW(
        &GUID_DEVINTERFACE_DISK,
        NULL,
        NULL,
        DIGCF_DEVICEINTERFACE | DIGCF_PRESENT);

    if (deviceInfoSet == INVALID_HANDLE_VALUE)
    {
        std::cout << "Failed to retrieve device information set. Error code: " << GetLastError() << std::endl;
        return partitionInfoList;
    }

    // Enumerate the devices
    SP_DEVINFO_DATA deviceInfoData = { 0 };
    deviceInfoData.cbSize = sizeof(SP_DEVINFO_DATA);
    DWORD deviceIndex = 0;

    while (SetupDiEnumDeviceInfo(deviceInfoSet, deviceIndex, &deviceInfoData))
    {
        // Retrieve the device path
        std::wstring devicePath = GetDevicePath(deviceInfoSet, &deviceInfoData);

        if (!devicePath.empty())
        {
            // Open the device
            HANDLE hDevice = CreateFileW(
                devicePath.c_str(),
                GENERIC_READ,
                FILE_SHARE_READ | FILE_SHARE_WRITE,
                NULL,
                OPEN_EXISTING,
                0,
                NULL);

            if (hDevice != INVALID_HANDLE_VALUE)
            {
                // Query the partition information
                PARTITION_INFORMATION_EX partitionInfo = { 0 };
                DWORD bytesReturned = 0;

                BOOL result = DeviceIoControl(
                    hDevice,
                    IOCTL_DISK_GET_PARTITION_INFO_EX,
                    NULL,
                    0,
                    &partitionInfo,
                    sizeof(PARTITION_INFORMATION_EX),
                    &bytesReturned,
                    NULL);

                if (result)
                {
                    partitionInfoList.push_back(partitionInfo);
                }
                else
                {
                    std::cout << "Failed to retrieve partition information for device: " << devicePath.c_str() << ". Error code: " << GetLastError() << std::endl;
                }

                CloseHandle(hDevice);
            }
            else
            {
                std::cout << "Failed to open device: " << devicePath.c_str() << ". Error code: " << GetLastError() << std::endl;
            }
        }

        deviceIndex++;
    }

    // Clean up the device information set
    SetupDiDestroyDeviceInfoList(deviceInfoSet);

    return partitionInfoList;
}

void PrintVolumeInfoWin32(const std::string& volumePath)
{
    std::vector<PARTITION_INFORMATION_EX> partitions = GetStorageDevices();
    std::cout << "partition count = " << partitions.size() << std::endl;

    // Display the partition information
    for (const auto& partition : partitions)
    {
        std::cout << "Partition Information:" << std::endl;
        std::cout << "Partition Style: " << partition.PartitionStyle << std::endl;
        std::cout << "Starting Offset: " << partition.StartingOffset.QuadPart << " bytes" << std::endl;
        std::cout << "Partition Length: " << partition.PartitionLength.QuadPart << " bytes" << std::endl;
        // Add more information as needed
        std::cout << std::endl;
    }
}


#endif

#ifdef __linux__
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
        "v:h",
        { "volume=", "help" });
    std::cout << result.args.size() << " " << result.opts.size() << std::endl;
    for (const OptionResult opt: result.opts) {
        if (opt.option == "h" || opt.option == "help") {
            PrintHelp();
            return 0;
        } else if (opt.option == "v" || opt.option == "volume") {
            std::string volumePath = opt.value;
#ifdef __linux__
            PrintVolumeInfoLinux(volumePath);
#endif
#ifdef _WIN32
            PrintVolumeInfoWin32(volumePath);
#endif
            return 0;
        }
    }
    PrintHelp();
    return 0;
}