cmake_minimum_required (VERSION 3.8)
set(CMAKE_BUILD_TYPE Debug)
project ("vbkup")

add_executable (vbkup
    "VolumeBackup.cpp"
    "VolumeBackupImpl.cpp"
    "VolumeBackupContext.cpp"
    "VolumeBlockReader.cpp"
    "VolumeBlockWriter.cpp"
    "VolumeBlockHasher.cpp"
    "VolumeBackupUtils.cpp"
    "Logger.cpp"
    "main.cpp"
)

if (CMAKE_VERSION VERSION_GREATER 3.12)
    set_property(TARGET vbkup PROPERTY CXX_STANDARD 17)
endif()

# require linux-utils
target_link_libraries(vbkup blkid parted crypto)