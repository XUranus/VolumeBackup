#include <cstring>
#include <exception>
#include <thread>

#include "Logger.h"
#include "VolumeBackupUtils.h"
#include "VolumeBackupContext.h"

#include "VolumeBlockReader.h"
#include "VolumeBlockWriter.h"
#include "VolumeBlockHasher.h"

using namespace volumebackup;

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
    for (int i = 0; i < m_blockNum; i++)
    {
        if (!m_allocTable[i]) {
            m_allocTable[i] = true;
            return m_pool + (m_blockSize * i);
        }
    }
    return nullptr;
}

void VolumeBlockAllocator::bfree(char* ptr)
{
    std::lock_guard<std::mutex> lk(m_mutex);
    if ((ptr - m_pool) % m_blockSize == 0) {
        m_allocTable[(ptr - m_pool) / m_blockSize] = false;
        return;
    }
    throw std::runtime_error("bfree error: bad address");
    // err
}

// bool VolumeBackupConfig::Validate() const
// {
//     // 1. validate volume and fetch volume size
//     try {
//         uint64_t volumeSize = volumebackup::ReadVolumeSize(blockDevicePath);
//     } catch (const std::exception& e) {
//         throw e;
//         return false;
//     }

//     // 2. validate blockSize
//     if (blockSize == 0 || blockSize % ONE_KB != 0 || blockSize > FOUR_MB) {
//         return false;
//     }
//     return true;
// }

bool VolumeBackupSession::IsTerminated() const
{
    DBGLOG("check session terminated, reader: %d, hasher: %d, writer: %d",
        reader == nullptr ? TaskStatus::SUCCEED : reader->GetStatus(),
        hasher == nullptr ? TaskStatus::SUCCEED : hasher->GetStatus(),
        writer == nullptr ? TaskStatus::SUCCEED : writer->GetStatus()
    );
    return (
        (reader == nullptr || reader->IsTerminated()) &&
        (hasher == nullptr || hasher->IsTerminated()) &&
        (writer == nullptr || writer->IsTerminated())
    );
}

bool VolumeBackupSession::IsFailed() const
{
    return (
        (reader != nullptr && reader->IsFailed()) ||
        (hasher != nullptr && hasher->IsFailed()) ||
        (writer != nullptr && writer->IsFailed())
    );
}

void VolumeBackupSession::Abort()
{
    if (reader != nullptr) {
        reader->Abort();
    }
    if (hasher != nullptr) {
        hasher->Abort();
    }
    if (writer != nullptr) {
        writer->Abort();
    }
}


