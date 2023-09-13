CMAKE_MINIMUM_REQUIRED (VERSION 3.14)
SET(Project volumebackup)
PROJECT(${Project} C CXX)
SET(SOURCE_DIR ${${Project}_SOURCE_DIR})

MESSAGE("Project = ${Project}")
MESSAGE("SOURCE_DIR = ${SOURCE_DIR}")
MESSAGE("CMAKE_BUILD_TYPE = ${CMAKE_BUILD_TYPE}")
MESSAGE("COVERAGE = ${COVERAGE}")

# supress MSVC/GCC warnings
IF(${CMAKE_HOST_WIN32})
    SET(CMAKE_CXX_FLAGS_DEBUG "/MTd /Zi /Ob0 /Od /RTC1")
    SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /wd4251")
    SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /wd4996")
ELSE()
    ADD_COMPILE_OPTIONS(-Wno-format-security)
ENDIF()

IF(${CMAKE_HOST_WIN32})
    FILE(
        GLOB_RECURSE
        VOLUMEPROTECTPROTECT_SOURCES
        "src/*.cpp"
        "src/native/*.cpp"
        "src/native/win32/*.cpp"
    )
ELSE()
    FILE(
        GLOB_RECURSE
        VOLUMEPROTECTPROTECT_SOURCES
        "src/*.cpp"
        "src/native/*.cpp"
        "src/native/linux/*.cpp"
    )
ENDIF()

FILE(
    GLOB_RECURSE
    VOLUMEPROTECTPROTECT_HEADERS
    "include/*.h"
)

MESSAGE("VOLUMEPROTECTPROTECT_SOURCES = ${VOLUMEPROTECTPROTECT_SOURCES}")
MESSAGE("VOLUMEPROTECTPROTECT_HEADERS = ${VOLUMEPROTECTPROTECT_HEADERS}")

INCLUDE_DIRECTORIES(
    "include"
    "include/native" 
    "dependency/minijson"
    "dependency/minilogger"
)

ADD_SUBDIRECTORY(dependency/minijson)
ADD_SUBDIRECTORY(dependency/minilogger)

IF (NOT "${JNI_INCLUDE}" STREQUAL "")
    MESSAGE("COMPILE JNI EXTENSION, INCLUDE ${JNI_INCLUDE}")
    ADD_SUBDIRECTORY(extension/java)
ENDIF()

# build a dynamic library
MESSAGE("Build libvolumebackup dynamic library ${VOLUMEPROTECT_DYNAMIC_LIBRARY_TARGET}")
SET(VOLUMEPROTECT_DYNAMIC_LIBRARY_TARGET ${Project})
ADD_LIBRARY(
    ${VOLUMEPROTECT_DYNAMIC_LIBRARY_TARGET}
    SHARED
    ${VOLUMEPROTECTPROTECT_SOURCES} ${VOLUMEPROTECTPROTECT_HEADERS}
)
SET_PROPERTY(
    TARGET ${VOLUMEPROTECT_DYNAMIC_LIBRARY_TARGET}
    PROPERTY CXX_STANDARD 11
)
# to generate export library when build dynamic library, pass LIBRARY_EXPORT macro
TARGET_COMPILE_DEFINITIONS(
    ${VOLUMEPROTECT_DYNAMIC_LIBRARY_TARGET}
    PRIVATE -DLIBRARY_EXPORT
)
# link again dynamic libs
TARGET_LINK_LIBRARIES(
    ${VOLUMEPROTECT_DYNAMIC_LIBRARY_TARGET}
    minijson
    minilogger
    # third part dependency
    uuid
)

# build a static library
MESSAGE("Build libvolumebackup static library ${VOLUMEPROTECT_STATIC_LIBRARY_TARGET}")
SET(VOLUMEPROTECT_STATIC_LIBRARY_TARGET ${Project}_static)
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
TARGET_LINK_LIBRARIES(
    ${VOLUMEPROTECT_STATIC_LIBRARY_TARGET}
    minijson_static
    minilogger_static
    # third part dependency
    uuid
)

# set -DCMAKE_BUILD_TYPE=Debug to enable LLT, set -DCOVERAGE=ON to enable code coverage
IF (CMAKE_BUILD_TYPE STREQUAL "Debug")
    # these config must be put at the level of source code in order to append compile flags
    IF ("${COVERAGE}" STREQUAL "lcov" OR "${COVERAGE}" STREQUAL "gcovr")
        MESSAGE("code coverage is enabled, COVERAGE = ${COVERAGE}, append coverage compiler flags")
        INCLUDE(${SOURCE_DIR}/test/CodeCoverage.cmake)
        APPEND_COVERAGE_COMPILER_FLAGS()
    ENDIF()

    ENABLE_TESTING()
    ADD_SUBDIRECTORY("test")
ENDIF()

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
IF(${CMAKE_HOST_WIN32})
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
ENDIF()

# build vtools executable
TARGET_LINK_LIBRARIES(
    vtools
)

# build vcopymount executable
IF(NOT ${CMAKE_HOST_WIN32})
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
ENDIF()