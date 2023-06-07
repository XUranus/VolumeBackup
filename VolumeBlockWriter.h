
#ifndef VOLUME_BLOCK_WRITER_H
#define VOLUME_BLOCK_WRITER_H

#include "VolumeBackupContext.h"

#include <iostream>
#include <memory>
#include <thread>
#include <fstream>

namespace volumebackup {

class VolumeBlockWriter {
public:
    enum TargetType {
        VOLUME,
        COPYFILE
    };

    // build a writer writing to copy file
    static std::shared_ptr<VolumeBlockWriter> BuildCopyWriter(
        const std::string& copyFilePath,
        std::shared_ptr<VolumeBackupContext> context
    );

    // build a writer writing to volume
    static std::shared_ptr<VolumeBlockWriter> BuildVolumeWriter(
        const std::string& blockDevicePath,
        std::shared_ptr<VolumeBackupContext> context
    );

    bool Start();

private:
    VolumeBlockWriter(
        TargetType targetType,
        const std::string& targetPath,
        std::shared_ptr<VolumeBackupContext> context
    );

    void WriterThread();

private:
    TargetType  m_targetType;
    std::string m_targetPath;
    std::shared_ptr<VolumeBackupContext> m_context;  // mutable, used for sync

    std::thread m_writerThread;
    bool m_failed { false };
};

}

#endif