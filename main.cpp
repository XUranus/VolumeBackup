#include <chrono>
#include <getopt.h>
#include <iostream>
#include <string>
#include <unistd.h>
#include <stdlib.h>

#include "Logger.h"
#include "VolumeBackup.h"

using namespace volumebackup;

void PrintHelp()
{
    std::cout << "vbkup -v volume -d datadir -m metadir [-p prevmetadir]" << std::endl;
}

int StartRestoreTask(
	const std::string&	copyDataDirPath,
	const std::string&	copyMetaDirPath,
    const std::string& 	blockDevicePath)
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
        std::cout
            << "bytesToReaded: " << statistics.bytesToRead << "\n"
            << "bytesRead: " << statistics.bytesRead << "\n"
            << "blocksToHash: " << statistics.blocksToHash << "\n"
            << "blocksHashed: " << statistics.blocksHashed << "\n"
            << "bytesToWrite: " << statistics.bytesToWrite << "\n"
            << "bytesWritten: " << statistics.bytesWritten
            << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    std::cout << "volume backup task completed!" << std::endl;
    return 0;
}

int main(int argc, char** argv)
{
    int ch;
    std::string blockDevicePath = "";
    std::string copyDataDirPath = "";
    std::string copyMetaDirPath = "";
    std::string prevCopyMetaDirPath = "";
    while ((ch = getopt(argc, argv, "v:d:m:p:h")) != -1) {
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

    xuranus::minilogger::Logger::GetInstance().SetLogLevel(xuranus::minilogger::LoggerLevel::DEBUG);
    if (prevCopyMetaDirPath.empty()) {
        std::cout << "Doing full backup" << std::endl;
        return ExecVolumeBackup(CopyType::FULL, blockDevicePath, prevCopyMetaDirPath, copyDataDirPath, copyMetaDirPath);
    } else {
        std::cout << "Doing increment backup" << std::endl;
        return ExecVolumeBackup(CopyType::INCREMENT, blockDevicePath, prevCopyMetaDirPath, copyDataDirPath, copyMetaDirPath);
    }
    return 0;
}