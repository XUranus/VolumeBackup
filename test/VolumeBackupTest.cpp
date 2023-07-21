/*================================================================
*   Copyright (C) 2023 XUranus All rights reserved.
*   
*   File:         VolumeBackupTest.cpp
*   Author:       XUranus
*   Date:         2023-07-20
*   Description:  
*
================================================================*/

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <vector>
#include <string>
#include <thread>

#include "VolumeProtector.h"
#include "Logger.h"

using namespace volumeprotect;

class VolumeBackupTest : public ::testing::Test {
protected:
    static void SetUpTestCase() {
        
    }

    static void TearDownTestCase() {}
};

TEST_F(VolumeBackupTest, BasicTest)
{
    EXPECT_TRUE(true);
}

TEST_F(VolumeBackupTest, VolumeBackupTaskTest)
{
    VolumeRestoreConfig restoreConfig {};
    // restoreConfig.blockDevicePath = blockDevicePath;
    // restoreConfig.copyDataDirPath = copyDataDirPath;
    // restoreConfig.copyMetaDirPath = copyMetaDirPath;

    std::shared_ptr<VolumeProtectTask> task = VolumeProtectTask::BuildRestoreTask(restoreConfig);
    if (task == nullptr) {
        std::cerr << "failed to build restore task" << std::endl;
        return;
    }
    task->Start();
    while (!task->IsTerminated()) {
        TaskStatistics statistics =  task->GetStatistics();
        DBGLOG("checkStatistics: bytesToReaded: %llu, bytesRead: %llu, blocksToHash: %llu, blocksHashed: %llu, bytesToWrite: %llu, bytesWritten: %llu",
            statistics.bytesToRead, statistics.bytesRead, statistics.blocksToHash, statistics.blocksHashed, statistics.bytesToWrite, statistics.bytesWritten);
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    EXPECT_NO_THROW();

}

TEST_F(VolumeBackupTest, VolumeRestoreTaskTest)
{
    VolumeBackupConfig backupConfig {};
    // backupConfig.copyType = copyType;
    // backupConfig.blockDevicePath = blockDevicePath;
    // backupConfig.prevCopyMetaDirPath = prevCopyMetaDirPath;
    // backupConfig.outputCopyDataDirPath = outputCopyDataDirPath;
    // backupConfig.outputCopyMetaDirPath = outputCopyMetaDirPath;
    backupConfig.blockSize = DEFAULT_BLOCK_SIZE;
    backupConfig.sessionSize = DEFAULT_SESSION_SIZE;
    backupConfig.hasherNum = DEFAULT_HASHER_NUM;
    backupConfig.hasherEnabled = true;

    std::shared_ptr<VolumeProtectTask> task = VolumeProtectTask::BuildBackupTask(backupConfig);
    if (task == nullptr) {
        std::cerr << "failed to build backup task" << std::endl;
        return;
    }
    task->Start();
    while (!task->IsTerminated()) {
        TaskStatistics statistics =  task->GetStatistics();
        DBGLOG("checkStatistics: bytesToReaded: %llu, bytesRead: %llu, blocksToHash: %llu, blocksHashed: %llu, bytesToWrite: %llu, bytesWritten: %llu",
            statistics.bytesToRead, statistics.bytesRead, statistics.blocksToHash, statistics.blocksHashed, statistics.bytesToWrite, statistics.bytesWritten);
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    EXPECT_NO_THROW();
}