CMAKE_MINIMUM_REQUIRED (VERSION 3.8)
PROJECT("VolumeProtect")
ADD_SUBDIRECTORY(module/minijson)
ADD_SUBDIRECTORY(module/minilogger)
FILE(GLOB_RECURSE VOLUME_BACKUP_SOURCE "src/*.cpp")

IF(${CMAKE_HOST_WIN32})
    SET(CMAKE_CXX_FLAGS_DEBUG "/MTd /Zi /Ob0 /Od /RTC1")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /wd4251")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /wd4996")
ENDIF()

INCLUDE_DIRECTORIES(
    "include"
    "module/minijson"
    "module/minilogger"
)

ADD_EXECUTABLE (vbkup
    ${VOLUME_BACKUP_SOURCE}
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
        # third part dependency provided by XUranus
        minijson_static
        minilogger_static
    )
ELSE()
    # require linux-utils
    TARGET_LINK_LIBRARIES(
        vbkup
        # openssl
        crypto
        pthread
        # third part dependency provided by XUranus
        minijson
        minilogger
    )
ENDIF()

# build voltool executable
TARGET_LINK_LIBRARIES(
    voltool
    # third part dependency provided by XUranus
    minijson_static
    minilogger_static
)
