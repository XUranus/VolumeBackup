# VolumeBackup
Volume backup/restore library and cli tools for Windows and Linux

 - [X] **FULL BACKUP** and **FOREVER INCREMENT BACKUP** support
 - [X] `*.img`,`*.vhd`,`*.vhdx` copy format support
 - [X] Volume copy mount support
 - [X] Checkpoint support
 - [ ] Zero copy optimization
 - [ ] Qt GUI
 - [ ] Auto snapshot creation of LVM,BTRFS for Linux and VSS for Windows
 - [ ] Auto filesystem type detection


<div align="center">
<img src="https://github.com/XUranus/VolumeBackup/actions/workflows/cmake-multi-platform.yml/badge.svg" alt="VolumeBackup" title="VolumeBackup">&thinsp;
<img src="https://img.shields.io/badge/-C++11-3F63B3.svg?style=flat&logo=C%2B%2B&logoColor=white" alt="C++14" title="C++ Standards Used: C++14">&thinsp;
<img src="https://img.shields.io/badge/-Windows-6E46A2.svg?style=flat&logo=windows-11&logoColor=white" alt="Windows" title="Supported Platform: Windows">&thinsp;
<img src="https://img.shields.io/badge/-Linux-9C2A91.svg?style=flat&logo=linux&logoColor=white" alt="Linux" title="Supported Platform: Linux">&thinsp;
<img src="https://img.shields.io/badge/MSVC%202015+-flag.svg?color=555555&style=flat&logo=visual%20studio&logoColor=white" alt="MSVC 2015+" title="Supported Windows Compiler: MSVC 2015 or later">&thinsp;
<img src="https://img.shields.io/badge/GCC%204.9+-flag.svg?color=555555&style=flat&logo=gnu&logoColor=white" alt="GCC 4.9+" title="Supported Unix Compiler: GCC 4.9 or later">&thinsp;
</div>

## Require
 - CXX 11
 - MSVC2015+/GCC4.9+
 - Windows/Linux
 - OpenSSL 3.0+
 - `libblkid-dev`,`uuid-dev` for Linux

## Build
clone this repository and it's dependency recusively:
```bash
git clone git@github.com:XUranus/VolumeBackup.git --recursive
```
build library `volumebackup` and executable cli tools `vbackup`,`vcopymount` and `vshow`:
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

build JNI volume copy mount extension library `libvolumemount_jni.so`:
```bash
cmake .. -DJNI_INCLUDE=your_jni_headers_directory_path && cmake --build .
```

## Cli Tools Usage
`vtool`, `vbackup` and `vcopymount` is provided as cli tools to backup/restore a volume
1. Use **vshow** to list and query volume info
```
> vshow --list
Name: \\?\Volume{a501f5cc-311e-423c-bc58-94a6c1b6b509}\
Path: \\.\HarddiskVolume3
C:\

Name: \\?\Volume{aeaadea6-033d-44c7-a6eb-5a5c275e5e5b}\
Path: \\.\HarddiskVolume4

Name: \\?\Volume{52ef083b-6ba4-4683-a73a-23a7290139b0}\
Path: \\.\HarddiskVolume1

> vshow --volume=\\.\HarddiskVolume3
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
If you have backuped a copy fully, you can specify the meta directory of last full copy to perform forever increment backup using path of previous full copy data (previous full backup copy data will be covered this time):
```
vbackup --volume=\\.\HarddiskVolume3 --data=D:\volumecopy\data --meta=D:\volumecopy\meta2 --name=diskC --prevmeta=D:\volumecopy\meta
```

> For the sake of data consistency, a umounted volume or a snapshot volume is recommend to be used for backup. On Windows, you can use VSS(Volume Shadow Service) to create a shadow copy, the volume path would be in the form of `\\.\HarddiskVolumeShadowCopyX`, while on Linux, you and use LVM(Logical Volume Management) to create volume snapshot, the path to backup may be look like `\dev\mapper\snap-xxxxx-xxxxx-xxxxx-xxxxx`.

3. Use `vcopymount` to mount a volume copy
```
vcopymount --mount --name=diskC --data=D:\volumecopy\data --meta=D:\volumecopy\meta --output=D:\cache --target=G:\
```
`--ouput` specified the directory to store the mount record file which is required for performing umount operation:
```
vcopymount --umount=D:\cache\diskC.mount.record.json
```