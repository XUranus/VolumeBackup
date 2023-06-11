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
{}

bool VolumeBackupTaskImpl::Start()
{
    if (m_status != TaskStatus::INIT) {
        return false;
    }
    if (!Prepare()) {
        m_status = TaskStatus::FAILED;
        return false;
    }
    m_thread = std::thread(&VolumeBackupTaskImpl::ThreadFunc, this);
    m_status = TaskStatus::RUNNING;
    return true;
}

bool VolumeBackupTaskImpl::IsTerminated() const
{
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
    }

    if (!util::WriteVolumeCopyMeta(m_backupConfig->outputCopyMetaDirPath, volumeCopyMeta)) {
        ERRLOG("failed to write copy meta to dir: %s", m_backupConfig->outputCopyMetaDirPath.c_str());
        return false;
    }
    return true;
}

void VolumeBackupTaskImpl::ThreadFunc()
{
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
        if (session->reader == nullptr || session->hasher == nullptr || session->writer == nullptr) {
            m_status = TaskStatus::FAILED;
            return;
        }
        if (!session->reader->Start() || session->hasher->Start() || session->writer->Start()) {
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