#pragma once
#include "RuntimeExport.h"
#include "Types.h"

#ifdef _WIN32

#ifndef NOMINMAX
#define NOMINMAX
#endif // NOMINMAX
#include <windows.h>
#include <psapi.h>

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

    struct ProcessMemoryUsage
    {
        uint64 committed_bytes = 0;
        uint64 working_set_bytes = 0;
    };

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

    inline static constexpr PlatformInfo GetInfo()
    {
        PlatformInfo info;
        info.type = GetType();
        info.is_64bit = sizeof(void*) == 8;
        return info;
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

    inline bool GetProcessMemoryUsage(ProcessMemoryUsage& out_usage)
    {
        out_usage = {};

#if defined(_WIN32)
        PROCESS_MEMORY_COUNTERS_EX process_memory = {};
        process_memory.cb = sizeof(process_memory);
        if (!K32GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&process_memory), sizeof(process_memory)))
        {
            return false;
        }

        out_usage.committed_bytes = static_cast<uint64>(process_memory.PrivateUsage);
        out_usage.working_set_bytes = static_cast<uint64>(process_memory.WorkingSetSize);
        return true;
#else
        return false;
#endif
    }
}
