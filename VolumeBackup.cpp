#include <exception>
#include <sys/types.h>
#include <vector>

#include "VolumeBackupContext.h"
#include "VolumeBackupUtils.h"
#include "VolumeBlockReader.h"
#include "VolumeBlockHasher.h"
#include "VolumeBlockWriter.h"

#include "VolumeBackup.h"

using namespace volumebackup;
using namespace volumebackup::util;

/*
 * backup copy folder herichical
 * 1. Full Copy
 *  ${CopyID}
 *       |
 *      ${UUID}
 *          |------data
 *          |       |------0.1024.data.full.bin
 *          |       |------0.1024.data.full.bin
 *          |       |------0.1024.data.full.bin
 *          |
 *          |------meta
 *                  |------fullcopy.meta.json
 *                  |------0.1024.sha256.bin
 *                  |------0.1024.sha256.bin
 *                  |------0.1024.sha256.bin
 *
 * 2. Increment Copy
 *  ${CopyID}
 *       |
 *      ${UUID}
 *          |------data
 *          |       |------0.1024.data.full.bin
 *          |       |------0.1024.data.full.bin
 *          |       |------0.1024.data.full.bin
 *          |
 *          |------meta
 *                  |------incrementcopy.meta.json
 *                  |------0.1024.sha256.bin
 *                  |------0.1024.sha256.bin
 *                  |------0.1024.sha256.bin
 */

bool StartFullBackupTask(
	const std::string& 	blockDevicePath,
	const std::string&	outputCopyDataDirPath,
	const std::string&	outputCopyMetaDirPath)
{
    VolumePartitionTableEntry partitionEntry {};
    uint64_t volumeSize = 0;
    VolumeBackupContext context {};

    // 1. check volume and retrive metadata
    try {
        volumeSize = util::ReadVolumeSize(blockDevicePath);
        std::vector<VolumePartitionTableEntry> partitionTable = util::ReadVolumePartitionTable(blockDevicePath);
        if (partitionTable.size() != 1) {
            // TODO:: err
            return false;
        }
        partitionEntry = partitionTable.back();
    } catch (std::exception& e) {
        // TODO:: err e.what()
        return false;
    }

    // 2. split session

    // 3. start task
    
    auto reader = VolumeBlockReader::BuildCopyReader(
        const std::string &copyFilePath,
        uint64_t size,
        std::shared_ptr<VolumeBackupContext> context)
    return true;    
}

bool StartIncrementBackupTask(
	const std::string& 	blockDevicePath,
    const std::string&	prevCopyDataDirPath,
	const std::string&	prevCopyMetaDirPath,
	const std::string&	outputCopyDataDirPath,
	const std::string&	outputCopyMetaDirPath)
{
    return true;    
}

bool StartRestoreTask(
	const std::string&	copyDataDirPath,
	const std::string&	copyMetaDirPath,
    const std::string& 	blockDevicePath)
{
    return true;    
}