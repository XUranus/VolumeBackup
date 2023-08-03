#ifndef VOLUMEBACKUP_NATIVE_IO_INTERFACE_HEADER
#define VOLUMEBACKUP_NATIVE_IO_INTERFACE_HEADER


#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN 1
#define UNICODE /* foring using WCHAR on windows */
#define NOGDI
#include <locale>
#include <codecvt>
#include <Windows.h>
#include <winioctl.h>
#endif

#include <string>
#include <cstdint>
#include <exception>
#include <stdexcept>

#include "VolumeProtectMacros.h"

/**
 * @brief this module is used to shield native I/O interface differences to provide a unified I/O layer
 */
namespace volumeprotect {
namespace native {

#ifdef _WIN32
using ErrCodeType = DWORD;
using IOHandle = HANDLE;
#endif

#ifdef __linux__
using ErrCodeType = int;
using IOHandle = int;
#endif

class SystemApiException : public std::exception {
public:
    // Constructor
    SystemApiException(ErrCodeType errorCode);
    SystemApiException(const char* message, ErrCodeType errorCode);
    const char* what() const noexcept override;
private:
    std::string m_message;
};

/**
 * @brief DataReader and DataWriter provide basic I/O interface
 *  for VolumeDataReader/VolumeDataWriter, FileDataReader/FileDataWriter to implement.
 *  Implement this interface if need to access other data source, ex: cloud, tape ...
 *
 */
class VOLUMEPROTECT_API DataReader {
public:
    virtual bool Read(uint64_t offset, char* buffer, int length, ErrCodeType& errorCode) = 0;
    virtual bool Ok() = 0;
    virtual ErrCodeType Error() = 0;
    virtual ~DataReader() = default;
};

class VOLUMEPROTECT_API DataWriter {
public:
    virtual bool Write(uint64_t offset, char* buffer, int length, ErrCodeType& errorCode) = 0;
    virtual bool Ok() = 0;
    virtual bool Flush() = 0;
    virtual ErrCodeType Error() = 0;
    virtual ~DataWriter() = default;
};

/**
 * @brief provide VolumeDataReader/VolumeDataWriter, FileDataReader/FileDataWriter implement using native API
 *
 */
class VOLUMEPROTECT_API SystemDataReader : public DataReader {
public:
    SystemDataReader(const std::string& path);
    ~SystemDataReader();
    bool Read(uint64_t offset, char* buffer, int length, ErrCodeType& errorCode) override;
    bool Ok() override;
    ErrCodeType Error() override;

private:
    IOHandle m_handle {};
};

class VOLUMEPROTECT_API SystemDataWriter : public DataWriter {
public:
    SystemDataWriter(const std::string& path);
    ~SystemDataWriter();
    bool Write(uint64_t offset, char* buffer, int length, ErrCodeType& errorCode) override;
    bool Ok() override;
    bool Flush() override;
    ErrCodeType Error() override;

private:
    IOHandle m_handle {};
};

using FileDataReader = SystemDataReader;
using FileDataWriter = SystemDataWriter;
using VolumeDataReader = SystemDataReader;
using VolumeDataWriter = SystemDataWriter;

VOLUMEPROTECT_API bool TruncateCreateFile(const std::string& path, uint64_t size, ErrCodeType& errorCode);

VOLUMEPROTECT_API bool IsFileExists(const std::string& path);

VOLUMEPROTECT_API uint64_t GetFileSize(const std::string& path);

VOLUMEPROTECT_API bool IsDirectoryExists(const std::string& path);

VOLUMEPROTECT_API uint8_t* ReadBinaryBuffer(const std::string& filepath, uint64_t length);

VOLUMEPROTECT_API bool WriteBinaryBuffer(const std::string& filepath, const uint8_t* buffer, uint64_t length);

VOLUMEPROTECT_API bool IsVolumeExists(const std::string& volumePath);

VOLUMEPROTECT_API uint64_t ReadVolumeSize(const std::string& volumePath);

VOLUMEPROTECT_API uint32_t ProcessorsNum();
}
};

#endif