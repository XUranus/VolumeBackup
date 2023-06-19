#include "VolumeProtector.h"
#include "VolumeBackupTask.h"
#include "VolumeRestoreTask.h"
#include "VolumeUtils.h"

using namespace volumeprotect;

/*
 * backup copy folder herichical
 * Example of sessionSize 1024, volumeSize 3072
 * 1. Full Copy
 *  ${CopyID}
 *       |
 *      ${UUID}
 *          |------data
 *          |       |------0.1024.data.full.bin
 *          |       |------1024.1024.data.full.bin
 *          |       |------2048.1024.data.full.bin
 *          |
 *          |------meta
 *                  |------fullcopy.meta.json
 *                  |------0.1024.sha256.meta.bin
 *                  |------1024.1024.sha256.meta.bin
 *                  |------2048.1024.sha256.meta.bin
 *
 * 2. Increment Copy
 *  ${CopyID}
 *       |
 *      ${UUID}
 *          |------data (sparse file)
 *          |       |------0.1024.data.inc.bin
 *          |       |------1024.1024.data.inc.bin
 *          |       |------2048.1024.data.inc.bin
 *          |
 *          |------meta
 *                  |------incrementcopy.meta.json
 *                  |------0.1024.sha256.meta.bin
 *                  |------1024.1024.sha256.meta.bin
 *                  |------2048.1024.sha256.meta.bin
 */

std::shared_ptr<VolumeProtectTask> VolumeProtectTask::BuildBackupTask(const VolumeBackupConfig& backupConfig)
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

    // 2. check dir existence
    if (!util::CheckDirectoryExistence(backupConfig.outputCopyDataDirPath) ||
        !util::CheckDirectoryExistence(backupConfig.outputCopyMetaDirPath) ||
        (backupConfig.copyType == CopyType::INCREMENT &&
        !util::CheckDirectoryExistence(backupConfig.prevCopyMetaDirPath))) {
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
        volumeSize = util::ReadVolumeSize(restoreConfig.blockDevicePath);
    } catch (std::exception& e) {
        ERRLOG("retrive volume size got exception: %s", e.what());
        return nullptr;
    }
    if (volumeSize == 0) { // invalid volume
        return nullptr;
    }

    // 2. check dir existence
    if (!util::CheckDirectoryExistence(restoreConfig.copyDataDirPath) ||
        !util::CheckDirectoryExistence(restoreConfig.copyMetaDirPath)) {
        ERRLOG("failed to prepare copy directory");
        return nullptr;
    }

    return std::make_shared<VolumeRestoreTask>(restoreConfig, volumeSize);
}

void StatefulTask::Abort()
{
    m_abort = true;
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

TaskStatistics TaskStatistics::operator+ (const TaskStatistics& statistic) const
{
    TaskStatistics res;
    res.bytesToRead     = statistic.bytesToRead + this->bytesToRead;
    res.bytesRead       = statistic.bytesRead + this->bytesRead;;
    res.blocksToHash    = statistic.blocksToHash + this->blocksToHash;
    res.blocksHashed    = statistic.blocksHashed + this->blocksHashed;
    res.bytesToWrite    = statistic.bytesToWrite + this->bytesToWrite;
    res.bytesWritten    = statistic.bytesWritten + this->bytesWritten;
    return res;
}