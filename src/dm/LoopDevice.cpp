#include "LoopDevice.h"

#include <cstddef>
#include <fcntl.h>
#include <linux/loop.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <unistd.h>

#include <string>
#include <memory>

using namespace volumebackup::dm;


int GetControlFd()
{
    const static std::string LOOP_DEVICE_CONTROL_PATH = "/dev/loop-control";
    int controlFd = ::open(LOOP_DEVICE_CONTROL_PATH.c_str(), O_RDWR | O_CLOEXEC);
    return controlFd;
}

std::shared_ptr<LoopDevice> LoopDevice::Attach(const std::string& filepath, uint32_t flag)
{
    return Attach(::open(filepath.c_str(), flag));
}

std::shared_ptr<LoopDevice> LoopDevice::Attach(int fd)
{
    if (fd < 0) {
        return nullptr;
    }
    int controlFd = GetControlFd();
    if (controlFd < 0) {
        return nullptr;
    }
    if (::ioctl(controlFd, LOOP_CTL_GET_FREE) < 0) {
        
        return nullptr;
    }
    return nullptr;
}

void LoopDevice::Detach()
{
    if (!m_valid) {
        return;
    }
    m_valid = false;
    m_bindFd = -1;
    m_devicePath.clear();
}