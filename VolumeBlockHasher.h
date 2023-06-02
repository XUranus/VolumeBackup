
#ifndef VOLUME_BLOCK_HASHER_H
#define VOLUME_BLOCK_HASHER_H

#include <cstdint>
#include <exception>
#include <fstream>
#include <memory>
#include <new>
#include <string>
#include <thread>
#include <vector>
#include "VolumeBackup.h"

enum HasherForwardMode {
    // direct move block to write queue after block checksum is computed
    DIRECT,        
    // diff the checksum computed with the corresponding previous one and move block forward only it's cheksum changed
    DIFF
};

class VolumeBlockHasher {
public:
    ~VolumeBlockHasher();

    static std::shared_ptr<VolumeBlockHasher>  BuildDirectHasher(
        const std::string& checksumBinPath          // path of the checksum bin to write latest copy
    );

    static std::shared_ptr<VolumeBlockHasher>  BuildDiffHasher(
        const std::string& prevChecksumBinPath,     // path of the checksum bin from previous copy
        const std::string& lastestChecksumBinPath   // path of the checksum bin to write latest copy
    );

    bool Start();

private:
    void WorkerThread();

    void SaveLatestChecksumBin();

    // direct hasher constructor
    VolumeBlockHasher(
        const std::shared_ptr<VolumeBackupContext> context,
        const std::string&  lastestChecksumBinPath,
        uint32_t            singleChecksumSize,
        char*               lastestChecksumTable,
        uint64_t            lastestChecksumTableCapacity
    );

    // diff hasher constructor
    VolumeBlockHasher(
        const  std::shared_ptr<VolumeBackupContext> context,
        const std::string&  prevChecksumBinPath,
        const std::string&  lastestChecksumBinPath,
        uint32_t            singleChecksumSize,
        char*               prevChecksumTable,
        uint64_t            prevChecksumTableSize,
        char*               lastestChecksumTable,
        uint64_t            lastestChecksumTableCapacity
    );

private:
    // mutable
    char*                                   m_lastestChecksumTable;         // mutable, shared within worker
    std::atomic<uint64_t>                   m_lastestChecksumTableSize;     // bytes writed
    uint64_t                                m_lastestChecksumTableCapacity; // bytes allocated
    std::shared_ptr<VolumeBackupContext>    m_context;                      // mutable, used for sync

    // immutable
    uint32_t                m_singleChecksumSize;
    HasherForwardMode       m_forwardMode;
    const char*             m_prevChecksumTable;
    uint64_t                m_prevChecksumTableSize;    // size in bytes
    std::string             m_prevChecksumBinPath;      // path of the checksum bin from previous copy
    std::string             m_lastestChecksumBinPath;   // path of the checksum bin to write latest copy

    std::vector<std::thread> m_workers;
};

#endif