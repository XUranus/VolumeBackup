#ifndef VOLUMEBACKUP_DM_DEVICE_MAPPER_CONTROL_H
#define VOLUMEBACKUP_DM_DEVICE_MAPPER_CONTROL_H

#include "VolumeProtectMacros.h"

namespace volumeprotect {
namespace devicemapper {

/**
 * reference url:
 * https://android.googlesource.com/platform/system/core/+/refs/heads/main/fs_mgr/libdm/include/libdm/dm_target.h
 */
class VOLUMEPROTECT_API DmTarget {
public:
    DmTarget(uint64_t startSector, uint64_t sectorsCount);

    // Return the first logical sector represented by this target.
    uint64_t    StartSector() const;
    // Returns size in number of sectors when this target is part of a DmTable, return 0 otherwise.
    uint64_t    SectorsCount() const;

    // Function that converts this object to a string of arguments that can
    // be passed to the kernel for adding this target in a table. Each target (e.g. verity, linear)
    // must implement this, for it to be used on a device.
    std::string Serialize() const;

    // Return the name of the parameter
    virtual std::string Name() const = 0;
    
    // Get the parameter string that is passed to the end of the dm_target_spec
    // for this target type.
    virtual std::string GetParameterString() const = 0;
private:
    uint64_t        m_startSector;
    uint64_t        m_sectorsCount;
};

class VOLUMEPROTECT_API DmTargetLinear final : public DmTarget {
public:
    DmTargetLinear(
        const std::string& blockDevicePath,
        uint64_t startSector,
        uint64_t sectorsCount,
        uint64_t physicalSector);
    std::string     BlockDevicePath() const;
    uint64_t        PhysicalSector() const;

    std::string GetParameterString() const override;
    std::string Name() const override;
private:
    std::string     m_blockDevicePath;
    uint64_t        m_physicalSector;
};

class VOLUMEPROTECT_API DmTable {
public:
    bool    AddTarget(std::shared_ptr<DmTarget> target);
    
    // Returns the string represntation of the table that is ready to be passed into the kernel
    // as part of the DM_TABLE_LOAD ioctl.
    std::string Serialize() const;

    uint64_t TargetCount() const;
    
    void SetReadOnly();
    
    bool IsReadOnly() const;

private:
    // list of targets defined in this table sorted by their start and end sectors.
    // Note: Overlapping targets MUST never be added in this list.
    std::vector<std::shared_ptr<DmTarget>>  m_targets;

    // Total size in terms of # of sectors, as calculated by looking at the last and the first
    // target in 'm_targets'.
    uint64_t    m_sectorsCount;

    bool   m_readonly { false };
};

/*
 * reference url:
 * https://android.googlesource.com/platform/system/core/+/refs/heads/main/fs_mgr/libdm/dm.cpp
 */

// create dm device and activate with specified dm table and name, return dm device path
VOLUMEPROTECT_API bool CreateDevice(const std::string& name, const DmTable& dmTable, std::string& path);


}
}

#endif