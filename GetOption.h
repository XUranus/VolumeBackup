/*
 * ================================================================
 *   Copyright (C) 2023 XUranus All rights reserved.
 *
 *   File:         GetOption.h
 *   Author:       XUranus
 *   Date:         2023-06-29
 *   Description:
 * ==================================================================
 */

#ifndef _XURANUS_CPPUTILS_GETOPTION_HEADER_
#define _XURANUS_CPPUTILS_GETOPTION_HEADER_

#include <array>
#include <string>
#include <set>
#include <vector>

namespace xuranus {
namespace getopt {

struct OptionResult {
    std::string option;
    std::string value;

    OptionResult(const std::string&, const std::string&);
};

struct GetOptionResult {
    std::vector<OptionResult> opts;
    std::vector<std::string>  args;
};

GetOptionResult GetOption(
    const char** argBegin,
    int argc,
    const std::string& shortOptionStr,
    const std::set<std::string>& longOptionSet
);

}
}

#endif