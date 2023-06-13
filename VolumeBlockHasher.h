
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

#include "VolumeBackupContext.h"

namespace volumebackup {

enum HasherForwardMode {
    // direct move block to write queue after block checksum is computed
    DIRECT,        
    // diff the checksum computed with the corresponding previous one and move block forward only it's cheksum changed
    DIFF
};

class VolumeBlockHasher : public StatefulTask {
public:
    ~VolumeBlockHasher();

    static std::shared_ptr<VolumeBlockHasher>  BuildDirectHasher(
        std::shared_ptr<VolumeBackupSession> session
    );

    static std::shared_ptr<VolumeBlockHasher>  BuildDiffHasher(
        std::shared_ptr<VolumeBackupSession> session
    );

    bool Start(int workerThreadNum = DEFAULT_HASHER_NUM);

    VolumeBlockHasher(
        std::shared_ptr<VolumeBackupSession> session,
        HasherForwardMode   forwardMode,
        const std::string&  prevChecksumBinPath,
        const std::string&  lastestChecksumBinPath,
        uint32_t            singleChecksumSize,
        char*               prevChecksumTable,
        uint64_t            prevChecksumTableSize,
        char*               lastestChecksumTable,
        uint64_t            lastestChecksumTableSize
    );

private:
    void WorkerThread(int workerIndex);

    void ComputeSHA256(char* data, uint32_t len, char* output, uint32_t outputLen);

    void SaveLatestChecksumBin();

private:
    // mutable
    char*                                   m_lastestChecksumTable;     // mutable, shared within worker
    uint64_t                                m_lastestChecksumTableSize; // bytes allocated
    std::shared_ptr<VolumeBackupSession>    m_session;                  // mutable, used for sync

    // immutable
    uint32_t                m_singleChecksumSize;
    HasherForwardMode       m_forwardMode;
    char*                   m_prevChecksumTable;
    uint64_t                m_prevChecksumTableSize;    // size in bytes
    std::string             m_prevChecksumBinPath;      // path of the checksum bin from previous copy
    std::string             m_lastestChecksumBinPath;   // path of the checksum bin to write latest copy

    std::vector<std::thread> m_workers;
};

}

#endif