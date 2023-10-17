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
#include "Logger.h"
#include "VolumeCopyMountProvider.h"

using namespace xuranus;
using namespace xuranus::getopt;
using namespace xuranus::minilogger;
using namespace volumeprotect;
using namespace volumeprotect::mount;

struct CliArgs {
    std::string     copyName;
    std::string     copyDataDirPath;
    std::string     copyMetaDirPath;
    std::string     mountTargetPath;
    std::string     outputDirPath;
    std::string     mountFsType;
    std::string     mountOptions;
    std::string     mountRecordJsonFilePath;
    bool            isMount                     { false };
    bool            isUmount                    { false };
    bool            printHelp                   { false };
};

static void PrintHelp()
{
    std::cout << "Usage: vcopymount --mount | --umount [option]" << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "--name    <name>      name of the copy to be mount" << std::endl;
    std::cout << "--data    <path>      copy data dir path" << std::endl;
    std::cout << "--meta    <path>      copy meta dir path" << std::endl;
    std::cout << "--output  <path>      output dir path to ouput checkpoint" << std::endl;
    std::cout << "--target  <path>      dir target to mount to" << std::endl;
    std::cout << "--type    <fs>        mount fs type, ex: ext4, xfs..." << std::endl;
    std::cout << "--option  <option>    mount fs option args" << std::endl;
}

static bool MountCopy(const VolumeCopyMountConfig& mountConfig)
{
    std::cout << "======== Mount Copy ========" << std::endl;
    std::cout << "CopyName " << mountConfig.copyName << std::endl;
    std::cout << "CopyMetaDirPath " << mountConfig.copyMetaDirPath << std::endl;
    std::cout << "CopyDataDirPath " << mountConfig.copyDataDirPath << std::endl;
    std::cout << "MountTargetPath " << mountConfig.mountTargetPath << std::endl;
    std::cout << "OutputDirPath " << mountConfig.outputDirPath << std::endl;
    std::cout << "MountFsType " << mountConfig.mountFsType << std::endl;
    std::cout << "MountOptions " << mountConfig.mountOptions << std::endl;
    std::cout << std::endl;

    std::unique_ptr<VolumeCopyMountProvider> mountProvider = VolumeCopyMountProvider::Build(mountConfig);
    if (mountProvider == nullptr) {
        std::cerr << "failed to build mount provider" << std::endl;
    }
    if (!mountProvider->Mount()) {
        std::cerr << "=== Mount Copy Failed! ===" << std::endl;
        std::cerr << mountProvider->GetError() << std::endl;
        return false;
    }
    std::cout << "Mount Copy Success" << std::endl;
    std::cout << "Mount Record Json File Path: " << mountProvider->GetMountRecordPath() << std::endl;
    return true;
}

static bool UmountCopy(const std::string& mountRecordJsonFilePath)
{
    std::cout << "Umount Copy Using Record: " << mountRecordJsonFilePath << std::endl;
    std::unique_ptr<VolumeCopyUmountProvider> umountProvider = VolumeCopyUmountProvider::Build(mountRecordJsonFilePath);
    if (umountProvider == nullptr) {
        std::cerr << "failed to build umount provider" << std::endl;
    }
    if (!umountProvider->Umount()) {
        ERRLOG("=== Umount Copy Failed! ===");
        std::cerr << umountProvider->GetError() << std::endl;
        return false;
    }
    std::cout << "Umount Success!" << std::endl;
    return true;
}

static void InitLogger()
{
    using namespace xuranus::minilogger;
    LoggerConfig conf {};
    conf.target = LoggerTarget::STDOUT;
    Logger::GetInstance()->SetLogLevel(LoggerLevel::DEBUG);
    if (!Logger::GetInstance()->Init(conf)) {
        std::cerr << "Init logger failed" << std::endl;
    }
}

static CliArgs ParseCliArgs(int argc, const char** argv)
{
    CliArgs cliArgs {};
    GetOptionResult result = GetOption(
        argv + 1,
        argc - 1,
        "n:m:d:ht:o:",
        {
            "--name=", "--meta=","--data=", "--target=", "--help",
            "--mount", "--umount=", "--output=", "--type=", "--option="});
    for (const OptionResult opt: result.opts) {
        if (opt.option == "n" || opt.option == "name") {
            cliArgs.copyName = opt.value;
        } else if (opt.option == "d" || opt.option == "data") {
            cliArgs.copyDataDirPath = opt.value;
        } else if (opt.option == "m" || opt.option == "meta") {
            cliArgs.copyMetaDirPath = opt.value;
        } else if (opt.option == "target") {
            cliArgs.mountTargetPath = opt.value;
        } else if (opt.option == "mount") {
            cliArgs.isMount = true;
        } else if (opt.option == "umount") {
            cliArgs.isUmount = true;
            cliArgs.mountRecordJsonFilePath = opt.value;
        } else if (opt.option == "output") {
            cliArgs.outputDirPath = opt.value;
        } else if (opt.option == "t" || opt.option == "type") {
            cliArgs.mountFsType = opt.value;
        } else if (opt.option == "o" || opt.option == "option") {
            cliArgs.mountOptions = opt.value;
        } else if (opt.option == "h" || opt.option == "help") {
            cliArgs.printHelp = true;
        }
    }
    return cliArgs;
}

int main(int argc, const char** argv)
{
    std::cout << "----- Volume Copy Mount Cli ----" << std::endl;
    InitLogger();
    CliArgs cliArgs = ParseCliArgs(argc, argv);
    if (cliArgs.printHelp) {
        PrintHelp();
        return 0;
    }
    if (cliArgs.isMount) {
        return !MountCopy(VolumeCopyMountConfig {
            cliArgs.outputDirPath,
            cliArgs.copyName,
            cliArgs.copyMetaDirPath,
            cliArgs.copyDataDirPath,
            cliArgs.mountTargetPath,
            cliArgs.mountFsType,
            cliArgs.mountOptions
        });
    }
    if (cliArgs.isUmount) {
        return !UmountCopy(cliArgs.mountRecordJsonFilePath);
    }
    PrintHelp();
    return 0;
}