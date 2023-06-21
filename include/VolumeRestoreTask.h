#ifndef VOLUME_RESTORE_TASK_H
#define VOLUME_RESTORE_TASK_H

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <exception>
#include <vector>

#include "VolumeProtector.h"
#include "VolumeProtectTaskContext.h"

namespace volumeprotect {

class VolumeRestoreTask : public VolumeProtectTask {
public:
    using SessionQueue = std::queue<VolumeRestoreSession>;
    
    bool            Start() override;
    TaskStatistics  GetStatistics() const override;

    VolumeRestoreTask(const VolumeRestoreConfig& restoreConfig);
    ~VolumeRestoreTask();
private:
    bool Prepare(); // split session and save meta
    void ThreadFunc();
    bool StartRestoreSession(std::shared_ptr<VolumeRestoreSession> session) const;
    bool InitRestoreSessionContext(std::shared_ptr<VolumeRestoreSession> session) const;
    bool IsIncrementCopy() const;
    void UpdateRunningSessionStatistics(std::shared_ptr<VolumeRestoreSession> session);
    void UpdateCompletedSessionStatistics(std::shared_ptr<VolumeRestoreSession> session);

private:
    uint64_t                                m_volumeSize;
    std::shared_ptr<VolumeRestoreConfig>    m_restoreConfig;

    std::thread     m_thread;
    SessionQueue    m_sessionQueue;
    bool            m_incrementCopy { false };
    // statistics
    TaskStatistics  m_currentSessionStatistics; // current running session statistics
    TaskStatistics  m_completedSessionStatistics; // statistic sum of all completed session
};

}

#endif