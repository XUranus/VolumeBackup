/**
 * @file TaskResourceManager.h
 * @brief Provide a set of interface to manage attachment and detachment for copy resource.
 * @copyright Copyright 2023-2024 XUranus. All rights reserved.
 * @license This project is released under the Apache License.
 * @author XUranus(2257238649wdx@gmail.com)
 */

#ifndef VOLUMEBACKUP_TASK_RESOURCE_MANAGER_HEADER
#define VOLUMEBACKUP_TASK_RESOURCE_MANAGER_HEADER

#include "VolumeProtector.h"

namespace volumeprotect {
namespace task {

/**
 * @brief Params struct used to build BackupTaskResourceManager
 */
struct BackupTaskResourceManagerParams {
    CopyFormat          copyFormat;
    BackupType          backupType;
    std::string         copyDataDirPath;
    std::string         copyName;
    uint64_t            volumeSize;
    uint64_t            maxSessionSize;     ///< only used to create fragment copy for CopyFormat::BIN
};

/**
 * @brief Params struct used to build RestoreTaskResourceManager
 */
struct RestoreTaskResourceManagerParams {
    CopyFormat                  copyFormat;
    std::string                 copyDataDirPath;
    std::string                 copyName;
    std::vector<std::string>    copyDataFiles;
};

/**
 * @brief Base class for BackupTaskResourceManager and RestoreTaskResourceManager.
 * Provide TaskResourceManager builder and RAII resource management.
 * PrepareCopyResource() need to be invoked before backup/restore task start.
 * TaskResourceManager is used to prepare resource for Backup/Restore tasks, including:
 *  1. Create copy file on disk for backup
 *  2. Init virtual disk partition info for Backup
 *  3. Attach virtual disk for Backup & Restore
 *  4. Detach virtual disk for Backup & Restore (when destroyed)
 **/
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

    virtual bool PrepareCopyResource() = 0;

protected:
    virtual bool AttachCopyResource();

    virtual bool DetachCopyResource();

    virtual bool ResourceExists() = 0;

protected:
    CopyFormat          m_copyFormat;
    std::string         m_copyDataDirPath;
    std::string         m_copyName;
    // mutable
    std::string         m_physicalDrivePath;
};

// BackupTaskResourceManager is inited before backup task start
class BackupTaskResourceManager : public TaskResourceManager {
public:
    explicit BackupTaskResourceManager(const BackupTaskResourceManagerParams& param);

    ~BackupTaskResourceManager();

    bool PrepareCopyResource() override;

private:
    // Create and Init operation is only need for backup
    bool CreateBackupCopyResource();

    bool InitBackupCopyResource();

    bool ResourceExists() override;

private:
    BackupType          m_backupType;
    uint64_t            m_volumeSize;
    uint64_t            m_maxSessionSize;     // only used to create fragment copy for CopyFormat::BIN
};

// RestoreTaskResourceManager is inited before restore task start
class RestoreTaskResourceManager : public TaskResourceManager {
public:
    explicit RestoreTaskResourceManager(const RestoreTaskResourceManagerParams& param);

    ~RestoreTaskResourceManager();

    bool PrepareCopyResource() override;

protected:
    bool ResourceExists() override;

private:
    std::vector<std::string> m_copyDataFiles;
};


}
}

#endif