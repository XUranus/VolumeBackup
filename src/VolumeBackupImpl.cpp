#include <chrono>
#include <exception>
#include <memory>
#include <queue>
#include <string>
#include <sys/types.h>
#include <thread>
#include <vector>

#include "Logger.h"
#include "VolumeBackup.h"
#include "VolumeBackupContext.h"
#include "VolumeBackupUtils.h"
#include "VolumeBlockReader.h"
#include "VolumeBlockHasher.h"
#include "VolumeBlockWriter.h"
#include "BlockingQueue.h"
#include "VolumeBackupImpl.h"

using namespace volumebackup;
using namespace volumebackup::util;

namespace {
    constexpr auto DEFAULT_ALLOCATOR_BLOCK_NUM = 32;
    constexpr auto DEFAULT_QUEUE_SIZE = 32;
}

VolumeBackupTaskImpl::VolumeBackupTaskImpl(const VolumeBackupConfig& backupConfig, uint64_t volumeSize)
 : m_backupConfig(std::make_shared<VolumeBackupConfig>(backupConfig)),
   m_volumeSize(volumeSize),
   m_status(TaskStatus::INIT)
{}

VolumeBackupTaskImpl::~VolumeBackupTaskImpl()
{
    DBGLOG("destroy VolumeBackupTaskImpl");
    if (m_thread.joinable()) {
        m_thread.join();
    }
}

bool VolumeBackupTaskImpl::Start()
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
    m_thread = std::thread(&VolumeBackupTaskImpl::ThreadFunc, this);
    return true;
}

bool VolumeBackupTaskImpl::IsTerminated() const
{
    DBGLOG("VolumeBackupTaskImpl::IsTerminated %d", m_status);
    return (
        m_status == TaskStatus::ABORTED ||
        m_status == TaskStatus::FAILED ||
        m_status == TaskStatus::SUCCEED
    );
}


TaskStatistics operator + (const TaskStatistics& statistic1, const TaskStatistics& statistic2)
{
    TaskStatistics res;
    res.bytesToRead     = statistic1.bytesToRead + statistic2.bytesToRead;
    res.bytesRead       = statistic1.bytesRead + statistic2.bytesRead;;
    res.blocksToHash    = statistic1.blocksToHash + statistic2.blocksToHash;
    res.blocksHashed    = statistic1.blocksHashed + statistic2.blocksHashed;
    res.bytesToWrite    = statistic1.bytesToWrite + statistic2.bytesToWrite;
    res.bytesWritten    = statistic1.bytesWritten + statistic2.bytesWritten;
    return res;
}

TaskStatistics VolumeBackupTaskImpl::GetStatistics() const
{
    return m_completedSessionStatistics + m_currentSessionStatistics;
}

TaskStatus VolumeBackupTaskImpl::GetStatus() const
{
    return m_status;
}

void VolumeBackupTaskImpl::Abort()
{
    m_abort = true;
    m_status = TaskStatus::ABORTING;
}

bool VolumeBackupTaskImpl::IsIncrementBackup() const
{
    return m_backupConfig->copyType == CopyType::INCREMENT;
}

// split session and save volume meta
bool VolumeBackupTaskImpl::Prepare()
{
    std::string blockDevicePath = m_backupConfig->blockDevicePath;
    // 1. retrive volume partition info
    VolumePartitionTableEntry partitionEntry {};
    try {
        std::vector<VolumePartitionTableEntry> partitionTable = util::ReadVolumePartitionTable(blockDevicePath);
        if (partitionTable.size() != 1) {
            ERRLOG("failed to read partition table, or has multiple volumes");
            return false;
        }
        partitionEntry =  partitionTable.back();
    } catch (std::exception& e) {
        ERRLOG("read volume partition got exception: %s", e.what());
        return false;
    }

    VolumeCopyMeta volumeCopyMeta {};
    volumeCopyMeta.size = m_volumeSize;
    volumeCopyMeta.blockSize = DEFAULT_BLOCK_SIZE;
    volumeCopyMeta.partition = partitionEntry;

    if (IsIncrementBackup()) {
        // TODO:: validate increment backup meta
    }

    // 2. split session
    for (uint64_t sessionOffset = 0; sessionOffset < m_volumeSize;) {
        uint64_t sessionSize = DEFAULT_SESSION_SIZE;
        if (sessionOffset + DEFAULT_SESSION_SIZE >= m_volumeSize) {
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

        VolumeBackupSession session {};
        session.config = m_backupConfig;
        session.sessionOffset = sessionOffset;
        session.sessionSize = sessionSize;
        session.lastestChecksumBinPath = lastestChecksumBinPath;
        session.prevChecksumBinPath = prevChecksumBinPath;
        session.copyFilePath = copyFilePath;
        volumeCopyMeta.slices.emplace_back(sessionOffset, sessionSize);
        m_sessionQueue.push(session);
        sessionOffset += sessionSize;
    }

    if (!util::WriteVolumeCopyMeta(m_backupConfig->outputCopyMetaDirPath, m_backupConfig->copyType, volumeCopyMeta)) {
        ERRLOG("failed to write copy meta to dir: %s", m_backupConfig->outputCopyMetaDirPath.c_str());
        return false;
    }
    return true;
}

bool VolumeBackupTaskImpl::InitBackupSessionContext(std::shared_ptr<VolumeBackupSession> session) const
{
    DBGLOG("init backup session context");
    // 1. init basic backup container
    session->counter = std::make_shared<SessionCounter>();
    session->allocator = std::make_shared<VolumeBlockAllocator>(session->config->blockSize, DEFAULT_ALLOCATOR_BLOCK_NUM);
    session->hashingQueue = std::make_shared<BlockingQueue<VolumeConsumeBlock>>(DEFAULT_QUEUE_SIZE);
    session->writeQueue = std::make_shared<BlockingQueue<VolumeConsumeBlock>>(DEFAULT_QUEUE_SIZE);
    
    // 2. check and init reader
    session->reader = VolumeBlockReader::BuildVolumeReader(
        m_backupConfig->blockDevicePath,
        session->sessionOffset,
        session->sessionSize,
        session
    );
    if (session->reader == nullptr) {
        ERRLOG("backup session failed to init reader");
        return false;
    }

    // 2. check and init hasher
    if (IsIncrementBackup()) {
        session->hasher = VolumeBlockHasher::BuildDiffHasher(session);
    } else {
        session->hasher = VolumeBlockHasher::BuildDirectHasher(session);
    }
    if (session->hasher == nullptr) {
        ERRLOG("backup session failed to init hasher");
        return false;
    }

    // 3. check and init writer
    session->writer = VolumeBlockWriter::BuildCopyWriter(session);
    if (session->writer == nullptr) {
        ERRLOG("backup session failed to init writer");
        return false;
    }
    return true;
}

bool VolumeBackupTaskImpl::StartBackupSession(std::shared_ptr<VolumeBackupSession> session) const
{
    DBGLOG("start backup session");
    if (session->reader == nullptr || session->hasher == nullptr || session->writer == nullptr) {
        ERRLOG("backup session member nullptr! reader: %p hasher: %p writer: %p ",
            session->reader.get(), session->hasher.get(), session->writer.get());
        return false;
    }
    DBGLOG("start backup session reader");
    if (!session->reader->Start()) {
        ERRLOG("backup session reader start failed");
        return false;
    }
    DBGLOG("start backup session hasher");
    if (!session->hasher->Start() ) {
        ERRLOG("backup session hasher start failed");
        return false;
    }
    DBGLOG("start backup session writer");
    if (!session->writer->Start() ) {
        ERRLOG("backup session writer start failed");
        return false;
    }
    return true;
}

void VolumeBackupTaskImpl::ThreadFunc()
{
    DBGLOG("start task main thread");
    while (!m_sessionQueue.empty()) {
        if (m_abort) {
            m_status = TaskStatus::ABORTED;
            return;
        }

        // pop a session from session queue to init a new session
        std::shared_ptr<VolumeBackupSession> session = std::make_shared<VolumeBackupSession>(m_sessionQueue.front());
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
            session->counter->bytesToRead.load(), session->counter->bytesRead.load(),
            session->counter->blocksToHash.load(), session->counter->blocksHashed.load(),
            session->counter->bytesToWrite.load(), session->counter->bytesWritten.load());
            std::this_thread::sleep_for(std::chrono::seconds(5));
        }
        DBGLOG("sesion complete successfully");
        UpdateCompletedSessionStatistics(session);
    }
    
    m_status = TaskStatus::SUCCEED;
    return;
}

void VolumeBackupTaskImpl::UpdateRunningSessionStatistics(std::shared_ptr<VolumeBackupSession> session)
{
    m_currentSessionStatistics.bytesToRead = session->counter->bytesToRead;
    m_currentSessionStatistics.bytesRead = session->counter->bytesRead;
    m_currentSessionStatistics.blocksToHash = session->counter->blocksToHash;
    m_currentSessionStatistics.blocksHashed = session->counter->blocksHashed;
    m_currentSessionStatistics.bytesToWrite = session->counter->bytesToWrite;
    m_currentSessionStatistics.bytesWritten = session->counter->bytesWritten;
}

void VolumeBackupTaskImpl::UpdateCompletedSessionStatistics(std::shared_ptr<VolumeBackupSession> session)
{
    m_completedSessionStatistics.bytesToRead += session->counter->bytesToRead;
    m_completedSessionStatistics.bytesRead += session->counter->bytesRead;
    m_completedSessionStatistics.blocksToHash += session->counter->blocksToHash;
    m_completedSessionStatistics.blocksHashed += session->counter->blocksHashed;
    m_completedSessionStatistics.bytesToWrite += session->counter->bytesToWrite;
    m_completedSessionStatistics.bytesWritten += session->counter->bytesWritten;
    memset(&m_currentSessionStatistics, 0, sizeof(TaskStatistics));
}