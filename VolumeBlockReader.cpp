#include <algorithm>
#include <string>
#include <iostream>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/fs.h>
#include <thread>
#include <chrono>
#include <unistd.h>
#include <cstring>
#include <cstdint>
#include <stdexcept>
#include <fstream>
#include <memory>
#include <cassert>
#include <algorithm>

#include "Logger.h"
#include "VolumeBlockReader.h"
#include "VolumeBackupUtils.h"

using namespace volumebackup;

// build a reader reading from volume (block device)
std::shared_ptr<VolumeBlockReader> VolumeBlockReader::BuildVolumeReader(
    const std::string& blockDevicePath,
    uint64_t offset,
    uint64_t length,
    std::shared_ptr<VolumeBackupSession> session)
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
    std::shared_ptr<VolumeBackupSession> session)
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
    m_readerThread = std::thread(&VolumeBlockReader::ReaderThread, this);
    m_status = TaskStatus::RUNNING;
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
    std::shared_ptr<VolumeBackupSession> session
) : m_sourceType(sourceType),
    m_sourcePath(sourcePath),
    m_sourceOffset(sourceOffset),
    m_sourceLength(sourceLength),
    m_session(session)
{}

void VolumeBlockReader::ReaderThread()
{
    // Open the device file for reading
    uint32_t defaultBufferSize = m_session->config->blockSize;
    uint64_t currentOffset = m_sourceOffset;
    uint32_t nBytesToRead = 0;
    int fd = ::open(m_sourcePath.c_str(), O_RDONLY);
    if (fd < 0) {
        ERRLOG("Failed to open %s for read, %d", m_sourcePath.c_str(), errno);
        m_status = TaskStatus::FAILED;
        return;
    }
    
    ::lseek(fd, m_sourceOffset, SEEK_SET); // read from m_sourceOffset
    m_session->counter->bytesToRead += m_sourceLength;
    DBGLOG("reader thread start, sourceOffset: %lu ", m_sourceOffset);

    while (true) {
        DBGLOG("reader thread check, sourceOffset: %lu, sourceLength %lu, currentOffset: %lu",
            m_sourceOffset, m_sourceLength, currentOffset);
        if (m_abort) {
            m_status = TaskStatus::ABORTED;
            ::close(fd);
            return;
        }

        if (m_sourceOffset + m_sourceLength <= currentOffset) {
            // read completed
            INFOLOG("reader read completed");
            break;
        }

        char* buffer = m_session->allocator->bmalloc();
        if (buffer == nullptr) {
            DBGLOG("failed to malloc, retry in 100ms");
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }
        nBytesToRead = static_cast<uint32_t>(std::min(
            defaultBufferSize,
            static_cast<uint32_t>(currentOffset + defaultBufferSize - (m_sourceOffset + m_sourceLength))));
        int n = ::read(fd, buffer, nBytesToRead);
        if (n != nBytesToRead) { // read failed, size mismatch
            ERRLOG("failed to read %u bytes, ret = %d", nBytesToRead, n);
            ::close(fd);
            m_status = TaskStatus::FAILED;
            return;
        }
        // push readed block to queue (convert to reader offset to sessionOffset)
        VolumeConsumeBlock consumeBlock {
            buffer,
            (currentOffset - m_sourceOffset + m_session->sessionOffset),
            nBytesToRead
        };
        if (m_session->config->hasherEnabled) {
            m_session->hashingQueue->Push(consumeBlock);
        } else {
            m_session->writeQueue->Push(consumeBlock);
        }
        currentOffset += static_cast<uint64_t>(nBytesToRead);
        m_session->counter->bytesRead += static_cast<uint64_t>(nBytesToRead);
        m_sourceOffset += nBytesToRead;
    }
    
    m_status = TaskStatus::SUCCEED;
    ::close(fd);
    return;
}