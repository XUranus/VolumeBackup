# VolumeBackup
Volume backup/restore util (full backup and incremental backup)

## Require
 - CXX 17
 - MSVC/GCC
 - Windows/Linux

## Build
```
mkdir build
cd build
cmake ..
cmake --build .
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