#include <cassert>

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

// check platform macro conflict
#ifdef __linux__
#ifdef _WIN32
static_assert(false, "conflict macro, both __linux__ and _WIN32 defined!");
#endif
#endif

// check if any of the platform macro defined
#ifndef __linux__
#ifndef _WIN32
static_assert(false, "platform unsupported, none of __linux__ and _WIN32 defined!");
#endif
#endif
