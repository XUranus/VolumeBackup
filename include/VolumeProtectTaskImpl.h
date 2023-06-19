#ifndef VOLUME_PROTECT_TASK_IMPL_H
#define VOLUME_PROTECT_TASK_IMPL_H

#include "VolumeProtector.h"
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <exception>
#include <vector>

#include "VolumeProtector.h"
#include "VolumeProtectTaskContext.h"

namespace volumeprotect {

class VolumeBackupTaskImpl : public VolumeBackupTask {
public:
    using SessionQueue = std::queue<VolumeBackupSession>;
    
    bool            Start() override;
    bool            IsTerminated() const override;
    TaskStatistics  GetStatistics() const override;
    TaskStatus      GetStatus() const override;
    void            Abort() override;

    VolumeBackupTaskImpl(const VolumeBackupConfig& backupConfig, uint64_t volumeSize);
    ~VolumeBackupTaskImpl();
private:
    bool Prepare(); // split session and save meta
    void ThreadFunc();
    bool StartBackupSession(std::shared_ptr<VolumeBackupSession> session) const;
    bool InitBackupSessionContext(std::shared_ptr<VolumeBackupSession> session) const;
    bool IsIncrementBackup() const;
    void UpdateRunningSessionStatistics(std::shared_ptr<VolumeBackupSession> session);
    void UpdateCompletedSessionStatistics(std::shared_ptr<VolumeBackupSession> session);

private:
    uint64_t                                m_volumeSize;
    std::shared_ptr<VolumeBackupConfig>     m_backupConfig;

    std::thread     m_thread;
    bool            m_abort { false }; // if aborted is invoked
    TaskStatus      m_status { TaskStatus::INIT };
    SessionQueue    m_sessionQueue;
    // statistics
    TaskStatistics  m_currentSessionStatistics; // current running session statistics
    TaskStatistics  m_completedSessionStatistics; // statistic sum of all completed session
};

}

#endif