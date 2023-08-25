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
#include <memory>
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
    std::cout << "Usage: vcopymount --mount | --umount [option]" << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "--data    <path>      copy data dir path>" << std::endl;
    std::cout << "--meta    <path>      copy meta dir path" << std::endl;
    std::cout << "--cache   <path>      cache dir path to ouput checkpoint" << std::endl;
    std::cout << "--target  <path>      dir target to mount to" << std::endl;
    std::cout << "--type    <fs>        mount fs type, ex: ext4, xfs..." << std::endl;
    std::cout << "--option  <option>    mount fs option args" << std::endl;
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
    mountConfig.mountFsType = mountFsType;
    mountConfig.mountOptions = mountOptions;
    
    std::shared_ptr<LinuxMountProvider> mountProvider = LinuxMountProvider::BuildLinuxMountProvider(cacheDirPath);
    if (mountProvider == nullptr) {
        std::cerr << "failed to build mount provider" << std::endl;
    }
    LinuxCopyMountRecord mountRecord {};
    if (!mountProvider->MountCopy(mountConfig)) {
        std::cerr << "=== Mount Copy Failed! ===" << std::endl;
        std::cerr << mountProvider->GetErrors() << std::endl;
        if (!mountProvider->ClearResidue()) {
            std::cerr << "Residue Not Cleared!" << std::endl;
        }
        return false;
    }
    std::cout << "Mount Copy Success" << std::endl;
    std::cout << "Mount Record Json File Path: " << mountProvider->GetMountRecordJsonPath() << std::endl; 
    return true;
}

static bool UmountCopy(const std::string& cacheDirPath)
{
    std::cout << "Umount Copy Using Cache Dir: " << cacheDirPath << std::endl;        
    std::shared_ptr<LinuxMountProvider> umountProvider = LinuxMountProvider::BuildLinuxMountProvider(cacheDirPath);
    if (umountProvider == nullptr) {
        std::cerr << "failed to build mount provider" << std::endl;
    }
    if (!umountProvider->UmountCopy()) {
        ERRLOG("=== Umount Copy Failed! ===");
        std::cerr << umountProvider->GetErrors() << std::endl;
        if (!umountProvider->ClearResidue()) {
            std::cerr << "Residue Not Cleared!" << std::endl;
        }
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

    GetOptionResult result = GetOption(
        argv + 1,
        argc - 1,
        "m:d:ht:o:",
        { "--meta=", "--data=", "--target=", "--mount", "--umount", "--cache=", "--type=", "--option=" });
    for (const OptionResult opt: result.opts) {
        std::cout << opt.option << " " << opt.value << std::endl;
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
        } else if (opt.option == "h") {
            PrintHelp();
            return 0;
        }
    }
    
    using namespace xuranus::minilogger;
    LoggerConfig conf {};
    conf.target = LoggerTarget::STDOUT;
    Logger::GetInstance()->SetLogLevel(LoggerLevel::DEBUG);
    if (!Logger::GetInstance()->Init(conf)) {
        std::cerr << "Init logger failed" << std::endl;
    }

    if (isMount) {
        return !MountCopy(
                copyDataDirPath, copyMetaDirPath, mountTargetPath, cacheDirPath, mountFsType, mountOptions);
    }
    if (isUmount) {
        return !UmountCopy(cacheDirPath);
    }
    PrintHelp();
    return 0;
}