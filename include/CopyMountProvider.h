#ifndef VOLUMEBACKUP_COPY_MOUNT_PROVIDER_HEADER
#define VOLUMEBACKUP_COPY_MOUNT_PROVIDER_HEADER

#include "VolumeProtectMacros.h"
// external logger/json library
#include "Json.h"

namespace volumeprotect {
namespace mount {

#ifdef __linux
/**
 * LinuxMountProvider provides the functionality to mount/umount volume copy from a specified data path and meta path.
 * This piece of code will load copy slices from volumecopy.meta.json and create a block device from it.
 *
 * For a copy contains only one session, LinuxMountProvider will create a loopback device from the file
 *  to be mount directly.
 *
 * For a copy contains multiple sessions, LinuxMountProvider will assign a loopback device for each
 *  copy file and create a devicemapper device with linear targets using the loopback devices.
 *
 * LinuxMountProvider needs to keep the mounting record for volume umount, which need to store the loopback device path,
 *  devicemapper name.
 */

struct VOLUMEPROTECT_API LinuxCopyMountConfig {
    std::string     copyMetaDirPath;
    std::string     copyDataDirPath;
    std::string     mountTargetPath;
    std::string     cacheDirPath;               // store the checkpoint and record info of the mount task
    std::string     mountFsType     { "ext4" };
    std::string     mountOptions    { "noatime" };

    SERIALIZE_SECTION_BEGIN
    SERIALIZE_FIELD(copyMetaDirPath, copyMetaDirPath);
    SERIALIZE_FIELD(copyDataDirPath, copyDataDirPath);
    SERIALIZE_FIELD(mountTargetPath, mountTargetPath);
    SERIALIZE_FIELD(cacheDirPath, cacheDirPath);
    SERIALIZE_FIELD(mountFsType, mountFsType);
    SERIALIZE_FIELD(mountOptions, mountOptions);
    SERIALIZE_SECTION_END
};

struct VOLUMEPROTECT_API CopySliceTarget {
    std::string         copyFilePath;
    uint64_t            volumeOffset;
    uint64_t            size;
    std::string         loopDevicePath;

    SERIALIZE_SECTION_BEGIN
    SERIALIZE_FIELD(copyFilePath, copyFilePath);
    SERIALIZE_FIELD(volumeOffset, volumeOffset);
    SERIALIZE_FIELD(size, size);
    SERIALIZE_FIELD(loopDevicePath, loopDevicePath);
    SERIALIZE_SECTION_END
};

struct VOLUMEPROTECT_API LinuxCopyMountRecord {
    // attributes required for umount
    std::string                 dmDeviceName;       // [opt] required only multiple copy files contained in a volume
    std::vector<std::string>    loopDevices;        // loopback device path like /dev/loopX
    std::string                 devicePath;         // block device mounted (loopback device path or dm device path)
    std::string                 mountTargetPath;    // the mount point path
    
    // attribute used only for debug
    std::vector<CopySliceTarget>    copySlices;
    LinuxCopyMountConfig            mountConfig;

    SERIALIZE_SECTION_BEGIN
    SERIALIZE_FIELD(dmDeviceName, dmDeviceName);
    SERIALIZE_FIELD(loopDevices, loopDevices);
    SERIALIZE_FIELD(devicePath, devicePath);
    SERIALIZE_FIELD(mountTargetPath, mountTargetPath);
    SERIALIZE_FIELD(copySlices, copySlices);
    SERIALIZE_FIELD(mountConfig, mountConfig);
    SERIALIZE_SECTION_END
};

/**
 * provide api to mount/umount volume copy on Linux system
 */
class VOLUMEPROTECT_API LinuxMountProvider {
public:
    bool MountCopy(const LinuxCopyMountConfig& mountConfig, LinuxCopyMountRecord& mountRecord);
    
    bool UmountCopy(const LinuxCopyMountRecord& record);

private:
    bool MountReadonlyDevice(
        const std::string& devicePath,
        const std::string& mountTargetPath,
        const std::string& fsType,
        const std::string& mountOptions);
    
    bool UmountDevice(const std::string& mountTargetPath);
    
    bool CreateReadonlyDmDevice(
        const std::vector<CopySliceTarget> copySlices,
        std::string& dmDeviceName,
        std::string& dmDevicePath);
    
    bool RemoveDmDevice(const std::string& dmDeviceName);
    
    bool AttachReadonlyLoopDevice(const std::string& filePath, std::string& loopDevicePath);
    
    bool DetachLoopDevice(const std::string& loopDevicePath);

    std::string GenerateNewDmDeviceName() const;
};
#endif

}
}
#endif