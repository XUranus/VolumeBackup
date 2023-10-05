#ifndef VOLUMEBACKUP_LINUX_IMAGE_MOUNT_PROVIDER_HEADER
#define VOLUMEBACKUP_LINUX_IMAGE_MOUNT_PROVIDER_HEADER

#include "VolumeProtectMacros.h"
// external logger/json library
#include "Json.h"
#include "VolumeUtils.h"

#include "VolumeCopyMountProvider.h"

namespace volumeprotect {
namespace mount {

struct LinuxImageCopyMountProviderParams {
    std::string     outputDirPath;
    std::string     copyName;
    std::string     imageFilePath;
    std::string     mountTargetPath;
    std::string     mountFsType;
    std::string     mountOptions;
}

/**
 * LinuxImageCopyMountProvider provides the functionality to mount volume copy with CopyFormat::IMAGE.
 * For *nix platform, this piece of code will create a loopback device from the volume image file and mount it
 * For Windows platform, mounting image whose filesystem is not UDF/ISO-9660 requiring 3rd utilities like ImDisk.
 */
class LinuxImageCopyMountProvider : public VolumeCopyMountProvider {
public:
    static std::unique_ptr<LinuxImageCopyMountProvider> Build(
        const VolumeCopyMountConfig& volumeCopyMountConfig,
        const VolumeCopyMeta& volumeCopyMeta);

    LinuxImageCopyMountProvider(const LinuxImageCopyMountProviderParams& params);
    
    bool IsMountSupported() override;

    bool Mount() override;

    std::string GetMountRecordPath() const override;

    ~LinuxImageCopyMountProvider() = default;

private:
    bool PosixLoopbackMountRollback(const std::string& loopbackDevicePath);

private:
    std::string     m_outputDirPath;
    std::string     m_copyName;
    std::string     m_imageFilePath;
    std::string     m_mountTargetPath;

    // [optional] used for *nix system
    std::string     m_mountFsType;
    std::string     m_mountOptions;
};

/**
 * LinuxImageCopyMountProvider provides the functionality to umount volume copy with CopyFormat::IMAGE.
 * For *nix platform, this piece of code will umount the mount point and detach the loopback device.
 * For Windows platform, umouting image whose filesystem is not UDF/ISO-9660 requiring 3rd utilities like ImDisk.
 */
class ImageCopyUmountProvider : public VolumeCopyUmountProvider {
public:
    static std::unique_ptr<ImageCopyUmountProvider> Build(
        const std::string& mountRecordJsonFilePath,
        const std::string& outputDirPath);

    ImageCopyUmountProvider(
    	const std::string& outputDirPath,
        const std::string& mountTargetPath,
        const std::string& loopbackDevicePath);

    ~ImageCopyUmountProvider() = default;

    bool Umount() override;

private:
    std::string     m_outputDirPath;
    std::string     m_mountTargetPath;
    std::string     m_loopbackDevicePath;
};

}
}


#endif