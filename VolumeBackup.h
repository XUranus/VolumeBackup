
#ifndef VOLUME_BACKUP_FACADE_H
#define VOLUME_BACKUP_FACADE_H

#include <string>

// volume backup application facade
namespace volumebackup {

bool StartFullBackupTask(
	const std::string& 	blockDevicePath,
	const std::string&	outputCopyDataDirPath,
	const std::string&	outputCopyMetaDirPath
);

bool StartIncrementBackupTask(
	const std::string& 	blockDevicePath,
    const std::string&	prevCopyDataDirPath,
	const std::string&	prevCopyMetaDirPath,
	const std::string&	outputCopyDataDirPath,
	const std::string&	outputCopyMetaDirPath
);

bool StartRestoreTask(
	const std::string&	copyDataDirPath,
	const std::string&	copyMetaDirPath,
    const std::string& 	blockDevicePat
);

}

#endif

