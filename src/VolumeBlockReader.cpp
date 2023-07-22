#include <algorithm>
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
#include "VolumeBlockReader.h"
#include "VolumeUtils.h"
#include "NativeIOInterface.h"

using namespace volumeprotect;

// build a reader reading from volume (block device)
std::shared_ptr<VolumeBlockReader> VolumeBlockReader::BuildVolumeReader(
    const std::string& volumePath,
    uint64_t offset,
    uint64_t length,
    std::shared_ptr<VolumeTaskSession> session)
{
    auto dataReader = std::dynamic_pointer_cast<native::DataReader>(
        std::make_shared<native::VolumeDataReader>(volumePath));
    if (!dataReader->Ok()) {
        ERRLOG("failed to init VolumeDataReader, path = %s, error = %u", volumePath.c_str(), dataReader->Error());
    }
    VolumeBlockReaderParam param { SourceType::VOLUME, volumePath, offset, length, session, dataReader};
    return std::make_shared<VolumeBlockReader>(param);
}

// build a reader reading from volume copy file
std::shared_ptr<VolumeBlockReader> VolumeBlockReader::BuildCopyReader(
    const std::string& copyFilePath,
    uint64_t offset,
    uint64_t length,
    std::shared_ptr<VolumeTaskSession> session)
{
    auto dataReader = std::dynamic_pointer_cast<native::DataReader>(
        std::make_shared<native::FileDataReader>(copyFilePath));
    if (!dataReader->Ok()) {
        ERRLOG("failed to init FileDataReader, path = %s, error = %u", copyFilePath.c_str(), dataReader->Error());
    }
    VolumeBlockReaderParam param { SourceType::COPYFILE, copyFilePath, offset, length, session, dataReader};
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

VolumeBlockReader::~VolumeBlockReader() {
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
    m_session(param.session),
    m_dataReader(param.dataReader)
{}

void VolumeBlockReader::MainThread()
{
    // Open the device file for reading
    uint32_t defaultBufferSize = m_session->blockSize;
    uint64_t currentOffset = m_sourceOffset;
    uint32_t nBytesToRead = 0;
    native::ErrCodeType errorCode = 0;

    // read from currentOffset
    m_session->counter->bytesToRead += m_sourceLength;
    DBGLOG("reader thread start, sourceOffset: %llu ", m_sourceOffset);

    while (true) {
        DBGLOG("reader thread check, sourceOffset: %llu, sourceLength %llu, currentOffset: %llu",
            m_sourceOffset, m_sourceLength, currentOffset);
        if (m_abort) {
            m_status = TaskStatus::ABORTED;
            return;
        }

        if (m_sourceOffset + m_sourceLength <= currentOffset) { // read completed
            break;
        }

        char* buffer = m_session->allocator->bmalloc();
        if (buffer == nullptr) {
            DBGLOG("failed to malloc, retry in 100ms");
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }
        nBytesToRead = defaultBufferSize;
        if (m_sourceOffset + m_sourceLength - currentOffset < defaultBufferSize) {
            nBytesToRead = static_cast<uint32_t>(m_sourceOffset + m_sourceLength - currentOffset);
        }
        if (!m_dataReader->Read(currentOffset, buffer, nBytesToRead, errorCode)) {
            ERRLOG("failed to read %u bytes, error code = %u", nBytesToRead, errorCode);
            m_status = TaskStatus::FAILED;
            return;
        }
        // push readed block to queue (convert to reader offset to sessionOffset)
        VolumeConsumeBlock consumeBlock {
            buffer,
            (currentOffset - m_sourceOffset + m_session->sessionOffset),
            nBytesToRead
        };
        DBGLOG("reader push consume block (%p, %llu, %u)",
            consumeBlock.ptr, consumeBlock.volumeOffset, consumeBlock.length);
        if (m_session->hasherEnabled) {
            ++m_session->counter->blocksToHash;
            m_session->hashingQueue->Push(consumeBlock);
        } else {
            m_session->writeQueue->Push(consumeBlock);
        }
        currentOffset += static_cast<uint64_t>(nBytesToRead);
        m_session->counter->bytesRead += static_cast<uint64_t>(nBytesToRead);
    }
    // handle success
    INFOLOG("reader read completed successfully");
    m_status = TaskStatus::SUCCEED;
    if (m_session->hasherEnabled) {
        m_session->hashingQueue->Finish();
    } else {
        m_session->writeQueue->Finish();
    }
    return;
}
