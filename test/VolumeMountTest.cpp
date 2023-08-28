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
#include <memory>
#include <vector>
#include <string>
#include <thread>

#include "CopyMountProvider.h"
#include "Logger.h"

using namespace ::testing;
using namespace volumeprotect;
using namespace volumeprotect::mount;


namespace {
    const std::string DUMMY_CACHE_DIR = "/dummy/cache";
    const std::string DUMMY_TARGET_DIR = "/dummy/target";
    const std::string DUMMY_META_DIR = "/dummy/meta";
    const std::string DUMMY_DATA_DIR = "/dummy/data";
}

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
    LinuxMountProviderMock();

    bool ReadMountRecord(LinuxCopyMountRecord& record) override;

    bool ReadVolumeCopyMeta(const std::string& copyMetaDirPath, VolumeCopyMeta& volumeCopyMeta) override;
    
    bool ListRecordFiles(std::vector<std::string>& filelist) override;

    MOCK_METHOD(bool, SaveMountRecord, (LinuxCopyMountRecord& mountRecord), (override));

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
};

LinuxMountProviderMock::LinuxMountProviderMock()
    : LinuxMountProvider(DUMMY_CACHE_DIR)
{}

bool ReadMountRecord(LinuxCopyMountRecord& record)
{
    
    return true;
}

bool ReadVolumeCopyMeta(const std::string& copyMetaDirPath, VolumeCopyMeta& volumeCopyMeta)
{
    return true;
}

bool ListRecordFiles(std::vector<std::string>& filelist)
{
    filelist = {
        "1.loop.record",
        "2.loop.record",
        "3.loop.record",
        "xuranus-volume.dm.record"
    };
    return true;
}

TEST_F(VolumeMountTest, Mount_Success)
{
    auto mountProviderMock = std::make_unique<LinuxMountProviderMock>();
    EXPECT_CALL(*mountProviderMock, SaveMountRecord(_))
        .WillRepeatedly(Return(true));
    EXPECT_CALL(*mountProviderMock, MountReadOnlyDevice(_, _, _, _))
        .WillRepeatedly(Return(true));
    EXPECT_CALL(*mountProviderMock, UmountDeviceIfExists(_))
        .WillRepeatedly(Return(true));
    EXPECT_CALL(*mountProviderMock, CreateReadOnlyDmDevice(_, _, _))
        .WillRepeatedly(Return(true));
    EXPECT_CALL(*mountProviderMock, RemoveDmDeviceIfExists(_))
        .WillRepeatedly(Return(true));
    EXPECT_CALL(*mountProviderMock, AttachReadOnlyLoopDevice(_, _))
        .WillRepeatedly(Return(true));
    EXPECT_CALL(*mountProviderMock, DetachLoopDeviceIfAttached(_))
        .WillRepeatedly(Return(true));
    EXPECT_CALL(*mountProviderMock, CreateEmptyFileInCacheDir(_))
        .WillRepeatedly(Return(true));
    EXPECT_CALL(*mountProviderMock, RemoveFileInCacheDir(_))
        .WillRepeatedly(Return(true));

    LinuxCopyMountConfig mountConfig {};
    mountConfig.copyMetaDirPath = DUMMY_META_DIR;
    mountConfig.copyDataDirPath = DUMMY_DATA_DIR;
    mountConfig.mountTargetPath = DUMMY_TARGET_DIR;
    mountConfig.mountFsType = "ext4";
    mountConfig.mountOptions = "noatime";
    EXPECT_TRUE(mountProviderMock->MountCopy(mountConfig));
}