#include "CopyMountProvider.h"
#include "DeviceMapperControl.h"
#include "Json.h"
#include "Logger.h"
#include "VolumeUtils.h"
#include "native/LoopDeviceControl.h"
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>

#ifdef __linux
#include <cerrno>
#include <fcntl.h>
#include <sys/mount.h>
#include <sys/stat.h>
#endif

using namespace volumeprotect::mount;
using namespace volumeprotect;

#ifdef __linux

namespace {
    const uint64_t SECTOR_SIZE = 512; // 512 bytes per sector
    const std::string SEPARATOR = "/";
    const std::string MOUNT_RECORD_JSON_NAME = "volumecopymount.record.json";
}


std::unique_ptr<LinuxMountProvider> LinuxMountProvider::BuildLinuxMountProvider(const std::string& cacheDirPath)
{
    struct stat st {};
    if (::stat(cacheDirPath.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) {
        // invalid directory path
        return nullptr;
    }
    return std::unique_ptr<LinuxMountProvider>(new LinuxMountProvider(cacheDirPath));
}

std::unique_ptr<LinuxMountProvider> LinuxMountProvider::BuildLinuxUmountProvider()
{
    // umount does not need cache dir
    return std::unique_ptr<LinuxMountProvider>(new LinuxMountProvider(""));
}

LinuxMountProvider::LinuxMountProvider(const std::string& cacheDirPath)
    : m_cacheDirPath(cacheDirPath), m_error("")
{}

bool LinuxMountProvider::MountCopy(
    const LinuxCopyMountConfig& mountConfig,
    std::string& linuxCopyMountRecordJsonPath)
{
    LinuxCopyMountRecord mountRecord {};
    if (!MountCopy(mountConfig, mountRecord)) {
        return false;
    }
    std::string jsonContent = xuranus::minijson::util::Serialize(mountRecord);
    std::string filepath = m_cacheDirPath + SEPARATOR + MOUNT_RECORD_JSON_NAME;
    std::ofstream file(filepath.c_str(), std::ios::trunc);
    if (!file.is_open()) {
        SetError("unable to open copy mount record %s for write, errno %d", filepath.c_str(), errno);
        return false;
    }
    file << jsonContent;
    file.close();
    return true;
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

bool LinuxMountProvider::UmountCopy(const std::string& linuxCopyMountRecordJsonPath)
{
    std::ifstream file(linuxCopyMountRecordJsonPath);
    if (!file.is_open()) {
        SetError("unabled to open copy mount record %s to read, errno %d", linuxCopyMountRecordJsonPath.c_str(), errno);
        return false;
    }
    std::string jsonContent;
    file >> jsonContent;
    LinuxCopyMountRecord record {};
    xuranus::minijson::util::Deserialize(jsonContent, record);
    return UmountCopy(record);
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

const std::string& LinuxMountProvider::GetError() const
{
    return m_error;
}

void LinuxMountProvider::SetError(const char* message, ...)
{
    va_list args;
    va_start(args, message);
    size_t size = std::vsnprintf(nullptr, 0, message, args) + 1;
    char* buffer = new char[size];
    std::vsnprintf(buffer, size, message, args);
    va_end(args);
    std::string formattedString(buffer);
    delete[] buffer;
    m_error = formattedString;
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

// used for c library
#ifdef __cplusplus
extern "C" {
#endif

// implement C style interface ...
inline static std::string StringFromCStr(char* str)
{
    return str == nullptr ? std::string("") : std::string(str);
}

void* CreateLinuxMountProvider(char* cacheDirPath)
{
    return reinterpret_cast<void*>(LinuxMountProvider::BuildLinuxMountProvider(cacheDirPath).release());
}

bool MountLinuxVolumeCopy(
    void*                   mountProvider,
    LinuxCopyMountConf_C    conf,
    char*                   pathBuffer,
    int                     bufferMax)
{
    LinuxCopyMountConfig mountConfig {};
    std::string linuxCopyMountRecordJsonPath {};
    mountConfig.copyMetaDirPath = StringFromCStr(conf.copyMetaDirPath);
    mountConfig.copyDataDirPath = StringFromCStr(conf.copyDataDirPath);
    mountConfig.mountTargetPath = StringFromCStr(conf.mountTargetPath);
    mountConfig.mountFsType = StringFromCStr(conf.mountFsType);
    mountConfig.mountOptions = StringFromCStr(conf.mountOptions);
    bool ret = reinterpret_cast<LinuxMountProvider*>(mountProvider)->MountCopy(mountConfig, linuxCopyMountRecordJsonPath);
    if (ret && !linuxCopyMountRecordJsonPath.empty()) {
        memset(pathBuffer, 0, bufferMax);
        ::strcpy(pathBuffer, linuxCopyMountRecordJsonPath.c_str());
    }
    return ret;
}

bool UmountLinuxVolumeCopy(void* mountProvider, char* linuxCopyMountRecordJsonPath)
{
    return reinterpret_cast<LinuxMountProvider*>(mountProvider)->UmountCopy(linuxCopyMountRecordJsonPath);
}

void DestroyLinuxMountProvider(void* mountProvider)
{
    delete reinterpret_cast<LinuxMountProvider*>(mountProvider);
}

const char* GetLinuxMountProviderError(void* mountProvider)
{
    return reinterpret_cast<LinuxMountProvider*>(mountProvider)->GetError().c_str();
}

#ifdef __cplusplus
}
#endif

#endif