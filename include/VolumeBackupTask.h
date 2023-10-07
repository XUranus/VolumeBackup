#ifndef VOLUMEBACKUP_BACKUPTASK_HEADER
#define VOLUMEBACKUP_BACKUPTASK_HEADER

#include "VolumeProtectMacros.h"
#include "VolumeProtector.h"
#include "VolumeProtectTaskContext.h"
#include "TaskResourceManager.h"
#include "VolumeUtils.h"

namespace volumeprotect {

class VolumeBackupTask
    : public VolumeProtectTask, public TaskStatisticTrait, public VolumeTaskCheckpointTrait {
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
    
    virtual bool InitBackupSessionTaskExecutor(std::shared_ptr<VolumeTaskSession> session) const;
    
    bool IsIncrementBackup() const;
    
    void SaveSessionWriterBitmap(std::shared_ptr<VolumeTaskSession> session);
    
    VolumeTaskSession NewVolumeTaskSession(uint64_t sessionOffset, uint64_t sessionSize, int sessionIndex) const;
    
    bool InitHashingContext(std::shared_ptr<VolumeTaskSession> session) const;
    
    virtual bool LoadSessionPreviousCopyChecksum(std::shared_ptr<VolumeTaskSession> session) const;
    
    virtual bool SaveVolumeCopyMeta(
        const std::string& copyMetaDirPath,
        const std::string& copyName,
        const VolumeCopyMeta& volumeCopyMeta) const;
    
    virtual bool ValidateIncrementBackup() const;

private:
    uint64_t                                m_volumeSize;
    std::shared_ptr<VolumeBackupConfig>     m_backupConfig;

    std::thread         m_thread;
    SessionQueue        m_sessionQueue;
    std::shared_ptr<TaskResourceManager> m_resourceManager;
};

}

#endif