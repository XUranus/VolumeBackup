#ifndef VOLUMEBACKUP_LINUX_COPY_MOUNT_PROVIDER_HEADER
#define VOLUMEBACKUP_LINUX_COPY_MOUNT_PROVIDER_HEADER

#ifdef __linux__

// external logger/json library
#include "Json.h"
#include "VolumeUtils.h"
#include "VolumeCopyMountProvider.h"

namespace volumeprotect {
namespace mount {

// same as LinuxLoopbackMountProviderParams
struct LinuxDeviceMapperMountProviderParams {
    std::string                 outputDirPath;
    std::string                 copyDataDirPath;
    std::string                 copyMetaDirPath;
    std::string                 copyName;
    std::vector<CopySegment>    segments;
    std::string                 mountTargetPath;
    std::string                 mountFsType;
    std::string                 mountOptions;
};

struct CopySliceTarget {
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

struct LinuxDeviceMapperCopyMountRecord {
    int                         copyFormat;
    // attributes required for umount
    std::string                 dmDeviceName;       // [opt] required only multiple copy files contained in a volume
    std::vector<std::string>    loopDevices;        // loopback device path like /dev/loopX
    std::string                 devicePath;         // block device mounted (loopback device path or dm device path)
    std::string                 mountTargetPath;    // the mount point path

    // attribute and origin mount config, used only for debug
    std::vector<CopySliceTarget>    copySlices;
    std::string                 copyDataDirPath;
    std::string                 copyMetaDirPath;
    std::string                 copyName;
    std::string                 mountFsType;
    std::string                 mountOptions;

    SERIALIZE_SECTION_BEGIN
    SERIALIZE_FIELD(copyFormat, copyFormat);
    SERIALIZE_FIELD(dmDeviceName, dmDeviceName);
    SERIALIZE_FIELD(loopDevices, loopDevices);
    SERIALIZE_FIELD(devicePath, devicePath);
    SERIALIZE_FIELD(mountTargetPath, mountTargetPath);
    SERIALIZE_FIELD(copySlices, copySlices);
    SERIALIZE_FIELD(copyDataDirPath, copyDataDirPath);
    SERIALIZE_FIELD(copyMetaDirPath, copyMetaDirPath);
    SERIALIZE_FIELD(copyName, copyName);
    SERIALIZE_FIELD(mountFsType, mountFsType);
    SERIALIZE_FIELD(mountOptions, mountOptions);
    SERIALIZE_SECTION_END
};

/**
 * @brief LinuxDeviceMapperMountProvider provide api to mount volume copy with CopyFormat::BIN on Linux system.
 * This provider need a output directory to store mount record and checkpoint file for each mount task.
 *
 * For a copy contains only one session, LinuxDeviceMapperMountProvider will create a loopback device from the file
 *    to be mount directly.
 * For a copy contains multiple sessions, LinuxDeviceMapperMountProvider will assign a loopback device for each
 *    copy file and create a devicemapper device with linear targets using the loopback devices.
 *
 * To ensure robust:
 * For each created dm device, a "dmDeviceName.dm.record" file will be created,
 *    and for each attached loop device, a "loopX.loop.record" file will be created.
 * These files will be used to track residual device if mount task is partial failed.
 * RollbackClearResidue method will be called to try to clear the residual device. if moun is failed.
 */
class LinuxDeviceMapperMountProvider : public VolumeCopyMountProvider {
public:
    static std::unique_ptr<LinuxDeviceMapperMountProvider> Build(
        const VolumeCopyMountConfig& volumeCopyMountConfig,
        const VolumeCopyMeta& volumeCopyMeta);

    explicit LinuxDeviceMapperMountProvider(const LinuxDeviceMapperMountProviderParams& params);

    virtual ~LinuxDeviceMapperMountProvider() = default;

    bool Mount() override;

    // if mount failed, caller can call this methods to try to remove residual loop/dm device
    bool RollbackClearResidue();

    // used to load residual record in cache directory
    bool LoadResidualLoopDeviceList(std::vector<std::string>& loopDeviceList);

    bool LoadResidualDmDeviceList(std::vector<std::string>& dmDeviceNameList);

    std::string GetMountRecordPath() const override;

protected:
    virtual bool MountReadOnlyDevice(
        const std::string& devicePath,
        const std::string& mountTargetPath,
        const std::string& fsType,
        const std::string& mountOptions);

    virtual bool CreateReadOnlyDmDevice(
        const std::vector<CopySliceTarget> copySlices,
        std::string& dmDeviceName,
        std::string& dmDevicePath);

    virtual bool RemoveDmDeviceIfExists(const std::string& dmDeviceName);

    virtual bool AttachReadOnlyLoopDevice(const std::string& filePath, std::string& loopDevicePath);

    virtual bool DetachLoopDeviceIfAttached(const std::string& loopDevicePath);

    std::string GenerateNewDmDeviceName() const;

    virtual bool ListRecordFiles(std::vector<std::string>& filelist);

private:
    std::string     m_outputDirPath;
    std::string     m_copyDataDirPath;
    std::string     m_copyMetaDirPath;
    std::string     m_copyName;
    std::string     m_mountTargetPath;
    std::string     m_mountFsType;
    std::string     m_mountOptions;
    std::vector<CopySegment>    m_segments;
};


class LinuxDeviceMapperUmountProvider : public VolumeCopyUmountProvider {
public:
    static std::unique_ptr<LinuxDeviceMapperUmountProvider> Build(
        const std::string& mountRecordJsonFilePath,
        const std::string& outputDirPath);

    LinuxDeviceMapperUmountProvider(
    	const std::string& outputDirPath,
        const std::string& mountTargetPath,
        const std::string& dmDeviceName,
        const std::vector<std::string> loopDevices);

    ~LinuxDeviceMapperUmountProvider() = default;

    bool Umount() override;

private:
    std::string m_outputDirPath;
    std::string m_mountTargetPath;
    std::string m_dmDeviceName;
    std::vector<std::string> m_loopDevices;
};

}
}

#endif

#endif