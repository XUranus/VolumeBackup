#include <chrono>
#include <getopt.h>
#include <iostream>
#include <string>
#include <unistd.h>
#include <stdlib.h>

#include "VolumeBackup.h"

using namespace volumebackup;

// bool StartIncrementBackupTask(
// 	const std::string& 	blockDevicePath,
//     const std::string&	prevCopyDataDirPath,
// 	const std::string&	prevCopyMetaDirPath,
// 	const std::string&	outputCopyDataDirPath,
// 	const std::string&	outputCopyMetaDirPath)
// {
    
//     return true;
// }

// bool StartRestoreTask(
// 	const std::string&	copyDataDirPath,
// 	const std::string&	copyMetaDirPath,
//     const std::string& 	blockDevicePath)
// {
//     return true;    
// }

void PrintHelp()
{
    std::cout << "vbkup -v volume -d datadir -m metadir" << std::endl;
}

int main(int argc, char** argv)
{
    int ch;
    std::string blockDevicePath = "";
    std::string copyDataDirPath = "";
    std::string copyMetaDirPath = "";
    while ((ch = getopt(argc, argv, "v:d:m:h")) != -1) {
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

    VolumeBackupConfig backupConfig {};
    backupConfig.copyType = CopyType::FULL;
    backupConfig.blockDevicePath = blockDevicePath;
    backupConfig.prevCopyMetaDirPath = "";
    backupConfig.outputCopyDataDirPath = copyDataDirPath;
    backupConfig.outputCopyMetaDirPath = copyMetaDirPath;
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
            << "bytesToHash: " << statistics.bytesToHash << "\n"
            << "bytesHashed: " << statistics.bytesHashed << "\n"
            << "bytesToWrite: " << statistics.bytesToWrite << "\n"
            << "bytesWritten: " << statistics.bytesWritten
            << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    std::cout << "volume backup task completed!" << std::endl;
    return 0;
}