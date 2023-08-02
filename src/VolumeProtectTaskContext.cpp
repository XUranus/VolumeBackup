#include <cstring>
#include <exception>
#include <thread>
#include <memory>

#include "Logger.h"
#include "VolumeUtils.h"
#include "VolumeProtectTaskContext.h"

#include "VolumeBlockReader.h"
#include "VolumeBlockWriter.h"
#include "VolumeBlockHasher.h"

using namespace volumeprotect;

namespace {
    constexpr uint32_t BITS_PER_UINT8 = 8;
    constexpr uint32_t BITMAP_RSHIFT = 3; // 2^3 = 8
}

// implement VolumeBlockAllocator...

VolumeBlockAllocator::VolumeBlockAllocator(uint32_t blockSize, uint32_t blockNum)
    : m_blockSize(blockSize), m_blockNum(blockNum)
{
    m_pool = new char[blockSize * blockNum];
    m_allocTable = new bool[blockNum];
    memset(m_allocTable, 0, blockNum * sizeof(bool));
}

VolumeBlockAllocator::~VolumeBlockAllocator()
{
    if (m_pool) {
        delete [] m_pool;
        m_pool = nullptr;
    }
    if (m_allocTable) {
        delete [] m_allocTable;
        m_allocTable = nullptr;
    }
}

char* VolumeBlockAllocator::bmalloc()
{
    std::lock_guard<std::mutex> lk(m_mutex);
    for (int i = 0; i < static_cast<int>(m_blockNum); i++) {
        if (!m_allocTable[i]) {
            m_allocTable[i] = true;
            char* ptr = m_pool + (m_blockSize * i);
            DBGLOG("bmalloc index = %d, address = %p", i, ptr);
            return ptr;
        }
    }
    return nullptr;
}

void VolumeBlockAllocator::bfree(char* ptr)
{
    std::lock_guard<std::mutex> lk(m_mutex);
    uint64_t index = (ptr - m_pool) / static_cast<uint64_t>(m_blockSize);
    DBGLOG("bfree address = %p, index = %llu", ptr, index);
    if ((ptr - m_pool) % m_blockSize == 0) {
        m_allocTable[index] = false;
        return;
    }
    // reach err here
    throw std::runtime_error("bfree error: bad address");
}

// implement BlockHashingContext...

BlockHashingContext::BlockHashingContext(uint64_t pSize, uint64_t lSize)
    : previousSize(pSize), lastestSize(lSize)
{
    lastestTable = new uint8_t[lSize];
    previousTable = new uint8_t[pSize];
    memset(lastestTable, 0, sizeof(uint8_t) * lSize);
    memset(previousTable, 0, sizeof(uint8_t) * pSize);
}

BlockHashingContext::~BlockHashingContext()
{
    if (lastestTable != nullptr) {
        delete[] lastestTable;
        lastestTable = nullptr;
    }
    if (previousTable == nullptr) {
        delete[] previousTable;
        previousTable = nullptr;
    }
}

// implement Bitmap...

Bitmap::Bitmap(uint64_t size)
{
    m_capacity = size / BITS_PER_UINT8 + 1;
    m_table = new uint8_t[m_capacity]; // avoid using make_unique (requiring CXX14)
}

Bitmap::Bitmap(uint8_t* ptr, uint64_t capacity)
    : m_capacity(capacity), m_table(ptr)
{}

Bitmap::~Bitmap()
{
    if (m_table != nullptr) {
        delete[] m_table;
        m_table = nullptr;
    }
}

void Bitmap::Set(uint64_t index)
{
    if (index >= m_capacity * BITS_PER_UINT8) { // illegal argument 
        return;
    }
    m_table[index >> BITMAP_RSHIFT] |= (((uint8_t)1) << (index % BITS_PER_UINT8));
}

bool Bitmap::Test(uint64_t index) const
{
    if (index >= m_capacity * BITS_PER_UINT8) { //illegal argument
        return false;
    }
    return (m_table[index >> BITMAP_RSHIFT] & (((uint8_t)1) << (index % BITS_PER_UINT8))) != 0;
}

uint64_t Bitmap::FirstIndexUnset() const
{
    for (uint64_t index = 0; index < m_capacity * BITS_PER_UINT8; ++index) {
        if (!Test(index)) {
            return index;
        }
    }
    return MaxIndex();
}

uint64_t Bitmap::Capacity() const
{
    return m_capacity;
}

uint64_t Bitmap::MaxIndex() const
{
    return m_capacity * BITS_PER_UINT8 - 1;
}

const uint8_t* Bitmap::Ptr() const
{
    return m_table;
}

// implement VolumeTaskSession...

bool VolumeTaskSession::IsTerminated() const
{
    DBGLOG("check session terminated, readerTask: %d, hasherTask: %d, writerTask: %d",
        readerTask == nullptr ? TaskStatus::SUCCEED : readerTask->GetStatus(),
        hasherTask == nullptr ? TaskStatus::SUCCEED : hasherTask->GetStatus(),
        writerTask == nullptr ? TaskStatus::SUCCEED : writerTask->GetStatus()
    );
    return (
        (readerTask == nullptr || readerTask->IsTerminated()) &&
        (hasherTask == nullptr || hasherTask->IsTerminated()) &&
        (writerTask == nullptr || writerTask->IsTerminated())
    );
}

bool VolumeTaskSession::IsFailed() const
{
    DBGLOG("check session failed, readerTask: %d, hasherTask: %d, writerTask: %d",
        readerTask == nullptr ? TaskStatus::SUCCEED : readerTask->GetStatus(),
        hasherTask == nullptr ? TaskStatus::SUCCEED : hasherTask->GetStatus(),
        writerTask == nullptr ? TaskStatus::SUCCEED : writerTask->GetStatus()
    );
    return (
        (readerTask != nullptr && readerTask->IsFailed()) ||
        (hasherTask != nullptr && hasherTask->IsFailed()) ||
        (writerTask != nullptr && writerTask->IsFailed())
    );
}

void VolumeTaskSession::Abort() const
{
    if (readerTask != nullptr) {
        readerTask->Abort();
    }
    if (hasherTask != nullptr) {
        hasherTask->Abort();
    }
    if (writerTask != nullptr) {
        writerTask->Abort();
    }
}

// implement TaskStatisticTrait ...

void TaskStatisticTrait::UpdateRunningSessionStatistics(std::shared_ptr<VolumeTaskSession> session)
{
    std::lock_guard<std::mutex> lock(m_statisticMutex);
    auto counter = session->sharedContext->counter;
    DBGLOG("UpdateRunningSessionStatistics: bytesToReaded: %llu, bytesRead: %llu, "
        "blocksToHash: %llu, blocksHashed: %llu, "
        "bytesToWrite: %llu, bytesWritten: %llu",
        counter->bytesToRead.load(), counter->bytesRead.load(),
        counter->blocksToHash.load(), counter->blocksHashed.load(),
        counter->bytesToWrite.load(), counter->bytesWritten.load());
    m_currentSessionStatistics.bytesToRead = counter->bytesToRead;
    m_currentSessionStatistics.bytesRead = counter->bytesRead;
    m_currentSessionStatistics.blocksToHash = counter->blocksToHash;
    m_currentSessionStatistics.blocksHashed = counter->blocksHashed;
    m_currentSessionStatistics.bytesToWrite = counter->bytesToWrite;
    m_currentSessionStatistics.bytesWritten = counter->bytesWritten;
}

void TaskStatisticTrait::UpdateCompletedSessionStatistics(std::shared_ptr<VolumeTaskSession> session)
{
    std::lock_guard<std::mutex> lock(m_statisticMutex);
    auto counter = session->sharedContext->counter;
    DBGLOG("UpdateCompletedSessionStatistics: bytesToReaded: %llu, bytesRead: %llu, "
        "blocksToHash: %llu, blocksHashed: %llu, "
        "bytesToWrite: %llu, bytesWritten: %llu",
        counter->bytesToRead.load(), counter->bytesRead.load(),
        counter->blocksToHash.load(), counter->blocksHashed.load(),
        counter->bytesToWrite.load(), counter->bytesWritten.load());
    m_completedSessionStatistics.bytesToRead += counter->bytesToRead;
    m_completedSessionStatistics.bytesRead += counter->bytesRead;
    m_completedSessionStatistics.blocksToHash += counter->blocksToHash;
    m_completedSessionStatistics.blocksHashed += counter->blocksHashed;
    m_completedSessionStatistics.bytesToWrite += counter->bytesToWrite;
    m_completedSessionStatistics.bytesWritten += counter->bytesWritten;
    memset(&m_currentSessionStatistics, 0, sizeof(TaskStatistics));
}

// implement VolumeTaskCheckpointTrait

void VolumeTaskCheckpointTrait::SaveSessionCheckpoint(std::shared_ptr<VolumeTaskSession> session) const
{
    if (!session->sharedConfig->checkpointEnabled) {
        return;
    }
    // only should work during backup with hasher enabled,
    // hashing checksum must be saved before writer bitmap
    if (session->sharedConfig->hasherEnabled) {
        SaveSessionHashingContext(session);
    }
    // could both work during backup/restore
    SaveSessionWriterBitmap(session);
}

void VolumeTaskCheckpointTrait::SaveSessionHashingContext(std::shared_ptr<VolumeTaskSession> session) const
{
    uint8_t* latestChecksumTable = session->sharedContext->hashingContext->lastestTable;
    uint64_t latestChecksumTableSize = session->sharedContext->hashingContext->lastestSize;
    if (session->sharedConfig->hasherEnabled && latestChecksumTable != nullptr) {
        std::string filepath = session->sharedConfig->lastestChecksumBinPath;
        DBGLOG("save latest hash checksum table to %s, size = %llu", filepath.c_str(), latestChecksumTableSize);
        if (!native::WriteBinaryBuffer(filepath, latestChecksumTable, latestChecksumTableSize)) {
            ERRLOG("failed to save session hashing context");
        }
    }
}

void VolumeTaskCheckpointTrait::SaveSessionWriterBitmap(std::shared_ptr<VolumeTaskSession> session) const
{
    std::string filepath = session->sharedConfig->writerBitmapFilePath;
    if (!util::SaveBitmap(filepath, *(session->sharedContext->writerBitmap))) {
        ERRLOG("failed to save bitmap file");
    }
    return;
}