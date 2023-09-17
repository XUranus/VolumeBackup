#ifndef VOLUMEBACKUP_NATIVE_Win32_RAW_IO_HEADER
#define VOLUMEBACKUP_NATIVE_Win32_RAW_IO_HEADER

#include "VolumeProtectMacros.h"
#include "RawIO.h"

// Raw I/O Reader/Writer for win32 subsystem using WIN32 API

namespace rawio {

// Win32RawDataReader can read from any block device or common file at given offset
class Win32RawDataReader : public RawDataReader {
public:
    explicit Win32RawDataReader(const std::string& path, int flag = 0, uint64_t shiftOffset = 0);
    ~Win32RawDataReader();
    bool Read(uint64_t offset, uint8_t* buffer, int length, ErrCodeType& errorCode) override;
    bool Ok() override;
    ErrCodeType Error() override;

private:
    int m_fd { INVALID_HANDLE_VALUE };
    int m_flag { 0 };
    uint64_t m_shiftOffset { 0 };
};

// Win32RawDataWriter can write to any block device or common file at give offset
class Win32RawDataWriter : public RawDataWriter {
public:
    explicit Win32RawDataWriter(const std::string& path, int flag = 0, uint64_t shiftOffset = 0);
    ~Win32RawDataWriter();
    bool Write(uint64_t offset, uint8_t* buffer, int length, ErrCodeType& errorCode) override;
    bool Ok() override;
    bool Flush() override;
    ErrCodeType Error() override;

private:
    int m_fd { INVALID_HANDLE_VALUE };
    int m_flag { 0 };
    uint64_t m_shiftOffset { 0 };
};

static bool TruncateCreateFile(const std::string& path, uint64_t size, ErrCodeType& errorCode);

static bool FastCreateFixedVHDFile();

static bool CreateFixedVHDFile();



}

#endif