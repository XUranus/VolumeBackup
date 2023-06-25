CMAKE_MINIMUM_REQUIRED (VERSION 3.8)
PROJECT ("vbkup")

ADD_SUBDIRECTORY(module/minijson)
ADD_SUBDIRECTORY(module/minilogger)
FILE(GLOB_RECURSE VOLUME_BACKUP_SOURCE "src/*.cpp")

INCLUDE_DIRECTORIES(
    "include"
    "module/minijson"
    "module/minilogger"
)

ADD_EXECUTABLE (vbkup
    ${VOLUME_BACKUP_SOURCE}
    "main.cpp"
)

IF (CMAKE_VERSION VERSION_GREATER 3.12)
    SET_PROPERTY(TARGET vbkup PROPERTY CXX_STANDARD 17)
ENDIF()


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
        # libblkid libparted openssl
        blkid
        parted
        crypto
        # third part dependency provided by XUranus
        minijson
        minilogger
    )

ENDIF()
