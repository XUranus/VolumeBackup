#ifdef __linux

#include "LinuxVolumeCopyMountProvider.h"
#include "native/linux/DeviceMapperControl.h"
#include "Json.h"
#include "Logger.h"
#include "RawIO.h"
#include "VolumeUtils.h"
#include "native/FileSystemAPI.h"
#include "native/linux/LoopDeviceControl.h"

#ifdef __linux
#include <cerrno>
#include <fcntl.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <mntent.h>
#include <dirent.h>
#include <stdlib.h>
#include <unistd.h>
#endif

using namespace volumeprotect::mount;
using namespace volumeprotect;

#ifdef __linux

#define RECORD_ERROR(format, args...) do { \
    RecordError(format, ##args); \
    ERRLOG(format, ##args); \
} while (0) \

namespace {
    const std::string SEPARATOR = "/";
    const int NUM1 = 1;
    const std::string LOOPBACK_DEVICE_PATH_PREFIX = "/dev/loop";
}

// implement public methods here ...

std::unique_ptr<DeviceMapper> DeviceMapper::BuildDeviceMapper(const std::string& cacheDirPath)
{
    struct stat st {};
    if (::stat(cacheDirPath.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) {
        // invalid directory path
        return nullptr;
    }
    return std::unique_ptr<DeviceMapper>(new DeviceMapper(cacheDirPath));
}

DeviceMapper::DeviceMapper(const std::string& cacheDirPath)
    : m_cacheDirPath(cacheDirPath)
{}

bool DeviceMapper::MountCopy(const LinuxCopyMountConfig& mountConfig)
{
    LinuxCopyMountRecord mountRecord {};
    mountRecord.mountTargetPath = mountConfig.mountTargetPath;
    std::string devicePath;

    // read the copy meta json and build dm table
    VolumeCopyMeta volumeCopyMeta {};
    if (!ReadVolumeCopyMeta(mountConfig.copyMetaDirPath, volumeCopyMeta)) {
        return false;
    }
    // build loopback device from copy slice
    std::string copyName = volumeCopyMeta.copyName;
    CopyFormat copyFormat = static_cast<CopyFormat>(volumeCopyMeta.copyFormat);
    if (copyFormat != CopyFormat::BIN) {
        ERRLOG("unsupport copy format %d for linux mount provider!", volumeCopyMeta.copyFormat);
        return false;
    }
    for (const auto& segment : volumeCopyMeta.segments) {
        uint64_t volumeOffset = segment.offset;
        uint64_t size = segment.length;
        int sessionIndex = segment.index;
        std::string copyFilePath = util::GetCopyDataFilePath(mountConfig.copyDataDirPath, copyName, copyFormat, sessionIndex);
        std::string loopDevicePath;
        if (!AttachReadOnlyLoopDevice(copyFilePath, loopDevicePath)) {
            return false;
        }
        mountRecord.loopDevices.push_back(loopDevicePath);
        mountRecord.copySlices.emplace_back(CopySliceTarget {
             copyFilePath, volumeOffset, size, loopDevicePath });
        INFOLOG("attach loopback device %s => %s (offset %llu, size %llu)",
            loopDevicePath.c_str(), copyFilePath.c_str(), volumeOffset, size);
    }
    // using loopdevice in single slice case or create dm device in multiple slice case
    if (mountRecord.copySlices.size() == NUM1) {
        // only one copy slice, attach as loop device
        mountRecord.devicePath = mountRecord.loopDevices[0];
    } else {
        // multiple slices involved, need to attach loop device and create dm device
        std::string dmDeviceName;
        std::string dmDevicePath;
        if (!CreateReadOnlyDmDevice(mountRecord.copySlices, dmDeviceName, dmDevicePath)) {
            return false;
        }
        mountRecord.devicePath = dmDevicePath;
        mountRecord.dmDeviceName = dmDeviceName;
        INFOLOG("create devicemapper device %s, name = %s", dmDevicePath.c_str(), dmDeviceName.c_str());
    }

    // mount the loop/dm device to target
    if (!MountReadOnlyDevice(
        mountRecord.devicePath,
        mountConfig.mountTargetPath,
        mountConfig.mountFsType,
        mountConfig.mountOptions)) {
        return false;
    }
    // save mount record json to cache directory
    return SaveMountRecord(mountRecord);
}

bool DeviceMapper::UmountCopy()
{
    LinuxCopyMountRecord record {};
    bool success = true; // if error occurs, make every effort to clear the mount
    if (!ReadMountRecord(record)) {
        return false;
    }
    std::string devicePath = record.devicePath;
    std::string dmDeviceName = record.dmDeviceName;
    std::string mountTargetPath = record.mountTargetPath;
    // umount the device first
    if (!UmountDeviceIfExists(mountTargetPath.c_str())) {
        RECORD_ERROR("failed to umount target %s, errno %u", mountTargetPath.c_str(), errno);
        success = false;
    }
    // check if need to remove dm device
    if (!dmDeviceName.empty() && !RemoveDmDeviceIfExists(dmDeviceName)) {
        RECORD_ERROR("failed to remove devicemapper device %s, errno", dmDeviceName.c_str(), errno);
        success = false;
    }
    // finally detach all loopback devices involed
    for (const std::string& loopDevicePath: record.loopDevices) {
        if (!DetachLoopDeviceIfAttached(loopDevicePath)) {
            RECORD_ERROR("failed to detach loopback device %s, errno %u", devicePath.c_str(), errno);
            success = false;
        }
    }
    return success;
}

bool DeviceMapper::MountReadOnlyDevice(
    const std::string& devicePath,
    const std::string& mountTargetPath,
    const std::string& fsType,
    const std::string& mountOptions)
{
    unsigned long mountFlags = MS_RDONLY;
    if (::mount(devicePath.c_str(), mountTargetPath.c_str(), fsType.c_str(), mountFlags, mountOptions.c_str()) != 0) {
        RECORD_ERROR("mount %s to %s failed, type %s, option %s, errno %u",
            devicePath.c_str(), mountTargetPath.c_str(), fsType.c_str(), mountOptions.c_str(), errno);
        return false;
    }
    return true;
}

bool DeviceMapper::UmountDeviceIfExists(const std::string& mountTargetPath)
{
    if (!fsapi::IsMountPoint(mountTargetPath)) { // not mounted
    	return true;
    }
    // umount the target
    if (::umount2(mountTargetPath.c_str(), MNT_FORCE) != 0) {
        RECORD_ERROR("failed to umount target %s, errno %u", mountTargetPath.c_str(), errno);
        return false;
    }
    return true;
}

bool DeviceMapper::ClearResidue()
{
    bool success = true; // allow failure, make every effort to remove residual
    // check residual dm device and remove
    std::vector<std::string> dmDeviceResidualList;
    if (!LoadResidualDmDeviceList(dmDeviceResidualList)) {
        RECORD_ERROR("failed to load device mapper device residual list");
    }
    for (const std::string& dmDeviceName : dmDeviceResidualList) {
        if (!RemoveDmDeviceIfExists(dmDeviceName)) {
            success = false;
        }
    }
    // check residual loopback device and detach
    std::vector<std::string> loopDeviceResidualList;
    if (!LoadResidualLoopDeviceList(loopDeviceResidualList)) {
        RECORD_ERROR("failed to load loopback device residual list");
    }
    for (const std::string& loopDevicePath : loopDeviceResidualList) {
        if (!DetachLoopDeviceIfAttached(loopDevicePath)) {
            success = false;
        }
    }
    return success;
}

// used to load checkpoint in cache directory
bool DeviceMapper::LoadResidualLoopDeviceList(std::vector<std::string>& loopDeviceList)
{
    std::vector<std::string> filelist;
    if (!ListRecordFiles(filelist)) {
        return false;
    }
    // filter name list of all created loopback device
    std::copy_if(filelist.begin(), filelist.end(),
        std::back_inserter(loopDeviceList),
        [&](const std::string& filename) {
            return filename.find(LOOPBACK_DEVICE_CREATION_RECORD_SUFFIX) != std::string::npos;
        });
    for (std::string& loopDevicePath : loopDeviceList) {
        std::size_t pos = loopDevicePath.find(LOOPBACK_DEVICE_CREATION_RECORD_SUFFIX);
        loopDevicePath = LOOPBACK_DEVICE_PATH_PREFIX + loopDevicePath.substr(0, pos);
    }
    return true;
}

bool DeviceMapper::LoadResidualDmDeviceList(std::vector<std::string>& dmDeviceNameList)
{
    std::vector<std::string> filelist;
    if (!ListRecordFiles(filelist)) {
        return false;
    }
    // filter name list of all created dm device
    std::copy_if(filelist.begin(), filelist.end(),
        std::back_inserter(dmDeviceNameList),
        [&](const std::string& filename) {
            return filename.find(DEVICE_MAPPER_DEVICE_CREATION_RECORD_SUFFIX) != std::string::npos;
        });
    for (std::string& dmDeviceName : dmDeviceNameList) {
        dmDeviceName = dmDeviceName.substr(0, dmDeviceName.length() - DEVICE_MAPPER_DEVICE_CREATION_RECORD_SUFFIX.length());
    }
    return true;
}

std::string DeviceMapper::GetMountRecordJsonPath() const
{
    return m_cacheDirPath + SEPARATOR + MOUNT_RECORD_JSON_NAME;
}

// implement private methods here ...

bool DeviceMapper::ReadMountRecord(LinuxCopyMountRecord& record)
{
    std::string linuxCopyMountRecordJsonPath = m_cacheDirPath + SEPARATOR + MOUNT_RECORD_JSON_NAME;
    std::ifstream file(linuxCopyMountRecordJsonPath);
    if (!file.is_open()) {
        RECORD_ERROR("unabled to open copy mount record %s to read, errno %u", linuxCopyMountRecordJsonPath.c_str(), errno);
        return false;
    }
    std::string jsonContent;
    file >> jsonContent;
    xuranus::minijson::util::Deserialize(jsonContent, record);
    return true;
}

bool DeviceMapper::SaveMountRecord(const LinuxCopyMountRecord& mountRecord)
{
    std::string jsonContent = xuranus::minijson::util::Serialize(mountRecord);
    std::string filepath = m_cacheDirPath + SEPARATOR + MOUNT_RECORD_JSON_NAME;
    std::ofstream file(filepath.c_str(), std::ios::trunc);
    if (!file.is_open()) {
        RECORD_ERROR("failed to save mount record to %s, errno %u", filepath.c_str(), errno);
        return false;
    }
    file << jsonContent;
    file.close();
    return true;
}

bool DeviceMapper::ReadVolumeCopyMeta(const std::string& copyMetaDirPath, VolumeCopyMeta& volumeCopyMeta)
{
    if (!util::ReadVolumeCopyMeta(copyMetaDirPath, volumeCopyMeta)) {
        RECORD_ERROR("failed to read volume copy meta in directory %s", copyMetaDirPath.c_str());
        return false;
    }
    return true;
}

bool DeviceMapper::CreateReadOnlyDmDevice(
    const std::vector<CopySliceTarget> copySlices,
    std::string& dmDeviceName,
    std::string& dmDevicePath)
{
    dmDeviceName = GenerateNewDmDeviceName();
    devicemapper::DmTable dmTable;
    for (const auto& copySlice : copySlices) {
        std::string blockDevicePath = copySlice.loopDevicePath;
        uint64_t sectorSize = 0LLU;
        try {
            sectorSize = fsapi::ReadSectorSizeLinux(blockDevicePath);
        } catch (const SystemApiException& e) {
            RECORD_ERROR(e.what());
            return false;
        }
        uint64_t startSector = copySlice.volumeOffset / sectorSize;
        uint64_t sectorsCount = copySlice.size / sectorSize;
        dmTable.AddTarget(std::make_shared<devicemapper::DmTargetLinear>(
            blockDevicePath, startSector, sectorsCount, 0
        ));
    }
    if (!devicemapper::CreateDevice(dmDeviceName, dmTable, dmDevicePath)) {
        RECORD_ERROR("failed to create dm device, errno %u", errno);
        return false;
    }
    // keep checkpoint for devicemapper device creation
    SaveDmDeviceCreationRecord(dmDeviceName);
    return true;
}

bool DeviceMapper::RemoveDmDeviceIfExists(const std::string& dmDeviceName)
{
    if (!devicemapper::RemoveDeviceIfExists(dmDeviceName)) {
        RECORD_ERROR("failed to remove dm device %s, errno %u", dmDeviceName.c_str(), errno);
        return false;
    }
    RemoveDmDeviceCreationRecord(dmDeviceName);
    return true;
}

bool DeviceMapper::AttachReadOnlyLoopDevice(const std::string& filePath, std::string& loopDevicePath)
{
    if (!loopback::Attach(filePath, loopDevicePath, O_RDONLY)) {
        RECORD_ERROR("failed to attach read only loopback device from %s, errno %u", filePath.c_str(), errno);
        return false;
    }
    // keep checkpoint for loopback device creation
    SaveLoopDeviceCreationRecord(loopDevicePath);
    return true;
}

bool DeviceMapper::DetachLoopDeviceIfAttached(const std::string& loopDevicePath)
{
    if (!loopback::Attached(loopDevicePath)) {
        RemoveLoopDeviceCreationRecord(loopDevicePath);
        return true;
    }
    if (!loopback::Detach(loopDevicePath)) {
        RECORD_ERROR("failed to detach loopback device %s, errno %u", loopDevicePath.c_str(), errno);
        return false;
    }
    RemoveLoopDeviceCreationRecord(loopDevicePath);
    return true;
}

std::string DeviceMapper::GenerateNewDmDeviceName() const
{
    namespace chrono = std::chrono;
    using clock = std::chrono::system_clock;
    auto timestamp = std::chrono::duration_cast<chrono::microseconds>(clock::now().time_since_epoch()).count();
    // name size must be limited in DM_NAME_LEN
    return std::string(DEVICE_MAPPER_DEVICE_NAME_PREFIX) + std::to_string(timestamp);
}

// used to store checkpoint
bool DeviceMapper::SaveLoopDeviceCreationRecord(const std::string& loopDevicePath)
{
    if (loopDevicePath.find(LOOPBACK_DEVICE_PATH_PREFIX) == 0) {
        std::string loopDeviceNumber = loopDevicePath.substr(LOOPBACK_DEVICE_PATH_PREFIX.length());
        return CreateEmptyFileInCacheDir(loopDeviceNumber + LOOPBACK_DEVICE_CREATION_RECORD_SUFFIX);
    }
    RECORD_ERROR("save loop device creation record failed, loopback device %s",
        loopDevicePath.c_str());
    return false;
}

bool DeviceMapper::SaveDmDeviceCreationRecord(const std::string& dmDeviceName)
{
    if (dmDeviceName.find(SEPARATOR) == std::string::npos) {
        return CreateEmptyFileInCacheDir(dmDeviceName + DEVICE_MAPPER_DEVICE_CREATION_RECORD_SUFFIX);
    }
    RECORD_ERROR("save dm device creation record failed, dm device name %s",
        dmDeviceName.c_str());
    return false;
}

bool DeviceMapper::RemoveLoopDeviceCreationRecord(const std::string& loopDevicePath)
{
    if (loopDevicePath.find(LOOPBACK_DEVICE_PATH_PREFIX) == 0) {
        std::string loopDeviceNumber = loopDevicePath.substr(LOOPBACK_DEVICE_PATH_PREFIX.length());
        return RemoveFileInCacheDir(loopDeviceNumber + LOOPBACK_DEVICE_CREATION_RECORD_SUFFIX);
    }
    RECORD_ERROR("remove loop device creation record failed, loopback device path %s, cache dir %s",
        loopDevicePath.c_str(), m_cacheDirPath.c_str());
    return false;
}

bool DeviceMapper::RemoveDmDeviceCreationRecord(const std::string& dmDeviceName)
{
    if (dmDeviceName.find(SEPARATOR) == std::string::npos) {
        return RemoveFileInCacheDir(dmDeviceName + DEVICE_MAPPER_DEVICE_CREATION_RECORD_SUFFIX);
    }
    RECORD_ERROR("save dm device creation record failed, dm device name %s, cache dir %s",
        dmDeviceName.c_str(), m_cacheDirPath.c_str());
    return false;
}

// create empty file in cache directory is not exist
bool DeviceMapper::CreateEmptyFileInCacheDir(const std::string& filename)
{
    std::string fullpath = m_cacheDirPath + SEPARATOR + filename;
    int fd = ::open(fullpath.c_str(), O_CREAT | O_WRONLY, 0644);
    if (fd == -1) {
        RECORD_ERROR("error creating empty file %s, errno %u", fullpath.c_str(), errno);
        return false;
    }
    ::close(fd);
    return true;
}

// remove file in cache directory is exists
bool DeviceMapper::RemoveFileInCacheDir(const std::string& filename)
{
    std::string fullpath = m_cacheDirPath + SEPARATOR + filename;
    if (::access(fullpath.c_str(), F_OK) == 0 && ::unlink(fullpath.c_str()) < 0) {
        RECORD_ERROR("failed to remove file %s, errno %u", fullpath.c_str(), errno);
        return false;
    }
    return true;
}

// load all files in cache dir
bool DeviceMapper::ListRecordFiles(std::vector<std::string>& filelist)
{
    DIR* dir = ::opendir(m_cacheDirPath.c_str());
    if (dir == nullptr) {
        RECORD_ERROR("error opening directory %s, errno %u", m_cacheDirPath.c_str(), errno);
        return false;
    }
    struct dirent* entry = nullptr;
    while ((entry = ::readdir(dir)) != nullptr) {
        if (entry->d_type == DT_REG) {
            filelist.emplace_back(entry->d_name);
        }
    }
    ::closedir(dir);
    return true;
}

#endif
#endif