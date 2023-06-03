#include <stdexcept>
#include <string>
#include <exception>

namespace volumebackup {

std::runtime_error BuildRuntimeException(
    const std::string& message,
    const std::string& blockDevice,
    uint32_t errcode);

uint64_t ReadVolumeSize(const std::string& blockDevice);

}