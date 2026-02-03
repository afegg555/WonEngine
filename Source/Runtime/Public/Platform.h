#pragma once

namespace won::platform
{
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

    class Platform
    {
    public:
        static constexpr PlatformInfo GetInfo()
        {
            PlatformInfo info;
            info.type = GetType();
            info.is_64bit = sizeof(void*) == 8;
            return info;
        }

        static constexpr PlatformType GetType()
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

        static constexpr const char* GetName()
        {
            switch (GetType())
            {
            case PlatformType::Windows: return "Windows";
            case PlatformType::Linux: return "Linux";
            case PlatformType::MacOS: return "MacOS";
            default: return "Unknown";
            }
        }

        static constexpr bool IsWindows()
        {
            return GetType() == PlatformType::Windows;
        }
    };
}
