#include "Logger.h"
#include "VolumeProtector.h"
#include "VolumeProtectTaskContext.h"
#include "VolumeUtils.h"
#include "VolumeBlockReader.h"
#include "VolumeBlockWriter.h"
#include "BlockingQueue.h"
#include "native/FileSystemAPI.h"
#include "VolumeRestoreTask.h"

using namespace volumeprotect;
using namespace volumeprotect::util;

namespace {
    constexpr auto TASK_CHECK_SLEEP_INTERVAL = std::chrono::seconds(1);
}

VolumeRestoreTask::VolumeRestoreTask(const VolumeRestoreConfig& restoreConfig)
    : m_restoreConfig(std::make_shared<VolumeRestoreConfig>(restoreConfig))
    // TODO::
    // m_resourceManager(TaskResourceManager::BuildRestoreTaskResourceManager(RestoreTaskResourceManagerParams {
    //     backupConfig.copyFormat,
    //     backupConfig.outputCopyDataDirPath,
    //     backupConfig.copyName
    // }))
{}

VolumeRestoreTask::~VolumeRestoreTask()
{
    DBGLOG("destroy volume restore task, wait main thread to join");
    if (m_thread.joinable()) {
        m_thread.join();
    }
    DBGLOG("reset restore resource manager");
    m_resourceManager.reset();
    DBGLOG("volume restore task destroyed");
}

bool VolumeRestoreTask::Start()
{
    AssertTaskNotStarted();
    if (!Prepare()) {
        ERRLOG("prepare task failed");
        m_status = TaskStatus::FAILED;
        return false;
    }
    m_status = TaskStatus::RUNNING;
    m_thread = std::thread(&VolumeRestoreTask::ThreadFunc, this);
    return true;
}

TaskStatistics VolumeRestoreTask::GetStatistics() const
{
    std::lock_guard<std::mutex> lock(m_statisticMutex);
    return m_completedSessionStatistics + m_currentSessionStatistics;
}

// split session and write back
bool VolumeRestoreTask::Prepare()
{
    std::string volumePath = m_restoreConfig->volumePath;

    // 1. read copy meta json and validate volume
    VolumeCopyMeta volumeCopyMeta {};
    if (!ReadVolumeCopyMeta(m_restoreConfig->copyMetaDirPath, volumeCopyMeta)) {
        ERRLOG("failed to write copy meta to dir: %s", m_restoreConfig->copyMetaDirPath.c_str());
        return false;
    }

    // 2. prepare restore resource
    if (!m_resourceManager->PrepareCopyResource()) {
        ERRLOG("failed to prepare copy resource for restore task");
        return false;
    }

    // 3. read volume info and validate
    if (!ValidateRestoreTask(volumeCopyMeta)) {
        ERRLOG("failed to validate restore task!");
        return false;
    }

    // 4. split session
    uint64_t volumeSize = volumeCopyMeta.volumeSize;
    CopyType copyType = static_cast<CopyType>(volumeCopyMeta.copyType);
    CopyFormat copyFormat = static_cast<CopyFormat>(volumeCopyMeta.copyFormat);
    for (const CopySegment& segment: volumeCopyMeta.segments) {
        uint64_t sessionOffset = segment.offset;
        uint64_t sessionSize = segment.length;
        int sessionIndex = segment.index;
        INFOLOG("Size = %llu sessionOffset %d sessionSize %d", volumeSize, sessionOffset, sessionSize);
        std::string copyFilePath = util::GetCopyDataFilePath(
            m_restoreConfig->copyDataDirPath, volumeCopyMeta.copyName, copyFormat, sessionIndex);
        VolumeTaskSession session {};
        session.sharedConfig = std::make_shared<VolumeTaskSharedConfig>();
        session.sharedConfig->copyFormat = static_cast<CopyFormat>(volumeCopyMeta.copyFormat);
        session.sharedConfig->volumePath = volumePath;
        session.sharedConfig->hasherEnabled = false;
        session.sharedConfig->blockSize = volumeCopyMeta.blockSize;
        session.sharedConfig->sessionOffset = sessionOffset;
        session.sharedConfig->sessionSize = sessionSize;
        session.sharedConfig->copyFilePath = copyFilePath;
        session.sharedConfig->checkpointEnabled = m_restoreConfig->enableCheckpoint;

        m_sessionQueue.push(session);
    }

    return true;
}

bool VolumeRestoreTask::ValidateRestoreTask(const VolumeCopyMeta& volumeCopyMeta) const
{
    if (!fsapi::IsDirectoryExists(m_restoreConfig->copyDataDirPath)
        || !fsapi::IsDirectoryExists(m_restoreConfig->copyMetaDirPath)) {
        ERRLOG("data directory %s or meta directory %s not exists!",
            m_restoreConfig->copyDataDirPath.c_str(), m_restoreConfig->copyMetaDirPath.c_str());
        return false;
    }
    uint64_t volumeSize = 0;
    try {
        volumeSize = fsapi::ReadVolumeSize(m_restoreConfig->volumePath);
    } catch (const SystemApiException& e) {
        ERRLOG("retrive volume size got exception: %s", e.what());
        return false;
    }
    if (volumeSize != volumeCopyMeta.volumeSize) {
        ERRLOG("restore volume size mismatch ! (copy : %llu, target: %llu)", volumeCopyMeta.volumeSize, volumeSize);
    }
    return true;
}

bool VolumeRestoreTask::InitRestoreSessionTaskExecutor(std::shared_ptr<VolumeTaskSession> session) const
{
    session->readerTask = VolumeBlockReader::BuildCopyReader(
        session->sharedConfig,
        session->sharedContext
    );
    if (session->readerTask == nullptr) {
        ERRLOG("restore session failed to init reader task");
        return false;
    }

    // 4. check and init writer
    session->writerTask = VolumeBlockWriter::BuildVolumeWriter(
        session->sharedConfig,
        session->sharedContext
    );
    if (session->writerTask == nullptr) {
        ERRLOG("restore session failed to init writer task");
        return false;
    }
    return true;
}

bool VolumeRestoreTask::InitRestoreSessionContext(std::shared_ptr<VolumeTaskSession> session) const
{
    DBGLOG("init restore session context");
    // 1. init basic restore container
    session->sharedContext = std::make_shared<VolumeTaskSharedContext>();
    session->sharedContext->counter = std::make_shared<SessionCounter>();
    session->sharedContext->allocator = std::make_shared<VolumeBlockAllocator>(
        session->sharedConfig->blockSize,
        DEFAULT_ALLOCATOR_BLOCK_NUM);
    session->sharedContext->writeQueue = std::make_shared<BlockingQueue<VolumeConsumeBlock>>(DEFAULT_QUEUE_SIZE);
    InitSessionBitmap(session);
    // 2. restore checkpoint if restarted
    RestoreSessionCheckpoint(session);
    // 3. check and init task executor
    return InitRestoreSessionTaskExecutor(session);
}

bool VolumeRestoreTask::ReadVolumeCopyMeta(const std::string& copyMetaDirPath, VolumeCopyMeta& volumeCopyMeta)
{
    return util::ReadVolumeCopyMeta(copyMetaDirPath, volumeCopyMeta);
}

bool VolumeRestoreTask::StartRestoreSession(std::shared_ptr<VolumeTaskSession> session) const
{
    DBGLOG("start restore session");
    if (session->readerTask == nullptr || session->writerTask == nullptr) {
        ERRLOG("restore session member nullptr! readerTask: %p writerTask: %p ",
            session->readerTask.get(), session->writerTask.get());
        return false;
    }
    DBGLOG("start restore session reader");
    if (!session->readerTask->Start()) {
        ERRLOG("restore session readerTask start failed");
        return false;
    }
    DBGLOG("start restore session writer");
    if (!session->writerTask->Start()) {
        ERRLOG("restore session writerTask start failed");
        return false;
    }
    return true;
}

void VolumeRestoreTask::ThreadFunc()
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

        if (!InitRestoreSessionContext(session)) {
            m_status = TaskStatus::FAILED;
            return;
        }
        RestoreSessionCheckpoint(session);
        if (!StartRestoreSession(session)) {
            session->Abort();
            m_status = TaskStatus::FAILED;
            return;
        }
        // block the thread
        while (true) {
            if (m_abort) {
                session->Abort();
                m_status = TaskStatus::ABORTED;
                return;
            }
            if (session->IsFailed()) {
                ERRLOG("session failed");
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
        FlushSessionWriter(session);
        FlushSessionBitmap(session);
        UpdateCompletedSessionStatistics(session);
    }
    m_status = TaskStatus::SUCCEED;
    return;
}