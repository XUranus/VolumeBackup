#include <string>
#include <iostream>
#include <thread>
#include <chrono>
#include <cstring>
#include <cstdint>
#include <stdexcept>
#include <fstream>
#include <memory>
#include <cassert>
#include <algorithm>

#include "Logger.h"
#include "VolumeUtils.h"
#include "NativeIOInterface.h"
#include "VolumeBlockReader.h"

using namespace volumeprotect;

namespace {
    constexpr auto FETCH_BLOCK_BUFFER_SLEEP_INTERVAL = std::chrono::milliseconds(100);
}

inline uint64_t Min(uint64_t a, uint64_t b)
{
    return a > b ? b : a;
}

// build a reader reading from volume (block device)
std::shared_ptr<VolumeBlockReader> VolumeBlockReader::BuildVolumeReader(
    std::shared_ptr<VolumeTaskSharedConfig> sharedConfig,
    std::shared_ptr<VolumeTaskSharedContext> sharedContext)
{
    std::string volumePath = sharedConfig->volumePath;
    uint64_t offset = sharedConfig->sessionOffset;
    uint32_t length = sharedConfig->sessionSize;
    auto dataReader = std::dynamic_pointer_cast<native::DataReader>(
        std::make_shared<native::VolumeDataReader>(volumePath));
    if (!dataReader->Ok()) {
        ERRLOG("failed to init VolumeDataReader, path = %s, error = %u", volumePath.c_str(), dataReader->Error());
    }
    VolumeBlockReaderParam param {
        SourceType::VOLUME,
        volumePath,
        offset,
        dataReader,
        sharedConfig,
        sharedContext
    };
    return std::make_shared<VolumeBlockReader>(param);
}

// build a reader reading from volume copy file
std::shared_ptr<VolumeBlockReader> VolumeBlockReader::BuildCopyReader(
    std::shared_ptr<VolumeTaskSharedConfig> sharedConfig,
    std::shared_ptr<VolumeTaskSharedContext> sharedContext)
{
    std::string copyFilePath = sharedConfig->copyFilePath;
    uint64_t offset = 0; // read copy file from beginning
    uint32_t length = sharedConfig->sessionSize;
    auto dataReader = std::dynamic_pointer_cast<native::DataReader>(
        std::make_shared<native::FileDataReader>(copyFilePath));
    if (!dataReader->Ok()) {
        ERRLOG("failed to init FileDataReader, path = %s, error = %u", copyFilePath.c_str(), dataReader->Error());
    }
    VolumeBlockReaderParam param {
        SourceType::COPYFILE,
        copyFilePath,
        offset,
        dataReader,
        sharedConfig,
        sharedContext
    };
    return std::make_shared<VolumeBlockReader>(param);
}

bool VolumeBlockReader::Start()
{
    if (m_status != TaskStatus::INIT) {
        return false;
    }
    m_status = TaskStatus::RUNNING;
    // check data reader
    if (!m_dataReader) {
        ERRLOG("dataReader is nullptr, path = %s", m_sourcePath.c_str());
        m_status = TaskStatus::FAILED;
        return false;
    }
    if (!m_dataReader->Ok()) {
        ERRLOG("invalid dataReader, path = %s", m_sourcePath.c_str());
        m_status = TaskStatus::FAILED;
        return false;
    }
    m_sharedContext->counter->bytesToRead = m_sharedConfig->sessionSize;
    m_readerThread = std::thread(&VolumeBlockReader::MainThread, this);
    return true;
}

VolumeBlockReader::~VolumeBlockReader()
{
    if (m_readerThread.joinable()) {
        m_readerThread.join();
    }
    m_dataReader.reset();
}

VolumeBlockReader::VolumeBlockReader(const VolumeBlockReaderParam& param)
 : m_sourceType(param.sourceType),
    m_sourcePath(param.sourcePath),
    m_sourceOffset(param.sourceOffset),
    m_sharedConfig(param.sharedConfig),
    m_sharedContext(param.sharedContext),
    m_dataReader(param.dataReader)
{
    uint64_t numBlocks = m_sharedConfig->sessionSize / m_sharedConfig->blockSize;
    if (m_sharedConfig->sessionSize % m_sharedConfig->blockSize != 0) {
        numBlocks++;
    }
    m_currentIndex = 0;
    m_maxIndex = (numBlocks == 0) ? 0 : numBlocks - 1;
}

/**
 * @brief redirect current index from checkpoint bitmap
 */
uint64_t VolumeBlockReader::InitIndex() const
{
    uint64_t index = 0;
    if (m_sharedConfig->checkpointEnabled) {
        index = m_sharedContext->processedBitmap->FirstIndexUnset();
        INFOLOG("init index to %llu from ProcessedBitmap for continuation", index);
    }
    return index;
}

void VolumeBlockReader::MainThread()
{
    // Open the device file for reading
    m_currentIndex = InitIndex(); // used to locate position of a block within a session

    // read from currentOffset
    DBGLOG("reader start from index: %llu/%llu, src offset: %llu , length: %llu", m_currentIndex, m_maxIndex, m_sourceOffset, m_sharedConfig->sessionSize);

    while (true) {
        DBGLOG("reader thread check, index %llu/%llu", m_currentIndex, m_maxIndex);
        if (IsReadCompleted()) { // read completed
            m_status = TaskStatus::SUCCEED;
            INFOLOG("reader read completed successfully");
            break;
        }

        if (m_abort) {
            m_status = TaskStatus::ABORTED;
            break;
        }

        if (SkipReadingBlock()) {
            RevertNextBlock();
            continue;
        }

        char* buffer = FetchBlockBuffer(std::chrono::seconds(1));
        if (buffer == nullptr) {
            m_status = TaskStatus::FAILED;
            break;
        }
        uint32_t nBytesReaded = 0;
        if (!ReadBlock(buffer, nBytesReaded)) {
            m_status = TaskStatus::FAILED;
            break;
        }
        // push readed block to queue (convert to reader offset to sessionOffset)
        uint64_t consumeBlockOffset = m_currentIndex * m_sharedConfig->blockSize + m_sharedConfig->sessionOffset;
        BlockingPushForward(VolumeConsumeBlock { buffer, m_currentIndex, consumeBlockOffset, nBytesReaded });
        RevertNextBlock();
    }
    // handle terminiation (success/fail/aborted)
    m_sharedConfig->hasherEnabled ? m_sharedContext->hashingQueue->Finish() : m_sharedContext->writeQueue->Finish();
    INFOLOG("reader thread terminated");
    return;
}

void VolumeBlockReader::BlockingPushForward(const VolumeConsumeBlock& consumeBlock) const
{
    DBGLOG("reader push consume block (%llu, %llu, %u)",
        consumeBlock.index, consumeBlock.volumeOffset, consumeBlock.length);
    if (m_sharedConfig->hasherEnabled) {
        m_sharedContext->hashingQueue->BlockingPush(consumeBlock);
    } else {
        m_sharedContext->writeQueue->BlockingPush(consumeBlock);
    }
    return;
}

bool VolumeBlockReader::SkipReadingBlock() const
{
    if (m_sharedConfig->checkpointEnabled &&
        m_sharedContext->processedBitmap->Test(m_currentIndex)) {
        return true;
    }
    DBGLOG("checkpoint enabled, reader skip reading current index: %llu", m_currentIndex);
    return false;
}

bool VolumeBlockReader::IsReadCompleted() const
{
    return m_currentIndex > m_maxIndex;
}

void VolumeBlockReader::RevertNextBlock()
{
    ++m_currentIndex;
}

char* VolumeBlockReader::FetchBlockBuffer(std::chrono::seconds timeout) const
{
    auto start = std::chrono::steady_clock::now();
    while (true) {
        char* buffer = m_sharedContext->allocator->bmalloc();
        if (buffer != nullptr) {
            return buffer;
        }
        auto now = std::chrono::steady_clock::now();
        if ((now - start).count() >= timeout.count()) {
            ERRLOG("malloc block buffer timeout!");
            return nullptr;
        }
        DBGLOG("failed to malloc, retry in 100ms");
        std::this_thread::sleep_for(FETCH_BLOCK_BUFFER_SLEEP_INTERVAL);
    }
    return nullptr;
}

bool VolumeBlockReader::ReadBlock(char* buffer, uint32_t& nBytesToRead)
{
    native::ErrCodeType errorCode = 0;
    uint32_t blockSize = m_sharedConfig->blockSize;
    uint64_t currentOffset = m_sourceOffset + m_currentIndex * m_sharedConfig->blockSize;
    uint64_t bytesRemain = m_sharedConfig->sessionSize - m_currentIndex * blockSize;
    nBytesToRead = static_cast<uint32_t>(Min(bytesRemain, static_cast<uint64_t>(blockSize)));
    if (!m_dataReader->Read(currentOffset, buffer, nBytesToRead, errorCode)) {
        ERRLOG("failed to read %u bytes, error code = %u", nBytesToRead, errorCode);
        return false;
    }
    m_sharedContext->counter->bytesRead += static_cast<uint64_t>(nBytesToRead);
    return true;
}
