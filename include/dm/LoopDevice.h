#ifndef VOLUMEBACKUP_DM_LOOP_DEVICE_H

#include <memory>
#include <string>

#include <fcntl.h>

namespace volumebackup {
namespace dm {

/*
 * manage loopback device list, creation and delete
 * reference url:
 * https://android.googlesource.com/platform/system/core/+/refs/heads/main/fs_mgr/libdm/include/libdm/loop_control.h
 */
class LoopDevice {
public:
    static std::shared_ptr<LoopDevice> Attach(const std::string& filepath, uint32_t flag = O_RDONLY);
    static std::shared_ptr<LoopDevice> Attach(int fd);
    
    void Detach(); // prevent loopdevice being destructed after LoopDevice struct finalized

private:
    std::string     m_devicePath;                   // path of the loopback device
    int             m_bindFd        { -1 };         // file descriptor binded to the loop device
    bool            m_valid         { false };
};

}
}

#define VOLUMEBACKUP_DM_LOOP_DEVICE_H
#endif