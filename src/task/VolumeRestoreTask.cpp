/**
 * @copyright Copyright 2023-2024 XUranus. All rights reserved.
 * @license This project is released under the Apache License.
 * @author XUranus(2257238649wdx@gmail.com)
 */

#include "Logger.h"
#include "VolumeProtector.h"
#include "VolumeProtectTaskContext.h"
#include "VolumeUtils.h"
#include "VolumeBlockReader.h"
#include "VolumeBlockWriter.h"
#include "BlockingQueue.h"
#include "VolumeRestoreTask.h"
#include "native/FileSystemAPI.h"

using namespace volumeprotect;
using namespace volumeprotect::task;
using namespace volumeprotect::common;

namespace {
    constexpr auto TASK_CHECK_SLEEP_INTERVAL = std::chrono::seconds(1);
}

static std::vector<std::string> GetCopyFilesFromCopyMeta(const VolumeCopyMeta& volumeCopyMeta)
{
    std::vector<std::string> files;
    for (const auto& segment : volumeCopyMeta.segments) {
        files.push_back(segment.copyDataFile);
    }
    return files;
}

VolumeRestoreTask::VolumeRestoreTask(const VolumeRestoreConfig& restoreConfig, const VolumeCopyMeta& volumeCopyMeta)
    : m_restoreConfig(std::make_shared<VolumeRestoreConfig>(restoreConfig)),
    m_volumeCopyMeta(std::make_shared<VolumeCopyMeta>(volumeCopyMeta)),
    m_resourceManager(TaskResourceManager::BuildRestoreTaskResourceManager(RestoreTaskResourceManagerParams {
        static_cast<CopyFormat>(volumeCopyMeta.copyFormat),
        restoreConfig.copyDataDirPath,
        volumeCopyMeta.copyName,
        GetCopyFilesFromCopyMeta(volumeCopyMeta)
    }))
{}

VolumeRestoreTask::~VolumeRestoreTask()
{
    DBGLOG("destroy volume restore task, wait main thread to join");
    if (m_thread.joinable()) {
        m_thread.join();
    }
    DBGLOG("reset restore resource manager");
    m_resourceManager.reset();
    DBGLOG("volume restore task destroyed");
}

bool VolumeRestoreTask::Start()
{
    AssertTaskNotStarted();
    if (!Prepare()) {
        ERRLOG("prepare task failed");
        m_status = TaskStatus::FAILED;
        return false;
    }
    m_status = TaskStatus::RUNNING;
    m_thread = std::thread(&VolumeRestoreTask::ThreadFunc, this);
    return true;
}

TaskStatistics VolumeRestoreTask::GetStatistics() const
{
    std::lock_guard<std::mutex> lock(m_statisticMutex);
    return m_completedSessionStatistics + m_currentSessionStatistics;
}

// split session and write back
bool VolumeRestoreTask::Prepare()
{
    std::string volumePath = m_restoreConfig->volumePath;

    // 2. prepare restore resource
    if (!m_resourceManager->PrepareCopyResource()) {
        ERRLOG("failed to prepare copy resource for restore task");
        return false;
    }

    // 4. split session
    uint64_t volumeSize = m_volumeCopyMeta->volumeSize;
    BackupType backupType = static_cast<BackupType>(m_volumeCopyMeta->backupType);
    CopyFormat copyFormat = static_cast<CopyFormat>(m_volumeCopyMeta->copyFormat);
    for (const CopySegment& segment: m_volumeCopyMeta->segments) {
        uint64_t sessionOffset = segment.offset;
        uint64_t sessionSize = segment.length;
        int sessionIndex = segment.index;
        INFOLOG("Size = %llu sessionOffset %d sessionSize %d", volumeSize, sessionOffset, sessionSize);
        std::string copyFilePath = common::GetCopyDataFilePath(
            m_restoreConfig->copyDataDirPath, m_volumeCopyMeta->copyName, copyFormat, sessionIndex);
        std::string writerBitmapPath = common::GetWriterBitmapFilePath(
            m_restoreConfig->checkpointDirPath, m_volumeCopyMeta->copyName, sessionIndex);
        VolumeTaskSession session {};
        session.sharedConfig = std::make_shared<VolumeTaskSharedConfig>();
        session.sharedConfig->copyFormat = static_cast<CopyFormat>(m_volumeCopyMeta->copyFormat);
        session.sharedConfig->volumePath = volumePath;
        session.sharedConfig->hasherEnabled = false;
        session.sharedConfig->blockSize = m_volumeCopyMeta->blockSize;
        session.sharedConfig->sessionOffset = sessionOffset;
        session.sharedConfig->sessionSize = sessionSize;
        session.sharedConfig->copyFilePath = copyFilePath;
        session.sharedConfig->checkpointFilePath = writerBitmapPath;
        session.sharedConfig->checkpointEnabled = m_restoreConfig->enableCheckpoint;
        session.sharedConfig->skipEmptyBlock = false;
        m_checkpointFiles.emplace_back(writerBitmapPath);
        m_sessionQueue.push(session);
    }

    return true;
}

bool VolumeRestoreTask::InitRestoreSessionTaskExecutor(std::shared_ptr<VolumeTaskSession> session) const
{
    session->readerTask = VolumeBlockReader::BuildCopyReader(
        session->sharedConfig,
        session->sharedContext);
    if (session->readerTask == nullptr) {
        ERRLOG("restore session failed to init reader task");
        return false;
    }
    // 4. check and init writer
    session->writerTask = VolumeBlockWriter::BuildVolumeWriter(
        session->sharedConfig,
        session->sharedContext);
    if (session->writerTask == nullptr) {
        ERRLOG("restore session failed to init writer task");
        return false;
    }
    return true;
}

bool VolumeRestoreTask::InitRestoreSessionContext(std::shared_ptr<VolumeTaskSession> session) const
{
    DBGLOG("init restore session context");
    // 1. init basic restore container
    session->sharedContext = std::make_shared<VolumeTaskSharedContext>();
    session->sharedContext->counter = std::make_shared<SessionCounter>();
    session->sharedContext->allocator = std::make_shared<VolumeBlockAllocator>(
        session->sharedConfig->blockSize,
        DEFAULT_ALLOCATOR_BLOCK_NUM);
    session->sharedContext->writeQueue = std::make_shared<BlockingQueue<VolumeConsumeBlock>>(DEFAULT_QUEUE_SIZE);
    InitSessionBitmap(session);
    // 2. restore checkpoint if restarted
    RestoreSessionCheckpoint(session);
    // 3. check and init task executor
    return InitRestoreSessionTaskExecutor(session);
}

bool VolumeRestoreTask::StartRestoreSession(std::shared_ptr<VolumeTaskSession> session) const
{
    DBGLOG("start restore session");
    if (session->readerTask == nullptr || session->writerTask == nullptr) {
        ERRLOG("restore session member nullptr! readerTask: %p writerTask: %p ",
            session->readerTask.get(), session->writerTask.get());
        return false;
    }
    DBGLOG("start restore session reader");
    if (!session->readerTask->Start()) {
        ERRLOG("restore session readerTask start failed");
        return false;
    }
    DBGLOG("start restore session writer");
    if (!session->writerTask->Start()) {
        ERRLOG("restore session writerTask start failed");
        return false;
    }
    return true;
}

bool VolumeRestoreTask::WaitSessionTerminate(std::shared_ptr<VolumeTaskSession> session)
{
    // block the thread
    while (true) {
        if (m_abort) {
            session->Abort();
            m_status = TaskStatus::ABORTED;
            return false;
        }
        if (session->IsFailed()) {
            ERRLOG("session failed");
            m_status = TaskStatus::FAILED;
            m_errorCode = session->GetErrorCode();
            return false;
        }
        if (session->IsTerminated())  {
            break;
        }
        UpdateRunningSessionStatistics(session);
        RefreshSessionCheckpoint(session);
        std::this_thread::sleep_for(TASK_CHECK_SLEEP_INTERVAL);
    }
    DBGLOG("restore session complete successfully");
    FlushSessionWriter(session);
    FlushSessionBitmap(session);
    UpdateCompletedSessionStatistics(session);
    return true;
}

void VolumeRestoreTask::ThreadFunc()
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
        if (!InitRestoreSessionContext(session)) {
            m_status = TaskStatus::FAILED;
            return;
        }
        RestoreSessionCheckpoint(session);
        if (!StartRestoreSession(session)) {
            session->Abort();
            m_status = TaskStatus::FAILED;
            return;
        }
        if (!WaitSessionTerminate(session)) {
            // fail and exit
            return;
        }
    }
    ClearAllCheckpoints();
    m_status = TaskStatus::SUCCEED;
    return;
}

void VolumeRestoreTask::ClearAllCheckpoints() const
{
    if (!m_restoreConfig->enableCheckpoint || !m_restoreConfig->clearCheckpointsOnSucceed) {
        return;
    }
    INFOLOG("clear all checkpoints file for this restore task, copyName : %s", m_volumeCopyMeta->copyName.c_str());
    for (const std::string& checkpointFile : m_checkpointFiles) {
        INFOLOG("remove checkpoint file %s", checkpointFile.c_str());
        fsapi::RemoveFile(checkpointFile);
    }
}