#include <vector>
#include <string>
#include <fstream>

#include "Logger.h"
#include "VolumeProtector.h"
#include "VolumeUtils.h"

namespace {
#ifdef _WIN32
    constexpr auto SEPARATOR = "\\";
#else
    constexpr auto SEPARATOR = "/";
#endif
    constexpr auto VOLUME_COPY_META_JSON_FILENAME = "volumecopy.meta.json";
}

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
