#ifndef VOLUMEBACKUP_VOLUME_COPY_MOUNT_PROVIDER_HEADER
#define VOLUMEBACKUP_VOLUME_COPY_MOUNT_PROVIDER_HEADER

#include "VolumeProtectMacros.h"

#ifdef _MSC_VER

#define RECORD_INNER_ERROR(format, ...) do { \
    RecordError(format, __VA_ARGS__); \
    ERRLOG(format, __VA_ARGS__); \
} while (0) \

#else

#define RECORD_INNER_ERROR(format, args...) do { \
    RecordError(format, ##args); \
    ERRLOG(format, ##args); \
} while (0) \

#endif

namespace volumeprotect {
namespace mount {

struct VolumeCopyMountConfig {
    // where files generated by mounting process created
    std::string     outputDirPath;
    // requiring a copy name to decide the name of copy meta json
    std::string     copyName;
    // directory path of the volume copy meta
    std::string     copyMetaDirPath;
    // directory path of the volume copy data
    std::string     copyDataDirPath;
    // mount target path.
    // For *nix loopback mount, the target directory must be created head
    // For Windows virtual disk mount, it can keep empty to retrieve a drive letter automatically
    std::string     mountTargetPath;

    // optional fields....
    // only used for *unix mount to sepecify filesystem type (option "-t", eg: ext4, xfs, btrfs...)
    std::string     mountFsType;
    // only used for *unix mount to sepecify mount options (option "-o", eg: "ro,loop,noatime")
    std::string     mountOptions;      
};

/**
 * Define a inner logger to track the errors occured in mount/umount routines.
 * This should be necessary, for this module may be provided as an independent dynamic lib for extensions
 * like CPython or JNI to invoke, inner logger may track and save the error info for debugging without
 * 3rd logger module being used.  
 */
class VOLUMEPROTECT_API InnerErrorLoggerTrait {
public:
    // get all errors splited by "\n"
    std::string GetError() const;

    std::vector<std::string> GetErrors() const;

protected:
    // format an error log to for tracking
    void RecordError(const char* message, ...);

private:
    std::vector<std::string> m_errors;
};

/**
 * function as base class for multple volume copy mount provider of different backup format
 * mount process takes a configuration from VolumeCopyMountConfig and may generate some magic file in outputDirPath
 * mounting process:
 *   1. validate directory path : outputDirPath, copyMetaDirPath, copyDataDirPath and mountTargetPath
 *   2. read copy meta json file with specified copyName and copyMetaDirPath
 *   3. extract copyMeta struct from json file content to decide which mount provider to load
 *   4. mount copy using corresponding provider
 *   5. save the mount record file to outputDirPath
 */
class VOLUMEPROTECT_API VolumeCopyMountProvider : public InnerErrorLoggerTrait {
public:
    // factory function to load target copy mount provider, depending on which CopyFormat specified
    static std::unique_ptr<VolumeCopyMountProvider> Build(
        VolumeCopyMountConfig& mountConfig
    );

    virtual ~VolumeCopyMountProvider() = default;

    virtual bool IsMountSupported();

    virtual bool Mount();

    virtual std::string GetMountRecordPath() const;

};

/**
 * function as base class for multple volume copy umount provider of different backup format
 * umount process read config from mountRecordJsonFilePath json file
 * umount process:
 *   1. validate json file mountRecordJsonFilePath and read mount record info
 *   2. deciding which umount provider to load
 *   3. umount copy using corresponding umount provider
 */
class VOLUMEPROTECT_API VolumeCopyUmountProvider : public InnerErrorLoggerTrait {
public:
    // factory function to read copy meta json info from target path and umount it using corresponding umount provider
    static std::unique_ptr<VolumeCopyUmountProvider> Build(
        const std::string mountRecordJsonFilePath
    );

    virtual ~VolumeCopyUmountProvider() = default;

    virtual bool Umount();
};

}
}

#endif