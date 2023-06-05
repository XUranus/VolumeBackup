

#ifndef VOLUME_BACKUP_CONTEXT_H
#define VOLUME_BACKUP_CONTEXT_H

#include <atomic>
#include <cstdint>
#include <string>

#include "BlockingQueue.h"

namespace {

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

/*
 *volume backup/restore facade and common struct defines
 */

enum class ActionType {
    BACKUP,     // backup a volume to a binary dump file copy
    RESTORE     // restore a volume from a binary dump file copy
};

enum class CopyType {
    FULL,       // the copy contains a full volume
    INCREMENT   // the copy is increment copy
};

struct VolumeBackupConfig {
    ActionType  actionType;
    CopyType    copyType;

    // immutable fields
    //std::string     prevChecksumBinPath;      // path of the checksum bin from previous copy
    //std::string     lastestChecksumBinPath;   // path of the checksum bin to write latest copy
    std::string             srcPath;
    uint32_t                blockSize;
    uint64_t                sessionOffset;
    uint64_t                sessionSize;
    bool                    hasherEnabled { true };

    bool Validate() const;
};

// commpound struct used for hash/writer consuming
struct VolumeConsumeBlock {
    char*           ptr;
    uint64_t        offset;
    uint32_t        length;
};

struct VolumeBackupSession {
    const std::string blockDevicePath;

};

struct VolumeBackupTask {
    std::string     blockDevicePath;
    uint64_t        sessionOffset;
    uint64_t        sessionSize;
    std::string     checksumBinPath;
    std::string     copyFilePath;
    uint64_t        copyFileMappingOffset;
};

/*
 * |                    |<-------session------>|
 * |==========================================================| logical volume
 * |        ...         |                      |    
 * 0              sessionOffset      sessionOffset + sessionSize
 *
 * each VolumeBackupContext corresponding to a volume <===> volume.part file backup session 
 */
struct VolumeBackupContext {
    // immutable fields
    VolumeBackupConfig      config;

    // mutable fields
    std::atomic<uint64_t>   bytesToRead;
    std::atomic<uint64_t>   bytesReaded;
    
    std::atomic<uint64_t>   blocksToHash;
    std::atomic<uint64_t>   blocksHashed;

    std::atomic<uint64_t>   bytesToWrite;
    std::atomic<uint64_t>   bytesWrited;

    VolumeBlockAllocator              allocator;
    BlockingQueue<VolumeConsumeBlock> hashingQueue;
    BlockingQueue<VolumeConsumeBlock> writeQueue;
};

}

#endif