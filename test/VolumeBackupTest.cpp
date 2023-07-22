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
#include "VolumeBackupTask.h"
#include "VolumeRestoreTask.h"
#include "VolumeProtectTaskContext.h"
#include "VolumeUtils.h"
#include "Logger.h"

using namespace ::testing;
using namespace volumeprotect;

class VolumeBackupTest : public ::testing::Test {
protected:
    static void SetUpTestCase() {
        
    }

    static void TearDownTestCase() {}
};






class VolumeBackupTaskMock : public VolumeBackupTask {
public:
    VolumeBackupTaskMock(const VolumeBackupConfig& backupConfig, uint64_t volumeSize);
    MOCK_METHOD(bool, InitBackupSessionContext, (std::shared_ptr<VolumeTaskSession>), (const));
};

VolumeBackupTaskMock::VolumeBackupTaskMock(const VolumeBackupConfig& backupConfig, uint64_t volumeSize)
   : VolumeBackupTask(backupConfig, volumeSize)
{}


TEST_F(VolumeBackupTest, VolumeBackupTaskBasicTest)
{
    VolumeBackupConfig backupConfig {};
    uint64_t volumeSize = 1024 * 1024 * 1024; // 1GB
    VolumeBackupTaskMock backupTaskMock(backupConfig, volumeSize);
    backupTaskMock.Start();
}