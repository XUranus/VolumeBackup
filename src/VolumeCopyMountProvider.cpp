#include "VolumeCopyMountProvider.h"

// external logger/json library
#include "Json.h"
#include "logger.h"
#include "VolumeUtils.h"

using namespace volumeprotect;
using namespace volumeprotect::mount;
using namespace volumeprotect::util;

// factory function to load target copy mount provider, depending on which CopyFormat it is
std::unique_ptr<VolumeCopyMountProvider> VolumeCopyMountProvider::BuildVolumeCopyMountProvider(
    VolumeCopyMountConfig& mountConfig)
{
    VolumeCopyMeta volumeCopyMeta {};
    if (!util::ReadVolumeCopyMeta(mountConfig.copyMetaDirPath, mountConfig.copyName, volumeCopyMeta)) {
        ERRLOG("failed to read volume copy meta from %s, copy name %s",
            mountConfig.copyMetaDirPath.c_str(), mountConfig.copyName.c_str());
        return nullptr;
    }
    CopyFormat copyFormat = static_cast<CopyFormat>(volumeCopyMeta.copyFormat);
    switch (volumeCopyMeta.copyFormat) {
        case static_cast<int>(CopyFormat::BIN) : {
            ;
        }
        case static_cast<int>(CopyFormat::IMAGE) : {
            ;
        }
#ifdef _WIN32
        case static_cast<int>(CopyFormat::VHD_DYNAMIC) :
        case static_cast<int>(CopyFormat::VHD_FIXED) :
        case static_cast<int>(CopyFormat::VHDX_DYNAMIC) :
        case static_cast<int>(CopyFormat::VHDX_FIXED) : {

            return;
        }
#endif
        default: ERRLOG("unknown copy format type %d", copyFormat);
    }
    return nullptr;
}

bool VolumeCopyMountProvider::IsMountSupported()
{
    // base class does not support mount, need implementation from derived class
    return false;
}

bool VolumeCopyMountProvider::Mount() {
    // base class does not support mount, need implementation from derived class
    return false;
}

std::string VolumeCopyMountProvider::GetMountRecordPath() const
{
    // return default empty string, need implementation from derived class
    return "";
}

std::string DeviceMapper::GetError() const
{
    std::string errors;
    for (const std::string& errorMessage : m_errors) {
        errors += errorMessage + "\n";
    }
    return errors;
}

void DeviceMapper::RecordError(const char* message, ...)
{
    va_list args;
    va_start(args, message);
    // Determine the length of the formatted string
    va_list args_copy;
    va_copy(args_copy, args);
    int length = std::vsnprintf(nullptr, 0, message, args_copy);
    va_end(args_copy);
    if (length <= 0) {
        va_end(args);
        ERRLOG("failed to compute str format buffer size, errno %u", errno);
        return;
    }
    // Create a buffer to store the formatted string
    std::string formattedString(static_cast<size_t>(length) + 1, '\0');
    std::vsnprintf(&formattedString[0], formattedString.size(), message, args);
    va_end(args);
    m_errors.emplace_back(formattedString);
}



std::unique_ptr<VolumeCopyUmountProvider> VolumeCopyUmountProvider::BuildVolumeCopyUmountProvider(
    const std::string mountRecordJsonFilePath
);

bool VolumeCopyUmountProvider::Umount();
