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
    // won't free hashing table here
}

std::shared_ptr<VolumeBlockHasher> VolumeBlockHasher::BuildHasher(
    std::shared_ptr<VolumeTaskSharedConfig> sharedConfig,
    std::shared_ptr<VolumeTaskSharedContext> sharedContext,
    HasherForwardMode mode)
{
    VolumeBlockHasherParam param {};
    param.sharedConfig = sharedConfig;
    param.sharedContext = sharedContext;
    param.workerThreadNum = sharedConfig->hasherWorkerNum;
    param.forwardMode = mode;
    param.singleChecksumSize = SHA256_CHECKSUM_SIZE;

    return std::make_shared<VolumeBlockHasher>(param);
}

VolumeBlockHasher::VolumeBlockHasher(const VolumeBlockHasherParam& param)
  : m_singleChecksumSize(param.singleChecksumSize),
    m_forwardMode(param.forwardMode),
    m_workerThreadNum(param.workerThreadNum),
    m_sharedConfig(param.sharedConfig),
    m_sharedContext(param.sharedContext)
{
    // // only the borrowed reference from BlockHashingContext, won't be free by VolumeBlockHasher
    m_lastestChecksumTable = m_sharedContext->hashingContext->lastestTable;
    m_prevChecksumTable = m_sharedContext->hashingContext->previousTable;
    m_lastestChecksumTableSize = m_sharedContext->hashingContext->lastestSize;
    m_prevChecksumTableSize = m_sharedContext->hashingContext->previousSize;
}

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

void VolumeBlockHasher::HandleWorkerTerminate()
{
    if (m_workersRunning != 0) {
        INFOLOG("left workers %d", m_workersRunning);
        return;
    }
    INFOLOG("workers all completed");
    m_sharedContext->writeQueue->Finish();
    m_status = TaskStatus::SUCCEED;
    return;
}
