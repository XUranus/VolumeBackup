#ifndef LINUX_MOUNT_UTILS_HEADER
#define LINUX_MOUNT_UTILS_HEADER

#ifdef __linux__

#include <string>

namespace linuxmountutil {

bool Mount(
    const std::string& devicePath,
    const std::string& mountTargetPath,
    const std::string& fsType,
    const std::string& mountOptions,
    bool readOnly);

bool Umount(const std::string& mountTargetPath, bool force);

bool IsMountPoint(const std::string& dirPath);

// get the block device path of the mount point
std::string GetMountDevicePath(const std::string& mountTargetPath);

}

#endif
#endif