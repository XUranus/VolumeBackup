cmake_minimum_required(VERSION 3.14)
set(Project "volumebackup_test")
set(${Project} C CXX)

set(Headers)
set(
    Sources
    "VolumeBackupTest.cpp"
    "VolumeMountTest.cpp"
    "CommonUtilTest.cpp"
)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# build test and run code coverage
enable_testing()

# auto fetch googletest
include(FetchContent)
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/03597a01ee50ed33e9dfd640b249b4be3799d395.zip
)

# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(${Project} ${Sources} ${Headers})

target_link_libraries(${Project} PUBLIC
    volumebackup_static
    ${VOLUMEPROTECT_LINK_LIBRARIES}
    GTest::gtest_main
    GTest::gmock_main
)

add_test(
    NAME ${Project}
    COMMAND ${Project}
)

include(GoogleTest)
gtest_discover_tests(${Project})

# configure code coverage
if("${COVERAGE}" STREQUAL "lcov")
    message("code coverage is enabled, using lcov")
    include(CodeCoverage.cmake)
    APPEND_COVERAGE_COMPILER_FLAGS()
    SETUP_TARGET_FOR_COVERAGE_LCOV(
        NAME "volumebackup_coverage_test"
        EXECUTABLE make test
        EXCLUDE
        "/usr/include/*"
        "*googlemock*"
        "*googletest*"
        "${PROJECT_SOURCE_DIR}/build/*"
        "${PROJECT_SOURCE_DIR}/test/*"
        "${PROJECT_SOURCE_DIR}/dependency/*"
        "${PROJECT_SOURCE_DIR}/src/native/*"
        "${PROJECT_SOURCE_DIR}/include/native/*"
        "${PROJECT_SOURCE_DIR}/cli/*"
    )
endif()

if("${COVERAGE}" STREQUAL "gcovr")
message("code coverage is enabled, using gcovr")
    include(CodeCoverage.cmake)
    APPEND_COVERAGE_COMPILER_FLAGS()
    SETUP_TARGET_FOR_COVERAGE_GCOVR_HTML(
        NAME "volumebackup_coverage_test"
        EXECUTABLE make test
        BASE_DIRECTORY "${PROJECT_SOURCE_DIR}"
        EXCLUDE
        "/usr/include/*"
        "*googlemock*"
        "*googletest*"
        "${PROJECT_SOURCE_DIR}/build/*"
        "${PROJECT_SOURCE_DIR}/test/*"
        "${PROJECT_SOURCE_DIR}/dependency/*"
        "${PROJECT_SOURCE_DIR}/src/native/*"
        "${PROJECT_SOURCE_DIR}/include/native/*"
        "${PROJECT_SOURCE_DIR}/cli/*"
    )
endif()
