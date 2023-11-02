/**
 * @copyright Copyright 2023 XUranus. All rights reserved.
 * @license This project is released under the Apache License.
 * @author XUranus(2257238649wdx@gmail.com)
 */

#ifndef XURANUS_CPPUTILS_GETOPTION_HEADER
#define XURANUS_CPPUTILS_GETOPTION_HEADER

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