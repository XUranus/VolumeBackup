#include <cstdint>
#include <stdexcept>
#include <string>
#include <exception>
#include <vector>

namespace volumebackup {
namespace util {

struct VolumePartitionTableEntry {
    std::string filesystem;
    uint64_t    patitionNumber;
    uint64_t    firstSector;
    uint64_t    lastSector;
    uint64_t    totalSectors;
};

std::runtime_error BuildRuntimeException(
    const std::string& message,
    const std::string& blockDevice,
    uint32_t errcode);

uint64_t ReadVolumeSize(const std::string& blockDevice);

uint32_t ProcessorsNum();

std::string ReadVolumeUUID(const std::string& blockDevicePath);

std::string ReadVolumeType(const std::string& blockDevicePath);

std::string ReadVolumeLabel(const std::string& blockDevicePath);

std::vector<VolumePartitionTableEntry> ReadVolumePartitionTable(const std::string& blockDevicePath);

}
}