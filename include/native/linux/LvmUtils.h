/**
 * @file LvmUtils.h
 * @brief Provide linux LVM api wrapper for liblvm2app to manipulate snapshots.
 * @copyright Copyright 2023-2024 XUranus. All rights reserved.
 * @license This project is released under the Apache License.
 * @author XUranus(2257238649wdx@gmail.com)
 */

#ifndef VOLUMEBACKUP_LINUX_LVM_UTILS_HEADER
#define VOLUMEBACKUP_LINUX_LVM_UTILS_HEADER

#include <string>

#ifdef __linux__

namespace volumeprotect {
namespace linuxlvmutil {

bool CreateSnapshot(const std::string& vg, const std::string& pv, const std::string& snapshotName);

}
}

#endif

#endif