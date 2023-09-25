#ifndef VOLUMEBACKUP_WIN32_COPY_MOUNT_PROVIDER_HEADER
#define VOLUMEBACKUP_WIN32_COPY_MOUNT_PROVIDER_HEADER

#include "VolumeProtectMacros.h"
// external logger/json library
#include "Json.h"
#include "VolumeUtils.h"

namespace volumeprotect {
namespace mount {

#ifdef _WIN32

/**
 * Win32VolumeCopyMountProvider provides the functionality to mount/umount volume copy from a specified
 *   data path and meta path. This piece of code will attach the virtual disk (*.vhd/*.vhdx) file and assign
 *   a driver letter to it, it can alse be assigned a non-root path if it's a NTFS volume.
 * Each virtual disk is guaranteed to have only one MSR partition and one data partition, it can only be mounted
 *   by the Windows OS version that support GPT partition and virtual disk service.
 * The mount process won't create any mount record thus won't create any garbage files.
 */

struct Win32CopyMountConfig {
    std::string     copyMetaDirPath;
    std::string     copyDataDirPath;
    std::string     mountTargetPath;
};

class Win32VolumeCopyMountProvider {
public:
    static std::unique_ptr<Win32VolumeCopyMountProvider> BuildWin32MountProvider(
        const Win32CopyMountConfig& mountConfig);

    Win32VolumeCopyMountProvider(
        std::string copyMetaDirPath,
        std::string copyDataDirPath,
        std::string mountTargetPath);

    ~Win32VolumeCopyMountProvider();

    bool MountCopy();

    bool UmountCopy();

private:
    std::string     m_copyMetaDirPath;
    std::string     m_copyDataDirPath;
    std::string     m_mountTargetPath;
};

#endif

}
}

#endif