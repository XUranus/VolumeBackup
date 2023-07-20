CMAKE_MINIMUM_REQUIRED (VERSION 3.8)
SET(Project volumeprotect)
PROJECT(${Project} C CXX)
SET(SOURCE_DIR ${${Project}_SOURCE_DIR})

ADD_SUBDIRECTORY(module/minijson)
ADD_SUBDIRECTORY(module/minilogger)

IF(${CMAKE_HOST_WIN32})
    SET(CMAKE_CXX_FLAGS_DEBUG "/MTd /Zi /Ob0 /Od /RTC1")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /wd4251")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /wd4996")
ENDIF()

FILE(GLOB_RECURSE VOLUMEPROTECTPROTECT_SOURCES "src/*.cpp")
FILE(GLOB_RECURSE VOLUMEPROTECTPROTECT_HEADERS "src/*.h")

INCLUDE_DIRECTORIES(
    "include"
    "module/minijson"
    "module/minilogger"
)

SET(VOLUMEPROTECT_DYNAMIC_LIBRARY_TARGET ${Project})
SET(VOLUMEPROTECT_STATIC_LIBRARY_TARGET ${Project}_static)

MESSAGE("Build volumeprotect dynamic library ${VOLUMEPROTECT_DYNAMIC_LIBRARY_TARGET}")
MESSAGE("Build volumeprotect static library ${VOLUMEPROTECT_STATIC_LIBRARY_TARGET}")
ADD_LIBRARY(${VOLUMEPROTECT_DYNAMIC_LIBRARY_TARGET} SHARED ${VOLUMEPROTECTPROTECT_SOURCES} ${VOLUMEPROTECTPROTECT_HEADERS})
ADD_LIBRARY(${VOLUMEPROTECT_STATIC_LIBRARY_TARGET}  STATIC ${VOLUMEPROTECTPROTECT_SOURCES} ${VOLUMEPROTECTPROTECT_HEADERS})

SET_PROPERTY(TARGET ${VOLUMEPROTECT_DYNAMIC_LIBRARY_TARGET} PROPERTY CXX_STANDARD 17)
SET_PROPERTY(TARGET ${VOLUMEPROTECT_STATIC_LIBRARY_TARGET} PROPERTY CXX_STANDARD 17)

TARGET_LINK_LIBRARIES(
    ${VOLUMEPROTECT_DYNAMIC_LIBRARY_TARGET}
    minijson
    minilogger
)

TARGET_LINK_LIBRARIES(
    ${VOLUMEPROTECT_STATIC_LIBRARY_TARGET}
    minijson_static
    minilogger_static
)

# to generate export library when build dynamic library
TARGET_COMPILE_DEFINITIONS(${VOLUMEPROTECT_DYNAMIC_LIBRARY_TARGET} PRIVATE -DLIBRARY_EXPORT)

MESSAGE("CMAKE_BUILD_TYPE is ${CMAKE_BUILD_TYPE}")
MESSAGE("SOURCE_DIR is ${SOURCE_DIR}")

# build test and run code coverage
IF (CMAKE_BUILD_TYPE STREQUAL "Debug" OR CMAKE_BUILD_TYPE STREQUAL "Coverage")

    ENABLE_TESTING()
    ADD_SUBDIRECTORY("test")

    IF (CMAKE_BUILD_TYPE STREQUAL "Coverage")

        INCLUDE(${SOURCE_DIR}/test/CodeCoverage.cmake)
        APPEND_COVERAGE_COMPILER_FLAGS()
        SETUP_TARGET_FOR_COVERAGE_LCOV(
            NAME "volumeprotect_lcov_test"
            EXECUTABLE make test
            EXCLUDE "${PROJECT_SOURCE_DIR}/test/*" "/usr/include/*" "*googlemock*" "*googletest*"
        )
    
        SETUP_TARGET_FOR_COVERAGE_GCOVR_HTML(
            NAME "volumeprotect_gcovr_test"
            EXECUTABLE make test
            BASE_DIRECTORY "${PROJECT_SOURCE_DIR}"
            EXCLUDE "${PROJECT_SOURCE_DIR}/test/*" "/usr/include/*" "*googlemock*" "*googletest*" "*build*"
        )
    ENDIF()
ENDIF()


ADD_EXECUTABLE (vbkup
    "vbkup.cpp"
    "GetOption.cpp"
)

ADD_EXECUTABLE (voltool
    "voltool.cpp"
    "GetOption.cpp"
)

IF (CMAKE_VERSION VERSION_GREATER 3.12)
    SET_PROPERTY(TARGET vbkup PROPERTY CXX_STANDARD 17)
    SET_PROPERTY(TARGET voltool PROPERTY CXX_STANDARD 17)
ENDIF()

# build vbkup executable
IF(${CMAKE_HOST_WIN32})
    TARGET_LINK_LIBRARIES(
        vbkup
        volumeprotect_static
        # third part dependency provided by XUranus
        minijson_static
        minilogger_static
    )
ELSE()
    # require linux-utils
    TARGET_LINK_LIBRARIES(
        vbkup
        volumeprotect
        # openssl
        crypto
        pthread
        # third part dependency provided by XUranus
        minijson_static
        minilogger_static
    )
ENDIF()

# build voltool executable
TARGET_LINK_LIBRARIES(
    voltool
)