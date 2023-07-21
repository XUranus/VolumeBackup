# VolumeBackup
Volume backup/restore util (support **FULL BACKUP** and **FOREVER INCREMENT BACKUP**)

## Require
 - CXX 11
 - MSVC/GCC
 - Windows/Linux

## Build
build library and executable:
```
mkdir build && cd build
cmake .. && cmake --build .
```

build and run test coverage:
```
mkdir build && cd build
# use lcov
cmake .. -DCMAKE_BUILD_TYPE=Debug -DCOVERAGE=lcov
# or use gcovr
# cmake .. -DCMAKE_BUILD_TYPE=Debug -DCOVERAGE=gcovr
cmake --build .
make volumebackup_coverage_test
```

## Usage
1. view volume info (**voltool**)
```bash
./voltool -v [volume]
```

2. backup volume (**vbkup**)

specify a lvm snapshot volume path or a VSS shadow copy path to backup
```bash
./vbkup -v [volume] -d [datadir] -m [metadir] { -p [prevmetadir] }
```