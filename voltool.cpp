/*
 * ================================================================
 *   Copyright (C) 2023 XUranus All rights reserved.
 *   
 *   File:         voltool.cpp
 *   Author:       XUranus
 *   Date:         2023-07-01
 *   Description:  a command line tool to display volume info
 * ==================================================================
 */


#include "GetOption.h"
#include "VolumeUtils.h"

using namespace volumeprotect;
using namespace xuranus::getopt;

int main(int argc, const char** argv)
{
    GetOptionResult result = GetOption(argv + 1, argc - 1, "v:d:m:p:h:r:l", {});

    return 0;
}