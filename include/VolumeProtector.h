
/**
 * @file VolumeProtector.h
 * @brief Volume backup/restore facade.
 * @copyright Copyright 2023-2024 XUranus. All rights reserved.
 * @license This project is released under the Apache License.
 * @author XUranus(2257238649wdx@gmail.com)
 */

#ifndef VOLUMEBACKUP_PROTECT_FACADE_HEADER
#define VOLUMEBACKUP_PROTECT_FACADE_HEADER

#include "common/VolumeProtectMacros.h"
#include <string>

/**
 * @brief volume backup/restore facade and common struct defines
 */

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

const std::string DEFAULT_VOLUME_COPY_NAME = "volumeprotect";

const std::string VOLUME_COPY_META_JSON_FILENAME_EXTENSION = ".volumecopy.meta.json";
const std::string SHA256_CHECKSUM_BINARY_FILENAME_EXTENSION = ".sha256.meta.bin";
const std::string COPY_DATA_BIN_FILENAME_EXTENSION = ".copydata.bin";
const std::string COPY_DATA_BIN_PARTED_FILENAME_EXTENSION = ".copydata.bin.part";
const std::string COPY_DATA_IMAGE_FILENAME_EXTENSION = ".copydata.img";
const std::string COPY_DATA_VHD_FILENAME_EXTENSION = ".copydata.vhd";
const std::string COPY_DATA_VHDX_FILENAME_EXTENSION = ".copydata.vhdx";
const std::string WRITER_BITMAP_FILENAME_EXTENSION = ".checkpoint.bin";

// define error codes used by backup/restore tasks
const ErrCodeType VOLUMEPROTECT_ERR_SUCCESS                 = 0x00000000;   // no error
const ErrCodeType VOLUMEPROTECT_ERR_VOLUME_ACCESS_DENIED    = 0x00114514;   // read/volume failed with permission error
const ErrCodeType VOLUMEPROTECT_ERR_COPY_ACCESS_DENIED      = 0x00114515;   // read/write copy data access denied
const ErrCodeType VOLUMEPROTECT_ERR_NO_SPACE                = 0x00114516;   // write copy data failed for no space left
const ErrCodeType VOLUMEPROTECT_ERR_INVALID_VOLUME          = 0x00114517;   // not a valid volume block device

/**
 * @brief Used to specify backup type : full backup or forever increment backup
 */
enum class VOLUMEPROTECT_API BackupType {
    FULL = 0,           ///< the copy is generated by full backup
    FOREVER_INC = 1     ///< the copy is generated by forever increment backup
};

/**
 * @brief Used to sepecify which format to use for generating copy
 */
enum class VOLUMEPROTECT_API CopyFormat {
    BIN = 0,            ///< sector-by-sector *.bin/*.bin.partX file with no header (allow fragmentation)
    IMAGE = 1,          ///< sector-by-sector *.img file with no header (force one fragmentation)
#ifdef _WIN32
    VHD_FIXED = 2,      ///< fixed *.vhd file, no size limit (force one fragmentation)
    VHD_DYNAMIC = 3,    ///< dynamic *.vhd file, limit volume size to 2040GB (force one fragmentation)
    VHDX_FIXED = 4,     ///< fixed *.vhdx file, limit volume size to 64TB (force one fragmentation)
    VHDX_DYNAMIC = 5    ///< dyamic *.vhdx file, limit volume size to 64TB (force one fragmentation)
#endif
};

/**
 * @brief Defines structs for volume backup/restore task
 */
namespace task {

/**
 * @brief Immutable config, used to build volume backup task
 */
struct VOLUMEPROTECT_API VolumeBackupConfig {
    BackupType      backupType      { BackupType::FULL };   ///< type of target copy to be generated
    CopyFormat      copyFormat      { CopyFormat::BIN };    ///< format of target copy to be generated
    std::string     copyName        { DEFAULT_VOLUME_COPY_NAME }; ///< a unique name is required for each copy
    std::string     volumePath;                             ///< path of the block device (volume)
    std::string     prevCopyMetaDirPath;                    ///< [optional] only be needed for increment backup
    std::string	    outputCopyDataDirPath;                  ///< output directory path for generating copy data
    std::string	    outputCopyMetaDirPath;                  ///< output directory path for generating copy meta
    // optional advance params
    uint32_t        blockSize       { DEFAULT_BLOCK_SIZE };  ///< [optional] default blocksize used for checksum
    uint64_t        sessionSize     { DEFAULT_SESSION_SIZE };///< default sesson size used to split session
    uint32_t        hasherNum       { DEFAULT_HASHER_NUM };  ///< hasher worker count, set to the num of processors
    bool            hasherEnabled   { true };                ///< if set to false, won't compute checksum
    bool            enableCheckpoint{ true };                ///< start from checkpoint if exists
    std::string     checkpointDirPath;                       ///< directory path where checkpoint stores at
    bool            clearCheckpointsOnSucceed { true };      ///< if clear checkpoint files on succeed
    bool            skipEmptyBlock  { false };               ///< use sparsefile and skip zero block to save storage
};

/**
 * @brief Immutable config, used to build volume restore task
 */
struct VOLUMEPROTECT_API VolumeRestoreConfig {
    std::string     volumePath;                                     ///< path of the block device (volume) to restore
    std::string     copyName         { DEFAULT_VOLUME_COPY_NAME };  ///< required to select the copy to restore
    std::string	    copyDataDirPath;                                ///< directory path where copy data stores at
    std::string	    copyMetaDirPath;                                ///< directory path where copy meta stores at
    bool            enableCheckpoint { true };                      ///< start from checkpoint if exists
    std::string     checkpointDirPath;                              ///< directory path where checkpoint stores at
    bool            clearCheckpointsOnSucceed { true };             ///< if clear checkpoint files on succeed
    bool            enableZeroCopy { false };                       ///< use zero copy optimization for CopyFormat::IMAGE restore
};

/**
 * @brief Enumerate task status for volume backup/restore task
 */
enum class VOLUMEPROTECT_API TaskStatus {
    INIT        =  0,
    RUNNING     =  1,
    SUCCEED     =  2,
    ABORTING    =  3,
    ABORTED     =  4,
    FAILED      =  5
};

/**
 * @brief Used for statistics of volume backup/restore task
 */
struct VOLUMEPROTECT_API TaskStatistics {
    uint64_t bytesToRead    { 0 };
    uint64_t bytesRead      { 0 };
    uint64_t blocksToHash   { 0 };
    uint64_t blocksHashed   { 0 };
    uint64_t bytesToWrite   { 0 };
    uint64_t bytesWritten   { 0 };

    TaskStatistics operator + (const TaskStatistics& statistic) const;
};

/**
 * @brief Describe a task with state, used as base class of VolumeProtectTask
 */
class VOLUMEPROTECT_API StatefulTask {
public:
    ///< Abort a running task
    void        Abort();
    ///< Get TaskStatus enum of current task
    TaskStatus  GetStatus() const;
    ///< Check if current task is failed
    bool        IsFailed() const;
    ///< Check if current task is terminated (succeed, aborted or failed)
    bool        IsTerminated() const;
    ///< Get literal string of task status
    std::string GetStatusString() const;
    ///< Throw exception if current task is not started
    void        AssertTaskNotStarted();
    ///< Get volume backup/restore task error code
    ErrCodeType GetErrorCode() const;

protected:
    TaskStatus      m_status    { TaskStatus::INIT };
    bool            m_abort     { false };  ///< Set this field `true` to terminate task into status `Aborted`
    bool            m_failed    { false };  ///< Set this field `true` to terminate task into status `Failed`
    ErrCodeType     m_errorCode { VOLUMEPROTECT_ERR_SUCCESS };
};

/**
 * @brief Base class of VolumeBackupTask and VolumeRestoreTask
 */
class VOLUMEPROTECT_API VolumeProtectTask : public StatefulTask {
public:
    ///< Start the task asynchronizely, return true if start succeed
    virtual bool            Start() = 0;
    ///< Get current statictic info of current running task
    virtual TaskStatistics  GetStatistics() const = 0;

    virtual ~VolumeProtectTask() = default;

    /**
     * @brief Builder function to build a backup task using specified backup config
     * @param backupConfig
     * @return a valid `std::unique_ptr<VolumeProtectTask>` ptr if succeed
     * @return `nullptr` if failed
     */
    static std::unique_ptr<VolumeProtectTask> BuildBackupTask(const VolumeBackupConfig& backupConfig);

    /**
     * @brief Builder function to build a restore task using specified backup config
     * @param backupConfig
     * @return a valid `std::unique_ptr<VolumeProtectTask>` ptr if succeed
     * @return `nullptr` if failed
     */
    static std::unique_ptr<VolumeProtectTask> BuildRestoreTask(const VolumeRestoreConfig& restoreConfig);
};

}
}

#ifdef __cplusplus
extern "C" {
#endif

// C style definitions of structs and functions to provide interface for cpython extension

struct VOLUMEPROTECT_API VolumeBackupConf_C {
    int         backupType;                     ///< type of target copy to be generated
    int         copyFormat;
    char*       copyName;
    char*       volumePath;                     ///< path of the block device (volume)
    char*       prevCopyMetaDirPath;            ///< [optional] only be needed for increment backup
    char*	    outputCopyDataDirPath;
    char*	    outputCopyMetaDirPath;
    uint32_t    blockSize;                      ///< [optional] default blocksize used for computing sha256
    uint64_t    sessionSize;                    ///< default sesson size used to split session
    uint32_t    hasherNum;                      ///< hasher worker count, set to the num of processors
    bool        hasherEnabled;                  ///< if set to false, won't compute sha256
    bool        enableCheckpoint;               ///< start from checkpoint if exists
};

struct VOLUMEPROTECT_API VolumeRestoreConf_C {
    char*       volumePath;                     ///< path of the block device (volume)
    char*       copyName;
    char*	    copyDataDirPath;
    char*	    copyMetaDirPath;
    bool        enableCheckpoint { true };      ///< start from checkpoint if exists
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

VOLUMEPROTECT_API int                 GetTaskErrorCode(void* task);

VOLUMEPROTECT_API bool                IsTaskFailed(void* task);

VOLUMEPROTECT_API bool                IsTaskTerminated(void* task);

#ifdef __cplusplus
}
#endif

#endif