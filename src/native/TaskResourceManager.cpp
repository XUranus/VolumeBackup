/**
 * @copyright Copyright 2023 XUranus. All rights reserved.
 * @license This project is released under the Apache License.
 * @author XUranus(2257238649wdx@gmail.com)
 */

#include "native/TaskResourceManager.h"
#include "common/VolumeUtils.h"
#include "Logger.h"
#include "native/RawIO.h"
#include "native/FileSystemAPI.h"

#ifdef __linux__
#include "native/linux/PosixRawIO.h"
#endif

#ifdef _WIN32
#include "native/win32/Win32RawIO.h"
#endif

using namespace volumeprotect;
using namespace volumeprotect::rawio;
using namespace volumeprotect::task;
using namespace volumeprotect::common;

namespace {
    constexpr auto DUMMY_SESSION_INDEX = 0;
}

// implement static util functions...


// return list of path and size
static std::vector<std::pair<std::string, uint64_t>> SplitFragmentBinaryBackupCopy(
    const std::string&  copyName,
    const std::string&  copyDataDirPath,
    uint64_t            volumeSize,
    uint64_t            defaultSessionSize)
{
    std::vector<std::pair<std::string, uint64_t>> fragmentFiles;
    int sessionIndex = 0;
    for (uint64_t sessionOffset = 0; sessionOffset < volumeSize;) {
        ErrCodeType errorCode = 0;
        uint64_t sessionSize = defaultSessionSize;
        if (sessionOffset + sessionSize >= volumeSize) {
            sessionSize = volumeSize - sessionOffset;
        }
        sessionOffset += sessionSize;
        std::string fragmentFilePath = common::GetCopyDataFilePath(
            copyDataDirPath, copyName, CopyFormat::BIN, sessionIndex);
        fragmentFiles.emplace_back(fragmentFilePath, sessionSize);
    }
    return fragmentFiles;
}

static bool CreateFragmentBinaryBackupCopy(
    const std::string&  copyName,
    const std::string&  copyDataDirPath,
    uint64_t            volumeSize,
    uint64_t            defaultSessionSize)
{
    std::vector<std::pair<std::string, uint64_t>> fragmentFiles
        = SplitFragmentBinaryBackupCopy(copyName, copyDataDirPath, volumeSize, defaultSessionSize);
    for (const auto& tup : fragmentFiles) {
        ErrCodeType errorCode = 0;
        std::string fragmentFilePath = tup.first;
        uint64_t filesize = tup.second;
        if (!rawio::TruncateCreateFile(fragmentFilePath, filesize, errorCode)) {
            ERRLOG("failed to create fragment binary copy file %s, size %llu, error code %d",
                fragmentFilePath.c_str(), filesize, errorCode);
            return false;
        }
    }
    return true;
}

static bool FragmentBinaryBackupCopyExists(std::vector<std::string> fragmentFiles)
{
    for (const std::string& fragmentFile : fragmentFiles) {
        ErrCodeType errorCode = 0;
        if (!fsapi::IsFileExists(fragmentFile)) {
            INFOLOG("fragment binary file %s not exists", fragmentFile.c_str());
            return false;
        }
    }
    return true;
}

#ifdef _WIN32
static bool CreateVirtualDiskBackupCopy(
    CopyFormat copyFormat,
    const std::string& copyDataDirPath,
    const std::string& copyName,
    uint64_t volumeSize)
{
    bool result = false;
    ErrCodeType errorCode = ERROR_SUCCESS;
    std::string virtualDiskFilePath;
    std::string physicalDrivePath;
    switch (static_cast<int>(copyFormat)) {
        case static_cast<int>(CopyFormat::VHD_FIXED): {
            virtualDiskFilePath = common::GetCopyDataFilePath(
                copyDataDirPath, copyName, copyFormat, DUMMY_SESSION_INDEX);
            result = rawio::win32::CreateFixedVHDFile(virtualDiskFilePath, volumeSize, errorCode);
            break;
        }
        case static_cast<int>(CopyFormat::VHD_DYNAMIC): {
            virtualDiskFilePath = common::GetCopyDataFilePath(
                copyDataDirPath, copyName, copyFormat, DUMMY_SESSION_INDEX);
            result = rawio::win32::CreateDynamicVHDFile(virtualDiskFilePath, volumeSize, errorCode);
            break;
        }
        case static_cast<int>(CopyFormat::VHDX_FIXED): {
            virtualDiskFilePath = common::GetCopyDataFilePath(
                copyDataDirPath, copyName, copyFormat, DUMMY_SESSION_INDEX);
            result = rawio::win32::CreateFixedVHDXFile(virtualDiskFilePath, volumeSize, errorCode);
            break;
        }
        case static_cast<int>(CopyFormat::VHDX_DYNAMIC): {
            virtualDiskFilePath = common::GetCopyDataFilePath(
                copyDataDirPath, copyName, copyFormat, DUMMY_SESSION_INDEX);
            result = rawio::win32::CreateDynamicVHDXFile(virtualDiskFilePath, volumeSize, errorCode);
            break;
        }
    }
    if (!result) {
        ERRLOG("failed to prepare win32 virtual disk backup copy %s, error code %d", copyName.c_str(), errorCode);
    }
    return result;
}
#endif


// TaskResourceManager factory builder
std::unique_ptr<TaskResourceManager> TaskResourceManager::BuildBackupTaskResourceManager(
    const BackupTaskResourceManagerParams& params)
{
    return exstd::make_unique<BackupTaskResourceManager>(params);
}

std::unique_ptr<TaskResourceManager> TaskResourceManager::BuildRestoreTaskResourceManager(
    const RestoreTaskResourceManagerParams& params)
{
    return exstd::make_unique<RestoreTaskResourceManager>(params);
}

TaskResourceManager::TaskResourceManager(
    CopyFormat copyFormat,
    const std::string& copyDataDirPath,
    const std::string& copyName)
    : m_copyFormat(copyFormat), m_copyDataDirPath(copyDataDirPath), m_copyName(copyName)
{}

// AttachCopyResource need to compatible with the scenario "resource already been attached"
bool TaskResourceManager::AttachCopyResource()
{
    switch (static_cast<int>(m_copyFormat)) {
        case static_cast<int>(CopyFormat::BIN) :
        case static_cast<int>(CopyFormat::IMAGE): {
            // binary fragment copy or image copy do not need to be attached
            return true;
        }
#ifdef _WIN32
        case static_cast<int>(CopyFormat::VHD_FIXED) :
        case static_cast<int>(CopyFormat::VHD_DYNAMIC) :
        case static_cast<int>(CopyFormat::VHDX_FIXED) :
        case static_cast<int>(CopyFormat::VHDX_DYNAMIC) : {
            ErrCodeType errorCode = ERROR_SUCCESS;
            std::string virtualDiskFilePath = common::GetCopyDataFilePath(
                m_copyDataDirPath, m_copyName, m_copyFormat, DUMMY_SESSION_INDEX);
            // need to check if attached ahead, attached virtual disk should not be attached again
            if (!rawio::win32::VirtualDiskAttached(virtualDiskFilePath) &&
                !rawio::win32::AttachVirtualDiskCopy(virtualDiskFilePath, errorCode)) {
                ERRLOG("failed to attach win32 virtual disk %s, error %d", virtualDiskFilePath.c_str(), errorCode);
                return false;
            }
            if (!rawio::win32::GetVirtualDiskPhysicalDrivePath(virtualDiskFilePath, m_physicalDrivePath, errorCode)) {
                ERRLOG("failed to get physical driver path for virtual disk %s, error %d",
                    virtualDiskFilePath.c_str(), errorCode);
                return false;
            }
            INFOLOG("virtual disk %s attached local physical drive path %s",
                virtualDiskFilePath.c_str(), m_physicalDrivePath.c_str());
            return true;
        }
#endif
    }
    ERRLOG("failed to attach & init backup copy resource, unknown copy format %d", static_cast<int>(m_copyFormat));
    return false;
}

// DetachCopyResource need to compatible with the scenario "resource already been detached"
bool TaskResourceManager::DetachCopyResource()
{
    switch (static_cast<int>(m_copyFormat)) {
        case static_cast<int>(CopyFormat::BIN) :
        case static_cast<int>(CopyFormat::IMAGE): {
            // binary fragment copy or image copy do not need to be dettached
            return true;
        }
#ifdef _WIN32
        case static_cast<int>(CopyFormat::VHD_FIXED) :
        case static_cast<int>(CopyFormat::VHD_DYNAMIC) :
        case static_cast<int>(CopyFormat::VHDX_FIXED) :
        case static_cast<int>(CopyFormat::VHDX_DYNAMIC) : {
            std::string virtualDiskFilePath = common::GetCopyDataFilePath(
                m_copyDataDirPath, m_copyName, m_copyFormat, DUMMY_SESSION_INDEX);
            ErrCodeType errorCode = 0;
            if (rawio::win32::VirtualDiskAttached(virtualDiskFilePath) &&
                !rawio::win32::DetachVirtualDiskCopy(virtualDiskFilePath, errorCode)) {
                ERRLOG("failed to detach virtual disk copy, error %d", errorCode);
            }
            INFOLOG("win32 virtual disk %s detached", virtualDiskFilePath.c_str());
            return true;
        }
#endif
    }
    ERRLOG("unknown copy format %d", static_cast<int>(m_copyFormat));
    return false;
}

// implement BackupTaskResourceManager...
BackupTaskResourceManager::BackupTaskResourceManager(const BackupTaskResourceManagerParams& param)
    : TaskResourceManager(param.copyFormat, param.copyDataDirPath, param.copyName),
    m_volumeSize(param.volumeSize),
    m_maxSessionSize(param.maxSessionSize),
    m_backupType(param.backupType)
{};

BackupTaskResourceManager::~BackupTaskResourceManager()
{
    if (!DetachCopyResource()) {
        ERRLOG("failed to detach backup copy resource");
    }
}

bool BackupTaskResourceManager::PrepareCopyResource()
{
    bool resourceExists = ResourceExists();
    if (m_backupType == BackupType::FULL && !resourceExists) {
        // only full backup need to create resource, check resource exists ahead to handle the crash-restart scenario
        DBGLOG("full backup, resources not exists");
        if (!CreateBackupCopyResource()) {
            ERRLOG("failed to create backup resource");
            return false;
        }
    }

    if (!AttachCopyResource()) {
        ERRLOG("failed to attach copy resource");
        return false;
    }
    if (!InitBackupCopyResource()) {
        ERRLOG("failed to attach & init copy resource");
        return false;
    }
    return true;
}

bool BackupTaskResourceManager::CreateBackupCopyResource()
{
    switch (static_cast<int>(m_copyFormat)) {
        case static_cast<int>(CopyFormat::BIN) : {
            return CreateFragmentBinaryBackupCopy(m_copyName, m_copyDataDirPath, m_volumeSize, m_maxSessionSize);
        }
        case static_cast<int>(CopyFormat::IMAGE): {
            std::string imageFilePath = common::GetCopyDataFilePath(
                m_copyDataDirPath, m_copyName, m_copyFormat, DUMMY_SESSION_INDEX);
            ErrCodeType errorCode = 0;
            bool result = rawio::TruncateCreateFile(imageFilePath, m_volumeSize, errorCode);
            if (!result) {
                ERRLOG("failed to truncate create file %s, error = %d", imageFilePath.c_str(), errorCode);
            }
            return result;
        }
#ifdef _WIN32
        case static_cast<int>(CopyFormat::VHD_FIXED) :
        case static_cast<int>(CopyFormat::VHD_DYNAMIC) :
        case static_cast<int>(CopyFormat::VHDX_FIXED) :
        case static_cast<int>(CopyFormat::VHDX_DYNAMIC) : {
            return CreateVirtualDiskBackupCopy(m_copyFormat, m_copyDataDirPath, m_copyName, m_volumeSize);
        }
#endif
    }
    ERRLOG("failed to prepare backup copy %s, unknown copy format %d", m_copyName.c_str(), m_copyFormat);
    return false;
}

bool BackupTaskResourceManager::InitBackupCopyResource()
{
    switch (static_cast<int>(m_copyFormat)) {
        case static_cast<int>(CopyFormat::BIN) :
        case static_cast<int>(CopyFormat::IMAGE): {
            // fragment binary and image format do not need to be inited
            return true;
        }
#ifdef _WIN32
        case static_cast<int>(CopyFormat::VHD_FIXED) :
        case static_cast<int>(CopyFormat::VHD_DYNAMIC) :
        case static_cast<int>(CopyFormat::VHDX_FIXED) :
        case static_cast<int>(CopyFormat::VHDX_DYNAMIC) : {
            ErrCodeType errorCode = 0;
            if (m_physicalDrivePath.empty()) {
                ERRLOG("physical drive path empty, virtual disk not properly attached!");
                return false;
            }
            if (!rawio::win32::InitVirtualDiskGPT(m_physicalDrivePath, m_volumeSize, errorCode)) {
                ERRLOG("failed to init GPT partition for %s, error %d", m_physicalDrivePath.c_str(), errorCode);
                return false;
            }
            INFOLOG("init GPT partition table to %s success", m_physicalDrivePath.c_str());
            return true;
        }
#endif
    }
    ERRLOG("failed to init backup copy %s, unknown copy format %d", m_copyName.c_str(), m_copyFormat);
    return false;
}

bool BackupTaskResourceManager::ResourceExists()
{
    switch (static_cast<int>(m_copyFormat)) {
        case static_cast<int>(CopyFormat::BIN) : {
            auto fragments = SplitFragmentBinaryBackupCopy(
                m_copyName, m_copyDataDirPath, m_volumeSize, m_maxSessionSize);
            std::vector<std::string> fragmentFiles;
            fragmentFiles.reserve(fragments.size());
            std::transform(fragments.begin(), fragments.end(), std::back_inserter(fragmentFiles),
                   [](const std::pair<std::string, uint64_t>& p) { return p.first; });
            return FragmentBinaryBackupCopyExists(fragmentFiles);
        }
#ifdef _WIN32
        case static_cast<int>(CopyFormat::VHD_FIXED) :
        case static_cast<int>(CopyFormat::VHD_DYNAMIC) :
        case static_cast<int>(CopyFormat::VHDX_FIXED) :
        case static_cast<int>(CopyFormat::VHDX_DYNAMIC) :
#endif
        case static_cast<int>(CopyFormat::IMAGE): {
            std::string filePath = common::GetCopyDataFilePath(
                m_copyDataDirPath, m_copyName, m_copyFormat, DUMMY_SESSION_INDEX);
            return fsapi::IsFileExists(filePath);
        }
    }
    DBGLOG("backup copy %s not exists, format %d", m_copyName.c_str(), m_copyFormat);
    return false;
}

// implement RestoreTaskResourceManager...
RestoreTaskResourceManager::RestoreTaskResourceManager(const RestoreTaskResourceManagerParams& param)
    : TaskResourceManager(param.copyFormat, param.copyDataDirPath, param.copyName),
    m_copyDataFiles(param.copyDataFiles)
{};

RestoreTaskResourceManager::~RestoreTaskResourceManager()
{
    if (!DetachCopyResource()) {
        ERRLOG("failed to detach restore copy resource");
    }
}

bool RestoreTaskResourceManager::PrepareCopyResource()
{
    if (!ResourceExists()) {
        ERRLOG("restore resource not exists!");
        return false;
    }
    if (!AttachCopyResource()) {
        ERRLOG("failed to attach restore resource");
        return false;
    }
    return true;
}

bool RestoreTaskResourceManager::ResourceExists()
{
    for (const std::string& copyDataFile : m_copyDataFiles) {
        if (!fsapi::IsFileExists(copyDataFile)) {
            ERRLOG("restore copy %s, copy data file %s not exists", m_copyName.c_str(), copyDataFile.c_str());
            return false;
        }
    }
    return true;
}