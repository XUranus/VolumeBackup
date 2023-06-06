#include <exception>
#include <queue>
#include <string>
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
 *          |       |------0.1024.data.bin
 *          |       |------1024.1024.data.bin
 *          |       |------2048.1024.data.bin
 *          |
 *          |------meta
 *                  |------fullcopy.meta.json
 *                  |------0.1024.sha256.bin
 *                  |------1024.1024.sha256.meta.bin
 *                  |------2048.1024.sha256.meta.bin
 *
 * 2. Increment Copy
 *  ${CopyID}
 *       |
 *      ${UUID}
 *          |------data (sparse file)
 *          |       |------0.1024.data.bin
 *          |       |------1024.1024.data.bin
 *          |       |------2048.1024.data.bin
 *          |
 *          |------meta
 *                  |------incrementcopy.meta.json
 *                  |------0.1024.sha256.meta.bin
 *                  |------1024.1024.sha256.meta.bin
 *                  |------2048.1024.sha256.meta.bin
 */

// TODO:: change this method from blocking to non-blocking
bool SplitFullBackupTask(
	const std::string& 	blockDevicePath,
	const std::string&	outputCopyDataDirPath,
	const std::string&	outputCopyMetaDirPath)
{
    VolumePartitionTableEntry partitionEntry {};
    uint64_t volumeSize = 0;
    uint64_t defaultSessionSize = ONE_TB;
    std::queue<VolumeBackupSession> sessionQueue;

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
    for (uint64_t sessionOffset = 0; sessionOffset < volumeSize;) {
        uint64_t sessionSize = defaultSessionSize;
        if (sessionOffset + defaultSessionSize >= volumeSize) {
            sessionSize = volumeSize - sessionOffset;
        }
        std::string checksumBinPath = util::GetChecksumBinPath(outputCopyMetaDirPath, sessionOffset, sessionSize);
        std::string copyFilePath = util::GetCopyFilePath(outputCopyDataDirPath, sessionOffset, sessionSize);
        VolumeBackupSession session {
            blockDevicePath,
            sessionOffset,
            sessionSize,
            checksumBinPath,
            copyFilePath,
        };
        sessionQueue.push(session);
    }

    // 3. start task in sequence
    while (!sessionQueue.empty()) {
        VolumeBackupSession session = sessionQueue.pop();
        auto context = std::make_shared<VolumeBackupContext>(); // build context
        session.reader = VolumeBlockReader::BuildVolumeReader(
            blockDevicePath,
            session.sessionOffset,
            session.sessionSize,
            context
        );
        session.hasher = VolumeBlockHasher::BuildDirectHasher(
            context,
            session.checksumBinPath
        );
        session.writer = VolumeBlockWriter::BuildCopyWriter(
            session.copyFilePath,
            context
        );
        if (session.reader == nullptr || session.hasher == nullptr || session.writer == nullptr) {
            return false;
        }
        if (!session.reader->Start() || session.hasher->Start() || session.writer->Start()) {
            return false;
        }
        session.wait(); // block the thread
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