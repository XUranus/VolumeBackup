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
    if (native::IsFileExists(copyFilePath) &&
        native::GetFileSize(copyFilePath) == sharedConfig->sessionSize) {
        // should be full copy file, this task should be increment backup
        DBGLOG("copy file already exists, using %s", copyFilePath.c_str());
        return nullptr;
    }
    // truncate copy file to session size
    DBGLOG("truncate target copy file %s to size %llu",copyFilePath.c_str(), sharedConfig->sessionSize);
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
    VolumeBlockWriterParam param {
        TargetType::COPYFILE,
        copyFilePath,
        sharedConfig,
        sharedContext,
        dataWriter
    };
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
    VolumeBlockWriterParam param {
        TargetType::VOLUME,
        volumePath,
        sharedConfig,
        sharedContext,
        dataWriter
    };
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

bool VolumeBlockWriter::Flush()
{
    m_dataWriter->Flush();
}

VolumeBlockWriter::~VolumeBlockWriter()
{
    DBGLOG("destroy VolumeBlockWriter");
    if (m_writerThread.joinable()) {
        m_writerThread.join();
    }
    m_dataWriter.reset();
}

VolumeBlockWriter::VolumeBlockWriter(const VolumeBlockWriterParam& param)
  : m_targetType(param.targetType),
    m_targetPath(param.targetPath),
    m_sharedConfig(param.sharedConfig),
    m_sharedContext(param.sharedContext),
    m_dataWriter(param.dataWriter)
{}

void VolumeBlockWriter::MainThread()
{
    VolumeConsumeBlock consumeBlock {};
    native::ErrCodeType errorCode = 0;
    DBGLOG("writer thread start");

    while (true) {
        DBGLOG("writer thread check");
        if (m_abort) {
            m_status = TaskStatus::ABORTED;
            break;
        }

        if (!m_sharedContext->writeQueue->BlockingPop(consumeBlock)) {
            // queue has been finished
            m_status = TaskStatus::SUCCEED;
            break;
        }

        uint8_t* buffer = consumeBlock.ptr;
        uint64_t writerOffset = consumeBlock.volumeOffset;
        uint32_t length = consumeBlock.length;
        uint64_t index = consumeBlock.index;

        // 1. volume => file   (file writer),   writerOffset = volumeOffset - sessionOffset
        // 2. file   => volume (volume writer), writerOffset = volumeOffset
        if (m_targetType == TargetType::COPYFILE) {
            writerOffset = consumeBlock.volumeOffset - m_sharedConfig->sessionOffset;
        }
        DBGLOG("write block[%llu] (%p, %llu, %u) writerOffset = %llu",
            index, buffer, consumeBlock.volumeOffset, length, writerOffset);
        if (!m_dataWriter->Write(writerOffset, reinterpret_cast<char*>(buffer), length, errorCode)) {
            ERRLOG("write %llu bytes failed, error code = %u", writerOffset, errorCode);
            m_sharedContext->allocator->bfree(buffer);
            ++m_sharedContext->counter->blockesWriteFailed;
            // writer should not return (otherwise writer queue may block reader)
        }

        m_sharedContext->writtenBitmap->Set(index);
        m_sharedContext->processedBitmap->Set(index);
        m_sharedContext->allocator->bfree(buffer);
        m_sharedContext->counter->bytesWritten += length;
    }
    if (m_status == TaskStatus::SUCCEED && m_sharedContext->counter->blockesWriteFailed != 0) {
        ERRLOG("%llu blockes failed to write, set writer status to fail");
    }
    INFOLOG("writer read terminated with status %s", GetStatusString().c_str());
    return;
}