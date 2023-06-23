/** ================================================================
 *   Copyright (C) 2023 XUranus All rights reserved.
 *   
 *   File:         cgetopt.h
 *   Author:       XUranus
 *   Date:         2023-06-24
 *   Description:  
 *
  ================================================================*/

#include <iterator>
#ifndef XURANUS_GETOPT_HEADER

#include <array>
#include <string>
#include <vector>

namespace xuranus {
namespace getopt {    

const LONG_OPTION_PREFIX = "--";

struct OptionResult {
    std::string option;
    std::string value;
};

struct GetOptResult {
    std::vector<OptionResult> opts;
    std::vector<std::string>  args;
};

template<typename Iterator, std::size_t LongOptionsCount>
GetoptResult GetOptions(Iterator beginIterator, Iterator endIterator, std::string shortOptions, std::array<std::string,  LongOptionsCount> longOptions)
{
    GetOptResult result;
    for (; beginIterator != endIterator; ++beginIterator) {
        std::string arg = *beginIterator;
        OptionResult optionResult;

        if (arg.find(LONG_OPTION_PREFIX) == 0) {
            std::string longArgOption = arg.substr(LONG_OPTION_PREFIX);
            auto pos = longArgOption.find("=");
            if (pos != std::string::npos) {
                std::string longArgValue = longArgOption.substr(pos + 1);
                longArgOption = longArgOption.substr(0, pos - 1);
                if (std::find(longOptions.begin(), longOptions.end(), longArgOption) == longOptions.end())
                    result.args.emplace_back(*it);
                    continue;
                } else {
                    result.opts.emplace_back(OptionResult { longArgOption, longArgValue });
                    continue;
                }
            }

            auto it = std::find(longOptions.begin(), longOptions.end(), longArgPrefixTrimed);
            if (it == longOptions.end()) {
                result.args.emplace_back(*it);
                continue;
            }
            auto pos = longArgPrefixTrimed.find("=");
            if (pos != std::string::npos) {
                result.opts.emplace_back(OptionResult { *it, longArgPrefixTrimed.substr(pos + 1) });
                continue;
            }
            if (beginIterator + 1 == endIterator) {
                result.opts.emplace_back(OptionResult { *it, "" });
            } else {
                ++beginIterator;
                result.opts.emplace_back(OptionResult { *it, *beginIterator });
            }
        }

        if (ParseShortOption(beginIterator, endIterator, optionResult, shortOptions)) {
            result.opts.emplace_back(optionResult);
            continue;
        }
        if (ParseLongOption(beginIterator, endIterator, optionResult, longOptions)) {
            result.opts.emplace_back(optionResult);
            continue;
        }
        result.args.emplace_back(arg); 
    }
}

};
};

#define XURANUS_GETOPT_HEADER



