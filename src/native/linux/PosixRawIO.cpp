#ifdef __linux__

#include <cerrno>
#include <cstdio>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <linux/fs.h>
#include <unistd.h>
#include <dirent.h>

#include "PosixRawIO.h"

namespace {
    const int INVALID_POSIX_FD_VALUE = -1;
}

using namespace rawio;

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
        errorCode = static_cast<ErrCodeType>(errno);;
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
        errorCode = static_cast<ErrCodeType>(errno);;
        return false;
    }
    return true;
}

bool PosixRawDataWriter::Ok()
{
    return m_fd > 0;
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

bool native::TruncateCreateFile(const std::string& path, uint64_t size, ErrCodeType& errorCode)
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

static bool rawio::TruncateCreateFile(const std::string& path, uint64_t size, ErrCodeType& errorCode)
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