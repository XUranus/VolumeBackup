cmake_minimum_required (VERSION 3.14)
set(Project volumebackup)
project(${Project} C CXX)
set(SOURCE_DIR ${${Project}_SOURCE_DIR})

message("Project = ${Project}")
message("SOURCE_DIR = ${SOURCE_DIR}")
message("CMAKE_BUILD_TYPE = ${CMAKE_BUILD_TYPE}")
message("COVERAGE = ${COVERAGE}")

message (STATUS "Build documentation open")
include ("docs/BuildDoxygen.cmake")
build_doxygen()

# supress MSVC/GCC warnings
if(${CMAKE_HOST_WIN32})
    set(CMAKE_CXX_FLAGS_DEBUG "/MTd /Zi /Ob0 /Od /RTC1")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /wd4251")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /wd4996")
else()
    ADD_COMPILE_OPTIONS(-Wno-format-security)
endif()

if(${CMAKE_HOST_WIN32})
    file(
        GLOB_RECURSE
        VOLUMEPROTECTPROTECT_SOURCES
        "src/*.cpp"
        "src/common/*.cpp"
        "src/task*.cpp"
        "src/native/*.cpp"
        "src/native/win32/*.cpp"
    )
else()
    file(
        GLOB_RECURSE
        VOLUMEPROTECTPROTECT_SOURCES
        "src/*.cpp"
        "src/common/*.cpp"
        "src/task*.cpp"
        "src/native/*.cpp"
        "src/native/linux/*.cpp"
    )
endif()

FILE(
    GLOB_RECURSE
    VOLUMEPROTECTPROTECT_HEADERS
    "include/*.h"
    "include/common/*.h"
    "include/task/*.h"
    "include/native/*.h"
)

message("VOLUMEPROTECTPROTECT_SOURCES = ${VOLUMEPROTECTPROTECT_SOURCES}")
message("VOLUMEPROTECTPROTECT_HEADERS = ${VOLUMEPROTECTPROTECT_HEADERS}")

include_directories(
    "include"
    "include/common"
    "include/task"
    "include/native"
    "dependency/minijson"
    "dependency/minilogger"
)

add_subdirectory(dependency/minijson)
add_subdirectory(dependency/minilogger)

if (NOT "${JNI_INCLUDE}" STREQUAL "")
    message("COMPILE JNI EXTENSION, INCLUDE ${JNI_INCLUDE}")
    add_subdirectory(extension/java)
endif()

# build a dynamic library
message("Build libvolumebackup dynamic library ${VOLUMEPROTECT_DYNAMIC_LIBRARY_TARGET}")
set(VOLUMEPROTECT_DYNAMIC_LIBRARY_TARGET ${Project})
add_library(
    ${VOLUMEPROTECT_DYNAMIC_LIBRARY_TARGET}
    SHARED
    ${VOLUMEPROTECTPROTECT_SOURCES} ${VOLUMEPROTECTPROTECT_HEADERS}
)
set_property(
    TARGET ${VOLUMEPROTECT_DYNAMIC_LIBRARY_TARGET}
    PROPERTY CXX_STANDARD 11
)
# to generate export library when build dynamic library, pass LIBRARY_EXPORT macro
target_compile_definitions(
    ${VOLUMEPROTECT_DYNAMIC_LIBRARY_TARGET}
    PRIVATE -DLIBRARY_EXPORT
)
# link again dynamic libs
if(${CMAKE_HOST_WIN32})
    target_link_libraries(
        ${VOLUMEPROTECT_DYNAMIC_LIBRARY_TARGET}
        minijson
        minilogger
        # third part win32 dependency (win32 msvc)
        virtDisk
        setupapi
        uuid
        Rpcrt4
    )
else()
    target_link_libraries(
        ${VOLUMEPROTECT_DYNAMIC_LIBRARY_TARGET}
        minijson
        minilogger
        # third part dependency (linux gcc)
        uuid
    )
endif()


# build a static library
message("Build libvolumebackup static library ${VOLUMEPROTECT_STATIC_LIBRARY_TARGET}")
set(VOLUMEPROTECT_STATIC_LIBRARY_TARGET ${Project}_static)
ADD_LIBRARY(
    ${VOLUMEPROTECT_STATIC_LIBRARY_TARGET}
    STATIC
    ${VOLUMEPROTECTPROTECT_SOURCES} ${VOLUMEPROTECTPROTECT_HEADERS}
)
SET_PROPERTY(
    TARGET ${VOLUMEPROTECT_STATIC_LIBRARY_TARGET}
    PROPERTY CXX_STANDARD 11
)
# link againt static libs
if(${CMAKE_HOST_WIN32})
    TARGET_LINK_LIBRARIES(
        ${VOLUMEPROTECT_STATIC_LIBRARY_TARGET}
        minijson_static
        minilogger_static
        # third part win32 dependency (win32 msvc)
        virtDisk
        setupapi
        uuid
        Rpcrt4
    )
ELSE()
    TARGET_LINK_LIBRARIES(
        ${VOLUMEPROTECT_STATIC_LIBRARY_TARGET}
        minijson_static
        minilogger_static
        # third part dependency (linux gcc)
        uuid
    )
endif()

# set -DCMAKE_BUILD_TYPE=Debug to enable LLT, set -DCOVERAGE=ON to enable code coverage
IF (CMAKE_BUILD_TYPE STREQUAL "Debug")
    # these config must be put at the level of source code in order to append compile flags
    IF ("${COVERAGE}" STREQUAL "lcov" OR "${COVERAGE}" STREQUAL "gcovr")
        message("code coverage is enabled, COVERAGE = ${COVERAGE}, append coverage compiler flags")
        INCLUDE(${SOURCE_DIR}/test/CodeCoverage.cmake)
        APPEND_COVERAGE_COMPILER_FLAGS()
    endif()

    ENABLE_TESTING()
    add_subdirectory("test")
endif()

# build executable tools
ADD_EXECUTABLE (vbackup
    "cli/vbackup.cpp"
    "cli/GetOption.cpp"
)

ADD_EXECUTABLE (vtools
    "cli/vtools.cpp"
    "cli/GetOption.cpp"
)

SET_PROPERTY(TARGET vbackup PROPERTY CXX_STANDARD 11)
SET_PROPERTY(TARGET vtools PROPERTY CXX_STANDARD 11)

# link vbackup executable
if(${CMAKE_HOST_WIN32})
    TARGET_LINK_LIBRARIES(
        vbackup
        volumebackup_static
        # third part dependency provided by XUranus
        minijson_static
        minilogger_static
    )
ELSE()
    # require linux-utils
    TARGET_LINK_LIBRARIES(
        vbackup
        volumebackup
        # openssl
        crypto
        pthread
        # third part dependency provided by XUranus
        minijson_static
        minilogger_static
    )
endif()

# build vtools executable
TARGET_LINK_LIBRARIES(
    vtools
)

# build vcopymount executable
ADD_EXECUTABLE (vcopymount
    "cli/vcopymount.cpp"
    "cli/GetOption.cpp"
)

SET_PROPERTY(TARGET vcopymount PROPERTY CXX_STANDARD 11)

TARGET_LINK_LIBRARIES(
    vcopymount
    volumebackup_static
    # third part dependency provided by XUranus
    minijson_static
    minilogger_static
)