#ifndef VOLUMEBACKUP_NATIVE_RAW_IO_HEADER
#define VOLUMEBACKUP_NATIVE_RAW_IO_HEADER

#include "VolumeProtectMacros.h"

/**
 * @brief this module is used to shield native I/O interface differences to provide a unified I/O layer
 */
namespace volumeprotect {
namespace rawio {

#ifdef _WIN32
using ErrCodeType = DWORD;
#endif

#ifdef __linux__
using ErrCodeType = int;
#endif

/**
 * @brief RawDataReader and RawDataWriter provide basic raw I/O interface
 *  for VolumeDataReader/VolumeDataWriter, FileDataReader/FileDataWriter to implement.
 *  Implement this interface if need to access other data source, ex: cloud, tape ...
 *
 */
class RawDataReader {
public:
    virtual bool Read(uint64_t offset, uint8_t* buffer, int length, ErrCodeType& errorCode) = 0;
    virtual bool Ok() = 0;
    virtual ErrCodeType Error() = 0;
    virtual ~DataReader() = default;
};

class DataWriter {
public:
    virtual bool Write(uint64_t offset, uint8_t* buffer, int length, ErrCodeType& errorCode) = 0;
    virtual bool Ok() = 0;
    virtual bool Flush() = 0;
    virtual ErrCodeType Error() = 0;
    virtual ~DataWriter() = default;
};

enum class RAW_IO_FACTORY_TYPE_ENUM {
    POSIX_FILE      = 0,
    POSIX_DEVICE    = 1,
    WIN32_FILE      = 2,
    WIN32_DEVICE    = 3
};


static bool PrepareTargetCopy(
    VolumeBackupConfig
    CopyFormat          copyFormat,
    uint64_t            volumeSize,
    const std::string&  copyDataDirPath,
    const std::string&  copyName);
    
static std::unique_ptr<DataReader> OpenRawDataReader(RAW_IO_FACTORY_ENUM readerType, const std::string& path);
static std::unique_ptr<DataWriter> CreateRawDataWriter(RAW_IO_FACTORY_ENUM writerType, const std::string& path);


}
};

#endif