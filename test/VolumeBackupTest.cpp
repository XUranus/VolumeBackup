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
#include "VolumeBlockReader.h"
#include "VolumeBlockWriter.h"
#include "VolumeBlockHasher.h"
#include "VolumeUtils.h"
#include "Logger.h"

using namespace ::testing;
using namespace volumeprotect;

namespace {
    constexpr auto DEFAULT_ALLOCATOR_BLOCK_NUM = 32;
    constexpr auto DEFAULT_QUEUE_SIZE = 32LLU;
    constexpr auto DEFAULT_MOCK_SESSION_BLOCK_SIZE = 4LLU * ONE_MB;
    constexpr auto DEFAULT_MOCK_SESSION_SIZE = 512LLU * ONE_MB;
    constexpr auto DEFAULT_MOCK_HASHER_NUM = 4LU;
}

class VolumeBackupTest : public ::testing::Test {
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

class DataReaderMock : public native::DataReader {
public:
    MOCK_METHOD(bool, Read, (uint64_t offset, char* buffer, int length, native::ErrCodeType& errorCode), (override));
    MOCK_METHOD(bool, Ok, (), (override));
    MOCK_METHOD(native::ErrCodeType, Error, (), (override));
};

class DataWriterMock : public native::DataWriter {
public:
    MOCK_METHOD(bool, Write, (uint64_t offset, char* buffer, int length, native::ErrCodeType& errorCode), (override));
    MOCK_METHOD(bool, Ok, (), (override));
    MOCK_METHOD(native::ErrCodeType, Error, (), (override));
};

static void InitSessionBasic(std::shared_ptr<VolumeTaskSession> session)
{
    std::string volumePath = "/dummy/volumePath";
    std::string targetPath = "/dummy/targetPath";
    uint64_t offset = 0LLU;
    uint64_t length = 512 * ONE_MB;
    auto sharedConfig = std::make_shared<VolumeTaskSharedConfig>();
    sharedConfig->sessionOffset = 0LLU;
    sharedConfig->sessionSize = length;
    sharedConfig->blockSize = DEFAULT_MOCK_SESSION_BLOCK_SIZE;
    sharedConfig->hasherEnabled = true;
    sharedConfig->volumePath = volumePath;
    sharedConfig->copyFilePath = targetPath;
    sharedConfig->hasherEnabled = true;
    session->sharedConfig = sharedConfig;
}

static void InitSessionSharedContext(std::shared_ptr<VolumeTaskSession> session)
{
    auto sharedContext = std::make_shared<VolumeTaskSharedContext>();
    // init session container
    sharedContext->counter = std::make_shared<SessionCounter>();
    sharedContext->allocator = std::make_shared<VolumeBlockAllocator>(session->sharedConfig->blockSize, DEFAULT_ALLOCATOR_BLOCK_NUM);
    sharedContext->hashingQueue = std::make_shared<BlockingQueue<VolumeConsumeBlock>>(DEFAULT_QUEUE_SIZE);
    sharedContext->writeQueue = std::make_shared<BlockingQueue<VolumeConsumeBlock>>(DEFAULT_QUEUE_SIZE);
    session->sharedContext = sharedContext;
}

static void InitSessionBlockReader(
    std::shared_ptr<VolumeTaskSession> session,
    std::shared_ptr<native::DataReader> dataReader)
{
    VolumeBlockReaderParam readerParam {
        SourceType::VOLUME,
        session->sharedConfig->volumePath,
        session->sharedConfig->sessionOffset,
        session->sharedConfig->sessionSize,
        dataReader,
        session->sharedConfig,
        session->sharedContext
    };
    auto volumeBlockReader = std::make_shared<VolumeBlockReader>(readerParam);
    session->readerTask = volumeBlockReader;
}

static void InitSessionBlockWriter(
    std::shared_ptr<VolumeTaskSession> session,
    std::shared_ptr<native::DataWriter> dataWriter)
{
    VolumeBlockWriterParam writerParam {
        TargetType::COPYFILE, session->sharedConfig->copyFilePath, session->sharedConfig, session->sharedContext, dataWriter
    };
    auto volumeBlockWriter = std::make_shared<VolumeBlockWriter>(writerParam);
    session->writerTask = volumeBlockWriter;
}

static void InitSessionBlockHasher(
	std::shared_ptr<VolumeTaskSession> session)
{
    uint32_t singleChecksumSize = 32LU; // SHA-256
    std::string previousChecksumBinPath = "/dummy/checksum1";
    std::string lastestChecksumBinPath = "/dummy/checksum2";
    
    // init hasher context
    uint64_t prevChecksumTableSize = singleChecksumSize * (session->sharedConfig->sessionSize / session->sharedConfig->blockSize);
    uint64_t lastestChecksumTableSize = singleChecksumSize * (session->sharedConfig->sessionSize / session->sharedConfig->blockSize);
    auto lastestChecksumTable = new char[prevChecksumTableSize];
    auto prevChecksumTable = new char[lastestChecksumTableSize];

    VolumeBlockHasherParam hasherParam {
        session->sharedConfig, session->sharedContext, HasherForwardMode::DIRECT, previousChecksumBinPath, lastestChecksumBinPath, singleChecksumSize,
        prevChecksumTable, prevChecksumTableSize,
        lastestChecksumTable, lastestChecksumTableSize
    };
    auto volumeBlockHasher = std::make_shared<VolumeBlockHasher>(hasherParam);
    session->hasherTask = volumeBlockHasher;
}

class VolumeBackupTaskMock : public VolumeBackupTask
{
public:
    VolumeBackupTaskMock(const VolumeBackupConfig& backupConfig, uint64_t volumeSize);
    bool InitBackupSessionContext(std::shared_ptr<VolumeTaskSession> session) const;
};

VolumeBackupTaskMock::VolumeBackupTaskMock(const VolumeBackupConfig& backupConfig, uint64_t volumeSize)
  : VolumeBackupTask(backupConfig, volumeSize) {}

bool VolumeBackupTaskMock::InitBackupSessionContext(std::shared_ptr<VolumeTaskSession> session) const
{
    // init mock
    auto dataReaderMock = std::make_shared<DataReaderMock>();
    EXPECT_CALL(*dataReaderMock, Read(_, _, _, _))
        .WillRepeatedly(Return(true));
    EXPECT_CALL(*dataReaderMock, Ok())
        .WillRepeatedly(Return(true));
    EXPECT_CALL(*dataReaderMock, Error())
        .WillRepeatedly(Return(static_cast<native::ErrCodeType>(0)));

    auto dataWriterMock = std::make_shared<DataWriterMock>();
    EXPECT_CALL(*dataWriterMock, Write(_, _, _, _))
        .WillRepeatedly(Return(true));
    EXPECT_CALL(*dataWriterMock, Ok())
        .WillRepeatedly(Return(true));
    EXPECT_CALL(*dataWriterMock, Error())
        .WillRepeatedly(Return(static_cast<native::ErrCodeType>(0)));

    // init session
    InitSessionSharedContext(session);
    InitSessionBlockReader(session, std::dynamic_pointer_cast<native::DataReader>(dataReaderMock));
    InitSessionBlockWriter(session, std::dynamic_pointer_cast<native::DataWriter>(dataWriterMock));
    InitSessionBlockHasher(session);
    return true;
}

TEST_F(VolumeBackupTest, VolumeBackupTaskSucessTest)
{
    // init mock
    auto dataReaderMock = std::make_shared<DataReaderMock>();
    EXPECT_CALL(*dataReaderMock, Read(_, _, _, _))
        .WillRepeatedly(Return(true));
    EXPECT_CALL(*dataReaderMock, Ok())
        .WillRepeatedly(Return(true));
    EXPECT_CALL(*dataReaderMock, Error())
        .WillRepeatedly(Return(static_cast<native::ErrCodeType>(0)));

    auto dataWriterMock = std::make_shared<DataWriterMock>();
    EXPECT_CALL(*dataWriterMock, Write(_, _, _, _))
        .WillRepeatedly(Return(true));
    EXPECT_CALL(*dataWriterMock, Ok())
        .WillRepeatedly(Return(true));
    EXPECT_CALL(*dataWriterMock, Error())
        .WillRepeatedly(Return(static_cast<native::ErrCodeType>(0)));

    // init session
    auto session = std::make_shared<VolumeTaskSession>();
    InitSessionBasic(session);
    InitSessionSharedContext(session);
    InitSessionBlockReader(session, std::dynamic_pointer_cast<native::DataReader>(dataReaderMock));
    InitSessionBlockWriter(session, std::dynamic_pointer_cast<native::DataWriter>(dataWriterMock));
    InitSessionBlockHasher(session);

    EXPECT_TRUE(session->readerTask->Start());
    EXPECT_TRUE(session->hasherTask->Start());
    EXPECT_TRUE(session->writerTask->Start());

    // wait all component to terminate
    while (!session->readerTask->IsTerminated()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    while (!session->writerTask->IsTerminated()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    while (!session->hasherTask->IsTerminated()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    EXPECT_EQ(session->readerTask->GetStatus(), TaskStatus::SUCCEED);
    EXPECT_EQ(session->writerTask->GetStatus(), TaskStatus::SUCCEED);
    EXPECT_EQ(session->hasherTask->GetStatus(), TaskStatus::SUCCEED);
}

TEST_F(VolumeBackupTest, VolumeBackupTaskReadFailedTest)
{
    // init mock (Read call failed)
    auto dataReaderMock = std::make_shared<DataReaderMock>();
    EXPECT_CALL(*dataReaderMock, Read(_, _, _, _))
        .WillRepeatedly(Return(false));
    EXPECT_CALL(*dataReaderMock, Ok())
        .WillRepeatedly(Return(false));
    EXPECT_CALL(*dataReaderMock, Error())
        .WillRepeatedly(Return(static_cast<native::ErrCodeType>(0)));

    auto dataWriterMock = std::make_shared<DataWriterMock>();
    EXPECT_CALL(*dataWriterMock, Write(_, _, _, _))
        .WillRepeatedly(Return(false));
    EXPECT_CALL(*dataWriterMock, Ok())
        .WillRepeatedly(Return(false));
    EXPECT_CALL(*dataWriterMock, Error())
        .WillRepeatedly(Return(static_cast<native::ErrCodeType>(0)));

    // init session
    auto session = std::make_shared<VolumeTaskSession>();
    InitSessionBasic(session);
    InitSessionSharedContext(session);
    InitSessionBlockReader(session, std::dynamic_pointer_cast<native::DataReader>(dataReaderMock));
    InitSessionBlockWriter(session, std::dynamic_pointer_cast<native::DataWriter>(dataWriterMock));
    InitSessionBlockHasher(session);

    EXPECT_FALSE(session->readerTask->Start());
    EXPECT_FALSE(session->writerTask->Start());

    // wait all component to terminate
    while (!session->readerTask->IsTerminated()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    while (!session->writerTask->IsTerminated()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    EXPECT_EQ(session->readerTask->GetStatus(), TaskStatus::FAILED);
    EXPECT_EQ(session->writerTask->GetStatus(), TaskStatus::FAILED);
    EXPECT_TRUE(session->readerTask->IsFailed());
    EXPECT_TRUE(session->writerTask->IsFailed());
}

TEST_F(VolumeBackupTest, VolumeBackTaskMockInvalidVolumeTest)
{
    VolumeBackupConfig backupConfig;
    backupConfig.blockSize = DEFAULT_MOCK_SESSION_BLOCK_SIZE;
    backupConfig.copyType = CopyType::FULL;
    backupConfig.hasherEnabled = true;
    backupConfig.hasherNum = DEFAULT_MOCK_HASHER_NUM;
    backupConfig.sessionSize = DEFAULT_MOCK_SESSION_SIZE;
    backupConfig.volumePath = "/dev/dummy";

    auto backupTask = std::make_shared<VolumeBackupTaskMock>(backupConfig, 4LLU * ONE_GB);
    EXPECT_FALSE(backupTask->Start());
    while (!backupTask->IsTerminated()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    backupTask->Abort();
    backupTask->GetStatistics();
}

TEST_F(VolumeBackupTest, VolumeBackupTaskInvalidVolumeTest)
{
    VolumeBackupConfig backupConfig {};
    backupConfig.blockSize = DEFAULT_MOCK_SESSION_BLOCK_SIZE;
    backupConfig.copyType = CopyType::FULL;
    backupConfig.hasherEnabled = true;
    backupConfig.hasherNum = DEFAULT_MOCK_HASHER_NUM;
    backupConfig.sessionSize = DEFAULT_MOCK_SESSION_SIZE;
    backupConfig.volumePath = "/dev/dummy";
    auto backupTask = VolumeProtectTask::BuildBackupTask(backupConfig);
    EXPECT_TRUE(backupTask == nullptr);
}