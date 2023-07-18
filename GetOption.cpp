/*================================================================
*   Copyright (C) 2023 XUranus All rights reserved.
*   
*   File:         GetOption.cpp
*   Author:       XUranus
*   Date:         2023-06-29
*   Description:  
*
================================================================*/

#include "GetOption.h"

#include <string>
#include <vector>
#include <map>
#include <set>

#include <iostream>

namespace {
    const char COLON = ':';
    const std::string LONG_OPTION_PREFIX = "--";
    const std::string SHORT_OPTION_PREFIX = "-";
}

using namespace xuranus::getopt;

inline bool IsAlphabet(char ch)
{
    return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z');
}

OptionResult::OptionResult(const std::string& optionName, const std::string& optionValue)
 : option(optionName), value(optionValue) {}

/**
 * @brief parse short option to tuple array
 * 
 * @param optionStr 
 * @return std::map<char, bool> represent <option, has arg>
 * @example "v:ha:" => [{ 'v' => true }, { 'h' => false }, { 'a' => true }]
 */
static std::map<char, bool> ParseShortOption(const std::string& str)
{
    std::map<char, bool> optionMap;
    for (std::string::const_iterator it = str.begin(); it != str.end(); ++it) {
        if (!IsAlphabet(*it)) {
            continue;
        }
        if (it + 1 != str.end() && *(it + 1) == COLON) {
            optionMap.emplace(*it, true);
            ++it;
            continue;
        }
        optionMap.emplace(*it, false);
    }
    return optionMap;
}

/**
 * @brief parse long option to tuple array
 * 
 * @param optionStr 
 * @return std::map<std::string, bool> represent <option, has arg>
 * @example { "--prev=", "--verbose" } => [{ "prev" => true }, { "verbose" => false }]
 */
static std::map<std::string, bool> ParseLongOption(const std::set<std::string>& longOptionSet)
{
    std::map<std::string, bool> optionMap;
    for (std::string option : longOptionSet) {
    	while (!option.empty() && option.front() == '-') {
    		option = option.substr(1);
    	}
        bool hasArg = false;
    	if (!option.empty() && option.back() == '=') {
    		option.pop_back();
            hasArg = true;
    	}
    	optionMap.emplace(option, hasArg);
    }
    return optionMap;
}

static std::string ParseLongOptionNameFromArg(const std::string& arg)
{
    if (arg.length() <= LONG_OPTION_PREFIX.length() ||
        arg.find(LONG_OPTION_PREFIX) != 0 ||
        arg.find_first_not_of('-') != 2) {
        return "";
    }
    auto pos = arg.find("=");
    if (pos == std::string::npos) {
        return arg.substr(2);
    }
    return arg.substr(2, pos - 2);
}

static std::string ParseLongOptionValueFromArg(const std::string& arg)
{
    auto pos = arg.find_last_of('=');
    if (pos == std::string::npos || pos == arg.length() - 1) {
        return "";
    }
    return arg.substr(pos + 1);
}

GetOptionResult xuranus::getopt::GetOption(
    const char** argBegin,
    int argc,
    const std::string& shortOptionStr,
    const std::set<std::string>& longOptionSet)
{
    GetOptionResult result;
    std::map<char, bool> shortOptionMap = ParseShortOption(shortOptionStr);
    std::map<std::string, bool> longOptionMap = ParseLongOption(longOptionSet);

    std::vector<std::string> args(argBegin, argBegin + argc);
    for (std::vector<std::string>::const_iterator it = args.begin(); it != args.end(); ++it) {
        if (it->find(LONG_OPTION_PREFIX) == 0) { // start with "--", maybe long option
            std::string optionName = ParseLongOptionNameFromArg(*it);
            auto entry = longOptionMap.find(optionName);
            if (optionName.empty() || entry == longOptionMap.end()) {
                result.args.emplace_back(*it);
                continue;
            }
            bool hasArg = entry->second;
            if (!hasArg) {
                result.opts.emplace_back(optionName, "");
                continue;
            }
            std::string optionValue = ParseLongOptionValueFromArg(*it);
            result.opts.emplace_back(optionName, optionValue);
            continue;
        } else if (it->find(SHORT_OPTION_PREFIX) == 0 && it->length() >= 2) { // start with "-", maybe short option
            char optionChar = it->at(1);
            std::string optionValue = it->substr(2);
            if (shortOptionMap.find(optionChar) == shortOptionMap.end()) {
                result.args.emplace_back(*it);
                continue;
            }
            bool hasArg = shortOptionMap[optionChar];
            if (!hasArg) {
                result.opts.emplace_back(std::string(1, optionChar), "");
                continue;
            }
            if (optionValue.empty() && it + 1 != args.end()) {
                optionValue = *(++it);
            }
            result.opts.emplace_back(std::string(1, optionChar), optionValue);
            continue;
        } else {
            result.args.emplace_back(*it);
            continue;
        }
    }
    return result;
}