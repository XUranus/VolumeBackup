
#ifndef VOLUME_BLOCK_WRITER_H
#define VOLUME_BLOCK_WRITER_H

#include "VolumeBackupContext.h"

#include <iostream>
#include <memory>
#include <thread>
#include <fstream>

namespace volumebackup {

class VolumeBlockWriter : public StatefulTask {
public:
    enum TargetType {
        VOLUME,
        COPYFILE
    };

    // build a writer writing to copy file
    static std::shared_ptr<VolumeBlockWriter> BuildCopyWriter(
        std::shared_ptr<VolumeBackupSession> session
    );

    // build a writer writing to volume
    static std::shared_ptr<VolumeBlockWriter> BuildVolumeWriter(
        std::shared_ptr<VolumeBackupSession> session
    );

    bool Start();

    ~VolumeBlockWriter();

private:
    VolumeBlockWriter(
        TargetType targetType,
        const std::string& targetPath,
        std::shared_ptr<VolumeBackupSession> session
    );

    void WriterThread();

private:
    // immutable fields
    TargetType  m_targetType;
    std::string m_targetPath;

    // mutable fields
    std::shared_ptr<VolumeBackupSession> m_session;
    std::thread m_writerThread;
};

}

#endif