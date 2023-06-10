
#ifndef VOLUME_BACKUP_FACADE_H
#define VOLUME_BACKUP_FACADE_H

#include <string>
#include <cstdint>
#include <thread>
#include <queue>
#include <memory>

#include "VolumeBackupContext.h"

// volume backup application facade
namespace volumebackup {

enum class TaskStatus {
    INIT        =  0,
    RUNNING     =  1,
    SUCCEED     =  2,
    ABORTING    =  3,
    ABORTED     =  4,
    FAILED      =  5
};

struct TaskStatistics {
    uint64_t bytesToRead;
    uint64_t bytesRead;
    uint64_t bytesToHash;
    uint64_t bytesHashed;
    uint64_t bytesToWrite;
    uint64_t bytesWritten;
};

class VolumeBackupTask {
public:
    using SessionQueue = std::queue<volumebackup::VolumeBackupSession>;

    static std::shared_ptr<VolumeBackupTask> BuildBackupTask(const VolumeBackupConfig& backupConfig);
    
    ~VolumeBackupTask();
    bool            Start();
    bool            IsTerminated() const;
    TaskStatus      GetStatus() const;
    bool            Abort();
    TaskStatistics  GetStatistics() const;

private:
    VolumeBackupTask(const VolumeBackupConfig& backupConfig, uint64_t volumeSize);
    bool Prepare(); // split session and save meta
    void ThreadFunc();

private:
    uint64_t            m_volumeSize;
    VolumeBackupConfig  m_backupConfig;

    std::thread     m_thread;
    bool            m_abort { false }; // if aborted is invoked
    TaskStatus      m_status { TaskStatus::INIT };
    SessionQueue    m_sessionQueue;
    TaskStatistics  m_statistics;
};

}

#endif