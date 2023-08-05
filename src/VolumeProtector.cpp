#include "VolumeProtector.h"
#include "VolumeBackupTask.h"
#include "VolumeRestoreTask.h"
#include "VolumeUtils.h"

using namespace volumeprotect;

/*
 * backup copy folder herichical
 * Example of session size 1024 bytes, volume size 3072 bytes
 * Full/Synethetic Copy:
 *  ${CopyID}
 *       |
 *      ${Volume UUID}
 *          |------data
 *          |       |------0.1024.data.full.bin
 *          |       |------1024.1024.copydata.bin
 *          |       |------2048.1024.copydata.bin
 *          |
 *          |------meta
 *                  |------volumecopy.meta.json
 *                  |------0.1024.sha256.meta.bin
 *                  |------1024.1024.sha256.meta.bin
 *                  |------2048.1024.sha256.meta.bin
 *
 */

std::shared_ptr<VolumeProtectTask> VolumeProtectTask::BuildBackupTask(const VolumeBackupConfig& backupConfig)
{
    // 1. check volume size
    uint64_t volumeSize = 0;
    try {
        volumeSize = native::ReadVolumeSize(backupConfig.volumePath);
    } catch (const native::SystemApiException& e) {
        ERRLOG("retrive volume size got exception: %s", e.what());
        return nullptr;
    }
    if (volumeSize == 0) { // invalid volume
        return nullptr;
    }

    // 2. check dir existence
    if (!native::IsDirectoryExists(backupConfig.outputCopyDataDirPath) ||
        !native::IsDirectoryExists(backupConfig.outputCopyMetaDirPath) ||
        (backupConfig.copyType == CopyType::INCREMENT &&
        !native::IsDirectoryExists(backupConfig.prevCopyMetaDirPath))) {
        ERRLOG("failed to prepare copy directory");
        return nullptr;
    }

    return std::make_shared<VolumeBackupTask>(backupConfig, volumeSize);
}

std::shared_ptr<VolumeProtectTask> VolumeProtectTask::BuildRestoreTask(const VolumeRestoreConfig& restoreConfig)
{
    // 1. check volume size
    uint64_t volumeSize = 0;
    try {
        volumeSize = native::ReadVolumeSize(restoreConfig.volumePath);
    } catch (const native::SystemApiException& e) {
        ERRLOG("retrive volume size got exception: %s", e.what());
        return nullptr;
    }
    if (volumeSize == 0) { // invalid volume
        return nullptr;
    }

    // 2. check dir existence
    if (!native::IsDirectoryExists(restoreConfig.copyDataDirPath) ||
        !native::IsDirectoryExists(restoreConfig.copyMetaDirPath)) {
        ERRLOG("failed to prepare copy directory");
        return nullptr;
    }

    return std::make_shared<VolumeRestoreTask>(restoreConfig);
}

void StatefulTask::Abort()
{
    m_abort = true;
    if (m_status == TaskStatus::INIT) {
        m_status = TaskStatus::ABORTED;
        return;
    }
    if (IsTerminated()) {
        return;
    }
    m_status = TaskStatus::ABORTING;
}

TaskStatus StatefulTask::GetStatus() const
{
    return m_status;
}

bool StatefulTask::IsFailed() const
{
    return m_status == TaskStatus::FAILED;
}

bool StatefulTask::IsTerminated() const
{
    return (
        m_status == TaskStatus::SUCCEED ||
        m_status == TaskStatus::ABORTED ||
        m_status == TaskStatus::FAILED
    );
}

std::string StatefulTask::GetStatusString() const
{
    std::unordered_map<TaskStatus, std::string> table {
        { TaskStatus::INIT, "INIT" },
        { TaskStatus::RUNNING, "RUNNING" },
        { TaskStatus::SUCCEED, "SUCCEED" },
        { TaskStatus::ABORTING, "ABORTING" },
        { TaskStatus::ABORTED, "ABORTED" },
        { TaskStatus::FAILED, "FAILED" },
    };
    return table[m_status];
}

TaskStatistics TaskStatistics::operator + (const TaskStatistics& statistic) const
{
    TaskStatistics res;
    res.bytesToRead     = statistic.bytesToRead + this->bytesToRead;
    res.bytesRead       = statistic.bytesRead + this->bytesRead;
    res.blocksToHash    = statistic.blocksToHash + this->blocksToHash;
    res.blocksHashed    = statistic.blocksHashed + this->blocksHashed;
    res.bytesToWrite    = statistic.bytesToWrite + this->bytesToWrite;
    res.bytesWritten    = statistic.bytesWritten + this->bytesWritten;
    return res;
}