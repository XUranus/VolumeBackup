#ifndef VOLUMEBACKUP_BLOCK_HASHER_HEADER
#define VOLUMEBACKUP_BLOCK_HASHER_HEADER

#include "VolumeProtectTaskContext.h"

namespace volumeprotect {
namespace task {

enum class  HasherForwardMode {
    // direct move block to write queue after block checksum is computed
    DIRECT,
    // diff the checksum computed with the corresponding previous one and move block forward only it's cheksum changed
    DIFF
};

/**
 * @brief param struct to build a hasher
 */
struct VolumeBlockHasherParam {
    std::shared_ptr<VolumeTaskSharedConfig>     sharedConfig    { nullptr };
    std::shared_ptr<VolumeTaskSharedContext>    sharedContext   { nullptr };
    uint32_t                    workerThreadNum                 { DEFAULT_HASHER_NUM };
    HasherForwardMode           forwardMode                     { HasherForwardMode::DIRECT };
    uint32_t                    singleChecksumSize              { 0 };
};

/**
 * @brief Independent routine to keep consuming block from queue and do checksum calculation
 */
class VolumeBlockHasher : public StatefulTask {
public:
    ~VolumeBlockHasher();

    static std::shared_ptr<VolumeBlockHasher>  BuildHasher(
        std::shared_ptr<VolumeTaskSharedConfig> sharedConfig,
        std::shared_ptr<VolumeTaskSharedContext> sharedContext,
        HasherForwardMode mode
    );

    bool Start();

    VolumeBlockHasher(const VolumeBlockHasherParam& param);

private:
    void WorkerThread(uint32_t workerID);

    void ComputeSHA256(uint8_t* data, uint32_t len, uint8_t* output, uint32_t outputLen);

    void HandleWorkerTerminate();

private:
    uint32_t                    m_singleChecksumSize    { 0 };
    HasherForwardMode           m_forwardMode           { HasherForwardMode::DIRECT };
    uint32_t                    m_workerThreadNum       { DEFAULT_HASHER_NUM };
    std::atomic<uint32_t>       m_workersRunning        { 0 };
    std::vector<std::shared_ptr<std::thread>>   m_workers;
    std::shared_ptr<VolumeTaskSharedConfig>     m_sharedConfig;

    // only the borrowed reference from BlockHashingContext, won't be free by VolumeBlockHasher
    uint8_t*                                    m_prevChecksumTable     { nullptr };
    uint64_t                                    m_prevChecksumTableSize { 0 };  // size in bytes
    uint8_t*                                    m_lastestChecksumTable { nullptr }; // mutable, shared within worker
    uint64_t                                    m_lastestChecksumTableSize;         // bytes allocated

    std::shared_ptr<VolumeTaskSharedContext>    m_sharedContext;
};

}
}

#endif