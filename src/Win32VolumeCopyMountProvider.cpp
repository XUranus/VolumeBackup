#include "Win32VolumeCopyMountProvider.h"
#include "native/FileSystemAPI.h"
#include "Logger.h"

using namespace volumeprotect;
using namespace volumeprotect::mount;
using namespace volumeprotect::fsapi;

std::unique_ptr<Win32VolumeCopyMountProvider> Win32VolumeCopyMountProvider::BuildWin32MountProvider(
    const Win32CopyMountConfig& mountConfig)
{
    if (!fsapi::IsDirectoryExists(mountConfig.copyDataDirPath)
        || !fsapi::IsDirectoryExists(mountConfig.copyMetaDirPath)) {
        ERRLOG("copy data/meta directory %s %s not prepared for mount",
            mountConfig.copyDataDirPath.c_str(), mountConfig.copyMetaDirPath.c_str());
        return nullptr;
    }
    return std::make_unique<Win32VolumeCopyMountProvider>(
        mountConfig.copyMetaDirPath,
        mountConfig.copyDataDirPath,
        mountConfig.mountTargetPath);
}

Win32VolumeCopyMountProvider::Win32VolumeCopyMountProvider(
    std::string copyMetaDirPath,
    std::string copyDataDirPath,
    std::string mountTargetPath)
    : m_copyMetaDirPath(copyMetaDirPath),
    m_copyDataDirPath(copyDataDirPath),
    m_mountTargetPath(mountTargetPath)
{}

Win32VolumeCopyMountProvider::~Win32VolumeCopyMountProvider()
{}

bool Win32VolumeCopyMountProvider::MountCopy()
{
    return false;
}

bool Win32VolumeCopyMountProvider::UmountCopy()
{
    return false;
}