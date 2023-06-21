
#ifndef VOLUME_BLOCK_WRITER_H
#define VOLUME_BLOCK_WRITER_H

#include "VolumeProtectTaskContext.h"

#include <iostream>
#include <memory>
#include <thread>
#include <fstream>

namespace volumeprotect {

class VolumeBlockWriter : public StatefulTask {
public:
    enum TargetType {
        VOLUME,
        COPYFILE
    };

    // build a writer writing to copy file
    static std::shared_ptr<VolumeBlockWriter> BuildCopyWriter(
        std::shared_ptr<VolumeTaskSession> session
    );

    // build a writer writing to volume
    static std::shared_ptr<VolumeBlockWriter> BuildVolumeWriter(
        std::shared_ptr<VolumeTaskSession> session
    );

    bool Start();

    ~VolumeBlockWriter();

    VolumeBlockWriter(
        TargetType targetType,
        const std::string& targetPath,
        std::shared_ptr<VolumeTaskSession> session
    );

private:
    void WriterThread();
    bool Prepare();

private:
    // immutable fields
    TargetType  m_targetType;
    std::string m_targetPath;

    // mutable fields
    std::shared_ptr<VolumeTaskSession> m_session;
    std::thread m_writerThread;
    int         m_fd { -1 };
};

}

#endif