#ifndef VOLUME_CHECKPOINT_MANAGER
#define VOLUME_CHECKPOINT_MANAGER

#include <memory>

namespace volumeprotect {

class Bitmap {
public:
    explicit Bitmap(uint64_t size);
    bool Get(uint64_t index) const;
    void Set(uint64_t index);
private:
    std::unique_ptr<char[]>     m_table { nullptr };
    uint64_t                    m_size  { 0 };
};

/**
 * @brief store the bitmap of volume block writed and and hash table calculated
 * for 1TB session, max blocks cnt 262144, max bitmap size = 32768 bytes
 */
struct BackupTaskCheckpoint {
    
};

struct RestoreTaskCheckpoint {

};

class CheckpointManager {
public:
    static std::shared_ptr<BackupTaskCheckpoint> DetectBackupTaskCheckpoint(const std::string& dirPath);
    static std::shared_ptr<RestoreTaskCheckpoint> DetectRestoreTaskCheckpoint(const std::string& dirPath);
    
};
}

#endif