
#ifndef VOLUME_BLOCK_WRITER_H
#define VOLUME_BLOCK_WRITER_H

#include <iostream>
#include <memory>
#include <thread>
#include <fstream>

#include "VolumeProtectMacros.h"
#include "VolumeProtectTaskContext.h"
#include "NativeIOInterface.h"

namespace volumeprotect {

enum class TargetType {
    VOLUME = 0,
    COPYFILE = 1
};

/**
 * @brief param to build a block reader
 */
struct VOLUMEPROTECT_API VolumeBlockWriterParam {
    TargetType      targetType;
    std::string     targetPath;
    std::shared_ptr<VolumeTaskSharedConfig>     sharedConfig;
    std::shared_ptr<VolumeTaskSharedContext>    sharedContext;
    std::shared_ptr<native::DataWriter>         dataWriter;
};

class VOLUMEPROTECT_API VolumeBlockWriter : public StatefulTask {
public:
    // build a writer writing to copy file
    static std::shared_ptr<VolumeBlockWriter> BuildCopyWriter(
        std::shared_ptr<VolumeTaskSharedConfig> sharedConfig,
        std::shared_ptr<VolumeTaskSharedContext> sharedContext
    );

    // build a writer writing to volume
    static std::shared_ptr<VolumeBlockWriter> BuildVolumeWriter(
    	std::shared_ptr<VolumeTaskSharedConfig> sharedConfig,
        std::shared_ptr<VolumeTaskSharedContext> sharedContext
    );

    bool Start();

    ~VolumeBlockWriter();

    VolumeBlockWriter(const VolumeBlockWriterParam& param);

private:
    void MainThread();

private:
    // immutable fields
    TargetType      m_targetType;
    std::string     m_targetPath;
    std::shared_ptr<VolumeTaskSharedConfig>             m_sharedConfig;
    
    // mutable fields
    std::shared_ptr<VolumeTaskSharedContext>            m_sharedContext;
    std::thread                                         m_writerThread;
    std::shared_ptr<volumeprotect::native::DataWriter>  m_dataWriter;
};

}

#endif