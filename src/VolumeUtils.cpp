#include "Logger.h"
#include "VolumeUtils.h"

namespace {
#ifdef _WIN32
    constexpr auto SEPARATOR = "\\";
#else
    constexpr auto SEPARATOR = "/";
#endif
    constexpr auto VOLUME_COPY_META_JSON_FILENAME = "volumecopy.meta.json";
    constexpr auto SHA256_CHECKSUM_BINARY_FILENAME_SUFFIX = ".sha256.meta.bin";
    constexpr auto COPY_BINARY_FILENAME_SUFFIX = ".copydata.bin";
    constexpr auto WRITER_BITMAP_FILENAME_SUFFIX = ".checkpoint.bin";
}

using namespace volumeprotect;

inline std::string ConcatSessionFileName(uint64_t sessionOffset, uint64_t sessionSize, const std::string& suffix)
{
    return std::to_string(sessionOffset) + "." + std::to_string(sessionSize) + suffix;
}

std::string util::GetChecksumBinPath(
    const std::string& copyMetaDirPath,
    uint64_t sessionOffset,
    uint64_t sessionSize)
{
    std::string filename = ConcatSessionFileName(sessionOffset, sessionSize, SHA256_CHECKSUM_BINARY_FILENAME_SUFFIX);
    return copyMetaDirPath + SEPARATOR + filename;
}

std::string util::GetCopyFilePath(
    const std::string& copyDataDirPath,
    uint64_t sessionOffset,
    uint64_t sessionSize)
{
    std::string filename = ConcatSessionFileName(sessionOffset, sessionSize, COPY_BINARY_FILENAME_SUFFIX);
    return copyDataDirPath + SEPARATOR + filename;
}

std::string util::GetWriterBitmapFilePath(
    const std::string&          copyMetaDirPath,
    uint64_t                    sessionOffset,
    uint64_t                    sessionSize)
{
    std::string filename = ConcatSessionFileName(sessionOffset, sessionSize, WRITER_BITMAP_FILENAME_SUFFIX);
    return copyMetaDirPath + SEPARATOR + filename;
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