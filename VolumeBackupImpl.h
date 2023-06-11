#ifndef VOLUME_BACKUP_TASK_IMPL_H
#define VOLUME_BACKUP_TASK_IMPL_H

#include "VolumeBackup.h"
#include <cstdint>
#include <stdexcept>
#include <string>
#include <exception>
#include <vector>

#include "VolumeBackup.h"
#include "VolumeBackupContext.h"

namespace volumebackup {

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

private:
    uint64_t                                m_volumeSize;
    std::shared_ptr<VolumeBackupConfig>     m_backupConfig;

    std::thread     m_thread;
    bool            m_abort { false }; // if aborted is invoked
    TaskStatus      m_status { TaskStatus::INIT };
    SessionQueue    m_sessionQueue;
    TaskStatistics  m_statistics;
};

}

#endif