/**
 * @file BlockProbeUtils.h
 * @brief Provide a util to lookup metadata(e.g. uuid, fstype, label) from block deviec using libblkid.
 * @copyright Copyright 2023-2024 XUranus. All rights reserved.
 * @license This project is released under the Apache License.
 * @author XUranus(2257238649wdx@gmail.com)
 */

#ifndef VOLUMEBACKUP_LINUX_BLOCK_PROBE_UTILS_HEADER
#define VOLUMEBACKUP_LINUX_BLOCK_PROBE_UTILS_HEADER

#ifdef __linux__

namespace volumeprotect {
namespace linuxmountutil {

const std::string BLKID_PROBE_TAG_UUID = "UUID";
const std::string BLKID_PROBE_TAG_LABEL = "LABEL";
const std::string BLKID_PROBE_TAG_TYPE = "TYPE";

std::string BlockProbeLookup(const std::string& path, const std::string& tag);

std::map<std::string, std::string> BlockProbeLookup(const std::string& path, const std::vector<std::string>& tags);


}
}

#endif

#endif