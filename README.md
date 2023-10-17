# VolumeBackup
Volume backup/restore util for Windows and Linux

 - [X] **FULL BACKUP** and **FOREVER INCREMENT BACKUP** support
 - [X] `*.img`,`*.vhd`,`*.vhdx` copy format support
 - [X] Volume copy mount support
 - [X] Checkpoint support

## Require
 - CXX 11
 - MSVC/GCC
 - Windows/Linux

## Build
clone this repository and it's dependency recusively:
```bash
git clone git@github.com:XUranus/VolumeBackup.git --recursive
```
build library and executable:
```bash
mkdir build && cd build
cmake .. && cmake --build .
```

build and run test coverage:
```bash
mkdir build && cd build
# use lcov
cmake .. -DCMAKE_BUILD_TYPE=Debug -DCOVERAGE=lcov
# or use gcovr
# cmake .. -DCMAKE_BUILD_TYPE=Debug -DCOVERAGE=gcovr -DGCOVR_ADDITIONAL_ARGS="--gcov-ignore-parse-errors"
cmake --build .
make volumebackup_coverage_test
```

build JNI volume copy mount extension `libvolumemount_jni.so`:
```bash
cmake .. -DJNI_INCLUDE=your_jni_headers_directory_path && cmake --build .
```

## Cli Tools Usage
`vtool`, `vbackup` and `vcopymount` is provided as cli tools to backup/restore a volume
1. Use **vtools** to list and query volume info
```
> vtools --list
Name: \\?\Volume{a501f5cc-311e-423c-bc58-94a6c1b6b509}\
Path: \\.\HarddiskVolume3
C:\

Name: \\?\Volume{aeaadea6-033d-44c7-a6eb-5a5c275e5e5b}\
Path: \\.\HarddiskVolume4

Name: \\?\Volume{52ef083b-6ba4-4683-a73a-23a7290139b0}\
Path: \\.\HarddiskVolume1

> vtools --volume=\\.\HarddiskVolume3
VolumeName:             Windows
VolumeSize:             254792433664
VolumeSerialNumber:     3430564453
MaximumComponentLength: 255
FileSystemName:         NTFS
FileSystemFlags:        65482495   
```

> this is what a Windows environment will output, for Linux system, volume path should be like `/dev/xxxx`

2. Use **vbackup** to backup volume.
   
Specify a volume path and output data/meta directory to store copy and it's meta info (can be same directory), a customed copy name is also required:
```
vbackup --volume=\\.\HarddiskVolume3 --data=D:\volumecopy\data --meta=D:\volumecopy\meta --name=diskC
```
If you had backuped a copy fully, you can specify the meta directory of last full copy to perform forever increment copy basing at the last copy data:
```
vbackup --volume=\\.\HarddiskVolume3 --data=D:\volumecopy\data --meta=D:\volumecopy\meta2 --name=diskC --prevmeta=D:\volumecopy\meta
```
(the previous full copy data will be covered)

3. Use `vcopymount` to mount a volume copy
```
vcopymount --mount --name=diskC --data=D:\volumecopy\data --meta=D:\volumecopy\meta --output=D:\cache --target=G:\
```
`--ouput` specified the directory to store the mount record file which is required for performing umount operation:
```
vcopymount --umount=D:\cache\diskC.mount.record.json
```