#ifndef VOLUMEBACKUP_TASK_RESOURCE_MANAGER_HEADER
#define VOLUMEBACKUP_TASK_RESOURCE_MANAGER_HEADER

#include "VolumeProtectMacros.h"
#include "VolumeProtector.h"
#include "VolumeProtectTaskContext.h"
#include "VolumeUtils.h"

namespace volumeprotect {

/**
 * TaskResourceManager is used to prepare resource for Backup/Restore tasks, including:
 *  Create copy file for backup
 *  Init virtual disk partition info for Backup
 *  Attach virtual disk for Backup & Restore
 *  Detach virtual disk for Backup & Restore (when destroyed)
 **/

struct BackupTaskResourceManagerParams {
    CopyFormat          copyFormat;
    std::string         copyDataDirPath;
    std::string         copyName;
    uint64_t            volumeSize;
    uint64_t            maxSessionSize;     // only used to create fragment copy for CopyFormat::BIN
};

struct RestoreTaskResourceManagerParams {
    CopyFormat          copyFormat;
    std::string         copyDataDirPath;
    std::string         copyName;
};

class TaskResourceManager {
public:
    static std::unique_ptr<TaskResourceManager> BuildBackupTaskResourceManager(
        const BackupTaskResourceManagerParams& params);

    static std::unique_ptr<TaskResourceManager> BuildRestoreTaskResourceManager(
        const RestoreTaskResourceManagerParams& params);

    TaskResourceManager(
        CopyFormat copyFormat,
        const std::string& copyDataDirPath,
        const std::string& copyName);

    virtual ~TaskResourceManager() = default;

    virtual bool PrepareResource();

protected:
    virtual bool AttachCopyResource();

    virtual bool DetachCopyResource();

protected:
    CopyFormat          m_copyFormat;
    std::string         m_copyDataDirPath;
    std::string         m_copyName;

    std::string         m_physicalDrivePath;
};

class BackupTaskResourceManager : public TaskResourceManager {
public:
    BackupTaskResourceManager(const BackupTaskResourceManagerParams& param);

    ~BackupTaskResourceManager();

    bool PrepareResource() override;
private:
    // Create and Init operation is only need for backup
    bool CreateBackupCopyResource();

    bool InitBackupCopyResource();

private:
    uint64_t            m_volumeSize;
    uint64_t            m_maxSessionSize;     // only used to create fragment copy for CopyFormat::BIN
};


class RestoreTaskResourceManager : public TaskResourceManager {
public:
    RestoreTaskResourceManager(const RestoreTaskResourceManagerParams& param);

    ~RestoreTaskResourceManager();

    bool PrepareResource() override;
};

 

}

#endif