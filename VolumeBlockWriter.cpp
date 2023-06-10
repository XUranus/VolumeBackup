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
    std::shared_ptr<VolumeBackupSession> session)
{
    std::string copyFilePath = session->copyFilePath;
    // TODO:: check target copy file
    return std::make_shared<VolumeBlockWriter>(
        TargetType::COPYFILE,
        copyFilePath,
        session
    );
}

// build a writer writing to volume
std::shared_ptr<VolumeBlockWriter> VolumeBlockWriter::BuildVolumeWriter(
    std::shared_ptr<VolumeBackupSession> session)
{
    std::string blockDevicePath = session->config->blockDevicePath;
    // check target block device
    if (util::IsBlockDeviceExists(blockDevicePath)) {
        ERRLOG("block device % not exists", blockDevicePath.c_str());
        return nullptr;
    }
    return std::make_shared<VolumeBlockWriter>(
        TargetType::VOLUME,
        blockDevicePath,
        session
    );
}

bool VolumeBlockWriter::Start()
{
    if (m_status != TaskStatus::INIT) {
        return false;
    }
    m_status = TaskStatus::RUNNING;
    m_writerThread = std::thread(&VolumeBlockWriter::WriterThread, this);
    return true;
}

VolumeBlockWriter::~VolumeBlockWriter()
{
    if (m_writerThread.joinable()) {
        VolumeBlockWriter.join();
    }
}

VolumeBlockWriter::VolumeBlockWriter(
    TargetType targetType,
    const std::string& targetPath,
    std::shared_ptr<VolumeBackupSession> session
) : m_targetType(targetType),
    m_targetPath(targetPath),
    m_session(session)
{}

void VolumeBlockWriter::WriterThread()
{
    VolumeConsumeBlock consumeBlock {};
    // open writer file handle
    int fd = ::open(m_targetPath.c_str() ,O_RDWR | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR);
    if (fd < 0) {
        m_status = TaskStatus::FAILED;
        return;
    }

    while (true) {
        if (m_abort) {
            m_status = TaskStatus::ABORTED;
            ::close(fd);
            return;
        }
    
        if (!m_session->hashingQueue.Pop(consumeBlock)) {
            break; // queue has been finished
        }

        char* buffer = consumeBlock.ptr;
        uint32_t len = consumeBlock.length;
        uint64_t writerOffset = consumeBlock.volumeOffset;

        // 1. volume => file   (file writer),   writerOffset = volumeOffset - sessionOffset
        // 2. file   => volume (volume writer), writerOffset = volumeOffset
        if (m_targetType == TargetType::COPYFILE) {
            writerOffset = volumeOffset - m_session.sessionOffset;
        }
        
        ::lseek(fd, writerOffset, SEEK_SET);
        int n = ::write(fd, buffer, len);
        if (n != len) {
            m_status = TaskStatus::FAILED;
            ::close(fd);
            m_session->allocator.bfree(buffer);
            return;
        }

        m_session->allocator.bfree(buffer);
        m_session->bytesWritten += len;
    }

    m_status = TaskStatus::SUCCEED;
    ::close(fd);
    return;
}