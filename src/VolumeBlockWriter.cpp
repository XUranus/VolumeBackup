#include "Logger.h"
#include "VolumeProtector.h"
#include "native/RawIO.h"
#include "VolumeBlockWriter.h"

using namespace volumeprotect;

// build a writer writing to copy file
std::shared_ptr<VolumeBlockWriter> VolumeBlockWriter::BuildCopyWriter(
    std::shared_ptr<VolumeTaskSharedConfig> sharedConfig,
    std::shared_ptr<VolumeTaskSharedContext> sharedContext)
{
    std::string copyFilePath = sharedConfig->copyFilePath;
    std::shared_ptr<rawio::RawDataWriter> dataWriter = nullptr;
    if (fsapi::IsFileExists(copyFilePath) && fsapi::GetFileSize(copyFilePath) == sharedConfig->sessionSize) {
        // should be full copy file, this task should be forever increment backup
        INFOLOG("copy file already exists, using %s", copyFilePath.c_str());
    } else {
        // truncate copy file to session size
        DBGLOG("truncate new target copy file %s to size %llu", copyFilePath.c_str(), sharedConfig->sessionSize);
        fsapi::ErrCodeType errorCode = 0;
        if (!fsapi::TruncateCreateFile(copyFilePath, sharedConfig->sessionSize, errorCode)) {
            ERRLOG("failed to truncate create file %s with size %llu, error code = %u",
                copyFilePath.c_str(), sharedConfig->sessionSize, errorCode);
            return nullptr;
        }
    }
    // init data writer
    dataWriter = std::dynamic_pointer_cast<rawio::RawDataWriter>(
        std::make_shared<fsapi::FileDataWriter>(copyFilePath));
    if (dataWriter == nullptr || !dataWriter->Ok()) {
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
    auto dataWriter = std::dynamic_pointer_cast<rawio::RawDataWriter>(
        std::make_shared<fsapi::VolumeDataWriter>(volumePath));
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
    AssertTaskNotStarted();
    m_status = TaskStatus::RUNNING;
    // check data writer
    if (!m_dataWriter || !m_dataWriter->Ok()) {
        ERRLOG("invalid dataWriter %p, path = %s", m_dataWriter.get(), m_targetPath.c_str());
        m_status = TaskStatus::FAILED;
        return false;
    }
    m_writerThread = std::thread(&VolumeBlockWriter::MainThread, this);
    return true;
}

bool VolumeBlockWriter::Flush()
{
    return m_dataWriter->Flush();
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
    fsapi::ErrCodeType errorCode = 0;
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
        if (!m_dataWriter->Write(writerOffset, buffer, length, errorCode)) {
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
        m_status = TaskStatus::FAILED;
        ERRLOG("%llu blockes failed to write, set writer status to fail");
    }
    INFOLOG("writer read terminated with status %s", GetStatusString().c_str());
    return;
}