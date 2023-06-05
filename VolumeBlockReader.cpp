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

#include "VolumeBlockReader.h"
#include "VolumeBackupUtils.h"

namespace {
    const uint32_t DEFAULT_BUFFER_SIZE = 4 * 1024 * 1024; // 4MB 
}

using namespace volumebackup;

// build a reader reading from volume (block device)
std::shared_ptr<VolumeBlockReader> VolumeBlockReader::BuildVolumeReader(
    const std::string& blockDevicePath,
    uint64_t offset,
    uint64_t length,
    const std::shared_ptr<VolumeBackupContext> context)
{
    return std::make_shared<VolumeBlockReader>(
        VolumeBlockReader::SourceType::VOLUME,
        blockDevicePath,
        offset,
        length,
        context
    );
}

// build a reader reading from volume copy
std::shared_ptr<VolumeBlockReader> VolumeBlockReader::BuildCopyReader(
    const std::string& copyFilePath,
    uint64_t offset,
    uint64_t length,
    std::shared_ptr<VolumeBackupContext> context)
{
    return std::make_shared<VolumeBlockReader>(
        VolumeBlockReader::SourceType::COPYFILE,
        copyFilePath,
        offset,
        length,
        context
    );
}

VolumeBlockReader::VolumeBlockReader(
    VolumeBlockReader::SourceType sourceType,
    std::string sourcePath,
    uint64_t    sourceOffset,
    uint64_t    sourceLength,
    std::shared_ptr<VolumeBackupContext> context
) : m_sourceType(sourceType),
    m_sourcePath(sourcePath),
    m_sourceOffset(sourceOffset),
    m_sourceLength(sourceLength),
    m_context(context)
{}

bool VolumeBlockReader::Start()
{
    m_readerThread = std::thread(&VolumeBlockReader::ReaderThread, this);
    return true;
}

void VolumeBlockReader::ReaderThread()
{
    // Open the device file for reading
    uint32_t bufferSize = m_context->config.blockSize;
    uint64_t currentOffset = m_sourceOffset;
    int fd = ::open(m_sourcePath.c_str(), O_RDONLY);
    uint32_t nBytesToRead = 0;

    if (fd < 0) {
        throw BuildRuntimeException("Failed to open the device file for read", m_sourcePath, errno);
        m_failed = true;
        return;
    }
    
    ::lseek(fd, m_sourceOffset, SEEK_SET);
    m_context->bytesReaded += m_sourceLength;

    while (m_sourceOffset + m_sourceLength < currentOffset) {
        auto buffer = m_context->allocator.bmalloc();
        if (buffer == nullptr) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }
        nBytesToRead = static_cast<uint32_t>(std::min(
            bufferSize,
            static_cast<uint32_t>(currentOffset + bufferSize - (m_sourceOffset + m_sourceLength))));
        int n = ::read(fd, buffer, nBytesToRead);
        if (n != nBytesToRead) { // read failed, size mismatch
            ::close(fd);
            m_failed = true;
            return;
        }
        // read success
        if (m_context->config.hasherEnabled) {
            m_context->hashingQueue.Push(VolumeConsumeBlock { buffer, currentOffset, nBytesToRead});
        }
        currentOffset += static_cast<uint64_t>(nBytesToRead);
        m_context->bytesReaded += static_cast<uint64_t>(nBytesToRead);

        ::close(fd);
    }
    return;
}