#ifndef VOLUMEBACKUP_BACKUPTASK_HEADER
#define VOLUMEBACKUP_BACKUPTASK_HEADER

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <exception>
#include <vector>

#include "VolumeProtectMacros.h"
#include "VolumeProtector.h"
#include "VolumeProtectTaskContext.h"
#include "VolumeUtils.h"

namespace volumeprotect {

class VOLUMEPROTECT_API VolumeBackupTask : public VolumeProtectTask {
public:
    using SessionQueue = std::queue<VolumeTaskSession>;

    bool            Start() override;
    TaskStatistics  GetStatistics() const override;

    VolumeBackupTask(const VolumeBackupConfig& backupConfig, uint64_t volumeSize);
    ~VolumeBackupTask();
private:
    bool Prepare(); // split session and save meta
    void ThreadFunc();
    bool StartBackupSession(std::shared_ptr<VolumeTaskSession> session) const;
    virtual bool InitBackupSessionContext(std::shared_ptr<VolumeTaskSession> session) const;
    bool IsIncrementBackup() const;
    void UpdateRunningSessionStatistics(std::shared_ptr<VolumeTaskSession> session);
    void UpdateCompletedSessionStatistics(std::shared_ptr<VolumeTaskSession> session);
    void SaveSessionWriterBitmap(std::shared_ptr<VolumeTaskSession> session);
    void InitWriterBitmap(std::shared_ptr<VolumeTaskSession> session);
    virtual bool SaveVolumeCopyMeta(const std::string& copyMetaDirPath, const VolumeCopyMeta& volumeCopyMeta);

private:
    uint64_t                                m_volumeSize;
    std::shared_ptr<VolumeBackupConfig>     m_backupConfig;

    std::thread     m_thread;
    SessionQueue    m_sessionQueue;

    // statistics
    mutable std::mutex m_statisticMutex;
    TaskStatistics  m_currentSessionStatistics;     // current running session statistics
    TaskStatistics  m_completedSessionStatistics;   // statistic sum of all completed session
};

}

#endif