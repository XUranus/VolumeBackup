#include <cstdint>
#include <cstring>
#include <thread>
#include <openssl/evp.h>
#include <cassert>

#include "VolumeBackupContext.h"
#include "VolumeBackupUtils.h"
#include "VolumeBlockHasher.h"

namespace {
    const uint64_t DEFAULT_CHECKSUM_TABLE_CAPACITY = 1024 * 1024 * 8; // 8MB
    const uint32_t SHA256_CHECKSUM_SIZE = 32; // 256bits
}

using namespace volumebackup;

VolumeBlockHasher::~VolumeBlockHasher()
{
    for (std::thread& worker: m_workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    SaveLatestChecksumBin();
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
    std::shared_ptr<VolumeBackupSession> session)
{
    // allocate for latest checksum table
    uint32_t blockCount = static_cast<uint32_t>(session->sessionSize / session->config->blockSize);
    uint64_t lastestChecksumTableSize = blockCount * SHA256_CHECKSUM_SIZE;
    char* lastestChecksumTable = new(std::nothrow)char[lastestChecksumTableSize];
    if (lastestChecksumTable == nullptr) {
        ERRLOG("insuficient memory for lastestChecksumTable");
        return nullptr;
    }
    memset(lastestChecksumTable, 0, sizeof(char) * lastestChecksumTableSize);

    return std::make_shared<VolumeBlockHasher>(
        session,
        HasherForwardMode::DIRECT,
        "",
        session->lastestChecksumBinPath,
        SHA256_CHECKSUM_SIZE,
        nullptr,
        0,
        lastestChecksumTable,
        lastestChecksumTableSize
    );
}

std::shared_ptr<VolumeBlockHasher> VolumeBlockHasher::BuildDiffHasher(
    std::shared_ptr<VolumeBackupSession> session)
{
    std::string prevChecksumBinPath = session->prevChecksumBinPath; // path of the checksum bin from previous copy
    std::string lastestChecksumBinPath = session->lastestChecksumBinPath; // path of the checksum bin to write latest copy

    // 1. allocate for latest checksum table
    uint32_t blockCount = static_cast<uint32_t>(session->sessionSize / session->config->blockSize);
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
            ERRLOG("previos checksum bin file open failed, errno: %d", errno);
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

    return std::make_shared<VolumeBlockHasher>(
        session,
        HasherForwardMode::DIFF,
        prevChecksumBinPath,
        lastestChecksumBinPath,
        SHA256_CHECKSUM_SIZE,
        prevChecksumTable,
        prevChecksumTableSize,
        lastestChecksumTable,
        lastestChecksumTableSize
    );
}

VolumeBlockHasher::VolumeBlockHasher(
    std::shared_ptr<VolumeBackupSession> session,
    HasherForwardMode   forwardMode,
    const std::string&  prevChecksumBinPath,
    const std::string&  lastestChecksumBinPath,
    uint32_t            singleChecksumSize,
    char*               prevChecksumTable,
    uint64_t            prevChecksumTableSize,
    char*               lastestChecksumTable,
    uint64_t            lastestChecksumTableSize
) : m_session(session),
    m_forwardMode(forwardMode),
    m_prevChecksumBinPath(prevChecksumBinPath),
    m_lastestChecksumBinPath(lastestChecksumBinPath),
    m_singleChecksumSize(singleChecksumSize),
    m_prevChecksumTable(prevChecksumTable),
    m_prevChecksumTableSize(prevChecksumTableSize),
    m_lastestChecksumTable(lastestChecksumTable),
    m_lastestChecksumTableSize(lastestChecksumTableSize)
{}


bool VolumeBlockHasher::Start(uint32_t workerThreadNum)
{
    if (m_status != TaskStatus::INIT) {
        return false;
    }
    if (workerThreadNum == 0) { // invalid parameter
        return false;
    }
    if (!m_workers.empty()) { // already started
        return false;
    }
    for (int i = 0; i < workerThreadNum; i++) {
        m_workers.emplace_back(&VolumeBlockHasher::WorkerThread, this);
    }
    m_status = TaskStatus::RUNNING;
    return true;
}

void VolumeBlockHasher::WorkerThread()
{
    VolumeConsumeBlock consumeBlock {};
    while (true) {
        if (m_abort) {
            m_status = TaskStatus::ABORTED;
            return;
        }

        if (!m_session->hashingQueue->Pop(consumeBlock)) {
            break; // queue has been finished
        }
        uint64_t index = (consumeBlock.volumeOffset - m_session->sessionOffset) / m_session->config->blockSize;
        
        // compute latest hash
        ComputeSHA256(
            consumeBlock.ptr,
            consumeBlock.length,
            m_lastestChecksumTable + index * m_singleChecksumSize,
            m_singleChecksumSize);

        m_session->counter->blocksHashed += consumeBlock.length;

        if (m_forwardMode == HasherForwardMode::DIFF) {
            // diff with previous hash
            uint32_t prevHash = reinterpret_cast<uint32_t*>(m_prevChecksumTable)[index];
            uint32_t lastestHash = reinterpret_cast<uint32_t*>(m_lastestChecksumTable)[index];
            if (prevHash == lastestHash) {
                continue;
            }
        }

        m_session->counter->bytesToWrite += consumeBlock.length;
        m_session->writeQueue->Push(consumeBlock);
    }

    m_status = TaskStatus::SUCCEED;
    return;
}

void VolumeBlockHasher::ComputeSHA256(char* data, uint32_t len, char* output, uint32_t outputLen)
{
    // EVP_MD_CTX *mdctx = nullptr;
    // const EVP_MD *md = nullptr;
    // unsigned char mdValue[EVP_MAX_MD_SIZE] = { 0 };
    // unsigned int mdLen;

    // if ((md = EVP_get_digestbyname("SHA256")) == nullptr) {
    //     ERRLOG("Unknown message digest SHA256");
    //     return;
    // }

    // if ((mdctx = EVP_MD_CTX_new()) == nullptr) {
    //     ERRLOG("Memory allocation failed");
    //     return;
    // }

    // EVP_DigestInit_ex(mdctx, md, nullptr);
    // EVP_DigestUpdate(mdctx, data, len);
    // EVP_DigestFinal_ex(mdctx, mdValue, &mdLen);
    // assert(mdLen == outputLen);
    // memcpy(output, mdValue, mdLen);
    // EVP_MD_CTX_free(mdctx);
    return;
}

void VolumeBlockHasher::SaveLatestChecksumBin()
{
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