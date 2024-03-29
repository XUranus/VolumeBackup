/**
 * @copyright Copyright 2023-2024 XUranus. All rights reserved.
 * @license This project is released under the Apache License.
 * @author XUranus(2257238649wdx@gmail.com)
 */

#include "RawIO.h"
#include "common/VolumeProtectMacros.h"

#ifdef POSIXAPI

#include <cerrno>
#include <cstdio>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <dirent.h>

#include "linux/PosixRawIO.h"

namespace {
    const int INVALID_POSIX_FD_VALUE = -1;
}

using namespace volumeprotect;
using namespace volumeprotect::rawio;
using namespace volumeprotect::rawio::posix;

PosixRawDataReader::PosixRawDataReader(const std::string& path, int flag, uint64_t shiftOffset)
    : m_flag(flag), m_shiftOffset(shiftOffset)
{
    m_fd = ::open(path.c_str(), O_RDONLY);
}

bool PosixRawDataReader::Read(uint64_t offset, uint8_t* buffer, int length, ErrCodeType& errorCode)
{
    if (m_flag > 0) {
        offset += m_shiftOffset;
    } else if (m_flag < 0) {
        offset -= m_shiftOffset;
    }
    ::lseek(m_fd, offset, SEEK_SET);
    int ret = ::read(m_fd, buffer, length);
    if (ret <= 0 || ret != length) {
        errorCode = static_cast<ErrCodeType>(errno);
        return false;
    }
    return true;
}

bool PosixRawDataReader::Ok()
{
    return m_fd > 0;
}

ErrCodeType PosixRawDataReader::Error()
{
    return static_cast<ErrCodeType>(errno);
}

HandleType PosixRawDataReader::Handle()
{
    return Ok() ? m_fd : -1;
}

PosixRawDataReader::~PosixRawDataReader()
{
    if (m_fd < 0) {
        return;
    }
    ::close(m_fd);
    m_fd = INVALID_POSIX_FD_VALUE;
}

PosixRawDataWriter::PosixRawDataWriter(const std::string& path, int flag, uint64_t shiftOffset)
    : m_flag(flag), m_shiftOffset(shiftOffset)
{
    m_fd = ::open(path.c_str(), O_RDWR | O_EXCL, S_IRUSR | S_IWUSR);
}

bool PosixRawDataWriter::Write(uint64_t offset, uint8_t* buffer, int length, ErrCodeType& errorCode)
{
    if (m_flag > 0) {
        offset += m_shiftOffset;
    } else if (m_flag < 0) {
        offset -= m_shiftOffset;
    }
    ::lseek(m_fd, offset, SEEK_SET);
    int ret = ::write(m_fd, buffer, length);
    if (ret <= 0 || ret != length) {
        errorCode = static_cast<ErrCodeType>(errno);
        return false;
    }
    return true;
}

bool PosixRawDataWriter::Ok()
{
    return m_fd > 0;
}

HandleType PosixRawDataWriter::Handle()
{
    return Ok() ? m_fd : -1;
}

bool PosixRawDataWriter::Flush()
{
    if (!Ok()) {
        return false;
    }
    ::fsync(m_fd);
    return true;
}

ErrCodeType PosixRawDataWriter::Error()
{
    return static_cast<ErrCodeType>(errno);
}

PosixRawDataWriter::~PosixRawDataWriter()
{
    if (m_fd < 0) {
        return;
    }
    ::close(m_fd);
    m_fd = INVALID_POSIX_FD_VALUE;
}

bool volumeprotect::rawio::TruncateCreateFile(
    const std::string&  path,
    uint64_t            size,
    ErrCodeType&        errorCode)
{
    int fd = ::open(path.c_str(), O_RDWR | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR);
    if (fd < 0) {
        return false;
    }
    ::close(fd);
    if (::truncate(path.c_str(), size) < 0) {
        errorCode = static_cast<ErrCodeType>(errno);
        return false;
    }
    return true;
}

#endif