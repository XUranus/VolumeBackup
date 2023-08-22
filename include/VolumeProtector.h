
#ifndef VOLUMEBACKUP_PROTECT_FACADE_HEADER
#define VOLUMEBACKUP_PROTECT_FACADE_HEADER

#include "VolumeProtectMacros.h"
#include <memory>

// volume backup application facade
namespace volumeprotect {

const uint64_t ONE_KB = 1024LLU;
const uint64_t ONE_MB = 1024LLU * ONE_KB;
const uint64_t ONE_GB = 1024LLU * ONE_MB;
const uint64_t ONE_TB = 1024LLU * ONE_GB;

const uint32_t DEFAULT_BLOCK_SIZE = 4LU * ONE_MB;
const uint64_t DEFAULT_SESSION_SIZE = ONE_TB;
const uint32_t DEFAULT_HASHER_NUM = 8LU;
const uint32_t DEFAULT_ALLOCATOR_BLOCK_NUM = 32; // 128MB
const uint32_t DEFAULT_QUEUE_SIZE = 64;
const uint32_t SHA256_CHECKSUM_SIZE = 32; // 256bits

/*
 * volume backup/restore facade and common struct defines
 */

enum class VOLUMEPROTECT_API CopyType {
    FULL = 0,       // the copy contains a full volume
    INCREMENT = 1   // the copy is increment copy
};

// immutable config, used to build volume full/increment backup task
struct VOLUMEPROTECT_API VolumeBackupConfig {
    CopyType        copyType        { CopyType::FULL };     // type of target copy to be generated
    std::string     volumePath;                             // path of the block device (volume)
    std::string     prevCopyMetaDirPath;                    // [optional] only be needed for increment backup
    std::string	    outputCopyDataDirPath;
    std::string	    outputCopyMetaDirPath;
    uint32_t        blockSize       { DEFAULT_BLOCK_SIZE };  // [optional] default blocksize used for computing sha256
    uint64_t        sessionSize     { DEFAULT_SESSION_SIZE };// default sesson size used to split session
    uint32_t        hasherNum       { DEFAULT_HASHER_NUM };  // hasher worker count, set to the num of processors
    bool            hasherEnabled   { true };                // if set to false, won't compute sha256
    bool            enableCheckpoint{ true };                // start from checkpoint if exists
};

// immutable config, used to build volume restore task
struct VOLUMEPROTECT_API VolumeRestoreConfig {
    std::string     volumePath;                             // path of the block device (volume)
    std::string	    copyDataDirPath;
    std::string	    copyMetaDirPath;
    bool            enableCheckpoint { true };              // start from checkpoint if exists
};

enum class VOLUMEPROTECT_API TaskStatus {
    INIT        =  0,
    RUNNING     =  1,
    SUCCEED     =  2,
    ABORTING    =  3,
    ABORTED     =  4,
    FAILED      =  5
};

struct VOLUMEPROTECT_API TaskStatistics {
    uint64_t bytesToRead    { 0 };
    uint64_t bytesRead      { 0 };
    uint64_t blocksToHash   { 0 };
    uint64_t blocksHashed   { 0 };
    uint64_t bytesToWrite   { 0 };
    uint64_t bytesWritten   { 0 };

    TaskStatistics operator+ (const TaskStatistics& taskStatistics) const;
};

class VOLUMEPROTECT_API StatefulTask {
public:
    void        Abort();
    TaskStatus  GetStatus() const;
    bool        IsFailed() const;
    bool        IsTerminated() const;
    std::string GetStatusString() const;
    void        AssertTaskNotStarted();

protected:
    TaskStatus  m_status { TaskStatus::INIT };
    bool        m_abort { false };
};

// base class of VolumeBackupTask and VolumeRestoreTask
class VOLUMEPROTECT_API VolumeProtectTask : public StatefulTask {
public:
    virtual bool            Start() = 0;
    virtual TaskStatistics  GetStatistics() const = 0;
    virtual ~VolumeProtectTask() = default;

    static std::unique_ptr<VolumeProtectTask> BuildBackupTask(const VolumeBackupConfig& backupConfig);
    static std::unique_ptr<VolumeProtectTask> BuildRestoreTask(const VolumeRestoreConfig& restoreConfig);
};

}

#ifdef __cplusplus
extern "C" {
#endif

// C style struct definition
struct VOLUMEPROTECT_API VolumeBackupConf_C {
    int         copyType;                       // type of target copy to be generated
    char*       volumePath;                     // path of the block device (volume)
    char*       prevCopyMetaDirPath;            // [optional] only be needed for increment backup
    char*	    outputCopyDataDirPath;
    char*	    outputCopyMetaDirPath;
    uint32_t    blockSize;                      // [optional] default blocksize used for computing sha256
    uint64_t    sessionSize;                    // default sesson size used to split session
    uint32_t    hasherNum;                      // hasher worker count, set to the num of processors
    bool        hasherEnabled;                  // if set to false, won't compute sha256
    bool        enableCheckpoint;               // start from checkpoint if exists
};

struct VOLUMEPROTECT_API VolumeRestoreConf_C {
    char*       volumePath;                     // path of the block device (volume)
    char*	    copyDataDirPath;
    char*	    copyMetaDirPath;
    bool        enableCheckpoint { true };      // start from checkpoint if exists
};

enum VOLUMEPROTECT_API TaskStatus_C {
    INIT        =  0,
    RUNNING     =  1,
    SUCCEED     =  2,
    ABORTING    =  3,
    ABORTED     =  4,
    FAILED      =  5
};

struct VOLUMEPROTECT_API TaskStatistics_C {
    uint64_t bytesToRead;
    uint64_t bytesRead;
    uint64_t blocksToHash;
    uint64_t blocksHashed;
    uint64_t bytesToWrite;
    uint64_t bytesWritten;
};

VOLUMEPROTECT_API void*               BuildBackupTask(VolumeBackupConf_C backupConfig);
VOLUMEPROTECT_API void*               BuildRestoreTask(VolumeRestoreConf_C restoreConfig);
VOLUMEPROTECT_API bool                StartTask(void* task);
VOLUMEPROTECT_API void                DestroyTask(void* task);
VOLUMEPROTECT_API TaskStatistics_C    GetTaskStatistics(void* task);
VOLUMEPROTECT_API void                AbortTask(void* task);
VOLUMEPROTECT_API TaskStatus_C        GetTaskStatus(void* task);
VOLUMEPROTECT_API bool                IsTaskFailed(void* task);
VOLUMEPROTECT_API bool                IsTaskTerminated(void* task);
#ifdef __cplusplus
}
#endif

#endif