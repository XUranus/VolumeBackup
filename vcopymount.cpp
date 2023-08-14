/*
 * ================================================================
 *   Copyright (C) 2023 XUranus All rights reserved.
 *
 *   File:         vcopymount.cpp
 *   Author:       XUranus
 *   Date:         2023-08-23
 *   Description:  a command line tool to mount/umount copy 
 *                 generated from volumebackup using dm linear mapper
 * ==================================================================
 */

#include <cstdint>
#include <iostream>
#include <string>
#include "GetOption.h"
#include "VolumeUtils.h"
#include "Logger.h"

using namespace xuranus;
using namespace xuranus::getopt;
using namespace volumeprotect;
using namespace xuranus::minilogger;

static void PrintHelp()
{

}

struct DeviceMapperMount {
    std::string     copyFilePath;
    std::string     loopbackDevicePath;
    uint64_t        startSector;
    uint64_t        sectorsCount;
};

struct DmTarget {
    std::string     copyFilePath;
    std::string     loopbackDevicePath;
    uint64_t        startSector;
    uint64_t        sectorsCount;
};

struct DeviceMapperMountMetaCheckpoint {
    std::string             dmName;
    std::string             dmPath;
    std::string             mountPath;
    std::vector<DmTarget>   targets;
};

static bool MountCopy(
    const std::string& copyDataDirPath,
    const std::string& copyMetaDirPath,
    const std::string& mountPath)
{
    VolumeCopyMeta volumeCopyMeta {};
    if (!util::ReadVolumeCopyMeta(copyMetaDirPath, volumeCopyMeta)) {
        ERRLOG("failed to read copy meta json in directory %s", copyMetaDirPath.c_str());
        return 1;
    }

    return 0;
}

static bool UmountCopy(
    const std::string& mountPath)
{
    return 0;
}

int main(int argc, const char** argv)
{
    std::cout << "=== vcopymount ===" << std::endl;
    std::string copyDataDirPath = "";
    std::string copyMetaDirPath = "";
    std::string mountPath = "";
    std::string logLevel = "DEBUG";
    bool isMount = false;
    bool isUmount = false;

    GetOptionResult result = GetOption(
        argv + 1,
        argc - 1,
        "m:d:",
        { "--meta=", "--data=", "--target="});
    for (const OptionResult opt: result.opts) {
        if (opt.option == "d") {
            copyDataDirPath = opt.value;
        } else if (opt.option == "m") {
            copyMetaDirPath = opt.value;
        } else if (opt.option == "--mount") {
            isMount = true;
        } else if (opt.option == "--umount") {
            isUmount = true;
        } else if (opt.option == "l") {
            logLevel = opt.value;
        } else if (opt.option == "h") {
            PrintHelp();
            return 0;
        }
    }

        if (logLevel == "INFO") {
        Logger::GetInstance()->SetLogLevel(LoggerLevel::INFO);
    } else if (logLevel == "DEBUG") {
        Logger::GetInstance()->SetLogLevel(LoggerLevel::DEBUG);
    }

    using namespace xuranus::minilogger;
    LoggerConfig conf {};
    conf.target = LoggerTarget::FILE;
    conf.archiveFilesNumMax = 10;
    conf.fileName = "vcopymount.log";
#ifdef __linux__
    conf.logDirPath = "/tmp/LoggerTest";
#endif
#ifdef _WIN32
    conf.logDirPath = R"(C:\LoggerTest)";
#endif
    Logger::GetInstance()->SetLogLevel(LoggerLevel::DEBUG);
    if (!Logger::GetInstance()->Init(conf)) {
        std::cerr << "Init logger failed" << std::endl;
    }

    if (isMount) {
        if (!MountCopy(copyDataDirPath, copyMetaDirPath, mountPath)) {
            std::cerr << "mount failed" << std::endl;
            return 1;
        }
    }
    if (isUmount) {
        if (!UmountCopy(mountPath)) {
            std::cerr << "umount failed" << std::endl;
            return 1;
        }
    }
    PrintHelp();
    return 0;
}