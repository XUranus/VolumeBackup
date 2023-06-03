#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/fs.h>
#include <unistd.h>

#include "VolumeBackupUtils.h"

using namespace volumebackup;

std::runtime_error volumebackup::BuildRuntimeException(
    const std::string& message,
    const std::string& blockDevice,
    uint32_t errcode)
{
    std::string label;
    label += (message + ", device = " + blockDevice + ", errno " + std::to_string(errcode));
    return std::runtime_error(label);
}

uint64_t volumebackup::ReadVolumeSize(const std::string& blockDevice)
{
    int fd = ::open(blockDevice.c_str(), O_RDONLY);
    if (fd < 0) {
        throw BuildRuntimeException("Error opening block device", blockDevice, errno);
        return 0;
    }

    uint64_t size = 0;
    if (::ioctl(fd, BLKGETSIZE64, &size) < 0) {
        close(fd);
        throw BuildRuntimeException("rror getting block device size", blockDevice, errno);
        return 0;
    }

    ::close(fd);
    return size;
}