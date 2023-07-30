

#ifndef VOLUMEBACKUP_PROTECT_TASK_CONTEXT_HEADER
#define VOLUMEBACKUP_PROTECT_TASK_CONTEXT_HEADER

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <cstdio>

#include "VolumeProtectMacros.h"
#include "VolumeProtector.h"
#include "BlockingQueue.h" 
#include "CheckpointManager.h"

namespace volumeprotect {

class VolumeBlockReader;
class VolumeBlockWriter;
class VolumeBlockHasher;

// commpound struct used for hash/writer consuming
struct VOLUMEPROTECT_API VolumeConsumeBlock {
    char*           ptr;
    uint64_t        index;
    uint64_t        volumeOffset;
    uint32_t        length;
};

// a fixed block allocator
class VOLUMEPROTECT_API VolumeBlockAllocator {
public:
    VolumeBlockAllocator(uint32_t blockSize, uint32_t blockNum);
    ~VolumeBlockAllocator();
    char*       bmalloc();
    void        bfree(char* ptr);

private:
    char*       m_pool;
    bool*       m_allocTable;
    uint32_t    m_blockSize;
    uint32_t    m_blockNum;
    std::mutex  m_mutex;
};

struct VOLUMEPROTECT_API SessionCounter {
    std::atomic<uint64_t>   bytesToRead     { 0 };
    std::atomic<uint64_t>   bytesRead       { 0 };
    std::atomic<uint64_t>   blocksToHash    { 0 };
    std::atomic<uint64_t>   blocksHashed    { 0 };
    std::atomic<uint64_t>   bytesToWrite    { 0 };
    std::atomic<uint64_t>   bytesWritten    { 0 };
};

/**
 * @brief A dynamic version of std::bitset, used to record index of block written.
 * for 1TB session, max blocks cnt 262144, max bitmap size = 32768 bytes
 */
class Bitmap {
public:
    explicit Bitmap(uint64_t size);
    Bitmap(std::unique_ptr<char[]> ptr, uint64_t capacity);
    bool Test(uint64_t index) const;
    void Set(uint64_t index);
    uint64_t FirstIndexUnset() const;
    uint64_t Capacity() const;      // capacity in bytes
    uint64_t MaxIndex() const;
    const uint8_t* Ptr() const;
private:
    std::unique_ptr<uint8_t[]>      m_table     { nullptr };
    uint64_t                        m_capacity  { 0 };
};

/**
 * Split a logical volume into multiple sessions
 * Sach session(default 1TB) corresponding to a SHA256 checksum binary file(8MB) and a data slice file(1TB)
 * Each backup/restore task involves one or more sessions represented by struct VolumeTaskSession
 *
 *       ...      |<------session[i]------>|<-----session[i+1]----->|<-----session[i+2]----->|   ...
 * |===================================================================================================| logical volume
 * |     ...      |----- sessionSize ------|
 * 0         sessionOffset   sessionOffset + sessionSize
 */

struct VOLUMEPROTECT_API VolumeTaskSharedConfig {
    // immutable fields (common)
    uint64_t        sessionOffset;
    uint64_t        sessionSize;
    uint32_t        blockSize;
    bool            hasherEnabled;
    bool            checkpointEnabled;
    uint32_t        hasherWorkerNum;
    std::string     volumePath;
    std::string     copyFilePath;

    // immutable fields (for backup)
    std::string     lastestChecksumBinPath;
    std::string     prevChecksumBinPath;
};

struct VOLUMEPROTECT_API VolumeTaskSharedContext {
    // shared container context
    std::shared_ptr<Bitmap>                             writerBitmap            { nullptr };
    std::shared_ptr<SessionCounter>                     counter                 { nullptr };
    std::shared_ptr<VolumeBlockAllocator>               allocator               { nullptr };
    std::shared_ptr<BlockingQueue<VolumeConsumeBlock>>  hashingQueue            { nullptr };
    std::shared_ptr<BlockingQueue<VolumeConsumeBlock>>  writeQueue              { nullptr };
};

struct VOLUMEPROTECT_API VolumeTaskSession {
    // stateful task component
    std::shared_ptr<VolumeBlockReader>          readerTask { nullptr };
    std::shared_ptr<VolumeBlockHasher>          hasherTask { nullptr };
    std::shared_ptr<VolumeBlockWriter>          writerTask { nullptr };

    std::shared_ptr<VolumeTaskSharedContext>    sharedContext { nullptr };
    std::shared_ptr<VolumeTaskSharedConfig>     sharedConfig  { nullptr };

    bool IsTerminated() const;
    bool IsFailed() const;
    void Abort() const;
};

}

#endif