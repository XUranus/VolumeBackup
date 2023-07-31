
#ifndef VOLUMEBACKUP_BLOCK_HASHER_HEADER
#define VOLUMEBACKUP_BLOCK_HASHER_HEADER

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
    std::shared_ptr<VolumeTaskSharedConfig>     sharedConfig    { nullptr };
    std::shared_ptr<VolumeTaskSharedContext>    sharedContext   { nullptr };
    uint32_t                    workerThreadNum                 { DEFAULT_HASHER_NUM };
    HasherForwardMode           forwardMode                     { HasherForwardMode::DIRECT };
    std::string                 prevChecksumBinPath             {};
    std::string                 lastestChecksumBinPath          {};
    uint32_t                    singleChecksumSize              { 0 };
    uint8_t*                    prevChecksumTable               { nullptr };
    uint64_t                    prevChecksumTableSize           { 0 };
    uint8_t*                    lastestChecksumTable            { nullptr };
    uint64_t                    lastestChecksumTableSize        { 0 };
};

class VOLUMEPROTECT_API VolumeBlockHasher : public StatefulTask {
public:
    ~VolumeBlockHasher();

    static std::shared_ptr<VolumeBlockHasher>  BuildDirectHasher(
        std::shared_ptr<VolumeTaskSharedConfig> sharedConfig,
        std::shared_ptr<VolumeTaskSharedContext> sharedContext
    );

    static std::shared_ptr<VolumeBlockHasher>  BuildDiffHasher(
        std::shared_ptr<VolumeTaskSharedConfig> sharedConfig,
        std::shared_ptr<VolumeTaskSharedContext> sharedContext
    );

    bool Start();

    VolumeBlockHasher(const VolumeBlockHasherParam& param);

    void SaveLatestChecksumBin();

private:
    void WorkerThread(int workerIndex);

    void ComputeSHA256(char* data, uint32_t len, char* output, uint32_t outputLen);

    void HandleWorkerTerminate();

private:
    // immutable
    uint32_t                    m_singleChecksumSize    { 0 };
    HasherForwardMode           m_forwardMode           { HasherForwardMode::DIRECT };
    uint8_t*                    m_prevChecksumTable     { nullptr };
    uint64_t                    m_prevChecksumTableSize { 0 };  // size in bytes
    std::string                 m_prevChecksumBinPath;          // path of the checksum bin from previous copy
    std::string                 m_lastestChecksumBinPath;       // path of the checksum bin to write latest copy

    uint32_t                    m_workerThreadNum       { DEFAULT_HASHER_NUM };
    uint32_t                    m_workersRunning        { 0 };
    std::vector<std::shared_ptr<std::thread>>   m_workers;
    std::shared_ptr<VolumeTaskSharedConfig>     m_sharedConfig;

    // mutable
    uint8_t*                                    m_lastestChecksumTable { nullptr }; // mutable, shared within worker
    uint64_t                                    m_lastestChecksumTableSize;         // bytes allocated
    std::shared_ptr<VolumeTaskSharedContext>    m_sharedContext;
};

}

#endif