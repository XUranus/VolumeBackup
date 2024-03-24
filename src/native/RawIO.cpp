/**
 * @copyright Copyright 2023-2024 XUranus. All rights reserved.
 * @license This project is released under the Apache License.
 * @author XUranus(2257238649wdx@gmail.com)
 */

#include "common/VolumeProtectMacros.h"
#include "VolumeProtector.h"
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <fstream>
#include <memory>
#include "Logger.h"
#include "native/RawIO.h"

using namespace volumeprotect;
using namespace volumeprotect::rawio;

#ifdef _WIN32
#include "win32/Win32RawIO.h"
using OsPlatformRawDataReader = rawio::win32::Win32RawDataReader;
using OsPlatformRawDataWriter = rawio::win32::Win32RawDataWriter;
#endif

#ifdef POSIXAPI
#include "linux/PosixRawIO.h"
using OsPlatformRawDataReader = rawio::posix::PosixRawDataReader;
using OsPlatformRawDataWriter = rawio::posix::PosixRawDataWriter;
#endif

namespace {
    constexpr auto DUMMY_SESSION_INDEX = 999;
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
            // need virtual disk be attached and inited ahead, this should be guaranteed by TaskResourceManager
            return std::make_shared<rawio::win32::Win32VirtualDiskVolumeRawDataReader>(copyFilePath, false);
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
            // need virtual disk be attached and inited ahead, this should be guaranteed by TaskResourceManager
            return std::make_shared<rawio::win32::Win32VirtualDiskVolumeRawDataWriter>(copyFilePath, false);
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