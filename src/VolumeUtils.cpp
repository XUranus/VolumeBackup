#include "Logger.h"
#include "VolumeUtils.h"
#include <string>

namespace {
#ifdef _WIN32
    constexpr auto SEPARATOR = "\\";
#else
    constexpr auto SEPARATOR = "/";
#endif
    constexpr auto VOLUME_COPY_META_JSON_FILENAME = "volumecopy.meta.json";
    constexpr auto SHA256_CHECKSUM_BINARY_FILENAME_SUFFIX = ".sha256.meta.bin";
    constexpr auto COPY_DATA_BIN_FILENAME_SUFFIX = ".copydata.bin";
    constexpr auto COPY_DATA_BIN_PARTED_FILENAME_SUFFIX = ".copydata.bin.part";
    constexpr auto COPY_DATA_VHD_FILENAME_SUFFIX = ".copydata.vhd";
    constexpr auto COPY_DATA_VHDX_FILENAME_SUFFIX = ".copydata.vhdx";
    constexpr auto WRITER_BITMAP_FILENAME_SUFFIX = ".checkpoint.bin";
}

using namespace volumeprotect;

std::string util::GetChecksumBinPath(
    const std::string&  copyMetaDirPath,
    const std::string&  copyName,
    int                 sessionIndex)
{
    std::string filename = copyName + "." + std::to_string(sessionIndex) + SHA256_CHECKSUM_BINARY_FILENAME_SUFFIX;
    return copyMetaDirPath + SEPARATOR + filename;
}

std::string util::GetCopyDataFilePath(
    const std::string&  copyDataDirPath,
    const std::string&  copyName,
    CopyFormat          copyFormat,
    int                 sessionIndex)
{
    std::string suffix = COPY_DATA_BIN_FILENAME_SUFFIX;
    std::string filename;
    if (copyFormat == CopyFormat::BIN && sessionIndex == 0) {
        filename = copyName + COPY_DATA_BIN_FILENAME_SUFFIX;        
    } else if (copyFormat == CopyFormat::BIN && sessionIndex != 0) {
        filename = copyName + COPY_DATA_BIN_PARTED_FILENAME_SUFFIX + std::to_string(sessionIndex);
    } else if (copyFormat == CopyFormat::VHD_FIXED || copyFormat == CopyFormat::VHD_DYNAMIC) {
        filename = copyName + COPY_DATA_VHD_FILENAME_SUFFIX;
    } else if (copyFormat == CopyFormat::VHDX_FIXED || copyFormat == CopyFormat::VHDX_DYNAMIC) {
        filename = copyName + COPY_DATA_VHDX_FILENAME_SUFFIX;
    }
    return copyDataDirPath + SEPARATOR + filename;
}

std::string util::GetWriterBitmapFilePath(
    const std::string&  copyMetaDirPath,
    const std::string&  copyName,
    int                 sessionIndex)
{
    std::string filename = copyName + "." + std::to_string(sessionIndex) + WRITER_BITMAP_FILENAME_SUFFIX;
    return copyMetaDirPath + SEPARATOR + filename;
}

std::string util::GetFileName(const std::string& fullpath)
{
    auto pos = fullpath.rfind(SEPARATOR);
    return pos == std::string::npos ? fullpath : fullpath.substr(pos + 1);
}

bool util::WriteVolumeCopyMeta(
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

bool util::ReadVolumeCopyMeta(
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