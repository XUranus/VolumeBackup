/**
 * @file PosixRawIO.h
 * @brief Linux volume block level I/O API.
 * @copyright Copyright 2023-2024 XUranus. All rights reserved.
 * @license This project is released under the Apache License.
 * @author XUranus(2257238649wdx@gmail.com)
 */

#ifndef VOLUMEBACKUP_NATIVE_POSIX_RAW_IO_HEADER
#define VOLUMEBACKUP_NATIVE_POSIX_RAW_IO_HEADER

#include "common/VolumeProtectMacros.h"

#ifdef POSIXAPI

#include "RawIO.h"

// Raw I/O Reader/Writer for *unix platform posix API implementation
namespace volumeprotect {
namespace rawio {
namespace posix {

// PosixRawDataReader can read from any block device or common file at given offset
class PosixRawDataReader : public RawDataReader {
public:
    PosixRawDataReader(const std::string& path, int flag = 0, uint64_t shiftOffset = 0);
    ~PosixRawDataReader();
    bool Read(uint64_t offset, uint8_t* buffer, int length, ErrCodeType& errorCode) override;
    bool Ok() override;
    HandleType Handle() override;
    ErrCodeType Error() override;

private:
    int m_fd {};
    int m_flag { 0 };
    uint64_t m_shiftOffset { 0 };
};

// PosixRawDataWriter can write to any block device or common file at give offset
class PosixRawDataWriter : public RawDataWriter {
public:
    PosixRawDataWriter(const std::string& path, int flag = 0, uint64_t shiftOffset = 0);
    ~PosixRawDataWriter();
    bool Write(uint64_t offset, uint8_t* buffer, int length, ErrCodeType& errorCode) override;
    bool Ok() override;
    HandleType Handle() override;
    bool Flush() override;
    ErrCodeType Error() override;

private:
    int m_fd {};
    int m_flag { 0 };
    uint64_t m_shiftOffset { 0 };
};

}
}
}

#endif
#endif