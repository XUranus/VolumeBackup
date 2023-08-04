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
    constexpr auto READER_CHECK_SLEEP_INTERVAL = std::chrono::seconds(1);
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
        length,
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
        length,
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
    m_sourceLength(param.sourceLength),
    m_sharedConfig(param.sharedConfig),
    m_sharedContext(param.sharedContext),
    m_dataReader(param.dataReader)
{}

/**
 * @brief redirect current offset/index from checkpoint bitmap
 */
uint64_t VolumeBlockReader::InitCurrentOffset() const
{
    uint64_t currentOffset = m_sourceOffset;
    if (m_sharedConfig->checkpointEnabled) {
        uint64_t index = m_sharedContext->processedBitmap->FirstIndexUnset();
        currentOffset += index * m_sharedConfig->blockSize;
        INFOLOG("init current offset to %llu from ProcessedBitmap for continuation", currentOffset);
    }
    return currentOffset;
}

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
    uint64_t index = InitIndex(); // used to locate position of a block within a session
    uint64_t currentOffset = InitCurrentOffset();
    uint64_t bytesRemain = m_sourceLength;
    uint32_t nBytesToRead = 0;
    native::ErrCodeType errorCode = 0;

    // read from currentOffset
    m_sharedContext->counter->bytesToRead = bytesRemain;
    DBGLOG("reader start, index: %llu, src offset: %llu , length: %llu", index, m_sourceOffset, m_sourceLength);

    while (true) {
        bytesRemain =  m_sourceOffset + m_sourceLength - currentOffset;
        DBGLOG("thread check, current offset %llu, remain: %llu", currentOffset, bytesRemain);
        if (bytesRemain <= currentOffset) { // read completed
            m_status = TaskStatus::SUCCEED;
            INFOLOG("reader read completed successfully");
            break;
        }

        if (m_abort) {
            m_status = TaskStatus::ABORTED;
            break;
        }

        char* buffer = m_sharedContext->allocator->bmalloc();
        if (buffer == nullptr) {
            DBGLOG("failed to malloc, retry in 100ms");
            std::this_thread::sleep_for(READER_CHECK_SLEEP_INTERVAL);
            continue;
        }
        nBytesToRead = static_cast<uint32_t>(Min(bytesRemain, static_cast<uint64_t>(m_sharedConfig->blockSize)));
        if (!m_dataReader->Read(currentOffset, buffer, nBytesToRead, errorCode)) {
            ERRLOG("failed to read %u bytes, error code = %u", nBytesToRead, errorCode);
            m_status = TaskStatus::FAILED;
            break;
        }
        // push readed block to queue (convert to reader offset to sessionOffset)
        uint64_t consumeBlockOffset = currentOffset - m_sourceOffset + m_sharedConfig->sessionOffset;
        VolumeConsumeBlock consumeBlock { buffer, index++, consumeBlockOffset, nBytesToRead };
        DBGLOG("push block (%llu, %llu, %u)", consumeBlock.index, consumeBlock.volumeOffset, consumeBlock.length);
        if (m_sharedConfig->hasherEnabled) {
            ++m_sharedContext->counter->blocksToHash;
            m_sharedContext->hashingQueue->BlockingPush(consumeBlock);
        } else {
            m_sharedContext->writeQueue->BlockingPush(consumeBlock);
        }
        currentOffset += static_cast<uint64_t>(nBytesToRead);
        m_sharedContext->counter->bytesRead += static_cast<uint64_t>(nBytesToRead);
    }
    // handle terminiation (success/fail/aborted)
    m_sharedConfig->hasherEnabled ? m_sharedContext->hashingQueue->Finish() : m_sharedContext->writeQueue->Finish();
    INFOLOG("reader thread terminated");
    return;
}
