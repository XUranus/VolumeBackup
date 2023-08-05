#ifndef VOLUMEBACKUP_RESTORE_TASK_HEADER
#define VOLUMEBACKUP_RESTORE_TASK_HEADER
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

class VOLUMEPROTECT_API VolumeRestoreTask
    : public VolumeProtectTask, public TaskStatisticTrait, public VolumeTaskCheckpointTrait {
public:
    using SessionQueue = std::queue<VolumeTaskSession>;

    bool            Start() override;
    TaskStatistics  GetStatistics() const override;

    VolumeRestoreTask(const VolumeRestoreConfig& restoreConfig);
    ~VolumeRestoreTask();
private:
    bool Prepare(); // split session and save meta
    void ThreadFunc();
    bool StartRestoreSession(std::shared_ptr<VolumeTaskSession> session) const;
    virtual bool InitRestoreSessionContext(std::shared_ptr<VolumeTaskSession> session) const;
    virtual bool ReadVolumeCopyMeta(const std::string& copyMetaDirPath, VolumeCopyMeta& volumeCopyMeta);
private:
    uint64_t                                m_volumeSize;
    std::shared_ptr<VolumeRestoreConfig>    m_restoreConfig;

    std::thread     m_thread;
    SessionQueue    m_sessionQueue;
};

}

#endif