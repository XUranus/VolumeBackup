#ifdef __linux__
#include <fcntl.h> 
#include <unistd.h>
#endif

#ifdef _WIN32
#endif

#include <algorithm>
#include <cerrno>
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
#include "VolumeProtector.h"
#include "VolumeBlockWriter.h"
#include "VolumeUtils.h"
#include "SystemIOInterface.h"

using namespace volumeprotect;

// build a writer writing to copy file
std::shared_ptr<VolumeBlockWriter> VolumeBlockWriter::BuildCopyWriter(
    std::shared_ptr<VolumeTaskSession> session)
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
    std::shared_ptr<VolumeTaskSession> session)
{
    std::string blockDevicePath = session->blockDevicePath;
    // check target block device
    if (!util::IsBlockDeviceExists(blockDevicePath)) {
        ERRLOG("block device %s not exists", blockDevicePath.c_str());
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
    if (!Prepare()) {
        m_status = TaskStatus::FAILED;
        return false;
    }
    m_status = TaskStatus::RUNNING;
    m_writerThread = std::thread(&VolumeBlockWriter::WriterThread, this);
    return true;
}

VolumeBlockWriter::~VolumeBlockWriter()
{
    if (m_writerThread.joinable()) {
        m_writerThread.join();
    }
}

VolumeBlockWriter::VolumeBlockWriter(
    TargetType targetType,
    const std::string& targetPath,
    std::shared_ptr<VolumeTaskSession> session
) : m_targetType(targetType),
    m_targetPath(targetPath),
    m_session(session)
{}

bool VolumeBlockWriter::Prepare()
{
    // open writer target file handle
    if (m_targetType == TargetType::COPYFILE) {
        // truncate copy file to session size
        DBGLOG("truncate target copy file %s to size %lu", m_targetPath.c_str(), m_session->sessionSize);
        if (!system::TruncateCreateFile(m_targetPath, m_session->sessionSize)) {
            ERRLOG("failed to truncate create file %s with size %lu, error code = %u",
                m_targetPath.c_str(),
                m_session->sessionSize,
                system::GetLastError());
            return false;
        }
    }
    
    if (m_targetType == TargetType::VOLUME) {
        system::IOHandle handle = system::OpenVolumeForWrite(m_targetPath.c_str());
        if (system::IsValidIOHandle(handle)) {
            ERRLOG("open block device %s failed, error code = %u", m_targetPath.c_str(), system::GetLastError());
            return false;
        }
        system::CloseVolume(handle);
    }
    return true;
}

void VolumeBlockWriter::WriterThread()
{
    VolumeConsumeBlock consumeBlock {};
    DBGLOG("writer thread start");

    system::IOHandle handle = system::OpenVolumeForWrite(m_targetPath);
    if (!system::IsValidIOHandle(handle)) {
        ERRLOG("open %s failed, error code = %u", m_targetPath.c_str(), system::GetLastError());
        return;
    }

    while (true) {
        if (m_abort) {
            m_status = TaskStatus::ABORTED;
            system::CloseVolume(handle);
            return;
        }
        DBGLOG("check writer thread");

        if (!m_session->writeQueue->Pop(consumeBlock)) {
            break; // queue has been finished
        }

        char* buffer = consumeBlock.ptr;
        uint64_t writerOffset = consumeBlock.volumeOffset;
        uint32_t len = consumeBlock.length;

        DBGLOG("writer pop consume block (%p, %lu, %lu)",
            consumeBlock.ptr, consumeBlock.volumeOffset, consumeBlock.length);

        // 1. volume => file   (file writer),   writerOffset = volumeOffset - sessionOffset
        // 2. file   => volume (volume writer), writerOffset = volumeOffset
        if (m_targetType == TargetType::COPYFILE) {
            writerOffset = consumeBlock.volumeOffset - m_session->sessionOffset;
        }
        if (!system::SetIOPointer(handle, writerOffset)) {
            ERRLOG("failed to set write offset to %lu", writerOffset);
        }
        uint32_t errorCode = 0;
        if (!system::WriteVolumeData(handle, buffer, len, errorCode)) {
            ERRLOG("write %lu bytes failed, error code = %u", writerOffset, errorCode);
            m_status = TaskStatus::FAILED;
            m_session->allocator->bfree(buffer);
            system::CloseVolume(handle);
            return;
        }

        m_session->allocator->bfree(buffer);
        m_session->counter->bytesWritten += len;
    }
    INFOLOG("writer read completed successfully");
    m_status = TaskStatus::SUCCEED;
    system::CloseVolume(handle);
    return;
}