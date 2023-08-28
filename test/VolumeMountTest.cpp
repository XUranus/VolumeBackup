/*================================================================
*   Copyright (C) 2023 XUranus All rights reserved.
*
*   File:         VolumeMountTest.cpp
*   Author:       XUranus
*   Date:         2023-08-27
*   Description:  LLT for volume mount
*
================================================================*/

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <vector>
#include <string>
#include <thread>

#include "CopyMountProvider.h"
#include "Logger.h"

using namespace ::testing;
using namespace volumeprotect;
using namespace volumeprotect::mount;


class VolumeMountTest : public ::testing::Test {
protected:
    static void SetUpTestCase() {
        using namespace xuranus::minilogger;
        LoggerConfig conf {};
        conf.target = LoggerTarget::STDOUT;
        Logger::GetInstance()->SetLogLevel(LoggerLevel::ERROR);
        if (!Logger::GetInstance()->Init(conf)) {
            std::cerr << "Init logger failed" << std::endl;
        }
    }

    static void TearDownTestCase() {
        std::cout << "TearDown" << std::endl;
        using namespace xuranus::minilogger;
        Logger::GetInstance()->Destroy();
    }
};

class LinuxMountProviderMock : public LinuxMountProvider {
public:
    MOCK_METHOD(bool, ReadMountRecord, (LinuxCopyMountRecord& record), (override));

    MOCK_METHOD(bool, SaveMountRecord, (LinuxCopyMountRecord& mountRecord), (override));

    MOCK_METHOD(bool, ReadVolumeCopyMeta, (const std::string& copyMetaDirPath, VolumeCopyMeta& volumeCopyMeta), (override));

    MOCK_METHOD(bool, MountReadOnlyDevice, (
        const std::string& devicePath,
        const std::string& mountTargetPath,
        const std::string& fsType,
        const std::string& mountOptions), (override));
    
    MOCK_METHOD(bool, UmountDeviceIfExists, (const std::string& mountTargetPath), (override));
    
    MOCK_METHOD(bool, CreateReadOnlyDmDevice, (
        const std::vector<CopySliceTarget> copySlices,
        std::string& dmDeviceName,
        std::string& dmDevicePath), (override));
    
    MOCK_METHOD(bool, RemoveDmDeviceIfExists, (const std::string& dmDeviceName), (override));
    
    MOCK_METHOD(bool, AttachReadOnlyLoopDevice, (const std::string& filePath, std::string& loopDevicePath), (override));
    
    MOCK_METHOD(bool, DetachLoopDeviceIfAttached, (const std::string& loopDevicePath), (override));

    // native interface ...
    MOCK_METHOD(bool, CreateEmptyFileInCacheDir, (const std::string& filename), (override));

    MOCK_METHOD(bool, RemoveFileInCacheDir, (const std::string& filename), (override));

    MOCK_METHOD(bool, ListRecordFiles, (std::vector<std::string>& filelist), (override));
};

TEST_F(VolumeMountTest, Mount_Success)
{
    EXPECT_TRUE(true);
}