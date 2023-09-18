#include "Logger.h"
#include "native/RawIO.h"
#include "native/FileSystemAPI.h"
#include "VolumeBlockReader.h"

using namespace volumeprotect;
using namespace volumeprotect::rawio;

namespace {
    constexpr auto FETCH_BLOCK_BUFFER_SLEEP_INTERVAL = std::chrono::milliseconds(100);
}

// build a reader reading from volume (block device)
std::shared_ptr<VolumeBlockReader> VolumeBlockReader::BuildVolumeReader(
    std::shared_ptr<VolumeTaskSharedConfig> sharedConfig,
    std::shared_ptr<VolumeTaskSharedContext> sharedContext)
{
    std::string volumePath = sharedConfig->volumePath;

    std::shared_ptr<RawDataReader> dataReader = rawio::OpenRawDataVolumeReader(volumePath);
    if (dataReader == nullptr) {
        ERRLOG("failed to build volume data reader");
        return nullptr;
    }
    if (!dataReader->Ok()) {
        ERRLOG("failed to init volume data reader, path = %s, error = %u",
            volumePath.c_str(), dataReader->Error());
        return nullptr;
    }
    VolumeBlockReaderParam param {
        SourceType::VOLUME,
        volumePath,
        sharedConfig->sessionOffset,
        dataReader,
        sharedConfig,
        sharedContext
    };
    return std::make_shared<VolumeBlockReader>(param);
}

// build a reader reading from volume copy file
std::shared_ptr<VolumeBlockReader> VolumeBlockReader::BuildCopyReader(
    std::shared_ptr<VolumeTaskSharedConfig> sharedConfig,
    std::shared_ptr<VolumeTaskSharedContext> sharedContext)
{
    std::string copyFilePath = sharedConfig->copyFilePath;

    SessionCopyRawIOParam sessionIOParam {};
    sessionIOParam.copyFormat = sharedConfig->copyFormat;
    sessionIOParam.volumeOffset = sharedConfig->sessionOffset;
    sessionIOParam.length = sharedConfig->sessionSize;
    sessionIOParam.copyFilePath = sharedConfig->copyFilePath;

    std::shared_ptr<RawDataReader> dataReader = rawio::OpenRawDataCopyReader(sessionIOParam);
    if (dataReader == nullptr) {
        ERRLOG("failed to build copy data reader");
        return nullptr;
    }
    if (!dataReader->Ok()) {
        ERRLOG("failed to init copy data reader, format = %d, copyfile = %s, error = %u",
            sharedConfig->copyFormat, sharedConfig->copyFilePath.c_str(), dataReader->Error());
        return nullptr;
    }
    VolumeBlockReaderParam param {
        SourceType::COPYFILE,
        copyFilePath,
        sharedConfig->sessionOffset,
        dataReader,
        sharedConfig,
        sharedContext
    };
    return std::make_shared<VolumeBlockReader>(param);
}

bool VolumeBlockReader::Start()
{
    AssertTaskNotStarted();
    m_status = TaskStatus::RUNNING;
    // check data reader
    if (!m_dataReader || !m_dataReader->Ok()) {
        ERRLOG("invalid dataReader %p, path = %s", m_dataReader.get(), m_sourcePath.c_str());
        m_status = TaskStatus::FAILED;
        return false;
    }
    m_sharedContext->counter->bytesToRead = m_sharedConfig->sessionSize;
    m_readerThread = std::thread(&VolumeBlockReader::MainThread, this);
    return true;
}

VolumeBlockReader::~VolumeBlockReader()
{
    DBGLOG("destroy VolumeBlockReader");
    if (m_readerThread.joinable()) {
        m_readerThread.join();
    }
    m_dataReader.reset();
}

VolumeBlockReader::VolumeBlockReader(const VolumeBlockReaderParam& param)
 : m_sourceType(param.sourceType),
    m_sourcePath(param.sourcePath),
    m_baseOffset(param.sourceOffset),
    m_sharedConfig(param.sharedConfig),
    m_sharedContext(param.sharedContext),
    m_dataReader(param.dataReader)
{
    uint64_t numBlocks = m_sharedConfig->sessionSize / m_sharedConfig->blockSize;
    if (m_sharedConfig->sessionSize % m_sharedConfig->blockSize != 0) {
        numBlocks++;
    }
    m_currentIndex = 0;
    m_maxIndex = (numBlocks == 0) ? 0 : numBlocks - 1;
}

void VolumeBlockReader::Pause()
{
    DBGLOG("pause reader");
    m_pause = true;
}

void VolumeBlockReader::Resume()
{
    DBGLOG("resume reader");
    m_pause = false;
}

/**
 * @brief redirect current index from checkpoint bitmap
 */
uint64_t VolumeBlockReader::InitCurrentIndex() const
{
    uint64_t index = 0;
    if (m_sharedConfig->checkpointEnabled) {
        index = m_sharedContext->processedBitmap->FirstIndexUnset();
        INFOLOG("init index to %llu from ProcessedBitmap for continuation", index);
    }
    return index;
}

void VolumeBlockReader::MainThread()
{
    // Open the device file for reading
    m_currentIndex = InitCurrentIndex(); // used to locate position of a block within a session
    // read from currentOffset
    DBGLOG("reader start from index: %llu/%llu, to read %llu bytes from base offset: %llu",
        m_currentIndex, m_maxIndex, m_sharedConfig->sessionSize, m_baseOffset);

    while (true) {
        DBGLOG("reader thread check, processing index %llu/%llu", m_currentIndex, m_maxIndex);
        if (IsReadCompleted()) { // read completed
            m_status = TaskStatus::SUCCEED;
            break;
        }

        if (m_abort) {
            m_status = TaskStatus::ABORTED;
            break;
        }

        if (m_pause) {
            DBGLOG("reader is paused, waiting...");
            std::this_thread::sleep_for(std::chrono::seconds(1));
            continue;
        }

        if (SkipReadingBlock()) {
            RevertNextBlock();
            continue;
        }

        uint8_t* buffer = FetchBlockBuffer(std::chrono::seconds(60));
        if (buffer == nullptr) {
            m_status = TaskStatus::FAILED;
            break;
        }
        uint32_t nBytesReaded = 0;
        if (!ReadBlock(buffer, nBytesReaded)) {
            m_status = TaskStatus::FAILED;
            break;
        }
        // push readed block to queue (convert to reader offset to sessionOffset)
        uint64_t consumeBlockOffset = m_currentIndex * m_sharedConfig->blockSize + m_sharedConfig->sessionOffset;
        BlockingPushForward(VolumeConsumeBlock { buffer, m_currentIndex, consumeBlockOffset, nBytesReaded });
        RevertNextBlock();
    }
    // handle terminiation (success/fail/aborted)
    m_sharedConfig->hasherEnabled ? m_sharedContext->hashingQueue->Finish() : m_sharedContext->writeQueue->Finish();
    INFOLOG("reader thread terminated with status %s", GetStatusString().c_str());
    return;
}

void VolumeBlockReader::BlockingPushForward(const VolumeConsumeBlock& consumeBlock) const
{
    DBGLOG("reader push consume block (%llu, %llu, %u)",
        consumeBlock.index, consumeBlock.volumeOffset, consumeBlock.length);
    if (m_sharedConfig->hasherEnabled) {
        m_sharedContext->hashingQueue->BlockingPush(consumeBlock);
        ++m_sharedContext->counter->blocksToHash;
    } else {
        m_sharedContext->writeQueue->BlockingPush(consumeBlock);
        m_sharedContext->counter->bytesToWrite += static_cast<uint64_t>(consumeBlock.length);
    }
    return;
}

bool VolumeBlockReader::SkipReadingBlock() const
{
    if (m_sharedConfig->checkpointEnabled &&
        m_sharedContext->processedBitmap->Test(m_currentIndex)) {
        DBGLOG("checkpoint enabled, reader skip reading current index: %llu", m_currentIndex);
        return true;
    }
    return false;
}

bool VolumeBlockReader::IsReadCompleted() const
{
    return m_currentIndex > m_maxIndex;
}

void VolumeBlockReader::RevertNextBlock()
{
    ++m_currentIndex;
}

uint8_t* VolumeBlockReader::FetchBlockBuffer(std::chrono::seconds timeout) const
{
    auto start = std::chrono::steady_clock::now();
    while (true) {
        uint8_t* buffer = m_sharedContext->allocator->bmalloc();
        if (buffer != nullptr) {
            return buffer;
        }
        auto now = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::seconds>(now - start);
        if (duration.count() >= timeout.count()) {
            ERRLOG("malloc block buffer timeout! %llu %llu", duration.count(), timeout.count());
            return nullptr;
        }
        DBGLOG("failed to malloc, retry in 100ms");
        std::this_thread::sleep_for(FETCH_BLOCK_BUFFER_SLEEP_INTERVAL);
    }
    return nullptr;
}

bool VolumeBlockReader::ReadBlock(uint8_t* buffer, uint32_t& nBytesToRead)
{
    ErrCodeType errorCode = 0;
    uint32_t blockSize = m_sharedConfig->blockSize;
    uint64_t currentOffset = m_baseOffset + m_currentIndex * m_sharedConfig->blockSize;
    uint64_t bytesRemain = m_sharedConfig->sessionSize - m_currentIndex * blockSize;
    if (bytesRemain < static_cast<uint64_t>(blockSize)) {
        nBytesToRead = static_cast<uint32_t>(bytesRemain);
    } else {
        nBytesToRead = blockSize;
    }

    if (!m_dataReader->Read(currentOffset, buffer, nBytesToRead, errorCode)) {
        ERRLOG("failed to read %u bytes, error code = %u", nBytesToRead, errorCode);
        return false;
    }
    m_sharedContext->counter->bytesRead += static_cast<uint64_t>(nBytesToRead);
    return true;
}
