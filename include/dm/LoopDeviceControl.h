#ifndef VOLUMEBACKUP_DM_LOOP_DEVICE_CONTROL_H
#define VOLUMEBACKUP_DM_LOOP_DEVICE_CONTROL_H

#include <string>
#include <cstdint>

namespace volumebackup {
namespace loopback {

/*
 * manage loopback device list, creation and delete
 * reference url:
 * https://android.googlesource.com/platform/system/core/+/refs/heads/main/fs_mgr/libdm/include/libdm/loop_control.h
 */

bool Attach(const std::string& filepath, std::string& loopDevicePath, uint32_t flag);
bool Attach(int fileFd, std::string& loopDevicePath);

bool Detach(const std::string& loopDevicePath);
bool Detach(int loopFd);

bool GetFreeLoopDevice(std::string& loopDevicePath);

}
}

#endif