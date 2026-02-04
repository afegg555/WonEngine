#pragma once

#ifdef _WIN32

#ifndef NOMINMAX
#define NOMINMAX
#endif // NOMINMAX
#include <windows.h>

#endif

namespace won::platform
{
#ifdef _WIN32
    using WindowType = HWND;
    using ErrorType = HRESULT;
#endif // _WIN32

    enum class PlatformType
    {
        Unknown,
        Windows,
        Linux,
        MacOS
    };

    struct PlatformInfo
    {
        PlatformType type = PlatformType::Unknown;
        bool is_64bit = true;
    };

    inline static constexpr PlatformInfo GetInfo()
    {
        PlatformInfo info;
        info.type = GetType();
        info.is_64bit = sizeof(void*) == 8;
        return info;
    }

    inline static constexpr PlatformType GetType()
    {
#if defined(_WIN32)
        return PlatformType::Windows;
#elif defined(__APPLE__)
        return PlatformType::MacOS;
#elif defined(__linux__)
        return PlatformType::Linux;
#else
        return PlatformType::Unknown;
#endif
    }

    inline static constexpr const char* GetName()
    {
        switch (GetType())
        {
        case PlatformType::Windows: return "Windows";
        case PlatformType::Linux: return "Linux";
        case PlatformType::MacOS: return "MacOS";
        default: return "Unknown";
        }
    }

    inline static constexpr bool IsWindows()
    {
        return GetType() == PlatformType::Windows;
    }
}
