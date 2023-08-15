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
#include <fstream>
#include <iostream>
#include <string>
#include "GetOption.h"
#include "Json.h"
#include "Logger.h"
#include "CopyMountProvider.h"

using namespace xuranus;
using namespace xuranus::getopt;
using namespace xuranus::minilogger;
using namespace volumeprotect;
using namespace volumeprotect::mount;

static void PrintHelp()
{

}

static bool MountCopy(
    const std::string& copyDataDirPath,
    const std::string& copyMetaDirPath,
    const std::string& mountTargetPath,
    const std::string& cacheDirPath,
    const std::string& mountFsType,
    const std::string& mountOptions)
{
    std::cout << "======== Mount Copy ========" << std::endl;
    std::cout << "CopyMetaDirPath " << copyMetaDirPath << std::endl;
    std::cout << "CopyDataDirPath " << copyDataDirPath << std::endl;
    std::cout << "MountTargetPath " << mountTargetPath << std::endl;
    std::cout << "CacheDirPath " << cacheDirPath << std::endl;
    std::cout << "MountFsType " << mountFsType << std::endl;
    std::cout << "MountOptions " << mountOptions << std::endl;
    std::cout << std::endl;

    LinuxCopyMountConfig mountConfig {};
    mountConfig.copyMetaDirPath = copyMetaDirPath;
    mountConfig.copyDataDirPath = copyDataDirPath;
    mountConfig.mountTargetPath = mountTargetPath;
    mountConfig.cacheDirPath = cacheDirPath;
    mountConfig.mountFsType = mountFsType;
    mountConfig.mountOptions = mountOptions;
    
    volumeprotect::mount::LinuxMountProvider mountProvider;
    LinuxCopyMountRecord mountRecord {};
    if (!mountProvider.MountCopy(mountConfig, mountRecord)) {
        std::cerr << "Nount Copy Failed" << std::endl;
        return false;
    }
    std::string mountRecordJson = minijson::util::Serialize(mountRecord);
    std::cout << "Mount Copy Success" << std::endl;
    std::cout << mountRecordJson << std::endl;
    return true;
}

static bool UmountCopy(const std::string& mountRecordJsonPath)
{
    std::cout << "Umount Copy Using Record Json: " << mountRecordJsonPath << std::endl;
    std::ifstream in(mountRecordJsonPath);
    if (!in.is_open()) {
        std::cout << "Open Mount Record JSON Failed, errno " << errno << std::endl;
        return false;
    }
    std::string mountRecordJsonString;
    in >> mountRecordJsonString;
    in.close();
    std::cout << mountRecordJsonString << std::endl;
    LinuxCopyMountRecord mountRecord {};
    minijson::util::Deserialize(mountRecordJsonString, mountRecord);
    volumeprotect::mount::LinuxMountProvider mountProvider;
    if (!mountProvider.UmountCopy(mountRecord)) {
        ERRLOG("Umount Copy Failed");
        return false;
    }
    return true;
}

int main(int argc, const char** argv)
{
    std::cout << "=== vcopymount ===" << std::endl;
    std::string copyDataDirPath = "";
    std::string copyMetaDirPath = "";
    std::string mountTargetPath = "";
    std::string cacheDirPath = "";
    std::string mountFsType = "";
    std::string mountOptions = "";
    bool isMount = false;
    bool isUmount = false;
    std::string mountRecordJsonPath = "";

    GetOptionResult result = GetOption(
        argv + 1,
        argc - 1,
        "m:d:h",
        { "--meta=", "--data=", "--target=", "--mount", "--umount" });
    for (const OptionResult opt: result.opts) {
        if (opt.option == "d" || opt.option == "data") {
            copyDataDirPath = opt.value;
        } else if (opt.option == "m" || opt.option == "meta") {
            copyMetaDirPath = opt.value;
        } else if (opt.option == "target") {
            mountTargetPath = opt.value;
        } else if (opt.option == "mount") {
            isMount = true;
        } else if (opt.option == "umount") {
            isUmount = true;
        } else if (opt.option == "cache") {
            cacheDirPath = opt.value;
        } else if (opt.option == "t" || opt.option == "type") {
            mountFsType = opt.value;
        } else if (opt.option == "o" || opt.option == "option") {
            mountOptions = opt.value;
        } else if (opt.option == "record") {
            mountRecordJsonPath = opt.value;
        } else if (opt.option == "h") {
            PrintHelp();
            return 0;
        }
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
        return !MountCopy(
                copyDataDirPath, copyMetaDirPath, mountTargetPath, cacheDirPath, mountFsType, mountOptions);
    }
    if (isUmount) {
        return !UmountCopy(mountRecordJsonPath))
    }
    PrintHelp();
    return 0;
}