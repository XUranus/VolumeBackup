/*================================================================
*   Copyright (C) 2023 XUranus All rights reserved.
*
*   File:         VolumeMountTest.cpp
*   Author:       XUranus
*   Date:         2023-08-27
*   Description:  LLT for volume mount
*
================================================================*/

#include <cstdint>
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
    const std::string SEPARATOR = "/";
    const uint64_t ONE_MB = 1024LLU * 1024LLU;
    const uint64_t ONE_GB = 1024LLU * ONE_MB;
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

    void SetUp() override {
        // mock common mount config
        mountConfig.copyMetaDirPath = DUMMY_META_DIR;
        mountConfig.copyDataDirPath = DUMMY_DATA_DIR;
        mountConfig.mountTargetPath = DUMMY_TARGET_DIR;
        mountConfig.mountFsType = "ext4";
        mountConfig.mountOptions = "noatime";

        // mock copyMetaSingleSliceMock, test mount single slice
        copyMetaSingleSliceMock.copyType = 0;
        copyMetaSingleSliceMock.volumeSize = ONE_GB * 4LLU; // 4GB
        copyMetaSingleSliceMock.blockSize = ONE_MB * 4LLU; // 4MB
        copyMetaSingleSliceMock.volumePath = "/dev/mapper/volumeprotect_dm_dummy_name";
        copyMetaSingleSliceMock.copySlices = std::vector<std::pair<uint64_t, uint64_t>> {
            { 0LLU, 4LLU * ONE_GB }
        };

        // mock copyMutipleSlicesMock, test mount multiple slices
        copyMutipleSlicesMock.copyType = 0;
        copyMutipleSlicesMock.volumeSize = ONE_GB * 10LLU; // 10GB
        copyMutipleSlicesMock.blockSize = ONE_MB * 4LLU; // 4MB
        copyMutipleSlicesMock.volumePath = "/dev/mapper/volumeprotect_dm_dummy_name";
        copyMutipleSlicesMock.copySlices = std::vector<std::pair<uint64_t, uint64_t>> {
            { 0LLU, 4LLU * ONE_GB },
            { 4LLU * ONE_GB, 4LLU * ONE_GB },
            { 8LLU * ONE_GB, 2LLU * ONE_GB }
        };
    }

protected:
    LinuxCopyMountConfig mountConfig {};
    VolumeCopyMeta copyMetaSingleSliceMock {};
    VolumeCopyMeta copyMutipleSlicesMock {};
};

class LinuxMountProviderMock : public LinuxMountProvider {
public:
    LinuxMountProviderMock(const std::string& cacheDirPath);

    ~LinuxMountProviderMock() = default;

    MOCK_METHOD(bool, ReadMountRecord, (LinuxCopyMountRecord& record), (override));

    MOCK_METHOD(bool, ListRecordFiles, (std::vector<std::string>& filelist), (override));

    MOCK_METHOD(bool, ReadVolumeCopyMeta, (const std::string& copyMetaDirPath, VolumeCopyMeta& volumeCopyMeta), (override));

    MOCK_METHOD(bool, SaveMountRecord, (const LinuxCopyMountRecord& mountRecord), (override));

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

LinuxMountProviderMock::LinuxMountProviderMock(const std::string& cacheDirPath)
    : LinuxMountProvider(cacheDirPath)
{}

// get a simple mock item with default mocks
static std::shared_ptr<LinuxMountProviderMock> NewDefaultLinuxMountProviderMock()
{
    auto mountProviderMock = std::make_shared<LinuxMountProviderMock>(DUMMY_CACHE_DIR);
    
    std::vector<std::string> defaultRecordFileList = {
        "1.loop.record",
        "2.loop.record",
        "3.loop.record",
        "xuranus-volume.dm.record"
    };

    LinuxCopyMountRecord defaultCopyMountRecord {};
    defaultCopyMountRecord.dmDeviceName = "volumeprotect_dm_dummy_name";
    defaultCopyMountRecord.loopDevices = {
        "/dev/loop100",
        "/dev/loop200"
    };
    defaultCopyMountRecord.devicePath = "/dev/mapper/volumeprotect_dm_dummy_name";
    defaultCopyMountRecord.mountTargetPath = "/mnt/dummyPoint";

    EXPECT_CALL(*mountProviderMock, SaveMountRecord(_))
        .WillRepeatedly(Return(true));
    EXPECT_CALL(*mountProviderMock, ListRecordFiles(::testing::_))
        .WillRepeatedly(::testing::DoAll(::testing::SetArgReferee<0>(defaultRecordFileList), ::testing::Return(true)));
    EXPECT_CALL(*mountProviderMock, ReadMountRecord(::testing::_))
        .WillRepeatedly(::testing::DoAll(::testing::SetArgReferee<0>(defaultCopyMountRecord), ::testing::Return(true)));
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
    return mountProviderMock;
}

TEST_F(VolumeMountTest, BuildLinuxMountProvider_Test)
{
    EXPECT_TRUE(LinuxMountProvider::BuildLinuxMountProvider("/tmp") != nullptr);
    EXPECT_TRUE(LinuxMountProvider::BuildLinuxMountProvider(DUMMY_CACHE_DIR) == nullptr);
}

TEST_F(VolumeMountTest, MountCopy_SingleSlice_Success)
{
    auto mountProviderMock = NewDefaultLinuxMountProviderMock();
    EXPECT_CALL(*mountProviderMock, ReadVolumeCopyMeta(::testing::_, ::testing::_))
        .WillRepeatedly(::testing::DoAll(::testing::SetArgReferee<1>(copyMetaSingleSliceMock), ::testing::Return(true)));
    EXPECT_TRUE(mountProviderMock->MountCopy(mountConfig));
    EXPECT_EQ(mountProviderMock->GetMountRecordJsonPath(), DUMMY_CACHE_DIR + SEPARATOR + MOUNT_RECORD_JSON_NAME);
}

TEST_F(VolumeMountTest, MountCopy_MultipleSlice_Success)
{
    auto mountProviderMock = NewDefaultLinuxMountProviderMock();
    EXPECT_CALL(*mountProviderMock, ReadVolumeCopyMeta(::testing::_, ::testing::_))
        .WillRepeatedly(::testing::DoAll(::testing::SetArgReferee<1>(copyMutipleSlicesMock), ::testing::Return(true)));
    EXPECT_TRUE(mountProviderMock->MountCopy(mountConfig));
}

TEST_F(VolumeMountTest, MountCopy_FailedForCopyMetaJsonNotFound)
{
    auto mountProviderMock = NewDefaultLinuxMountProviderMock();
    EXPECT_CALL(*mountProviderMock, ReadVolumeCopyMeta(::testing::_, ::testing::_))
        .WillRepeatedly(::testing::DoAll(::testing::SetArgReferee<1>(copyMutipleSlicesMock), ::testing::Return(true)));
    EXPECT_CALL(*mountProviderMock, ReadVolumeCopyMeta(_, _))
        .WillRepeatedly(Return(false));
    EXPECT_FALSE(mountProviderMock->MountCopy(mountConfig));
}

TEST_F(VolumeMountTest, MountCopy_FailedForAttachLoopDeviceFailed)
{
    auto mountProviderMock = NewDefaultLinuxMountProviderMock();
    EXPECT_CALL(*mountProviderMock, ReadVolumeCopyMeta(::testing::_, ::testing::_))
        .WillRepeatedly(::testing::DoAll(::testing::SetArgReferee<1>(copyMutipleSlicesMock), ::testing::Return(true)));
    EXPECT_CALL(*mountProviderMock, AttachReadOnlyLoopDevice(_, _))
        .WillRepeatedly(Return(false));
    EXPECT_FALSE(mountProviderMock->MountCopy(mountConfig));
}

TEST_F(VolumeMountTest, MountCopy_FailedForCreateDmDeviceFailed)
{
    auto mountProviderMock = NewDefaultLinuxMountProviderMock();
    EXPECT_CALL(*mountProviderMock, ReadVolumeCopyMeta(::testing::_, ::testing::_))
        .WillRepeatedly(::testing::DoAll(::testing::SetArgReferee<1>(copyMutipleSlicesMock), ::testing::Return(true)));
    EXPECT_CALL(*mountProviderMock, CreateReadOnlyDmDevice(_, _, _))
        .WillRepeatedly(Return(false));
    EXPECT_FALSE(mountProviderMock->MountCopy(mountConfig));
}

TEST_F(VolumeMountTest, UmountCopy_Success)
{
    auto mountProviderMock = NewDefaultLinuxMountProviderMock();
    EXPECT_TRUE(mountProviderMock->UmountCopy());
}

TEST_F(VolumeMountTest, UmountCopy_FailedForMountRecordNotFound)
{
    auto mountProviderMock = NewDefaultLinuxMountProviderMock();
    EXPECT_CALL(*mountProviderMock, ReadMountRecord(_))
        .WillRepeatedly(Return(false));
    EXPECT_FALSE(mountProviderMock->UmountCopy());
}

TEST_F(VolumeMountTest, UmountCopy_FailedForIOError)
{
    auto mountProviderMock = NewDefaultLinuxMountProviderMock();
    EXPECT_CALL(*mountProviderMock, UmountDeviceIfExists(_))
        .WillRepeatedly(Return(false));
    EXPECT_CALL(*mountProviderMock, RemoveDmDeviceIfExists(_))
        .WillRepeatedly(Return(false));
    EXPECT_CALL(*mountProviderMock, DetachLoopDeviceIfAttached(_))
        .WillRepeatedly(Return(false));
    EXPECT_FALSE(mountProviderMock->UmountCopy());
    EXPECT_NO_THROW(mountProviderMock->GetErrors());
}

TEST_F(VolumeMountTest, MountCopy_FailedForMount)
{
    auto mountProviderMock = NewDefaultLinuxMountProviderMock();
    EXPECT_CALL(*mountProviderMock, MountReadOnlyDevice(_, _, _, _))
        .WillRepeatedly(Return(false));
    
    EXPECT_CALL(*mountProviderMock, ReadVolumeCopyMeta(::testing::_, ::testing::_))
        .WillRepeatedly(::testing::DoAll(::testing::SetArgReferee<1>(copyMutipleSlicesMock), ::testing::Return(true)));
    
    EXPECT_FALSE(mountProviderMock->MountCopy(mountConfig));
    EXPECT_NO_THROW(mountProviderMock->GetErrors());
    EXPECT_TRUE(mountProviderMock->ClearResidue());
}

TEST_F(VolumeMountTest, MountCopy_ClearResidueFailed)
{
    auto mountProviderMock = NewDefaultLinuxMountProviderMock();
    // to make both LoadResidualDmDeviceList and LoadResidualLoopDeviceList fail
    EXPECT_CALL(*mountProviderMock, RemoveDmDeviceIfExists(_))
        .WillRepeatedly(Return(false));
    EXPECT_CALL(*mountProviderMock, DetachLoopDeviceIfAttached(_))
        .WillRepeatedly(Return(false));
    EXPECT_FALSE(mountProviderMock->ClearResidue());
    EXPECT_NO_THROW(mountProviderMock->GetErrors());
}