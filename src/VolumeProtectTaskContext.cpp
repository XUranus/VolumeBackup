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

// implement bitmap

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
    m_table[index >> BITMAP_RSHIFT] |= ((uint8_t)1) << (index % BITS_PER_UINT8);
}

bool Bitmap::Test(uint64_t index) const
{
    if (index >= m_capacity * BITS_PER_UINT8) { //illegal argument
        return false;
    }
    return m_table[index >> BITMAP_RSHIFT] & (((uint8_t)1) << (index % BITS_PER_UINT8)) != 0;
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

// implement VolumeTaskSession

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