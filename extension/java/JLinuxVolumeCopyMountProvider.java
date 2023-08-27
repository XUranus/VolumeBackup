
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
        System.load("/path/to/libvolumemount_jni.so"); // Load the shared library containing your C functions
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

class MountDemo {
    private static String cacheDirPath = "/dummy/path";
    
    private static LinuxVolumeCopyMountConfig mountConfig = new LinuxVolumeCopyMountConfig();
    
    static {
        mountConfig.copyMetaDirPath = "/dummy/path";
        mountConfig.copyDataDirPath = "/dummy/path";
        mountConfig.mountTargetPath = "/dummy/path";
        mountConfig.mountTargetPath = "/mnt/volumecopy";
        mountConfig.mountOptions = "ro";
        mountConfig.mountFsType = "ext4";
    }

    public static void testMount() {
        try {
            JLinuxVolumeCopyMountProvider mountProvider = new JLinuxVolumeCopyMountProvider(cacheDirPath);
            if (mountProvider.mountCopy(mountConfig)) {
                System.out.println("mount success");
            } else {
                System.out.println("mount failed");
                System.out.println("===== ERROR ======");
                System.out.println(mountProvider.getErrorString());
                System.out.println("---- Start To Clear Residue ----");
                if (!mountProvider.clearResidue()) {
                    System.out.println("failed to clear residue");
                    System.out.println("===== ERROR =====");
                    System.out.println(mountProvider.getErrorString());
                }
            }
        } catch (Exception e) {
            e.printStackTrace();
        }
    }

    public static void testUmount() {
        try {
            JLinuxVolumeCopyMountProvider mountProvider = new JLinuxVolumeCopyMountProvider(cacheDirPath);
            if (mountProvider.umountCopy()) {
                System.out.println("umount success");
            } else {
                System.out.println("umount failed");
                System.out.println("===== ERROR ======");
                System.out.println(mountProvider.getErrorString());
            }
        } catch (Exception e) {
            e.printStackTrace();
        }
    }

    public static void main(String[] args) {
        testMount();
    }
}
