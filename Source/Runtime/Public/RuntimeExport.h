#pragma once

#if defined(WONENGINE_SHARED_RUNTIME)
    #if defined(_WIN32)
        #if defined(WONENGINE_EXPORTS)
            #define WONENGINE_API __declspec(dllexport)
        #else
            #define WONENGINE_API __declspec(dllimport)
        #endif
    #else
        #define WONENGINE_API
    #endif
#else
    #define WONENGINE_API
#endif
