#include "VolumeProtector.h"
#include "VolumeBackupTask.h"
#include "VolumeRestoreTask.h"
#include "native/NativeIOInterface.h"
#include <chrono>
#include <string>
#include <thread>

using namespace volumeprotect;

static std::unordered_map<int, std::string> g_statusStringTable {
    { static_cast<int>(TaskStatus::INIT), "INIT" },
    { static_cast<int>(TaskStatus::RUNNING), "RUNNING" },
    { static_cast<int>(TaskStatus::SUCCEED), "SUCCEED" },
    { static_cast<int>(TaskStatus::ABORTING), "ABORTING" },
    { static_cast<int>(TaskStatus::ABORTED), "ABORTED" },
    { static_cast<int>(TaskStatus::FAILED), "FAILED" },
};

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

std::unique_ptr<VolumeProtectTask> VolumeProtectTask::BuildBackupTask(const VolumeBackupConfig& backupConfig)
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

    return std::unique_ptr<VolumeProtectTask>(new VolumeBackupTask(backupConfig, volumeSize));
}

std::unique_ptr<VolumeProtectTask> VolumeProtectTask::BuildRestoreTask(const VolumeRestoreConfig& restoreConfig)
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

    return std::unique_ptr<VolumeProtectTask>(new VolumeRestoreTask(restoreConfig));
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
    return g_statusStringTable[static_cast<int>(m_status)];
}

void StatefulTask::AssertTaskNotStarted()
{
    (m_status != TaskStatus::INIT) ? throw std::runtime_error("task already started") : void();
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

// implement C style interface ...
inline static std::string StringFromCStr(char* str)
{
    return str == nullptr ? std::string("") : std::string(str);
}

void* BuildBackupTask(VolumeBackupConf_C cBackupConf)
{
    VolumeBackupConfig backupConfig {};
    backupConfig.copyType = static_cast<CopyType>(cBackupConf.copyType);
    backupConfig.volumePath = StringFromCStr(cBackupConf.volumePath);
    backupConfig.prevCopyMetaDirPath = StringFromCStr(cBackupConf.prevCopyMetaDirPath);
    backupConfig.outputCopyDataDirPath = StringFromCStr(cBackupConf.outputCopyDataDirPath);
    backupConfig.outputCopyMetaDirPath = StringFromCStr(cBackupConf.outputCopyMetaDirPath);
    backupConfig.blockSize = cBackupConf.blockSize;
    backupConfig.sessionSize = cBackupConf.sessionSize;
    backupConfig.hasherNum = cBackupConf.hasherNum;
    backupConfig.hasherEnabled = cBackupConf.hasherEnabled;
    backupConfig.enableCheckpoint = cBackupConf.enableCheckpoint;
    std::unique_ptr<VolumeProtectTask> task = VolumeProtectTask::BuildBackupTask(backupConfig);
    return reinterpret_cast<void*>(task.release());
}

void* BuildRestoreTask(VolumeRestoreConf_C cRestoreConf)
{   VolumeRestoreConfig restoreConfig {};
    restoreConfig.volumePath = StringFromCStr(cRestoreConf.volumePath);
    restoreConfig.copyDataDirPath = StringFromCStr(cRestoreConf.copyDataDirPath);
    restoreConfig.copyMetaDirPath = StringFromCStr(cRestoreConf.copyMetaDirPath);
    restoreConfig.enableCheckpoint = cRestoreConf.enableCheckpoint;
    std::unique_ptr<VolumeProtectTask> task = VolumeProtectTask::BuildRestoreTask(restoreConfig);
    return reinterpret_cast<void*>(task.release());
}

bool StartTask(void* task)
{
    return reinterpret_cast<VolumeProtectTask*>(task)->Start();
}

void DestroyTask(void* task)
{
    delete reinterpret_cast<VolumeProtectTask*>(task);
}

TaskStatistics_C GetTaskStatistics(void* task)
{
    TaskStatistics statistic = reinterpret_cast<VolumeProtectTask*>(task)->GetStatistics();
    TaskStatistics_C cstat;
    ::memset(&cstat, 0, sizeof(TaskStatistics_C));
    cstat.blocksHashed = statistic.blocksHashed;
    cstat.blocksToHash = statistic.blocksToHash;
    cstat.bytesRead = statistic.bytesRead;
    cstat.bytesToRead = statistic.bytesToRead;
    cstat.bytesToWrite = statistic.bytesToWrite;
    cstat.bytesWritten = statistic.bytesWritten;
    return cstat;
}

void AbortTask(void* task)
{
    reinterpret_cast<VolumeProtectTask*>(task)->Abort();
}

TaskStatus_C GetTaskStatus(void* task)
{
    TaskStatus taskStatus = reinterpret_cast<VolumeProtectTask*>(task)->GetStatus();
    return static_cast<TaskStatus_C>(taskStatus);
}

bool IsTaskFailed(void* task)
{
    return reinterpret_cast<VolumeProtectTask*>(task)->IsFailed();
}

bool IsTaskTerminated(void* task)
{
    return reinterpret_cast<VolumeProtectTask*>(task)->IsTerminated();
}