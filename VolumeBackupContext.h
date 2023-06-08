

#ifndef VOLUME_BACKUP_CONTEXT_H
#define VOLUME_BACKUP_CONTEXT_H

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <cstdio>

#include "BlockingQueue.h"
#include "VolumeBlockHasher.h"
#include "VolumeBlockReader.h"
#include "VolumeBlockWriter.h"

#define ERRLOG  ::printf

namespace volumebackup {

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
    uint64_t        sessionSize { DEFAULT_SESSION_SIZE };   // defauly sesson size used to split session
    uint64_t        hasherNum { DEFAULT_HASHER_NUM };       // hasher worker count, recommended set to the num of processors
    bool            hasherEnabled { true };                 // if set to false, won't compute checksum
};

// immutable config, used to build volume restore task
struct VolumeRestoreConfig {
    CopyType        copyType;                               // type of the source copy to be restored from
    std::string     blockDevicePath;                        // path of the block device (volume)
    std::string	    copyDataDirPath;
	std::string	    copyMetaDirPath;
};

// commpound struct used for hash/writer consuming
struct VolumeConsumeBlock {
    char*           ptr;
    uint64_t        offset;
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

/*
 * |                    |<-------session------>|
 * |==========================================================| logical volume
 * |        ...         |                      |    
 * 0              sessionOffset      sessionOffset + sessionSize
 *
 * each VolumeBackupContext corresponding to a volume <===> volume.part file backup session 
 */
struct VolumeBackupSession {
    uint64_t        sessionOffset;
    uint64_t        sessionSize;
    std::string     checksumBinPath;
    std::string     copyFilePath;
    //uint64_t        copyFileMappingOffset;

    std::shared_ptr<volumebackup::VolumeBlockReader> reader { nullptr };
    std::shared_ptr<volumebackup::VolumeBlockHasher> hasher { nullptr };
    std::shared_ptr<volumebackup::VolumeBlockWriter> writer { nullptr };

    bool IsTerminated() const;
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

struct VolumePartitionTableEntry {
    std::string filesystem;
    uint64_t    patitionNumber;
    uint64_t    firstSector;
    uint64_t    lastSector;
    uint64_t    totalSectors;
};

struct VolumeCopyMeta {
    using Range = std::vector<std::pair<uint64_t, uint64_t>>;

    uint64_t    size;
    uint32_t    blockSize;
    Range       slices;
    VolumePartitionTableEntry partition;
};

}

#endif