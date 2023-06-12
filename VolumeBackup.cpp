#include "VolumeBackup.h"
#include "VolumeBackupImpl.h"
#include "VolumeBackupUtils.h"

using namespace volumebackup;

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

    return std::make_shared<VolumeBackupTaskImpl>(backupConfig, volumeSize);
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