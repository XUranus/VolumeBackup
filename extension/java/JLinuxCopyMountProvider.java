
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;

public class JLinuxCopyMountProvider {
    
    static {
        System.loadLibrary("libvolumebackup.so"); // Load the shared library containing your C functions
    }

    public static JLinuxCopyMountProvider buildMountProvider(String cacheDirPath) {
        if (!Files.isDirectory(patPaths.get(cacheDirPath))) {
            return null;
        }
        long providerHandle = createLinuxMountProvider(cacheDirPath);
        if (providerHandle == 0) {
            return null;
        }
        JLinuxCopyMountProvider mountProvider = new JLinuxCopyMountProvider(providerHandle);
        return mountProvider;
    }

    // return the mount record json file path is success, return empty string if failed
    String mountVolume(JLinuxCopyMountConf mountConf) {
        StringBuilder stringBuilder = new StringBuilder(4096);
        if (mountLinuxVolumeCopy(mountConf, stringBuilder)) {
            return stringBuilder.toString();
        }
        return "";
    }

    boolean umountVolume(linuxCopyMountRecordJsonPath) {
        return umountLinuxVolumeCopy(providerHandle, linuxCopyMountRecordJsonPath);
    }

    String getErrorString() {
        return getLinuxMountProviderError(providerHandle);
    }

    @Override
    protected void finalize() throws Throwable {
        try {
            if (providerHandle != 0) {
                destroyLinuxMountProvider(providerHandle);
                providerHandle = 0;
            }
        } finally {
            super.finalize();
        }
    }

    private JLinuxCopyMountProvider(long providerHandle) {
        this.providerHandle = providerHandle;
    }

    // native methods ...
    private native long createLinuxMountProvider(String cacheDirPath);

    private native boolean mountLinuxVolumeCopy(long mountProvider, JLinuxCopyMountConf conf, StringBuilder pathBuffer);

    private native boolean umountLinuxVolumeCopy(long mountProvider, String linuxCopyMountRecordJsonPath);

    private native void destroyLinuxMountProvider(long mountProvider);

    private native String getLinuxMountProviderError(long mountProvider);

    private long providerHandle;
}

class JLinuxCopyMountConf {
    public String copyMetaDirPath;
    public String copyDataDirPath;
    public String mountTargetPath;
    public String mountFsType;
    public String mountOptions;
}