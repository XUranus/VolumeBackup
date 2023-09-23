#include "VolumeProtector.h"
#include "VolumeBackupTask.h"
#include "VolumeRestoreTask.h"
#include "native/RawIO.h"
#include "native/FileSystemAPI.h"

using namespace volumeprotect;

namespace {
    constexpr auto VOLUME_NAME_LEN_MAX = 32;
}

static std::unordered_map<int, std::string> g_statusStringTable {
    { static_cast<int>(TaskStatus::INIT), "INIT" },
    { static_cast<int>(TaskStatus::RUNNING), "RUNNING" },
    { static_cast<int>(TaskStatus::SUCCEED), "SUCCEED" },
    { static_cast<int>(TaskStatus::ABORTING), "ABORTING" },
    { static_cast<int>(TaskStatus::ABORTED), "ABORTED" },
    { static_cast<int>(TaskStatus::FAILED), "FAILED" },
};

/*
 * Backup copy folder hierarchy (Full/Synethetic Copy):
 * Example1: a copy with format "CopyFormat::BIN" and copyName "backupvolume" with 3 session
 *  It should have fragment copy data files and corresponding checksum meta files with same nums of sessions
 *  ${CopyID}
 *       |
 *      ${Volume UUID}
 *          |------data
 *          |       |------backupvolume.copydata.bin.part1
 *          |       |------backupvolume.copydata.bin.part2
 *          |       |------backupvolume.copydata.bin.part3
 *          |
 *          |------meta
 *                  |------volumecopy.meta.json
 *                  |------backupvolume.1.meta.bin
 *                  |------backupvolume.2.sha256.meta.bin
 *                  |------backupvolume.3.sha256.meta.bin
 *
 * Example2: a copy with format "CopyFormat::IMAGE" and copyName "raspberry" with 2 session
 *  It should have only one data file and 2 checksum files
 *  ${CopyID}
 *       |
 *      ${Volume UUID}
 *          |------data
 *          |       |------raspberry.copydata.img
 *          |
 *          |------meta
 *                  |------volumecopy.meta.json
 *                  |------raspberry.1.meta.bin
 *                  |------raspberry.2.sha256.meta.bin
 * 
 * For Windows OS, *.vhd, *.vhdx are also supported and it's handled similar to image format
 * volumecopy.meta.json saves meta data (format, sessions) of the copy and it's critical for the copy to mount or restore
 */

std::unique_ptr<VolumeProtectTask> VolumeProtectTask::BuildBackupTask(const VolumeBackupConfig& backupConfig)
{
    // fill missing BackupConfig fields
    VolumeBackupConfig finalBackupConfig = backupConfig;
    if (finalBackupConfig.copyName.empty() || VOLUME_NAME_LEN_MAX < finalBackupConfig.copyName.length()) {
        namespace chrono = std::chrono;
        using clock = std::chrono::system_clock;
        auto timestamp = std::chrono::duration_cast<chrono::microseconds>(clock::now().time_since_epoch()).count();
        finalBackupConfig.copyName = std::to_string(timestamp);
        WARNLOG("invalid copy name %s, generate new copyname %s",
            backupConfig.copyName.c_str(), finalBackupConfig.copyName.c_str());
    }

    // 2. check volume size
    uint64_t volumeSize = 0;
    try {
        volumeSize = fsapi::ReadVolumeSize(backupConfig.volumePath);
    } catch (const SystemApiException& e) {
        ERRLOG("retrive volume size got exception: %s", e.what());
        return nullptr;
    }
    if (volumeSize == 0) { // invalid volume
        return nullptr;
    }

    // 3. check dir existence
    if (!fsapi::IsDirectoryExists(backupConfig.outputCopyDataDirPath) ||
        !fsapi::IsDirectoryExists(backupConfig.outputCopyMetaDirPath) ||
        (backupConfig.copyType == CopyType::INCREMENT &&
        !fsapi::IsDirectoryExists(backupConfig.prevCopyMetaDirPath))) {
        ERRLOG("failed to prepare copy directory");
        return nullptr;
    }

    return std::unique_ptr<VolumeProtectTask>(new VolumeBackupTask(finalBackupConfig, volumeSize));
}

std::unique_ptr<VolumeProtectTask> VolumeProtectTask::BuildRestoreTask(const VolumeRestoreConfig& restoreConfig)
{
    // 1. check volume size
    uint64_t volumeSize = 0;
    try {
        volumeSize = fsapi::ReadVolumeSize(restoreConfig.volumePath);
    } catch (const SystemApiException& e) {
        ERRLOG("retrive volume size got exception: %s", e.what());
        return nullptr;
    }
    if (volumeSize == 0) { // invalid volume
        return nullptr;
    }

    // 2. check dir existence
    if (!fsapi::IsDirectoryExists(restoreConfig.copyDataDirPath) ||
        !fsapi::IsDirectoryExists(restoreConfig.copyMetaDirPath)) {
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