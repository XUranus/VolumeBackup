#include "Logger.h"
#include "VolumeProtector.h"
#include "native/RawIO.h"
#include "VolumeBlockWriter.h"

using namespace volumeprotect;
using namespace volumeprotect::rawio;

// build a writer writing to copy file
std::shared_ptr<VolumeBlockWriter> VolumeBlockWriter::BuildCopyWriter(
    std::shared_ptr<VolumeTaskSharedConfig> sharedConfig,
    std::shared_ptr<VolumeTaskSharedContext> sharedContext)
{
    std::string copyFilePath = sharedConfig->copyFilePath;
    // init data writer
    SessionCopyRawIOParam sessionIOParam {};
    sessionIOParam.copyFormat = sharedConfig->copyFormat;
    sessionIOParam.volumeOffset = sharedConfig->sessionOffset;
    sessionIOParam.length = sharedConfig->sessionSize;
    sessionIOParam.copyFilePath = sharedConfig->copyFilePath;

    std::shared_ptr<RawDataWriter> dataWriter = rawio::OpenRawDataCopyWriter(sessionIOParam);
    if (dataWriter == nullptr) {
        ERRLOG("failed to build copy data writer");
        return nullptr;
    }
    if (!dataWriter->Ok()) {
        ERRLOG("failed to init copy data writer, format = %d, copyfile = %s, error = %u",
            sharedConfig->copyFormat, sharedConfig->copyFilePath.c_str(), dataWriter->Error());
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
    std::shared_ptr<RawDataWriter> dataWriter = rawio::OpenRawDataVolumeWriter(volumePath);
    if (dataWriter == nullptr) {
        ERRLOG("failed to build volume data reader");
        return nullptr;
    }
    if (!dataWriter->Ok()) {
        ERRLOG("failed to init VolumeDataWriter, path = %s, error = %u",
            volumePath.c_str(), dataWriter->Error());
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

bool VolumeBlockWriter::NeedToWrite(uint8_t* buffer, int length) const
{
    if (!m_sharedConfig->skipEmptyBlock) {
        return true;
    }
    if (buffer[0] == 0 && !::memcmp(buffer, buffer + 1, length - 1)) {
        // is all zero
        return true;
    }
    return false;
}

void VolumeBlockWriter::MainThread()
{
    VolumeConsumeBlock consumeBlock {};
    ErrCodeType errorCode = 0;
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

        DBGLOG("write block[%llu] (%p, %llu, %u) writerOffset = %llu",
            index, buffer, consumeBlock.volumeOffset, length, writerOffset);
        if (NeedToWrite(buffer, length) &&
            !m_dataWriter->Write(writerOffset, buffer, length, errorCode)) {
            ERRLOG("write %d bytes at %llu failed, error code = %u", length, writerOffset, errorCode);
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