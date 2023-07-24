
#ifndef VOLUME_BLOCK_HASHER_H
#define VOLUME_BLOCK_HASHER_H

#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "VolumeProtectMacros.h"
#include "VolumeProtectTaskContext.h"

namespace volumeprotect {

enum class VOLUMEPROTECT_API  HasherForwardMode {
    // direct move block to write queue after block checksum is computed
    DIRECT,        
    // diff the checksum computed with the corresponding previous one and move block forward only it's cheksum changed
    DIFF
};

/**
 * @brief param struct to build a hasher
 */
struct VOLUMEPROTECT_API VolumeBlockHasherParam {
    std::shared_ptr<VolumeTaskSharedConfig>     sharedConfig;
    std::shared_ptr<VolumeTaskSharedContext>    sharedContext;
    HasherForwardMode   forwardMode;
    std::string         prevChecksumBinPath;
    std::string         lastestChecksumBinPath;
    uint32_t            singleChecksumSize;
    char*               prevChecksumTable;
    uint64_t            prevChecksumTableSize;
    char*               lastestChecksumTable;
    uint64_t            lastestChecksumTableSize;
};

class VOLUMEPROTECT_API VolumeBlockHasher : public StatefulTask {
public:
    ~VolumeBlockHasher();

    static std::shared_ptr<VolumeBlockHasher>  BuildDirectHasher(
        std::shared_ptr<VolumeTaskSession> session
    );

    static std::shared_ptr<VolumeBlockHasher>  BuildDiffHasher(
        std::shared_ptr<VolumeTaskSession> session
    );

    bool Start();

    VolumeBlockHasher(const VolumeBlockHasherParam& param);

private:
    void WorkerThread(int workerIndex);

    void ComputeSHA256(char* data, uint32_t len, char* output, uint32_t outputLen);

    void SaveLatestChecksumBin();

    void HandleWorkerTerminate();

private:
    // mutable
    char*                                       m_lastestChecksumTable;     // mutable, shared within worker
    uint64_t                                    m_lastestChecksumTableSize; // bytes allocated
    std::shared_ptr<VolumeTaskSharedContext>    m_sharedContext;

    // immutable
    uint32_t                m_singleChecksumSize;
    HasherForwardMode       m_forwardMode;
    char*                   m_prevChecksumTable;
    uint64_t                m_prevChecksumTableSize;    // size in bytes
    std::string             m_prevChecksumBinPath;      // path of the checksum bin from previous copy
    std::string             m_lastestChecksumBinPath;   // path of the checksum bin to write latest copy

    int                     m_workerThreadNum { DEFAULT_HASHER_NUM };
    int                     m_workersRunning;
    std::vector<std::shared_ptr<std::thread>>   m_workers;
    std::shared_ptr<VolumeTaskSharedConfig>     m_sharedConfig;
};

}

#endif