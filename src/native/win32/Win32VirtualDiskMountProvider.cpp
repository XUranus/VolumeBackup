#ifdef _WIN32

#include "Win32VirtualDiskMountProvider.h"
#include "VolumeUtils.h"
#include "native/win32/Win32RawIO.h"
#include "Logger.h"

using namespace volumeprotect;
using namespace volumeprotect::mount;
using namespace volumeprotect::rawio;
using namespace volumeprotect::util;

namespace {
    const std::string SEPARATOR = "\\";
    const std::string VIRTUAL_DISK_COPY_MOUNT_RECORD_FILE_SUFFIX = ".vhd.mount.record.json";
}

// serialize to $copyName.image.mount.record.json
struct Win32VirtualDiskCopyMountRecord {
    std::string     virtualDiskFilePath;
    std::string     mountTargetPath;

    SERIALIZE_SECTION_BEGIN
    SERIALIZE_FIELD(virtualDiskFilePath, virtualDiskFilePath);
    SERIALIZE_FIELD(mountTargetPath, mountTargetPath);
    SERIALIZE_SECTION_END
};

// implement Win32VirtualDiskMountProvider...
std::unique_ptr<Win32VirtualDiskMountProvider> Win32VirtualDiskMountProvider::Build(
    const std::string& outputDirPath,
    const std::string& copyName,
    const std::string& virtualDiskFilePath,
    const std::string& mountTargetPath)
{
    return std::make_unique<Win32VirtualDiskMountProvider>(
        outputDirPath, copyName, virtualDiskFilePath, mountTargetPath);
}

Win32VirtualDiskMountProvider::Win32VirtualDiskMountProvider(
    const std::string& outputDirPath,
    const std::string& copyName,
    const std::string& virtualDiskFilePath,
    const std::string& mountTargetPath)
    : m_outputDirPath(outputDirPath),
    m_copyName(copyName),
    m_virtualDiskFilePath(virtualDiskFilePath),
    m_mountTargetPath(mountTargetPath)
{}

bool Win32VirtualDiskMountProvider::Mount()
{
    std::string physicalDrivePath;
    std::string volumeDevicePath;
    ErrCodeType errorCode = ERROR_SUCCESS;
    // serialize mountRecord ahead
    std::string filepath = GetMountRecordPath();
    Win32VirtualDiskCopyMountRecord mountRecord {};
    mountRecord.mountTargetPath = m_mountTargetPath;
    mountRecord.virtualDiskFilePath = m_virtualDiskFilePath;
    if (!util::JsonSerialize(mountRecord, filepath)) {
        RECORD_INNER_ERROR("failed to save image copy mount record to %s, errno %u", filepath.c_str(), errno);
        return false;
    }

    if (!rawio::win32::VirtualDiskAttached(m_virtualDiskFilePath)
        && !rawio::win32::AttachVirtualDiskCopy(m_virtualDiskFilePath, errorCode)) {
        RECORD_INNER_ERROR("failed to attach virtualdisk file %s, error %u", m_virtualDiskFilePath.c_str(), errorCode);
        return false;
    }
    if (!rawio::win32::GetVirtualDiskPhysicalDrivePath(m_virtualDiskFilePath, physicalDrivePath, errorCode)) {
        RECORD_INNER_ERROR("failed to get virtual disk physical drive path from %s, error %u",
            m_virtualDiskFilePath.c_str(), errorCode);
        MountRollback();
        return false;
    }
    if (!rawio::win32::GetCopyVolumeDevicePath(physicalDrivePath, volumeDevicePath, errorCode)) {
        RECORD_INNER_ERROR("failed to get volume device path from %s, error %u",
            physicalDrivePath.c_str(), errorCode);
        MountRollback();
        return false;
    }
    if (!::SetVolumeMountPointA(m_mountTargetPath.c_str(), volumeDevicePath.c_str())) {
        RECORD_INNER_ERROR("failed to assign mount point %s for volume %s, error %u",
            m_mountTargetPath.c_str(), volumeDevicePath.c_str(), ::GetLastError());
        MountRollback();
        return false;
    }
    return true;
}

std::string Win32VirtualDiskMountProvider::GetMountRecordPath() const
{
    return m_outputDirPath + SEPARATOR + m_copyName + VIRTUAL_DISK_COPY_MOUNT_RECORD_FILE_SUFFIX;
}

// virtual disk that has been attached need to be detached
void Win32VirtualDiskMountProvider::MountRollback()
{
    ErrCodeType errorCode = ERROR_SUCCESS;
    if (rawio::win32::VirtualDiskAttached(m_virtualDiskFilePath)
        && !rawio::win32::DetachVirtualDiskCopy(m_virtualDiskFilePath, errorCode)) {
        RECORD_INNER_ERROR("failed to detach virtual disk %s, error %u", m_virtualDiskFilePath.c_str(), errorCode);
    }
    return;
}

// implement Win32VirtualDiskUmountProvider...
std::unique_ptr<Win32VirtualDiskUmountProvider> Win32VirtualDiskUmountProvider::Build(
    const std::string& mountRecordJsonFilePath)
{
    Win32VirtualDiskCopyMountRecord mountRecord {};
    if (!util::JsonDeserialize(mountRecord, mountRecordJsonFilePath)) {
        ERRLOG("unabled to open copy mount record %s to read, errno %u", mountRecordJsonFilePath.c_str(), errno);
        return nullptr;
    };
    return std::make_unique<Win32VirtualDiskUmountProvider>(mountRecord.virtualDiskFilePath);
}

Win32VirtualDiskUmountProvider::Win32VirtualDiskUmountProvider(const std::string& virtualDiskFilePath)
    : m_virtualDiskFilePath(virtualDiskFilePath)
{}

bool Win32VirtualDiskUmountProvider::Umount()
{
    // HINT:: if volume is in use, need to wait for virtual disk to be detached?
    ErrCodeType errorCode = ERROR_SUCCESS;
    if (rawio::win32::VirtualDiskAttached(m_virtualDiskFilePath)
        && !rawio::win32::DetachVirtualDiskCopy(m_virtualDiskFilePath, errorCode)) {
        RECORD_INNER_ERROR("failed to detach virtual disk %s, error %u", m_virtualDiskFilePath.c_str(), errorCode);
        return false;
    }
    return true;
}

#endif