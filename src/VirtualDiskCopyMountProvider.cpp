#include "VirtualDiskCopyMountProvider.h"
#include "VolumeUtils.h"
#include "native/FileSystemAPI.h"
#include "Logger.h"

using namespace volumeprotect;
using namespace volumeprotect::mount;
using namespace volumeprotect::fsapi;
using namespace volumeprotect::util;

std::unique_ptr<VirtualDiskCopyMountProvider> VirtualDiskCopyMountProvider::BuildWin32MountProvider(
    const Win32CopyMountConfig& mountConfig)
{
    if (!fsapi::IsDirectoryExists(mountConfig.copyDataDirPath)
        || !fsapi::IsDirectoryExists(mountConfig.copyMetaDirPath)) {
        ERRLOG("copy data/meta directory %s %s not prepared for mount",
            mountConfig.copyDataDirPath.c_str(), mountConfig.copyMetaDirPath.c_str());
        return nullptr;
    }
    return std::make_unique<VirtualDiskCopyMountProvider>(
        mountConfig.copyMetaDirPath,
        mountConfig.copyDataDirPath,
        mountConfig.mountTargetPath);
}

VirtualDiskCopyMountProvider::VirtualDiskCopyMountProvider(
    std::string copyMetaDirPath,
    std::string copyDataDirPath,
    std::string mountTargetPath)
    : m_copyMetaDirPath(copyMetaDirPath),
    m_copyDataDirPath(copyDataDirPath),
    m_mountTargetPath(mountTargetPath)
{}

VirtualDiskCopyMountProvider::~VirtualDiskCopyMountProvider()
{}

bool VirtualDiskCopyMountProvider::MountCopy()
{
    return false;
}

bool VirtualDiskCopyMountProvider::UmountCopy()
{
    return false;
}