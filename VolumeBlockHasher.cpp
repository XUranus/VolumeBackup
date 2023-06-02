#include <cstring>
#include <thread>

#include "VolumeBlockHasher.h"

namespace {
    const uint64_t DEFAULT_CHECKSUM_TABLE_CAPACITY = 1024 * 1024 * 8; // 8MB
    const uint32_t SHA256_CHECKSUM_SIZE = 32;
}

// direct hasher constructor
VolumeBlockHasher::VolumeBlockHasher(
    const std::shared_ptr<VolumeBackupContext> context,
    const std::string&  lastestChecksumBinPath,
    uint32_t            singleChecksumSize,
    char*               lastestChecksumTable,
    uint64_t            lastestChecksumTableCapacity
) : m_lastestChecksumBinPath(lastestChecksumBinPath),
    m_forwardMode(HasherForwardMode::DIRECT)
{
    m_context = context;
    m_lastestChecksumTable = lastestChecksumTable;

    // disable previous checksum table
    m_prevChecksumTable = nullptr;
    m_prevChecksumTableSize = 0;

    // lastest checksum table
    m_singleChecksumSize = singleChecksumSize;
    m_lastestChecksumTableSize = 0;
    m_lastestChecksumTableCapacity = lastestChecksumTableCapacity;
}

// diff hasher constructor
VolumeBlockHasher::VolumeBlockHasher(
    const  std::shared_ptr<VolumeBackupContext> context,
    const std::string&  prevChecksumBinPath,
    const std::string&  lastestChecksumBinPath,
    uint32_t            singleChecksumSize,
    char*               prevChecksumTable,
    uint64_t            prevChecksumTableSize,
    char*               lastestChecksumTable,
    uint64_t            lastestChecksumTableCapacity
) : m_lastestChecksumBinPath(lastestChecksumBinPath),
    m_prevChecksumBinPath(prevChecksumBinPath),
    m_forwardMode(HasherForwardMode::DIFF)
{
    m_context = context;
    m_singleChecksumSize = singleChecksumSize;

    // 1. assign previous checksum table
    m_prevChecksumTable = prevChecksumTable;
    m_prevChecksumTableSize = prevChecksumTableSize;

    // lastest checksum table
    m_lastestChecksumTable = lastestChecksumTable;
    m_lastestChecksumTableSize = 0;
    m_lastestChecksumTableCapacity = lastestChecksumTableCapacity;
}

VolumeBlockHasher::~VolumeBlockHasher()
{
    for (std::thread& worker: m_workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    SaveLatestChecksumBin();
}

std::shared_ptr<VolumeBlockHasher> VolumeBlockHasher::BuildDirectHasher(
    std::shared_ptr<VolumeBackupContext> context,
    const std::string &checksumBinPath)
{
    // 1. check checksumBinPath, open and create
    try {
        std::ofstream checksumBinOut(checksumBinPath, std::ios::binary);
        if (!checksumBinOut.is_open()) {
            throw std::runtime_error("checksum bin file open failed");
            return nullptr;
        }
        checksumBinOut.close();
    } catch (const std::exception& e) {
        // TODO:: print errno
        return nullptr;
    }

    // 2. allocate for latest checksum table
    uint64_t lastestChecksumTableCapacity = DEFAULT_CHECKSUM_TABLE_CAPACITY;
    char* lastestChecksumTable = new(std::nothrow)char[lastestChecksumTableCapacity];
    if (lastestChecksumTable == nullptr) {
        throw std::runtime_error("insuficient memory for lastestChecksumTable");
        return nullptr;
    }
    memset(lastestChecksumTable, 0, sizeof(char) * lastestChecksumTableCapacity);

    return std::make_shared<VolumeBlockHasher>(
        context,
        checksumBinPath,
        SHA256_CHECKSUM_SIZE,
        lastestChecksumTable,
        lastestChecksumTableCapacity
    );
}

std::shared_ptr<VolumeBlockHasher> VolumeBlockHasher::BuildDiffHasher(
    std::shared_ptr<VolumeBackupContext> context,
    const std::string &prevChecksumBinPath,
    const std::string &lastestChecksumBinPath)
{
    // 1. open previous checksumBinPath and check
    try {
        
    } catch (const std::exception& e) {

    }

    return std::make_shared<VolumeBlockHasher>(
        context,
    const std::string&  prevChecksumBinPath,
    const std::string&  lastestChecksumBinPath,
    uint32_t            singleChecksumSize,
    char*               prevChecksumTable,
    uint64_t            prevChecksumTableSize,
    char*               lastestChecksumTable,
    uint64_t            lastestChecksumTableCapacity
}


bool VolumeBlockHasher::Start(uint32_t workerThreadNum)
{
    if (workerThreadNum == 0) { // invalid parameter
        return false;
    }
    if (!m_workers.empty()) { // already started
        return false;
    }
    for (int i = 0; i < workerThreadNum; i++) {
        m_workers.emplace_back(&VolumeBlockHasher::WorkerThread, this);
    }
}

void VolumeBlockHasher::WorkerThread()
{
    VolumeConsumeBlock consumeBlock;
    while (true) {
        if (!m_context->hashingQueue.Pop(consumeBlock)) {
            break;; // pop failed, queue has been finished
        }
        uint64_t index = (consumeBlock.offset - m_context->sessionOffset) / m_context->blockSize;
        
    }   
}

