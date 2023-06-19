#ifndef VOLUME_BACKUP_TASK_H
#define VOLUME_BACKUP_TASK_H

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <exception>
#include <vector>

#include "VolumeProtector.h"
#include "VolumeProtectTaskContext.h"

namespace volumeprotect {

class VolumeBackupTask : public VolumeProtectTask {
public:
    using SessionQueue = std::queue<VolumeBackupSession>;
    
    bool            Start() override;
    TaskStatistics  GetStatistics() const override;

    VolumeBackupTask(const VolumeBackupConfig& backupConfig, uint64_t volumeSize);
    ~VolumeBackupTask();
private:
    bool Prepare(); // split session and save meta
    void ThreadFunc();
    bool StartBackupSession(std::shared_ptr<VolumeBackupSession> session) const;
    bool InitBackupSessionContext(std::shared_ptr<VolumeBackupSession> session) const;
    bool IsIncrementBackup() const;
    void UpdateRunningSessionStatistics(std::shared_ptr<VolumeBackupSession> session);
    void UpdateCompletedSessionStatistics(std::shared_ptr<VolumeBackupSession> session);
    bool IsSessionTerminated(std::shared_ptr<VolumeBackupSession> session) const;
    bool IsSessionFailed(std::shared_ptr<VolumeBackupSession> session) const;
    void AbortSession(std::shared_ptr<VolumeBackupSession> session) const;

private:
    uint64_t                                m_volumeSize;
    std::shared_ptr<VolumeBackupConfig>     m_backupConfig;

    std::thread     m_thread;

    SessionQueue    m_sessionQueue;
    // statistics
    TaskStatistics  m_currentSessionStatistics; // current running session statistics
    TaskStatistics  m_completedSessionStatistics; // statistic sum of all completed session
};

}

#endif