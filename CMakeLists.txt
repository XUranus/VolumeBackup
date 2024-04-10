cmake_minimum_required (VERSION 3.14)
set(Project volumebackup)
project(${Project} C CXX)
set(SOURCE_DIR ${${Project}_SOURCE_DIR})

message("Project = ${Project}")
message("SOURCE_DIR = ${SOURCE_DIR}")
message("CMAKE_BUILD_TYPE = ${CMAKE_BUILD_TYPE}")
message("COVERAGE = ${COVERAGE}")

# generate doc using doxygen
message (STATUS "Build documentation open")
include ("docs/BuildDoxygen.cmake")
build_doxygen()

# check 3rd dependencies
set(
    CMAKE_PREFIX_PATH
    "C:/vcpkg/packages/openssl_x64-windows"
    "/usr/local/Cellar/openssl@3"
    "/usr/local/Cellar/openssl@3/3.1.3"
    ${CMAKE_PREFIX_PATH}
)

# in order to be compatible with older cmake (<3.25) that CMAKE_HOST_LINUX not introduced
message(STATUS "CMAKE_SYSTEM_NAME = ${CMAKE_SYSTEM_NAME}")
if ("${CMAKE_SYSTEM_NAME}" STREQUAL "Linux")
    set(CMAKE_HOST_LINUX true)
elseif("${CMAKE_SYSTEM_NAME}" STREQUAL "Windows")
    set(CMAKE_HOST_WIN32 true)
endif()

find_package(
    OpenSSL 3
    REQUIRED)
message(STATUS "OPENSSL_VERSION = ${OPENSSL_VERSION}")
message(STATUS "OPENSSL_INCLUDE_DIR = ${OPENSSL_INCLUDE_DIR}")
message(STATUS "OPENSSL_CRYPTO_LIBRARY = ${OPENSSL_CRYPTO_LIBRARY}")
message(STATUS "OPENSSL_CRYPTO_LIBRARIES = ${OPENSSL_CRYPTO_LIBRARIES}")
message(STATUS "OPENSSL_SSL_LIBRARY = ${OPENSSL_SSL_LIBRARY}")
message(STATUS "OPENSSL_SSL_LIBRARIES = ${OPENSSL_SSL_LIBRARIES}")

# supress MSVC/GCC warnings
if(${CMAKE_HOST_WIN32})
    set(CMAKE_CXX_FLAGS_DEBUG "/MTd /Zi /Ob0 /Od /RTC1")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /wd4251")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /wd4996")
else()
    add_compile_options(-Wno-format-security)
endif()

if(${CMAKE_HOST_WIN32})
    file(
        GLOB_RECURSE
        VOLUMEPROTECTPROTECT_SOURCES
        "src/*.cpp"
        "src/common/*.cpp"
        "src/task/*.cpp"
        "src/native/*.cpp"
        "src/native/win32/*.cpp"
    )
else()
    file(
        GLOB_RECURSE
        VOLUMEPROTECTPROTECT_SOURCES
        "src/*.cpp"
        "src/common/*.cpp"
        "src/task/*.cpp"
        "src/native/*.cpp"
        "src/native/linux/*.cpp"
    )
endif()

file(
    GLOB_RECURSE
    VOLUMEPROTECTPROTECT_HEADERS
    "include/*.h"
    "include/common/*.h"
    "include/task/*.h"
    "include/native/*.h"
)

# message("VOLUMEPROTECTPROTECT_SOURCES = ${VOLUMEPROTECTPROTECT_SOURCES}")
# message("VOLUMEPROTECTPROTECT_HEADERS = ${VOLUMEPROTECTPROTECT_HEADERS}")

include_directories(
    "include"
    "include/common"
    "include/task"
    "include/native"
    "dependency/minijson"
    "dependency/minilogger"
    ${OPENSSL_INCLUDE_DIR}
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

if (${CMAKE_HOST_WIN32})
    message ("resolving 3rd dependencies for windows platform")
    set (VOLUMEPROTECT_LINK_LIBRARIES
        virtDisk
        setupapi
        uuid
        Rpcrt4
        OpenSSL::SSL
        OpenSSL::Crypto
    )
elseif(${CMAKE_HOST_LINUX})
    message ("resolving 3rd dependencies for linux platform")
    set (VOLUMEPROTECT_LINK_LIBRARIES
        uuid
        blkid
        OpenSSL::SSL
        OpenSSL::Crypto
    )
else()
message ("resolving 3rd dependencies for other platform")
    set (VOLUMEPROTECT_LINK_LIBRARIES
        OpenSSL::SSL
        OpenSSL::Crypto
    )
endif()
message("set VOLUMEPROTECT_LINK_LIBRARIES = ${VOLUMEPROTECT_LINK_LIBRARIES}")

# link against dynamic libs
target_link_libraries(
    ${VOLUMEPROTECT_DYNAMIC_LIBRARY_TARGET}
    minijson
    minilogger
    ${VOLUMEPROTECT_LINK_LIBRARIES}
)

# build a static library
message("Build libvolumebackup static library ${VOLUMEPROTECT_STATIC_LIBRARY_TARGET}")
set(VOLUMEPROTECT_STATIC_LIBRARY_TARGET ${Project}_static)
add_library(
    ${VOLUMEPROTECT_STATIC_LIBRARY_TARGET}
    STATIC
    ${VOLUMEPROTECTPROTECT_SOURCES} ${VOLUMEPROTECTPROTECT_HEADERS}
)
set_property(
    TARGET ${VOLUMEPROTECT_STATIC_LIBRARY_TARGET}
    PROPERTY CXX_STANDARD 11
)
# link against static libs
target_link_libraries(
    ${VOLUMEPROTECT_STATIC_LIBRARY_TARGET}
    minijson_static
    minilogger_static
    ${VOLUMEPROTECT_LINK_LIBRARIES}
)

# set -DCMAKE_BUILD_TYPE=Debug to enable LLT, set -DCOVERAGE=ON to enable code coverage
if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    # these config must be put at the level of source code in order to append compile flags
    if ("${COVERAGE}" STREQUAL "lcov" OR "${COVERAGE}" STREQUAL "gcovr")
        message("code coverage is enabled, COVERAGE = ${COVERAGE}, append coverage compiler flags")
        include(${SOURCE_DIR}/test/CodeCoverage.cmake)
        APPEND_COVERAGE_COMPILER_FLAGS()
    endif()

    enable_testing()
    add_subdirectory("test")
endif()

# build executable cli tools
add_subdirectory("cli")

