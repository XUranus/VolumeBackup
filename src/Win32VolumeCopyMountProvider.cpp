#include "Win32VolumeCopyMountProvider.h"

using namespace volumeprotect;
using namespace volumeprotect::mount;

std::unique_ptr<Win32VolumeCopyMountProvider> Win32VolumeCopyMountProvider::BuildWin32MountProvider(
    const Win32CopyMountConfig& mountConfig)
    : m_copyMetaDirPath(mountConfig.copyMetaDirPath),
    m_copyDataDirPath(mountConfig.copyDataDirPath),
    m_mountTargetPath(mountConfig.mountTargetPath)
{

}

Win32VolumeCopyMountProvider::Win32VolumeCopyMountProvider()
{

}

Win32VolumeCopyMountProvider::~Win32VolumeCopyMountProvider()
{

}

bool Win32VolumeCopyMountProvider::MountCopy()
{
    return false;
}

bool Win32VolumeCopyMountProvider::UmountCopy()
{
    return false;
}