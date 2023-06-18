

#ifndef VOLUME_BACKUP_CONTEXT_H
#define VOLUME_BACKUP_CONTEXT_H

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <cstdio>

#include "VolumeBackup.h"
#include "BlockingQueue.h"

namespace volumebackup {

class VolumeBlockReader;
class VolumeBlockWriter;
class VolumeBlockHasher;

// commpound struct used for hash/writer consuming
struct VolumeConsumeBlock {
    char*           ptr;
    uint64_t        volumeOffset;
    uint32_t        length;
};

// a fixed block allocator
class VolumeBlockAllocator {
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

struct SessionCounter {
    std::atomic<uint64_t>   bytesToRead     { 0 };
    std::atomic<uint64_t>   bytesRead       { 0 };
    std::atomic<uint64_t>   blocksToHash    { 0 };
    std::atomic<uint64_t>   blocksHashed    { 0 };
    std::atomic<uint64_t>   bytesToWrite    { 0 };
    std::atomic<uint64_t>   bytesWritten    { 0 };
};

/*
 * |                    |<-------session------>|
 * |==========================================================| logical volume
 * |        ...         |                      |    
 * 0              sessionOffset      sessionOffset + sessionSize
 *
 * each VolumeBackupSession corresponding to a volume <===> volume.part file backup session 
 */
struct VolumeBackupSession {
    // immutable fields
    std::shared_ptr<VolumeBackupConfig> config;

    uint64_t        sessionOffset;
    uint64_t        sessionSize;
    std::string     lastestChecksumBinPath;
    std::string     prevChecksumBinPath;
    std::string     copyFilePath;
    //uint64_t        copyFileMappingOffset;

    // mutable fields
    std::shared_ptr<VolumeBlockReader> reader { nullptr };
    std::shared_ptr<VolumeBlockHasher> hasher { nullptr };
    std::shared_ptr<VolumeBlockWriter> writer { nullptr };

    // shared container context
    std::shared_ptr<SessionCounter>                     counter { nullptr };
    std::shared_ptr<VolumeBlockAllocator>               allocator { nullptr };
    std::shared_ptr<BlockingQueue<VolumeConsumeBlock>>  hashingQueue { nullptr };
    std::shared_ptr<BlockingQueue<VolumeConsumeBlock>>  writeQueue { nullptr };

    bool IsTerminated() const;
    bool IsFailed() const;
    void Abort();
};

struct VolumeRestoreSession {
    std::string     blockDevicePath;
    uint64_t        sessionOffset;
    uint64_t        sessionSize;
    std::string     copyFilePath;
    //uint64_t        copyFileMappingOffset;

    std::shared_ptr<volumebackup::VolumeBlockReader> reader { nullptr };
    std::shared_ptr<volumebackup::VolumeBlockWriter> writer { nullptr };
};

}

#endif