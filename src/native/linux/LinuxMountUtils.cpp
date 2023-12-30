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
}

// #include <sys/mount.h>
// #include <sys/stat.h>
// #include <fcntl.h>
// #include <errno.h>
// #include <stdio.h>
// #include <stdlib.h>
// #include <string.h>
// #include <unistd.h>
// #include <linux/loop.h>

// #define ARRAY_SIZE(x)	(sizeof(x) / sizeof(x[0]))
// #define DEFAULT_LOOP_DEVICE "/dev/block/loop0"
// #define LOOPDEV_MAXLEN 64
// struct mount_opts {
// 	const char str[16];
// 	unsigned long rwmask;
// 	unsigned long rwset;
// 	unsigned long rwnoset;
// };
// struct extra_opts {
// 	char *str;
// 	char *end;
// 	int used_size;
// 	int alloc_size;
// };
// /*
//  * These options define the function of "mount(2)".
//  */
// #define MS_TYPE	(MS_REMOUNT|MS_BIND|MS_MOVE)
// static const struct mount_opts options[] = {
// 	/* name		mask		set		noset		*/
// 	{ "async",	MS_SYNCHRONOUS,	0,		MS_SYNCHRONOUS	},
// 	{ "atime",	MS_NOATIME,	0,		MS_NOATIME	},
// 	{ "bind",	MS_TYPE,	MS_BIND,	0,		},
// 	{ "dev",	MS_NODEV,	0,		MS_NODEV	},
// 	{ "diratime",	MS_NODIRATIME,	0,		MS_NODIRATIME	},
// 	{ "dirsync",	MS_DIRSYNC,	MS_DIRSYNC,	0		},
// 	{ "exec",	MS_NOEXEC,	0,		MS_NOEXEC	},
// 	{ "move",	MS_TYPE,	MS_MOVE,	0		},
// 	{ "recurse",	MS_REC,		MS_REC,		0		},
// 	{ "rec",	MS_REC,		MS_REC,		0		},
// 	{ "remount",	MS_TYPE,	MS_REMOUNT,	0		},
// 	{ "ro",		MS_RDONLY,	MS_RDONLY,	0		},
// 	{ "rw",		MS_RDONLY,	0,		MS_RDONLY	},
// 	{ "suid",	MS_NOSUID,	0,		MS_NOSUID	},
// 	{ "sync",	MS_SYNCHRONOUS,	MS_SYNCHRONOUS,	0		},
// 	{ "verbose",	MS_VERBOSE,	MS_VERBOSE,	0		},
// 	{ "unbindable",	MS_UNBINDABLE,	MS_UNBINDABLE,	0		},
// 	{ "private",	MS_PRIVATE,	MS_PRIVATE,	0		},
// 	{ "slave",	MS_SLAVE,	MS_SLAVE,	0		},
// 	{ "shared",	MS_SHARED,	MS_SHARED,	0		},
// };

// static void add_extra_option(struct extra_opts *extra, char *s)
// {
// 	int len = strlen(s);
// 	int newlen;
// 	if (extra->str)
// 	       len++;			/* +1 for ',' */
// 	newlen = extra->used_size + len;
// 	if (newlen >= extra->alloc_size) {
// 		char *new;
// 		new = realloc(extra->str, newlen + 1);	/* +1 for NUL */
// 		if (!new)
// 			return;
// 		extra->str = new;
// 		extra->end = extra->str + extra->used_size;
// 		extra->alloc_size = newlen + 1;
// 	}
// 	if (extra->used_size) {
// 		*extra->end = ',';
// 		extra->end++;
// 	}
// 	strcpy(extra->end, s);
// 	extra->used_size += len;
// }
// static unsigned long
// parse_mount_options(char *arg, unsigned long rwflag, struct extra_opts *extra, int* loop, char *loopdev)
// {
// 	char *s;
    
//     *loop = 0;
// 	while ((s = strsep(&arg, ",")) != NULL) {
// 		char *opt = s;
// 		unsigned int i;
// 		int res, no = s[0] == 'n' && s[1] == 'o';
// 		if (no)
// 			s += 2;
//         if (strncmp(s, "loop=", 5) == 0) {
//             *loop = 1;
//             strlcpy(loopdev, s + 5, LOOPDEV_MAXLEN);
//             continue;
//         }
//         if (strcmp(s, "loop") == 0) {
//             *loop = 1;
//             strlcpy(loopdev, DEFAULT_LOOP_DEVICE, LOOPDEV_MAXLEN);
//             continue;
//         }
// 		for (i = 0, res = 1; i < ARRAY_SIZE(options); i++) {
// 			res = strcmp(s, options[i].str);
// 			if (res == 0) {
// 				rwflag &= ~options[i].rwmask;
// 				if (no)
// 					rwflag |= options[i].rwnoset;
// 				else
// 					rwflag |= options[i].rwset;
// 			}
// 			if (res <= 0)
// 				break;
// 		}
// 		if (res != 0 && s[0])
// 			add_extra_option(extra, opt);
// 	}
// 	return rwflag;
// }
// static char *progname;
// static struct extra_opts extra;
// static unsigned long rwflag;
// static int
// do_mount(char *dev, char *dir, char *type, unsigned long rwflag, void *data, int loop,
//          char *loopdev)
// {
// 	char *s;
// 	int error = 0;
//     if (loop) {
//         int file_fd, device_fd;
//         int flags;
//         flags = (rwflag & MS_RDONLY) ? O_RDONLY : O_RDWR;
        
//         file_fd = open(dev, flags);
//         if (file_fd < 0) {
//             perror("open backing file failed");
//             return 1;
//         }
//         device_fd = open(loopdev, flags);
//         if (device_fd < 0) {
//             perror("open loop device failed");
//             close(file_fd);
//             return 1;
//         }
//         if (ioctl(device_fd, LOOP_SET_FD, file_fd) < 0) {
//             perror("ioctl LOOP_SET_FD failed");
//             close(file_fd);
//             close(device_fd);
//             return 1;
//         }
//         close(file_fd);
//         close(device_fd);
//         dev = loopdev;
//     }
// 	while ((s = strsep(&type, ",")) != NULL) {
// retry:
// 		if (mount(dev, dir, s, rwflag, data) == -1) {
// 			error = errno;
// 			/*
// 			 * If the filesystem is not found, or the
// 			 * superblock is invalid, try the next.
// 			 */
// 			if (error == ENODEV || error == EINVAL)
// 				continue;
// 			/*
// 			 * If we get EACCESS, and we're trying to
// 			 * mount readwrite and this isn't a remount,
// 			 * try read only.
// 			 */
// 			if (error == EACCES &&
// 			    (rwflag & (MS_REMOUNT|MS_RDONLY)) == 0) {
// 				rwflag |= MS_RDONLY;
// 				goto retry;
// 			}
// 			break;
// 		}
// 	}
// 	if (error) {
// 		errno = error;
// 		perror("mount");
// 		return 255;
// 	}
// 	return 0;
// }

// static int get_mounts_dev_dir(const char *arg, char **dev, char **dir)
// {
// 	FILE *f;
// 	char mount_dev[256];
// 	char mount_dir[256];
// 	char mount_type[256];
// 	char mount_opts[256];
// 	int mount_freq;
// 	int mount_passno;
// 	int match;
// 	f = fopen("/proc/mounts", "r");
// 	if (!f) {
// 		fprintf(stdout, "could not open /proc/mounts\n");
// 		return -1;
// 	}
// 	do {
// 		match = fscanf(f, "%255s %255s %255s %255s %d %d\n",
// 					   mount_dev, mount_dir, mount_type,
// 					   mount_opts, &mount_freq, &mount_passno);
// 		mount_dev[255] = 0;
// 		mount_dir[255] = 0;
// 		mount_type[255] = 0;
// 		mount_opts[255] = 0;
// 		if (match == 6 &&
// 			(strcmp(arg, mount_dev) == 0 ||
// 			 strcmp(arg, mount_dir) == 0)) {
// 			*dev = strdup(mount_dev);
// 			*dir = strdup(mount_dir);
// 			fclose(f);
// 			return 0;
// 		}
// 	} while (match != EOF);
// 	fclose(f);
// 	return -1;
// }


// int Mount(
//     const std::string& devicePath,
//     const std::string& mountTargetPath,
//     const std::string& mountType,
//     const std::string& mountOptions,
//     bool readOnly)
// {
//     int rwflag = MS_VERBOSE;
//     if (readOnly) {
//         rwflag |= MS_RDONLY;
//     }

//     /*
// 	 * If remount, bind or move was specified, then we don't
// 	 * have a "type" as such.  Use the dummy "none" type.
// 	 */
//     const char* type = mountType.empty() ? "none" : mountType.c_str();
// 	if (rwflag & MS_TYPE) {
// 		type = "none";
//     }

//     if (!LinuxApiMount(devicePath, mountTargetPath, mountType, mountOptions)) {
//         return CommandMount(devicePath, mountTargetPath, mountType, mountOptions);
//     }
// }

// {}
// 	char *type = NULL;
// 	char *dev = NULL;
// 	char *dir = NULL;
// 	int c;
// 	int loop = 0;
// 	char loopdev[LOOPDEV_MAXLEN];
// 	progname = argv[0];
// 	rwflag = MS_VERBOSE;
	

// 		case 'o':
// 			rwflag = parse_mount_options(optarg, rwflag, &extra, &loop, loopdev);
// 			break;
// 		case 'w':
// 			rwflag &= ~MS_RDONLY;
// 			break;


// 	if (optind + 2 == argc) {
// 		dev = argv[optind];
// 		dir = argv[optind + 1];
// 	} else if (optind + 1 == argc && rwflag & MS_REMOUNT) {
// 		get_mounts_dev_dir(argv[optind], &dev, &dir);
// 	}

// 	return do_mount(dev, dir, type, rwflag, extra.str, loop, loopdev);
// 	/* We leak dev and dir in some cases, but we're about to exit */
// }


bool linuxmountutil::Mount(
    const std::string& devicePath,
    const std::string& mountTargetPath,
    const std::string& fsType,
    const std::string& mountOptions,
    bool readOnly)
{
    unsigned long mountFlags = readOnly ? MS_RDONLY : 0; // TODO:: check mountOptions contains ro
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