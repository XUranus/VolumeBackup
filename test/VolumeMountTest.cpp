/*================================================================
*   Copyright (C) 2023 XUranus All rights reserved.
*
*   File:         VolumeMountTest.cpp
*   Author:       XUranus
*   Date:         2023-08-27
*   Description:  LLT for volume mount
*
================================================================*/

#include <cstddef>
#include <cstdint>
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <memory>
#include <vector>
#include <string>
#include <thread>

#include "VolumeCopyMountProvider.h"
#include "Logger.h"

using namespace ::testing;
using namespace volumeprotect;
using namespace volumeprotect::mount;

namespace {
    const std::string DUMMY_CACHE_DIR = "/dummy/cache";
    const std::string DUMMY_TARGET_DIR = "/dummy/target";
    const std::string DUMMY_META_DIR = "/dummy/meta";
    const std::string DUMMY_DATA_DIR = "/dummy/data";
    const uint64_t ONE_MB = 1024LLU * 1024LLU;
    const uint64_t ONE_GB = 1024LLU * ONE_MB;
}

class VolumeCopyMountProviderTest : public ::testing::Test {
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

// basic class does not provide any functionality
TEST_F(VolumeCopyMountProviderTest, TestBaseClass)
{
    std::shared_ptr<VolumeCopyMountProvider> mountProvider = std::make_shared<VolumeCopyMountProvider>();
    EXPECT_FALSE(mountProvider->Mount());
    EXPECT_TRUE(mountProvider->GetMountRecordPath().empty());
    EXPECT_FALSE(mountProvider->GetError().empty());
    EXPECT_FALSE(mountProvider->GetErrors().empty());

    std::shared_ptr<VolumeCopyUmountProvider> umountProvider = std::make_shared<VolumeCopyUmountProvider>();
    EXPECT_FALSE(umountProvider->Umount());
    EXPECT_FALSE(umountProvider->GetError().empty());
    EXPECT_FALSE(umountProvider->GetErrors().empty());
}

TEST_F(VolumeCopyMountProviderTest, TestBuildMountProviderFail)
{
    VolumeCopyMountConfig mountConfig {};
    mountConfig.outputDirPath = "/tmp";
    mountConfig.copyName = "dummyCopyName";
    mountConfig.copyMetaDirPath = "/tmp";
    mountConfig.copyDataDirPath = "/tmp";
    mountConfig.mountTargetPath = "/tmp/mnt";
    auto mountProvider = VolumeCopyMountProvider::Build(mountConfig);
    EXPECT_TRUE(mountProvider == nullptr);
}

TEST_F(VolumeCopyMountProviderTest, TestBuildUmountProviderFail)
{
    std::string mountRecordJsonFilePath = "/tmp/dummyRecord.json";
    auto umountProvider = VolumeCopyUmountProvider::Build(mountRecordJsonFilePath);
    EXPECT_TRUE(umountProvider == nullptr);
}