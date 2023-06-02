#include <string>
#include <iostream>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/fs.h>
#include <unistd.h>
#include <cstring>
#include <cstdint>
#include <stdexcept>
#include <fstream>
#include <memory>
#include <cassert>
#include <openssl/evp.h>

#include "PosixVolumeScanner.h"

namespace {
    const uint32_t DEFAULT_BUFFER_SIZE = 4 * 1024 * 1024; // 4MB 
}

static std::runtime_error BuildRuntimeException(
    const std::string& message,
    const std::string& blockDevice,
    uint32_t errcode)
{
    std::string label;
    label += (message + ", device = " + blockDevice + ", errno " + std::to_string(errcode));
    return std::runtime_error(label);
}

PosixVolumeScanner PosixVolumeScanner::BuildFullScanner(
    const std::string& blockDevicePath,
    const std::string& checksumOutputPath,
    uint32_t blockSize)
{
    uint64_t volumeSize = 0;
    std::string prevChecksumPath = ""; // useless in full scan

    try {
        // 1. validate volume and fetch volume size
        volumeSize = ReadVolumeSize(blockDevice)
    } catch (const std::exception& e) {
        throw e;
    }

    // 2. validate blockSize
    if (blockSize == 0 || blockSize % ONE_KB != 0 || blockSize > FOUR_MB) {
        throw std::runtime_error(std::string("invalid blocksize ") + std::to_string(blockSize));
    }

    // 3. validate checksumOutputPath


    return PosixVolumeScanner(
        blockDevicePath,
        prevChecksumPath,
        checksumOutputPath,
        blockSize,
        volumeSize
    );
}

PosixVolumeScanner PosixVolumeScanner::BuildDiffScanner(
    const std::string& blockDevicePath,
    const std::string& prevChecksumPath,
    const std::string& checksumOutputPath,
    const std::string& controlFileOutputDir,
    uint32_t blockSize
);

PosixVolumeScanner::PosixVolumeScanner(const std::string& blockDevice, const std::string& checksumOutputPath)
 : m_blockDevicePath(blockDevice), m_checksumOutputPath(checksumOutputPath)
{
    try {
        // 1. check volume size
        m_volumeSize = ReadVolumeSize(blockDevice);
    } catch (const std::exception& e) {
        throw e;
    }
}

uint64_t PosixVolumeScanner::ReadVolumeSize(const std::string& blockDevice)
{
    int fd = ::open(blockDevice.c_str(), O_RDONLY);
    if (fd < 0) {
        throw BuildRuntimeException("Error opening block device", blockDevice, errno);
        return 0;
    }

    uint64_t size = 0;
    if (::ioctl(fd, BLKGETSIZE64, &size) < 0) {
        close(fd);
        throw BuildRuntimeException("rror getting block device size", blockDevice, errno);
        return 0;
    }

    ::close(fd);
    return size;
}

PosixVolumeScanner::PosixVolumeScanner(
    const std::string& blockDevicePath,
    const std::string& prevChecksumPath,
    const std::string& checksumOutputPath,
    uint32_t blockSize,
    uint64_t volumeSize
) : m_blockDevicePath(blockDevicePath),
    m_prevChecksumPath(prevChecksumPath),
    m_checksumOutputPath(checksumOutputPath),
    m_blockSize(blockSize),
    m_volumeSize(volumeSize)
{}

bool PosixVolumeScanner::ReadVolume(uint32_t bufferSize, OnBlockCopyFunction onCopy)
{
    // Open the device file for reading
    std::ifstream deviceFile(m_blockDevicePath, std::ios::binary);
    if (!deviceFile.is_open()) {
        throw BuildRuntimeException("Failed to open the device file for read", m_blockDevicePath, errno);
        return false;
    }
    // Buffer for reading data
    auto buffer = std::make_unique<char[]>(bufferSize);
    uint64_t offset = 0;
    // Read data from the device file and write it to the image file
    while (deviceFile.read(buffer.get(), bufferSize)) {
        onCopy(offset, buffer.get(), deviceFile.gcount());
        offset += deviceFile.gcount();
    }
    // Handle the last, potentially partial read
    if (deviceFile.gcount() > 0) {
        onCopy(offset, buffer.get(), deviceFile.gcount());
        offset += deviceFile.gcount();
    }
    // Close the src device handle
    deviceFile.close();
    return 0;
}

uint64_t PosixVolumeScanner::VolumeSize() const
{
    return m_volumeSize;
}

void PosixVolumeScanner::ComputeSHA256(char* data, uint32_t len, char* output, uint32_t outputLen)
{
    EVP_MD_CTX *mdctx;
    const EVP_MD *md;
    unsigned char mdValue[EVP_MAX_MD_SIZE];
    unsigned int mdLen;

    if ((md = EVP_get_digestbyname("SHA256")) == nullptr) {
        std::cerr << "Unknown message digest SHA256" << std::endl;
        return;
    }

    if ((mdctx = EVP_MD_CTX_new()) == nullptr) {
        std::cerr << "Memory allocation failed" << std::endl;
        return;
    }

    EVP_DigestInit_ex(mdctx, md, nullptr);
    EVP_DigestUpdate(mdctx, data, len);
    EVP_DigestFinal_ex(mdctx, mdValue, &mdLen);
    assert(mdLen == outputLen);
    memcpy(output, mdValue, mdLen);
    EVP_MD_CTX_free(mdctx);
}

bool PosixVolumeScanner::UsingDiffGenerate() const
{
    return !m_prevChecksumPath.empty();
}