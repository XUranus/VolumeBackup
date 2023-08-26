import JLinuxCopyMountProvider
import JLinuxCopyMountConf

public class MountDemo {
    public static void testMount() {
        StringBuildertring cacheDirPath;
        JLinuxCopyMountProvider mountProvider = JLinuxCopyMountProvider.buildMountProvider(cacheDirPath);
        if (mountProvider == null) {
            System.out.println(String.format("invalid cache dir path %s", cacheDirPath));
            return;
        }
        mountProvider.
    }

    public static void main(String[] args) {
        testMount();
    }
}
