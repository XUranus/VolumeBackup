#include <bits/stdc++.h>
#include <chrono>
#include <getopt.h>
#include <thread>

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

int main(int argc, char** argv)
{
    VolumeBackupConfig backupConfig {};
    std::shared_ptr<VolumeBackupTask> task = VolumeBackupTask::BuildBackupTask(backupConfig);
    task->Start();
    while (!task->IsTerminated()) {
        TaskStatistics statistics =  task->GetStatistics();
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    return 0;
}