    /**
 * @file VolumeProtectMacros.h
 * @brief This file is used as a PCH, and define some utils missing in STL of CXX11.
 * @copyright Copyright 2023-2024 XUranus. All rights reserved.
 * @license This project is released under the Apache License.
 * @author XUranus(2257238649wdx@gmail.com)
 */

#ifndef VOLUMEBACKUP_PROTECT_MACROS_HEADER
#define VOLUMEBACKUP_PROTECT_MACROS_HEADER

// function as stdafx.h, include common used STL headers here
#include <string>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <thread>
#include <fstream>
#include <vector>
#include <atomic>
#include <cstdio>
#include <exception>
#include <stdexcept>
#include <iostream>
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <memory>
#include <queue>
#include <map>
#include <unordered_map>

using ErrCodeType = int;

 /*
 * @brief
 * add -DLIBRARY_EXPORT build param to export lib on Win32 MSVC
 * define LIBRARY_IMPORT before including VolumeProtector.h to add __declspec(dllimport) to use dll library
 * libvolumeprotect is linked static by default
 */

// define library export macro
#ifdef _WIN32
    #ifdef LIBRARY_EXPORT
        #define VOLUMEPROTECT_API __declspec(dllexport)
    #else
        #ifdef LIBRARY_IMPORT
            #define VOLUMEPROTECT_API __declspec(dllimport)
        #else
            #define VOLUMEPROTECT_API
        #endif
    #endif
#else
    #define VOLUMEPROTECT_API  __attribute__((__visibility__("default")))
#endif

// macro POSIXAPI used for Linux/AIX/Solaris/MacOSX, these platform use posix api
#if defined(__linux__) || defined(__APPLE__) || defined(__sun__) || defined(_AIX)
#ifndef POSIXAPI
#define POSIXAPI
#endif
#endif

#if defined(POSIXAPI) && defined(_WIN32)
static_assert(false, "both windows platform macro _WIN32 and posix platform macro POSIXAPI defined!")
#endif

#if !defined(POSIXAPI) && !defined(_WIN32)
static_assert(false, "none of windows platform macro _WIN32 or posix platform macro POSIXAPI defined"!)
#endif

// check if make_unique defined
#ifndef __cpp_lib_make_unique
/**
 * @brief define extended std function
 */
namespace exstd {
template<typename T, class... Args>
std::unique_ptr<T> make_unique(Args&&... args)
{
    return std::unique_ptr<T>(new T(std::forward<Args>(args)...));
}
}
#else
namespace exstd = std;
#endif

/**
 * @brief extended smart pointer module
 */
namespace mem {
template<typename TO, typename FROM>
std::unique_ptr<TO> static_unique_pointer_cast(std::unique_ptr<FROM>&& old)
{
	return std::unique_ptr<TO>(static_cast<TO*>(old.release()));
}
}

#endif