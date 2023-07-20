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