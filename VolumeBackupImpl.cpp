#include <chrono>
#include <exception>
#include <memory>
#include <queue>
#include <string>
#include <sys/types.h>
#include <thread>
#include <vector>

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

TaskStatistics VolumeBackupTaskImpl::GetStatistics() const
{
    return m_statistics;
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

    // 2. split session
    for (uint64_t sessionOffset = 0; sessionOffset < m_volumeSize;) {
        uint64_t sessionSize = DEFAULT_SESSION_SIZE;
        if (sessionOffset + DEFAULT_SESSION_SIZE >= m_volumeSize) {
            sessionSize = m_volumeSize - sessionOffset;
        }
        std::string lastestChecksumBinPath = util::GetChecksumBinPath(m_backupConfig->outputCopyMetaDirPath, sessionOffset, sessionSize);
        std::string copyFilePath = util::GetCopyFilePath(m_backupConfig->outputCopyDataDirPath, sessionOffset, sessionSize);
        VolumeBackupSession session {};
        session.config = m_backupConfig;
        session.sessionOffset = sessionOffset;
        session.sessionSize = sessionSize;
        session.lastestChecksumBinPath = lastestChecksumBinPath;
        session.prevChecksumBinPath = "";
        session.copyFilePath = copyFilePath;
        volumeCopyMeta.slices.emplace_back(sessionOffset, sessionSize);
        m_sessionQueue.push(session);
        sessionOffset += sessionSize;
    }

    if (!util::WriteVolumeCopyMeta(m_backupConfig->outputCopyMetaDirPath, volumeCopyMeta)) {
        ERRLOG("failed to write copy meta to dir: %s", m_backupConfig->outputCopyMetaDirPath.c_str());
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

        // init session container context
        session->counter = std::make_shared<SessionCounter>();
        session->allocator = std::make_shared<VolumeBlockAllocator>(session->config->blockSize, DEFAULT_ALLOCATOR_BLOCK_NUM);
        session->hashingQueue = std::make_shared<BlockingQueue<VolumeConsumeBlock>>(DEFAULT_QUEUE_SIZE);
        session->writeQueue = std::make_shared<BlockingQueue<VolumeConsumeBlock>>(DEFAULT_QUEUE_SIZE);

        session->reader = VolumeBlockReader::BuildVolumeReader(
            m_backupConfig->blockDevicePath,
            session->sessionOffset,
            session->sessionSize,
            session
        );
        session->hasher = VolumeBlockHasher::BuildDirectHasher(session);
        session->writer = VolumeBlockWriter::BuildCopyWriter(session);
        DBGLOG("start new session");
        if (session->reader == nullptr || session->hasher == nullptr || session->writer == nullptr) {
            ERRLOG("session member nullptr! reader: %p hasher: %p writer: %p ", session->reader.get(), session->hasher.get(), session->writer.get());
            m_status = TaskStatus::FAILED;
            return;
        }
        DBGLOG("start session reader");
        if (!session->reader->Start()) {
            ERRLOG("session reader start failed");
            m_status = TaskStatus::FAILED;
            return;
        }
        DBGLOG("start session hasher");
        if (!session->hasher->Start() ) {
            ERRLOG("session hasher start failed");
            m_status = TaskStatus::FAILED;
            return;
        }
        DBGLOG("start session writer");
        if (!session->writer->Start() ) {
            ERRLOG("session writer start failed");
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
            // TODO:: add statistics
            std::this_thread::sleep_for(std::chrono::seconds(5));
        }
    }
    
    m_status = TaskStatus::SUCCEED;
    return;
}