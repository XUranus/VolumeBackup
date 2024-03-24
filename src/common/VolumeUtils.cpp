/**
 * @copyright Copyright 2023-2024 XUranus. All rights reserved.
 * @license This project is released under the Apache License.
 * @author XUranus(2257238649wdx@gmail.com)
 */

#include "VolumeUtils.h"
#include <string>

namespace {
#ifdef _WIN32
    constexpr auto SEPARATOR = "\\";
#else
    constexpr auto SEPARATOR = "/";
#endif
}

using namespace volumeprotect;

std::string common::GetChecksumBinPath(
    const std::string&  copyMetaDirPath,
    const std::string&  copyName,
    int                 sessionIndex)
{
    std::string filename = copyName + "." + std::to_string(sessionIndex) + SHA256_CHECKSUM_BINARY_FILENAME_EXTENSION;
    return common::PathJoin(copyMetaDirPath, filename);
}

std::string common::GetCopyDataFilePath(
    const std::string&  copyDataDirPath,
    const std::string&  copyName,
    CopyFormat          copyFormat,
    int                 sessionIndex)
{
    std::string suffix = COPY_DATA_BIN_FILENAME_EXTENSION;
    std::string filename;
    if (copyFormat == CopyFormat::BIN && sessionIndex == 0) {
        filename = copyName + COPY_DATA_BIN_FILENAME_EXTENSION;
    } else if (copyFormat == CopyFormat::IMAGE) {
        filename = copyName + COPY_DATA_IMAGE_FILENAME_EXTENSION;
    } else if (copyFormat == CopyFormat::BIN && sessionIndex != 0) {
        filename = copyName + COPY_DATA_BIN_PARTED_FILENAME_EXTENSION + std::to_string(sessionIndex);
#ifdef _WIN32
    } else if (copyFormat == CopyFormat::VHD_FIXED || copyFormat == CopyFormat::VHD_DYNAMIC) {
        filename = copyName + COPY_DATA_VHD_FILENAME_EXTENSION;
    } else if (copyFormat == CopyFormat::VHDX_FIXED || copyFormat == CopyFormat::VHDX_DYNAMIC) {
        filename = copyName + COPY_DATA_VHDX_FILENAME_EXTENSION;
#endif
    }
    return common::PathJoin(copyDataDirPath, filename);
}

std::string common::GetWriterBitmapFilePath(
    const std::string&  checkpointDirPath,
    const std::string&  copyName,
    int                 sessionIndex)
{
    std::string filename = copyName + "." + std::to_string(sessionIndex) + WRITER_BITMAP_FILENAME_EXTENSION;
    return common::PathJoin(checkpointDirPath, filename);
}

std::string common::GetFileName(const std::string& fullpath)
{
    auto pos = fullpath.find_last_of("/\\");
    return pos == std::string::npos ? fullpath : fullpath.substr(pos + 1);
}

std::string common::GetParentDirectoryPath(const std::string& fullpath)
{
    std::string parentDirPath = fullpath;
    while (!parentDirPath.empty() && parentDirPath.back() == SEPARATOR[0]) {
        parentDirPath.pop_back();
    }
    auto pos = parentDirPath.rfind(SEPARATOR);
    return pos == std::string::npos ? "" : fullpath.substr(0, pos);
}

bool common::WriteVolumeCopyMeta(
    const std::string& copyMetaDirPath,
    const std::string& copyName,
    const VolumeCopyMeta& volumeCopyMeta)
{
    std::string filepath = common::PathJoin(copyMetaDirPath, copyName + VOLUME_COPY_META_JSON_FILENAME_EXTENSION);
    return JsonSerialize(volumeCopyMeta, filepath);
}

bool common::ReadVolumeCopyMeta(
    const std::string& copyMetaDirPath,
    const std::string& copyName,
    VolumeCopyMeta& volumeCopyMeta)
{
    std::string filepath = common::PathJoin(copyMetaDirPath, copyName + VOLUME_COPY_META_JSON_FILENAME_EXTENSION);
    return JsonDeserialize(volumeCopyMeta, filepath);
}