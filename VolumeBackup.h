
#ifndef VOLUME_BACKUP_FACADE_H
#define VOLUME_BACKUP_FACADE_H

#include <string>
#include <cstdint>
#include <thread>
#include <queue>
#include <memory>

#include "VolumeBackupSession."

// volume backup application facade
namespace volumebackup {

enum class TaskStatus {
    INIT        =  0,
    RUNNING     =  1,
    SUCCEED     =  2,
    ABORTING    =  3,
    ABORTED     =  4,
    FAILED      =  5
};

class VolumeBackupTask {
public:
    std::shared_ptr<VolumeBackupTask> BuildFullBackupTask(
	const std::string& 	blockDevicePath,
	const std::string&	outputCopyDataDirPath,
	const std::string&	outputCopyMetaDirPath);

    ~VolumeBackupTask();

    bool Start();
    bool IsTerminated();
    TaskStatus GetStatus();
    bool Abort();

private:
    VolumeBackupTask(
        const std::string blockDevicePath,
        uint64_t volumeSize,
	    const std::string outputCopyDataDirPath,
	    const std::string outputCopyMetaDirPath
    );
    bool Prepare(); // split session and save meta
    void ThreadFunc();
     

private:
    std::string 	m_blockDevicePath;
	std::string	    m_outputCopyDataDirPath;
	std::string 	m_outputCopyMetaDirPath;
    uint64_t        m_volumeSize;

    std::thread     m_thread;
    bool            m_abort { false }; // if aborted is invoked
    TaskStatus      m_status { TaskStatus::INIT };

    std::queue<volumebackup::VolumeBackupSession> m_sessionQueue;
};

}

#endif