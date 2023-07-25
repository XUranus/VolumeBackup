#include <chrono>
#include <cstdlib>
#include <iostream>
#include <ostream>
#include <string>

#ifdef __linux__
#include <getopt.h>
#include <unistd.h>
#endif

#include "GetOption.h"
#include "VolumeProtector.h"
#include "Logger.h"

using namespace volumeprotect;
using namespace xuranus::getopt;
using namespace xuranus::minilogger;

void PrintHelp()
{
    std::cout << "vbkup -v volume -d datadir -m metadir [-p prevmetadir]" << std::endl;
}

int ExecVolumeRestore(
    const std::string& 	volumePath,
    const std::string&	copyDataDirPath,
    const std::string&	copyMetaDirPath)
{
    VolumeRestoreConfig restoreConfig {};
    restoreConfig.volumePath = volumePath;
    restoreConfig.copyDataDirPath = copyDataDirPath;
    restoreConfig.copyMetaDirPath = copyMetaDirPath;

    std::shared_ptr<VolumeProtectTask> task = VolumeProtectTask::BuildRestoreTask(restoreConfig);
    if (task == nullptr) {
        std::cerr << "failed to build restore task" << std::endl;
        return 1;
    }
    task->Start();
    while (!task->IsTerminated()) {
        TaskStatistics statistics =  task->GetStatistics();
        DBGLOG("checkStatistics: bytesToReaded: %llu, bytesRead: %llu, blocksToHash: %llu, blocksHashed: %llu, bytesToWrite: %llu, bytesWritten: %llu",
            statistics.bytesToRead, statistics.bytesRead, statistics.blocksToHash, statistics.blocksHashed, statistics.bytesToWrite, statistics.bytesWritten);
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    std::cout << "volume restore task completed!" << std::endl;
    return 0;
}

int ExecVolumeBackup(
    CopyType copyType,
    const std::string& volumePath,
    const std::string& prevCopyMetaDirPath,
    const std::string& outputCopyDataDirPath,
    const std::string& outputCopyMetaDirPath)
{
    VolumeBackupConfig backupConfig {};
    backupConfig.copyType = copyType;
    backupConfig.volumePath = volumePath;
    backupConfig.prevCopyMetaDirPath = prevCopyMetaDirPath;
    backupConfig.outputCopyDataDirPath = outputCopyDataDirPath;
    backupConfig.outputCopyMetaDirPath = outputCopyMetaDirPath;
    backupConfig.blockSize = DEFAULT_BLOCK_SIZE;
    backupConfig.sessionSize = DEFAULT_SESSION_SIZE;
    backupConfig.hasherNum = DEFAULT_HASHER_NUM;
    backupConfig.hasherEnabled = true;

    std::shared_ptr<VolumeProtectTask> task = VolumeProtectTask::BuildBackupTask(backupConfig);
    if (task == nullptr) {
        std::cerr << "failed to build backup task" << std::endl;
        return 1;
    }
    task->Start();
    while (!task->IsTerminated()) {
        TaskStatistics statistics =  task->GetStatistics();
        DBGLOG("checkStatistics: bytesToReaded: %llu, bytesRead: %llu, blocksToHash: %llu, blocksHashed: %llu, bytesToWrite: %llu, bytesWritten: %llu",
            statistics.bytesToRead, statistics.bytesRead, statistics.blocksToHash, statistics.blocksHashed, statistics.bytesToWrite, statistics.bytesWritten);
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    std::cout << "volume backup task completed!" << std::endl;
    return 0;
}

int main(int argc, const char** argv)
{
    std::cout << "=== vbkup ===" << std::endl;
    std::string volumePath = "";
    std::string copyDataDirPath = "";
    std::string copyMetaDirPath = "";
    std::string prevCopyMetaDirPath = "";
    std::string logLevel = "DEBUG";
    bool isRestore = false;

    GetOptionResult result = GetOption(argv + 1, argc - 1, "v:d:m:p:h:r:l", {});
    for (const OptionResult opt: result.opts) {
        if (opt.option == "v") {
            volumePath = opt.value;
        } else if (opt.option == "d") {
            copyDataDirPath = opt.value;
        } else if (opt.option == "m") {
            copyMetaDirPath = opt.value;
        } else if (opt.option == "p") {
            prevCopyMetaDirPath = opt.value;
        } else if (opt.option == "r") {
            isRestore = true;
        } else if (opt.option == "l") {
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
    std::cout << "volumePath: " << volumePath << std::endl;
    std::cout << "copyDataDirPath: " << copyDataDirPath << std::endl;
    std::cout << "copyMetaDirPath: " << copyMetaDirPath << std::endl;
    std::cout << "prevCopyMetaDirPath: " << prevCopyMetaDirPath << std::endl;
    std::cout << "logLevel: " << logLevel << std::endl;

    if (logLevel == "INFO") {
        Logger::GetInstance()->SetLogLevel(LoggerLevel::INFO);
    } else if (logLevel == "DEBUG") {
        Logger::GetInstance()->SetLogLevel(LoggerLevel::DEBUG);
    }

    using namespace xuranus::minilogger;
    LoggerConfig conf {};
    conf.target = LoggerTarget::FILE;
    conf.archiveFilesNumMax = 10;
    conf.fileName = "demo.log";
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
        ExecVolumeRestore(volumePath, copyDataDirPath, copyMetaDirPath);
    } else if (prevCopyMetaDirPath.empty()) {
        std::cout << "Doing full backup" << std::endl;
        ExecVolumeBackup(CopyType::FULL, volumePath, prevCopyMetaDirPath, copyDataDirPath, copyMetaDirPath);
    } else {
        std::cout << "Doing increment backup" << std::endl;
        ExecVolumeBackup(CopyType::INCREMENT, volumePath, prevCopyMetaDirPath, copyDataDirPath, copyMetaDirPath);
    }
    Logger::GetInstance()->Destroy();
    return 0;
}