#pragma once

#include "RuntimeExport.h"
#include "Types.h"

#include <string>

namespace won::profiler
{
    using range_id = Size;

    WONENGINE_API void BeginFrame();
    WONENGINE_API void EndFrame();

    WONENGINE_API range_id BeginRangeCPU(const std::string& name);
    WONENGINE_API range_id BeginRangeGPU(const std::string& name);
    WONENGINE_API void EndRange(range_id id);

    struct WONENGINE_API ScopedRangeCPU
    {
        range_id id = 0;
        explicit ScopedRangeCPU(const char* name);
        ~ScopedRangeCPU();
    };

    struct WONENGINE_API ScopedRangeGPU
    {
        range_id id = 0;
        explicit ScopedRangeGPU(const char* name);
        ~ScopedRangeGPU();
    };

    WONENGINE_API void SetEnabled(bool enabled);
    WONENGINE_API bool IsEnabled();
    WONENGINE_API void GetProfileInfo(std::string& performance_profile, std::string& resource_profile);
}
