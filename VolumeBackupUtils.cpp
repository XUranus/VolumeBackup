#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/fs.h>
#include <unistd.h>
#include <vector>

extern "C" {
    #include <blkid/blkid.h>
    #include <parted/parted.h>
}

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

uint32_t volumebackup::ProcessorsNum()
{
    return sysconf(_SC_NPROCESSORS_ONLN);
}

static std::string ReadPosixBlockDeviceAttribute(const std::string& blockDevicePath, const std::string& attributeName)
{
    const char* devname = blockDevicePath.c_str();
    const char* attribute = nullptr;
    blkid_probe pr = blkid_new_probe_from_filename(devname);
    if (!pr) {
        // Failed to create a new libblkid probe
        return "";
    }

    blkid_do_probe(pr);
    
    if (blkid_probe_lookup_value(pr, attributeName.c_str(), &attribute, nullptr) < 0) {
        // Failed to look up the UUID of the device
        blkid_free_probe(pr);
        return "";
    }
    std::string attributeValueString = attribute;
    blkid_free_probe(pr);
    return attributeValueString;
}

std::string volumebackup::ReadVolumeUUID(const std::string& blockDevicePath)
{
    return ReadPosixBlockDeviceAttribute(blockDevicePath, "UUID");
}

std::string volumebackup::ReadVolumeType(const std::string& blockDevicePath)
{
    return ReadPosixBlockDeviceAttribute(blockDevicePath, "TYPE");
}

std::string volumebackup::ReadVolumeLabel(const std::string& blockDevicePath)
{
    return ReadPosixBlockDeviceAttribute(blockDevicePath, "LABEL");
}

std::vector<VolumePartitionTableEntry> volumebackup::ReadVolumePartitionTable(const std::string& blockDevicePath)
{
    PedDevice* dev = nullptr;
    PedDisk* disk = nullptr;
    PedPartition* part = nullptr;

    // Initialize libparted and disable exception handling (we'll do it ourselves)
    ped_exception_fetch_all();
    ped_device_probe_all();

    std::vector<VolumePartitionTableEntry> partitionTable;

    // Get device
    dev = ped_device_get(blockDevicePath.c_str());
    if (dev == nullptr) {
        // Failed to get device
        ped_exception_leave_all();
        return partitionTable;
    }

    // Get disk (partition table)
    disk = ped_disk_new(dev);
    if (disk == nullptr) {
        // Failed to read partition table
        ped_device_destroy(dev);
        ped_exception_leave_all();
        return partitionTable;
    }

    // Loop over all partitions
    for (part = ped_disk_next_partition(disk, nullptr);
        part;
        part = ped_disk_next_partition(disk, part)) {
        if (part->num < 0) {
            continue;  // Skip special partitions (like free space and extended partitions)
        }

        VolumePartitionTableEntry entry;
        entry.patitionNumber = part->num;
        entry.firstSector = part->geom.start;
        entry.lastSector = part->geom.end;
        entry.totalSectors = part->geom.length;

        if (part->fs_type != nullptr) {
            entry.filesystem = part->fs_type->name;
        }
        partitionTable.emplace_back(entry);
    }

    // Cleanup
    ped_disk_destroy(disk);
    ped_device_destroy(dev);
    ped_exception_leave_all();
    return partitionTable;
}
