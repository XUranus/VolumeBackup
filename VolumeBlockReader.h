#ifndef VOLUME_BLOCK_READER_H
#define VOLUME_BLOCK_READER_H

#include <cstdint>
#include <string>
#include <memory>
#include <thread>
#include "VolumeBackupContext.h"

namespace volumebackup {

// read m_sourceLength bytes from block device/copy from m_sourceOffset
class VolumeBlockReader {
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
        const std::shared_ptr<VolumeBackupContext> context);

    // build a reader reading from volume copy
    static std::shared_ptr<VolumeBlockReader> BuildCopyReader(
        const std::string& copyFilePath,
        uint64_t offset,
        uint64_t length,
        std::shared_ptr<VolumeBackupContext> context);

    bool Start();

private:
    void ReaderThread();

    VolumeBlockReader(
        SourceType sourceType,
        std::string sourcePath,
        uint64_t    sourceOffset,
        uint64_t    sourceLength,
        std::shared_ptr<VolumeBackupContext> context
    );

private:
    SourceType  m_sourceType;
    std::string m_sourcePath;
    uint64_t    m_sourceOffset;
    uint64_t    m_sourceLength;
    std::shared_ptr<VolumeBackupContext> m_context;

    std::thread m_readerThread;
    bool        m_failed { false };
};

}

#endif