#ifndef VOLUMEBACKUP_VOLUME_COPY_MOUNT_PROVIDER_HEADER
#define VOLUMEBACKUP_VOLUME_COPY_MOUNT_PROVIDER_HEADER

#include "VolumeProtectMacros.h"
// external logger/json library
#include "Json.h"
#include "VolumeUtils.h"

namespace volumeprotect {
namespace mount {

struct VolumeCopyMountConfig {
    // meta json file path of the volume copy
    std::string     copyMetaJsonPath;
    // directory path of the volume copy data
    std::string     copyDataDirPath;
    // mount target path.
    // For *nix mount, the target directory must be created head
    // For Windows virtual disk mount, it can keep empty to retrieve a drive letter automatically
    std::string     mountTargetPath;

    // field is only used for *unix mount to sepecify filesystem type (option "-t", eg: ext4, xfs, btrfs...)
    std::string     mountFsType;
    // field is only used for *unix mount to sepecify mount options (option "-o", eg: "ro,loop,noatime")
    std::string     mountOptions;      
};

// function as base class for multple volume copy mount provider of different backup format
class VOLUMEPROTECT_API VolumeCopyMountProvider {
public:
    static std::unique_ptr<VolumeCopyMountProvider> BuildVolumeCopyMountProvider(
        const std::string outputDirPath, 
    );

    virtual bool IsMountSupported();

    virtual bool Mount();
};

// function as base class for multple volume copy umount provider of different backup format
class VOLUMEPROTECT_API VolumeCopyUmountProvider {
    static std::unique_ptr<VolumeCopyUmountProvider> BuildVolumeCopyUmountProvider(
        const std::string mountRecordJsonPath, 
    );


    virtual bool Umount();
};

}
}

#endif