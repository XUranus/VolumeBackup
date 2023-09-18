#ifndef VOLUMEBACKUP_NATIVE_RAW_IO_HEADER
#define VOLUMEBACKUP_NATIVE_RAW_IO_HEADER

#include "VolumeProtectMacros.h"
#include "VolumeProtector.h"
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
    virtual ~RawDataReader() = default;
};

class RawDataWriter {
public:
    virtual bool Write(uint64_t offset, uint8_t* buffer, int length, ErrCodeType& errorCode) = 0;
    virtual bool Ok() = 0;
    virtual bool Flush() = 0;
    virtual ErrCodeType Error() = 0;
    virtual ~RawDataWriter() = default;
};

// param struct to build RawDataReader/RawDataWriter for each backup restore session
// to read/write from/to copyfile
struct SessionCopyRawIOParam {
    CopyFormat          copyFormat;
    std::string         copyFilePath;
    uint64_t            volumeOffset;
    uint64_t            length;
};

bool PrepareBackupCopy(const VolumeBackupConfig& config, uint64_t volumeSize);
    
std::shared_ptr<RawDataReader> OpenRawDataCopyReader(const SessionCopyRawIOParam& param);

std::shared_ptr<RawDataWriter> OpenRawDataCopyWriter(const SessionCopyRawIOParam& param);

std::shared_ptr<RawDataReader> OpenRawDataVolumeReader(const std::string& volumePath);

std::shared_ptr<RawDataWriter> OpenRawDataVolumeWriter(const std::string& volumePath);

// implementation depend on OS platform
bool TruncateCreateFile(const std::string& path, uint64_t size, ErrCodeType& errorCode);

}
};

#endif