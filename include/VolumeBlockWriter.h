#ifndef VOLUMEBACKUP_BLOCK_WRITER_HEADER
#define VOLUMEBACKUP_BLOCK_WRITER_HEADER

#include "VolumeProtectMacros.h"
#include "VolumeProtectTaskContext.h"
#include "native/RawIO.h"

namespace volumeprotect {

enum class TargetType {
    VOLUME = 0,
    COPYFILE = 1
};

/**
 * @brief param to build a block reader
 */
struct VolumeBlockWriterParam {
    TargetType      targetType;
    std::string     targetPath;
    std::shared_ptr<VolumeTaskSharedConfig>     sharedConfig;
    std::shared_ptr<VolumeTaskSharedContext>    sharedContext;
    std::shared_ptr<rawio::RawDataWriter>         dataWriter;
};

class VolumeBlockWriter : public StatefulTask {
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

    bool Flush();

private:
    bool NeedToWrite(uint8_t* buffer, int length) const;

    void MainThread();

private:
    // immutable fields
    TargetType      m_targetType;
    std::string     m_targetPath;
    std::shared_ptr<VolumeTaskSharedConfig>                 m_sharedConfig  { nullptr };

    // mutable fields
    std::shared_ptr<VolumeTaskSharedContext>                m_sharedContext { nullptr };
    std::thread                                             m_writerThread;
    std::shared_ptr<volumeprotect::rawio::RawDataWriter>    m_dataWriter    { nullptr };
};

}

#endif