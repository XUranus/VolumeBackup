#ifndef VOLUMEBACKUP_RESTORE_TASK_HEADER
#define VOLUMEBACKUP_RESTORE_TASK_HEADER

#include "VolumeProtector.h"
#include "VolumeProtectTaskContext.h"
#include "native/TaskResourceManager.h"
#include "VolumeUtils.h"

namespace volumeprotect {
namespace task {

/**
 * @brief Control control volume restore procedure
 */
class VolumeRestoreTask
    : public VolumeProtectTask, public TaskStatisticTrait, public VolumeTaskCheckpointTrait {
public:
    using SessionQueue = std::queue<VolumeTaskSession>;

    bool            Start() override;

    TaskStatistics  GetStatistics() const override;

    VolumeRestoreTask(const VolumeRestoreConfig& restoreConfig, const VolumeCopyMeta& volumeCopyMeta);

    ~VolumeRestoreTask();

private:
    bool Prepare(); // split session and save meta

    void ThreadFunc();

    bool StartRestoreSession(std::shared_ptr<VolumeTaskSession> session) const;

    virtual bool InitRestoreSessionContext(std::shared_ptr<VolumeTaskSession> session) const;

    virtual bool InitRestoreSessionTaskExecutor(std::shared_ptr<VolumeTaskSession> session) const;

protected:
    uint64_t                                m_volumeSize;
    std::shared_ptr<VolumeRestoreConfig>    m_restoreConfig;
    std::shared_ptr<VolumeCopyMeta>         m_volumeCopyMeta;

    std::thread     m_thread;
    SessionQueue    m_sessionQueue;
    std::shared_ptr<TaskResourceManager> m_resourceManager;
};

}
}

#endif