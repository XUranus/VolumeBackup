#include <chrono>
#include <exception>
#include <queue>
#include <string>
#include <sys/types.h>
#include <thread>
#include <vector>

#include "VolumeBackupContext.h"
#include "VolumeBackupUtils.h"
#include "VolumeBlockReader.h"
#include "VolumeBlockHasher.h"
#include "VolumeBlockWriter.h"

#include "VolumeBackup.h"

using namespace volumebackup;
using namespace volumebackup::util;

/*
 * backup copy folder herichical
 * 1. Full Copy
 *  ${CopyID}
 *       |
 *      ${UUID}
 *          |------data
 *          |       |------0.1024.data.bin
 *          |       |------1024.1024.data.bin
 *          |       |------2048.1024.data.bin
 *          |
 *          |------meta
 *                  |------fullcopy.meta.json
 *                  |------0.1024.sha256.bin
 *                  |------1024.1024.sha256.meta.bin
 *                  |------2048.1024.sha256.meta.bin
 *
 * 2. Increment Copy
 *  ${CopyID}
 *       |
 *      ${UUID}
 *          |------data (sparse file)
 *          |       |------0.1024.data.bin
 *          |       |------1024.1024.data.bin
 *          |       |------2048.1024.data.bin
 *          |
 *          |------meta
 *                  |------incrementcopy.meta.json
 *                  |------0.1024.sha256.meta.bin
 *                  |------1024.1024.sha256.meta.bin
 *                  |------2048.1024.sha256.meta.bin
 */


std::shared_ptr<VolumeBackupTask> VolumeBackupTask::BuildBackupTask(const VolumeBackupConfig& backupConfig)
{
    // 1. check volume size
    uint64_t volumeSize = 0;
    try {
        volumeSize = util::ReadVolumeSize(backupConfig.blockDevicePath);
    } catch (std::exception& e) {
        ERRLOG("retrive volume size got exception: %s", e.what());
        return nullptr;
    }
    if (volumeSize == 0) { // invalid volume
        return nullptr;
    }

    // 2. TODO:: check dir existence
    if (!util::CheckDirectoryExistence(backupConfig.outputCopyDataDirPath) ||
        !util::CheckDirectoryExistence(backupConfig.outputCopyMetaDirPath) ||
        (backupConfig.copyType == CopyType::INCREMENT &&
        !util::CheckDirectoryExistence(backupConfig.prevCopyMetaDirPath))) {
        ERRLOG("failed to prepare copy directory");
        return nullptr;
    }

    return std::make_shared<VolumeBackupTask>(backupConfig, volumeSize);
}


VolumeBackupTask::VolumeBackupTask(const VolumeBackupConfig& backupConfig, uint64_t volumeSize)
 : m_backupConfig(backupConfig), m_volumeSize(volumeSize)
{}

VolumeBackupTask::~VolumeBackupTask()
{}

bool VolumeBackupTask::Start()
{
    if (m_status != TaskStatus::INIT) {
        return false;
    }
    if (!Prepare()) {
        m_status = TaskStatus::FAILED;
        return false;
    }
    m_thread = std::thread(&VolumeBackupTask::ThreadFunc, this);
    m_status = TaskStatus::RUNNING;
    return true;
}

bool VolumeBackupTask::IsTerminated()
{
    return (
        m_status == TaskStatus::ABORTED ||
        m_status == TaskStatus::FAILED ||
        m_status == TaskStatus::SUCCEED
    );
}

TaskStatus VolumeBackupTask::GetStatus()
{
    return m_status;
}

bool VolumeBackupTask::Abort()
{
    m_abort = true;
}

// split session and save volume meta
bool VolumeBackupTask::Prepare()
{
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
        std::string checksumBinPath = util::GetChecksumBinPath(m_backupConfig.outputCopyMetaDirPath, sessionOffset, sessionSize);
        std::string copyFilePath = util::GetCopyFilePath(m_backupConfig.outputCopyDataDirPath, sessionOffset, sessionSize);
        VolumeBackupSession session {
            sessionOffset,
            sessionSize,
            checksumBinPath,
            copyFilePath,
        };
        volumeCopyMeta.slices.emplace_back(sessionOffset, sessionSize);
        m_sessionQueue.push(session);
    }

    if (!util::WriteVolumeCopyMeta(m_backupConfig.outputCopyMetaDirPath, volumeCopyMeta)) {
        ERRLOG("failed to write copy meta to dir: %s", m_backupConfig.outputCopyMetaDirPath.c_str());
        return false;
    }
}

void VolumeBackupTask::ThreadFunc()
{
    while (!m_sessionQueue.empty()) {
        if (m_abort) {
            m_status = TaskStatus::ABORTED;
            return;
        }
        VolumeBackupSession session = m_sessionQueue.front();
        m_sessionQueue.pop();
        auto context = std::make_shared<VolumeBackupContext>(); // init new context
        session.reader = VolumeBlockReader::BuildVolumeReader(
            m_backupConfig.blockDevicePath,
            session.sessionOffset,
            session.sessionSize,
            context
        );
        session.hasher = VolumeBlockHasher::BuildDirectHasher(
            context,
            session.checksumBinPath
        );
        session.writer = VolumeBlockWriter::BuildCopyWriter(
            session.copyFilePath,
            context
        );
        if (session.reader == nullptr || session.hasher == nullptr || session.writer == nullptr) {
            m_status = TaskStatus::FAILED;
            return;
        }
        if (!session.reader->Start() || session.hasher->Start() || session.writer->Start()) {
            m_status = TaskStatus::FAILED;
            return;
        }
        // block the thread
        while (true) {
            if (m_abort) {
                m_status = TaskStatus::ABORTED;
                return;
            }
            if (session.IsTerminated())  {
                break;
            }
            // TODO:: add statistics
            std::this_thread::sleep_for(std::chrono::seconds(5));
        }
    }
    
    m_status = TaskStatus::SUCCEED;
    return;
}