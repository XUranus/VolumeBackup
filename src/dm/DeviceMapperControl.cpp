#include "DeviceMapperControl.h"
#include <string>
#include <linux/dm-ioctl.h>

using namespace volumebackup;
using namespace volumebackup::devicemapper;

namespace {
    const std::string DEVICE_MAPPER_CONTROL_PATH = "/dev/mapper/control";
};

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
    size_t padding = data.size() % 8;
    for (size_t i = 0; i < padding; i++) {
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

// implement DeviceMapper
