#include "ImageCopyMountProvider.h"
#include "VolumeCopyMountProvider.h"
#include "Logger.h"

#ifdef __linux
#include <cerrno>
#include <fcntl.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <mntent.h>
#include <dirent.h>
#include <stdlib.h>
#include <unistd.h>

#include "native/FileSystemAPI.h"
#include "native/linux/LoopDeviceControl.h"
#endif

using namespace volumeprotect;
using namespace volumeprotect::util;
using namespace volumeprotect::mount;

namespace {
    const std::string SEPARATOR = "/";
    const std::string IMAGE_COPY_MOUNT_RECORD_FILE_SUFFIX = ".image.mount.record.json";
    const std::string LOOPBACK_DEVICE_PATH_PREFIX = "/dev/loop";
    const std::string LOOPBACK_DEVICE_CREATION_RECORD_SUFFIX = ".loop.record";
}

struct ImageCopyMountRecord {
    std::string     loopbackDevicePath;
    std::string     mountTargetPath;
    std::string     mountFsType;
    std::string     mountOptions;

    SERIALIZE_SECTION_BEGIN
    SERIALIZE_FIELD(loopbackDevicePath, loopbackDevicePath);
    SERIALIZE_FIELD(mountTargetPath, mountTargetPath);
    SERIALIZE_FIELD(mountFsType, mountFsType);
    SERIALIZE_FIELD(mountOptions, mountOptions);
    SERIALIZE_SECTION_END
};

std::unique_ptr<ImageCopyMountProvider> ImageCopyMountProvider::Build(
    const VolumeCopyMountConfig& volumeCopyMountConfig,
    const VolumeCopyMeta& volumeCopyMeta)
{
    ImageCopyMountProviderParams params {};
    params.outputDirPath = volumeCopyMountConfig.outputDirPath;
    params.copyName = volumeCopyMeta.copyName;
    if (volumeCopyMeta.segments.empty()) {
        ERRLOG("illegal volume copy meta, image file segments list empty");
        return nullptr;
    }
    params.imageFilePath = volumeCopyMeta.segments.front().copyDataFile;
    params.mountTargetPath = volumeCopyMountConfig.mountTargetPath;
    params.mountFsType = volumeCopyMountConfig.mountFsType;
    params.mountOptions = volumeCopyMountConfig.mountOptions;
    return std::make_unique<ImageCopyMountProvider>(params);
}

ImageCopyMountProvider::ImageCopyMountProvider(const ImageCopyMountProviderParams& params)
    : m_outputDirPath(params.outputDirPath),
    m_copyName(params.copyName),
    m_imageFilePath(params.imageFilePath),
    m_mountTargetPath(params.mountTargetPath),
    m_mountFsType(params.mountFsType),
    m_mountOptions(params.mountOptions)
{}

bool ImageCopyMountProvider::IsMountSupported()
{
#ifdef _WIN32
    // win32 image mounting requires 3rd utilities, implement later...
    return false;
#else
    return true;
#endif
}

bool ImageCopyMountProvider::Mount()
{
#ifdef _WIN32
    // win32 image mounting requires 3rd utilities, implement later...
    RECORD_ERROR("Win32 platform does not support mount image copy %s", m_copyName.c_str());
    return false;
#else
    return PosixLoopMount();
#endif
}

std::string ImageCopyMountProvider::GetMountRecordPath() const
{
    return m_outputDirPath + SEPARATOR + m_copyName + IMAGE_COPY_MOUNT_RECORD_FILE_SUFFIX; 
}

// mount using *nix loopback device
bool ImageCopyMountProvider::PosixLoopMount()
{
    // 1. attach loopback device
    std::string loopDevicePath;
    if (!loopback::Attach(m_imageFilePath, loopDevicePath, O_RDONLY)) {
        RECORD_ERROR("failed to attach read only loopback device from %s, errno %u", filePath.c_str(), errno);
        return false;
    }
    // keep checkpoint for loopback device creation
    std::string loopDeviceNumber = loopDevicePath.substr(LOOPBACK_DEVICE_PATH_PREFIX.length());
    CreateEmptyFileInCacheDir(loopDeviceNumber + LOOPBACK_DEVICE_CREATION_RECORD_SUFFIX);

    // 2. mount block device as readonly
    unsigned long mountFlags = MS_RDONLY;
    if (::mount(loopDevicePath.c_str(), m_mountTargetPath.c_str(), m_mountFsType.c_str(),
        mountFlags, m_mountOptions.c_str()) != 0) {
        RECORD_ERROR("mount %s to %s failed, type %s, option %s, errno %u",
            loopDevicePath.c_str(), m_mountTargetPath.c_str(), m_mountFsType.c_str(), m_mountOptions.c_str(), errno);
        return false;
    }

    // 3. save mount record to output directory
    ImageCopyMountRecord mountRecord {};
    mountRecord.loopbackDevicePath = loopDevicePath;
    mountRecord.mountTargetPath = m_mountTargetPath;
    mountRecord.mountFsType = m_mountFsType;
    mountRecord.mountOptions = m_mountOptions;
    std::string jsonContent = xuranus::minijson::util::Serialize(mountRecord);
    std::string filepath = GetMountRecordPath();
    std::ofstream file(filepath.c_str(), std::ios::trunc);
    if (!file.is_open()) {
        RECORD_ERROR("failed to save image copy mount record to %s, errno %u", filepath.c_str(), errno);
        // TODO:: rollback
        return false;
    }
    file << jsonContent;
    file.close();
    return true;
}

void ImageCopyMountProvider::CreateEmptyFileInCacheDir(const std::string& filename) const
{
    std::string fullpath = m_outputDirPath + SEPARATOR + filename;
    int fd = ::open(fullpath.c_str(), O_CREAT | O_WRONLY, 0644);
    if (fd == -1) {
        RECORD_ERROR("error creating empty file %s, errno %u", fullpath.c_str(), errno);
        return;
    }
    ::close(fd);
    return;
}





std::unique_ptr<ImageCopyUmountProvider> ImageCopyUmountProvider::Build(const std::string& mountRecordJsonFilePath)
{
    ImageCopyMountRecord mountRecord {};
    std::string jsonContent;
    std::ifstream file(mountRecordJsonFilePath);
    if (!file.is_open()) {
        RECORD_ERROR("unabled to open copy mount record %s to read, errno %u", mountRecordJsonFilePath.c_str(), errno);
        return nullptr;
    }
    std::string jsonContent;
    file >> jsonContent;
    xuranus::minijson::util::Deserialize(jsonContent, mountRecord);
    return std::make_unique<ImageCopyUmountProvider>(mountRecord.mountTargetPath, mountRecord.loopbackDevicePath);
}

ImageCopyUmountProvider::ImageCopyUmountProvider(
    const std::string& mountTargetPath, const std::string& loopbackDevicePath)
    : m_mountTargetPath(mountTargetPath), m_loopbackDevicePath(loopbackDevicePath)
{}

bool ImageCopyUmountProvider::Umount()
{
#ifdef _WIN32
    // win32 image umounting requires 3rd utilities, implement later...
    RECORD_ERROR("Win32 platform does not support umount image copy %s", m_copyName.c_str());
    return false;
#else
    return PosixLoopUMount();
#endif
}

bool ImageCopyUmountProvider::PosixLoopUMount()
{
    // TODO:: check mounted
    // 1. umount filesystem
    if (!m_mountTargetPath.empty() && ::umount2(m_mountTargetPath.c_str(), MNT_FORCE) != 0) {
        RECORD_ERROR("failed to umount target %s, errno %u", mountTargetPath.c_str(), errno);
        return false;
    }
    // 2. detach loopback device
    if (!m_loopbackDevicePath.empty() && loopback::Attached(m_loopbackDevicePath) && !loopback::Detach(m_loopbackDevicePath)) {
        RECORD_ERROR("failed to detach loopback device %s, errno %u", loopDevicePath.c_str(), errno);
        return false;
    }
    if (m_loopbackDevicePath.find(LOOPBACK_DEVICE_PATH_PREFIX) == 0) {
        std::string loopDeviceNumber = m_loopbackDevicePath.substr(LOOPBACK_DEVICE_PATH_PREFIX.length());
        RemoveFileInCacheDir(loopDeviceNumber + LOOPBACK_DEVICE_CREATION_RECORD_SUFFIX);
    }
    return true;
}