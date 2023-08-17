#!/usr/bin/python
# coding=utf-8

# python api wrapper for VolumeBackup using ctypes
import ctypes
from ctypes import *
from ctypes.util import find_library
from dataclasses import dataclass

VOLUME_BACKUP_LIBRARY_PATH = "./build/libvolumebackup.so"

class VolumeBackupConf_C(ctypes.Structure):
    _fields_ = [
        ("copyType", ctypes.c_int),
        ("volumePath", ctypes.c_char_p),
        ("prevCopyMetaDirPath", ctypes.c_char_p),
        ("outputCopyDataDirPath", ctypes.c_char_p),
        ("outputCopyMetaDirPath", ctypes.c_char_p),
        ("blockSize", ctypes.c_uint32),
        ("sessionSize", ctypes.c_uint64),
        ("hasherNum", ctypes.c_uint32),
        ("hasherEnabled", ctypes.c_bool),
        ("enableCheckpoint", ctypes.c_bool)
    ]

class VolumeRestoreConf_C(ctypes.Structure):
    _fields_ = [
        ("volumePath", ctypes.c_char_p),
        ("copyDataDirPath", ctypes.c_char_p),
        ("copyMetaDirPath", ctypes.c_char_p),
        ("enableCheckpoint", ctypes.c_bool)
    ]

class TaskStatus_C(ctypes.c_int):
    INIT = 0
    RUNNING = 1
    SUCCEED = 2
    ABORTING = 3
    ABORTED = 4
    FAILED = 5

class TaskStatistics_C(ctypes.Structure):
    _fields_ = [
        ("bytesToRead", ctypes.c_uint64),
        ("bytesRead", ctypes.c_uint64),
        ("blocksToHash", ctypes.c_uint64),
        ("blocksHashed", ctypes.c_uint64),
        ("bytesToWrite", ctypes.c_uint64),
        ("bytesWritten", ctypes.c_uint64)
    ]

# Load the shared library
libcrypto_path = find_library("crypto")
if libcrypto_path:
    shared_lib_path = "/home/xuranus/workspace/VolumeBackup/build/libvolumebackup.so"
    shared_lib = ctypes.CDLL(shared_lib_path, mode=ctypes.RTLD_GLOBAL)
else:
    print("Error: libcrypto not found.")
shared_lib = ctypes.CDLL(VOLUME_BACKUP_LIBRARY_PATH, mode=ctypes.RTLD_GLOBAL)

# Define function prototypes
shared_lib.BuildBackupTask.restype = ctypes.c_void_p
shared_lib.BuildBackupTask.argtypes = [VolumeBackupConf_C]

shared_lib.BuildRestoreTask.restype = ctypes.c_void_p
shared_lib.BuildRestoreTask.argtypes = [VolumeRestoreConf_C]

shared_lib.StartTask.argtypes = [ctypes.c_void_p]
shared_lib.StartTask.restype = ctypes.c_bool

shared_lib.DestroyTask.argtypes = [ctypes.c_void_p]

shared_lib.GetTaskStatistics.argtypes = [ctypes.c_void_p]
shared_lib.GetTaskStatistics.restype = TaskStatistics_C

shared_lib.AbortTask.argtypes = [ctypes.c_void_p]

shared_lib.GetTaskStatus.argtypes = [ctypes.c_void_p]
shared_lib.GetTaskStatus.restype = TaskStatus_C

shared_lib.IsTaskFailed.argtypes = [ctypes.c_void_p]
shared_lib.IsTaskFailed.restype = ctypes.c_bool

shared_lib.IsTaskTerminated.argtypes = [ctypes.c_void_p]
shared_lib.IsTaskTerminated.restype = ctypes.c_bool


class VolumeProtectTask:
    def __init__(self, config : any):
        if isinstance(config, VolumeBackupConf_C):
            self.instance = shared_lib.BuildBackupTask(config)
        elif isinstance(config, VolumeRestoreConf_C):
            self.instance = shared_lib.BuildRestoreTask(config)
        else:
            raise Exception(f'invalid config {config}')

    def valid(self) -> bool:
        return self.instance is not None

    def start(self) -> bool:
        return shared_lib.StartTask(self.instance)

    def destroy(self) -> None:
        shared_lib.DestroyTask(self.instance)

    def statistics(self) -> TaskStatistics_C:
        return shared_lib.GetTaskStatistics(self.instance)

    def abort(self) -> None:
        shared_lib.DestroyTask(self.instance)

    def status(self) -> TaskStatus_C:
        return shared_lib.GetTaskStatus(self.instance)

    def is_failed(self) -> bool:
        return shared_lib.IsTaskFailed(self.instance)

    def is_terminated(self) -> bool:
        return shared_lib.IsTaskTerminated(self.instance)



def start_backup():
    backup_config = VolumeBackupConf_C(
        copyType=1,
        volumePath=b"/dev/loop0",
        prevCopyMetaDirPath=None,
        outputCopyDataDirPath=b"/home/xuranus/workspace/VolumeBackup/build/vol2",
        outputCopyMetaDirPath=b"/home/xuranus/workspace/VolumeBackup/build/vol2",
        blockSize=4096,
        sessionSize=1024 * 1024 * 1024,
        hasherNum=8,
        hasherEnabled=True,
        enableCheckpoint=True
    )
    task = VolumeProtectTask(backup_config)
    if not task.valid():
        print('invalid task')
        return
    if task.start():
        print('task start failed')
        return
    while not task.is_terminated():
        print(task.statistics())
    print(task.status())

def start_restore():
    restore_config = VolumeRestoreConf_C(
        volumePath=b"/dev/loop0",
        copyDataDirPath=b"/home/xuranus/workspace/VolumeBackup/build/vol2",
        copyMetaDirPath=b"/home/xuranus/workspace/VolumeBackup/build/vol2",
        enableCheckpoint=True
    )
    task = VolumeProtectTask(restore_config)
    if not task.valid():
        print('invalid task')
        return
    if task.start():
        print('task start failed')
        return
    while not task.is_terminated():
        print(task.statistics())
    print(task.status())


if __name__ == "__main__":
    start_backup()
