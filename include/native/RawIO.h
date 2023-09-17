#ifndef VOLUMEBACKUP_NATIVE_RAW_IO_HEADER
#define VOLUMEBACKUP_NATIVE_RAW_IO_HEADER

#include "VolumeProtectMacros.h"
#include <string>

/**
 * @brief this module is used to shield native I/O interface differences to provide a unified I/O layer
 */
namespace volumeprotect {
namespace rawio {

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

// param struct to build RawDataReader/RawDataWriter for each backup restore session
// to read/write from/to copyfile
struct SessionCopyRawIOParam {
    CopyFormat          copyFormat;
    std::string         copyFilePath;
    uint64_t            volumeOffset;
    uint64_t            length;
};

static bool PrepareBackupCopy(
    CopyFormat          copyFormat,
    uint64_t            volumeSize,
    const std::string&  copyDataDirPath,
    const std::string&  copyName);
    
static std::unique_ptr<RawDataReader> OpenRawDataCopyReader(const SessionCopyRawIOParam& param);

static std::unique_ptr<RawDataWriter> OpenRawDataCopyWriter(const SessionCopyRawIOParam& param);

static std::unique_ptr<RawDataReader> OpenRawDataVolumeReader(const std::string& volumePath);

static std::unique_ptr<RawDataWriter> OpenRawDataVolumeWriter(const std::string& volumePath);

// implementation depend on OS platform
static bool TruncateCreateFile(const std::string& path, uint64_t size, ErrCodeType& errorCode);

}
};

#endif