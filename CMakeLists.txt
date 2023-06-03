cmake_minimum_required (VERSION 3.8)

project ("vbkup")

add_executable (vbkup
    "VolumeBlockReader.cpp"
    "VolumeBlockWriter.cpp"
    "VolumeBlockHasher.cpp"
    "VolumeBackup.cpp"
    "BlockAllocator.cpp"
    "BlockingQueue.cpp"
    "VolumeBackupContext.cpp"
    "BlockingQueue.cpp"
    "main.cpp"
)

if (CMAKE_VERSION VERSION_GREATER 3.12)
  set_property(TARGET vbkup PROPERTY CXX_STANDARD 11)
endif()

target_link_libraries(fsutil)