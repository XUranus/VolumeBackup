#ifndef VOLUME_BLOCK_READER_H
#define VOLUME_BLOCK_READER_H

#include <cstdint>
#include <string>
#include <memory>
#include <thread>

#include "VolumeProtectMacros.h"
#include "VolumeProtectTaskContext.h"
#include "NativeIOInterface.h"

namespace volumeprotect {

enum class SourceType{
    VOLUME = 0,
    COPYFILE = 1
};

/**
 * @brief param to build a block reader
 */
struct VOLUMEPROTECT_API VolumeBlockReaderParam {
    SourceType  sourceType;
    std::string sourcePath;
    uint64_t    sourceOffset;
    uint64_t    sourceLength;
    std::shared_ptr<VolumeTaskSession> session;
    std::shared_ptr<native::DataReader> dataReader;

};

// read m_sourceLength bytes from block device/copy from m_sourceOffset
class VOLUMEPROTECT_API VolumeBlockReader : public StatefulTask {
public:
    // build a reader reading from volume (block device)
    static std::shared_ptr<VolumeBlockReader> BuildVolumeReader(
        const std::string& volumePath,
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

    // provide to gmock or builder, not recommended to use
    VolumeBlockReader(const VolumeBlockReaderParam& param);

private:
    void MainThread();

private:
    // immutable fields
    SourceType  m_sourceType;
    std::string m_sourcePath;
    uint64_t    m_sourceOffset;
    uint64_t    m_sourceLength;

    // mutable fields
    std::shared_ptr<VolumeTaskSession>  m_session;
    std::thread                         m_readerThread;
    std::shared_ptr<volumeprotect::native::DataReader> m_dataReader;  
};

}

#endif