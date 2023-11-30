/**
 * @file VolumeProtectTaskContext.h
 * @brief Common context for both backup task and restore task.
 * @copyright Copyright 2023 XUranus. All rights reserved.
 * @license This project is released under the Apache License.
 * @author XUranus(2257238649wdx@gmail.com)
 */

#ifndef VOLUMEBACKUP_PROTECT_TASK_CONTEXT_HEADER
#define VOLUMEBACKUP_PROTECT_TASK_CONTEXT_HEADER

#include "VolumeProtectMacros.h"
#include "VolumeProtector.h"
#include "BlockingQueue.h"

namespace volumeprotect {
namespace task {

class VolumeBlockReader;
class VolumeBlockWriter;
class VolumeBlockHasher;

/**
 * @brief Struct to describle a volume data block in memory, used for hash/writer consuming
 */
struct VolumeConsumeBlock {
    uint8_t*        ptr;
    uint64_t        index;
    uint64_t        volumeOffset;
    uint32_t        length;
};

/**
 * @brief A fixed memory block allocator to improve `malloc` performance
 */
class VolumeBlockAllocator {
public:
    VolumeBlockAllocator(uint32_t blockSize, uint32_t blockNum);
    ~VolumeBlockAllocator();
    uint8_t*    BlockAlloc();
    void        BlockFree(uint8_t* ptr);

private:
    uint8_t*    m_pool;
    bool*       m_allocTable;
    uint32_t    m_blockSize;
    uint32_t    m_blockNum;
    std::mutex  m_mutex;
};

/**
 * @brief Used for concurrent data statistics for a session
 */
struct SessionCounter {
    std::atomic<uint64_t>   bytesToRead             { 0 };
    std::atomic<uint64_t>   bytesRead               { 0 };
    std::atomic<uint64_t>   blocksToHash            { 0 };
    std::atomic<uint64_t>   blocksHashed            { 0 };
    std::atomic<uint64_t>   bytesToWrite            { 0 };
    std::atomic<uint64_t>   bytesWritten            { 0 };
    std::atomic<uint64_t>   blockesWriteFailed      { 0 };
};

/**
 * @brief Manage the checksum table of previous/latest hashing checksum
 */
struct BlockHashingContext {
    uint64_t    lastestSize     { 0 }; // size in bytes
    uint64_t    previousSize    { 0 };
    uint8_t*    lastestTable    { nullptr };
    uint8_t*    previousTable   { nullptr };

    BlockHashingContext(uint64_t pSize, uint64_t lSize);
    BlockHashingContext(uint64_t lSize);
    ~BlockHashingContext();
};

/**
 * @brief A dynamic version of std::bitset, used to record index of block written.
 * for 1TB session, max blocks cnt 262144, max bitmap size = 32768 bytes
 */
class Bitmap {
public:
    explicit Bitmap(uint64_t size);
    Bitmap(uint8_t* ptr, uint64_t capacity);
    ~Bitmap();
    bool Test(uint64_t index) const;
    void Set(uint64_t index);
    uint64_t FirstIndexUnset() const;
    uint64_t Capacity() const;      // capacity in bytes
    uint64_t MaxIndex() const;
    uint64_t TotalSetCount() const;
    const uint8_t* Ptr() const;
private:
    uint64_t    m_capacity  { 0 };
    uint8_t*    m_table     { nullptr };
};

/**
 * Split a logical volume into multiple sessions.
 * Sach session(default 1TB) corresponding to a SHA256 checksum binary file(8MB) and a data slice file(1TB)
 * Each backup/restore task involves one or more sessions represented by struct VolumeTaskSession
 *
 *       ...      |<------session[i]------>|<-----session[i+1]----->|<-----session[i+2]----->|   ...
 * |===================================================================================================| logical volume
 * |     ...      |----- sessionSize ------|
 * 0         sessionOffset   sessionOffset + sessionSize
 */

/**
 * @brief A immutable configuration struct for a backup/restore session
 */
struct VolumeTaskSharedConfig {
    // immutable fields (common)
    uint64_t        sessionOffset;
    uint64_t        sessionSize;
    uint32_t        blockSize;
    bool            hasherEnabled;
    bool            checkpointEnabled;
    uint32_t        hasherWorkerNum;
    std::string     volumePath;
    std::string     copyFilePath;
    CopyFormat      copyFormat;

    // immutable fields (for backup)
    std::string     lastestChecksumBinPath;
    std::string     prevChecksumBinPath;
    std::string     checkpointFilePath;
    bool            skipEmptyBlock;
};


/**
 * @brief Snapshot of bitmap of a task
 */
struct CheckpointSnapshot {
    uint64_t    bitmapBufferBytesLength;     // mark single buffer length in bytes, all bitmap buffer share same length
    // buffer that only needed during backup/restore (all padding to zero during restore)
    uint8_t*    processedBitmapBuffer;      // mark blocks need to be written
    uint8_t*    writtenBitmapBuffer;        // mark blocks written to disk/copyfile

    CheckpointSnapshot(uint64_t length);
    ~CheckpointSnapshot();
    static std::shared_ptr<CheckpointSnapshot> LoadFrom(const std::string& filepath);
    bool SaveTo(const std::string& filepath) const;
};

/**
 * @brief Manage context of a volume backup/restore task.
 */
struct VolumeTaskSharedContext {
    // bitmap to implement checkpoint
    std::shared_ptr<Bitmap>                             processedBitmap         { nullptr };
    std::shared_ptr<Bitmap>                             writtenBitmap           { nullptr };

    std::shared_ptr<SessionCounter>                     counter                 { nullptr };
    std::shared_ptr<VolumeBlockAllocator>               allocator               { nullptr };
    std::shared_ptr<BlockingQueue<VolumeConsumeBlock>>  hashingQueue            { nullptr };
    std::shared_ptr<BlockingQueue<VolumeConsumeBlock>>  writeQueue              { nullptr };
    std::shared_ptr<BlockHashingContext>                hashingContext          { nullptr };
};

struct VolumeTaskSession {
    // stateful task component
    std::shared_ptr<VolumeBlockReader>          readerTask { nullptr };
    std::shared_ptr<VolumeBlockHasher>          hasherTask { nullptr };
    std::shared_ptr<VolumeBlockWriter>          writerTask { nullptr };

    std::shared_ptr<VolumeTaskSharedContext>    sharedContext { nullptr };
    std::shared_ptr<VolumeTaskSharedConfig>     sharedConfig  { nullptr };

    uint64_t    TotalBlocks() const;
    uint64_t    MaxIndex() const;
    bool        IsTerminated() const;
    bool        IsFailed() const;
    void        Abort() const;
    ErrCodeType GetErrorCode() const;
};

/**
 * @brief TaskStatisticTrait provides the trait of calculate all stats of running/completed sessions
 */
class TaskStatisticTrait {
protected:
    void UpdateRunningSessionStatistics(std::shared_ptr<VolumeTaskSession> session);
    void UpdateCompletedSessionStatistics(std::shared_ptr<VolumeTaskSession> session);
protected:
    mutable std::mutex m_statisticMutex;
    TaskStatistics  m_currentSessionStatistics;     // current running session statistics
    TaskStatistics  m_completedSessionStatistics;   // statistic sum of all completed session
};

/**
 * @brief VolumeTaskCheckpointTrait provides the trait of managing checkpoint
 *  checkpoint feature will be enabled by set "enableCheckpoint" option in BackupConfig/RestoreConfig,
 *  checkpoint file include checksum file and writerBitmap file,
 *  which records checksum info computed and block data that have been written to disk.
 *  Both two file will also be generated no matter "enableCheckpoint" is true or false,
 *  "enableCheckpoint" will only decide if to restore the task if process is restarted.
 */
class VolumeTaskCheckpointTrait {
    using SessionPtr = std::shared_ptr<VolumeTaskSession>;
    using CheckpointSnapshotPtr = std::shared_ptr<CheckpointSnapshot>;
protected:
    // refresh and save checkpoint
    void RefreshSessionCheckpoint(SessionPtr session);
    bool FlushSessionLatestHashingTable(SessionPtr session) const;
    bool FlushSessionWriter(SessionPtr session) const;
    bool FlushSessionBitmap(SessionPtr session) const;
    // common utils
    virtual bool IsSessionRestarted(SessionPtr session) const;
    bool IsCheckpointEnabled(SessionPtr session) const;
    void InitSessionBitmap(SessionPtr session) const;
    CheckpointSnapshotPtr TakeSessionCheckpointSnapshot(SessionPtr session) const;
    // read and restore checkpoints
    void RestoreSessionCheckpoint(std::shared_ptr<VolumeTaskSession> session) const;
    bool RestoreSessionLatestHashingTable(std::shared_ptr<VolumeTaskSession> session) const;
    bool RestoreSessionBitmap(std::shared_ptr<VolumeTaskSession> session) const;
    void RestoreSessionCounter(std::shared_ptr<VolumeTaskSession> session) const;

    virtual std::shared_ptr<CheckpointSnapshot> ReadCheckpointSnapshot(
        std::shared_ptr<VolumeTaskSession> session) const;
    virtual bool ReadLatestHashingTable(std::shared_ptr<VolumeTaskSession> session) const;

private:
    bool CheckLastUpdateTimer();

private:
    std::chrono::steady_clock::time_point   m_lastUpdate { std::chrono::steady_clock::now() };
};

}
}

#endif