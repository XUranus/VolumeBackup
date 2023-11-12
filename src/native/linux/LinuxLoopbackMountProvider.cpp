/**
 * @copyright Copyright 2023 XUranus. All rights reserved.
 * @license This project is released under the Apache License.
 * @author XUranus(2257238649wdx@gmail.com)
 */

#ifdef __linux__

#include <cerrno>
#include <fcntl.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <mntent.h>
#include <dirent.h>
#include <stdlib.h>
#include <unistd.h>

#include "native/FileSystemAPI.h"
#include "native/linux/LoopDeviceControl.h"
#include "VolumeCopyMountProvider.h"
#include "Logger.h"
#include "common/VolumeUtils.h"
#include "native/linux/LinuxLoopbackMountProvider.h"

using namespace volumeprotect;
using namespace volumeprotect::common;
using namespace volumeprotect::mount;
using namespace volumeprotect::fsapi;

namespace {
    const std::string IMAGE_COPY_MOUNT_RECORD_FILE_SUFFIX = ".image.mount.record.json";
    const std::string LOOPBACK_DEVICE_PATH_PREFIX = "/dev/loop";
    const std::string LOOPBACK_DEVICE_CREATION_RECORD_SUFFIX = ".loop.record";
}

// serialize to $copyName.image.mount.record.json
struct LinuxImageCopyMountRecord {
    int             copyFormat;
    std::string     loopbackDevicePath;
    std::string     mountTargetPath;
    std::string     mountFsType;
    std::string     mountOptions;

    SERIALIZE_SECTION_BEGIN
    SERIALIZE_FIELD(copyFormat, copyFormat);
    SERIALIZE_FIELD(loopbackDevicePath, loopbackDevicePath);
    SERIALIZE_FIELD(mountTargetPath, mountTargetPath);
    SERIALIZE_FIELD(mountFsType, mountFsType);
    SERIALIZE_FIELD(mountOptions, mountOptions);
    SERIALIZE_SECTION_END
};

std::unique_ptr<LinuxLoopbackMountProvider> LinuxLoopbackMountProvider::Build(
    const VolumeCopyMountConfig& volumeCopyMountConfig,
    const VolumeCopyMeta& volumeCopyMeta)
{
    LinuxLoopbackMountProviderParams params {};
    params.outputDirPath = volumeCopyMountConfig.outputDirPath;
    params.copyName = volumeCopyMeta.copyName;
    if (volumeCopyMeta.segments.empty()) {
        ERRLOG("illegal volume copy meta, image file segments list empty");
        return nullptr;
    }
    params.imageFilePath = common::PathJoin(
        volumeCopyMountConfig.copyDataDirPath,
        volumeCopyMeta.segments.front().copyDataFile);
    params.mountTargetPath = volumeCopyMountConfig.mountTargetPath;
    params.mountFsType = volumeCopyMountConfig.mountFsType;
    params.mountOptions = volumeCopyMountConfig.mountOptions;
    return exstd::make_unique<LinuxLoopbackMountProvider>(params);
}

LinuxLoopbackMountProvider::LinuxLoopbackMountProvider(const LinuxLoopbackMountProviderParams& params)
    : m_outputDirPath(params.outputDirPath),
    m_copyName(params.copyName),
    m_imageFilePath(params.imageFilePath),
    m_mountTargetPath(params.mountTargetPath),
    m_mountFsType(params.mountFsType),
    m_mountOptions(params.mountOptions)
{}

std::string LinuxLoopbackMountProvider::GetMountRecordPath() const
{
    return common::PathJoin(m_outputDirPath, m_copyName + IMAGE_COPY_MOUNT_RECORD_FILE_SUFFIX);
}

// mount using *nix loopback device
bool LinuxLoopbackMountProvider::Mount()
{
    // 1. attach loopback device
    std::string loopDevicePath;
    if (!loopback::Attach(m_imageFilePath, loopDevicePath, O_RDONLY)) {
        RECORD_INNER_ERROR("failed to attach read only loopback device from %s, errno %u",
            m_imageFilePath.c_str(), errno);
        return false;
    }
    // keep checkpoint for loopback device creation
    std::string loopDeviceNumber = loopDevicePath.substr(LOOPBACK_DEVICE_PATH_PREFIX.length());
    std::string loopbackDeviceCheckpointName = loopDeviceNumber + LOOPBACK_DEVICE_CREATION_RECORD_SUFFIX;
    if (!fsapi::CreateEmptyFile(m_outputDirPath, loopbackDeviceCheckpointName)) {
    	RECORD_INNER_ERROR("failed to create checkpoint file %s", loopbackDeviceCheckpointName.c_str());
    }

    // 2. mount block device as readonly
    unsigned long mountFlags = MS_RDONLY;
    if (::mount(loopDevicePath.c_str(), m_mountTargetPath.c_str(), m_mountFsType.c_str(),
        mountFlags, m_mountOptions.c_str()) != 0) {
        RECORD_INNER_ERROR("mount %s to %s failed, type %s, option %s, errno %u",
            loopDevicePath.c_str(), m_mountTargetPath.c_str(), m_mountFsType.c_str(), m_mountOptions.c_str(), errno);
        PosixLoopbackMountRollback(loopDevicePath);
        return false;
    }

    // 3. save mount record to output directory
    LinuxImageCopyMountRecord mountRecord {};
    mountRecord.copyFormat = static_cast<int>(CopyFormat::IMAGE);
    mountRecord.loopbackDevicePath = loopDevicePath;
    mountRecord.mountTargetPath = m_mountTargetPath;
    mountRecord.mountFsType = m_mountFsType;
    mountRecord.mountOptions = m_mountOptions;
    std::string filepath = GetMountRecordPath();
    if (!common::JsonSerialize(mountRecord, filepath)) {
        RECORD_INNER_ERROR("failed to save image copy mount record to %s, errno %u", filepath.c_str(), errno);
        PosixLoopbackMountRollback(loopDevicePath);
        return false;
    }
    return true;
}

bool LinuxLoopbackMountProvider::PosixLoopbackMountRollback(const std::string& loopbackDevicePath)
{
	if (loopbackDevicePath.empty()) {
		// no loopback device attached, no mounts, return directly
		return true;
    }
    if (fsapi::IsMountPoint(m_mountTargetPath) && fsapi::GetMountDevicePath(m_mountTargetPath) != loopbackDevicePath) {
    	// moint point used by other application, return directly
    	return true;
    }
    LinuxLoopbackUmountProvider umountProvider(m_outputDirPath, m_mountTargetPath, loopbackDevicePath);
    if (!umountProvider.Umount()) {
    	RECORD_INNER_ERROR("failed to clear loopback mount residue");
    	return false;
    }
    return true;
}

std::unique_ptr<LinuxLoopbackUmountProvider> LinuxLoopbackUmountProvider::Build(
    const std::string& mountRecordJsonFilePath,
    const std::string& outputDirPath)
{
    LinuxImageCopyMountRecord mountRecord {};
    if (!common::JsonDeserialize(mountRecord, mountRecordJsonFilePath)) {
        ERRLOG("unabled to open copy mount record %s to read, errno %u",
            mountRecordJsonFilePath.c_str(), errno);
        return nullptr;
    };
    return exstd::make_unique<LinuxLoopbackUmountProvider>(
        outputDirPath,
        mountRecord.mountTargetPath,
        mountRecord.loopbackDevicePath);
}

LinuxLoopbackUmountProvider::LinuxLoopbackUmountProvider(
    const std::string& outputDirPath, const std::string& mountTargetPath, const std::string& loopbackDevicePath)
    : m_outputDirPath(outputDirPath), m_mountTargetPath(mountTargetPath), m_loopbackDevicePath(loopbackDevicePath)
{}

bool LinuxLoopbackUmountProvider::Umount()
{
    // 1. umount filesystem
    if (!m_mountTargetPath.empty()
        && fsapi::IsMountPoint(m_mountTargetPath)
        && ::umount2(m_mountTargetPath.c_str(), MNT_FORCE) != 0) {
        RECORD_INNER_ERROR("failed to umount target %s, errno %u", m_mountTargetPath.c_str(), errno);
        return false;
    }
    // 2. detach loopback device
    if (!m_loopbackDevicePath.empty()
        && loopback::Attached(m_loopbackDevicePath)
        && !loopback::Detach(m_loopbackDevicePath)) {
        RECORD_INNER_ERROR("failed to detach loopback device %s, errno %u", m_loopbackDevicePath.c_str(), errno);
        return false;
    }
    if (m_loopbackDevicePath.find(LOOPBACK_DEVICE_PATH_PREFIX) == 0) {
        std::string loopDeviceNumber = m_loopbackDevicePath.substr(LOOPBACK_DEVICE_PATH_PREFIX.length());
        if (!fsapi::RemoveFile(m_outputDirPath, loopDeviceNumber + LOOPBACK_DEVICE_CREATION_RECORD_SUFFIX)) {
        	ERRLOG("failed to remove loopback record checkpoint");
        }
    }
    return true;
}

#endif