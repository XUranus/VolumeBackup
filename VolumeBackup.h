
#ifndef VOLUME_BACKUP_FACADE_H
#define VOLUME_BACKUP_FACADE_H

#include <string>

namespace volumebackup {



/*
 * backup copy folder herichy
 * backcopy
 *
 */
bool StartBackupTask(
	const std::string& 	blockDevicePath,
	const std::string&	outputCopyDataDirPath,
	const std::string&	outputCopyMetaDirPath
);

}

#endif

