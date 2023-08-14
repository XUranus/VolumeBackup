#include "CopyMountProvider.h"
#include "DeviceMapperControl.h"
#include "Logger.h"
#include "VolumeUtils.h"
#include "dm/LoopDeviceControl.h"
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

#ifdef __linux
#include <cerrno>
#include <fcntl.h>
#include <sys/mount.h>
#endif

using namespace volumeprotect::mount;
using namespace volumeprotect;

#ifdef __linux

namespace {
    const uint64_t SECTOR_SIZE = 512; // 512 bytes per sector
}


bool LinuxMountProvider::MountCopy(
    const LinuxCopyMountConfig& mountConfig,
    LinuxCopyMountRecord& mountRecord)
{
    mountRecord.mountTargetPath = mountConfig.mountTargetPath;
    std::string devicePath;

    // read the copy meta json and build dm table
    VolumeCopyMeta volumeCopyMeta {};
    if (!util::ReadVolumeCopyMeta(mountConfig.copyMetaDirPath, volumeCopyMeta)) {
        ERRLOG("failed to read volume copy meta in %s", mountConfig.copyMetaDirPath.c_str());
        return false;
    }

    for (const auto& range : volumeCopyMeta.copySlices) {
        uint64_t volumeOffset = range.first;
        uint64_t size = range.second;
        std::string copyFilePath = util::GetCopyFilePath(mountConfig.copyDataDirPath, volumeOffset, size);
        std::string loopDevicePath;
        if (!AttachReadonlyLoopDevice(copyFilePath, loopDevicePath)) {
            return false;
        }
        // TODO:: keep checkpoint for loopback device creation
        mountRecord.loopDevices.push_back(loopDevicePath);
        mountRecord.copySlices.emplace_back(CopySliceTarget {
             copyFilePath, volumeOffset, size, loopDevicePath });
        INFOLOG("attach loopback device %s => %s (offset %llu, size %llu)",
            loopDevicePath.c_str(), copyFilePath.c_str(), volumeOffset, size);
    }

    if (mountRecord.copySlices.size() == 1) {
        // only one copy slice, attach as loop device
        mountRecord.devicePath = mountRecord.loopDevices[0];
    } else {
        // multiple slices involved, need to attach loop device and create dm device
        std::string dmDeviceName;
        std::string dmDevicePath;
        if (!CreateReadonlyDmDevice(mountRecord.copySlices, dmDeviceName, dmDevicePath)) {
            ERRLOG("failed to create readonly devicemapper device");
            return false;
        }
        // TODO:: keep checkpoint for devicemapper device creation
        mountRecord.devicePath = dmDevicePath;
        mountRecord.dmDeviceName = dmDeviceName;
        INFOLOG("create devicemapper device %s, name = %s", dmDevicePath.c_str(), dmDeviceName.c_str());
    }

    // mount the loop/dm device to target
    if (!MountReadonlyDevice(
            mountRecord.devicePath,
            mountConfig.mountTargetPath,
            mountConfig.mountFsType,
            mountConfig.mountOptions)) {
        ERRLOG("failed to mount %s to %s",
            mountRecord.devicePath.c_str(), mountConfig.mountTargetPath.c_str());
        return false;
    }
    return true;
}

bool LinuxMountProvider::UmountCopy(const LinuxCopyMountRecord& record)
{
    std::string devicePath = record.devicePath;
    std::string dmDeviceName = record.dmDeviceName;
    std::string mountTargetPath = record.mountTargetPath;
    // umount the device first
    if (!UmountDevice(mountTargetPath.c_str())) {
        ERRLOG("failed to umount target %s, errno %u", mountTargetPath.c_str(), errno);
        return false;
    }
    // check if need to remove dm device
    if (!dmDeviceName.empty() && !RemoveDmDevice(dmDeviceName)) {
        ERRLOG("failed to remove devicemapper device %s, errno", dmDeviceName.c_str(), errno);
        return false;
    }
    // finally detach all loopback devices involed
    for (const std::string& loopDevicePath: record.loopDevices) {
        if (!DetachLoopDevice(loopDevicePath)) {
            DBGLOG("failed to detach loopback device %s, errno %u", devicePath.c_str(), errno);
            return false;
        }
    }
    return true;
}

bool LinuxMountProvider::MountReadonlyDevice(
    const std::string& devicePath,
    const std::string& mountTargetPath,
    const std::string& fsType,
    const std::string& mountOptions)
{
    unsigned long mountFlags = MS_RDONLY;
    if (::mount(devicePath.c_str(), mountTargetPath.c_str(), fsType.c_str(), mountFlags, mountOptions.c_str()) != 0) {
        ERRLOG("mount failed");
        return false;
    }
    return true;
}

bool LinuxMountProvider::UmountDevice(const std::string& mountTargetPath)
{
    if (::umount2(mountTargetPath.c_str(), MNT_FORCE) != 0) {
        ERRLOG("failed to umount target %s, errno %u", mountTargetPath.c_str(), errno);
        return false;
    }
    return true;
}

bool LinuxMountProvider::CreateReadonlyDmDevice(
        const std::vector<CopySliceTarget> copySlices,
        std::string& dmDeviceName,
        std::string& dmDevicePath)
{
    dmDeviceName = GenerateNewDmDeviceName();
    devicemapper::DmTable dmTable;
    for (const auto& copySlice : copySlices) {
        std::string blockDevicePath = copySlice.loopDevicePath;
        uint64_t startSector = copySlice.volumeOffset / SECTOR_SIZE;
        uint64_t sectorsCount = copySlice.size / SECTOR_SIZE;
        dmTable.AddTarget(std::make_shared<devicemapper::DmTargetLinear>(
            blockDevicePath,
            startSector,
            sectorsCount,
            0
        ));
    }
    return devicemapper::CreateDevice(dmDeviceName, dmTable, dmDevicePath);
}

bool LinuxMountProvider::RemoveDmDevice(const std::string& dmDeviceName)
{
    return devicemapper::RemoveDeviceIfExists(dmDeviceName);
}

bool LinuxMountProvider::AttachReadonlyLoopDevice(const std::string& filePath, std::string& loopDevicePath)
{
    if (!loopback::Attach(filePath, loopDevicePath, O_RDONLY)) {
        ERRLOG("failed to attach readonly loopback device from %s, errno %u", filePath.c_str(), errno);
        return false;
    }
    return true;
}

bool LinuxMountProvider::DetachLoopDevice(const std::string& loopDevicePath)
{
    if (!loopback::Detach(loopDevicePath)) {
        ERRLOG("failed to detach loopback device %s, errno %u", loopDevicePath.c_str(), errno);
        return false;
    }
    return true;
}

std::string LinuxMountProvider::GenerateNewDmDeviceName() const
{
    namespace chrono = std::chrono;
    using clock = std::chrono::system_clock;
    auto timestamp = std::chrono::duration_cast<chrono::microseconds>(clock::now().time_since_epoch()).count();
    return std::string("volumeprotect_dm_copy_") + std::to_string(timestamp);
}

#endif