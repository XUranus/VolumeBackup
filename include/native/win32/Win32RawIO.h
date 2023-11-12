/**
 * @file Win32RawIO.h
 * @brief Windows volume block level I/O API and Virtualdisk API wrapper.
 * @copyright Copyright 2023 XUranus. All rights reserved.
 * @license This project is released under the Apache License.
 * @author XUranus(2257238649wdx@gmail.com)
 */

#ifndef VOLUMEBACKUP_NATIVE_Win32_RAW_IO_HEADER
#define VOLUMEBACKUP_NATIVE_Win32_RAW_IO_HEADER

#ifdef _WIN32

#include <locale>
#include <codecvt>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef UNICODE
#define UNICODE /* foring using WCHAR on windows */
#endif

#ifndef NOGDI
#define NOGDI
#endif

#include <Windows.h>
#include <winioctl.h>

#include "common/VolumeProtectMacros.h"
#include "RawIO.h"

// define customed win32 error codes, starts from 0x80600000
#define ERROR_VOLUMEBBACKUP_TOO_LARGE_VOLUME ((DWORD)0x80600000 | 0x114514)

// Raw I/O Reader/Writer for win32 subsystem using WIN32 API
namespace volumeprotect {
namespace rawio {
namespace win32 {

// Win32RawDataReader can read from any block device or common file at given offset
class Win32RawDataReader : public RawDataReader {
public:
    Win32RawDataReader(const std::string& path, int flag = 0, uint64_t shiftOffset = 0);
    ~Win32RawDataReader();
    bool Read(uint64_t offset, uint8_t* buffer, int length, ErrCodeType& errorCode) override;
    bool Ok() override;
    ErrCodeType Error() override;

private:
    HANDLE m_handle { INVALID_HANDLE_VALUE };
    int m_flag { 0 };
    uint64_t m_shiftOffset { 0 };
};

// Win32RawDataWriter can write to any block device or common file at give offset
class Win32RawDataWriter : public RawDataWriter {
public:
    Win32RawDataWriter(const std::string& path, int flag = 0, uint64_t shiftOffset = 0);
    ~Win32RawDataWriter();
    bool Write(uint64_t offset, uint8_t* buffer, int length, ErrCodeType& errorCode) override;
    bool Ok() override;
    bool Flush() override;
    ErrCodeType Error() override;

private:
    HANDLE m_handle { INVALID_HANDLE_VALUE };
    int m_flag { 0 };
    uint64_t m_shiftOffset { 0 };
};

/*
 * Win32VirtualDiskVolumeRawDataReader can read from attached VHD/VHDX file at given offset
 * the VHD/VHDX file must be created and it's partition must be inited
 */
class Win32VirtualDiskVolumeRawDataReader : public RawDataReader {
public:
    Win32VirtualDiskVolumeRawDataReader(const std::string& virtualDiskFilePath, bool autoDetach = true);
    ~Win32VirtualDiskVolumeRawDataReader();
    bool Read(uint64_t offset, uint8_t* buffer, int length, ErrCodeType& errorCode) override;
    bool Ok() override;
    ErrCodeType Error() override;

private:
    std::shared_ptr<Win32RawDataReader> m_volumeReader { nullptr };
    std::string m_virtualDiskFilePath;
    bool m_autoDetach { true };
};

/*
 * Win32VirtualDiskVolumeRawDataWriter can write to attached VHD/VHDX file at given offset
 * the VHD/VHDX file must be created and it's partition must be inited
 */
class Win32VirtualDiskVolumeRawDataWriter : public RawDataWriter {
public:
    Win32VirtualDiskVolumeRawDataWriter(const std::string& virtualDiskFilePath, bool autoDetach = true);
    ~Win32VirtualDiskVolumeRawDataWriter();
    bool Write(uint64_t offset, uint8_t* buffer, int length, ErrCodeType& errorCode) override;
    bool Ok() override;
    bool Flush() override;
    ErrCodeType Error() override;

private:
    std::shared_ptr<Win32RawDataWriter> m_volumeWriter { nullptr };
    std::string m_virtualDiskFilePath;
    bool m_autoDetach { true };
};

bool CreateFixedVHDFile(
    const std::string& filePath,
    uint64_t volumeSize,
    ErrCodeType& errorCode);

bool CreateFixedVHDXFile(
    const std::string& filePath,
    uint64_t volumeSize,
    ErrCodeType& errorCode);

bool CreateDynamicVHDFile(
    const std::string& filePath,
    uint64_t volumeSize,
    ErrCodeType& errorCode);

bool CreateDynamicVHDXFile(
    const std::string& filePath,
    uint64_t volumeSize,
    ErrCodeType& errorCode);

bool VirtualDiskAttached(const std::string& virtualDiskFilePath);

bool GetVirtualDiskPhysicalDrivePath(
    const std::string&  virtualDiskFilePath,
    std::string&        physicalDrivePath,
    ErrCodeType&        errorCode);

bool AttachVirtualDiskCopy(
    const std::string&  virtualDiskFilePath,
    ErrCodeType&        errorCode);

bool DetachVirtualDiskCopy(
    const std::string&  virtualDiskFilePath,
    ErrCodeType&        errorCode);

bool InitVirtualDiskGPT(
    const std::string& physicalDrivePath,
    uint64_t volumeSize,
    ErrCodeType& errorCode);

// Get volume path (\\.\HarddiskVolumeX) of the first partition in physicalDrivePath of attached virtual disk
bool GetCopyVolumeDevicePath(
    const std::string& physicalDrivePath,
    std::string& volumeDevicePath,
    ErrCodeType& errorCode);

// Get volume name in form of "\\?\Volume{GUID}\" from volume device path (\\.\HarddiskVolumeX)
bool GetVolumeGuidNameByVolumeDevicePath(
    const std::string& volumeDevicePath,
    std::string& volumeGuidName,
    ErrCodeType& errorCode);

// assign a driver path (X:\ or X:\dirX\dirXX\) to a volume
bool AddVolumeMountPoint(
    const std::string& volumeGuidName,
    const std::string& mountPoint,
    ErrCodeType& errorCode);

}
}
}

#endif
#endif