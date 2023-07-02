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

using namespace volumeprotect;
using namespace xuranus::getopt;

void PrintHelp()
{
    std::cout << "usage: voltool -v [device path]" << std::endl;
}

void PrintVolumeInfo(const std::string& volumePath)
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

int main(int argc, const char** argv)
{
    GetOptionResult result = GetOption(
        argv + 1,
        argc - 1,
        "v:h",
        { "volume=", "help" });
    for (const OptionResult opt: result.opts) {
        if (opt.option == "h" || opt.option == "help") {
            PrintHelp();
            return 0;
        } else if (opt.option == "v" || opt.option == "volume") {
            std::string volumePath = opt.value;
            PrintVolumeInfo(volumePath);
            return 0;
        }
    }
    PrintHelp();
    return 0;
}