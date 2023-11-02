/**
 * @copyright Copyright 2023 XUranus. All rights reserved.
 * @license This project is released under the Apache License.
 * @author XUranus(2257238649wdx@gmail.com)
 */

#ifdef __linux__

#include "native/linux/LoopDeviceControl.h"

#include <cstddef>
#include <fcntl.h>
#include <dirent.h>
#include <linux/loop.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <unistd.h>
#include <string>
#include <memory>

using namespace volumeprotect;

namespace {
    const std::string LOOP_DEVICE_CONTROL_PATH = "/dev/loop-control";
    const std::string LOOP_DEVICE_PREFIX = "/dev/loop"; // loopback device path /dev/loopX
    const std::string SYS_BLOCK_PATH = "/sys/block";
}

bool loopback::Attach(const std::string& filepath, std::string& loopDevicePath, uint32_t flag = O_RDONLY)
{
    return loopback::Attach(::open(filepath.c_str(), flag), loopDevicePath);
}

bool loopback::Attach(int fileFd, std::string& loopDevicePath)
{
    if (fileFd < 0) {
        return false;
    }
    if (!loopback::GetFreeLoopDevice(loopDevicePath)) {
        return false;
    }
    int loopFd = ::open(loopDevicePath.c_str(), O_RDWR | O_CLOEXEC);
    if (loopFd < 0) {
        return false;
    }
    int rc = ::ioctl(loopFd, LOOP_SET_FD, fileFd);
    if (rc != 0) {
        return false;
    }
    return true;
}

bool loopback::GetFreeLoopDevice(std::string& loopDevicePath)
{
    int controlFd = ::open(LOOP_DEVICE_CONTROL_PATH.c_str(), O_RDWR | O_CLOEXEC);
    if (controlFd < 0) {
        // invalid control fd
        return false;
    }
    int rc = ::ioctl(controlFd, LOOP_CTL_GET_FREE);
    if (rc < 0) {
        return false;
    }
    loopDevicePath = LOOP_DEVICE_PREFIX + std::to_string(rc);
    return true;
}

bool loopback::Detach(const std::string& loopDevicePath)
{
    int loopFd = ::open(loopDevicePath.c_str(), O_RDWR | O_CLOEXEC);
    return loopback::Detach(loopFd);
}

bool loopback::Detach(int loopFd)
{
    if (loopFd < 0) {
        return false;
    }
    int rc = ::ioctl(loopFd, LOOP_CLR_FD, 0);
    if (rc != 0) {
        return false;
    }
    return true;
}

bool loopback::Attached(const std::string& loopDevicePath)
{
    std::string loopDeviceName = loopDevicePath;
    if (loopDeviceName.find("/dev/") == 0) {
        loopDeviceName = loopDeviceName.substr(std::string("/dev/").length());
    }
    struct dirent *entry = nullptr;
    DIR *dir = ::opendir(SYS_BLOCK_PATH.c_str());
    if (dir == nullptr) {
        return false;
    }
    while ((entry = ::readdir(dir)) != nullptr) {
        if (std::string(entry->d_name) == loopDeviceName) {
            ::closedir(dir);
            return true;
        }
    }
    ::closedir(dir);
    return false;
}

#endif