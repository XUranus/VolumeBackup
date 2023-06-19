
#ifndef VOLUME_PROTECT_FACADE_H
#define VOLUME_PROTECT_FACADE_H

#include <string>
#include <cstdint>
#include <thread>
#include <queue>
#include <memory>

// volume backup application facade
namespace volumeprotect {

const uint64_t ONE_KB = 1024;
const uint64_t ONE_MB = 1024 * ONE_KB;
const uint64_t ONE_GB = 1024 * ONE_MB;
const uint64_t ONE_TB = 1024 * ONE_GB;

const uint32_t DEFAULT_BLOCK_SIZE = 4 * ONE_MB;
const uint64_t DEFAULT_SESSION_SIZE = ONE_TB;
const uint32_t DEFAULT_HASHER_NUM = 8;

/*
 *volume backup/restore facade and common struct defines
 */

enum class CopyType {
    FULL,       // the copy contains a full volume
    INCREMENT   // the copy is increment copy
};

// immutable config, used to build volume full/increment backup task
struct VolumeBackupConfig {
    CopyType        copyType;                               // type of target copy to be generated
    std::string     blockDevicePath;                        // path of the block device (volume)
    std::string     prevCopyMetaDirPath;                    // [optional] only be needed for increment backup
    std::string	    outputCopyDataDirPath;
    std::string	    outputCopyMetaDirPath;
    uint32_t        blockSize { DEFAULT_BLOCK_SIZE };       // [optional] default block size used for computing checksum
    uint64_t        sessionSize { DEFAULT_SESSION_SIZE };   // default sesson size used to split session
    uint64_t        hasherNum { DEFAULT_HASHER_NUM };       // hasher worker count, recommended set to the num of processors
    bool            hasherEnabled { true };                 // if set to false, won't compute checksum
};

// immutable config, used to build volume restore task
struct VolumeRestoreConfig {
    std::string     blockDevicePath;                        // path of the block device (volume)
    std::string	    copyDataDirPath;
    std::string	    copyMetaDirPath;
};

enum class TaskStatus {
    INIT        =  0,
    RUNNING     =  1,
    SUCCEED     =  2,
    ABORTING    =  3,
    ABORTED     =  4,
    FAILED      =  5
};

struct TaskStatistics {
    uint64_t bytesToRead;
    uint64_t bytesRead;
    uint64_t blocksToHash;
    uint64_t blocksHashed;
    uint64_t bytesToWrite;
    uint64_t bytesWritten;
};

class StatefulTask {
public:
    void Abort();
    TaskStatus GetStatus() const;
    bool IsFailed() const;
    bool IsTerminated() const;

protected:
    TaskStatus  m_status { TaskStatus::INIT };
    bool        m_abort { false };
};

class VolumeTaskBase {
public:
    virtual bool            Start() = 0;
    virtual bool            IsTerminated() const = 0;
    virtual TaskStatus      GetStatus() const = 0;
    virtual void            Abort() = 0;
    virtual TaskStatistics  GetStatistics() const = 0;
};

class VolumeBackupTask : public VolumeTaskBase {
public:
    static std::shared_ptr<VolumeBackupTask> BuildBackupTask(const VolumeBackupConfig& backupConfig);
};

class VolumeRestoreTask : public VolumeTaskBase {
public:
    static std::shared_ptr<VolumeRestoreTask> BuildRestoreTask(const VolumeBackupConfig& backupConfig);
};

}

#endif