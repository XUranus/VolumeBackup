#include "TaskResourceManager.h"
#include "Logger.h"
#include "native/RawIO.h"

#ifdef __linux__
#include "native/linux/PosixRawIO.h"
#endif

#ifdef _WIN32
#include "native/win32/Win32RawIO.h"
#endif

using namespace volumeprotect;
using namespace volumeprotect::rawio;

namespace {
    constexpr auto DUMMY_SESSION_INDEX = 0;
}

// implement static util functions...
static bool CreateFragmentBinaryBackupCopy(
    const std::string&  copyName,
    const std::string&  copyDataDirPath,
    uint64_t            volumeSize,
    uint64_t            defaultSessionSize)
{
    int sessionIndex = 0;
    for (uint64_t sessionOffset = 0; sessionOffset < volumeSize;) {
        ErrCodeType errorCode = 0;
        uint64_t sessionSize = defaultSessionSize;
        if (sessionOffset + sessionSize >= volumeSize) {
            sessionSize = volumeSize - sessionOffset;
        }
        sessionOffset += sessionSize;
        std::string fragmentFilePath = util::GetCopyDataFilePath(
            copyDataDirPath, copyName, CopyFormat::BIN, sessionIndex);
        if (!rawio::TruncateCreateFile(fragmentFilePath, sessionSize, errorCode)) {
            ERRLOG("failed to create fragment binary copy file %s, size %llu, error code %d",
                fragmentFilePath.c_str(), sessionSize, errorCode);
            return false;
        }
        ++sessionIndex;
    }
    return true;
}

#ifdef _WIN32
static bool CreateWin32VirtualDiskBackupCopy(
    CopyFormat copyFormat,
    const std::string& copyDataDirPath,
    const std::string& copyName,
    uint64_t volumeSize)
{
    bool result = false;
    ErrCodeType errorCode = ERROR_SUCCESS;
    std::string virtualDiskPath;
    std::string physicalDrivePath;
    switch (static_cast<int>(copyFormat)) {
        case static_cast<int>(CopyFormat::VHD_FIXED): {
            virtualDiskPath = util::GetCopyDataFilePath(
                copyDataDirPath, copyName, copyFormat, DUMMY_SESSION_INDEX);
            result = rawio::win32::CreateFixedVHDFile(virtualDiskPath, volumeSize, errorCode);
            break;
        }
        case static_cast<int>(CopyFormat::VHD_DYNAMIC): {
            virtualDiskPath = util::GetCopyDataFilePath(
                copyDataDirPath, copyName, copyFormat, DUMMY_SESSION_INDEX);
            result = rawio::win32::CreateDynamicVHDFile(virtualDiskPath, volumeSize, errorCode);
            break;
        }
        case static_cast<int>(CopyFormat::VHDX_FIXED): {
            virtualDiskPath = util::GetCopyDataFilePath(
                copyDataDirPath, copyName, copyFormat, DUMMY_SESSION_INDEX);
            result = rawio::win32::CreateFixedVHDXFile(virtualDiskPath, volumeSize, errorCode);
            break;
        }
        case static_cast<int>(CopyFormat::VHDX_DYNAMIC): {
            virtualDiskPath = util::GetCopyDataFilePath(
                copyDataDirPath, copyName, copyFormat, DUMMY_SESSION_INDEX);
            result = rawio::win32::CreateDynamicVHDXFile(virtualDiskPath, volumeSize, errorCode);
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
    return std::make_unique<BackupTaskResourceManager>(params);
}

std::unique_ptr<TaskResourceManager> TaskResourceManager::BuildRestoreTaskResourceManager(
    const RestoreTaskResourceManagerParams& params)
{
    return std::make_unique<RestoreTaskResourceManager>(params);
}

TaskResourceManager::TaskResourceManager(
    CopyFormat copyFormat,
    const std::string& copyDataDirPath,
    const std::string& copyName)
    : m_copyFormat(copyFormat), m_copyDataDirPath(copyDataDirPath), m_copyName(copyName)
{}

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
            std::string virtualDiskPath = util::GetCopyDataFilePath(
                m_copyDataDirPath, m_copyName, m_copyFormat, DUMMY_SESSION_INDEX);
            std::string physicalDrivePath;
            ErrCodeType errorCode = 0;
            if (!rawio::win32::AttachVirtualDiskCopy(virtualDiskPath, physicalDrivePath, errorCode)) {
                ERRLOG("failed to attach win32 virtual disk %s, error %d", virtualDiskPath.c_str(), errorCode);
                return false;
            }
            INFOLOG("win32 virtual disk %s attached, physical driver path: %s",
                virtualDiskPath.c_str(), physicalDrivePath.c_str());
            return true;
        }
#endif
    }
    ERRLOG("failed to attach & init backup copy resource, unknown copy format %d", static_cast<int>(m_copyFormat));
    return false;
}

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
            std::string virtualDiskPath = util::GetCopyDataFilePath(
                m_copyDataDirPath, m_copyName, m_copyFormat, DUMMY_SESSION_INDEX);
            ErrCodeType errorCode = 0;
            if (!rawio::win32::DetachVirtualDiskCopy(virtualDiskPath, errorCode)) {
                ERRLOG("failed to detach virtual disk copy, error %d", errorCode);
            }
            INFOLOG("win32 virtual disk %s detached, physical driver path: %s", virtualDiskPath.c_str());
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
    m_maxSessionSize(param.maxSessionSize)
{};

BackupTaskResourceManager::~BackupTaskResourceManager()
{
    if (!DetachCopyResource()) {
        ERRLOG("failed to detach backup copy resource");
    }
}

bool BackupTaskResourceManager::PrepareResource()
{
    // TODO::check file exists for checkpoint
    if (!CreateBackupCopyResource()) {
        ERRLOG("failed to create backup resource");
        return false;
    }
    if (!AttachCopyResource()) {
        ERRLOG("failed to attach copy resource");
        return false;
    }
    if (!InitBackupCopyResource()) {
        ERRLOG("failed to attach & init copy resource");
        return false;
    }
}

bool BackupTaskResourceManager::CreateBackupCopyResource()
{
    switch (static_cast<int>(m_copyFormat)) {
        case static_cast<int>(CopyFormat::BIN) : {
            return CreateFragmentBinaryBackupCopy(m_copyName, m_copyDataDirPath, m_volumeSize, m_maxSessionSize);
        }
        case static_cast<int>(CopyFormat::IMAGE): {
            std::string imageFilePath = util::GetCopyDataFilePath(
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
            return CreateWin32VirtualDiskBackupCopy(m_copyFormat, m_copyDataDirPath, m_copyName, m_volumeSize);
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
            if (!rawio::win32::InitVirtualDiskGPT(m_physicalDrivePath, m_volumeSize, errorCode)) {
                ERRLOG("failed to init GPT partition for %s, error %d", m_physicalDrivePath.c_str(), errorCode);
                return false;
            }
        }
#endif
    }
    ERRLOG("failed to init backup copy %s, unknown copy format %d", m_copyName.c_str(), m_copyFormat);
    return false;
}


// implement RestoreTaskResourceManager...
RestoreTaskResourceManager::RestoreTaskResourceManager(const RestoreTaskResourceManagerParams& param)
    : TaskResourceManager(param.copyFormat, param.copyDataDirPath, param.copyName)
{};

RestoreTaskResourceManager::~RestoreTaskResourceManager()
{
    if (!DetachCopyResource()) {
        ERRLOG("failed to detach restore copy resource");
    }
}

bool RestoreTaskResourceManager::PrepareResource()
{
    if (!AttachCopyResource()) {
        ERRLOG("failed to attach restore resource");
        return false;
    }
}



