

#ifndef VOLUME_BACKUP_H
#define VOLUME_BACKUP_H

#include <atomic>
#include <cstdint>
#include <string>

#include "BlockingQueue.h"

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
};

// commpound struct used for hash/writer consuming
struct VolumeConsumeBlock {
    char*           ptr;
    uint64_t        offset;
    uint32_t        length
};

struct VolumeBackupContext {
    // immutable fields
    //std::string     prevChecksumBinPath;      // path of the checksum bin from previous copy
    //std::string     lastestChecksumBinPath;   // path of the checksum bin to write latest copy
    std::string     srcPath;
    uint32_t        blockSize;
    uint64_t        sessionOffset;
    uint64_t        length;

    // mutable fields
    std::atomic<uint64_t>   bytesToRead;
    std::atomic<uint64_t>   bytesReaded;
    
    std::atomic<uint64_t>   blocksToHash;
    std::atomic<uint64_t>   blocksHashed;

    std::atomic<uint64_t>   bytesToWrite;
    std::atomic<uint64_t>   bytesWrited;

    BlockingQueue<VolumeConsumeBlock> hashingQueue;
    BlockingQueue<VolumeConsumeBlock> writeQueue;
};

#endif