#include <fcntl.h>
#include <fstream>
#include <sys/ioctl.h>
#include <linux/fs.h>
#include <unistd.h>
#include <vector>
#include <string>

#include <filesystem>

extern "C" {
    #include <blkid/blkid.h>
    #include <parted/parted.h>
}

#include "VolumeBackupUtils.h"

namespace {
#ifdef WIN32
    constexpr auto SEPARATOR = "\\";
#else
    constexpr auto SEPARATOR = "/";
#endif
}

using namespace volumebackup;

std::runtime_error volumebackup::util::BuildRuntimeException(
    const std::string& message,
    const std::string& blockDevice,
    uint32_t errcode)
{
    std::string label;
    label += (message + ", device = " + blockDevice + ", errno " + std::to_string(errcode));
    return std::runtime_error(label);
}

uint64_t volumebackup::util::ReadVolumeSize(const std::string& blockDevice)
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

bool volumebackup::util::IsBlockDeviceExists(const std::string& blockDevicePath)
{
    try {
        ReadVolumeSize(blockDevicePath);
    } catch (...) {
        return false;
    }
    return true;
}

bool volumebackup::util::CheckDirectoryExistence(const std::string& path)
{
    try {
        if (std::filesystem::is_directory(path)) {
            return true;
        }
        return std::filesystem::create_directories(path);
    } catch (const std::exception& e) {
        return false;
    }
}

uint32_t volumebackup::util::ProcessorsNum()
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

std::string volumebackup::util::ReadVolumeUUID(const std::string& blockDevicePath)
{
    return ReadPosixBlockDeviceAttribute(blockDevicePath, "UUID");
}

std::string volumebackup::util::ReadVolumeType(const std::string& blockDevicePath)
{
    return ReadPosixBlockDeviceAttribute(blockDevicePath, "TYPE");
}

std::string volumebackup::util::ReadVolumeLabel(const std::string& blockDevicePath)
{
    return ReadPosixBlockDeviceAttribute(blockDevicePath, "LABEL");
}

std::vector<VolumePartitionTableEntry> volumebackup::util::ReadVolumePartitionTable(const std::string& blockDevicePath)
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

        VolumePartitionTableEntry entry {};
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

std::string volumebackup::util::GetChecksumBinPath(
    const std::string& copyMetaDirPath,
    uint64_t sessionOffset,
    uint64_t sessionSize)
{
    std::string filename = std::to_string(sessionOffset) + "." + std::to_string(sessionSize) + ".sha256.meta.bin";
    return copyMetaDirPath + SEPARATOR + filename;
}

std::string volumebackup::util::GetCopyFilePath(
    const std::string& copyDataDirPath,
    uint64_t sessionOffset,
    uint64_t sessionSize)
{
    std::string filename = std::to_string(sessionOffset) + "." + std::to_string(sessionSize) + ".data.bin";
    return copyDataDirPath + SEPARATOR + filename;
}

bool volumebackup::util::WriteVolumeCopyMeta(
    const std::string& copyMetaDirPath,
    const VolumeCopyMeta& volumeCopyMeta)
{
    // TODO:: implement
    std::string jsonStr = xuranus::minijson::util::Serialize(const_cast<VolumeCopyMeta&>(volumeCopyMeta));
    std::string filepath = copyMetaDirPath + "/" + "copymeta.json";
    std::ofstream file(filepath);
    if (!file.is_open()) {
        ERRLOG("failed to open file %s to write json %s", filepath.c_str(), jsonStr.c_str());
        return false;
    }
    file << jsonStr;
    file.close();
    return true;
}

bool volumebackup::util::ReadVolumeCopyMeta(
    const std::string& copyMetaDirPath,
    VolumeCopyMeta& volumeCopyMeta)
{
    // TODO:: implement
    return true;
}