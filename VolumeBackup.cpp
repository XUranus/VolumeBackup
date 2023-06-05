#include <exception>
#include <queue>
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

bool SplitFullBackupTask(
	const std::string& 	blockDevicePath,
	const std::string&	outputCopyDataDirPath,
	const std::string&	outputCopyMetaDirPath)
{
    VolumePartitionTableEntry partitionEntry {};
    uint64_t volumeSize = 0;
    VolumeBackupContext context {};

    uint64_t sessionSize = ONE_TB;
    std::queue<VolumeBackupTask> taskQueue;

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
    for (uint64_t sessionOffset = 0; sessionOffset < volumeSize; sessionOffset += sessionSize) {
        VolumeBackupTask task {
            blockDevicePath,
            sessionOffset,
            sessionSize,
            checksumBinPath,
            copyFilePath,
            copyFileMappingOffset
        };
        taskQueue.push(task);
    }

    // 3. start task
    auto reader = VolumeBlockReader::BuildVolumeReader(
        blockDevicePath,
        sessionOffset,
        sessionSize,
        context
    );

    auto hasher = VolumeBlockHasher::BuildDirectHasher(
        context,
        checksumBinPath
    );

    auto writer = VolumeBlockWriter::BuildCopyWriter(
        copyFilePath,
        context
    );

    if (reader == nullptr || hasher == nullptr || writer == nullptr) {
        return false;
    }

    if (!reader->Start() || hasher->Start() || writer->Start()) {
        return false;
    }
      
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