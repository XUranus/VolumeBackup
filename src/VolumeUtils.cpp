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

bool util::SaveBitmap(const std::string& filepath, const Bitmap& bitmap)
{
    try {
        std::ofstream file(filepath, std::ios::binary | std::ios::trunc);
        if (!file.is_open()) {
            ERRLOG("failed to open file %s to save bitmap file %s", filepath.c_str());
            return false;
        }
        file.write(bitmap.Ptr(), bitmap.Capacity());
        if (file.fail()) {
            file.close();
            ERRLOG("failed to write bitmap file %s, size %llu, errno: %d", filepath.c_str(), bitmap.Capacity(), errno);
        }
        file.close();
    } catch (const std::exception& e) {
        ERRLOG("failed to save bitmap file %s, exception: %s", filepath.c_str(), e.what());
        return false;
    } catch (...) {
        ERRLOG("failed to save bitmap file %s, exception caught", filepath.c_str());
        return false;
    }
    return true;
}

std::shared_ptr<Bitmap> util::ReadBitmap(const std::string& filepath)
{
    uint64_t size = native::GetFileSize(path);
    if (size == 0) {
        return nullptr;
    }
    try {
        auto buffer = std::make_unique<char[]>(size);
        memset(buffer.get(), 0, size);
        std::ifstream file(filepath, std::ios::binary);
        if (!file.is_open()) {
            ERRLOG("failed to open %s, errno = %d", filepath.c_str(), errno);
            return nullptr;
        }
        file.write(buffer.get(), size);
        if (file.fail()) {
            ERRLOG("failed to read bitmap %s, errno: %d", filepath.c_str(), errno);
            file.close();
            return nullptr;
        }
        file.close();
        return std::make_shared<Bitmap>(buffer, size);
    } catch (const std::exception& e) {
        ERRLOG("failed to read bitmap file %s, exception: %s", filepath.c_str(), e.what());
        return nullptr;
    } catch (...) {
        ERRLOG("failed to read bitmap file %s, exception caught", filepath.c_str());
        return nullptr;
    }
    return nullptr;
}