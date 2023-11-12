#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "common/VolumeUtils.h"

using namespace ::testing;
using namespace volumeprotect;
using namespace volumeprotect::common;

TEST(CommonUtilTest, PathJoinTest)
{
#ifdef _WIN32
    EXPECT_EQ(
        common::PathJoin(R"(C:\Windows\System32)", "etc", "hosts"),
        R"(C:\Windows\System32\etc\hosts)");
#else
    EXPECT_EQ(
        common::PathJoin("/home/xuranus", "Desktop"),
        "/home/xuranus/Desktop");
#endif
}

TEST(CommonUtilTest, GetFileNameTest)
{
    EXPECT_EQ(common::GetFileName("/home/xuranus/file"), "file");
    EXPECT_EQ(common::GetFileName(R"(C:\Windows\System32\zip.dll)"), "zip.dll");
}