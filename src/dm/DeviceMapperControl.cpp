#include "DeviceMapperControl.h"
#include <cstdint>
#include <fcntl.h>
#include <string>
#include <cstring>
#include <linux/dm-ioctl.h>
#include <sys/ioctl.h>

using namespace volumebackup;
using namespace volumebackup::devicemapper;

namespace {
    // The minimum expected device mapper major.minor version
    const int DM_VERSION0 = 4;
    const int DM_VERSION1 = 0;
    const int DM_VERSION2 = 0;
    // device mapper require 8 byte padding
    const int DM_ALIGN_MASK = 7;
    const std::string DEVICE_MAPPER_CONTROL_PATH = "/dev/mapper/control";
};

static int DM_ALIGN(int x)
{
    return (((x) + DM_ALIGN_MASK) & ~DM_ALIGN_MASK);
}

// implement DmTarget
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
    // Finally fill in the dm_target_spec.
    struct dm_target_spec* spec = reinterpret_cast<struct dm_target_spec*>(&data[0]);
    spec->sector_start = StartSector();
    spec->length = SectorsCount();
    snprintf(spec->target_type, sizeof(spec->target_type), "%s", Name().c_str());
    spec->next = (uint32_t)data.size();
    return data;
}

// implement DmTargetLinear
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

static bool CreateDevice(const std::string& name)
{
    // TODO
    return false;
}

// create empty dm device with specified uuid
static bool CreateDevice(const std::string& name, const std::string& uuid)
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
    int dmControlFd = GetDmControlFd();
    if (dmControlFd < 0) {
        return false;
    }
    if (::ioctl(dmControlFd, DM_DEV_CREATE, &io)) {
        return false;
    }
    // Check to make sure the newly created device doesn't already have targets added or opened by someone
    if (io.target_count == 0 || io.open_count == 0) {
        return false;
    }
    return true;
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
    // TODO
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

static bool WaitForDevice(const std::string& name, const std::string& path)
{
    // TODO
    return false;
}

bool CreateDevice(
    const std::string& name,
    const DmTable& dmTable,
    std::string& path)
{
    if (!CreateDevice(name)) {
        return false;
    }
    //defer(DeleteDevice(name);)
    bool activate = true;
    if (!LoadTable(name, dmTable, activate)) {
        return false;
    }
    if (!WaitForDevice(name, path)) {
        return false;
    }
    return true;
}