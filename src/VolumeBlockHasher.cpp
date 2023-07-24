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
    std::shared_ptr<VolumeTaskSession> session)
{
    // allocate for latest checksum table
    uint32_t blockCount = static_cast<uint32_t>(session->sessionSize / session->blockSize);
    uint64_t lastestChecksumTableSize = blockCount * SHA256_CHECKSUM_SIZE;
    char* lastestChecksumTable = new(std::nothrow)char[lastestChecksumTableSize];
    if (lastestChecksumTable == nullptr) {
        ERRLOG("insuficient memory for lastestChecksumTable");
        return nullptr;
    }
    memset(lastestChecksumTable, 0, sizeof(char) * lastestChecksumTableSize);

    VolumeBlockHasherParam param {
        session,
        HasherForwardMode::DIRECT,
        "",
        session->lastestChecksumBinPath,
        SHA256_CHECKSUM_SIZE,
        nullptr,
        0,
        lastestChecksumTable,
        lastestChecksumTableSize
    };
    return std::make_shared<VolumeBlockHasher>(param);
}

std::shared_ptr<VolumeBlockHasher> VolumeBlockHasher::BuildDiffHasher(
    std::shared_ptr<VolumeTaskSession> session)
{
    std::string prevChecksumBinPath = session->prevChecksumBinPath; // path of the checksum bin from previous copy
    std::string lastestChecksumBinPath = session->lastestChecksumBinPath; // path of the checksum bin to write latest copy

    // 1. allocate for latest checksum table
    uint32_t blockCount = static_cast<uint32_t>(session->sessionSize / session->blockSize);
    uint64_t lastestChecksumTableSize = blockCount * SHA256_CHECKSUM_SIZE;
    char* lastestChecksumTable = new(std::nothrow)char[lastestChecksumTableSize];
    if (lastestChecksumTable == nullptr) {
        ERRLOG("insuficient memory for lastestChecksumTable");
        return nullptr;
    }
    memset(lastestChecksumTable, 0, sizeof(char) * lastestChecksumTableSize);

    uint64_t prevChecksumTableSize = lastestChecksumTableSize; // TODO:: validate
    char* prevChecksumTable = new(std::nothrow)char[prevChecksumTableSize];
    if (prevChecksumTable == nullptr) {
        ERRLOG("insuficient memory for prevChecksumTable");
        delete[] lastestChecksumTable;
        return nullptr;
    }
    memset(prevChecksumTable, 0, sizeof(char) * prevChecksumTableSize);

    // 2. check previous prevChecksumBinPath, open and load
    try {
        std::ifstream checksumBinFile(prevChecksumBinPath, std::ios::binary);
        if (!checksumBinFile.is_open()) {
            ERRLOG("previous checksum bin file %s open failed, errno: %d", prevChecksumBinPath.c_str(), errno);
            delete[] lastestChecksumTable;
            delete[] prevChecksumTable;
            return nullptr;
        }
        checksumBinFile.read(prevChecksumTable, prevChecksumTableSize); // TODO:: check success
        checksumBinFile.close();
    } catch (const std::exception& e) {
        ERRLOG("failed to read previous checksum bin %s with exception %s", prevChecksumBinPath.c_str(), e.what());
        delete[] lastestChecksumTable;
        delete[] prevChecksumTable;
        return nullptr;
    }

    // TODO:: 3. validate previous checksum table size with current one
    VolumeBlockHasherParam param {
        session,
        HasherForwardMode::DIFF,
        prevChecksumBinPath,
        lastestChecksumBinPath,
        SHA256_CHECKSUM_SIZE,
        prevChecksumTable,
        prevChecksumTableSize,
        lastestChecksumTable,
        lastestChecksumTableSize
    };

    return std::make_shared<VolumeBlockHasher>(param);
}

VolumeBlockHasher::VolumeBlockHasher(const VolumeBlockHasherParam& param)
  : m_session(param.session),
    m_forwardMode(param.forwardMode),
    m_prevChecksumBinPath(param.prevChecksumBinPath),
    m_lastestChecksumBinPath(param.lastestChecksumBinPath),
    m_singleChecksumSize(param.singleChecksumSize),
    m_prevChecksumTable(param.prevChecksumTable),
    m_prevChecksumTableSize(param.prevChecksumTableSize),
    m_lastestChecksumTable(param.lastestChecksumTable),
    m_lastestChecksumTableSize(param.lastestChecksumTableSize)
{}

bool VolumeBlockHasher::Start()
{
    if (m_status != TaskStatus::INIT) {
        return false;
    }
    if (m_workerThreadNum == 0) { // invalid parameter
        m_status = TaskStatus::FAILED;
        return false;
    }
    if (!m_workers.empty()) { // already started
        return false;
    }
    if (!m_session->hasherEnabled) {
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

        if (!m_session->hashingQueue->Pop(consumeBlock)) {
            break; // queue has been finished
        }
        uint64_t index = (consumeBlock.volumeOffset - m_session->sessionOffset) / m_session->blockSize;
        // compute latest hash
        ComputeSHA256(
            consumeBlock.ptr,
            consumeBlock.length,
            m_lastestChecksumTable + index * m_singleChecksumSize,
            m_singleChecksumSize);

        ++m_session->counter->blocksHashed;

        if (m_forwardMode == HasherForwardMode::DIFF) {
            // diff with previous hash
            uint32_t prevHash = reinterpret_cast<uint32_t*>(m_prevChecksumTable)[index];
            uint32_t lastestHash = reinterpret_cast<uint32_t*>(m_lastestChecksumTable)[index];
            if (prevHash == lastestHash) {
                // drop the block and free
                m_session->allocator->bfree(consumeBlock.ptr);
                continue;
            }
        }

        m_session->counter->bytesToWrite += consumeBlock.length;
        m_session->writeQueue->Push(consumeBlock);
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
        std::ofstream checksumBinFile(m_lastestChecksumBinPath, std::ios::binary | std::ios::trunc);
        if (!checksumBinFile.is_open()) {
            ERRLOG("failed to open checksum bin file %s", m_lastestChecksumBinPath.c_str());
            return;
        }
        checksumBinFile.write(m_lastestChecksumTable, m_lastestChecksumTableSize);
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
    m_session->writeQueue->Finish();
    m_status = TaskStatus::SUCCEED;
    SaveLatestChecksumBin();
    return;
}
