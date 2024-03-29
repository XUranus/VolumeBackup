/**
 * @file LinuxMountAPI.cpp
 * Code referenced from
 * https://android.googlesource.com/platform/system/core/+/jb-mr1.1-dev-plus-aosp/toolbox/mount.c
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

#include "common/VolumeProtectMacros.h"
#include "Logger.h"
#include "native/linux/LinuxMountUtils.h"

namespace {
    const int MNTENT_BUFFER_MAX = 4096;
    const std::string SYS_MOUNTS_ENTRY_PATH = "/proc/mounts";
    const int MAX_MOUNT_RETRY = 3;
}

using namespace volumeprotect;
using namespace volumeprotect::linuxmountutil;

struct MountOptionMapEntry {
    std::string option;
    unsigned long rwmask;
    unsigned long rwset;
    unsigned long rwnoset;
};

/* War is peace. Verbosity is silence.*/
#ifndef MS_VERBOSE
#define MS_VERBOSE 32768
#endif

/*
 * These options define the function of "mount(2)".
 */
#ifndef MS_TYPE
#define MS_TYPE	(MS_REMOUNT | MS_BIND | MS_MOVE)
#endif

static const std::vector<MountOptionMapEntry> g_MountOptionMapEntries = {
    /* name         mask            set             noset       */
    { "async",      MS_SYNCHRONOUS, 0,              MS_SYNCHRONOUS  },
    { "atime",      MS_NOATIME,     0,              MS_NOATIME      },
    { "bind",       MS_TYPE,        MS_BIND,        0,              },
    { "dev",        MS_NODEV,       0,              MS_NODEV        },
    { "diratime",   MS_NODIRATIME,  0,              MS_NODIRATIME   },
    { "dirsync",    MS_DIRSYNC,     MS_DIRSYNC,     0               },
    { "exec",       MS_NOEXEC,      0,              MS_NOEXEC       },
    { "move",       MS_TYPE,        MS_MOVE,        0               },
    { "recurse",    MS_REC,         MS_REC,         0               },
    { "rec",        MS_REC,         MS_REC,         0               },
    { "remount",    MS_TYPE,        MS_REMOUNT,     0               },
    { "ro",         MS_RDONLY,      MS_RDONLY,      0               },
    { "rw",         MS_RDONLY,      0,              MS_RDONLY       },
    { "suid",       MS_NOSUID,      0,              MS_NOSUID       },
    { "sync",       MS_SYNCHRONOUS, MS_SYNCHRONOUS,	0               },
    { "verbose",    MS_VERBOSE,     MS_VERBOSE,     0               },
    { "unbindable",	MS_UNBINDABLE,  MS_UNBINDABLE,  0               },
    { "private",    MS_PRIVATE,     MS_PRIVATE,     0               },
    { "slave",      MS_SLAVE,       MS_SLAVE,       0               },
    { "shared",     MS_SHARED,      MS_SHARED,      0               },
};

static std::vector<std::string> StringSplit(const std::string& str, const char* delim)
{
    std::vector<std::string> tokens;
    char* newstr = ::strdup(str.c_str());
    char* s = nullptr;
    while ((s = ::strsep(&newstr, delim)) != nullptr) {
        tokens.emplace_back(s);
    }
    ::free(newstr);
    return tokens;
}

static std::string StringJoin(const std::vector<std::string>& strs, const std::string& delim)
{
    std::string res;
    for (const std::string& str : strs) {
        res += str + delim;
    }
    if (!res.empty()) {
        return res.substr(0, res.size() - delim.size());
    }
    return res;
}

static void ParseMountOptions(const std::string& mountOptions, uint64_t& mountFlags, std::string& finalMountOptions)
{
    std::vector<std::string> options = StringSplit(mountOptions, ",");
    std::vector<std::string> fsOptions; // options to deliver to fs driver
    for (const std::string& option : options) {
        bool no = (option.size() > 2 && option.find("no") == 0); // if option starts with 'no' ?
        bool matched = false;
        if (option.find("loop=") == 0 || option == "loop") {
            // option specified loop device using 'loop=/dev/loopX' or request alloc a random loop device using 'loop'
            // skip such option due to unsupported
            continue;
        }
        for (const MountOptionMapEntry& entry : g_MountOptionMapEntries) {
            if (entry.option == option) {
                mountFlags &= ~entry.rwmask;
                mountFlags |= entry.rwset;
                matched = true;
            } else if (no && entry.option == option.substr(2)) {
                mountFlags &= ~entry.rwmask;
                mountFlags |= entry.rwnoset;
                matched = true;
            }
        }
        if (!matched) {
            fsOptions.emplace_back(option);
        }
    }
    finalMountOptions = StringJoin(fsOptions, ",");
}

bool linuxmountutil::Mount2(
    const std::string& devicePath,
    const std::string& mountTargetPath,
    const std::string& fsType,
    const std::string& mountOptions,
    bool readOnly)
{
    unsigned long mountFlags = MS_VERBOSE;
    if (readOnly) {
        mountFlags |= MS_RDONLY;
    }
    std::string finalMountOptions;
    ParseMountOptions(mountOptions, mountFlags, finalMountOptions);

    /*
	 * If remount, bind or move was specified, then we don't
	 * have a "type" as such.  Use the dummy "none" type.
	 */
    const char* type = fsType.c_str();
    if (fsType.empty() || (mountFlags & MS_TYPE)) {
        type = "none";
    }

    int retry = 0;
    int ret = -1;
    while ((ret = ::mount(
        devicePath.c_str(),
        mountTargetPath.c_str(),
        fsType.c_str(),
        mountFlags,
        finalMountOptions.empty() ? nullptr : finalMountOptions.c_str())) != 0 && retry < MAX_MOUNT_RETRY) {
        retry++;
        ERRLOG("mount failed with errno %d, flags %llu, retry %d",
            errno, mountFlags, retry);
        // filesystem is not found, or the superblock is invalid
        if (errno == ENODEV || errno == EINVAL) {
            break;
        }
        // If got EACCES when trying to mount readwrite and this isn't a remount, try read only
        if (errno == EACCES && (mountFlags & (MS_REMOUNT | MS_RDONLY)) == 0) {
            WARNLOG("mount failed with EACCESS, try mount read-only");
            mountFlags |= MS_RDONLY;
        }
    }
    if (ret != 0) {
        ERRLOG("mount failed with errno %d, device %s, target %s, type %s, flags %llu, options %s, final options %s",
            errno, devicePath.c_str(), mountTargetPath.c_str(), fsType.c_str(), mountFlags,
            mountOptions.c_str(), finalMountOptions.c_str());
        return false;
    }
    return true;
}

bool linuxmountutil::Mount(
    const std::string& devicePath,
    const std::string& mountTargetPath,
    const std::string& fsType,
    const std::string& mountOptions,
    bool readOnly)
{
    unsigned long mountFlags = MS_VERBOSE;
    if (readOnly) {
        mountFlags |= MS_RDONLY; // TODO:: check mountOptions contains ro
    }
    if (::mount(devicePath.c_str(),
        mountTargetPath.c_str(),
        fsType.c_str(),
        mountFlags,
        mountOptions.c_str()) != 0) {
        return false;
    }
    return true;
}

bool linuxmountutil::Umount(const std::string& mountTargetPath, bool force)
{
    int flags = force ? (MNT_FORCE | MNT_DETACH) : 0;
    if (linuxmountutil::IsMountPoint(mountTargetPath) &&
        ::umount2(mountTargetPath.c_str(), flags) != 0) {
        return false;
    }
    return true;
}

bool linuxmountutil::IsMountPoint(const std::string& dirPath)
{
    bool mounted = false;
    FILE* mountsFile = ::setmntent(SYS_MOUNTS_ENTRY_PATH.c_str(), "r");
    if (mountsFile == nullptr) {
        ERRLOG("failed to open /proc/mounts, errno %u", errno);
        return false;
    }
    struct mntent entry {};
    char mntentBuffer[MNTENT_BUFFER_MAX] = { 0 };
    while (::getmntent_r(mountsFile, &entry, mntentBuffer, MNTENT_BUFFER_MAX) != nullptr) {
        if (std::string(entry.mnt_dir) == dirPath) {
            mounted = true;
            break;
        }
    }
    ::endmntent(mountsFile);
    return mounted;
}

std::string linuxmountutil::GetMountDevicePath(const std::string& mountTargetPath)
{
    std::string devicePath;
    FILE* mountsFile = ::setmntent(SYS_MOUNTS_ENTRY_PATH.c_str(), "r");
    if (mountsFile == nullptr) {
        ERRLOG("failed to open /proc/mounts, errno %u", errno);
        return "";
    }
    struct mntent entry {};
    char mntentBuffer[MNTENT_BUFFER_MAX] = { 0 };
    while (::getmntent_r(mountsFile, &entry, mntentBuffer, MNTENT_BUFFER_MAX) != nullptr) {
        if (std::string(entry.mnt_dir) == mountTargetPath) {
            devicePath = entry.mnt_fsname;
            break;
        }
    }
    ::endmntent(mountsFile);
    return devicePath;
}

#endif