/**
 * @copyright Copyright 2023-2024 XUranus. All rights reserved.
 * @license This project is released under the Apache License.
 * @author XUranus(2257238649wdx@gmail.com)
 */

#ifdef __linux__

#include "Logger.h"
#include "common/VolumeProtectMacros.h"
#include "native/linux/BlockProbeUtils.h"

#include <blkid/blkid.h>

using namespace volumeprotect;
using namespace volumeprotect::linuxmountutil;

std::string linuxmountutil::BlockProbeLookup(const std::string& path, const std::string& tag)
{
    return linuxmountutil::BlockProbeLookup(path, std::vector<std::string> { tag })[tag];
}

std::map<std::string, std::string> linuxmountutil::BlockProbeLookup(const std::string& path, const std::vector<std::string>& tags)
{
    std::map<std::string, std::string> results {};
    for (const std::string& tag : tags) {
        results[tag] = "";
    }
    ::blkid_probe pr = ::blkid_new_probe_from_filename(path.c_str());
    if (!pr) {
        ERRLOG("failed to open %s for probing, errno = %d", path.c_str(), errno);
        return results;
    }
    ::blkid_do_probe(pr);
    for (const std::string& tag: tags) {
        const char* tagValue = nullptr; // point to probe struct, do not free twice
        ::blkid_probe_lookup_value(pr, tag.c_str(), &tagValue, nullptr);
        std::string tagStr = (tagValue != nullptr ? std::string(tagValue) : "");
        results[tag] = tagStr;
    }
    ::blkid_free_probe(pr);
   return results;
}

#endif