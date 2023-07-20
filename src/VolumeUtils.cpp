#include <vector>
#include <string>
#include <fstream>
#include <stdexcept>

#if __cplusplus >= 201703L
// using std::filesystem requires CXX17
#include <filesystem>
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

#ifdef __linux__
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/fs.h>
#include <unistd.h>
#endif

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
    constexpr auto DEFAULT_PROCESSORS_NUM = 4;
    constexpr auto DEFAULT_MKDIR_MASK = 0755;
}

using namespace volumeprotect;

SystemApiException::SystemApiException(uint32_t errorCode)
{
    m_message = std::string("error code = ") + std::to_string(errorCode);
}

SystemApiException::SystemApiException(const char* message, uint32_t errorCode)
{
    m_message = std::string(message) + " , error code = " + std::to_string(errorCode);
}

const char* SystemApiException::what() const noexcept
{
    return m_message.c_str();
}

#ifdef _WIN32
static std::wstring Utf8ToUtf16(const std::string& str)
{
    using ConvertTypeX = std::codecvt_utf8_utf16<wchar_t>;
    std::wstring_convert<ConvertTypeX> converterX;
    std::wstring wstr = converterX.from_bytes(str);
    return wstr;
}

static std::string Utf16ToUtf8(const std::wstring& wstr)
{
    using ConvertTypeX = std::codecvt_utf8_utf16<wchar_t>;
    std::wstring_convert<ConvertTypeX> converterX;
    return converterX.to_bytes(wstr);
}

uint64_t GetVolumeSizeWin32(const std::string& devicePath)
{
    std::wstring wDevicePath = Utf8ToUtf16(devicePath);
    // Open the device
    HANDLE hDevice = ::CreateFileW(
        wDevicePath.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS,
        nullptr);
    if (hDevice == INVALID_HANDLE_VALUE) {
        // Failed to open handle
        throw SystemApiException("failed to open volume", ::GetLastError());
        return 0;
    }
    // Query the length information
    GET_LENGTH_INFORMATION lengthInfo {};
    DWORD bytesReturned = 0;
    if (!::DeviceIoControl(
        hDevice,
        IOCTL_DISK_GET_LENGTH_INFO,
        nullptr,
        0,
        &lengthInfo,
        sizeof(GET_LENGTH_INFORMATION),
        &bytesReturned,
        nullptr)) {
        // Failed to query length
        ::CloseHandle(hDevice);
        throw SystemApiException("failed to call IOCTL_DISK_GET_LENGTH_INFO", ::GetLastError());
        return 0;
    }
    ::CloseHandle(hDevice);
    return lengthInfo.Length.QuadPart;
}
#endif

#ifdef __linux__
uint64_t GetVolumeSizeLinux(const std::string& devicePath) {
    int fd = ::open(devicePath.c_str(), O_RDONLY);
    if (fd < 0) {
        throw SystemApiException("failed to open device", errno);
        return 0;
    }
    uint64_t size = 0;
    if (::ioctl(fd, BLKGETSIZE64, &size) < 0) {
        close(fd);
        throw SystemApiException("failed to execute ioctl BLKGETSIZE64", errno);
        return 0;
    }
    ::close(fd);
    return size;
}
#endif

uint64_t volumeprotect::util::ReadVolumeSize(const std::string& volumePath)
{
    uint64_t size = 0;
    try {
#ifdef _WIN32
        size = GetVolumeSizeWin32(volumePath);
#endif
#ifdef __linux__
        size = GetVolumeSizeLinux(volumePath);
#endif
    } catch (const SystemApiException& e) {
        throw e;
        return 0;
    }
    return size;
}


bool volumeprotect::util::IsBlockDeviceExists(const std::string& blockDevicePath)
{
    try {
        ReadVolumeSize(blockDevicePath);
    } catch (...) {
        return false;
    }
    return true;
}

bool volumeprotect::util::CheckDirectoryExistence(const std::string& path)
{
#if __cplusplus >= 201703L
    // using std::filesystem requires CXX17
    try {
        if (std::filesystem::is_directory(path)) {
            return true;
        }
        return std::filesystem::create_directories(path);
    } catch (...) {
        return false;
    }
#else
    // using WIN32/POSIX API
#ifdef _WIN32
    std::wstring wpath = Utf8ToUtf16(path);
    DWORD attribute = ::GetFileAttributesW(wpath.c_str());
    if (attribute != INVALID_FILE_ATTRIBUTES && (attribute & FILE_ATTRIBUTE_DIRECTORY)) {
        return true;
    }
    return ::CreateDirectoryW(wpath.c_str(), nullptr) != 0;
#else
    DIR* dir = ::opendir(path.c_str());
    if (dir) {
        closedir(dir);
        return true;
    }
    return ::mkdir(path.c_str(), DEFAULT_MKDIR_MASK) == 0;
#endif
#endif
}

#ifdef __linux
uint32_t volumeprotect::util::ProcessorsNum()
{
    auto processorCount = sysconf(_SC_NPROCESSORS_ONLN);
    return processorCount <= 0 ? DEFAULT_PROCESSORS_NUM : processorCount;
}
#endif

#ifdef _WIN32
uint32_t volumeprotect::util::ProcessorsNum()
{
    SYSTEM_INFO systemInfo;
    ::GetSystemInfo(&systemInfo);
    
    DWORD processorCount = systemInfo.dwNumberOfProcessors;
    return processorCount <= 0 ? DEFAULT_PROCESSORS_NUM : processorCount;
}
#endif

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
