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
    DBGLOG("destroy VolumeBlockHasher");
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
    AssertTaskNotStarted();
    if (m_workerThreadNum == 0 || m_workerThreadNum > MAX_HASHER_WORKER_NUM || !m_sharedConfig->hasherEnabled) {
        // invalid parameter
        WARNLOG("hasher diasable or invalid worker number: %lu, exit hasher directly", m_workerThreadNum);
        m_status = TaskStatus::FAILED;
        return false;
    }
    m_status = TaskStatus::RUNNING;
    for (uint32_t i = 0; i < m_workerThreadNum; i++) {
        m_workers.emplace_back(std::make_shared<std::thread>(&VolumeBlockHasher::WorkerThread, this, i));
    }
    return true;
}

void VolumeBlockHasher::WorkerThread(uint32_t workerID)
{
    VolumeConsumeBlock consumeBlock {};
    m_workersRunning++;
    DBGLOG("hasher worker[%lu] started, total worker running: %lu", workerID, m_workersRunning.load());
    while (true) {
        DBGLOG("hasher worker[%d] thread check", workerID);
        if (m_abort) {
            m_status = TaskStatus::ABORTED;
            break;
        }

        if (!m_sharedContext->hashingQueue->BlockingPop(consumeBlock)) {
            m_status = TaskStatus::SUCCEED;
            break; // queue has been finished
        }
        uint64_t index = consumeBlock.index;
        DBGLOG("hasher worker[%d] computing block[%llu]", workerID, index);
        // compute latest hash
        ComputeSHA256(
            consumeBlock.ptr,
            consumeBlock.length,
            m_lastestChecksumTable + index * m_singleChecksumSize,
            m_singleChecksumSize);

        ++m_sharedContext->counter->blocksHashed;

        if (m_forwardMode == HasherForwardMode::DIFF) {
            // diff with previous hash
            uint32_t previousHash = reinterpret_cast<uint32_t*>(m_prevChecksumTable)[index];
            uint32_t lastestHash = reinterpret_cast<uint32_t*>(m_lastestChecksumTable)[index];
            if (previousHash == lastestHash) {
                // drop the block and free
                DBGLOG("block[%llu] checksum remain unchanged, block dropped", index);
                m_sharedContext->allocator->bfree(consumeBlock.ptr);
                m_sharedContext->processedBitmap->Set(index);
                continue;
            }
        }
        DBGLOG("block[%llu] checksum changed, push to writer", index);
        m_sharedContext->counter->bytesToWrite += consumeBlock.length;
        m_sharedContext->writeQueue->BlockingPush(consumeBlock);
    }
    INFOLOG("hasher worker[%lu] terminated with status %s", workerID, GetStatusString().c_str());
    HandleWorkerTerminate();
    return;
}

#ifdef __linux__
void VolumeBlockHasher::ComputeSHA256(uint8_t* data, uint32_t len, uint8_t* output, uint32_t outputLen)
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
void VolumeBlockHasher::ComputeSHA256(uint8_t* data, uint32_t len, uint8_t* output, uint32_t outputLen)
{
    // TODO
}
#endif

void VolumeBlockHasher::HandleWorkerTerminate()
{
    m_workersRunning--;
    if (m_workersRunning != 0) {
        INFOLOG("one hasher worker exit, left workers: %d", m_workersRunning.load());
        return;
    }
    INFOLOG("hasher workers all terminated");
    m_sharedContext->writeQueue->Finish();
    return;
}
