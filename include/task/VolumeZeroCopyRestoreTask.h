/**
 * @file VolumeZeroCopyRestoreTask.h
 * @brief Provide zero copy implement for CopyFormat::IMAGE copy restoration.
 * @copyright Copyright 2023 XUranus. All rights reserved.
 * @license This project is released under the Apache License.
 * @author XUranus(2257238649wdx@gmail.com)
 */

#ifndef VOLUMEBACKUP_ZERO_COPY_RESTORE_TASK_HEADER
#define VOLUMEBACKUP_ZERO_COPY_RESTORE_TASK_HEADER

#include "VolumeProtector.h"
#include "VolumeProtectTaskContext.h"
#include "native/TaskResourceManager.h"
#include "VolumeUtils.h"
#include "native/RawIO.h"
#include <queue>

namespace volumeprotect {
namespace task {

/**
 * @brief Control control volume restore procedure
 */
class VolumeZeroCopyRestoreTask : public VolumeProtectTask, public TaskStatisticTrait {
public:
    using SessionQueue = std::queue<VolumeTaskSession>;

    bool            Start() override;

    TaskStatistics  GetStatistics() const override;

    VolumeZeroCopyRestoreTask(const VolumeRestoreConfig& restoreConfig, const VolumeCopyMeta& volumeCopyMeta);

    ~VolumeZeroCopyRestoreTask();

private:
    bool Prepare(); // split session and save meta

    void ThreadFunc();

    bool PerformZeroCopyRestore(
        std::shared_ptr<volumeprotect::rawio::RawDataReader> copyDataReader,
        std::shared_ptr<volumeprotect::rawio::RawDataWriter> volumeDataWriter,
        const VolumeTaskSharedConfig& sessionConfig);

protected:
    uint64_t                                            m_volumeSize;
    std::shared_ptr<VolumeRestoreConfig>                m_restoreConfig;
    std::shared_ptr<VolumeCopyMeta>                     m_volumeCopyMeta;
    std::queue<VolumeTaskSharedConfig>                  m_sessionQueue;
    std::thread                                         m_thread;

    std::shared_ptr<TaskResourceManager>                m_resourceManager;
    std::vector<std::string>                            m_checkpointFiles;
};

}
}

#endif