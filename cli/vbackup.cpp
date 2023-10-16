#include "native/RawIO.h"
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <ostream>
#include <string>

#ifdef __linux__
#include <getopt.h>
#include <unistd.h>
#endif

#include "GetOption.h"
#include "native/FileSystemAPI.h"
#include "native/win32/Win32RawIO.h"
#include "VolumeProtector.h"
#include "Logger.h"


#include "native/TaskResourceManager.h"

using namespace volumeprotect;
using namespace xuranus::getopt;
using namespace xuranus::minilogger;

static void PrintHelp()
{
    std::cout << "vbkup -v volume -n name -f format -d datadir -m metadir [-p prevmetadir]" << std::endl;
}

static void PrintTaskStatistics(const TaskStatistics& statistics)
{
    ::printf("checkStatistics: bytesToReaded: %llu, bytesRead: %llu, "
        "blocksToHash: %llu, blocksHashed: %llu, "
        "bytesToWrite: %llu, bytesWritten: %llu\n",
        statistics.bytesToRead, statistics.bytesRead,
        statistics.blocksToHash, statistics.blocksHashed,
        statistics.bytesToWrite, statistics.bytesWritten);
}

int ExecVolumeBackup(
    BackupType backupType,
    CopyFormat copyFormat,
    const std::string& copyName,
    const std::string& volumePath,
    const std::string& prevCopyMetaDirPath,
    const std::string& outputCopyDataDirPath,
    const std::string& outputCopyMetaDirPath)
{
    uint32_t hasherWorkerNum = fsapi::ProcessorsNum();
    std::cout << "using " << hasherWorkerNum << " processing units" << std::endl;

    VolumeBackupConfig backupConfig {};
    backupConfig.copyFormat = copyFormat;
    backupConfig.copyName = copyName;
    backupConfig.backupType = backupType;
    backupConfig.volumePath = volumePath;
    backupConfig.prevCopyMetaDirPath = prevCopyMetaDirPath;
    backupConfig.outputCopyDataDirPath = outputCopyDataDirPath;
    backupConfig.outputCopyMetaDirPath = outputCopyMetaDirPath;
    backupConfig.blockSize = DEFAULT_BLOCK_SIZE;
    backupConfig.sessionSize = 3 * ONE_GB;
    backupConfig.hasherNum = hasherWorkerNum;
    backupConfig.hasherEnabled = true;
    backupConfig.enableCheckpoint = true;

    std::shared_ptr<VolumeProtectTask> task = VolumeProtectTask::BuildBackupTask(backupConfig);
    if (task == nullptr) {
        std::cerr << "failed to build backup task" << std::endl;
        return 1;
    }
    task->Start();
    uint64_t prevWrittenBytes = 0;
    while (!task->IsTerminated()) {
        TaskStatistics statistics =  task->GetStatistics();
        PrintTaskStatistics(statistics);
        uint64_t speedMB = (statistics.bytesWritten - prevWrittenBytes) / 1024 / 1024;
        std::cout << "Speed: " << speedMB << " MB/s" << std::endl;
        prevWrittenBytes = statistics.bytesWritten;
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    PrintTaskStatistics(task->GetStatistics());
    std::cout << "volume backup task completed with status " << task->GetStatusString() << std::endl;
    return 0;
}

int ExecVolumeForeverIncrementBackup(
    const std::string&  volumePath,
    CopyFormat          copyFormat,
    const std::string&  copyName,
    const std::string&  prevCopyMetaDirPath,
    const std::string&  copyDataDirPath,
    const std::string&  copyMetaDirPath)
{
    return ExecVolumeBackup(
        BackupType::FOREVER_INC,
        copyFormat,
        copyName,
        volumePath,
        prevCopyMetaDirPath,
        copyDataDirPath,
        copyMetaDirPath);
}

int ExecVolumeFullBackup(
    const std::string&  volumePath,
    CopyFormat          copyFormat,
    const std::string&  copyName,
    const std::string&  copyDataDirPath,
    const std::string&  copyMetaDirPath)
{
    return ExecVolumeBackup(
        BackupType::FULL,
        copyFormat,
        copyName,
        volumePath,
        "",
        copyDataDirPath,
        copyMetaDirPath);
}

int ExecVolumeRestore(
    const std::string& 	volumePath,
    const std::string&  copyName,
    const std::string&	copyDataDirPath,
    const std::string&	copyMetaDirPath)
{
    VolumeRestoreConfig restoreConfig {};
    restoreConfig.copyName = copyName;
    restoreConfig.volumePath = volumePath;
    restoreConfig.copyDataDirPath = copyDataDirPath;
    restoreConfig.copyMetaDirPath = copyMetaDirPath;

    std::shared_ptr<VolumeProtectTask> task = VolumeProtectTask::BuildRestoreTask(restoreConfig);
    if (task == nullptr) {
        std::cerr << "failed to build restore task" << std::endl;
        return 1;
    }
    task->Start();
    uint64_t prevWrittenBytes = 0;
    while (!task->IsTerminated()) {
        TaskStatistics statistics =  task->GetStatistics();
        PrintTaskStatistics(statistics);
        uint64_t speedMB = (statistics.bytesWritten - prevWrittenBytes) / 1024 / 1024;
        std::cout << "Speed: " << speedMB << " MB/s" << std::endl;
        prevWrittenBytes = statistics.bytesWritten;
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    PrintTaskStatistics(task->GetStatistics());
    std::cout << "volume restore task completed!" << std::endl;
    return 0;
}

int main(int argc, const char** argv)
{
    std::cout << "=== vbackup cli ===" << std::endl;
    std::string volumePath;
    std::string copyName;
    std::string copyFormat;
    std::string copyDataDirPath;
    std::string copyMetaDirPath;
    std::string prevCopyMetaDirPath;
    std::string logLevel = "DEBUG";
    bool isRestore = false;

    GetOptionResult result = GetOption(
        argv + 1, argc - 1,
        "v:n:f:d:m:p:h:r:l:",
        {"--volume=", "--name=", "--format", "--data=", "--meta=", "--prevmeta=", "--help", "--restore", "--loglevel="});
    for (const OptionResult opt: result.opts) {
        if (opt.option == "v" || opt.option == "volume") {
            volumePath = opt.value;
        } else if (opt.option == "n" || opt.option == "name") {
            copyName = opt.value;
        } else if (opt.option == "f" || opt.option == "format") {
            copyFormat = opt.value;
        } else if (opt.option == "d" || opt.option == "data") {
            copyDataDirPath = opt.value;
        } else if (opt.option == "m" || opt.option == "meta") {
            copyMetaDirPath = opt.value;
        } else if (opt.option == "p" || opt.option == "prevmeta") {
            prevCopyMetaDirPath = opt.value;
        } else if (opt.option == "r") {
            isRestore = true;
        } else if (opt.option == "l" || opt.option == "loglevel") {
            logLevel = opt.value;
        } else if (opt.option == "h") {
            PrintHelp();
            return 0;
        }
    }

    if (volumePath.empty() || copyDataDirPath.empty() || copyMetaDirPath.empty()) {
        PrintHelp();
        return 1;
    }
    std::cout << "VolumePath: " << volumePath << std::endl;
    std::cout << "CopyName: " << copyName << std::endl;
    std::cout << "CopyFormat: " << copyFormat << std::endl;
    std::cout << "CopyDataDirPath: " << copyDataDirPath << std::endl;
    std::cout << "CopyMetaDirPath: " << copyMetaDirPath << std::endl;
    std::cout << "PrevCopyMetaDirPath: " << prevCopyMetaDirPath << std::endl;
    std::cout << "LogLevel: " << logLevel << std::endl;

    CopyFormat copyFormatEnum = CopyFormat::BIN;
    if (copyFormat == "BIN") {
        copyFormatEnum = CopyFormat::BIN;
    } else if (copyFormat == "IMAGE") {
        copyFormatEnum = CopyFormat::IMAGE;
#ifdef _WIN32
    } else if (copyFormat == "VHD_FIXED") {
        copyFormatEnum = CopyFormat::VHD_FIXED;
    } else if (copyFormat == "VHD_DYNAMIC") {
        copyFormatEnum = CopyFormat::VHD_DYNAMIC;
    } else if (copyFormat == "VHDX_FIXED") {
        copyFormatEnum = CopyFormat::VHDX_FIXED;
    } else if (copyFormat == "VHDX_DYNAMIC") {
        copyFormatEnum = CopyFormat::VHDX_DYNAMIC;
#endif
    }

    if (logLevel == "INFO") {
        Logger::GetInstance()->SetLogLevel(LoggerLevel::INFO);
    } else if (logLevel == "DEBUG") {
        Logger::GetInstance()->SetLogLevel(LoggerLevel::DEBUG);
    } else {
        std::cerr << "invalid logger level: " << logLevel << std::endl;
        return 1;
    }

    using namespace xuranus::minilogger;
    LoggerConfig conf {};
    conf.target = LoggerTarget::FILE;
    conf.archiveFilesNumMax = 10;
    conf.fileName = "vbackup.log";
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

    if (isRestore) {
        std::cout << "Doing copy restore" << std::endl;
        ExecVolumeRestore(volumePath, copyName, copyDataDirPath, copyMetaDirPath);
    } else if (prevCopyMetaDirPath.empty()) {
        std::cout << "Doing full backup" << std::endl;
        ExecVolumeFullBackup(
            volumePath, copyFormatEnum, copyName, copyDataDirPath, copyMetaDirPath);
    } else {
        std::cout << "Doing increment backup" << std::endl;
        ExecVolumeForeverIncrementBackup(
            volumePath, copyFormatEnum, copyName, prevCopyMetaDirPath, copyDataDirPath, copyMetaDirPath);
    }
    Logger::GetInstance()->Destroy();
    return 0;
}