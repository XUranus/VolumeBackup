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
#include "NativeIOInterface.h"

using namespace volumeprotect;

// build a writer writing to copy file
std::shared_ptr<VolumeBlockWriter> VolumeBlockWriter::BuildCopyWriter(
    std::shared_ptr<VolumeTaskSharedConfig> sharedConfig,
    std::shared_ptr<VolumeTaskSharedContext> sharedContext)
{
    std::string copyFilePath = sharedConfig->copyFilePath;
    if (native::IsFileExists(copyFilePath) && native::GetFileSize(copyFilePath) == sharedConfig->sessionSize) {
        // should be full copy file, this task should be increment backup
        DBGLOG("copy file already exists, using %s", copyFilePath.c_str());
        return nullptr;
    }
    // truncate copy file to session size
    DBGLOG("truncate target copy file %s to size %llu", copyFilePath.c_str(), sharedConfig->sessionSize);
    native::ErrCodeType errorCode = 0;
    if (!native::TruncateCreateFile(copyFilePath, sharedConfig->sessionSize, errorCode)) {
        ERRLOG("failed to truncate create file %s with size %llu, error code = %u",
            copyFilePath.c_str(), sharedConfig->sessionSize, errorCode);
        return nullptr;
    }
    // init data writer
    auto dataWriter = std::dynamic_pointer_cast<native::DataWriter>(
        std::make_shared<native::FileDataWriter>(copyFilePath));
    if (!dataWriter->Ok()) {
        ERRLOG("failed to init FileDataWriter, path = %s, error = %u", copyFilePath.c_str(), dataWriter->Error());
        return nullptr;
    }
    VolumeBlockWriterParam param { TargetType::COPYFILE, copyFilePath, sharedConfig, sharedContext, dataWriter };
    return std::make_shared<VolumeBlockWriter>(param);
}

// build a writer writing to volume
std::shared_ptr<VolumeBlockWriter> VolumeBlockWriter::BuildVolumeWriter(
    std::shared_ptr<VolumeTaskSharedConfig> sharedConfig,
    std::shared_ptr<VolumeTaskSharedContext> sharedContext)
{
    std::string volumePath = sharedConfig->volumePath;
    // check target block device valid to write
    auto dataWriter = std::dynamic_pointer_cast<native::DataWriter>(
        std::make_shared<native::VolumeDataWriter>(volumePath));
    if (!dataWriter->Ok()) {
        ERRLOG("failed to init VolumeDataWriter, path = %s, error = %u", volumePath.c_str(), dataWriter->Error());
        return nullptr;
    }
    VolumeBlockWriterParam param { TargetType::VOLUME, volumePath, sharedConfig, sharedContext, dataWriter };
    return std::make_shared<VolumeBlockWriter>(param);
}

bool VolumeBlockWriter::Start()
{
    if (m_status != TaskStatus::INIT) {
        return false;
    }
    m_status = TaskStatus::RUNNING;
    // check data writer
    if (!m_dataWriter) {
        ERRLOG("dataWriter is nullptr, path = %s", m_targetPath.c_str());
        m_status = TaskStatus::FAILED;
        return false;
    }
    if (!m_dataWriter->Ok()) {
        ERRLOG("invalid dataWriter, path = %s", m_targetPath.c_str());
        m_status = TaskStatus::FAILED;
        return false;
    }
    m_writerThread = std::thread(&VolumeBlockWriter::MainThread, this);
    return true;
}

VolumeBlockWriter::~VolumeBlockWriter()
{
    if (m_writerThread.joinable()) {
        m_writerThread.join();
    }
    m_dataWriter.reset();
}

VolumeBlockWriter::VolumeBlockWriter(const VolumeBlockWriterParam& param)
  : m_targetType(param.targetType),
    m_targetPath(param.targetPath),
    m_sharedContext(param.sharedContext),
    m_sharedConfig(param.sharedConfig),
    m_dataWriter(param.dataWriter)
{}

void VolumeBlockWriter::MainThread()
{
    VolumeConsumeBlock consumeBlock {};
    native::ErrCodeType errorCode = 0;
    DBGLOG("writer thread start");

    while (true) {
        if (m_abort) {
            m_status = TaskStatus::ABORTED;
            return;
        }
        DBGLOG("check writer thread");

        if (!m_sharedContext->writeQueue->Pop(consumeBlock)) {
            break; // queue has been finished
        }

        char* buffer = consumeBlock.ptr;
        uint64_t writerOffset = consumeBlock.volumeOffset;
        uint32_t len = consumeBlock.length;

        // 1. volume => file   (file writer),   writerOffset = volumeOffset - sessionOffset
        // 2. file   => volume (volume writer), writerOffset = volumeOffset
        if (m_targetType == TargetType::COPYFILE) {
            writerOffset = consumeBlock.volumeOffset - m_sharedConfig->sessionOffset;
        }
        DBGLOG("writer pop consume block (%p, %llu, %u) writerOffset = %llu",
            consumeBlock.ptr, consumeBlock.volumeOffset, consumeBlock.length, writerOffset);
        if (!m_dataWriter->Write(writerOffset, buffer, len, errorCode)) {
            ERRLOG("write %llu bytes failed, error code = %u", writerOffset, errorCode);
            m_status = TaskStatus::FAILED;
            m_sharedContext->allocator->bfree(buffer);
            return;
        }

        m_sharedContext->allocator->bfree(buffer);
        m_sharedContext->counter->bytesWritten += len;
    }
    INFOLOG("writer read completed successfully");
    m_status = TaskStatus::SUCCEED;
    return;
}