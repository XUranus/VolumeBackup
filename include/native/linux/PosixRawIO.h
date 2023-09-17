#ifndef VOLUMEBACKUP_NATIVE_POSIX_RAW_IO_HEADER
#define VOLUMEBACKUP_NATIVE_POSIX_RAW_IO_HEADER

#include "VolumeProtectMacros.h"
#include "RawIO.h"

// Raw I/O Reader/Writer for *unix platform posix API implementation

namespace rawio {

// PosixRawDataReader can read from any block device or common file at given offset
class PosixRawDataReader : public RawDataReader {
public:
    explicit PosixRawDataReader(const std::string& path, int flag = 0, uint64_t shiftOffset = 0);
    ~PosixRawDataReader();
    bool Read(uint64_t offset, uint8_t* buffer, int length, ErrCodeType& errorCode) override;
    bool Ok() override;
    ErrCodeType Error() override;

private:
    int m_fd {};
    int m_flag { 0 };
    uint64_t m_shiftOffset { 0 };
};

// PosixRawDataWriter can write to any block device or common file at give offset
class PosixRawDataWriter : public RawDataWriter {
public:
    explicit PosixRawDataWriter(const std::string& path, int flag = 0, uint64_t shiftOffset = 0);
    ~PosixRawDataWriter();
    bool Write(uint64_t offset, uint8_t* buffer, int length, ErrCodeType& errorCode) override;
    bool Ok() override;
    bool Flush() override;
    ErrCodeType Error() override;

private:
    int m_fd {};
    int m_flag { 0 };
    uint64_t m_shiftOffset { 0 };
};

static bool TruncateCreateFile(const std::string& path, uint64_t size, ErrCodeType& errorCode);

}

#endif