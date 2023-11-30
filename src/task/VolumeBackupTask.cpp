/**
 * @copyright Copyright 2023 XUranus. All rights reserved.
 * @license This project is released under the Apache License.
 * @author XUranus(2257238649wdx@gmail.com)
 */

#include "Logger.h"
#include "VolumeProtector.h"
#include "VolumeProtectTaskContext.h"
#include "VolumeUtils.h"
#include "VolumeBlockReader.h"
#include "VolumeBlockHasher.h"
#include "VolumeBlockWriter.h"
#include "BlockingQueue.h"
#include "native/FileSystemAPI.h"
#include "VolumeBackupTask.h"

using namespace volumeprotect;
using namespace volumeprotect::common;
using namespace volumeprotect::task;

namespace {
    constexpr auto TASK_CHECK_SLEEP_INTERVAL = std::chrono::seconds(1);
}

VolumeBackupTask::VolumeBackupTask(const VolumeBackupConfig& backupConfig, uint64_t volumeSize)
    : m_volumeSize(volumeSize),
    m_backupConfig(std::make_shared<VolumeBackupConfig>(backupConfig)),
    m_resourceManager(TaskResourceManager::BuildBackupTaskResourceManager(BackupTaskResourceManagerParams {
        backupConfig.copyFormat,
        backupConfig.backupType,
        backupConfig.outputCopyDataDirPath,
        backupConfig.copyName,
        volumeSize,
        backupConfig.sessionSize
    }))
{}

VolumeBackupTask::~VolumeBackupTask()
{
    DBGLOG("destroy volume backup task, wait main thread to join");
    if (m_thread.joinable()) {
        m_thread.join();
    }
    DBGLOG("reset backup resource manager");
    m_resourceManager.reset();
    DBGLOG("volume backup task destroyed");
}

bool VolumeBackupTask::Start()
{
    AssertTaskNotStarted();
    if (!Prepare()) {
        ERRLOG("prepare task failed");
        m_status = TaskStatus::FAILED;
        return false;
    }
    m_status = TaskStatus::RUNNING;
    m_thread = std::thread(&VolumeBackupTask::ThreadFunc, this);
    return true;
}

TaskStatistics VolumeBackupTask::GetStatistics() const
{
    std::lock_guard<std::mutex> lock(m_statisticMutex);
    return m_completedSessionStatistics + m_currentSessionStatistics;
}

bool VolumeBackupTask::IsIncrementBackup() const
{
    return m_backupConfig->backupType == BackupType::FOREVER_INC;
}

// split session and save volume meta
bool VolumeBackupTask::Prepare()
{
    std::string volumePath = m_backupConfig->volumePath;
    // 1. fill volume meta info
    VolumeCopyMeta volumeCopyMeta {};
    volumeCopyMeta.copyName = m_backupConfig->copyName;
    volumeCopyMeta.backupType = static_cast<int>(m_backupConfig->backupType);
    volumeCopyMeta.copyFormat = static_cast<int>(m_backupConfig->copyFormat);
    volumeCopyMeta.volumeSize = m_volumeSize;
    volumeCopyMeta.blockSize = DEFAULT_BLOCK_SIZE;
    volumeCopyMeta.volumePath = volumePath;

    // prepare backup resource
    if (!m_resourceManager->PrepareCopyResource()) {
        ERRLOG("failed to prepare copy resource for backup task");
        return false;
    }

    // validate increment backup
    if (IsIncrementBackup() && !ValidateIncrementBackup()) {
        ERRLOG("failed to validate increment backup");
        return false;
    }

    // 2. split session
    int sessionIndex = 0;
    for (uint64_t sessionOffset = 0; sessionOffset < m_volumeSize;) {
        uint64_t sessionSize = m_backupConfig->sessionSize;
        if (sessionOffset + m_backupConfig->sessionSize >= m_volumeSize) {
            sessionSize = m_volumeSize - sessionOffset;
        }
        volumeCopyMeta.segments.emplace_back(CopySegment {
            common::GetFileName(common::GetCopyDataFilePath(
                m_backupConfig->outputCopyDataDirPath,
                m_backupConfig->copyName,
                m_backupConfig->copyFormat,
                sessionIndex)),
            common::GetFileName(common::GetChecksumBinPath(
                m_backupConfig->outputCopyMetaDirPath, m_backupConfig->copyName, sessionIndex)),
            sessionIndex,
            sessionOffset,
            sessionSize
        });
        m_sessionQueue.push(NewVolumeTaskSession(sessionOffset, sessionSize, sessionIndex));
        sessionOffset += sessionSize;
        ++sessionIndex;
    }

    if (!SaveVolumeCopyMeta(m_backupConfig->outputCopyMetaDirPath, m_backupConfig->copyName, volumeCopyMeta)) {
        ERRLOG("failed to write copy meta to dir: %s", m_backupConfig->outputCopyMetaDirPath.c_str());
        return false;
    }
    return true;
}

VolumeTaskSession VolumeBackupTask::NewVolumeTaskSession(
    uint64_t sessionOffset, uint64_t sessionSize, int sessionIndex) const
{
    std::string lastestChecksumBinPath = common::GetChecksumBinPath(
        m_backupConfig->outputCopyMetaDirPath, m_backupConfig->copyName, sessionIndex);
    std::string copyFilePath = common::GetCopyDataFilePath(
        m_backupConfig->outputCopyDataDirPath, m_backupConfig->copyName, m_backupConfig->copyFormat, sessionIndex);
    std::string writerBitmapPath = common::GetWriterBitmapFilePath(
        m_backupConfig->outputCopyMetaDirPath, m_backupConfig->copyName, sessionIndex);
    // for increment backup, set previous checksum bin path
    std::string prevChecksumBinPath = "";
    if (IsIncrementBackup()) {
        prevChecksumBinPath = common::GetChecksumBinPath(
            m_backupConfig->prevCopyMetaDirPath, m_backupConfig->copyName, sessionIndex);
    }

    VolumeTaskSession session {};
    session.sharedConfig = std::make_shared<VolumeTaskSharedConfig>();
    session.sharedConfig->copyFormat = m_backupConfig->copyFormat;
    session.sharedConfig->volumePath = m_backupConfig->volumePath;
    session.sharedConfig->hasherEnabled = m_backupConfig->hasherEnabled;
    session.sharedConfig->hasherWorkerNum = m_backupConfig->hasherNum;
    session.sharedConfig->blockSize = m_backupConfig->blockSize;
    session.sharedConfig->sessionOffset = sessionOffset;
    session.sharedConfig->sessionSize = sessionSize;
    session.sharedConfig->lastestChecksumBinPath = lastestChecksumBinPath;
    session.sharedConfig->prevChecksumBinPath = prevChecksumBinPath;
    session.sharedConfig->copyFilePath = copyFilePath;
    session.sharedConfig->checkpointFilePath = writerBitmapPath;
    session.sharedConfig->checkpointEnabled = m_backupConfig->enableCheckpoint;
    session.sharedConfig->skipEmptyBlock = m_backupConfig->skipEmptyBlock;
    return session;
}

bool VolumeBackupTask::InitBackupSessionTaskExecutor(std::shared_ptr<VolumeTaskSession> session) const
{
    session->readerTask = VolumeBlockReader::BuildVolumeReader(session->sharedConfig, session->sharedContext);
    if (session->readerTask == nullptr) {
        ERRLOG("backup session failed to init reader");
        return false;
    }

    // 4. check and init hasher
    auto hasherMode = IsIncrementBackup() ? HasherForwardMode::DIFF : HasherForwardMode::DIRECT;
    session->hasherTask  = VolumeBlockHasher::BuildHasher(session->sharedConfig, session->sharedContext, hasherMode);
    if (session->hasherTask  == nullptr) {
        ERRLOG("backup session failed to init hasher");
        return false;
    }

    // 5. check and init writer
    session->writerTask  = VolumeBlockWriter::BuildCopyWriter(session->sharedConfig, session->sharedContext);
    if (session->writerTask  == nullptr) {
        ERRLOG("backup session failed to init writer");
        return false;
    }
    return true;
}

bool VolumeBackupTask::InitBackupSessionContext(std::shared_ptr<VolumeTaskSession> session) const
{
    DBGLOG("init backup session context, offset %llu, size %llu",
            session->sharedConfig->sessionOffset, session->sharedConfig->sessionSize);
    // 1. init basic backup container
    session->sharedContext = std::make_shared<VolumeTaskSharedContext>();
    session->sharedContext->counter = std::make_shared<SessionCounter>();
    session->sharedContext->allocator = std::make_shared<VolumeBlockAllocator>(
        session->sharedConfig->blockSize, DEFAULT_ALLOCATOR_BLOCK_NUM);
    session->sharedContext->hashingQueue = std::make_shared<BlockingQueue<VolumeConsumeBlock>>(DEFAULT_QUEUE_SIZE);
    session->sharedContext->writeQueue = std::make_shared<BlockingQueue<VolumeConsumeBlock>>(DEFAULT_QUEUE_SIZE);
    if (!InitHashingContext(session)) {
        ERRLOG("failed to init hashing context");
        return false;
    }
    InitSessionBitmap(session);
    // 2. restore checkpoint if restarted
    RestoreSessionCheckpoint(session);
    // 3. check and init task executor
    return InitBackupSessionTaskExecutor(session);
}

bool VolumeBackupTask::StartBackupSession(std::shared_ptr<VolumeTaskSession> session) const
{
    DBGLOG("start backup session");
    if (session->readerTask == nullptr || session->hasherTask  == nullptr || session->writerTask  == nullptr) {
        ERRLOG("backup session member nullptr! reader: %p hasher: %p writer: %p ",
            session->readerTask.get(), session->hasherTask.get(), session->writerTask.get());
        return false;
    }
    DBGLOG("start backup session reader");
    if (!session->readerTask->Start()) {
        ERRLOG("backup session reader start failed");
        return false;
    }
    DBGLOG("start backup session hasher, hasher enabled: %u", session->sharedConfig->hasherEnabled);
    if (!session->hasherTask->Start()) {
        ERRLOG("backup session hasher start failed");
        return false;
    }
    DBGLOG("start backup session writer");
    if (!session->writerTask->Start()) {
        ERRLOG("backup session writer start failed");
        return false;
    }
    return true;
}

void VolumeBackupTask::ThreadFunc()
{
    DBGLOG("start task main thread");
    while (!m_sessionQueue.empty()) {
        if (m_abort) {
            m_status = TaskStatus::ABORTED;
            return;
        }
        // pop a session from session queue to init a new session
        std::shared_ptr<VolumeTaskSession> session = std::make_shared<VolumeTaskSession>(m_sessionQueue.front());
        m_sessionQueue.pop();
        if (!InitBackupSessionContext(session)) {
            m_status = TaskStatus::FAILED;
            return;
        }
        RestoreSessionCheckpoint(session);
        if (!StartBackupSession(session)) {
            session->Abort();
            m_status = TaskStatus::FAILED;
            return;
        }
        // block the thread
        auto counter = session->sharedContext->counter;
        while (true) {
            if (m_abort) {
                session->Abort();
                m_status = TaskStatus::ABORTED;
                return;
            }
            if (session->IsFailed()) {
                ERRLOG("backup session failed");
                m_errorCode = session->GetErrorCode();
                m_status = TaskStatus::FAILED;
                return;
            }
            if (session->IsTerminated())  {
                break;
            }
            UpdateRunningSessionStatistics(session);
            RefreshSessionCheckpoint(session);
            std::this_thread::sleep_for(TASK_CHECK_SLEEP_INTERVAL);
        }
        DBGLOG("session complete successfully");
        FlushSessionLatestHashingTable(session);
        FlushSessionWriter(session);
        FlushSessionBitmap(session);
        UpdateCompletedSessionStatistics(session);
    }
    m_status = TaskStatus::SUCCEED;
    return;
}

bool VolumeBackupTask::InitHashingContext(std::shared_ptr<VolumeTaskSession> session) const
{
    // 1. allocate checksum table
    auto sharedConfig = session->sharedConfig;
    auto sharedContext = session->sharedContext;
    uint64_t lastestChecksumTableSize = session->TotalBlocks() * SHA256_CHECKSUM_SIZE;
    uint64_t prevChecksumTableSize = lastestChecksumTableSize;
    try {
        sharedContext->hashingContext = IsIncrementBackup() ?
            std::make_shared<BlockHashingContext>(prevChecksumTableSize, lastestChecksumTableSize)
            : std::make_shared<BlockHashingContext>(lastestChecksumTableSize);
    } catch (const std::exception& e) {
        ERRLOG("failed to malloc BlockHashingContext, length: %llu, message: %s", lastestChecksumTableSize, e.what());
        return false;
    }
    // 2. load previous checksum table from file if increment backup
    if (IsIncrementBackup() && !LoadSessionPreviousCopyChecksum(session)) {
        return false;
    }
    return true;
}

bool VolumeBackupTask::LoadSessionPreviousCopyChecksum(std::shared_ptr<VolumeTaskSession> session) const
{
    auto sharedConfig = session->sharedConfig;
    uint32_t blockCount = static_cast<uint32_t>(sharedConfig->sessionSize / sharedConfig->blockSize);
    uint64_t lastestChecksumTableSize = blockCount * SHA256_CHECKSUM_SIZE;
    uint64_t prevChecksumTableSize = lastestChecksumTableSize;
    uint8_t* buffer = fsapi::ReadBinaryBuffer(session->sharedConfig->prevChecksumBinPath, prevChecksumTableSize);
    if (buffer == nullptr) {
        ERRLOG("failed to read previous checksum from %s", session->sharedConfig->prevChecksumBinPath.c_str());
        return false;
    }
    memcpy(session->sharedContext->hashingContext->previousTable, buffer, sizeof(uint8_t) * prevChecksumTableSize);
    delete[] buffer;
    buffer = nullptr;
    return true;
}

bool VolumeBackupTask::SaveVolumeCopyMeta(
    const std::string& copyMetaDirPath, const std::string& copyName, const VolumeCopyMeta& volumeCopyMeta) const
{
    return common::WriteVolumeCopyMeta(copyMetaDirPath, copyName, volumeCopyMeta);
}

bool VolumeBackupTask::ValidateIncrementBackup() const
{
    if (!fsapi::IsDirectoryExists(m_backupConfig->outputCopyDataDirPath)
        || !fsapi::IsDirectoryExists(m_backupConfig->prevCopyMetaDirPath)) {
        ERRLOG("data directory %s or previous meta directory %s not exists!",
            m_backupConfig->outputCopyDataDirPath.c_str(), m_backupConfig->prevCopyMetaDirPath.c_str());
        return false;
    }
    VolumeCopyMeta volumeCopyMeta {};
    if (!common::ReadVolumeCopyMeta(m_backupConfig->prevCopyMetaDirPath, m_backupConfig->copyName, volumeCopyMeta)) {
        ERRLOG("failed to read previous copy meta in %s", m_backupConfig->prevCopyMetaDirPath.c_str());
        return false;
    }
    if (m_backupConfig->blockSize != volumeCopyMeta.blockSize) {
        ERRLOG("increment backup block size mismatach! (previous: %llu latest: %llu)",
            m_backupConfig->blockSize, volumeCopyMeta.blockSize);
        return false;
    }
    return true;
}