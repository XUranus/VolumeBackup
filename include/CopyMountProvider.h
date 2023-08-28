#ifndef VOLUMEBACKUP_COPY_MOUNT_PROVIDER_HEADER
#define VOLUMEBACKUP_COPY_MOUNT_PROVIDER_HEADER

#include "VolumeProtectMacros.h"
// external logger/json library
#include "Json.h"
#include "VolumeUtils.h"

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
    std::string     mountFsType     { "ext4" };
    std::string     mountOptions    { "noatime" };

    SERIALIZE_SECTION_BEGIN
    SERIALIZE_FIELD(copyMetaDirPath, copyMetaDirPath);
    SERIALIZE_FIELD(copyDataDirPath, copyDataDirPath);
    SERIALIZE_FIELD(mountTargetPath, mountTargetPath);
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

// define constants for LinuxMountProvider
const std::string DEVICE_MAPPER_DEVICE_NAME_PREFIX = "volumeprotect_dm_copy_";
const std::string MOUNT_RECORD_JSON_NAME = "volumecopymount.record.json";
const std::string LOOPBACK_DEVICE_CREATION_RECORD_SUFFIX = ".loop.record";
const std::string DEVICE_MAPPER_DEVICE_CREATION_RECORD_SUFFIX = ".dm.record";

/**
 * provide api to mount volume copy on Linux system
 * LinuxMountProvider need a cache directory to store mount record and residual device info for each mount task.
 *
 * "MountCopy" method require a config struct containing mount fs type, options, target path and so on,
 * and save mount record as volumecopymount.record.json into cache directory if success.
 *
 * "UmountCopy" method load the volumecopymount.record.json from cache directory and execute umount and remove device.
 *
 * When mount/umount action failed:
 * For each created dm device, a "dmDeviceName.dm.record" file will be created,
 * and For each attached loop device, a "loopX.loop.record" file will be created.
 * These files will be used to track residual device if mount task is partial failed.
 * Callers can use ClearResidue method to try to clear the residual device.
 */
class VOLUMEPROTECT_API LinuxMountProvider {
public:
    static std::unique_ptr<LinuxMountProvider> BuildLinuxMountProvider(const std::string& cacheDirPath);

    LinuxMountProvider(const std::string& cacheDirPath);

    // create device and mount using mountConfig and save result to volumecopymount.record.json in cache directory
    bool MountCopy(const LinuxCopyMountConfig& mountConfig);

    // load volumecopymount.record.json in cache directory and execute umount and remove device
    bool UmountCopy();

    // get all errors splited by "\n"
    std::string GetErrors() const;

    // format error message and store inner, provider a debugging way for JNI c extension
    void RecordError(const char* message, ...);

    // if mount failed, caller can call this methods to try to remove residual loop/dm device
    bool ClearResidue();

    // used to load residual record in cache directory
    bool LoadResidualLoopDeviceList(std::vector<std::string>& loopDeviceList);

    bool LoadResidualDmDeviceList(std::vector<std::string>& dmDeviceNameList);

    std::string GetMountRecordJsonPath() const;

protected:
    virtual bool ReadMountRecord(LinuxCopyMountRecord& record);

    virtual bool SaveMountRecord(LinuxCopyMountRecord& mountRecord);

    virtual bool ReadVolumeCopyMeta(const std::string& copyMetaDirPath, VolumeCopyMeta& volumeCopyMeta);

    virtual bool MountReadOnlyDevice(
        const std::string& devicePath,
        const std::string& mountTargetPath,
        const std::string& fsType,
        const std::string& mountOptions);
    
    virtual bool UmountDeviceIfExists(const std::string& mountTargetPath);
    
    virtual bool CreateReadOnlyDmDevice(
        const std::vector<CopySliceTarget> copySlices,
        std::string& dmDeviceName,
        std::string& dmDevicePath);
    
    virtual bool RemoveDmDeviceIfExists(const std::string& dmDeviceName);
    
    virtual bool AttachReadOnlyLoopDevice(const std::string& filePath, std::string& loopDevicePath);
    
    virtual bool DetachLoopDeviceIfAttached(const std::string& loopDevicePath);

    std::string GenerateNewDmDeviceName() const;

    // used to store checkpoint
    bool SaveLoopDeviceCreationRecord(const std::string& loopDevicePath);

    bool SaveDmDeviceCreationRecord(const std::string& dmDeviceName);

    bool RemoveLoopDeviceCreationRecord(const std::string& loopDevicePath);

    bool RemoveDmDeviceCreationRecord(const std::string& dmDeviceName);

    // native interface ...
    virtual bool CreateEmptyFileInCacheDir(const std::string& filename);

    virtual bool RemoveFileInCacheDir(const std::string& filename);

    virtual bool ListRecordFiles(std::vector<std::string>& filelist);

private:
    std::string m_cacheDirPath; // store the checkpoint and record info of the mount task
    std::vector<std::string> m_errors {};
};
#endif

}
}
#endif