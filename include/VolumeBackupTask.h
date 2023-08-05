#ifndef VOLUMEBACKUP_BACKUPTASK_HEADER
#define VOLUMEBACKUP_BACKUPTASK_HEADER

#include "VolumeProtectMacros.h"
#include "VolumeProtector.h"
#include "VolumeProtectTaskContext.h"
#include "VolumeUtils.h"

namespace volumeprotect {

class VOLUMEPROTECT_API VolumeBackupTask
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
    bool IsIncrementBackup() const;
    void SaveSessionWriterBitmap(std::shared_ptr<VolumeTaskSession> session);
    bool InitHashingContext(std::shared_ptr<VolumeTaskSession> session) const;
    virtual bool SaveVolumeCopyMeta(const std::string& copyMetaDirPath, const VolumeCopyMeta& volumeCopyMeta);

private:
    uint64_t                                m_volumeSize;
    std::shared_ptr<VolumeBackupConfig>     m_backupConfig;

    std::thread     m_thread;
    SessionQueue    m_sessionQueue;
};

}

#endif