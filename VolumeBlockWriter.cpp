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

#include "VolumeBlockWriter.h"

using namespace volumebackup;

// build a writer writing to copy file
std::shared_ptr<VolumeBlockWriter> VolumeBlockWriter::BuildCopyWriter(
    const std::string& copyFilePath,
    std::shared_ptr<VolumeBackupContext> context)
{
    // TODO:: check target
    return std::make_shared<VolumeBlockWriter>(
        TargetType::COPYFILE,
        copyFilePath,
        context
    );
}

// build a writer writing to volume
std::shared_ptr<VolumeBlockWriter> VolumeBlockWriter::BuildVolumeWriter(
    const std::string& blockDevicePath,
    std::shared_ptr<VolumeBackupContext> context)
{
    // TODO:: check target
    return std::make_shared<VolumeBlockWriter>(
        TargetType::VOLUME,
        blockDevicePath,
        context
    );
}

bool VolumeBlockWriter::Start()
{
    m_writerThread = std::thread(&VolumeBlockWriter::WriterThread, this);
    return true;
}

VolumeBlockWriter::VolumeBlockWriter(
    TargetType targetType,
    const std::string& targetPath,
    std::shared_ptr<VolumeBackupContext> context
) : m_targetType(targetType),
    m_targetPath(targetPath),
    m_context(context)
{}

void VolumeBlockWriter::WriterThread()
{
    VolumeConsumeBlock consumeBlock;
    int fd = ::open(m_targetPath.c_str() ,O_RDWR | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR);
    while (true) {
        if (!m_context->hashingQueue.Pop(consumeBlock)) {
            break;; // pop failed, queue has been finished
        }

        uint64_t index = (consumeBlock.offset - m_context->config.sessionOffset) / m_context->config.blockSize;

        char* buffer = consumeBlock.ptr;
        uint32_t len = consumeBlock.length;
        uint64_t offset = consumeBlock.offset;
        
        ::lseek(fd, offset, SEEK_SET);
        int n = ::write(fd, buffer, len);
        if (n != len) {
            // TODO:: error
            ::close(fd);
            return;
        }

        m_context->allocator.bfree(buffer);
        m_context->bytesWrited += len;
    }

    ::close(fd);
}