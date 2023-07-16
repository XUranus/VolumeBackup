#ifdef __linux__
#include <fcntl.h>
#include <unistd.h>
#endif

#ifdef _WIN32
#endif

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
#include "SystemIOInterface.h"

using namespace volumeprotect;

// build a reader reading from volume (block device)
std::shared_ptr<VolumeBlockReader> VolumeBlockReader::BuildVolumeReader(
    const std::string& blockDevicePath,
    uint64_t offset,
    uint64_t length,
    std::shared_ptr<VolumeTaskSession> session)
{
    return std::make_shared<VolumeBlockReader>(
        VolumeBlockReader::SourceType::VOLUME,
        blockDevicePath,
        offset,
        length,
        session
    );
}

// build a reader reading from volume copy file
std::shared_ptr<VolumeBlockReader> VolumeBlockReader::BuildCopyReader(
    const std::string& copyFilePath,
    uint64_t offset,
    uint64_t length,
    std::shared_ptr<VolumeTaskSession> session)
{
    return std::make_shared<VolumeBlockReader>(
        VolumeBlockReader::SourceType::COPYFILE,
        copyFilePath,
        offset,
        length,
        session
    );
}

bool VolumeBlockReader::Start()
{
    if (m_status != TaskStatus::INIT) {
        return false;
    }
    m_status = TaskStatus::RUNNING;
    m_readerThread = std::thread(&VolumeBlockReader::ReaderThread, this);
    return true;
}

VolumeBlockReader::~VolumeBlockReader() {
    if (m_readerThread.joinable()) {
        m_readerThread.join();
    }
}

VolumeBlockReader::VolumeBlockReader(
    VolumeBlockReader::SourceType sourceType,
    const std::string& sourcePath,
    uint64_t    sourceOffset,
    uint64_t    sourceLength,
    std::shared_ptr<VolumeTaskSession> session
) : m_sourceType(sourceType),
    m_sourcePath(sourcePath),
    m_sourceOffset(sourceOffset),
    m_sourceLength(sourceLength),
    m_session(session)
{}

void VolumeBlockReader::ReaderThread()
{
    // Open the device file for reading
    uint32_t defaultBufferSize = m_session->blockSize;
    uint64_t currentOffset = m_sourceOffset;
    uint32_t nBytesToRead = 0;
    system::IOHandle handle = system::OpenVolumeForRead(m_sourcePath);
    if (!system::IsValidIOHandle(handle)) {
        ERRLOG("Failed to open %s for read, %d", m_sourcePath.c_str(), errno);
        m_status = TaskStatus::FAILED;
        return;
    }
    
    if (!system::SetIOPointer(handle, m_sourceOffset)) { // read from m_sourceOffset
        ERRLOG("failed to read from %llu", m_sourceOffset);
    }
    m_session->counter->bytesToRead += m_sourceLength;
    DBGLOG("reader thread start, sourceOffset: %lu ", m_sourceOffset);

    while (true) {
        DBGLOG("reader thread check, sourceOffset: %lu, sourceLength %lu, currentOffset: %lu",
            m_sourceOffset, m_sourceLength, currentOffset);
        if (m_abort) {
            m_status = TaskStatus::ABORTED;
            system::CloseVolume(handle);
            return;
        }

        if (m_sourceOffset + m_sourceLength <= currentOffset) {
            // read completed
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
            nBytesToRead = m_sourceOffset + m_sourceLength - currentOffset;
        }
        
        uint32_t errorCode = 0;
        if (!system::ReadVolumeData(handle, buffer, nBytesToRead, errorCode)) {
            ERRLOG("failed to read %u bytes, error code = %u", nBytesToRead, errorCode);
            system::CloseVolume(handle);
            m_status = TaskStatus::FAILED;
            return;
        }
        // push readed block to queue (convert to reader offset to sessionOffset)
        VolumeConsumeBlock consumeBlock {
            buffer,
            (currentOffset - m_sourceOffset + m_session->sessionOffset),
            nBytesToRead
        };
        DBGLOG("reader push consume block (%p, %lu, %lu)",
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
    system::CloseVolume(handle);
    if (m_session->hasherEnabled) {
        m_session->hashingQueue->Finish();
    } else {
        m_session->writeQueue->Finish();
    }
    return;
}
