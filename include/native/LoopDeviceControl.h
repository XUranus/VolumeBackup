#ifndef VOLUMEBACKUP_DM_LOOP_DEVICE_CONTROL_H
#define VOLUMEBACKUP_DM_LOOP_DEVICE_CONTROL_H

#include "VolumeProtectMacros.h"

namespace volumeprotect {
namespace loopback {

/*
 * manage loopback device list, creation and delete
 * reference url:
 * https://android.googlesource.com/platform/system/core/+/refs/heads/main/fs_mgr/libdm/include/libdm/loop_control.h
 */

VOLUMEPROTECT_API bool Attach(const std::string& filepath, std::string& loopDevicePath, uint32_t flag);
VOLUMEPROTECT_API bool Attach(int fileFd, std::string& loopDevicePath);

VOLUMEPROTECT_API bool Detach(const std::string& loopDevicePath);
VOLUMEPROTECT_API bool Detach(int loopFd);

VOLUMEPROTECT_API bool Attached(const std::string& loopDevicePath);

VOLUMEPROTECT_API bool GetFreeLoopDevice(std::string& loopDevicePath);

}
}

#endif