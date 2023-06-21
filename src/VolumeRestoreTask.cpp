#include <chrono>
#include <exception>
#include <memory>
#include <queue>
#include <string>
#include <sys/types.h>
#include <thread>
#include <vector>

#include "Logger.h"
#include "VolumeProtector.h"
#include "VolumeProtectTaskContext.h"
#include "VolumeUtils.h"
#include "VolumeBlockReader.h"
#include "VolumeBlockHasher.h"
#include "VolumeBlockWriter.h"
#include "BlockingQueue.h"
#include "VolumeRestoreTask.h"

using namespace volumeprotect;
using namespace volumeprotect::util;

namespace {
    constexpr auto DEFAULT_ALLOCATOR_BLOCK_NUM = 32;
    constexpr auto DEFAULT_QUEUE_SIZE = 32;
}

VolumeRestoreTask::VolumeRestoreTask(const VolumeRestoreConfig& restoreConfig)
 : m_restoreConfig(std::make_shared<VolumeRestoreConfig>(restoreConfig))
{}

VolumeRestoreTask::~VolumeRestoreTask()
{
    DBGLOG("destroy VolumeRestoreTask");
    if (m_thread.joinable()) {
        m_thread.join();
    }
}

bool VolumeRestoreTask::Start()
{
    if (m_status != TaskStatus::INIT) {
        return false;
    }
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
    return m_completedSessionStatistics + m_currentSessionStatistics;
}

bool VolumeRestoreTask::IsIncrementCopy() const
{
    return m_incrementCopy;
}

// split session and write back
bool VolumeRestoreTask::Prepare()
{
    std::string blockDevicePath = m_restoreConfig->blockDevicePath;
    
    // 1. read copy meta json and validate volume
    VolumeCopyMeta volumeCopyMeta {};
    if (!util::ReadVolumeCopyMeta(m_restoreConfig->copyMetaDirPath, volumeCopyMeta)) {
        ERRLOG("failed to write copy meta to dir: %s", m_restoreConfig->copyMetaDirPath.c_str());
        return false;
    }

    // TODO:: read volume info and validate

    // 2. split session
    uint64_t volumeSize = volumeCopyMeta.size;
    CopyType copyType = static_cast<CopyType>(volumeCopyMeta.copyType);
    for (const std::pair<uint64_t, uint64_t> slice: volumeCopyMeta.slices) {
        uint64_t sessionSize = slice.second;
        uint64_t sessionOffset = slice.first;
        std::string copyFilePath = util::GetCopyFilePath(
            m_restoreConfig->copyDataDirPath, copyType, sessionOffset, sessionSize);

        VolumeTaskSession session {};
        session.blockDevicePath = blockDevicePath;
        session.hasherEnabled = false;
        session.blockSize = volumeCopyMeta.blockSize;
        session.sessionOffset = sessionOffset;
        session.sessionSize = sessionSize;
        session.copyFilePath = copyFilePath;

        m_sessionQueue.push(session);
    }

    return true;
}

bool VolumeRestoreTask::InitRestoreSessionContext(std::shared_ptr<VolumeTaskSession> session) const
{
    DBGLOG("init restore session context");
    // 1. init basic restore container
    session->counter = std::make_shared<SessionCounter>();
    session->allocator = std::make_shared<VolumeBlockAllocator>(session->blockSize, DEFAULT_ALLOCATOR_BLOCK_NUM);
    session->writeQueue = std::make_shared<BlockingQueue<VolumeConsumeBlock>>(DEFAULT_QUEUE_SIZE);
    
    // 2. check and init reader
    session->reader = VolumeBlockReader::BuildCopyReader(
        session->copyFilePath,
        0,
        session->sessionSize,
        session
    );
    if (session->reader == nullptr) {
        ERRLOG("restore session failed to init reader");
        return false;
    }

    // // 3. check and init writer
    session->writer = VolumeBlockWriter::BuildVolumeWriter(session);
    if (session->writer == nullptr) {
        ERRLOG("restore session failed to init writer");
        return false;
    }
    return true;
}

bool VolumeRestoreTask::StartRestoreSession(std::shared_ptr<VolumeTaskSession> session) const
{
    DBGLOG("start restore session");
    if (session->reader == nullptr || session->writer == nullptr) {
        ERRLOG("restore session member nullptr! reader: %p writer: %p ",
            session->reader.get(), session->writer.get());
        return false;
    }
    DBGLOG("start restore session reader");
    if (!session->reader->Start()) {
        ERRLOG("restore session reader start failed");
        return false;
    }
    DBGLOG("start restore session writer");
    if (!session->writer->Start() ) {
        ERRLOG("restore session writer start failed");
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

        if (!InitRestoreSessionContext(session) || !StartRestoreSession(session)) {
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
            DBGLOG("updateStatistics: bytesToReaded: %llu, bytesRead: %llu, bytesToWrite: %llu, bytesWritten: %llu",
            session->counter->bytesToRead.load(), session->counter->bytesRead.load(),
            session->counter->bytesToWrite.load(), session->counter->bytesWritten.load());
            std::this_thread::sleep_for(std::chrono::seconds(5));
        }
        DBGLOG("session complete successfully");
        UpdateCompletedSessionStatistics(session);
    }
    
    m_status = TaskStatus::SUCCEED;
    return;
}

void VolumeRestoreTask::UpdateRunningSessionStatistics(std::shared_ptr<VolumeTaskSession> session)
{
    m_currentSessionStatistics.bytesToRead = session->counter->bytesToRead;
    m_currentSessionStatistics.bytesRead = session->counter->bytesRead;
    m_currentSessionStatistics.blocksToHash = session->counter->blocksToHash;
    m_currentSessionStatistics.blocksHashed = session->counter->blocksHashed;
    m_currentSessionStatistics.bytesToWrite = session->counter->bytesToWrite;
    m_currentSessionStatistics.bytesWritten = session->counter->bytesWritten;
}

void VolumeRestoreTask::UpdateCompletedSessionStatistics(std::shared_ptr<VolumeTaskSession> session)
{
    m_completedSessionStatistics.bytesToRead += session->counter->bytesToRead;
    m_completedSessionStatistics.bytesRead += session->counter->bytesRead;
    m_completedSessionStatistics.blocksToHash += session->counter->blocksToHash;
    m_completedSessionStatistics.blocksHashed += session->counter->blocksHashed;
    m_completedSessionStatistics.bytesToWrite += session->counter->bytesToWrite;
    m_completedSessionStatistics.bytesWritten += session->counter->bytesWritten;
    memset(&m_currentSessionStatistics, 0, sizeof(TaskStatistics));
}