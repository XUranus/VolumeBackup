add_executable (vbackup
    "vbackup.cpp"
    "GetOption.cpp"
)

add_executable (vshow
    "vshow.cpp"
    "GetOption.cpp"
)

add_executable (vchecksum
    "vchecksum.cpp"
    "GetOption.cpp"
)

set_property(TARGET vbackup PROPERTY CXX_STANDARD 11)
set_property(TARGET vshow PROPERTY CXX_STANDARD 11)

# link vbackup executable
if(${CMAKE_HOST_WIN32})
    target_link_libraries(
        vbackup
        volumebackup_static
        # third part dependency provided by XUranus
        minijson_static
        minilogger_static
    )
else()
    # require linux-utils
    target_link_libraries(
        vbackup
        volumebackup
        pthread
        # third part dependency provided by XUranus
        minijson_static
        minilogger_static
    )
endif()

# build vshow executable
target_link_libraries(
    vshow
)

# build vcopymount executable
add_executable (vcopymount
    "vcopymount.cpp"
    "GetOption.cpp"
)

# build vchecksum executable
target_link_libraries(
    vchecksum
    OpenSSL::SSL
    OpenSSL::Crypto
)


set_property(TARGET vcopymount PROPERTY CXX_STANDARD 11)

target_link_libraries(
    vcopymount
    volumebackup_static
    # third part dependency provided by XUranus
    minijson_static
    minilogger_static
)