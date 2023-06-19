#include <chrono>
#include <cstdlib>
#include <getopt.h>
#include <iostream>
#include <ostream>
#include <string>
#include <unistd.h>
#include <stdlib.h>

#include "VolumeBackup.h"
#include "Logger.h"

using namespace volumebackup;
using namespace xuranus::minilogger;

void PrintHelp()
{
    std::cout << "vbkup -v volume -d datadir -m metadir [-p prevmetadir]" << std::endl;
}

int ExecVolumeRestore(
    const std::string& 	blockDevicePath,
	const std::string&	copyDataDirPath,
	const std::string&	copyMetaDirPath)
{
    // TODO::implement it!
    std::cerr << "restore not implemented!" << std::endl;
    return 0;    
}

int ExecVolumeBackup(
    CopyType copyType,
    const std::string& blockDevicePath,
    const std::string& prevCopyMetaDirPath,
    const std::string& outputCopyDataDirPath,
    const std::string& outputCopyMetaDirPath)
{
    VolumeBackupConfig backupConfig {};
    backupConfig.copyType = copyType;
    backupConfig.blockDevicePath = blockDevicePath;
    backupConfig.prevCopyMetaDirPath = prevCopyMetaDirPath;
    backupConfig.outputCopyDataDirPath = outputCopyDataDirPath;
    backupConfig.outputCopyMetaDirPath = outputCopyMetaDirPath;
    backupConfig.blockSize = DEFAULT_BLOCK_SIZE;
    backupConfig.sessionSize = DEFAULT_SESSION_SIZE;
    backupConfig.hasherNum = DEFAULT_HASHER_NUM;
    backupConfig.hasherEnabled = true;

    std::shared_ptr<VolumeBackupTask> task = VolumeBackupTask::BuildBackupTask(backupConfig);
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

int main(int argc, char** argv)
{
    std::string blockDevicePath = "";
    std::string copyDataDirPath = "";
    std::string copyMetaDirPath = "";
    std::string prevCopyMetaDirPath = "";
    std::string logLevel = "INFO";
    bool isRestore = false;
    int ch = -1;
    while ((ch = getopt(argc, argv, "v:d:m:p:h:r:l")) != -1) {
        switch (ch) {
            case 'v' : {
                blockDevicePath = optarg;
                break;
            }
            case 'd' : {
                copyDataDirPath = optarg;
                break;
            }
            case 'm' : {
                copyMetaDirPath = optarg;
                break;
            }
            case 'p' : {
                prevCopyMetaDirPath = optarg;
                break;
            }
            case 'r' : {
                isRestore = true;
                break;
            }
            case 'l' : {
                logLevel = atoi(optarg);
                break;
            }
            case 'h' : {
                PrintHelp();
                return 0;
            }
        }
    }
    if (blockDevicePath.empty() || copyDataDirPath.empty() || copyMetaDirPath.empty()) {
        PrintHelp();
        return 1;
    } 
    std::cout << "blockDevicePath: " << blockDevicePath << std::endl;
    std::cout << "copyDataDirPath: " << copyDataDirPath << std::endl;
    std::cout << "copyMetaDirPath: " << copyMetaDirPath << std::endl;
    std::cout << "prevCopyMetaDirPath: " << prevCopyMetaDirPath << std::endl;
    std::cout << "logLevel: " << logLevel << std::endl;

    if (logLevel == "INFO") {
        Logger::GetInstance().SetLogLevel(LoggerLevel::INFO);
    } else if (logLevel == "DEBUG") {
        Logger::GetInstance().SetLogLevel(LoggerLevel::DEBUG);
    }

    if (isRestore) {
        std::cout << "Doing copy restore" << std::endl;
        return ExecVolumeRestore(blockDevicePath, copyDataDirPath, copyMetaDirPath);
    } else if (prevCopyMetaDirPath.empty()) {
        std::cout << "Doing full backup" << std::endl;
        return ExecVolumeBackup(CopyType::FULL, blockDevicePath, prevCopyMetaDirPath, copyDataDirPath, copyMetaDirPath);
    } else {
        std::cout << "Doing increment backup" << std::endl;
        return ExecVolumeBackup(CopyType::INCREMENT, blockDevicePath, prevCopyMetaDirPath, copyDataDirPath, copyMetaDirPath);
    }
    return 0;
}