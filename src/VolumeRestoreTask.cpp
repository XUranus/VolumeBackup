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
#include "VolumeRestoreTask.h"

using namespace volumeprotect;
using namespace volumeprotect::util;

namespace {
    constexpr auto DEFAULT_ALLOCATOR_BLOCK_NUM = 32;
    constexpr auto DEFAULT_QUEUE_SIZE = 32;
}

VolumeRestoreTask::VolumeRestoreTask(const VolumeRestoreConfig& restoreConfig, uint64_t volumeSize)
 : m_restoreConfig(std::make_shared<VolumeRestoreConfig>(restoreConfig)),
   m_volumeSize(volumeSize)
{}

VolumeRestoreTask::~VolumeRestoreTask()
{
    DBGLOG("destroy VolumeRestoreTask");
    if (m_thread.joinable()) {
        m_thread.join();
    }
}

bool VolumeRestoreTask::Start()
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
    m_thread = std::thread(&VolumeRestoreTask::ThreadFunc, this);
    return true;
}

TaskStatistics VolumeRestoreTask::GetStatistics() const
{
    return m_completedSessionStatistics + m_currentSessionStatistics;
}

bool VolumeRestoreTask::IsIncrementCopy() const
{
    return m_incrementCopy;
}

// split session and write back
bool VolumeRestoreTask::Prepare()
{
    std::string blockDevicePath = m_restoreConfig->blockDevicePath;
    // 1. retrive volume partition info
    // VolumePartitionTableEntry partitionEntry {};
    // try {
    //     std::vector<VolumePartitionTableEntry> partitionTable = util::ReadVolumePartitionTable(blockDevicePath);
    //     if (partitionTable.size() != 1) {
    //         ERRLOG("failed to read partition table, or has multiple volumes");
    //         return false;
    //     }
    //     partitionEntry =  partitionTable.back();
    // } catch (std::exception& e) {
    //     ERRLOG("read volume partition got exception: %s", e.what());
    //     return false;
    // }

    // VolumeCopyMeta volumeCopyMeta {};
    // volumeCopyMeta.size = m_volumeSize;
    // volumeCopyMeta.blockSize = DEFAULT_BLOCK_SIZE;
    // volumeCopyMeta.partition = partitionEntry;

    // if (IsIncrementBackup()) {
    //     // TODO:: validate increment backup meta
    // }

    // 2. split session
    // for (uint64_t sessionOffset = 0; sessionOffset < m_volumeSize;) {
    //     uint64_t sessionSize = DEFAULT_SESSION_SIZE;
    //     if (sessionOffset + DEFAULT_SESSION_SIZE >= m_volumeSize) {
    //         sessionSize = m_volumeSize - sessionOffset;
    //     }
    //     std::string lastestChecksumBinPath = util::GetChecksumBinPath(
    //         m_restoreConfig->outputCopyMetaDirPath, sessionOffset, sessionSize);
    //     std::string copyFilePath = util::GetCopyFilePath(
    //         m_restoreConfig->outputCopyDataDirPath, m_restoreConfig->copyType, sessionOffset, sessionSize);
    //     // for increment backup
    //     std::string prevChecksumBinPath = "";
    //     if (IsIncrementBackup()) {
    //         prevChecksumBinPath = util::GetChecksumBinPath(m_restoreConfig->prevCopyMetaDirPath, sessionOffset, sessionSize);
    //     }

    //     VolumeRestoreSession session {};
    //     session.config = m_restoreConfig;
    //     session.sessionOffset = sessionOffset;
    //     session.sessionSize = sessionSize;
    //     session.lastestChecksumBinPath = lastestChecksumBinPath;
    //     session.prevChecksumBinPath = prevChecksumBinPath;
    //     session.copyFilePath = copyFilePath;
    //     volumeCopyMeta.slices.emplace_back(sessionOffset, sessionSize);
    //     m_sessionQueue.push(session);
    //     sessionOffset += sessionSize;
    // }

    return true;
}

bool VolumeRestoreTask::InitRestoreSessionContext(std::shared_ptr<VolumeRestoreSession> session) const
{
    // DBGLOG("init backup session context");
    // // 1. init basic backup container
    // session->counter = std::make_shared<SessionCounter>();
    // // session->allocator = std::make_shared<VolumeBlockAllocator>(session->config->blockSize, DEFAULT_ALLOCATOR_BLOCK_NUM);
    // session->writeQueue = std::make_shared<BlockingQueue<VolumeConsumeBlock>>(DEFAULT_QUEUE_SIZE);
    
    // // 2. check and init reader
    // session->reader = VolumeBlockReader::BuildCopyReader(
    //     m_restoreConfig->,
    //     session->sessionOffset,
    //     session->sessionSize,
    //     session
    // );
    // if (session->reader == nullptr) {
    //     ERRLOG("backup session failed to init reader");
    //     return false;
    // }

    // // 3. check and init writer
    // //session->writer = VolumeBlockWriter::BuildVolumeWriter(session);
    // if (session->writer == nullptr) {
    //     ERRLOG("backup session failed to init writer");
    //     return false;
    // }
    return true;
}

bool VolumeRestoreTask::StartRestoreSession(std::shared_ptr<VolumeRestoreSession> session) const
{
    DBGLOG("start restore session");
    if (session->reader == nullptr || session->writer == nullptr) {
        ERRLOG("restore session member nullptr! reader: %p writer: %p ",
            session->reader.get(), session->writer.get());
        return false;
    }
    DBGLOG("start restore session reader");
    if (!session->reader->Start()) {
        ERRLOG("backup session reader start failed");
        return false;
    }
    DBGLOG("start restore session writer");
    if (!session->writer->Start() ) {
        ERRLOG("restore session writer start failed");
        return false;
    }
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
        std::shared_ptr<VolumeRestoreSession> session = std::make_shared<VolumeRestoreSession>(m_sessionQueue.front());
        m_sessionQueue.pop();

        if (!InitRestoreSessionContext(session) || !StartRestoreSession(session)) {
            m_status = TaskStatus::FAILED;
            return;
        }

        // block the thread
        while (true) {
            // if (m_abort) {
            //     AbortSession(session);
            //     m_status = TaskStatus::ABORTED;
            //     return;
            // }
            // if (IsSessionFailed(session)) {
            //     ERRLOG("session failed");
            //     m_status = TaskStatus::FAILED;
            //     return;
            // }
            // if (IsSessionTerminated(session))  {
            //     break;
            // }
            DBGLOG("updateStatistics: bytesToReaded: %llu, bytesRead: %llu, bytesToWrite: %llu, bytesWritten: %llu",
            session->counter->bytesToRead.load(), session->counter->bytesRead.load(),
            session->counter->bytesToWrite.load(), session->counter->bytesWritten.load());
            std::this_thread::sleep_for(std::chrono::seconds(5));
        }
        DBGLOG("session complete successfully");
        UpdateCompletedSessionStatistics(session);
    }
    
    m_status = TaskStatus::SUCCEED;
    return;
}

void VolumeRestoreTask::UpdateRunningSessionStatistics(std::shared_ptr<VolumeRestoreSession> session)
{
    m_currentSessionStatistics.bytesToRead = session->counter->bytesToRead;
    m_currentSessionStatistics.bytesRead = session->counter->bytesRead;
    m_currentSessionStatistics.blocksToHash = session->counter->blocksToHash;
    m_currentSessionStatistics.blocksHashed = session->counter->blocksHashed;
    m_currentSessionStatistics.bytesToWrite = session->counter->bytesToWrite;
    m_currentSessionStatistics.bytesWritten = session->counter->bytesWritten;
}

void VolumeRestoreTask::UpdateCompletedSessionStatistics(std::shared_ptr<VolumeRestoreSession> session)
{
    m_completedSessionStatistics.bytesToRead += session->counter->bytesToRead;
    m_completedSessionStatistics.bytesRead += session->counter->bytesRead;
    m_completedSessionStatistics.blocksToHash += session->counter->blocksToHash;
    m_completedSessionStatistics.blocksHashed += session->counter->blocksHashed;
    m_completedSessionStatistics.bytesToWrite += session->counter->bytesToWrite;
    m_completedSessionStatistics.bytesWritten += session->counter->bytesWritten;
    memset(&m_currentSessionStatistics, 0, sizeof(TaskStatistics));
}