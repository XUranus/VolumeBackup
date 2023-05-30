
#ifndef VOLUME_BLOCK_HASHER_H
#define VOLUME_BLOCK_HASHER_H

#include <memory>
#include <string>

#include "VolumeBackup.h"

enum HasherForwardMode {
    // direct move block to write queue after block checksum is computed
    DIRECT,        
    // diff the checksum computed with the corresponding previous one and move block forward only it's cheksum changed
    DIFF
};

class VolumeBlockHasher {
public:
    VolumeBlockHasher(const std::string& );

    std::unique_ptr<VolumeBlockHasher>  BuildDirectHasher(
        std::shared_ptr<VolumeBackupContext>
    );

    std::unique_ptr<VolumeBlockHasher>  BuildDiffHasher(
        const std::string& prevChecksumBinPath,     // path of the checksum bin from previous copy
        const std::string& lastestChecksumBinPath   // path of the checksum bin to write latest copy
    );

    bool Start();

private:
    void WorkThread();

private:
    const char*                             m_prevChecksumTable;    // immutabe
    char*                                   m_lastestChecksumTable; // mutable, shared within worker

    HasherForwardMode                       m_forwardMode;          // imutable
    std::shared_ptr<VolumeBackupContext>    m_context;              // mutable, used for sync
    VolumeBackupConfig&                     m_config;               // imutable
};

#endif

std::unique_ptr<VolumeBlockHasher> VolumeBlockHasher::BuildDirectHasher(
    const std::string &checksumBinPath)
 : m_forwardMode(HasherForwardMode::DIRECT)
{
    
}

std::unique_ptr<VolumeBlockHasher> VolumeBlockHasher::BuildDiffHasher(
    const std::string &prevChecksumBinPath,
    const std::string &lastestChecksumBinPath)
 : m_forwardMode(HasherForwardMode::DIFF)
{

}

