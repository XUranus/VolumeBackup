
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;

class LinuxVolumeCopyMountConfig {
    public String copyMetaDirPath;
    public String copyDataDirPath;
    public String mountTargetPath;
    public String mountFsType;
    public String mountOptions;
}

public class JLinuxVolumeCopyMountProvider {
    
    static {
        System.loadLibrary("libvolumebackup.so"); // Load the shared library containing your C functions
    }

    public JLinuxVolumeCopyMountProvider(String cacheDirPath) throws IllegalArgumentException {
        if (!Files.isDirectory(Paths.get(cacheDirPath))) {
            throw new IllegalArgumentException("cache directory path not valid");
        }
        this.cacheDirPath = cacheDirPath;
    }

    public boolean mountCopy(LinuxVolumeCopyMountConfig mountConfig) throws RuntimeException {
        long provider = createMountProvider(this.cacheDirPath);
        if (provider == 0) {
            throw new RuntimeException("build LinuxMountProvider C instance failed");
        }
        if (!mountVolumeCopy(provider, mountConfig)) {
            errorString = getMountProviderError(provider);
            destroyMountProvider(provider);
            return false;
        }
        destroyMountProvider(provider);
        return true;
    }

    public boolean umountCopy() throws RuntimeException {
        long provider = createMountProvider(this.cacheDirPath);
        if (provider == 0) {
            throw new RuntimeException("build LinuxMountProvider C instance failed");
        }
        if (!umountVolumeCopy(provider)) {
            errorString = getMountProviderError(provider);
            destroyMountProvider(provider);
            return false;
        }
        return true;
    }

    public boolean clearResidue() throws RuntimeException {
        long provider = createMountProvider(this.cacheDirPath);
        if (provider == 0) {
            throw new RuntimeException("build LinuxMountProvider C instance failed");
        }
        if (!clearResidue(provider)) {
            errorString = getMountProviderError(provider);
            destroyMountProvider(provider);
            return false;
        }
        return true;
    }

    public String getErrorString() {
        return errorString;
    }

    // native methods ...
    private native long createMountProvider(String cacheDirPath);

    private native boolean mountVolumeCopy(long provider, LinuxVolumeCopyMountConfig mountConf);

    private native boolean umountVolumeCopy(long provider);

    private native boolean clearResidue(long provider);

    private native void destroyMountProvider(long provider);

    private native String getMountProviderError(long provider);

    private String errorString;
    private String cacheDirPath;
}