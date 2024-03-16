/**
 * @copyright Copyright 2023 XUranus. All rights reserved.
 * @license This project is released under the Apache License.
 * @author XUranus(2257238649wdx@gmail.com)
 */

#include "Logger.h"
#include "VolumeProtector.h"
#include "VolumeProtectTaskContext.h"
#include "VolumeUtils.h"
#include "VolumeZeroCopyRestoreTask.h"
#include "native/RawIO.h"

#ifdef __linux__
#include <sys/sendfile.h>
#endif

using namespace volumeprotect;
using namespace volumeprotect::task;
using namespace volumeprotect::common;
using namespace volumeprotect::rawio;

namespace {
    constexpr auto TASK_CHECK_SLEEP_INTERVAL = std::chrono::seconds(1);
}

static std::vector<std::string> GetCopyFilesFromCopyMeta(const VolumeCopyMeta& volumeCopyMeta)
{
    std::vector<std::string> files;
    for (const auto& segment : volumeCopyMeta.segments) {
        files.push_back(segment.copyDataFile);
    }
    return files;
}

VolumeZeroCopyRestoreTask::VolumeZeroCopyRestoreTask(const VolumeRestoreConfig& restoreConfig, const VolumeCopyMeta& volumeCopyMeta)
    : m_restoreConfig(std::make_shared<VolumeRestoreConfig>(restoreConfig)),
    m_volumeCopyMeta(std::make_shared<VolumeCopyMeta>(volumeCopyMeta)),
    m_resourceManager(TaskResourceManager::BuildRestoreTaskResourceManager(RestoreTaskResourceManagerParams {
        static_cast<CopyFormat>(volumeCopyMeta.copyFormat),
        restoreConfig.copyDataDirPath,
        volumeCopyMeta.copyName,
        GetCopyFilesFromCopyMeta(volumeCopyMeta)
    }))
{
    CopyFormat copyFormat = static_cast<CopyFormat>(m_volumeCopyMeta->copyFormat);
    if (copyFormat != CopyFormat::IMAGE || !restoreConfig.enableZeroCopy) {
        throw std::runtime_error("only image format supported for zero copy");
        // support CopyFormat::IMAGE only
    }
}

VolumeZeroCopyRestoreTask::~VolumeZeroCopyRestoreTask()
{
    DBGLOG("destroy volume zero copy restore task, wait main thread to join");
    if (m_thread.joinable()) {
        m_thread.join();
    }
    DBGLOG("reset zero copy restore resource manager");
    m_resourceManager.reset();
    DBGLOG("volume zero copy restore destroyed");
}

bool VolumeZeroCopyRestoreTask::Start()
{
    AssertTaskNotStarted();
    if (!Prepare()) {
        ERRLOG("prepare task failed");
        m_status = TaskStatus::FAILED;
        return false;
    }
    m_status = TaskStatus::RUNNING;
    m_thread = std::thread(&VolumeZeroCopyRestoreTask::ThreadFunc, this);
    return true;
}

TaskStatistics VolumeZeroCopyRestoreTask::GetStatistics() const
{
    std::lock_guard<std::mutex> lock(m_statisticMutex);
    return m_completedSessionStatistics + m_currentSessionStatistics;
}

// split session and write back
bool VolumeZeroCopyRestoreTask::Prepare()
{
    std::string volumePath = m_restoreConfig->volumePath;

    // 2. prepare zero copy restore resource
    if (!m_resourceManager->PrepareCopyResource()) {
        ERRLOG("failed to prepare copy resource for zero copy");
        return false;
    }

    // 4. split session
    uint64_t volumeSize = m_volumeCopyMeta->volumeSize;
    CopyFormat copyFormat = static_cast<CopyFormat>(m_volumeCopyMeta->copyFormat);
    for (const CopySegment& segment: m_volumeCopyMeta->segments) {
        uint64_t sessionOffset = segment.offset;
        uint64_t sessionSize = segment.length;
        int sessionIndex = segment.index;
        INFOLOG("Size = %llu sessionOffset %d sessionSize %d", volumeSize, segment.offset, sessionSize);
        std::string copyFilePath = common::GetCopyDataFilePath(
            m_restoreConfig->copyDataDirPath, m_volumeCopyMeta->copyName, copyFormat, sessionIndex);

        VolumeTaskSharedConfig sessionConfig;
        sessionConfig.copyFormat = static_cast<CopyFormat>(m_volumeCopyMeta->copyFormat);
        sessionConfig.volumePath = volumePath;
        sessionConfig.hasherEnabled = false;
        sessionConfig.blockSize = m_volumeCopyMeta->blockSize;
        sessionConfig.sessionOffset = sessionOffset;
        sessionConfig.sessionSize = sessionSize;
        sessionConfig.copyFilePath = copyFilePath;
        sessionConfig.checkpointFilePath = "";
        sessionConfig.checkpointEnabled = false;
        sessionConfig.skipEmptyBlock = false;
        m_sessionQueue.emplace(sessionConfig);
    }
    return true;
}

void VolumeZeroCopyRestoreTask::ThreadFunc()
{
    DBGLOG("start task main thread");
    CopyFormat copyFormat = static_cast<CopyFormat>(m_volumeCopyMeta->copyFormat);
    while (!m_sessionQueue.empty()) {
        if (m_abort) {
            m_status = TaskStatus::ABORTED;
            return;
        }
        // pop a session from session queue to init a new session
        VolumeTaskSharedConfig sessionConfig = m_sessionQueue.front();
        m_sessionQueue.pop();
        std::shared_ptr<RawDataReader> dataReader = rawio::OpenRawDataCopyReader(SessionCopyRawIOParam {
            sessionConfig.copyFormat,
            sessionConfig.copyFilePath,
            sessionConfig.sessionOffset,
            sessionConfig.sessionSize
        });
        std::shared_ptr<RawDataWriter> dataWriter = rawio::OpenRawDataVolumeWriter(sessionConfig.volumePath);
        // check reader writer valid
        if (dataReader == nullptr || dataWriter == nullptr) {
            ERRLOG("failed to build copy data reader or writer");
            m_status = TaskStatus::FAILED;
            return;
        }
        if (!dataReader->Ok() || !dataWriter->Ok()) {
            ERRLOG("failed to init copy data reader, format = %d, copyfile = %s, error = %u, %u",
                sessionConfig.copyFormat, sessionConfig.copyFilePath.c_str(), dataReader->Error(), dataWriter->Error());
            m_status = TaskStatus::FAILED;
            return;
        }
        if (!PerformZeroCopyRestore(dataReader, dataWriter, sessionConfig)) {
            ERRLOG("session (%llu, %llu) failed during copy", sessionConfig.sessionOffset, sessionConfig.sessionSize);
            m_status = TaskStatus::FAILED;
            return;
        }
    }
    DBGLOG("exit zero copy main thread, all session succeed");
    m_status = TaskStatus::SUCCEED;
    return;
}

bool VolumeZeroCopyRestoreTask::PerformZeroCopyRestore(
    std::shared_ptr<RawDataReader> copyDataReader,
    std::shared_ptr<RawDataWriter> volumeDataWriter,
    const VolumeTaskSharedConfig& sessionConfig)
{
    {
        std::lock_guard<std::mutex> lock(m_statisticMutex);
        m_completedSessionStatistics = m_completedSessionStatistics + m_currentSessionStatistics;
        memset(&m_currentSessionStatistics, 0, sizeof(TaskStatistics));
        m_currentSessionStatistics.bytesToRead = sessionConfig.sessionSize;
        m_currentSessionStatistics.bytesToWrite = sessionConfig.sessionSize;
    }

    uint64_t offset = sessionConfig.sessionOffset;
    size_t len = sessionConfig.blockSize;
    uint64_t sessionMax = sessionConfig.sessionOffset + sessionConfig.sessionSize;
    INFOLOG("perform zero copy restore, offset %llu, len %ld, sessionMax %llu", offset, sessionMax);
    while (offset < sessionMax) {
        if (offset + len > sessionMax) {
            len = sessionMax - offset;
        }
#ifdef __linux__
        int ret = ::sendfile(volumeDataWriter->Handle(), copyDataReader->Handle(), reinterpret_cast<off_t*>(&offset), len);
        DBGLOG("sendfile syscall return = %d, offset = %llu, len = %llu", ret, offset, len);
        if (ret < 0) {
            ERRLOG("sendfile (%llu, %llu) failed with errno %d", offset, len, errno);
            return false;
        }
#endif
        m_currentSessionStatistics.bytesRead += ret;
        m_currentSessionStatistics.bytesWritten += ret;
    }


    return true;
}