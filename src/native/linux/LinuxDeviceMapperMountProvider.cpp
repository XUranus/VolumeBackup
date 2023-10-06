//#ifdef __linux

#include "LinuxVolumeCopyMountProvider.h"
#include "native/linux/LinuxDeviceMapperMountProviderControl.h"
#include "Json.h"
#include "Logger.h"
#include "RawIO.h"
#include "VolumeUtils.h"
#include "native/FileSystemAPI.h"
#include "native/linux/LoopDeviceControl.h"

#include <cerrno>
#include <fcntl.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <mntent.h>
#include <dirent.h>
#include <stdlib.h>
#include <unistd.h>

using namespace volumeprotect::mount;
using namespace volumeprotect::util;
using namespace volumeprotect::fsapi;
using namespace volumeprotect;

namespace {
    const std::string SEPARATOR = "/";
    const int NUM1 = 1;
    const std::string LOOPBACK_DEVICE_PATH_PREFIX = "/dev/loop";
    const std::string DEVICE_MAPPER_DEVICE_NAME_PREFIX = "volumeprotect_dm_copy_";
    const std::string LOOPBACK_DEVICE_CREATION_RECORD_SUFFIX = ".loop.record";
    const std::string DEVICE_MAPPER_DEVICE_CREATION_RECORD_SUFFIX = ".dm.record";
}

// used to create/remove checkpoint
inline void SaveLoopDeviceCreationRecord(const std::string& outputDirPath, const std::string& loopDevicePath)
{
    if (loopDevicePath.find(LOOPBACK_DEVICE_PATH_PREFIX) != 0) {
        ERRLOG("save loop device creation record failed, illegal loopback device %s", loopDevicePath.c_str());
        return;
    }
    std::string loopDeviceNumber = loopDevicePath.substr(LOOPBACK_DEVICE_PATH_PREFIX.length());
    std::string filename = loopDeviceNumber + LOOPBACK_DEVICE_CREATION_RECORD_SUFFIX;
    if (!fsapi::CreateEmptyFile(outputDirPath, filename)) {
        ERRLOG("save loop device creation record failed to create checkpoint file %s", filename.c_str());
    }
    return;
}

inline void SaveDmDeviceCreationRecord(const std::string& outputDirPath, const std::string& dmDeviceName)
{
    if (dmDeviceName.find(SEPARATOR) != std::string::npos) {
        ERRLOG("save dm device creation record failed, illegal dm device name %s", dmDeviceName.c_str());
        return;
    }
    std::string filename = dmDeviceName + DEVICE_MAPPER_DEVICE_CREATION_RECORD_SUFFIX;
    if (!fsapi::CreateEmptyFile(outputDirPath, filename)) {
        ERRLOG("save dm device creation record failed, failed to create checkpoint %s", filename.c_str());
    }
    return;
}

inline void RemoveLoopDeviceCreationRecord(const std::string& outputDirPath, const std::string& loopDevicePath)
{
    if (loopDevicePath.find(LOOPBACK_DEVICE_PATH_PREFIX) != 0) {
        ERRLOG("remove loop device creation record failed, illegal loopback device %s", loopDevicePath.c_str());
        return;
    }
    std::string loopDeviceNumber = loopDevicePath.substr(LOOPBACK_DEVICE_PATH_PREFIX.length());
    std::string filename = loopDeviceNumber + LOOPBACK_DEVICE_CREATION_RECORD_SUFFIX;
    if (!fsapi::RemoveFile(outputDirPath, filename)) {
        ERRLOG("remove loop device creation record failed, failed to remove checkpoin%s", filename.c_str());
    }
}

inline void RemoveDmDeviceCreationRecord(const std::string& outputDirPath, const std::string& dmDeviceName)
{
    if (dmDeviceName.find(SEPARATOR) != std::string::npos) {
        ERRLOG("remove dm device creation record failed, illegal dm device name %s", dmDeviceName.c_str());
        return;
    }
    std::string filename = dmDeviceName + DEVICE_MAPPER_DEVICE_CREATION_RECORD_SUFFIX;
    if (!fsapi::RemoveFile(outputDirPath, filename)) {
        ERRLOG("remove dm device creation record failed, failed to remove checkpoint %s", filename.c_str());
    }
    return;
}

// implement public methods here ...
std::unique_ptr<LinuxDeviceMapperMountProvider> LinuxLoopbackMountProvider::Build(
    const VolumeCopyMountConfig& volumeCopyMountConfig,
    const VolumeCopyMeta& volumeCopyMeta)
{
    CopyFormat copyFormat = static_cast<CopyFormat>(volumeCopyMeta.copyFormat);
    if (copyFormat != CopyFormat::BIN) {
        ERRLOG("unsupport copy format %d for linux devicemapper mount provider!", volumeCopyMeta.copyFormat);
        return nullptr;
    }
    if (!fsapi::IsDirectoryExists(volumeCopyMountConfig.outputDirPath)) {
        ERRLOG("invalid output directory path %s", volumeCopyMountConfig.outputDirPath.c_str());
        return nullptr;
    }
    if (!fsapi::IsDirectoryExists(volumeCopyMountConfig.copyDataDirPath)) {
        ERRLOG("invalid copy data directory path %s", volumeCopyMountConfig.copyDataDirPath.c_str());
        return nullptr;
    }
    if (!fsapi::IsDirectoryExists(volumeCopyMountConfig.copyMetaDirPath)) {
        ERRLOG("invalid copy meta directory path %s", volumeCopyMountConfig.copyMetaDirPath.c_str());
        return nullptr;
    }
    LinuxDeviceMapperMountProviderParams params {};
    params.outputDirPath = volumeCopyMountConfig.outputDirPath;
    params.copyDataDirPath = volumeCopyMountConfig.copyDataDirPath;
    params.copyMetaDirPath = volumeCopyMountConfig.copyMetaDirPath;
    params.copyName = volumeCopyMeta.copyName;
    if (volumeCopyMeta.segments.empty()) {
        ERRLOG("illegal volume copy meta, image file segments list empty");
        return nullptr;
    }
    params.segments = volumeCopyMeta.segments;
    params.mountTargetPath = volumeCopyMountConfig.mountTargetPath;
    params.mountFsType = volumeCopyMountConfig.mountFsType;
    params.mountOptions = volumeCopyMountConfig.mountOptions;
    return std::make_unique<LinuxDeviceMapperMountProvider>(params);
}

LinuxDeviceMapperMountProvider::LinuxDeviceMapperMountProvider(
    const LinuxDeviceMapperMountProviderParams& params)
    : m_outputDirPath(params.outputDirPath),
    m_copyDataDirPath(params.copyDataDirPath),
    m_copyMetaDirPath(params.copyMetaDirPath),
    m_copyName(params.copyName),
    m_mountTargetPath(params.mountTargetPath),
    m_mountFsType(params.mountFsType),
    m_mountOptions(params.mountOptions),
    m_segments(params.segments)
{}

bool LinuxDeviceMapperMountProvider::Mount()
{
    LinuxDeviceMapperCopyMountRecord mountRecord {};
    mountRecord.mountTargetPath = m_mountTargetPath;
    std::string devicePath;
    // init and attach loopback device from each copy slice
    for (const auto& segment : m_segments) {
        uint64_t volumeOffset = segment.offset;
        uint64_t size = segment.length;
        int sessionIndex = segment.index;
        std::string copyFilePath = util::GetCopyDataFilePath(
            m_copyDataDirPath, m_copyName, CopyFormat::BIN, sessionIndex);
        std::string loopDevicePath;
        if (!AttachReadOnlyLoopDevice(copyFilePath, loopDevicePath)) {
            return false;
        }
        mountRecord.loopDevices.push_back(loopDevicePath);
        mountRecord.copySlices.emplace_back(CopySliceTarget { copyFilePath, volumeOffset, size, loopDevicePath });
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
    if (!MountReadOnlyDevice(mountRecord.devicePath, m_mountTargetPath, m_mountFsType, m_mountOptions)) {
        return false;
    }
    // save mount record json to cache directory
    return SaveMountRecord(mountRecord);
}

bool LinuxDeviceMapperMountProvider::MountReadOnlyDevice(
    const std::string& devicePath,
    const std::string& mountTargetPath,
    const std::string& fsType,
    const std::string& mountOptions)
{
    unsigned long mountFlags = MS_RDONLY;
    if (::mount(devicePath.c_str(), mountTargetPath.c_str(), fsType.c_str(), mountFlags, mountOptions.c_str()) != 0) {
        RECORD_INNER_ERROR("mount %s to %s failed, type %s, option %s, errno %u",
            devicePath.c_str(), mountTargetPath.c_str(), fsType.c_str(), mountOptions.c_str(), errno);
        return false;
    }
    return true;
}

bool LinuxDeviceMapperMountProvider::ClearResidue()
{
    bool success = true; // allow failure, make every effort to remove residual
    // check residual dm device and remove
    std::vector<std::string> dmDeviceResidualList;
    if (!LoadResidualDmDeviceList(dmDeviceResidualList)) {
        RECORD_INNER_ERROR("failed to load device mapper device residual list");
    }
    for (const std::string& dmDeviceName : dmDeviceResidualList) {
        if (!RemoveDmDeviceIfExists(dmDeviceName)) {
            success = false;
        }
    }
    // check residual loopback device and detach
    std::vector<std::string> loopDeviceResidualList;
    if (!LoadResidualLoopDeviceList(loopDeviceResidualList)) {
        RECORD_INNER_ERROR("failed to load loopback device residual list");
    }
    for (const std::string& loopDevicePath : loopDeviceResidualList) {
        if (!DetachLoopDeviceIfAttached(loopDevicePath)) {
            success = false;
        }
    }
    return success;
}

// used to load checkpoint in cache directory
bool LinuxDeviceMapperMountProvider::LoadResidualLoopDeviceList(std::vector<std::string>& loopDeviceList)
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

bool LinuxDeviceMapperMountProvider::LoadResidualDmDeviceList(std::vector<std::string>& dmDeviceNameList)
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

std::string LinuxDeviceMapperMountProvider::GetMountRecordJsonPath() const
{
    return m_outputDirPath + SEPARATOR + MOUNT_RECORD_JSON_NAME;
}

// implement private methods here ...

bool LinuxDeviceMapperMountProvider::ReadMountRecord(LinuxDeviceMapperCopyMountRecord& record)
{
    std::string linuxCopyMountRecordJsonPath = m_outputDirPath + SEPARATOR + MOUNT_RECORD_JSON_NAME;
    std::ifstream file(linuxCopyMountRecordJsonPath);
    if (!file.is_open()) {
        RECORD_INNER_ERROR("unabled to open copy mount record %s to read, errno %u", linuxCopyMountRecordJsonPath.c_str(), errno);
        return false;
    }
    std::string jsonContent;
    file >> jsonContent;
    xuranus::minijson::util::Deserialize(jsonContent, record);
    return true;
}

bool LinuxDeviceMapperMountProvider::SaveMountRecord(const LinuxDeviceMapperCopyMountRecord& mountRecord)
{
    std::string jsonContent = xuranus::minijson::util::Serialize(mountRecord);
    std::string filepath = m_outputDirPath + SEPARATOR + MOUNT_RECORD_JSON_NAME;
    std::ofstream file(filepath.c_str(), std::ios::trunc);
    if (!file.is_open()) {
        RECORD_INNER_ERROR("failed to save mount record to %s, errno %u", filepath.c_str(), errno);
        return false;
    }
    file << jsonContent;
    file.close();
    return true;
}

bool LinuxDeviceMapperMountProvider::ReadVolumeCopyMeta(const std::string& copyMetaDirPath, VolumeCopyMeta& volumeCopyMeta)
{
    if (!util::ReadVolumeCopyMeta(copyMetaDirPath, volumeCopyMeta)) {
        RECORD_INNER_ERROR("failed to read volume copy meta in directory %s", copyMetaDirPath.c_str());
        return false;
    }
    return true;
}

bool LinuxDeviceMapperMountProvider::CreateReadOnlyDmDevice(
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
            RECORD_INNER_ERROR(e.what());
            return false;
        }
        uint64_t startSector = copySlice.volumeOffset / sectorSize;
        uint64_t sectorsCount = copySlice.size / sectorSize;
        dmTable.AddTarget(std::make_shared<devicemapper::DmTargetLinear>(
            blockDevicePath, startSector, sectorsCount, 0
        ));
    }
    if (!devicemapper::CreateDevice(dmDeviceName, dmTable, dmDevicePath)) {
        RECORD_INNER_ERROR("failed to create dm device, errno %u", errno);
        return false;
    }
    // keep checkpoint for devicemapper device creation
    SaveDmDeviceCreationRecord(m_outputDirPath, dmDeviceName);
    return true;
}

bool LinuxDeviceMapperMountProvider::RemoveDmDeviceIfExists(const std::string& dmDeviceName)
{
    if (!devicemapper::RemoveDeviceIfExists(dmDeviceName)) {
        RECORD_INNER_ERROR("failed to remove dm device %s, errno %u", dmDeviceName.c_str(), errno);
        return false;
    }
    RemoveDmDeviceCreationRecord(m_outputDirPath, dmDeviceName);
    return true;
}

bool LinuxDeviceMapperMountProvider::AttachReadOnlyLoopDevice(const std::string& filePath, std::string& loopDevicePath)
{
    if (!loopback::Attach(filePath, loopDevicePath, O_RDONLY)) {
        RECORD_INNER_ERROR("failed to attach read only loopback device from %s, errno %u", filePath.c_str(), errno);
        return false;
    }
    // keep checkpoint for loopback device creation
    SaveLoopDeviceCreationRecord(m_outputDirPath, loopDevicePath);
    return true;
}

bool LinuxDeviceMapperMountProvider::DetachLoopDeviceIfAttached(const std::string& loopDevicePath)
{
    if (!loopback::Attached(loopDevicePath)) {
        RemoveLoopDeviceCreationRecord(m_outputDirPath, loopDevicePath);
        return true;
    }
    if (!loopback::Detach(loopDevicePath)) {
        RECORD_INNER_ERROR("failed to detach loopback device %s, errno %u", loopDevicePath.c_str(), errno);
        return false;
    }
    RemoveLoopDeviceCreationRecord(m_outputDirPath, loopDevicePath);
    return true;
}

std::string LinuxDeviceMapperMountProvider::GenerateNewDmDeviceName() const
{
    namespace chrono = std::chrono;
    using clock = std::chrono::system_clock;
    auto timestamp = std::chrono::duration_cast<chrono::microseconds>(clock::now().time_since_epoch()).count();
    // name size must be limited in DM_NAME_LEN
    return std::string(DEVICE_MAPPER_DEVICE_NAME_PREFIX) + std::to_string(timestamp);
}

// load all files in cache dir
bool LinuxDeviceMapperMountProvider::ListRecordFiles(std::vector<std::string>& filelist)
{
    DIR* dir = ::opendir(m_outputDirPath.c_str());
    if (dir == nullptr) {
        RECORD_INNER_ERROR("error opening directory %s, errno %u", m_outputDirPath.c_str(), errno);
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




// implement LinuxDeviceMapperUmountProvider...

std::unique_ptr<LinuxDeviceMapperUmountProvider> LinuxDeviceMapperUmountProvider::Build(
    const std::string& mountRecordJsonFilePath, const std::string& outputDirPath)
{
    LinuxDeviceMapperCopyMountRecord mountRecord {};
    if (!util::JsonDeserialize(mountRecord, mountRecordJsonFilePath)) {
        ERRLOG("unabled to open copy mount record %s to read, errno %u", mountRecordJsonFilePath.c_str(), errno);
        return nullptr;
    };
    return std::make_unique<LinuxLoopbackUmountProvider>(
        outputDirPath, mountTargetPath, dmDeviceName, loopDevices);
}

LinuxDeviceMapperUmountProvider::LinuxDeviceMapperUmountProvider(
    const std::string& outputDirPath,
    const std::string& mountTargetPath,
    const std::string& dmDeviceName,
    const std::vector<std::string> loopDevices)
    : m_outputDirPath(outputDirPath),
    m_mountTargetPath(mountTargetPath),
    m_dmDeviceName(dmDeviceName),
    m_loopDevices(loopDevices)
{}

bool LinuxDeviceMapperUmountProvider::Umount()
{
    bool success = true; // if error occurs, make every effort to clear the mount
    // umount the device first
    if (fsapi::IsMountPoint(m_mountTargetPath) && ::umount2(m_mountTargetPath.c_str(), MNT_FORCE) != 0) {
        RECORD_INNER_ERROR("failed to umount target %s, errno %u", m_mountTargetPath.c_str(), errno);
        success = false;
    }
    // check if need to remove dm device
    if (!m_dmDeviceName.empty() && !devicemapper::RemoveDeviceIfExists(m_dmDeviceName)) {
        RECORD_INNER_ERROR("failed to remove devicemapper device %s, errno", m_dmDeviceName.c_str(), errno);
        success = false;
    }
    // finally detach all loopback devices involed
    for (const std::string& loopDevicePath: m_loopDevices) {
        if (loopback::Attached(loopDevicePath) && !loopback::Detach(loopDevicePath)) {
            RECORD_INNER_ERROR("failed to detach loopback device %s, errno %u", devicePath.c_str(), errno);
            RemoveLoopDeviceCreationRecord(m_outputDirPath, loopDevicePath);
            success = false;
        }
    }
    return success;
}



//#endif