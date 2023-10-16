#ifdef _WIN32

#ifndef VOLUMEBACKUP_WIN32_VIRTUALDISK_MOUNT_PROVIDER_HEADER
#define VOLUMEBACKUP_WIN32_VIRTUALDISK_MOUNT_PROVIDER_HEADER

#include "VolumeProtectMacros.h"
// external logger/json library
#include "Json.h"
#include "VolumeUtils.h"

#include "VolumeCopyMountProvider.h"

namespace volumeprotect {
namespace mount {

/**
 * Win32VirtualDiskMountProvider provides the functionality to mount/umount volume copy from a specified
 *   data path and meta path. This piece of code will attach the virtual disk (*.vhd/*.vhdx) file and assign
 *   a driver letter to it, it can alse be assigned a non-root path if it's a NTFS volume.
 * Each virtual disk is guaranteed to have only one MSR partition and one data partition, it can only be mounted
 *   by the Windows OS version that support GPT partition and virtual disk service.
 */
class Win32VirtualDiskMountProvider : public VolumeCopyMountProvider {
public:
    static std::unique_ptr<Win32VirtualDiskMountProvider> Build(
        const VolumeCopyMountConfig& volumeCopyMountConfig,
        const VolumeCopyMeta& volumeCopyMeta);

    Win32VirtualDiskMountProvider(
        const std::string& outputDirPath,
        const std::string& copyName,
        CopyFormat copyFormat,
        const std::string& virtualDiskFilePath,
        const std::string& mountTargetPath);

    bool Mount() override;

    std::string GetMountRecordPath() const override;

private:
    void MountRollback();

private:
    std::string     m_outputDirPath;
    std::string     m_copyName;
    CopyFormat      m_copyFormat;
    std::string     m_virtualDiskFilePath;
    std::string     m_mountTargetPath;
};

class Win32VirtualDiskUmountProvider : public VolumeCopyUmountProvider {
public:
    static std::unique_ptr<Win32VirtualDiskUmountProvider> Build(const std::string& mountRecordJsonFilePath);

    Win32VirtualDiskUmountProvider(const std::string& virtualDiskFilePath);

    bool Umount() override;

private:
    std::string m_virtualDiskFilePath;

};

}
}

#endif

#endif