/*================================================================
*   Copyright (C) 2023 XUranus All rights reserved.
*
*   File:         VolumeBackupTest.cpp
*   Author:       XUranus
*   Date:         2023-07-20
*   Description:  LLT for volume backup
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
    constexpr auto DEFAULT_MOCK_SESSION_BLOCK_SIZE = 4LLU * ONE_MB;
    constexpr auto DEFAULT_MOCK_SESSION_SIZE = 513LLU * ONE_MB;
    constexpr auto DEFAULT_MOCK_HASHER_NUM = 8LU;
    constexpr auto TASK_CHECK_SLEEP_INTERVAL = std::chrono::milliseconds(100);
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

class DataReaderMock : public rawio::RawDataReader {
public:
    MOCK_METHOD(bool, Read, (uint64_t offset, uint8_t* buffer, int length, fsapi::ErrCodeType& errorCode), (override));
    MOCK_METHOD(bool, Ok, (), (override));
    MOCK_METHOD(fsapi::ErrCodeType, Error, (), (override));
};

class DataWriterMock : public rawio::RawDataWriter {
public:
    MOCK_METHOD(bool, Write, (uint64_t offset, uint8_t* buffer, int length, fsapi::ErrCodeType& errorCode), (override));
    MOCK_METHOD(bool, Ok, (), (override));
    MOCK_METHOD(bool, Flush, (), (override));
    MOCK_METHOD(fsapi::ErrCodeType, Error, (), (override));
};

static void InitSessionSharedConfig(std::shared_ptr<VolumeTaskSession> session)
{
    auto sharedConfig = std::make_shared<VolumeTaskSharedConfig>();
    sharedConfig->sessionOffset = 0LLU;
    sharedConfig->sessionSize = 1000 * ONE_MB; // make it not divide by block size
    sharedConfig->blockSize = DEFAULT_MOCK_SESSION_BLOCK_SIZE;
    sharedConfig->hasherEnabled = true;
    sharedConfig->checkpointEnabled = true;
    sharedConfig->volumePath = "/dummy/volumePath";
    sharedConfig->copyFilePath = "/dummy/targetPath";
    session->sharedConfig = sharedConfig;
}

static void InitSessionSharedContext(std::shared_ptr<VolumeTaskSession> session)
{
    auto sharedContext = std::make_shared<VolumeTaskSharedContext>();
    // init basic container
    sharedContext->counter = std::make_shared<SessionCounter>();
    sharedContext->allocator = std::make_shared<VolumeBlockAllocator>(
        session->sharedConfig->blockSize, DEFAULT_ALLOCATOR_BLOCK_NUM);
    sharedContext->hashingQueue = std::make_shared<BlockingQueue<VolumeConsumeBlock>>(DEFAULT_QUEUE_SIZE);
    // init hasher context
    sharedContext->writeQueue = std::make_shared<BlockingQueue<VolumeConsumeBlock>>(DEFAULT_QUEUE_SIZE);
    uint64_t blockCount = session->sharedConfig->sessionSize / static_cast<uint64_t>(session->sharedConfig->blockSize); 
    uint64_t numBlocks = session->TotalBlocks();
    uint64_t lastestChecksumTableSize = numBlocks * SHA256_CHECKSUM_SIZE;
    uint64_t prevChecksumTableSize = lastestChecksumTableSize;
    sharedContext->hashingContext = std::make_shared<BlockHashingContext>(prevChecksumTableSize, lastestChecksumTableSize);
    // init bitmap
    sharedContext->processedBitmap = std::make_shared<Bitmap>(numBlocks);
    sharedContext->writtenBitmap = std::make_shared<Bitmap>(numBlocks);
    session->sharedContext = sharedContext;
}

static void InitSessionBlockVolumeReader(
    std::shared_ptr<VolumeTaskSession> session,
    std::shared_ptr<rawio::RawDataReader> dataReader)
{
    VolumeBlockReaderParam readerParam {
        SourceType::VOLUME,
        session->sharedConfig->volumePath,
        session->sharedConfig->sessionOffset,
        dataReader,
        session->sharedConfig,
        session->sharedContext
    };
    auto volumeBlockReader = std::make_shared<VolumeBlockReader>(readerParam);
    session->readerTask = volumeBlockReader;
}

static void InitSessionBlockCopyWriter(
    std::shared_ptr<VolumeTaskSession> session,
    std::shared_ptr<rawio::RawDataWriter> dataWriter)
{
    VolumeBlockWriterParam writerParam {
        TargetType::COPYFILE,
        session->sharedConfig->copyFilePath,
        session->sharedConfig,
        session->sharedContext,
        dataWriter
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
        session->sharedConfig, session->sharedContext, DEFAULT_HASHER_NUM,
        HasherForwardMode::DIFF, singleChecksumSize
    };
    auto volumeBlockHasher = std::make_shared<VolumeBlockHasher>(hasherParam);
    session->hasherTask = volumeBlockHasher;
}

// Test Backup From Here...

class VolumeBackupTaskMock : public VolumeBackupTask
{
public:
    VolumeBackupTaskMock(const VolumeBackupConfig& backupConfig, uint64_t volumeSize);
    bool ValidateIncrementBackup() const;
    bool InitBackupSessionTaskExecutor(std::shared_ptr<VolumeTaskSession> session) const;
    bool SaveVolumeCopyMeta(const std::string& copyMetaDirPath, const VolumeCopyMeta& volumeCopyMeta) const;
    bool LoadSessionPreviousCopyChecksum(std::shared_ptr<VolumeTaskSession> session) const;
    std::shared_ptr<CheckpointSnapshot> ReadCheckpointSnapshot(
        std::shared_ptr<VolumeTaskSession> session) const;
    bool ReadLatestHashingTable(std::shared_ptr<VolumeTaskSession> session) const;
    bool IsSessionRestarted(std::shared_ptr<VolumeTaskSession> session) const;

    MOCK_METHOD(bool, SaveVolumeCopyMetaMockReturn, (), (const));
    MOCK_METHOD(bool, DataReaderReadMockReturn, (), (const));
    MOCK_METHOD(bool, DataWriterWriteMockReturn, (), (const));
    MOCK_METHOD(bool, LoadSessionPreviousCopyChecksumMockReturn, (), (const));
};

VolumeBackupTaskMock::VolumeBackupTaskMock(const VolumeBackupConfig& backupConfig, uint64_t volumeSize)
  : VolumeBackupTask(backupConfig, volumeSize) {}

bool VolumeBackupTaskMock::ValidateIncrementBackup() const
{
    return true;
}

bool VolumeBackupTaskMock::SaveVolumeCopyMeta(
    const std::string& copyMetaDirPath, const VolumeCopyMeta& volumeCopyMeta) const
{
    return SaveVolumeCopyMetaMockReturn();
}

bool VolumeBackupTaskMock::LoadSessionPreviousCopyChecksum(std::shared_ptr<VolumeTaskSession> session) const
{
    return LoadSessionPreviousCopyChecksumMockReturn();
}

bool VolumeBackupTaskMock::IsSessionRestarted(std::shared_ptr<VolumeTaskSession> session) const
{
    return true;
}

std::shared_ptr<CheckpointSnapshot> VolumeBackupTaskMock::ReadCheckpointSnapshot(
    std::shared_ptr<VolumeTaskSession> session) const
{
    uint64_t bitmapBytes = session->TotalBlocks() / 8 + 1;
    return std::make_shared<CheckpointSnapshot>(bitmapBytes);
}

bool VolumeBackupTaskMock::ReadLatestHashingTable(std::shared_ptr<VolumeTaskSession> session) const
{
    return true;
}

bool VolumeBackupTaskMock::InitBackupSessionTaskExecutor(std::shared_ptr<VolumeTaskSession> session) const
{
    // init mock
    auto dataReaderMock = std::make_shared<DataReaderMock>();
    EXPECT_CALL(*dataReaderMock, Read(_, _, _, _))
        .WillRepeatedly(Return(DataReaderReadMockReturn()));
    EXPECT_CALL(*dataReaderMock, Ok())
        .WillRepeatedly(Return(true));
    EXPECT_CALL(*dataReaderMock, Error())
        .WillRepeatedly(Return(static_cast<fsapi::ErrCodeType>(0)));

    auto dataWriterMock = std::make_shared<DataWriterMock>();
    EXPECT_CALL(*dataWriterMock, Write(_, _, _, _))
        .WillRepeatedly(Return(DataWriterWriteMockReturn()));
    EXPECT_CALL(*dataWriterMock, Ok())
        .WillRepeatedly(Return(true));
    EXPECT_CALL(*dataWriterMock, Error())
        .WillRepeatedly(Return(static_cast<fsapi::ErrCodeType>(0)));
    EXPECT_CALL(*dataWriterMock, Flush())
        .WillRepeatedly(Return(true));

    // init session
    InitSessionBlockVolumeReader(session, std::dynamic_pointer_cast<rawio::RawDataReader>(dataReaderMock));
    InitSessionBlockCopyWriter(session, std::dynamic_pointer_cast<rawio::RawDataWriter>(dataWriterMock));
    InitSessionBlockHasher(session);
    return true;
}

TEST_F(VolumeBackupTest, VolumeBackupTask_InvalidDataReaderOrWriterBeforeStart)
{
    // init session
    auto session = std::make_shared<VolumeTaskSession>();
    InitSessionSharedConfig(session);    
    InitSessionSharedContext(session);
    InitSessionBlockVolumeReader(session, nullptr);
    InitSessionBlockCopyWriter(session, nullptr);

    EXPECT_FALSE(session->readerTask->Start());
    EXPECT_FALSE(session->writerTask->Start());
}

TEST_F(VolumeBackupTest, VolumeBackTask_RunBackupSuccess)
{
    VolumeBackupConfig backupConfig;
    backupConfig.blockSize = DEFAULT_MOCK_SESSION_BLOCK_SIZE;
    backupConfig.copyType = CopyType::INCREMENT;
    backupConfig.hasherEnabled = true;
    backupConfig.enableCheckpoint = true;
    backupConfig.hasherNum = DEFAULT_MOCK_HASHER_NUM;
    backupConfig.sessionSize = DEFAULT_MOCK_SESSION_SIZE;
    backupConfig.volumePath = "/dev/dummyVolume";

    auto backupTaskMock = std::make_shared<VolumeBackupTaskMock>(backupConfig, 1LLU * ONE_GB); // 2 session

    EXPECT_CALL(*backupTaskMock, DataReaderReadMockReturn())
        .WillRepeatedly(Return(true));
    EXPECT_CALL(*backupTaskMock, DataWriterWriteMockReturn())
        .WillRepeatedly(Return(true));
    EXPECT_CALL(*backupTaskMock, SaveVolumeCopyMetaMockReturn())
        .WillRepeatedly(Return(true));
    EXPECT_CALL(*backupTaskMock, LoadSessionPreviousCopyChecksumMockReturn())
        .WillRepeatedly(Return(true));

    EXPECT_TRUE(backupTaskMock->Start());
    while (!backupTaskMock->IsTerminated()) {
        backupTaskMock->GetStatistics();
        std::this_thread::sleep_for(TASK_CHECK_SLEEP_INTERVAL);
    }
    EXPECT_EQ(backupTaskMock->GetStatus(), TaskStatus::SUCCEED);
    EXPECT_EQ(backupTaskMock->GetStatusString(), "SUCCEED");
}

TEST_F(VolumeBackupTest, VolumeBackTask_RunBackupThenAbort)
{
    VolumeBackupConfig backupConfig;
    backupConfig.blockSize = DEFAULT_MOCK_SESSION_BLOCK_SIZE;
    backupConfig.copyType = CopyType::INCREMENT;
    backupConfig.hasherEnabled = true;
    backupConfig.enableCheckpoint = false;
    backupConfig.hasherNum = DEFAULT_MOCK_HASHER_NUM;
    backupConfig.sessionSize = DEFAULT_MOCK_SESSION_SIZE;
    backupConfig.volumePath = "/dev/dummyVolume";

    auto backupTaskMock = std::make_shared<VolumeBackupTaskMock>(backupConfig, 1LLU * ONE_GB); // 2 session

    EXPECT_CALL(*backupTaskMock, DataReaderReadMockReturn())
        .WillRepeatedly(Return(true));
    EXPECT_CALL(*backupTaskMock, DataWriterWriteMockReturn())
        .WillRepeatedly(Return(true));
    EXPECT_CALL(*backupTaskMock, SaveVolumeCopyMetaMockReturn())
        .WillRepeatedly(Return(true));
    EXPECT_CALL(*backupTaskMock, LoadSessionPreviousCopyChecksumMockReturn())
        .WillRepeatedly(Return(true));

    EXPECT_TRUE(backupTaskMock->Start());
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    backupTaskMock->Abort();
    while (!backupTaskMock->IsTerminated()) {
        backupTaskMock->GetStatistics();
        std::this_thread::sleep_for(TASK_CHECK_SLEEP_INTERVAL);
    }
    EXPECT_EQ(backupTaskMock->GetStatus(), TaskStatus::ABORTED);
}

TEST_F(VolumeBackupTest, VolumeBackTask_DataReaderReadFail)
{
    VolumeBackupConfig backupConfig;
    backupConfig.blockSize = DEFAULT_MOCK_SESSION_BLOCK_SIZE;
    backupConfig.copyType = CopyType::FULL;
    backupConfig.hasherEnabled = true;
    backupConfig.hasherNum = DEFAULT_MOCK_HASHER_NUM;
    backupConfig.sessionSize = DEFAULT_MOCK_SESSION_SIZE;
    backupConfig.volumePath = "/dev/dummyVolumePath";

    auto backupTaskMock = std::make_shared<VolumeBackupTaskMock>(backupConfig, 1LLU * ONE_GB); // 2 session

    // mock DataReaderReadMockReturn() from to force return false
    EXPECT_CALL(*backupTaskMock, DataReaderReadMockReturn())
        .WillRepeatedly(Return(false));
    EXPECT_CALL(*backupTaskMock, DataWriterWriteMockReturn())
        .WillRepeatedly(Return(true));
    // mock SaveVolumeCopyMetaMockReturn() from to force return false, skip failure of saving copy meta json
    EXPECT_CALL(*backupTaskMock, SaveVolumeCopyMetaMockReturn())
        .WillRepeatedly(Return(true));

    EXPECT_TRUE(backupTaskMock->Start());
    while (!backupTaskMock->IsTerminated()) {
        backupTaskMock->GetStatistics();
        std::this_thread::sleep_for(TASK_CHECK_SLEEP_INTERVAL);
    }
    EXPECT_EQ(backupTaskMock->GetStatus(), TaskStatus::FAILED);
}

TEST_F(VolumeBackupTest, VolumeBackTask_DataWriterWriteFail)
{
    VolumeBackupConfig backupConfig;
    backupConfig.blockSize = DEFAULT_MOCK_SESSION_BLOCK_SIZE;
    backupConfig.copyType = CopyType::INCREMENT;
    backupConfig.hasherEnabled = true;
    backupConfig.hasherNum = DEFAULT_MOCK_HASHER_NUM;
    backupConfig.sessionSize = DEFAULT_MOCK_SESSION_SIZE;
    backupConfig.volumePath = "/dev/dummyVolumePath";

    auto backupTaskMock = std::make_shared<VolumeBackupTaskMock>(backupConfig, 1LLU * ONE_GB); // 2 session

    // mock DataReaderReadMockReturn() from to force return false
    EXPECT_CALL(*backupTaskMock, DataReaderReadMockReturn())
        .WillRepeatedly(Return(true));
    EXPECT_CALL(*backupTaskMock, DataWriterWriteMockReturn())
        .WillRepeatedly(Return(false));
    // mock SaveVolumeCopyMetaMockReturn() from to force return false, skip failure of saving copy meta json
    EXPECT_CALL(*backupTaskMock, SaveVolumeCopyMetaMockReturn())
        .WillRepeatedly(Return(true));
    EXPECT_CALL(*backupTaskMock, LoadSessionPreviousCopyChecksumMockReturn())
        .WillRepeatedly(Return(true));

    EXPECT_TRUE(backupTaskMock->Start());
    while (!backupTaskMock->IsTerminated()) {
        backupTaskMock->GetStatistics();
        std::this_thread::sleep_for(TASK_CHECK_SLEEP_INTERVAL);
    }
    EXPECT_EQ(backupTaskMock->GetStatus(), TaskStatus::FAILED);
}

TEST_F(VolumeBackupTest, BuildBackupTask_UnMockedTaskFailedDueToInvalidVolume)
{
    VolumeBackupConfig backupConfig {};
    backupConfig.blockSize = DEFAULT_MOCK_SESSION_BLOCK_SIZE;
    backupConfig.copyType = CopyType::FULL;
    backupConfig.hasherEnabled = true;
    backupConfig.hasherNum = DEFAULT_MOCK_HASHER_NUM;
    backupConfig.sessionSize = DEFAULT_MOCK_SESSION_SIZE;
    backupConfig.volumePath = "/dev/dummy/dummyVolumePath";
    auto backupTask = VolumeProtectTask::BuildBackupTask(backupConfig);
    EXPECT_TRUE(backupTask == nullptr);
}

TEST_F(VolumeBackupTest, VolumeBackTask_FailToSaveCopyMetaJson)
{
    VolumeBackupConfig backupConfig;
    backupConfig.blockSize = DEFAULT_MOCK_SESSION_BLOCK_SIZE;
    backupConfig.copyType = CopyType::FULL;
    backupConfig.hasherEnabled = true;
    backupConfig.hasherNum = DEFAULT_MOCK_HASHER_NUM;
    backupConfig.sessionSize = DEFAULT_MOCK_SESSION_SIZE;
    backupConfig.volumePath = "/dev/dummy";

    auto backupTaskMock = std::make_shared<VolumeBackupTaskMock>(backupConfig, 4LLU * ONE_GB);

    EXPECT_CALL(*backupTaskMock, SaveVolumeCopyMetaMockReturn())
        .WillRepeatedly(Return(false));
    // backupTaskMock will failed at saving copy meta json

    EXPECT_FALSE(backupTaskMock->Start());
}

// Test Restore From Here ...

static void InitSessionBlockCopyReader(
    std::shared_ptr<VolumeTaskSession> session,
    std::shared_ptr<rawio::RawDataReader> dataReader)
{
    VolumeBlockReaderParam readerParam {
        SourceType::COPYFILE,
        session->sharedConfig->volumePath,
        session->sharedConfig->sessionOffset,
        dataReader,
        session->sharedConfig,
        session->sharedContext
    };
    auto volumeBlockReader = std::make_shared<VolumeBlockReader>(readerParam);
    session->readerTask = volumeBlockReader;
}

static void InitSessionBlockVolumeWriter(
    std::shared_ptr<VolumeTaskSession> session,
    std::shared_ptr<rawio::RawDataWriter> dataWriter)
{
    VolumeBlockWriterParam writerParam {
        TargetType::VOLUME,
        session->sharedConfig->copyFilePath,
        session->sharedConfig,
        session->sharedContext,
        dataWriter
    };
    auto volumeBlockWriter = std::make_shared<VolumeBlockWriter>(writerParam);
    session->writerTask = volumeBlockWriter;
}

class VolumeRestoreTaskMock : public VolumeRestoreTask
{
public:
    VolumeRestoreTaskMock(const VolumeRestoreConfig& restoreConfig);
    bool ValidateRestoreTask(const VolumeCopyMeta& volumeCopyMeta) const;
    bool InitRestoreSessionTaskExecutor(std::shared_ptr<VolumeTaskSession> session) const;
    bool ReadVolumeCopyMeta(const std::string& copyMetaDirPath, VolumeCopyMeta& volumeCopyMeta);
    bool IsSessionRestarted(std::shared_ptr<VolumeTaskSession> session) const;

    MOCK_METHOD(bool, ReadVolumeCopyMetaMockReturn, (), (const));
    MOCK_METHOD(bool, DataReaderReadMockReturn, (), (const));
    MOCK_METHOD(bool, DataWriterWriteMockReturn, (), (const));
};

VolumeRestoreTaskMock::VolumeRestoreTaskMock(const VolumeRestoreConfig& restoreConfig)
  : VolumeRestoreTask(restoreConfig) {}

bool VolumeRestoreTaskMock::ReadVolumeCopyMeta(const std::string& copyMetaDirPath, VolumeCopyMeta& volumeCopyMeta)
{
    volumeCopyMeta.copyType = 0;
    volumeCopyMeta.volumeSize  = ONE_GB;
    volumeCopyMeta.blockSize = 4 * ONE_MB;
    volumeCopyMeta.copySlices = std::vector<std::pair<std::uint64_t, uint64_t>> {
        { 0, ONE_MB * 512 }, { ONE_MB * 512, ONE_MB * 512 }
    };
    return ReadVolumeCopyMetaMockReturn();
}

bool VolumeRestoreTaskMock::ValidateRestoreTask(const VolumeCopyMeta& volumeCopyMeta) const
{
    return true;
}

bool VolumeRestoreTaskMock::InitRestoreSessionTaskExecutor(std::shared_ptr<VolumeTaskSession> session) const
{
    // init mock
    auto dataReaderMock = std::make_shared<DataReaderMock>();
    EXPECT_CALL(*dataReaderMock, Read(_, _, _, _))
        .WillRepeatedly(Return(DataReaderReadMockReturn()));
    EXPECT_CALL(*dataReaderMock, Ok())
        .WillRepeatedly(Return(true));
    EXPECT_CALL(*dataReaderMock, Error())
        .WillRepeatedly(Return(static_cast<fsapi::ErrCodeType>(0)));

    auto dataWriterMock = std::make_shared<DataWriterMock>();
    EXPECT_CALL(*dataWriterMock, Write(_, _, _, _))
        .WillRepeatedly(Return(DataWriterWriteMockReturn()));
    EXPECT_CALL(*dataWriterMock, Ok())
        .WillRepeatedly(Return(true));
    EXPECT_CALL(*dataWriterMock, Error())
        .WillRepeatedly(Return(static_cast<fsapi::ErrCodeType>(0)));
    EXPECT_CALL(*dataWriterMock, Flush())
        .WillRepeatedly(Return(true));

    // init session
    InitSessionBlockCopyReader(session, std::dynamic_pointer_cast<rawio::RawDataReader>(dataReaderMock));
    InitSessionBlockVolumeWriter(session, std::dynamic_pointer_cast<rawio::RawDataWriter>(dataWriterMock));
    return true;
}

bool VolumeRestoreTaskMock::IsSessionRestarted(std::shared_ptr<VolumeTaskSession> session) const
{
    return true;
}

TEST_F(VolumeBackupTest, VolumeRestoreTask_RunRestoreSuccess)
{
    VolumeRestoreConfig restoreConfig;
    restoreConfig.copyDataDirPath = "/dummy/dummyData";
    restoreConfig.copyMetaDirPath = "/dummy/dummyMeta";
    restoreConfig.volumePath = "/dev/dummy/dummyVolume";
    restoreConfig.enableCheckpoint = true;

    auto restoreTaskMock = std::make_shared<VolumeRestoreTaskMock>(restoreConfig); // 2 session

    EXPECT_CALL(*restoreTaskMock, DataReaderReadMockReturn())
        .WillRepeatedly(Return(true));
    EXPECT_CALL(*restoreTaskMock, DataWriterWriteMockReturn())
        .WillRepeatedly(Return(true));
    EXPECT_CALL(*restoreTaskMock, ReadVolumeCopyMetaMockReturn())
        .WillRepeatedly(Return(true));

    EXPECT_TRUE(restoreTaskMock->Start());
    while (!restoreTaskMock->IsTerminated()) {
        restoreTaskMock->GetStatistics();
        std::this_thread::sleep_for(TASK_CHECK_SLEEP_INTERVAL);
    }
    EXPECT_EQ(restoreTaskMock->GetStatus(), TaskStatus::SUCCEED);
}

TEST_F(VolumeBackupTest, VolumeRestoreTask_RunRestoreThenAbort)
{
    VolumeRestoreConfig restoreConfig;
    restoreConfig.copyDataDirPath = "/dummy/dummyData";
    restoreConfig.copyMetaDirPath = "/dummy/dummyMeta";
    restoreConfig.volumePath = "/dev/dummy/dummyVolume";

    auto restoreTaskMock = std::make_shared<VolumeRestoreTaskMock>(restoreConfig); // 2 session

    EXPECT_CALL(*restoreTaskMock, DataReaderReadMockReturn())
        .WillRepeatedly(Return(true));
    EXPECT_CALL(*restoreTaskMock, DataWriterWriteMockReturn())
        .WillRepeatedly(Return(true));
    EXPECT_CALL(*restoreTaskMock, ReadVolumeCopyMetaMockReturn())
        .WillRepeatedly(Return(true));

    EXPECT_TRUE(restoreTaskMock->Start());
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    restoreTaskMock->Abort();
    while (!restoreTaskMock->IsTerminated()) {
        restoreTaskMock->GetStatistics();
        std::this_thread::sleep_for(TASK_CHECK_SLEEP_INTERVAL);
    }
    EXPECT_EQ(restoreTaskMock->GetStatus(), TaskStatus::ABORTED);
}

// // Test Basic Component From Here...
TEST_F(VolumeBackupTest, BuildBackupOrRestoreTask_FailForInvalidVolumePath)
{
    VolumeRestoreConfig restoreConfig;
    restoreConfig.copyDataDirPath = "/dummy/dummyData";
    restoreConfig.copyMetaDirPath = "/dummy/dummyMeta";
    restoreConfig.volumePath = "/dev/dummy/dummyVolume";
    EXPECT_TRUE(VolumeProtectTask::BuildRestoreTask(restoreConfig) == nullptr);

    VolumeBackupConfig backupConfig;
    backupConfig.blockSize = DEFAULT_MOCK_SESSION_BLOCK_SIZE;
    backupConfig.copyType = CopyType::INCREMENT;
    backupConfig.hasherEnabled = true;
    backupConfig.enableCheckpoint = false;
    backupConfig.hasherNum = DEFAULT_MOCK_HASHER_NUM;
    backupConfig.sessionSize = DEFAULT_MOCK_SESSION_SIZE;
    backupConfig.volumePath = "/dev/dummy/dummyVolume";
    EXPECT_TRUE(VolumeProtectTask::BuildBackupTask(backupConfig) == nullptr);
}

TEST_F(VolumeBackupTest, BuildComponentTask_FailForInvalidPath)
{
    std::shared_ptr<VolumeTaskSharedConfig> sharedConfig = 
        std::make_shared<VolumeTaskSharedConfig>();
    std::shared_ptr<VolumeTaskSharedContext> sharedContext =
        std::make_shared<VolumeTaskSharedContext>();
    sharedConfig->sessionSize = 512 * ONE_MB;
    sharedConfig->copyFilePath = "/copy/dummy/dummycopy";
    sharedConfig->volumePath = "/dev/dummy/dummyvolume";
    sharedConfig->blockSize = DEFAULT_BLOCK_SIZE;
    EXPECT_TRUE(VolumeBlockWriter::BuildCopyWriter(sharedConfig, sharedContext) == nullptr);
    EXPECT_TRUE(VolumeBlockWriter::BuildVolumeWriter(sharedConfig, sharedContext) == nullptr);
    EXPECT_TRUE(VolumeBlockReader::BuildCopyReader(sharedConfig, sharedContext) == nullptr);
    EXPECT_TRUE(VolumeBlockReader::BuildVolumeReader(sharedConfig, sharedContext) == nullptr);
}

TEST_F(VolumeBackupTest, BuildHasher_Success)
{
    auto session = std::make_shared<VolumeTaskSession>();
    InitSessionSharedConfig(session);
    InitSessionSharedContext(session);
    EXPECT_TRUE(VolumeBlockHasher::BuildHasher(
        session->sharedConfig,
        session->sharedContext,
        HasherForwardMode::DIRECT) != nullptr);
}

TEST_F(VolumeBackupTest, VolumeBlockHasher_HasherDisabled)
{
    uint32_t hasherNum = 0; // invalid hasher num
    uint32_t singleChecksumSize = 32LU; // SHA-256
    auto session = std::make_shared<VolumeTaskSession>();
    InitSessionSharedConfig(session);
    InitSessionSharedContext(session);
    session->sharedConfig->hasherEnabled = false;

    VolumeBlockHasherParam hasherParam {
        session->sharedConfig, session->sharedContext, hasherNum, HasherForwardMode::DIFF, singleChecksumSize
    };
    auto volumeBlockHasher = std::make_shared<VolumeBlockHasher>(hasherParam);
    EXPECT_TRUE(volumeBlockHasher->Start());
}