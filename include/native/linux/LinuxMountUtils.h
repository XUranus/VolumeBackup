#ifndef LINUX_MOUNT_UTILS_HEADER
#define LINUX_MOUNT_UTILS_HEADER

#ifdef __linux__

#include <string>

namespace volumeprotect {
namespace linuxmountutil {

// entry of /proc/mounts
struct MountEntry {
    std::string devicePath;
    std::string mountTargetPath;
    std::string type;
    std::string options;
};

// a single implement
bool Mount(
    const std::string& devicePath,
    const std::string& mountTargetPath,
    const std::string& fsType,
    const std::string& mountOptions,
    bool readOnly);

// a more complicate implement, automatically parse the options into flags
bool Mount2(
    const std::string& devicePath,
    const std::string& mountTargetPath,
    const std::string& fsType,
    const std::string& mountOptions,
    bool readOnly);

// umount one mount point
bool Umount(const std::string& mountTargetPath, bool force);

// umount all mount points releated to the device by force
bool UmountAll(const std::string& devicePath);

// return if the directory path is a mount point
bool IsMountPoint(const std::string& dirPath);

// get the block device path of the mount point
std::string GetMountDevicePath(const std::string& mountTargetPath);

// get all mount points of a device
std::vector<MountEntry> GetAllMounts(const std::string& devicePath);

}
}

#endif
#endif