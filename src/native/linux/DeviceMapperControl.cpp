#ifdef __linux__

#include "native/linux/DeviceMapperControl.h"

#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <fcntl.h>
#include <iostream>
#include <string>
#include <cstring>
#include <memory>
#include <thread>

#include <unistd.h>
#include <linux/dm-ioctl.h>
#include <uuid/uuid.h>
#include <sys/ioctl.h>
#include <sys/sysmacros.h>
#include <sys/types.h>
#include <sys/utsname.h>

using namespace volumeprotect;
using namespace volumeprotect::devicemapper;

namespace {
    // The minimum expected device mapper major.minor version
    const int DM_VERSION0 = 4;
    const int DM_VERSION1 = 0;
    const int DM_VERSION2 = 0;
    // device mapper require 8 byte padding
    const int DM_ALIGN_MASK = 7;
    const std::string DEVICE_MAPPER_CONTROL_PATH = "/dev/mapper/control";
    const std::string DM_DEVICE_UUID_PATH_PREFIX = "/dev/mapper/by-uuid/";
    const std::string DM_DEVICE_PATH_PREFIX = "/dev/dm-";
    const int UUID_BUFFER_MAX = 36 + 1; // 36bytes uuid with 1 \0 terminator
};

static int DM_ALIGN(int x)
{
    return (((x) + DM_ALIGN_MASK) & ~DM_ALIGN_MASK);
}

// implement DmTarget
DmTarget::DmTarget(uint64_t startSector, uint64_t sectorsCount)
    : m_startSector(startSector), m_sectorsCount(sectorsCount)
{}

uint64_t DmTarget::StartSector() const
{
    return m_startSector;
}

uint64_t DmTarget::SectorsCount() const
{
    return m_sectorsCount;
}

std::string DmTarget::Serialize() const
{
    // Create a string containing a dm_target_spec, parameter data, and an
    // explicit null terminator.
    std::string data(sizeof(dm_target_spec), '\0');
    data += GetParameterString();
    data.push_back('\0');
    // The kernel expects each target to be 8-byte aligned.
    size_t padding = DM_ALIGN(data.size()) - data.size();
    for (int i = 0; i < padding; ++i) {
        data.push_back('\0');
    }
    // Finally fill in the dm_target_spec.
    struct dm_target_spec* spec = reinterpret_cast<struct dm_target_spec*>(&data[0]);
    spec->sector_start = StartSector();
    spec->length = SectorsCount();
    snprintf(spec->target_type, sizeof(spec->target_type), "%s", Name().c_str());
    spec->next = (uint32_t)data.size();
    return data;
}

// implement DmTargetLinear
DmTargetLinear::DmTargetLinear(
    const std::string& blockDevicePath,
    uint64_t startSector,
    uint64_t sectorsCount,
    uint64_t physicalSector)
    : DmTarget(startSector, sectorsCount),
    m_blockDevicePath(blockDevicePath),
    m_physicalSector(physicalSector)
{}

std::string DmTargetLinear::Name() const
{
    return "linear";
}

std::string DmTargetLinear::GetParameterString() const
{
    return m_blockDevicePath + " " + std::to_string(m_physicalSector);
}

std::string DmTargetLinear::BlockDevicePath() const
{
    return m_blockDevicePath;
}

uint64_t DmTargetLinear::PhysicalSector() const
{
    return m_physicalSector;
}

// implement DmTable
bool DmTable::AddTarget(std::shared_ptr<DmTarget> target)
{
    // HINT:: check target valid
    m_targets.push_back(target);
    return true;
}

std::string DmTable::Serialize() const
{
    // HINT:: check target valid
    std::string tableString;
    for (const auto& target : m_targets) {
        tableString += target->Serialize();
    }
    return tableString;
}

void DmTable::SetReadOnly()
{
    m_readonly = true;
}

bool DmTable::IsReadOnly() const
{
    return m_readonly;
}

uint64_t DmTable::TargetCount() const
{
    return m_targets.size();
}

// implement DeviceMapper

static int GetDmControlFd()
{
    return ::open(DEVICE_MAPPER_CONTROL_PATH.c_str(), O_RDWR | O_CLOEXEC);
}

// init dm_ioctl struct with specified name
static void InitDmIoctlStruct(struct dm_ioctl& io, const std::string& name)
{
    memset(&io, 0, sizeof(io));
    io.version[0] = DM_VERSION0;
    io.version[1] = DM_VERSION1;
    io.version[2] = DM_VERSION2;
    io.data_size = sizeof(io);
    io.data_start = 0;
    if (!name.empty()) {
        snprintf(io.name, sizeof(io.name), "%s", name.c_str());
    }
}

// create empty dm device with specified uuid
static bool CreateEmptyDevice(const std::string& name, const std::string& uuid)
{
    if (name.empty() || uuid.empty()) {
        // create unnamed device mapper device is not supported
        return false;
    }
    if (name.size() >= DM_NAME_LEN) {
        // name too long
        return false;
    }
    struct dm_ioctl io;
    InitDmIoctlStruct(io, name);
    if (!uuid.empty()) {
        std::sprintf(io.uuid, "%s", uuid.c_str());
    }
    int dmControlFd = GetDmControlFd();
    if (dmControlFd < 0) {
        return false;
    }
    if (::ioctl(dmControlFd, DM_DEV_CREATE, &io)) {
        return false;
    }
    // Check to make sure the newly created device doesn't already have targets added or opened by someone
    if (io.target_count != 0 || io.open_count != 0) {
        return false;
    }
    return true;
}

static bool CreateEmptyDevice(const std::string& name)
{
    uuid_t generatedUuid;
    char uuidStr[UUID_BUFFER_MAX] = { 0 };  // string representation of UUID including null-terminator
    uuid_generate(generatedUuid);  // generate a new UUID
    uuid_unparse(generatedUuid, uuidStr);  // convert UUID to string
    return CreateEmptyDevice(name, uuidStr);
}

static bool LoadTable(const std::string& name, const DmTable& dmTable, bool activate)
{
    int dmControlFd = GetDmControlFd();
    if (dmControlFd < 0) {
        return false;
    }
    std::string ioctlBuffer(sizeof(struct dm_ioctl), 0);
    ioctlBuffer += dmTable.Serialize();
    struct dm_ioctl* io = reinterpret_cast<struct dm_ioctl*>(&ioctlBuffer[0]);
    InitDmIoctlStruct(*io, name);
    io->data_size = ioctlBuffer.size();
    io->data_start = sizeof(struct dm_ioctl);
    io->target_count = static_cast<uint32_t>(dmTable.TargetCount());
    if (dmTable.IsReadOnly()) {
        io->flags |= DM_READONLY_FLAG;
    }
    if (::ioctl(dmControlFd, DM_TABLE_LOAD, io)) {
        return false;
    }
    if (!activate) {
        return true;
    }
    // activate table
    struct dm_ioctl io1;
    InitDmIoctlStruct(io1, name);
    if (::ioctl(dmControlFd, DM_DEV_SUSPEND, &io1) != 0) {
        // activate table failed
        return false;
    }
    return true;
}

static bool WaitForDeviceCreated(const std::string& name, std::string& path, std::chrono::milliseconds timeout)
{
    // We use the unique path for testing whether the device is ready. After
    // that, it's safe to use the dm-N path which is compatible with callers
    // that expect it to be formatted as such.
    std::string uniquePath;
    if (!devicemapper::GetDeviceStatusUniquePath(name, uniquePath) || !devicemapper::GetDevicePathByName(name, path)) {
        devicemapper::RemoveDevice(name);
        return false;
    }

    // on some env, uuid path won't generate, so check dm device path instead
    auto tick = std::chrono::steady_clock::now();
    auto tok = std::chrono::steady_clock::now();
    do {
        if (::access(path.c_str(), F_OK) == 0) {
            return true;
        }
        if (errno != ENOENT) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        tok = std::chrono::steady_clock::now();
    } while (tok - tick < timeout);
    return false;
}

static bool WaitForDeviceRemoved(const std::string& path, const std::chrono::milliseconds timeout)
{
    auto tick = std::chrono::steady_clock::now();
    auto tok = std::chrono::steady_clock::now();
    do {
        if (::access(path.c_str(), F_OK) != 0 && errno == ENOENT) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        tok = std::chrono::steady_clock::now();
    } while (tok - tick < timeout);
    return false;
}

bool devicemapper::CreateDevice(
    const std::string& name,
    const DmTable& dmTable,
    std::string& path)
{
    if (!CreateEmptyDevice(name)) {
        return false;
    }
    bool activate = true;
    if (!LoadTable(name, dmTable, activate)) {
        RemoveDevice(name);
        return false;
    }
    if (!WaitForDeviceCreated(name, path, std::chrono::milliseconds(1000 * 10))) {
        RemoveDevice(name);
        return false;
    }
    return true;
}

bool devicemapper::RemoveDeviceIfExists(const std::string& name)
{
    if (devicemapper::GetDeviceStatus(name) == DmDeviceStatus::INVALID) {
        return true;
    }
    return RemoveDevice(name);
}

bool devicemapper::RemoveDevice(const std::string& name)
{
    if (devicemapper::GetDeviceStatus(name) == DmDeviceStatus::INVALID) {
        return true;
    }
    std::string dmDevicePath;
    if (!devicemapper::GetDevicePathByName(name, dmDevicePath)) {
        return false;
    }
    // Expect to have uevent generated if the unique path actually exists. This may not exist
    // if the device was created but has never been activated before it gets deleted.
    bool needUevent = !dmDevicePath.empty() && ::access(dmDevicePath.c_str(), F_OK) == 0;

    int dmControlFd = GetDmControlFd();
    if (dmControlFd < 0) {
        return false;
    }
    struct dm_ioctl io;
    InitDmIoctlStruct(io, name);
    if (::ioctl(dmControlFd, DM_DEV_REMOVE, &io)) {
        return false;
    }
    // Check to make sure appropriate uevent is generated so ueventd will
    // do the right thing and remove the corresponding device node and symlinks.
    if (needUevent && (io.flags & DM_UEVENT_GENERATED_FLAG) == 0) {
        // Didn't generate uevent for removal
        return false;
    }
    if (dmDevicePath.empty()) {
        return false;
    }
    if (!WaitForDeviceRemoved(dmDevicePath, std::chrono::milliseconds(1000))) {
        // Failed waiting for uniquePathto be deleted
        return false;
    }
    return true;
}

DmDeviceStatus devicemapper::GetDeviceStatus(const std::string &name)
{
    struct dm_ioctl io;
    InitDmIoctlStruct(io, name);
    int dmControlFd = GetDmControlFd();
    if (dmControlFd < 0) {
        return DmDeviceStatus::INVALID;
    }
    if (::ioctl(dmControlFd, DM_DEV_STATUS, &io) < 0) {
        return DmDeviceStatus::INVALID;
    }
    if ((io.flags & DM_ACTIVE_PRESENT_FLAG) != 0 && (io.flags & DM_SUSPEND_FLAG) == 0) {
        return DmDeviceStatus::ACTIVE;
    }
    return DmDeviceStatus::SUSPENDED;
}

bool devicemapper::GetDeviceStatusUniquePath(const std::string& name, std::string& uniquePath)
{
    struct dm_ioctl io;
    InitDmIoctlStruct(io, name);
    int dmControlFd = GetDmControlFd();
    if (dmControlFd < 0) {
        return false;
    }
    if (::ioctl(dmControlFd, DM_DEV_STATUS, &io) < 0) {
        // failed to get device path
        return false;
    }
    if (io.uuid[0] == '\0') {
        // device does not have a unique path
        return false;
    }
    uniquePath = DM_DEVICE_UUID_PATH_PREFIX + io.uuid;
    return true;
}

// Accepts a device mapper device name (like system_a, vendor_b etc) and
// returns the path to it's device node (or symlink to the device node)
bool devicemapper::GetDevicePathByName(const std::string& name, std::string& dmDevicePath)
{
    struct dm_ioctl io;
    InitDmIoctlStruct(io, name);
    int dmControlFd = GetDmControlFd();
    if (dmControlFd < 0) {
        return false;
    }
    if (::ioctl(dmControlFd, DM_DEV_STATUS, &io) < 0) {
        // DM_DEV_STATUS failed for name
        return false;
    }
    uint32_t devNum = minor(io.dev);
    dmDevicePath = DM_DEVICE_PATH_PREFIX + std::to_string(devNum);
    return true;
}

#endif