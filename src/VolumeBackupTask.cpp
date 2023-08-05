#include "Logger.h"
#include "VolumeProtector.h"
#include "VolumeProtectTaskContext.h"
#include "VolumeUtils.h"
#include "VolumeBlockReader.h"
#include "VolumeBlockHasher.h"
#include "VolumeBlockWriter.h"
#include "BlockingQueue.h"
#include "VolumeBackupTask.h"

using namespace volumeprotect;
using namespace volumeprotect::util;

namespace {
    constexpr auto DEFAULT_ALLOCATOR_BLOCK_NUM = 32;
    constexpr auto DEFAULT_QUEUE_SIZE = 32;
    constexpr auto TASK_CHECK_SLEEP_INTERVAL = std::chrono::seconds(1);
    const uint64_t DEFAULT_CHECKSUM_TABLE_CAPACITY = 1024 * 1024 * 8; // 8MB
    const uint32_t SHA256_CHECKSUM_SIZE = 32; // 256bits
}

VolumeBackupTask::VolumeBackupTask(const VolumeBackupConfig& backupConfig, uint64_t volumeSize)
    : m_volumeSize(volumeSize),
    m_backupConfig(std::make_shared<VolumeBackupConfig>(backupConfig))
{}

VolumeBackupTask::~VolumeBackupTask()
{
    DBGLOG("destroy VolumeBackupTask");
    if (m_thread.joinable()) {
        m_thread.join();
    }
}

bool VolumeBackupTask::Start()
{
    AssertTaskNotStarted();
    if (!Prepare()) {
        ERRLOG("prepare task failed");
        m_status = TaskStatus::FAILED;
        return false;
    }
    m_status = TaskStatus::RUNNING;
    m_thread = std::thread(&VolumeBackupTask::ThreadFunc, this);
    return true;
}

TaskStatistics VolumeBackupTask::GetStatistics() const
{
    std::lock_guard<std::mutex> lock(m_statisticMutex);
    return m_completedSessionStatistics + m_currentSessionStatistics;
}

bool VolumeBackupTask::IsIncrementBackup() const
{
    return m_backupConfig->copyType == CopyType::INCREMENT;
}

// split session and save volume meta
bool VolumeBackupTask::Prepare()
{
    std::string volumePath = m_backupConfig->volumePath;
    // 1. fill volume meta info
    VolumeCopyMeta volumeCopyMeta {};
    volumeCopyMeta.copyType = static_cast<int>(m_backupConfig->copyType);
    volumeCopyMeta.volumeSize = m_volumeSize;
    volumeCopyMeta.blockSize = DEFAULT_BLOCK_SIZE;
    volumeCopyMeta.volumePath = volumePath;

    if (IsIncrementBackup()) {
        // TODO:: validate increment backup meta
    }

    // 2. split session
    for (uint64_t sessionOffset = 0; sessionOffset < m_volumeSize;) {
        uint64_t sessionSize = m_backupConfig->sessionSize;
        if (sessionOffset + m_backupConfig->sessionSize >= m_volumeSize) {
            sessionSize = m_volumeSize - sessionOffset;
        }
        std::string lastestChecksumBinPath = util::GetChecksumBinPath(
            m_backupConfig->outputCopyMetaDirPath, sessionOffset, sessionSize);
        std::string copyFilePath = util::GetCopyFilePath(
            m_backupConfig->outputCopyDataDirPath, sessionOffset, sessionSize);
        std::string writerBitmapPath = util::GetWriterBitmapFilePath(
            m_backupConfig->outputCopyMetaDirPath, sessionOffset, sessionSize);
        // for increment backup
        std::string prevChecksumBinPath = "";
        if (IsIncrementBackup()) {
            prevChecksumBinPath = util::GetChecksumBinPath(m_backupConfig->prevCopyMetaDirPath, sessionOffset, sessionSize);
        }

        VolumeTaskSession session {};
        session.sharedConfig = std::make_shared<VolumeTaskSharedConfig>();
        session.sharedConfig->volumePath = m_backupConfig->volumePath;
        session.sharedConfig->hasherEnabled = true;
        session.sharedConfig->hasherWorkerNum = m_backupConfig->hasherNum;
        session.sharedConfig->blockSize = m_backupConfig->blockSize;
        session.sharedConfig->sessionOffset = sessionOffset;
        session.sharedConfig->sessionSize = sessionSize;
        session.sharedConfig->lastestChecksumBinPath = lastestChecksumBinPath;
        session.sharedConfig->prevChecksumBinPath = prevChecksumBinPath;
        session.sharedConfig->copyFilePath = copyFilePath;
        session.sharedConfig->checkpointFilePath = writerBitmapPath;

        volumeCopyMeta.copySlices.emplace_back(sessionOffset, sessionSize);
        m_sessionQueue.push(session);
        sessionOffset += sessionSize;
    }

    if (!SaveVolumeCopyMeta(m_backupConfig->outputCopyMetaDirPath, volumeCopyMeta)) {
        ERRLOG("failed to write copy meta to dir: %s", m_backupConfig->outputCopyMetaDirPath.c_str());
        return false;
    }
    return true;
}

bool VolumeBackupTask::InitBackupSessionTaskExecutor(std::shared_ptr<VolumeTaskSession> session) const
{
    session->readerTask = VolumeBlockReader::BuildVolumeReader(session->sharedConfig, session->sharedContext);
    if (session->readerTask == nullptr) {
        ERRLOG("backup session failed to init reader");
        return false;
    }

    // 4. check and init hasher
    auto hasherMode = IsIncrementBackup() ? HasherForwardMode::DIFF : HasherForwardMode::DIRECT;
    session->hasherTask  = VolumeBlockHasher::BuildHasher(session->sharedConfig, session->sharedContext, hasherMode);
    if (session->hasherTask  == nullptr) {
        ERRLOG("backup session failed to init hasher");
        return false;
    }

    // 5. check and init writer
    session->writerTask  = VolumeBlockWriter::BuildCopyWriter(session->sharedConfig, session->sharedContext);
    if (session->writerTask  == nullptr) {
        ERRLOG("backup session failed to init writer");
        return false;
    }
    return true;
}

bool VolumeBackupTask::InitBackupSessionContext(std::shared_ptr<VolumeTaskSession> session) const
{
    DBGLOG("init backup session context");
    // 1. init basic backup container
    session->sharedContext = std::make_shared<VolumeTaskSharedContext>();
    session->sharedContext->counter = std::make_shared<SessionCounter>();
    session->sharedContext->allocator = std::make_shared<VolumeBlockAllocator>(
        session->sharedConfig->blockSize, DEFAULT_ALLOCATOR_BLOCK_NUM);
    session->sharedContext->hashingQueue = std::make_shared<BlockingQueue<VolumeConsumeBlock>>(DEFAULT_QUEUE_SIZE);
    session->sharedContext->writeQueue = std::make_shared<BlockingQueue<VolumeConsumeBlock>>(DEFAULT_QUEUE_SIZE);
    if (!InitHashingContext(session)) {
        ERRLOG("failed to init hashing context");
        return false;
    }
    InitSessionBitmap(session);
    // 2. restore checkpoint if restarted
    RestoreSessionCheckpoint(session);
    // 3. check and init task executor
    return InitBackupSessionTaskExecutor(session);
}

bool VolumeBackupTask::StartBackupSession(std::shared_ptr<VolumeTaskSession> session) const
{
    DBGLOG("start backup session");
    if (session->readerTask == nullptr || session->hasherTask  == nullptr || session->writerTask  == nullptr) {
        ERRLOG("backup session member nullptr! reader: %p hasher: %p writer: %p ",
            session->readerTask.get(), session->hasherTask.get(), session->writerTask.get());
        return false;
    }
    DBGLOG("start backup session reader");
    if (!session->readerTask->Start()) {
        ERRLOG("backup session reader start failed");
        return false;
    }
    DBGLOG("start backup session hasher");
    if (!session->hasherTask->Start()) {
        ERRLOG("backup session hasher start failed");
        return false;
    }
    DBGLOG("start backup session writer");
    if (!session->writerTask->Start()) {
        ERRLOG("backup session writer start failed");
        return false;
    }
    return true;
}

void VolumeBackupTask::ThreadFunc()
{
    DBGLOG("start task main thread");
    while (!m_sessionQueue.empty()) {
        if (m_abort) {
            m_status = TaskStatus::ABORTED;
            return;
        }
        // pop a session from session queue to init a new session
        std::shared_ptr<VolumeTaskSession> session = std::make_shared<VolumeTaskSession>(m_sessionQueue.front());
        m_sessionQueue.pop();

        if (!InitBackupSessionContext(session)) {
            m_status = TaskStatus::FAILED;
            return;
        }
        RestoreSessionCheckpoint(session);
        if (!StartBackupSession(session)) {
            m_status = TaskStatus::FAILED;
            return;
        }
        // block the thread
        auto counter = session->sharedContext->counter;
        while (true) {
            if (m_abort) {
                session->Abort();
                m_status = TaskStatus::ABORTED;
                return;
            }
            if (session->IsFailed()) {
                ERRLOG("backup session failed");
                m_status = TaskStatus::FAILED;
                return;
            }
            if (session->IsTerminated())  {
                break;
            }
            UpdateRunningSessionStatistics(session);
            RefreshSessionCheckpoint(session);
            std::this_thread::sleep_for(TASK_CHECK_SLEEP_INTERVAL);
        }
        DBGLOG("session complete successfully");
        FlushSessionLatestHashingTable(session);
        FlushSessionWriter(session);
        FlushSessionBitmap(session);
        UpdateCompletedSessionStatistics(session);
    }
    m_status = TaskStatus::SUCCEED;
    return;
}

bool VolumeBackupTask::InitHashingContext(std::shared_ptr<VolumeTaskSession> session) const
{
    // 1. allocate checksum table
    auto sharedConfig = session->sharedConfig;
    auto sharedContext = session->sharedContext;
    uint64_t lastestChecksumTableSize = session->TotalBlocks() * SHA256_CHECKSUM_SIZE;
    uint64_t prevChecksumTableSize = lastestChecksumTableSize;
    try {
        sharedContext->hashingContext = IsIncrementBackup() ?
            std::make_shared<BlockHashingContext>(prevChecksumTableSize, lastestChecksumTableSize)
            : std::make_shared<BlockHashingContext>(lastestChecksumTableSize);
    } catch (const std::exception& e) {
        ERRLOG("failed to malloc BlockHashingContext, length: %llu, message: %s", lastestChecksumTableSize, e.what());
        return false;
    }
    // 2. load previous checksum table from file if increment backup
    if (IsIncrementBackup() && !LoadSessionPreviousCopyChecksum(session)) {
        return false;
    }
    return true;
}

bool VolumeBackupTask::LoadSessionPreviousCopyChecksum(std::shared_ptr<VolumeTaskSession> session) const
{
    auto sharedConfig = session->sharedConfig;
    uint32_t blockCount = static_cast<uint32_t>(sharedConfig->sessionSize / sharedConfig->blockSize);
    uint64_t lastestChecksumTableSize = blockCount * SHA256_CHECKSUM_SIZE;
    uint64_t prevChecksumTableSize = lastestChecksumTableSize;
    uint8_t* buffer = native::ReadBinaryBuffer(session->sharedConfig->prevChecksumBinPath, prevChecksumTableSize);
    if (buffer == nullptr) {
        ERRLOG("failed to read previous checksum from %s", session->sharedConfig->prevChecksumBinPath.c_str());
        return false;
    }
    memcpy(session->sharedContext->hashingContext->previousTable, buffer, sizeof(uint8_t) * prevChecksumTableSize);
    delete[] buffer;
    buffer = nullptr;
    return true;
}

bool VolumeBackupTask::SaveVolumeCopyMeta(const std::string& copyMetaDirPath, const VolumeCopyMeta& volumeCopyMeta)
{
    return util::WriteVolumeCopyMeta(copyMetaDirPath, volumeCopyMeta);
}