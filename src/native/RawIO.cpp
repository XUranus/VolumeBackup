#include <cerrno>
#include <cstdio>
#ifdef __linux__
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <linux/fs.h>
#include <unistd.h>
#include <dirent.h>
#endif

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN 1
#define UNICODE /* foring using WCHAR on windows */
#define NOGDI
#include <locale>
#include <codecvt>
#include <Windows.h>
#include <winioctl.h>
#endif

#include <iostream>
#include <fstream>
#include "Logger.h"
#include "native/NativeIOInterface.h"

using namespace volumeprotect;
using namespace volumeprotect::native;

namespace {
    constexpr auto DEFAULT_PROCESSORS_NUM = 4;
    constexpr auto DEFAULT_MKDIR_MASK = 0755;
#ifdef _WIN32
    constexpr auto SEPARATOR = "\\";
    const IOHandle SYSTEM_IO_INVALID_HANDLE = INVALID_HANDLE_VALUE;
#else
    constexpr auto SEPARATOR = "/";
    const IOHandle SYSTEM_IO_INVALID_HANDLE = -1;
#endif
}





std::unique_ptr<DataReader> RawIOFactory::CreateRawDataReader(RAW_IO_FACTORY_ENUM readerType, const std::string& path)
{
    switch (static_cast<int>(readerType)) {
        case static_cast<int>(RAW_IO_FACTORY_ENUM::BIN_FILE) : 
            return std::make_unique<Win32RawDataReader>(const std::string& path);
        case static_cast<int>(RAW_IO_FACTORY_ENUM::BIN_FILE) : 
            return std::make_unique<Win32RawDataReader>(const std::string& path);
        case static_cast<int>(RAW_IO_FACTORY_ENUM::BIN_FILE) : 
            return std::make_unique<Win32RawDataReader>(const std::string& path);
        case static_cast<int>(RAW_IO_FACTORY_ENUM::BIN_FILE) : 
            return std::make_unique<Win32RawDataReader>(const std::string& path);
        case static_cast<int>(RAW_IO_FACTORY_ENUM::BIN_FILE) : 
            return std::make_unique<Win32RawDataReader>(const std::string& path);
    }
}

std::unique_ptr<DataWriter> RawIOFactory::CreateRawDataWriter(RAW_IO_FACTORY_ENUM writerType, const std::string& path)
{

}






using FileDataReader = SystemDataReader;
using FileDataWriter = SystemDataWriter;
using VolumeDataReader = SystemDataReader;
using VolumeDataWriter = SystemDataWriter;