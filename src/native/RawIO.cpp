#include "VolumeProtector.h"
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <fstream>
#include <memory>
#include "Logger.h"
#include "VolumeUtils.h"
#include "native/RawIO.h"

using namespace volumeprotect;
using namespace volumeprotect::rawio;

#ifdef _WIN32
#include "win32/Win32RawIO.h"
using OsPlatformRawDataReader = rawio::win32::Win32RawDataReader;
using OsPlatformRawDataWriter = rawio::win32::Win32RawDataWriter;
#endif

#ifdef  __linux__
#include "linux/PosixRawIO.h"
using OsPlatformRawDataReader = rawio::posix::PosixRawDataReader;
using OsPlatformRawDataWriter = rawio::posix::PosixRawDataWriter;
#endif

namespace {
    constexpr auto DUMMY_SESSION_INDEX = 999;
}

static bool PrepareFragmentBinaryBackupCopy(
    const std::string&  copyName,
    const std::string&  copyDataDirPath,
    uint64_t            volumeSize,
    uint64_t            defaultSessionSize)
{
    int sessionIndex = 0;
    for (uint64_t sessionOffset = 0; sessionOffset < volumeSize;) {
        ErrCodeType errorCode = 0;
        uint64_t sessionSize = defaultSessionSize;
        if (sessionOffset + sessionSize >= volumeSize) {
            sessionSize = volumeSize - sessionOffset;
        }
        sessionOffset += sessionSize;
        ++sessionIndex;
        std::string fragmentFilePath = util::GetCopyDataFilePath(
            copyDataDirPath, copyName, CopyFormat::BIN, sessionIndex);
        if (!rawio::TruncateCreateFile(fragmentFilePath, sessionSize, errorCode)) {
            ERRLOG("failed to create fragment binary copy file %s, size %llu, error code %d",
                fragmentFilePath.c_str(), sessionSize, errorCode);
            return false;
        }
    }
    return true;
}

bool rawio::PrepareBackupCopy(const VolumeBackupConfig& backupConfig, uint64_t volumeSize)
{
    CopyFormat copyFormat = backupConfig.copyFormat;
    std::string copyDataDirPath = backupConfig.outputCopyDataDirPath;
    std::string copyName = backupConfig.copyName;
    bool result = false;
    ErrCodeType errorCode = 0;
    // TODO:: all followings check file exists for checkpoint
    switch (static_cast<int>(copyFormat)) {
        case static_cast<int>(CopyFormat::BIN): {
            result = PrepareFragmentBinaryBackupCopy(
                copyName, copyDataDirPath, volumeSize, backupConfig.sessionSize);
            break;
        }
        case static_cast<int>(CopyFormat::IMAGE): {
            std::string imageFilePath = util::GetCopyDataFilePath(
                copyDataDirPath, copyName, copyFormat, DUMMY_SESSION_INDEX);
            result = rawio::TruncateCreateFile(imageFilePath, volumeSize, errorCode);
            break;
        }
#ifdef _WIN32
        case static_cast<int>(CopyFormat::VHD_FIXED): {
            std::string virtualDiskPath = util::GetCopyDataFilePath(
                copyDataDirPath, copyName, copyFormat, DUMMY_SESSION_INDEX);
            result = CreateFixedVHDFile(virtualDiskPath, volumeSize, errorCode);
            break;
        }
        case static_cast<int>(CopyFormat::VHD_DYNAMIC): {
            std::string virtualDiskPath = util::GetCopyDataFilePath(
                copyDataDirPath, copyName, copyFormat, DUMMY_SESSION_INDEX);
            result = CreateDynamicVHDFile(virtualDiskPath, volumeSize, errorCode);
            break;
        }
        case static_cast<int>(CopyFormat::VHDX_FIXED): {
            std::string virtualDiskPath = util::GetCopyDataFilePath(
                copyDataDirPath, copyName, copyFormat, DUMMY_SESSION_INDEX);
            result = CreateFixedVHDXFile(virtualDiskPath, volumeSize, errorCode);
            break;
        }
        case static_cast<int>(CopyFormat::VHDX_DYNAMIC): {
            std::string virtualDiskPath = util::GetCopyDataFilePath(
                copyDataDirPath, copyName, copyFormat, DUMMY_SESSION_INDEX);
            result = CreateDynamicVHDXFile(virtualDiskPath, volumeSize, errorCode);
            break;
        }
#endif
    }
    if (!result) {
        ERRLOG("failed to prepare backup copy %s, error code %d", copyName.c_str(), errorCode);
    }
    return result;
}

std::shared_ptr<rawio::RawDataReader> rawio::OpenRawDataCopyReader(const SessionCopyRawIOParam& param)
{
    CopyFormat copyFormat = param.copyFormat;
    std::string copyFilePath = param.copyFilePath;

    switch (static_cast<int>(copyFormat)) {
        case static_cast<int>(CopyFormat::BIN): {
            return std::make_shared<OsPlatformRawDataReader>(copyFilePath, -1, param.volumeOffset);
        }
        case static_cast<int>(CopyFormat::IMAGE): {
            return std::make_shared<OsPlatformRawDataReader>(copyFilePath, 0, 0);
        }
#ifdef _WIN32
        case static_cast<int>(CopyFormat::VHD_FIXED):
        case static_cast<int>(CopyFormat::VHD_DYNAMIC):
        case static_cast<int>(CopyFormat::VHDX_FIXED):
        case static_cast<int>(CopyFormat::VHDX_DYNAMIC): {
            // TODO:: check and attach VHD and open device
            break;
        }
#endif
        default: ERRLOG("open unsupport copy format %d for read", static_cast<int>(copyFormat));
    }
    return nullptr;
}

std::shared_ptr<RawDataWriter> rawio::OpenRawDataCopyWriter(const SessionCopyRawIOParam& param)
{
    CopyFormat copyFormat = param.copyFormat;
    std::string copyFilePath = param.copyFilePath;

    switch (static_cast<int>(copyFormat)) {
        case static_cast<int>(CopyFormat::BIN): {
            return std::make_shared<OsPlatformRawDataWriter>(copyFilePath, -1, param.volumeOffset);
        }
        case static_cast<int>(CopyFormat::IMAGE): {
            return std::make_shared<OsPlatformRawDataWriter>(copyFilePath, 0, 0);
        }
#ifdef _WIN32
        case static_cast<int>(CopyFormat::VHD_FIXED):
        case static_cast<int>(CopyFormat::VHD_DYNAMIC):
        case static_cast<int>(CopyFormat::VHDX_FIXED):
        case static_cast<int>(CopyFormat::VHDX_DYNAMIC): {
            // TODO:: create GPT partition and check and attach VHD and open device
            break;
        }
#endif
        default: ERRLOG("open unsupport copy format %d for write", static_cast<int>(copyFormat));
    }
    return nullptr;
}

std::shared_ptr<RawDataReader> rawio::OpenRawDataVolumeReader(const std::string& volumePath)
{
    return std::make_shared<OsPlatformRawDataReader>(volumePath, 0, 0);
}

std::shared_ptr<RawDataWriter> rawio::OpenRawDataVolumeWriter(const std::string& volumePath)
{
    return std::make_shared<OsPlatformRawDataWriter>(volumePath, 0, 0);
}