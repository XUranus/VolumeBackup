#include <chrono>
#include <exception>
#include <memory>
#include <queue>
#include <string>
#include <sys/types.h>
#include <thread>
#include <vector>

#include "Logger.h"
#include "VolumeProtector.h"
#include "VolumeProtectTaskContext.h"
#include "VolumeUtils.h"
#include "VolumeBlockReader.h"
#include "VolumeBlockHasher.h"
#include "VolumeBlockWriter.h"
#include "BlockingQueue.h"
#include "VolumeBackupTask.h"

using namespace volumeprotect;
using namespace volumeprotect::util;

namespace {
    constexpr auto DEFAULT_ALLOCATOR_BLOCK_NUM = 32;
    constexpr auto DEFAULT_QUEUE_SIZE = 32;
}

VolumeBackupTask::VolumeBackupTask(const VolumeBackupConfig& backupConfig, uint64_t volumeSize)
 : m_backupConfig(std::make_shared<VolumeBackupConfig>(backupConfig)),
   m_volumeSize(volumeSize)
{}

VolumeBackupTask::~VolumeBackupTask()
{
    DBGLOG("destroy VolumeBackupTask");
    if (m_thread.joinable()) {
        m_thread.join();
    }
}

bool VolumeBackupTask::Start()
{
    if (m_status != TaskStatus::INIT) {
        return false;
    }
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
    return m_backupConfig->copyType == CopyType::INCREMENT;
}

// split session and save volume meta
bool VolumeBackupTask::Prepare()
{
    std::string volumePath = m_backupConfig->volumePath;
    // 1. fill volume meta info
    VolumeCopyMeta volumeCopyMeta {};
    volumeCopyMeta.copyType = static_cast<int>(m_backupConfig->copyType);
    volumeCopyMeta.volumeSize = m_volumeSize;
    volumeCopyMeta.blockSize = DEFAULT_BLOCK_SIZE;

    if (IsIncrementBackup()) {
        // TODO:: validate increment backup meta
    }

    // 2. split session
    for (uint64_t sessionOffset = 0; sessionOffset < m_volumeSize;) {
        uint64_t sessionSize = m_backupConfig->sessionSize;
        if (sessionOffset + m_backupConfig->sessionSize >= m_volumeSize) {
            sessionSize = m_volumeSize - sessionOffset;
        }
        std::string lastestChecksumBinPath = util::GetChecksumBinPath(
            m_backupConfig->outputCopyMetaDirPath, sessionOffset, sessionSize);
        std::string copyFilePath = util::GetCopyFilePath(
            m_backupConfig->outputCopyDataDirPath, m_backupConfig->copyType, sessionOffset, sessionSize);
        // for increment backup
        std::string prevChecksumBinPath = "";
        if (IsIncrementBackup()) {
            prevChecksumBinPath = util::GetChecksumBinPath(m_backupConfig->prevCopyMetaDirPath, sessionOffset, sessionSize);
        }

        VolumeTaskSession session {};
        session.sharedConfig = std::make_shared<VolumeTaskSharedConfig>();
        session.sharedConfig->volumePath = m_backupConfig->volumePath;
        session.sharedConfig->hasherEnabled = true;
        session.sharedConfig->blockSize = m_backupConfig->blockSize;
        session.sharedConfig->sessionOffset = sessionOffset;
        session.sharedConfig->sessionSize = sessionSize;
        session.sharedConfig->lastestChecksumBinPath = lastestChecksumBinPath;
        session.sharedConfig->prevChecksumBinPath = prevChecksumBinPath;
        session.sharedConfig->copyFilePath = copyFilePath;

        volumeCopyMeta.copySlices.emplace_back(sessionOffset, sessionSize);
        m_sessionQueue.push(session);
        sessionOffset += sessionSize;
    }

    if (!SaveVolumeCopyMeta(m_backupConfig->outputCopyMetaDirPath, volumeCopyMeta)) {
        ERRLOG("failed to write copy meta to dir: %s", m_backupConfig->outputCopyMetaDirPath.c_str());
        return false;
    }
    return true;
}

bool VolumeBackupTask::InitBackupSessionContext(std::shared_ptr<VolumeTaskSession> session) const
{
    DBGLOG("init backup session context");
    // 1. init basic backup container
    session->sharedContext = std::make_shared<VolumeTaskSharedContext>();
    session->sharedContext->counter = std::make_shared<SessionCounter>();
    session->sharedContext->allocator = std::make_shared<VolumeBlockAllocator>(session->sharedConfig->blockSize, DEFAULT_ALLOCATOR_BLOCK_NUM);
    session->sharedContext->hashingQueue = std::make_shared<BlockingQueue<VolumeConsumeBlock>>(DEFAULT_QUEUE_SIZE);
    session->sharedContext->writeQueue = std::make_shared<BlockingQueue<VolumeConsumeBlock>>(DEFAULT_QUEUE_SIZE);

    // 2. check and init reader
    session->readerTask = VolumeBlockReader::BuildVolumeReader(
        session->sharedConfig,
        session->sharedContext
    );
    if (session->readerTask == nullptr) {
        ERRLOG("backup session failed to init reader");
        return false;
    }

    // 3. check and init hasher
    if (IsIncrementBackup()) {
        session->hasherTask  = VolumeBlockHasher::BuildDiffHasher(session->sharedConfig, session->sharedContext);
    } else {
        session->hasherTask  = VolumeBlockHasher::BuildDirectHasher(session->sharedConfig, session->sharedContext);
    }
    if (session->hasherTask  == nullptr) {
        ERRLOG("backup session failed to init hasher");
        return false;
    }

    // 4. check and init writer
    session->writerTask  = VolumeBlockWriter::BuildCopyWriter(session->sharedConfig, session->sharedContext);
    if (session->writerTask  == nullptr) {
        ERRLOG("backup session failed to init writer");
        return false;
    }
    return true;
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
    DBGLOG("start backup session hasher");
    if (!session->hasherTask->Start() ) {
        ERRLOG("backup session hasher start failed");
        return false;
    }
    DBGLOG("start backup session writer");
    if (!session->writerTask->Start() ) {
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

        if (!InitBackupSessionContext(session) || !StartBackupSession(session)) {
            m_status = TaskStatus::FAILED;
            return;
        }

        // block the thread
        while (true) {
            if (m_abort) {
                session->Abort();
                m_status = TaskStatus::ABORTED;
                return;
            }
            if (session->IsFailed()) {
                ERRLOG("session failed");
                m_status = TaskStatus::FAILED;
                return;
            }
            if (session->IsTerminated())  {
                break;
            }
            DBGLOG("updateStatistics: bytesToReaded: %llu, bytesRead: %llu, blocksToHash: %llu, blocksHashed: %llu, bytesToWrite: %llu, bytesWritten: %llu",
                session->sharedContext->counter->bytesToRead.load(), session->sharedContext->counter->bytesRead.load(),
                session->sharedContext->counter->blocksToHash.load(), session->sharedContext->counter->blocksHashed.load(),
                session->sharedContext->counter->bytesToWrite.load(), session->sharedContext->counter->bytesWritten.load());
            UpdateRunningSessionStatistics(session);
            std::this_thread::sleep_for(std::chrono::seconds(5));
        }
        DBGLOG("session complete successfully");
        UpdateCompletedSessionStatistics(session);
    }

    m_status = TaskStatus::SUCCEED;
    return;
}

void VolumeBackupTask::UpdateRunningSessionStatistics(std::shared_ptr<VolumeTaskSession> session)
{
    std::lock_guard<std::mutex> lock(m_statisticMutex);
    m_currentSessionStatistics.bytesToRead = session->sharedContext->counter->bytesToRead;
    m_currentSessionStatistics.bytesRead = session->sharedContext->counter->bytesRead;
    m_currentSessionStatistics.blocksToHash = session->sharedContext->counter->blocksToHash;
    m_currentSessionStatistics.blocksHashed = session->sharedContext->counter->blocksHashed;
    m_currentSessionStatistics.bytesToWrite = session->sharedContext->counter->bytesToWrite;
    m_currentSessionStatistics.bytesWritten = session->sharedContext->counter->bytesWritten;
}

void VolumeBackupTask::UpdateCompletedSessionStatistics(std::shared_ptr<VolumeTaskSession> session)
{
    std::lock_guard<std::mutex> lock(m_statisticMutex);
    m_completedSessionStatistics.bytesToRead += session->sharedContext->counter->bytesToRead;
    m_completedSessionStatistics.bytesRead += session->sharedContext->counter->bytesRead;
    m_completedSessionStatistics.blocksToHash += session->sharedContext->counter->blocksToHash;
    m_completedSessionStatistics.blocksHashed += session->sharedContext->counter->blocksHashed;
    m_completedSessionStatistics.bytesToWrite += session->sharedContext->counter->bytesToWrite;
    m_completedSessionStatistics.bytesWritten += session->sharedContext->counter->bytesWritten;
    memset(&m_currentSessionStatistics, 0, sizeof(TaskStatistics));
}

bool VolumeBackupTask::SaveVolumeCopyMeta(const std::string& copyMetaDirPath, const VolumeCopyMeta& volumeCopyMeta)
{
    return util::WriteVolumeCopyMeta(copyMetaDirPath, volumeCopyMeta);
}