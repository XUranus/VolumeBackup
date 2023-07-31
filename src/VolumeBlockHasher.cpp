#include <cstdint>
#include <cstring>
#include <thread>
#include <cassert>
#include <fstream>

#ifdef __linux__
#include <openssl/evp.h>
#endif

#include "Logger.h"
#include "VolumeProtectTaskContext.h"
#include "VolumeUtils.h"
#include "VolumeBlockHasher.h"

namespace {
    const uint64_t DEFAULT_CHECKSUM_TABLE_CAPACITY = 1024 * 1024 * 8; // 8MB
    const uint32_t SHA256_CHECKSUM_SIZE = 32; // 256bits
    const uint32_t MAX_HASHER_WORKER_NUM = 32;
}

using namespace volumeprotect;

VolumeBlockHasher::~VolumeBlockHasher()
{
    INFOLOG("finalize VolumeBlockHasher");
    for (std::shared_ptr<std::thread>& worker: m_workers) {
        if (worker->joinable()) {
            worker->join();
        }
    }
    if (m_prevChecksumTable != nullptr) {
        delete[] m_prevChecksumTable;
        m_prevChecksumTable = nullptr;
    }
    if (m_lastestChecksumTable != nullptr) {
        delete[] m_lastestChecksumTable;
        m_lastestChecksumTable = nullptr;
    }
}

std::shared_ptr<VolumeBlockHasher> VolumeBlockHasher::BuildDirectHasher(
    std::shared_ptr<VolumeTaskSharedConfig> sharedConfig,
    std::shared_ptr<VolumeTaskSharedContext> sharedContext)
{
    // allocate for latest checksum table
    uint32_t blockCount = static_cast<uint32_t>(sharedConfig->sessionSize / sharedConfig->blockSize);
    uint64_t lastestChecksumTableSize = blockCount * SHA256_CHECKSUM_SIZE;
    uint8_t* lastestChecksumTable = new (std::nothrow) uint8_t[lastestChecksumTableSize];
    if (lastestChecksumTable == nullptr) {
        ERRLOG("insuficient memory for lastestChecksumTable");
        return nullptr;
    }
    memset(lastestChecksumTable, 0, sizeof(char) * lastestChecksumTableSize);

    VolumeBlockHasherParam param {};
    param.sharedConfig = sharedConfig;
    param.sharedContext = sharedContext;
    param.workerThreadNum = sharedConfig->hasherWorkerNum;
    param.forwardMode = HasherForwardMode::DIRECT;
    param.prevChecksumBinPath = "";
    param.lastestChecksumBinPath = sharedConfig->lastestChecksumBinPath;
    param.singleChecksumSize = SHA256_CHECKSUM_SIZE;
    param.prevChecksumTable = nullptr;
    param.prevChecksumTableSize = 0;
    param.lastestChecksumTable = lastestChecksumTable;
    param.lastestChecksumTableSize = lastestChecksumTableSize;

    return std::make_shared<VolumeBlockHasher>(param);
}

std::shared_ptr<VolumeBlockHasher> VolumeBlockHasher::BuildDiffHasher(
    std::shared_ptr<VolumeTaskSharedConfig> sharedConfig,
    std::shared_ptr<VolumeTaskSharedContext> sharedContext)
{
    // path of the checksum bin from previous copy
    std::string prevChecksumBinPath = sharedConfig->prevChecksumBinPath;
    // path of the checksum bin to write latest copy
    std::string lastestChecksumBinPath = sharedConfig->lastestChecksumBinPath;

    // 1. allocate for latest checksum table
    uint32_t blockCount = static_cast<uint32_t>(sharedConfig->sessionSize / sharedConfig->blockSize);
    uint64_t lastestChecksumTableSize = blockCount * SHA256_CHECKSUM_SIZE;
    uint64_t prevChecksumTableSize = lastestChecksumTableSize; // TODO:: validate
    uint8_t* lastestChecksumTable = new (std::nothrow) uint8_t[lastestChecksumTableSize];
    uint8_t* prevChecksumTable = new (std::nothrow) uint8_t[prevChecksumTableSize];
    if (lastestChecksumTable == nullptr || prevChecksumTable == nullptr) {
        ERRLOG("insuficient memory for lastestChecksumTable and prevChecksumTableSize");
        delete[] lastestChecksumTable;
        delete[] prevChecksumTable;
        return nullptr;
    }
    memset(lastestChecksumTable, 0, sizeof(char) * lastestChecksumTableSize);
    memset(prevChecksumTable, 0, sizeof(char) * prevChecksumTableSize);

    // 2. check previous prevChecksumBinPath, open and load
    try {
        std::ifstream checksumBinFile(prevChecksumBinPath, std::ios::binary);
        if (!checksumBinFile.is_open()) {
            ERRLOG("previous checksum bin file %s open failed, errno: %d", prevChecksumBinPath.c_str(), errno);
            return nullptr;
        }
        checksumBinFile.read(reinterpret_cast<char*>(prevChecksumTable), prevChecksumTableSize); // TODO:: check success
        checksumBinFile.close();
    } catch (const std::exception& e) {
        ERRLOG("failed to read previous checksum bin %s with exception %s", prevChecksumBinPath.c_str(), e.what());
        return nullptr;
    }

    // TODO:: 3. validate previous checksum table size with current one
    VolumeBlockHasherParam param {};
    param.sharedConfig = sharedConfig;
    param.sharedContext = sharedContext;
    param.workerThreadNum = sharedConfig->hasherWorkerNum;
    param.forwardMode = HasherForwardMode::DIFF;
    param.prevChecksumBinPath = prevChecksumBinPath;
    param.lastestChecksumBinPath = lastestChecksumBinPath;
    param.singleChecksumSize = SHA256_CHECKSUM_SIZE;
    param.prevChecksumTable = prevChecksumTable;
    param.prevChecksumTableSize = prevChecksumTableSize;
    param.lastestChecksumTable = lastestChecksumTable;
    param.lastestChecksumTableSize = lastestChecksumTableSize;
    
    return std::make_shared<VolumeBlockHasher>(param);
}

VolumeBlockHasher::VolumeBlockHasher(const VolumeBlockHasherParam& param)
  : m_singleChecksumSize(param.singleChecksumSize),
    m_forwardMode(param.forwardMode),
    m_prevChecksumTable(param.prevChecksumTable),
    m_prevChecksumTableSize(param.prevChecksumTableSize),
    m_prevChecksumBinPath(param.prevChecksumBinPath),
    m_lastestChecksumBinPath(param.lastestChecksumBinPath),
    m_workerThreadNum(param.workerThreadNum),
    m_sharedConfig(param.sharedConfig),
    m_sharedContext(param.sharedContext),
    m_lastestChecksumTable(param.lastestChecksumTable),
    m_lastestChecksumTableSize(param.lastestChecksumTableSize)
{}

bool VolumeBlockHasher::Start()
{
    if (m_status != TaskStatus::INIT) {
        return false;
    }
    if (m_workerThreadNum == 0 || m_workerThreadNum > MAX_HASHER_WORKER_NUM) { // invalid parameter
        m_status = TaskStatus::FAILED;
        return false;
    }
    if (!m_workers.empty()) { // already started
        return false;
    }
    if (!m_sharedConfig->hasherEnabled) {
        m_status = TaskStatus::FAILED;
        WARNLOG("hasher not enabled, exit hasher directly");
        return false;
    }
    m_status = TaskStatus::RUNNING;
    m_workersRunning = m_workerThreadNum;
    for (int i = 0; i < m_workerThreadNum; i++) {
        m_workers.emplace_back(std::make_shared<std::thread>(&VolumeBlockHasher::WorkerThread, this, i));
    }
    return true;
}

void VolumeBlockHasher::WorkerThread(int workerIndex)
{
    VolumeConsumeBlock consumeBlock {};
    while (true) {
        DBGLOG("hasher worker[%d] thread check", workerIndex);
        if (m_abort) {
            INFOLOG("hasher worker %d aborted", workerIndex);
            m_workersRunning--;
            HandleWorkerTerminate();
            return;
        }

        if (!m_sharedContext->hashingQueue->BlockingPop(consumeBlock)) {
            break; // queue has been finished
        }
        uint64_t index = (consumeBlock.volumeOffset - m_sharedConfig->sessionOffset) / m_sharedConfig->blockSize;
        // compute latest hash
        ComputeSHA256(
            consumeBlock.ptr,
            consumeBlock.length,
            reinterpret_cast<char*>(m_lastestChecksumTable) + index * m_singleChecksumSize,
            m_singleChecksumSize);

        ++m_sharedContext->counter->blocksHashed;

        if (m_forwardMode == HasherForwardMode::DIFF) {
            // diff with previous hash
            uint32_t prevHash = reinterpret_cast<uint32_t*>(m_prevChecksumTable)[index];
            uint32_t lastestHash = reinterpret_cast<uint32_t*>(m_lastestChecksumTable)[index];
            if (prevHash == lastestHash) {
                // drop the block and free
                m_sharedContext->allocator->bfree(consumeBlock.ptr);
                continue;
            }
        }

        m_sharedContext->counter->bytesToWrite += consumeBlock.length;
        m_sharedContext->writeQueue->BlockingPush(consumeBlock);
    }
    INFOLOG("hasher worker %d read completed successfully", workerIndex);
    m_workersRunning--;
    HandleWorkerTerminate();
    return;
}

#ifdef __linux__
void VolumeBlockHasher::ComputeSHA256(char* data, uint32_t len, char* output, uint32_t outputLen)
{
    EVP_MD_CTX *mdctx = nullptr;
    const EVP_MD *md = nullptr;
    unsigned char mdValue[EVP_MAX_MD_SIZE] = { 0 };
    unsigned int mdLen;

    if ((md = EVP_get_digestbyname("SHA256")) == nullptr) {
        ERRLOG("Unknown message digest SHA256");
        return;
    }

    if ((mdctx = EVP_MD_CTX_new()) == nullptr) {
        ERRLOG("Memory allocation failed");
        return;
    }

    EVP_DigestInit_ex(mdctx, md, nullptr);
    EVP_DigestUpdate(mdctx, data, len);
    EVP_DigestFinal_ex(mdctx, mdValue, &mdLen);
    assert(mdLen == outputLen);
    memcpy(output, mdValue, mdLen);
    EVP_MD_CTX_free(mdctx);
    return;
}
#endif

#ifdef _WIN32
void VolumeBlockHasher::ComputeSHA256(char* data, uint32_t len, char* output, uint32_t outputLen)
{
    // TODO
}
#endif

void VolumeBlockHasher::SaveLatestChecksumBin()
{
    DBGLOG("save sha256 checksum to %s", m_lastestChecksumBinPath.c_str());
    // save lastest checksum bin
    try {
        if (m_lastestChecksumTable == nullptr) {
            ERRLOG("lastestChecksumTable is nullptr");
            return;
        }
        std::ofstream checksumBinFile(m_lastestChecksumBinPath, std::ios::binary | std::ios::trunc);
        if (!checksumBinFile.is_open()) {
            ERRLOG("failed to open checksum bin file %s", m_lastestChecksumBinPath.c_str());
            return;
        }
        checksumBinFile.write(reinterpret_cast<char*>(m_lastestChecksumTable), m_lastestChecksumTableSize);
        checksumBinFile.close();
    } catch (const std::exception& e) {
        ERRLOG("save lastest checksum bin %s failed with exception: %s", m_lastestChecksumBinPath.c_str(), e.what());
    }
    return;
}

void VolumeBlockHasher::HandleWorkerTerminate()
{
    if (m_workersRunning != 0) {
        INFOLOG("left workers %d", m_workersRunning);
        return;
    }
    INFOLOG("workers all completed");
    m_sharedContext->writeQueue->Finish();
    SaveLatestChecksumBin();
    m_status = TaskStatus::SUCCEED;
    return;
}
