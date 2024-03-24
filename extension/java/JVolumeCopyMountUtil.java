/**
 * @copyright Copyright 2023-2024 XUranus. All rights reserved.
 * @license This project is released under the Apache License.
 * @author XUranus(2257238649wdx@gmail.com)
 */

import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;

class JVolumeCopyMountConfig {
    public String outputDirPath;
    public String copyName;
    public String copyMetaDirPath;
    public String copyDataDirPath;
    public String mountTargetPath;
    public String mountFsType;
    public String mountOptions;
}

public class JVolumeCopyMountUtil {

    static {
        System.load("/path/to/libvolumemount_jni.so"); // Load the shared library containing your C functions
    }

    // return mount record json path if succeed
    public String mount(JVolumeCopyMountConfig mountConfig) throws IllegalArgumentException {
        String mountRecordPath = null;
        // check output directory existence
        if (!Files.isDirectory(Paths.get(mountConfig.outputDirPath))) {
            throw new IllegalArgumentException("cache directory path not valid");
        }
        // build mount provider instance
        long ptr = buildMountProvider(mountConfig);
        if (ptr == 0) {
            throw new RuntimeException("build mount provider C instance failed");
        }
        if (!mount0(ptr)) {
            String errors = getError(ptr);
            // print errors
            destroyMountProvider(ptr);
            throw new RuntimeException(errors);
        }
        mountRecordPath = getMountRecordPath(ptr);
        destroyMountProvider(ptr);
        return mountRecordPath;
    }

    public boolean umount(String mountRecordJsonFilePath) throws RuntimeException {
        long ptr = buildUmountProvider(mountRecordJsonFilePath);
        if (ptr == 0) {
            throw new RuntimeException("build umount provider C instance failed");
        }
        if (!umount0(ptr)) {
            String errors = getError(ptr);
            destroyUmountProvider(ptr);
            throw new RuntimeException(errors);
        }
        destroyUmountProvider(ptr);
        return true;
    }

    // native methods ...
    private native long buildMountProvider(JVolumeCopyMountConfig mountConfig);

    private native boolean mount0(long ptr);

    private native String getMountRecordPath(long ptr);

    private native void destroyMountProvider(long ptr);

    private native long buildUmountProvider(String mountRecordJsonFilePath);

    private native boolean umount0(long ptr);

    private native void destroyUmountProvider(long ptr);

    private native String getError(long ptr);
}

class MountExample {
    public static void main(String[] args) {
        // test mount and umount
        JVolumeCopyMountUtil mountUtil = new JVolumeCopyMountUtil();
        JVolumeCopyMountConfig mountConfig = new JVolumeCopyMountConfig();
        mountConfig.outputDirPath = "/tmp";
        mountConfig.copyName = "copyname";
        mountConfig.copyMetaDirPath = "/dummy/path";
        mountConfig.copyDataDirPath = "/dummy/path";
        mountConfig.mountTargetPath = "/dummy/path";
        mountConfig.mountTargetPath = "/mnt/volumecopy";
        mountConfig.mountOptions = "ro";
        mountConfig.mountFsType = "ext4";
        try {
            String mountRecordPath = mountUtil.mount(mountConfig);
            if (mountRecordPath == null) {
                System.out.println("invalid mount record path");
                return;
            } else {
                System.out.println("mount success");
            }
            if (mountUtil.umount(mountRecordPath)) {
                System.out.println("umount success");
            }
        } catch (RuntimeException e) {
            System.out.println(e.getMessage());
            return;
        }
        return;
    }
}
