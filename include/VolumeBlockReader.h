#ifndef VOLUME_BLOCK_READER_H
#define VOLUME_BLOCK_READER_H

#include <cstdint>
#include <string>
#include <memory>
#include <thread>

#include "VolumeProtectMacros.h"
#include "VolumeProtectTaskContext.h"

namespace volumeprotect {

// read m_sourceLength bytes from block device/copy from m_sourceOffset
class VOLUMEPROTECT_API VolumeBlockReader : public StatefulTask {
public:
    enum SourceType {
        VOLUME,
        COPYFILE
    };

    // build a reader reading from volume (block device)
    static std::shared_ptr<VolumeBlockReader> BuildVolumeReader(
        const std::string& blockDevicePath,
        uint64_t offset,
        uint64_t length,
        std::shared_ptr<VolumeTaskSession> session);

    // build a reader reading from volume copy
    static std::shared_ptr<VolumeBlockReader> BuildCopyReader(
        const std::string& copyFilePath,
        uint64_t offset,
        uint64_t length,
        std::shared_ptr<VolumeTaskSession> session);

    bool Start();

    ~VolumeBlockReader();

    VolumeBlockReader(
        SourceType sourceType,
        const std::string& sourcePath,
        uint64_t    sourceOffset,
        uint64_t    sourceLength,
        std::shared_ptr<VolumeTaskSession> session
    );

private:
    void ReaderThread();

private:
    // immutable fields
    SourceType  m_sourceType;
    std::string m_sourcePath;
    uint64_t    m_sourceOffset;
    uint64_t    m_sourceLength;

    // mutable fields
    std::shared_ptr<VolumeTaskSession> m_session;
    std::thread m_readerThread;
};

}

#endif