#ifdef _WIN32

#ifndef VOLUMEBACKUP_NATIVE_Win32_RAW_IO_HEADER
#define VOLUMEBACKUP_NATIVE_Win32_RAW_IO_HEADER

#include "VolumeProtectMacros.h"
#include "RawIO.h"
#include <cstdint>

// Raw I/O Reader/Writer for win32 subsystem using WIN32 API

namespace volumeprotect {
namespace rawio {
namespace win32 {

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

/*
 * Win32VirtualDiskVolumeRawDataReader can read from attached VHD/VHDX file at given offset
 * the VHD/VHDX file must be created and it's partition must be inited
 */
class Win32VirtualDiskVolumeRawDataReader : public RawDataReader {
public:
    explicit Win32VirtualDiskVolumeRawDataReader(const std::string& virtualDiskFilePath, bool autoDetach = true);
    ~Win32VirtualDiskVolumeRawDataReader();
    bool Read(uint64_t offset, uint8_t* buffer, int length, ErrCodeType& errorCode) override;
    bool Ok() override;
    ErrCodeType Error() override;

private:
    int m_hVolume { INVALID_HANDLE_VALUE };     // handle to the volume device
    bool autoDetach { true };
};

/*
 * Win32VirtualDiskVolumeRawDataWriter can write to attached VHD/VHDX file at given offset
 * the VHD/VHDX file must be created and it's partition must be inited
 */
class Win32VirtualDiskVolumeRawDataWriter : public RawDataWriter {
public:
    explicit Win32VirtualDiskVolumeRawDataWriter(const std::string& path, bool autoDetach = true);
    ~Win32VirtualDiskVolumeRawDataWriter();
    bool Write(uint64_t offset, uint8_t* buffer, int length, ErrCodeType& errorCode) override;
    bool Ok() override;
    bool Flush() override;
    ErrCodeType Error() override;

private:
    int m_hVolume { INVALID_HANDLE_VALUE };     // handle to the volume device
    bool autoDetach { true };
};

static bool CreateFixedVHDFile(const std::string& filePath, uint64_t volumeSize, ErrCodeType& errorCode);

static bool CreateFixedVHDXFile(const std::string& filePath, uint64_t volumeSize, ErrCodeType& errorCode);

static bool CreateDynamicVHDFile(const std::string& filePath, uint64_t volumeSize, ErrCodeType& errorCode);

static bool CreateDynamicVHDXFile(const std::string& filePath, uint64_t volumeSize, ErrCodeType& errorCode);

static bool InitVirtualDiskGPT(const std::string& filePath, uint64_t volumeSize, ErrCodeType& errorCode);

static bool AttachVirtualDisk(
    const std::string&  virtualDiskFilePath,
    std::string&        mountedDevicePath,
    ErrorCodeType&      errorCode);

}
}
}

#endif
#endif