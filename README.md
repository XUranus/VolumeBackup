# VolumeBackup
Volume backup/restore util (support **FULL BACKUP** and **FOREVER INCREMENT BACKUP**)

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

## Usage
1. view volume info (**voltool**)
```bash
./vtools -v [volume]
```

2. backup volume (**vbkup**)

specify a lvm snapshot volume path or a VSS shadow copy path to backup
```bash
./vbackup -v [volume] -d [datadir] -m [metadir] { -p [prevmetadir] }
```