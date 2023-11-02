/**
 * @file LoopDeviceControl.h
 * @brief Linux loopback device utilites wrapper.
 * @copyright Copyright 2023 XUranus. All rights reserved.
 * @license This project is released under the Apache License.
 * @author XUranus(2257238649wdx@gmail.com)
 */

#ifndef VOLUMEBACKUP_DM_LOOP_DEVICE_CONTROL_H
#define VOLUMEBACKUP_DM_LOOP_DEVICE_CONTROL_H

#ifdef __linux__

#include <string>
#include <cstdint>

namespace volumeprotect {
/*
 * @brief Manage loopback device list, creation and delete. Code referenced from AOSP
 * @link https://android.googlesource.com/platform/system/core/+/refs/heads/main/fs_mgr/libdm/include/libdm/loop_control.h
 */
namespace loopback {

bool Attach(const std::string& filepath, std::string& loopDevicePath, uint32_t flag);
bool Attach(int fileFd, std::string& loopDevicePath);

bool Detach(const std::string& loopDevicePath);
bool Detach(int loopFd);

bool Attached(const std::string& loopDevicePath);

bool GetFreeLoopDevice(std::string& loopDevicePath);

}
}

#endif
#endif