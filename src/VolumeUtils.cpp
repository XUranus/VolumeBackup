#include <vector>
#include <string>
#include <fstream>
#include <filesystem>

#ifdef __linux__
#include <fcntl.h>
#include <fstream>
#include <sys/ioctl.h>
#include <linux/fs.h>
#include <unistd.h>
extern "C" {
    #include <blkid/blkid.h>
    #include <parted/parted.h>
}
#endif

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN 1
#define UNICODE /* foring using WCHAR on windows */
#define NOGDI
#include <Windows.h>
#include <locale>
#include <codecvt>
#endif

#include "Logger.h"
#include "VolumeProtector.h"
#include "VolumeUtils.h"

namespace {
#ifdef WIN32
    constexpr auto SEPARATOR = "\\";
#else
    constexpr auto SEPARATOR = "/";
#endif
    constexpr auto VOLUME_COPY_META_JSON_FILENAME = "volumecopy.meta.json";
}

using namespace volumeprotect;

#ifdef _WIN32
static std::wstring Utf8ToUtf16(const std::string& str)
{
    using ConvertTypeX = std::codecvt_utf8_utf16<wchar_t>;
    std::wstring_convert<ConvertTypeX> converterX;
    std::wstring wstr = converterX.from_bytes(str);
    return wstr;
}

static std::string Utf16ToUtf8(const std::wstring& wstr)
{
    using ConvertTypeX = std::codecvt_utf8_utf16<wchar_t>;
    std::wstring_convert<ConvertTypeX> converterX;
    return converterX.to_bytes(wstr);
}
#endif

std::runtime_error volumeprotect::util::BuildRuntimeException(
    const std::string& message,
    const std::string& blockDevice,
    uint32_t errcode)
{
    std::string label;
    label += (message + ", device = " + blockDevice + ", errno " + std::to_string(errcode));
    return std::runtime_error(label);
}

#ifdef __linux__
uint64_t volumeprotect::util::ReadVolumeSize(const std::string& blockDevice)
{
    int fd = ::open(blockDevice.c_str(), O_RDONLY);
    if (fd < 0) {
        throw BuildRuntimeException("Error opening block device", blockDevice, errno);
        return 0;
    }

    uint64_t size = 0;
    if (::ioctl(fd, BLKGETSIZE64, &size) < 0) {
        close(fd);
        throw BuildRuntimeException("Error getting block device size", blockDevice, errno);
        return 0;
    }

    ::close(fd);
    return size;
}
#endif

#ifdef _WIN32
uint64_t volumeprotect::util::ReadVolumeSize(const std::string& volumePath)
{
    std::wstring wVolumePath = Utf8ToUtf16(volumePath);
    ULARGE_INTEGER totalSize {};

    if (::GetDiskFreeSpaceExW(wVolumePath.c_str(), nullptr, &totalSize, nullptr)) {
        return totalSize.QuadPart;
    }
    throw BuildRuntimeException("Error GetDiskFreeSpaceEx", volumePath, ::GetLastError());
    return 0;
}
#endif


bool volumeprotect::util::IsBlockDeviceExists(const std::string& blockDevicePath)
{
    try {
        ReadVolumeSize(blockDevicePath);
    } catch (...) {
        return false;
    }
    return true;
}

bool volumeprotect::util::CheckDirectoryExistence(const std::string& path)
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

#ifdef __linux
uint32_t volumeprotect::util::ProcessorsNum()
{
    return sysconf(_SC_NPROCESSORS_ONLN);
}
#endif

#ifdef _WIN32
uint32_t volumeprotect::util::ProcessorsNum()
{
    SYSTEM_INFO systemInfo;
    ::GetSystemInfo(&systemInfo);
    
    DWORD processorCount = systemInfo.dwNumberOfProcessors;
    return processorCount;
}
#endif

#ifdef __linux__
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
#endif

#ifdef __linux__
std::string volumeprotect::util::ReadVolumeUUID(const std::string& blockDevicePath)
{
    return ReadPosixBlockDeviceAttribute(blockDevicePath, "UUID");
}

std::string volumeprotect::util::ReadVolumeType(const std::string& blockDevicePath)
{
    return ReadPosixBlockDeviceAttribute(blockDevicePath, "TYPE");
}

std::string volumeprotect::util::ReadVolumeLabel(const std::string& blockDevicePath)
{
    return ReadPosixBlockDeviceAttribute(blockDevicePath, "LABEL");
}
#endif

#ifdef _WIN32
std::string volumeprotect::util::ReadVolumeUUID(const std::string& blockDevicePath)
{
    // TODO
    return "";
}

std::string volumeprotect::util::ReadVolumeType(const std::string& blockDevicePath)
{
    // TODO
    return "";
}

std::string volumeprotect::util::ReadVolumeLabel(const std::string& blockDevicePath)
{
    // TODO
    return "";
}
#endif

#ifdef __linux__
std::vector<VolumePartitionTableEntry> volumeprotect::util::ReadVolumePartitionTable(const std::string& blockDevicePath)
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
#endif

#ifdef _WIN32
std::vector<VolumePartitionTableEntry> volumeprotect::util::ReadVolumePartitionTable(const std::string& blockDevicePath)
{
    // TODO
    return std::vector<VolumePartitionTableEntry> {};
}
#endif

std::string volumeprotect::util::GetChecksumBinPath(
    const std::string& copyMetaDirPath,
    uint64_t sessionOffset,
    uint64_t sessionSize)
{
    std::string suffix = ".sha256.meta.bin";
    std::string filename = std::to_string(sessionOffset) + "." + std::to_string(sessionSize) + suffix;
    return copyMetaDirPath + SEPARATOR + filename;
}

std::string volumeprotect::util::GetCopyFilePath(
    const std::string& copyDataDirPath,
    CopyType copyType,
    uint64_t sessionOffset,
    uint64_t sessionSize)
{
    std::string suffix = ".data.full.bin";
    if (copyType == CopyType::INCREMENT) {
        suffix = ".data.inc.bin";
    }
    std::string filename = std::to_string(sessionOffset) + "." + std::to_string(sessionSize) + suffix;
    return copyDataDirPath + SEPARATOR + filename;
}

bool volumeprotect::util::WriteVolumeCopyMeta(
    const std::string& copyMetaDirPath,
    const VolumeCopyMeta& volumeCopyMeta)
{
    std::string jsonStr = xuranus::minijson::util::Serialize(volumeCopyMeta);
    std::string filepath = copyMetaDirPath + SEPARATOR + VOLUME_COPY_META_JSON_FILENAME;
    try {
        std::ofstream file(filepath);
        if (!file.is_open()) {
            ERRLOG("failed to open file %s to write copy meta json %s", filepath.c_str(), jsonStr.c_str());
            return false;
        }
        file << jsonStr;
        file.close();
    } catch (const std::exception& e) {
        ERRLOG("failed to write copy meta json %s, exception: %s", filepath.c_str(), e.what());
        return false;
    } catch (...) {
        ERRLOG("failed to write copy meta json %s, exception caught", filepath.c_str());
        return false;
    }
    return true;
}

bool volumeprotect::util::ReadVolumeCopyMeta(
    const std::string& copyMetaDirPath,
    VolumeCopyMeta& volumeCopyMeta)
{
    std::string filepath = copyMetaDirPath + SEPARATOR + VOLUME_COPY_META_JSON_FILENAME;
    try {
        std::ifstream file(filepath);
        std::string jsonStr;
        if (!file.is_open()) {
            ERRLOG("failed to open file %s to read copy meta json %s", filepath.c_str(), jsonStr.c_str());
            return false;
        }
        file >> jsonStr;
        file.close();
        xuranus::minijson::util::Deserialize(jsonStr, volumeCopyMeta);
    } catch (const std::exception& e) {
        ERRLOG("failed to read copy meta json %s, exception: %s", filepath.c_str(), e.what());
        return false;
    } catch (...) {
        ERRLOG("failed to read copy meta json %s, exception caught", filepath.c_str());
        return false;
    }
    return true;
}
