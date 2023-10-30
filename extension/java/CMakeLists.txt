cmake_minimum_required (VERSION 3.14)
set(Project volumemount_jni)
project(${Project} C CXX)
set(SOURCE_DIR ${${Project}_SOURCE_DIR})

message("=== COMPILE JNI EXTENSION ${Project} ==")
message("Project = ${Project}")
message("SOURCE_DIR = ${SOURCE_DIR}")
message("CMAKE_BUILD_TYPE = ${CMAKE_BUILD_TYPE}")
message("COVERAGE = ${COVERAGE}")
message("JNI_INCLUDE = ${JNI_INCLUDE}")

# supress MSVC/GCC warnings
if(${CMAKE_HOST_WIN32})
    set(CMAKE_CXX_FLAGS_DEBUG "/MTd /Zi /Ob0 /Od /RTC1")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /wd4251")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /wd4996")
else()
    add_compile_options(-Wno-format-security)
endif()

# define headers and sources
set(Headers JVolumeCopyMountUtil.h)
set(Sources JVolumeCopyMountUtil.cpp)

include_directories(
    ${JNI_INCLUDE}
    ${JNI_INCLUDE}/linux
    "../../include"
)

# build a dynamic JNI library
set(LINUX_VOLUME_MOUNT_JNI_LIBRARY_TARGET ${Project})
message("Build minilogger dynamic library ${LINUX_VOLUME_MOUNT_JNI_LIBRARY_TARGET}")
add_library(${LINUX_VOLUME_MOUNT_JNI_LIBRARY_TARGET} SHARED ${Sources} ${Headers})
set_property(TARGET ${LINUX_VOLUME_MOUNT_JNI_LIBRARY_TARGET} PROPERTY CXX_STANDARD 11)
# to generate export library when build dynamic library, pass LIBRARY_EXPORT macro
target_compile_definitions(${LINUX_VOLUME_MOUNT_JNI_LIBRARY_TARGET} PRIVATE -DLIBRARY_EXPORT)

target_link_libraries(
    ${LINUX_VOLUME_MOUNT_JNI_LIBRARY_TARGET}
    volumebackup
)